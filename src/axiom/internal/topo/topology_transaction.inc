namespace {

// Every face edit dirties the owning bodies and their Eval consumers. The body
// undo record also restores analytic eligibility on rollback and savepoints.
void invalidate_body_for_face_edit(detail::KernelState& state,
                                  detail::TopologyTransactionState& transaction,
                                  FaceId face_id) {
  for (auto &[body_value, body] : state.bodies) {
    const bool owns_face = std::any_of(body.shells.begin(), body.shells.end(),
        [&](ShellId shell_id) {
          const auto shell = state.shells.find(shell_id.value);
          return shell != state.shells.end() &&
              std::any_of(shell->second.faces.begin(), shell->second.faces.end(),
                  [face_id](FaceId current) { return current.value == face_id.value; });
        });
    if (owns_face) {
      transaction.snapshot_body(state, BodyId{body_value});
      body.analytic_mass_valid = false;
      body.primitive_tessellation_valid = false;
      detail::invalidate_eval_for_body(state, BodyId{body_value});
    }
  }
}

// A discarded body cannot remain the owner of runtime meshes or Eval bindings.
// Run after restoration (or commit), so temporarily deleted bodies retain their
// associations when rollback restores them. This only manages Core handle stores.
void discard_removed_body_runtime(detail::KernelState& state) {
  for (auto it = state.eval_body_bindings.begin(); it != state.eval_body_bindings.end();) {
    if (state.bodies.contains(it->first)) {
      ++it;
    } else {
      detail::invalidate_eval_for_body(state, BodyId{it->first});
      it = state.eval_body_bindings.erase(it);
    }
  }
  for (auto it = state.meshes.begin(); it != state.meshes.end();) {
    if (it->second.source_body.value != 0 && !state.bodies.contains(it->second.source_body.value))
      it = state.meshes.erase(it);
    else
      ++it;
  }
  for (auto it = state.tessellation_cache.begin(); it != state.tessellation_cache.end();) {
    if (!state.meshes.contains(it->second.value)) it = state.tessellation_cache.erase(it);
    else ++it;
  }
  for (auto it = state.face_tessellation_cache.begin(); it != state.face_tessellation_cache.end();) {
    if (!state.meshes.contains(it->second.value)) it = state.face_tessellation_cache.erase(it);
    else ++it;
  }
}

}  // namespace

TopologyTransaction::TopologyTransaction(
    std::shared_ptr<detail::KernelState> state)
    : TopologyTransaction(std::move(state), TopologyCancellationToken{}) {}

TopologyTransaction::TopologyTransaction(
    std::shared_ptr<detail::KernelState> state,
    TopologyCancellationToken cancellation_token)
    : state_(std::move(state)),
      transaction_state_(std::make_shared<detail::TopologyTransactionState>()),
      cancellation_token_(std::move(cancellation_token)) {
  if (token_cancellation_requested()) {
    record_cancellation_observed(false, 0);
    active_ = false;
    return;
  }
  if (!state_->active_topology_transaction.expired()) {
    active_ = false;
    return;
  }
  state_->active_topology_transaction = transaction_state_;
}

TopologyTransaction::TopologyTransaction(TopologyTransaction&& other)
    : state_(other.state_) {
  // Allocate the moved-from sentinel before transferring any undo state, so a
  // failed allocation leaves the source transaction untouched.
  auto closed_state = std::make_shared<detail::TopologyTransactionState>();
  transaction_state_ = std::move(other.transaction_state_);
  cancellation_token_ = std::move(other.cancellation_token_);
  created_vertices_ = std::move(other.created_vertices_);
  created_edges_ = std::move(other.created_edges_);
  created_coedges_ = std::move(other.created_coedges_);
  created_loops_ = std::move(other.created_loops_);
  created_faces_ = std::move(other.created_faces_);
  created_shells_ = std::move(other.created_shells_);
  created_bodies_ = std::move(other.created_bodies_);
  txn_deleted_faces_ = other.txn_deleted_faces_;
  txn_deleted_shells_ = other.txn_deleted_shells_;
  txn_deleted_bodies_ = other.txn_deleted_bodies_;
  txn_replaced_surfaces_ = other.txn_replaced_surfaces_;
  txn_coedge_pcurve_binds_ = other.txn_coedge_pcurve_binds_;
  txn_coedge_pcurve_clears_ = other.txn_coedge_pcurve_clears_;
  txn_write_ops_ = other.txn_write_ops_;
  cancelled_write_ops_ = other.cancelled_write_ops_;
  cancellation_observed_ = other.cancellation_observed_;
  active_ = other.active_;
  other.transaction_state_ = std::move(closed_state);
  other.txn_deleted_faces_ = 0;
  other.txn_deleted_shells_ = 0;
  other.txn_deleted_bodies_ = 0;
  other.txn_replaced_surfaces_ = 0;
  other.txn_coedge_pcurve_binds_ = 0;
  other.txn_coedge_pcurve_clears_ = 0;
  other.txn_write_ops_ = 0;
  other.cancelled_write_ops_ = 0;
  other.cancellation_observed_ = false;
  other.active_ = false;
}

TopologyTransaction::~TopologyTransaction() noexcept {
  if (!active_) {
    return;
  }
  if (txn_write_ops_ == 0) {
    if (token_cancellation_requested()) {
      record_cancellation_observed(true, 0);
    }
    if (const auto owner = state_->active_topology_transaction.lock();
        owner.get() == transaction_state_.get()) {
      state_->active_topology_transaction.reset();
    }
    active_ = false;
    return;
  }
  try {
    (void)rollback();
  } catch (...) {
    // Destructors must not propagate. rollback marks the transaction closed
    // only after restoring the model, so ordinary scope exit follows the same
    // consistency path as an explicit rollback.
  }
}

Result<VertexId> TopologyTransaction::create_vertex(const Point3 &point) {
  if (const auto cancelled = observe_cancellation("创建顶点")) {
    return error_result<VertexId>(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_) {
    return detail::failed_result<VertexId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "创建顶点失败：事务已关闭", "创建顶点失败");
  }
  if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
      !std::isfinite(point.z)) {
    return detail::invalid_input_result<VertexId>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "创建顶点失败：顶点坐标必须为有限值", "创建顶点失败");
  }
  const auto id = VertexId{state_->allocate_id()};
  state_->vertices.emplace(id.value, detail::VertexRecord{point});
  created_vertices_.push_back(id.value);
  ++txn_write_ops_;
  return ok_result(id, state_->create_diagnostic("已创建顶点"));
}

Result<EdgeId> TopologyTransaction::create_edge(CurveId curve_id, VertexId v0,
                                                VertexId v1) {
  if (const auto cancelled = observe_cancellation("创建边")) {
    return error_result<EdgeId>(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_ || !detail::has_curve(*state_, curve_id) ||
      !detail::has_vertex(*state_, v0) || !detail::has_vertex(*state_, v1) ||
      v0.value == v1.value) {
    return detail::invalid_input_result<EdgeId>(
        *state_, diag_codes::kCoreInvalidHandle,
        "创建边失败：事务已关闭、目标曲线不存在、端点不存在或两个端点重合",
        "创建边失败");
  }
  const auto id = EdgeId{state_->allocate_id()};
  state_->edges.emplace(id.value, detail::EdgeRecord{curve_id, v0, v1});
  created_edges_.push_back(id.value);
  ++txn_write_ops_;
  return ok_result(id, state_->create_diagnostic("已创建边"));
}

Result<EdgeId> TopologyTransaction::create_trimmed_edge(
    CurveId curve_id, Scalar start_parameter, Scalar end_parameter,
    VertexId v0, VertexId v1) {
  if (const auto cancelled = observe_cancellation("创建裁剪边")) {
    return error_result<EdgeId>(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_ || !detail::has_curve(*state_, curve_id) ||
      !detail::has_vertex(*state_, v0) || !detail::has_vertex(*state_, v1) ||
      v0.value == v1.value) {
    return detail::invalid_input_result<EdgeId>(
        *state_, diag_codes::kCoreInvalidHandle,
        "创建裁剪边失败：事务已关闭、目标曲线不存在、端点不存在或两个端点重合",
        "创建裁剪边失败");
  }
  if (!std::isfinite(start_parameter) || !std::isfinite(end_parameter) ||
      start_parameter == end_parameter) {
    return detail::invalid_input_result<EdgeId>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "创建裁剪边失败：裁剪参数必须为有限且不同的值", "创建裁剪边失败");
  }
  const CurveService curve_service(state_);
  const auto domain = curve_service.domain(curve_id);
  if (!domain.value || start_parameter < domain.value->min ||
      start_parameter > domain.value->max ||
      end_parameter < domain.value->min || end_parameter > domain.value->max) {
    return detail::invalid_input_result<EdgeId>(
        *state_, diag_codes::kGeoParameterOutOfDomain,
        "创建裁剪边失败：裁剪参数超出支撑曲线定义域", "创建裁剪边失败");
  }
  Point3 start_point{};
  Point3 end_point{};
  const auto evaluated_start =
      curve_service.point_at_parameter(curve_id, start_parameter);
  const auto evaluated_end =
      curve_service.point_at_parameter(curve_id, end_parameter);
  if (!evaluated_start.value || !evaluated_end.value) {
    return detail::failed_result<EdgeId>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoCurveTopologyMismatch,
        "创建裁剪边失败：无法在裁剪参数处求值支撑曲线", "创建裁剪边失败",
        {curve_id.value, v0.value, v1.value});
  }
  start_point = *evaluated_start.value;
  end_point = *evaluated_end.value;
  const auto distance = [](const Point3 &a, const Point3 &b) {
    return std::hypot(static_cast<long double>(a.x) - b.x,
                      static_cast<long double>(a.y) - b.y,
                      static_cast<long double>(a.z) - b.z);
  };
  const auto tolerance = detail::resolve_linear_tolerance(
      0.0, state_->config.tolerance);
  const auto d0 = distance(start_point, state_->vertices.at(v0.value).point);
  const auto d1 = distance(end_point, state_->vertices.at(v1.value).point);
  if (!std::isfinite(d0) || !std::isfinite(d1) || d0 > tolerance ||
      d1 > tolerance) {
    const auto evidence_distance = [](long double value) {
      return std::isfinite(value) &&
                     value <= std::numeric_limits<Scalar>::max()
                 ? static_cast<Scalar>(value)
                 : std::numeric_limits<Scalar>::max();
    };
    auto issue = detail::make_error_issue(
        diag_codes::kTopoCurveTopologyMismatch,
        "创建裁剪边失败：裁剪区间端点与拓扑顶点不一致",
        {curve_id.value, v0.value, v1.value});
    issue.numeric_evidence = {
        {"start_distance", evidence_distance(d0), "model_unit"},
        {"end_distance", evidence_distance(d1), "model_unit"},
        {"tolerance", tolerance, "model_unit"}};
    return error_result<EdgeId>(
        StatusCode::InvalidTopology,
        state_->create_diagnostic("创建裁剪边失败", {std::move(issue)}));
  }
  const auto id = EdgeId{state_->allocate_id()};
  state_->edges.emplace(
      id.value, detail::EdgeRecord{curve_id, v0, v1, true,
                                   start_parameter, end_parameter});
  created_edges_.push_back(id.value);
  ++txn_write_ops_;
  return ok_result(id, state_->create_diagnostic("已创建显式曲线裁剪边"));
}

Result<CoedgeId> TopologyTransaction::create_coedge(EdgeId edge_id,
                                                    bool reversed) {
  if (const auto cancelled = observe_cancellation("创建定向边")) {
    return error_result<CoedgeId>(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_ || state_->edges.find(edge_id.value) == state_->edges.end()) {
    return detail::invalid_input_result<CoedgeId>(
        *state_, diag_codes::kCoreInvalidHandle,
        "创建定向边失败：事务已关闭或目标边不存在", "创建定向边失败");
  }
  const auto id = CoedgeId{state_->allocate_id()};
  state_->coedges.emplace(id.value, detail::CoedgeRecord{edge_id, reversed, {}});
  created_coedges_.push_back(id.value);
  rebuild_topology_indices(*state_);
  ++txn_write_ops_;
  return ok_result(id, state_->create_diagnostic("已创建定向边"));
}

Result<void> TopologyTransaction::set_coedge_pcurve(CoedgeId coedge_id,
                                                    PCurveId pcurve_id) {
  if (const auto cancelled = observe_cancellation("绑定共边参数曲线")) {
    return error_void(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxCommitFailure,
                               "绑定共边参数曲线失败：事务已关闭",
                               "绑定共边参数曲线失败");
  }
  auto it = state_->coedges.find(coedge_id.value);
  if (it == state_->coedges.end()) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "绑定共边参数曲线失败：目标定向边不存在",
                                      "绑定共边参数曲线失败");
  }
  if (pcurve_id.value != 0 && !detail::has_pcurve(*state_, pcurve_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "绑定共边参数曲线失败：目标参数曲线不存在",
                                      "绑定共边参数曲线失败");
  }
  const auto prev_pcurve = it->second.pcurve_id.value;
  transaction_state_->original_coedge_pcurves.try_emplace(coedge_id.value,
                                                         it->second.pcurve_id);
  if (pcurve_id.value != prev_pcurve) {
    const auto loop = state_->coedge_to_loop.find(coedge_id.value);
    if (loop != state_->coedge_to_loop.end()) {
      const auto faces = state_->loop_to_faces.find(loop->second);
      if (faces != state_->loop_to_faces.end())
        for (const auto face : faces->second)
          invalidate_body_for_face_edit(*state_, *transaction_state_, FaceId{face});
    }
  }
  it->second.pcurve_id = pcurve_id;
  rebuild_topology_indices(*state_);
  if (pcurve_id.value != 0) {
    ++txn_coedge_pcurve_binds_;
  } else if (prev_pcurve != 0) {
    ++txn_coedge_pcurve_clears_;
  }
  ++txn_write_ops_;
  return ok_void(state_->create_diagnostic("已绑定共边参数曲线"));
}

Result<LoopId>
TopologyTransaction::create_loop(std::span<const CoedgeId> coedges) {
  if (const auto cancelled = observe_cancellation("创建环")) {
    return error_result<LoopId>(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_ || coedges.empty()) {
    return detail::failed_result<LoopId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "创建环失败：事务已关闭或输入定向边集合为空", "创建环失败");
  }

  for (const auto coedge_id : coedges) {
    if (state_->coedges.find(coedge_id.value) == state_->coedges.end()) {
      return detail::invalid_input_result<LoopId>(
          *state_, diag_codes::kTopoLoopNotClosed, "创建环失败：存在无效定向边",
          "创建环失败");
    }
  }
  for (const auto coedge_id : coedges) {
    if (state_->coedge_to_loop.find(coedge_id.value) != state_->coedge_to_loop.end()) {
      return detail::failed_result<LoopId>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoCoedgeAlreadyOwned,
          "创建环失败：定向边已属于其他环", "创建环失败");
    }
  }
  if (has_duplicate_ids(coedges)) {
    return detail::failed_result<LoopId>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoLoopDuplicateEdge,
        "创建环失败：定向边序列包含重复引用", "创建环失败");
  }
  {
    std::unordered_map<std::uint64_t, CoedgeId> first_coedge_by_vertex;
    first_coedge_by_vertex.reserve(coedges.size());
    for (const auto coedge_id : coedges) {
      const auto oriented = oriented_vertices(*state_, coedge_id);
      if (!oriented.has_value()) {
        continue;
      }
      const auto [it, inserted] = first_coedge_by_vertex.emplace(
          (*oriented)[0].value, coedge_id);
      if (!inserted) {
        return error_result<LoopId>(
            StatusCode::InvalidTopology,
            detail::error_diag(
                *state_, "创建环失败", diag_codes::kTopoLoopRepeatedVertex,
                "创建环失败：环在闭合终点之外重复经过同一顶点",
                std::vector<std::uint64_t>{(*oriented)[0].value,
                                           it->second.value,
                                           coedge_id.value}));
      }
    }
  }
  detail::LoopRecord loop{
      std::vector<CoedgeId>(coedges.begin(), coedges.end())};
  std::string reason;
  if (!validate_loop_record(*state_, loop, reason)) {
    std::string_view fail_code = diag_codes::kTopoLoopNotClosed;
    if (reason == "环包含重复边引用" || reason == "环包含重复定向边引用") {
      fail_code = diag_codes::kTopoLoopDuplicateEdge;
    } else if (reason == "环在闭合终点之外重复经过同一顶点") {
      fail_code = diag_codes::kTopoLoopRepeatedVertex;
    }
    return detail::failed_result<LoopId>(*state_, StatusCode::InvalidTopology, fail_code,
                                         "创建环失败：" + reason, "创建环失败");
  }
  const auto id = LoopId{state_->allocate_id()};
  state_->loops.emplace(id.value, std::move(loop));
  created_loops_.push_back(id.value);
  rebuild_topology_indices(*state_);
  ++txn_write_ops_;
  return ok_result(id, state_->create_diagnostic("已创建环"));
}

Result<FaceId>
TopologyTransaction::create_face(SurfaceId surface_id, LoopId outer_loop,
                                 std::span<const LoopId> inner_loops) {
  if (const auto cancelled = observe_cancellation("创建面")) {
    return error_result<FaceId>(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_ || !detail::has_surface(*state_, surface_id) ||
      !detail::has_loop(*state_, outer_loop)) {
    return detail::invalid_input_result<FaceId>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "创建面失败：事务已关闭、目标曲面不存在或外环不存在", "创建面失败");
  }

  for (const auto loop_id : inner_loops) {
    if (!detail::has_loop(*state_, loop_id)) {
      return detail::invalid_input_result<FaceId>(
          *state_, diag_codes::kTopoFaceOuterLoopInvalid,
          "创建面失败：存在无效内环", "创建面失败");
    }
  }
  if (has_duplicate_ids(inner_loops)) {
    return detail::failed_result<FaceId>(*state_, StatusCode::InvalidTopology,
                                         diag_codes::kTopoFaceOuterLoopInvalid,
                                         "创建面失败：内环列表包含重复引用",
                                         "创建面失败");
  }
  if (std::any_of(inner_loops.begin(), inner_loops.end(),
                  [outer_loop](LoopId loop_id) {
                    return loop_id.value == outer_loop.value;
                  })) {
    return detail::failed_result<FaceId>(*state_, StatusCode::InvalidTopology,
                                         diag_codes::kTopoFaceOuterLoopInvalid,
                                         "创建面失败：内环不能与外环相同",
                                         "创建面失败");
  }
  std::string reason;
  if (!validate_loop_id(*state_, outer_loop, reason)) {
    return detail::failed_result<FaceId>(*state_, StatusCode::InvalidTopology,
                                         diag_codes::kTopoFaceOuterLoopInvalid,
                                         "创建面失败：外环非法，" + reason,
                                         "创建面失败");
  }
  for (const auto loop_id : inner_loops) {
    if (!validate_loop_id(*state_, loop_id, reason)) {
      return detail::failed_result<FaceId>(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoFaceOuterLoopInvalid,
          "创建面失败：内环非法，" + reason, "创建面失败");
    }
  }
  if (!valid_face_bound_loop_size(*state_, state_->loops.at(outer_loop.value))) {
    return detail::failed_result<FaceId>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoFaceOuterLoopInvalid,
        "创建面失败：外环边数不足（仅同曲线双弧环允许两条共边）",
        "创建面失败", {outer_loop.value});
  }
  for (const auto loop_id : inner_loops) {
    if (!valid_face_bound_loop_size(*state_, state_->loops.at(loop_id.value))) {
      return detail::failed_result<FaceId>(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoFaceInnerLoopInvalid,
          "创建面失败：内环边数不足（仅同曲线双弧环允许两条共边）",
          "创建面失败", {loop_id.value});
    }
  }
  // 7.2: each LoopId may belong to at most one Face (outer or inner lists).
  const auto outer_used = state_->loop_to_faces.find(outer_loop.value);
  if (outer_used != state_->loop_to_faces.end() &&
      !outer_used->second.empty()) {
    return error_result<FaceId>(
        StatusCode::InvalidTopology,
        detail::error_diag(
            *state_, "创建面失败", diag_codes::kTopoFaceOuterLoopInvalid,
            "创建面失败：外环已被其他面引用",
            std::vector<std::uint64_t>{outer_loop.value,
                                       outer_used->second.front()}));
  }
  for (const auto loop_id : inner_loops) {
    const auto inner_used = state_->loop_to_faces.find(loop_id.value);
    if (inner_used != state_->loop_to_faces.end() &&
        !inner_used->second.empty()) {
      return error_result<FaceId>(
          StatusCode::InvalidTopology,
          detail::error_diag(
              *state_, "创建面失败", diag_codes::kTopoFaceInnerLoopInvalid,
              "创建面失败：内环已被其他面引用",
              std::vector<std::uint64_t>{loop_id.value,
                                         inner_used->second.front()}));
    }
  }
  // Each loop is valid and unowned; reject cross-loop Edge reuse before writing
  // the face or its reverse indices, matching validate_face's boundary rule.
  std::unordered_map<std::uint64_t, LoopId> edge_loops;
  for (std::size_t i = 0; i <= inner_loops.size(); ++i) {
    const auto loop_id = i == 0 ? outer_loop : inner_loops[i - 1];
    for (const auto coedge_id : state_->loops.at(loop_id.value).coedges) {
      const auto edge_id = state_->coedges.at(coedge_id.value).edge_id;
      const auto [it, inserted] = edge_loops.emplace(edge_id.value, loop_id);
      if (!inserted) {
        return detail::failed_result<FaceId>(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoLoopDuplicateEdge,
            "创建面失败：面内存在跨环复用的边", "创建面失败",
            {it->second.value, loop_id.value, edge_id.value});
      }
    }
  }
  if (const auto shared = face_cross_loop_shared_vertex(*state_, outer_loop,
                                                        inner_loops)) {
    return detail::failed_result<FaceId>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoFaceSharedBoundaryVertex,
        "创建面失败：不同边界环共用同一拓扑顶点", "创建面失败",
        {(*shared)[0], (*shared)[1], (*shared)[2]});
  }
  if (const auto coincident = face_cross_loop_coincident_vertices(
          *state_, outer_loop, inner_loops)) {
    return detail::failed_result<FaceId>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoFaceCoincidentBoundaryVertices,
        "创建面失败：不同边界环的独立顶点坐标重合", "创建面失败",
        {(*coincident)[0], (*coincident)[1], (*coincident)[2],
         (*coincident)[3]});
  }
  const auto boundary_tolerance = detail::resolve_linear_tolerance(
      0.0, state_->config.tolerance);
  const auto boundary_search = face_cross_loop_boundary_conflict(
      *state_, outer_loop, inner_loops, boundary_tolerance);
  if (boundary_search.status != StatusCode::Ok) {
    return detail::failed_result<FaceId>(
        *state_, boundary_search.status,
        diag_codes::kTopoFaceBoundaryIntersectionIndeterminate,
        "创建面失败：显式裁剪曲线求交无法在预算和数值误差约束内完成",
        "创建面失败",
        {outer_loop.value, boundary_search.first_edge.value,
         boundary_search.second_edge.value});
  }
  if (const auto &conflict = boundary_search.conflict) {
    std::string_view code =
        conflict->error_controlled
            ? diag_codes::kTopoFaceCrossLoopCurveIntersection
            : diag_codes::kTopoFaceCrossLoopStraightEdgeIntersection;
    std::string message =
        "创建面失败：不同边界环的边界曲线在内部相交";
    switch (conflict->kind) {
      case FaceBoundaryConflictKind::EndpointTouch:
        code = conflict->error_controlled
                   ? diag_codes::kTopoFaceCrossLoopCurveEndpointTouch
                   : diag_codes::kTopoFaceCrossLoopStraightEdgeEndpointTouch;
        message = "创建面失败：不同边界环的边界曲线在拓扑边端点相接";
        break;
      case FaceBoundaryConflictKind::CollinearOverlap:
        code = diag_codes::kTopoFaceCrossLoopCollinearOverlap;
        message = "创建面失败：不同边界环的共线边界片段发生重叠";
        break;
      case FaceBoundaryConflictKind::CurveOverlap:
        code = diag_codes::kTopoFaceCrossLoopCurveOverlap;
        message = "创建面失败：不同边界环的曲线边界存在连续重合区间";
        break;
      case FaceBoundaryConflictKind::NearContact:
        code = diag_codes::kTopoFaceCrossLoopNearContact;
        message = "创建面失败：不同边界环的边界曲线在拓扑容差内邻近相接（间距=" +
                  std::to_string(conflict->distance) + "，容差=" +
                  std::to_string(boundary_tolerance) + "）";
        break;
      case FaceBoundaryConflictKind::ProperIntersection:
        break;
    }
    return detail::failed_result<FaceId>(
        *state_, StatusCode::InvalidTopology, code, message,
        "创建面失败", {conflict->first_loop.value,
                         conflict->second_loop.value,
                         conflict->first_edge.value,
                         conflict->second_edge.value});
  }
  const auto id = FaceId{state_->allocate_id()};
  detail::FaceRecord face_record;
  face_record.surface_id = surface_id;
  face_record.outer_loop = outer_loop;
  face_record.inner_loops.assign(inner_loops.begin(), inner_loops.end());
  face_record.source_faces.push_back(id);
  state_->faces.emplace(id.value, std::move(face_record));
  created_faces_.push_back(id.value);
  rebuild_topology_indices(*state_);
  ++txn_write_ops_;
  return ok_result(id, state_->create_diagnostic("已创建面"));
}

Result<ShellId>
TopologyTransaction::create_shell(std::span<const FaceId> faces) {
  if (const auto cancelled = observe_cancellation("创建壳")) {
    return error_result<ShellId>(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_ || faces.empty()) {
    return detail::failed_result<ShellId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "创建壳失败：事务已关闭或输入面集合为空", "创建壳失败");
  }

  for (const auto face_id : faces) {
    if (!detail::has_face(*state_, face_id)) {
      return detail::invalid_input_result<ShellId>(
          *state_, diag_codes::kTopoShellNotClosed, "创建壳失败：存在无效面",
          "创建壳失败");
    }
  }
  if (has_duplicate_ids(faces)) {
    return detail::failed_result<ShellId>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoDuplicateFaceInShell,
        "创建壳失败：面列表包含重复引用", "创建壳失败");
  }
  for (const auto face_id : faces) {
    const auto face_it = state_->faces.find(face_id.value);
    std::string reason;
    if (face_it == state_->faces.end() ||
        !validate_loop_id(*state_, face_it->second.outer_loop, reason)) {
      return detail::failed_result<ShellId>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "创建壳失败：包含非法面拓扑", "创建壳失败");
    }
    if (!detail::has_surface(*state_, face_it->second.surface_id)) {
      return detail::failed_result<ShellId>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "创建壳失败：成员面引用的曲面不存在", "创建壳失败",
          {face_id.value, face_it->second.surface_id.value});
    }
    for (const auto inner_loop : face_it->second.inner_loops) {
      if (!validate_loop_id(*state_, inner_loop, reason)) {
        return detail::failed_result<ShellId>(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
            "创建壳失败：面内环无效，" + reason, "创建壳失败",
            {face_id.value, inner_loop.value});
      }
    }
  }
  const auto id = ShellId{state_->allocate_id()};
  detail::ShellRecord shell_record;
  shell_record.faces.assign(faces.begin(), faces.end());
  for (const auto face_id : faces) {
    const auto face_it = state_->faces.find(face_id.value);
    if (face_it == state_->faces.end()) {
      continue;
    }
    if (face_it->second.source_faces.empty()) {
      append_unique(shell_record.source_faces, face_id);
      continue;
    }
    for (const auto source_face_id : face_it->second.source_faces) {
      append_unique(shell_record.source_faces, source_face_id);
    }
    const auto source_shell_it = state_->face_to_shells.find(face_id.value);
    if (source_shell_it != state_->face_to_shells.end()) {
      for (const auto source_shell_value : source_shell_it->second) {
        append_unique(shell_record.source_shells, ShellId{source_shell_value});
      }
    }
  }
  state_->shells.emplace(id.value, std::move(shell_record));
  created_shells_.push_back(id.value);
  rebuild_topology_indices(*state_);
  ++txn_write_ops_;
  return ok_result(id, state_->create_diagnostic("已创建壳"));
}

Result<BodyId>
TopologyTransaction::create_body(std::span<const ShellId> shells) {
  if (const auto cancelled = observe_cancellation("创建体")) {
    return error_result<BodyId>(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_ || shells.empty()) {
    return detail::failed_result<BodyId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "创建体失败：事务已关闭或输入壳集合为空", "创建体失败");
  }

  for (const auto shell_id : shells) {
    if (!detail::has_shell(*state_, shell_id)) {
      return detail::invalid_input_result<BodyId>(
          *state_, diag_codes::kTopoShellNotClosed, "创建体失败：存在无效壳",
          "创建体失败");
    }
  }
  if (has_duplicate_ids(shells)) {
    return detail::failed_result<BodyId>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "创建体失败：壳列表包含重复引用", "创建体失败");
  }
  for (const auto shell_id : shells) {
    for (const auto face_id : state_->shells.at(shell_id.value).faces) {
      const auto face_it = state_->faces.find(face_id.value);
      if (face_it != state_->faces.end() &&
          !detail::has_surface(*state_, face_it->second.surface_id)) {
        return detail::failed_result<BodyId>(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
            "创建体失败：成员面引用的曲面不存在", "创建体失败",
            {shell_id.value, face_id.value, face_it->second.surface_id.value});
      }
    }
  }
  detail::BodyRecord record;
  record.kind = detail::BodyKind::Generic;
  record.rep_kind = RepKind::ExactBRep;
  record.shells.assign(shells.begin(), shells.end());
  record.source_shells.assign(shells.begin(), shells.end());
  for (const auto shell_id : shells) {
    const auto shell_it = state_->shells.find(shell_id.value);
    if (shell_it == state_->shells.end()) {
      continue;
    }
    const auto body_it = state_->shell_to_bodies.find(shell_id.value);
    if (body_it != state_->shell_to_bodies.end()) {
      for (const auto body_value : body_it->second) {
        append_unique(record.source_bodies, BodyId{body_value});
      }
    }
    for (const auto source_shell_id : shell_it->second.source_shells) {
      append_unique(record.source_shells, source_shell_id);
    }
    if (shell_it->second.source_faces.empty()) {
      for (const auto face_id : shell_it->second.faces) {
        append_unique(record.source_faces, face_id);
      }
      continue;
    }
    for (const auto face_id : shell_it->second.source_faces) {
      append_unique(record.source_faces, face_id);
    }
  }
  record.bbox = compute_body_bbox(state_, shells);
  if (!record.bbox.is_valid) {
    return detail::failed_result<BodyId>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "创建体失败：无法从输入壳推导有效包围盒，拓扑引用可能不完整",
        "创建体失败");
  }
  const auto id = BodyId{state_->allocate_id()};
  state_->bodies.emplace(id.value, record);
  created_bodies_.push_back(id.value);
  rebuild_topology_indices(*state_);
  ++txn_write_ops_;
  return ok_result(id, state_->create_diagnostic("已创建体"));
}

Result<void> TopologyTransaction::delete_face(FaceId face_id) {
  if (const auto cancelled = observe_cancellation("删除面")) {
    return error_void(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxCommitFailure,
                               "删除面失败：事务已关闭", "删除面失败");
  }
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "删除面失败：目标面不存在", "删除面失败");
  }

  transaction_state_->snapshot_face(*state_, face_id);
  detail::invalidate_eval_for_faces(*state_, std::array{face_id});

  std::vector<ShellId> affected_shells;
  std::vector<ShellId> shells_to_remove;
  std::vector<BodyId> bodies_to_remove;
  for (auto &[shell_value, shell] : state_->shells) {
    if (std::none_of(shell.faces.begin(), shell.faces.end(),
                     [face_id](FaceId current) {
                       return current.value == face_id.value;
                     })) {
      continue;
    }
    transaction_state_->snapshot_shell(*state_, ShellId{shell_value});
    const auto old_size = shell.faces.size();
    shell.faces.erase(std::remove_if(shell.faces.begin(), shell.faces.end(),
                                     [face_id](FaceId current) {
                                       return current.value == face_id.value;
                                     }),
                      shell.faces.end());
    if (shell.faces.size() != old_size) {
      affected_shells.push_back(ShellId{shell_value});
      if (shell.faces.empty()) {
        shells_to_remove.push_back(ShellId{shell_value});
      }
    }
  }

  for (auto &[body_value, body] : state_->bodies) {
    if (std::none_of(body.shells.begin(), body.shells.end(),
                     [&affected_shells](ShellId shell_id) {
                       return std::any_of(
                           affected_shells.begin(), affected_shells.end(),
                           [shell_id](ShellId removed) {
                             return removed.value == shell_id.value;
                           });
                     })) {
      continue;
    }
    transaction_state_->snapshot_body(*state_, BodyId{body_value});
    body.analytic_mass_valid = false;
    body.primitive_tessellation_valid = false;
    const auto old_size = body.shells.size();
    body.shells.erase(
        std::remove_if(body.shells.begin(), body.shells.end(),
                       [&shells_to_remove](ShellId shell_id) {
                         return std::any_of(
                             shells_to_remove.begin(), shells_to_remove.end(),
                             [shell_id](ShellId removed) {
                               return removed.value == shell_id.value;
                             });
                       }),
        body.shells.end());
    if (body.shells.empty() && body.shells.size() != old_size) {
      bodies_to_remove.push_back(BodyId{body_value});
    }
  }

  for (const auto shell_id : shells_to_remove) {
    state_->shells.erase(shell_id.value);
  }
  for (const auto body_id : bodies_to_remove) {
    state_->bodies.erase(body_id.value);
  }
  for (auto &[body_value, body] : state_->bodies) {
    if (std::none_of(body.shells.begin(), body.shells.end(),
                     [&affected_shells](ShellId shell_id) {
                       return std::any_of(
                           affected_shells.begin(), affected_shells.end(),
                           [shell_id](ShellId affected) {
                             return affected.value == shell_id.value;
                           });
                     })) {
      continue;
    }
    const auto bbox = compute_body_bbox(state_, body.shells);
    if (bbox.is_valid) {
      body.bbox = bbox;
    }
  }

  state_->faces.erase(face_id.value);
  rebuild_topology_indices(*state_);
  ++txn_deleted_faces_;
  ++txn_write_ops_;
  return ok_void(state_->create_diagnostic("已删除面"));
}

Result<void> TopologyTransaction::delete_shell(ShellId shell_id) {
  if (const auto cancelled = observe_cancellation("删除壳")) {
    return error_void(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxCommitFailure,
                               "删除壳失败：事务已关闭", "删除壳失败");
  }
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "删除壳失败：目标壳不存在", "删除壳失败");
  }

  transaction_state_->snapshot_shell(*state_, shell_id);

  // Remove shell ownership from bodies; delete bodies that become empty.
  std::vector<BodyId> bodies_to_remove;
  for (auto &[body_value, body] : state_->bodies) {
    if (std::none_of(body.shells.begin(), body.shells.end(),
                     [shell_id](ShellId current) {
                       return current.value == shell_id.value;
                     })) {
      continue;
    }
    transaction_state_->snapshot_body(*state_, BodyId{body_value});
    detail::invalidate_eval_for_body(*state_, BodyId{body_value});
    body.analytic_mass_valid = false;
    body.primitive_tessellation_valid = false;
    const auto old_size = body.shells.size();
    body.shells.erase(std::remove_if(body.shells.begin(), body.shells.end(),
                                     [shell_id](ShellId current) {
                                       return current.value == shell_id.value;
                                     }),
                      body.shells.end());
    if (body.shells.empty() && body.shells.size() != old_size) {
      bodies_to_remove.push_back(BodyId{body_value});
    } else {
      const auto bbox = compute_body_bbox(state_, body.shells);
      if (bbox.is_valid) {
        body.bbox = bbox;
      }
    }
  }

  for (const auto body_id : bodies_to_remove) {
    state_->bodies.erase(body_id.value);
  }
  state_->shells.erase(shell_id.value);
  rebuild_topology_indices(*state_);
  ++txn_deleted_shells_;
  ++txn_write_ops_;
  return ok_void(state_->create_diagnostic("已删除壳"));
}

Result<void> TopologyTransaction::delete_body(BodyId body_id) {
  if (const auto cancelled = observe_cancellation("删除体")) {
    return error_void(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxCommitFailure,
                               "删除体失败：事务已关闭", "删除体失败");
  }
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "删除体失败：目标体不存在", "删除体失败");
  }

  transaction_state_->snapshot_body(*state_, body_id);
  detail::invalidate_eval_for_body(*state_, body_id);

  // Note: we do NOT delete referenced shells/faces automatically here, because
  // shells can be shared by multiple bodies (e.g. boolean provenance tests).
  state_->bodies.erase(body_id.value);
  rebuild_topology_indices(*state_);
  ++txn_deleted_bodies_;
  ++txn_write_ops_;
  return ok_void(state_->create_diagnostic("已删除体"));
}

Result<void> TopologyTransaction::replace_surface(FaceId face_id,
                                                  SurfaceId replacement) {
  if (const auto cancelled = observe_cancellation("替换面曲面")) {
    return error_void(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxCommitFailure,
                               "替换面失败：事务已关闭", "替换面失败");
  }
  const auto it = state_->faces.find(face_id.value);
  if (it == state_->faces.end() || !detail::has_surface(*state_, replacement)) {
    return detail::invalid_input_void(
        *state_, diag_codes::kModReplaceFaceIncompatible,
        "替换面失败：目标面不存在或替换曲面不存在", "替换面失败");
  }
  transaction_state_->snapshot_face(*state_, face_id);
  invalidate_body_for_face_edit(*state_, *transaction_state_, face_id);
  it->second.surface_id = replacement;
  rebuild_topology_indices(*state_);
  ++txn_replaced_surfaces_;
  ++txn_write_ops_;
  return ok_void(state_->create_diagnostic("已替换面曲面"));
}

Result<VersionId> TopologyTransaction::commit() {
  if (const auto cancelled = observe_cancellation("提交事务")) {
    return error_result<VersionId>(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_) {
    return detail::failed_result<VersionId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "事务提交失败：事务已关闭", "事务提交失败");
  }
  discard_removed_body_runtime(*state_);
  if (const auto owner = state_->active_topology_transaction.lock();
      owner.get() == transaction_state_.get()) {
    state_->active_topology_transaction.reset();
  }
  transaction_state_->savepoints.clear();
  active_ = false;
  const VersionId assigned = state_->next_version;
  state_->topology_commit_success_count += 1;
  state_->topology_committed_write_operations_total += txn_write_ops_;
  state_->topology_last_committed_version = assigned;
  state_->topology_last_commit_write_operations = txn_write_ops_;
  TopologyCommitWriteBreakdown snap{};
  snap.created_vertices = static_cast<std::uint64_t>(created_vertices_.size());
  snap.created_edges = static_cast<std::uint64_t>(created_edges_.size());
  snap.created_coedges = static_cast<std::uint64_t>(created_coedges_.size());
  snap.created_loops = static_cast<std::uint64_t>(created_loops_.size());
  snap.created_faces = static_cast<std::uint64_t>(created_faces_.size());
  snap.created_shells = static_cast<std::uint64_t>(created_shells_.size());
  snap.created_bodies = static_cast<std::uint64_t>(created_bodies_.size());
  snap.deleted_faces = txn_deleted_faces_;
  snap.deleted_shells = txn_deleted_shells_;
  snap.deleted_bodies = txn_deleted_bodies_;
  snap.replaced_surfaces = txn_replaced_surfaces_;
  snap.coedge_pcurve_binds = txn_coedge_pcurve_binds_;
  snap.coedge_pcurve_clears = txn_coedge_pcurve_clears_;
  state_->topology_last_commit_write_breakdown = snap;
  auto& tot = state_->topology_committed_write_breakdown_totals;
  tot.created_vertices += snap.created_vertices;
  tot.created_edges += snap.created_edges;
  tot.created_coedges += snap.created_coedges;
  tot.created_loops += snap.created_loops;
  tot.created_faces += snap.created_faces;
  tot.created_shells += snap.created_shells;
  tot.created_bodies += snap.created_bodies;
  tot.deleted_faces += snap.deleted_faces;
  tot.deleted_shells += snap.deleted_shells;
  tot.deleted_bodies += snap.deleted_bodies;
  tot.replaced_surfaces += snap.replaced_surfaces;
  tot.coedge_pcurve_binds += snap.coedge_pcurve_binds;
  tot.coedge_pcurve_clears += snap.coedge_pcurve_clears;
  ++state_->next_version;
  return ok_result<VersionId>(assigned, state_->create_diagnostic("事务已提交"));
}

Result<void> TopologyTransaction::rollback() {
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxRollbackFailure,
                               "事务回滚失败：事务已关闭", "事务回滚失败");
  }
  if (token_cancellation_requested()) {
    record_cancellation_observed(true, txn_write_ops_);
  }
  restore_model_and_close();
  return ok_void(state_->create_diagnostic(
      cancellation_observed_ ? "事务已响应取消并回滚" : "事务已回滚"));
}

Result<TopologySavepoint> TopologyTransaction::create_savepoint() {
  if (const auto cancelled = observe_cancellation("创建事务保存点")) {
    return error_result<TopologySavepoint>(StatusCode::OperationFailed,
                                           *cancelled);
  }
  if (!active_) {
    return detail::failed_result<TopologySavepoint>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "创建事务保存点失败：事务已关闭", "创建事务保存点失败");
  }

  detail::TopologySavepointSnapshot snapshot;
  snapshot.handle = TopologySavepoint{
      transaction_state_->transaction_cookie,
      transaction_state_->next_savepoint_sequence};
  snapshot.vertices = state_->vertices;
  snapshot.edges = state_->edges;
  snapshot.coedges = state_->coedges;
  snapshot.loops = state_->loops;
  snapshot.faces = state_->faces;
  snapshot.shells = state_->shells;
  snapshot.bodies = state_->bodies;
  snapshot.created_vertices = created_vertices_;
  snapshot.created_edges = created_edges_;
  snapshot.created_coedges = created_coedges_;
  snapshot.created_loops = created_loops_;
  snapshot.created_faces = created_faces_;
  snapshot.created_shells = created_shells_;
  snapshot.created_bodies = created_bodies_;
  snapshot.original_coedge_pcurves =
      transaction_state_->original_coedge_pcurves;
  snapshot.original_faces = transaction_state_->original_faces;
  snapshot.original_shells = transaction_state_->original_shells;
  snapshot.original_bodies = transaction_state_->original_bodies;
  snapshot.deleted_faces = txn_deleted_faces_;
  snapshot.deleted_shells = txn_deleted_shells_;
  snapshot.deleted_bodies = txn_deleted_bodies_;
  snapshot.replaced_surfaces = txn_replaced_surfaces_;
  snapshot.coedge_pcurve_binds = txn_coedge_pcurve_binds_;
  snapshot.coedge_pcurve_clears = txn_coedge_pcurve_clears_;
  snapshot.write_operations = txn_write_ops_;

  const auto handle = snapshot.handle;
  transaction_state_->savepoints.push_back(std::move(snapshot));
  ++transaction_state_->next_savepoint_sequence;
  ++state_->topology_savepoint_created_count;
  return ok_result(handle, state_->create_diagnostic("已创建拓扑事务保存点"));
}

Result<void>
TopologyTransaction::rollback_to_savepoint(TopologySavepoint savepoint) {
  if (const auto cancelled = observe_cancellation("回滚到事务保存点")) {
    return error_void(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxRollbackFailure,
                               "回滚到事务保存点失败：事务已关闭",
                               "回滚到事务保存点失败");
  }
  const auto matches = [savepoint](const auto &snapshot) {
    return snapshot.handle.transaction_cookie_ == savepoint.transaction_cookie_ &&
           snapshot.handle.sequence_ == savepoint.sequence_;
  };
  const auto found = std::find_if(transaction_state_->savepoints.begin(),
                                  transaction_state_->savepoints.end(), matches);
  if (savepoint.transaction_cookie_ != transaction_state_->transaction_cookie ||
      savepoint.sequence_ == 0 || found == transaction_state_->savepoints.end()) {
    return detail::failed_void(
        *state_, StatusCode::OperationFailed, diag_codes::kTxConflict,
        "回滚到事务保存点失败：句柄不属于当前事务或已失效",
        "回滚到事务保存点失败");
  }

  // Copy every baseline first. If snapshot allocation fails, the live topology
  // and transaction audit remain untouched.
  auto vertices = found->vertices;
  auto edges = found->edges;
  auto coedges = found->coedges;
  auto loops = found->loops;
  auto faces = found->faces;
  auto shells = found->shells;
  auto bodies = found->bodies;
  auto created_vertices = found->created_vertices;
  auto created_edges = found->created_edges;
  auto created_coedges = found->created_coedges;
  auto created_loops = found->created_loops;
  auto created_faces = found->created_faces;
  auto created_shells = found->created_shells;
  auto created_bodies = found->created_bodies;
  auto original_coedge_pcurves = found->original_coedge_pcurves;
  auto original_faces = found->original_faces;
  auto original_shells = found->original_shells;
  auto original_bodies = found->original_bodies;
  const auto deleted_faces = found->deleted_faces;
  const auto deleted_shells = found->deleted_shells;
  const auto deleted_bodies = found->deleted_bodies;
  const auto replaced_surfaces = found->replaced_surfaces;
  const auto coedge_pcurve_binds = found->coedge_pcurve_binds;
  const auto coedge_pcurve_clears = found->coedge_pcurve_clears;
  const auto restored_write_operations = found->write_operations;
  const auto rolled_back_write_operations =
      txn_write_ops_ >= restored_write_operations
          ? txn_write_ops_ - restored_write_operations
          : 0;
  const auto nested_count = static_cast<std::uint64_t>(
      std::distance(found, transaction_state_->savepoints.end()) - 1);

  // Recompute may have observed the edited model since the savepoint. Restoring
  // topology must dirty its consumers again, including bodies being removed.
  if (rolled_back_write_operations != 0) {
    for (const auto &[id, record] : transaction_state_->original_bodies) {
      (void)record;
      detail::invalidate_eval_for_body(*state_, BodyId{id});
    }
  }
  for (const auto &[id, record] : state_->bodies) {
    (void)record;
    if (!bodies.contains(id))
      detail::invalidate_eval_for_body(*state_, BodyId{id});
  }

  state_->vertices = std::move(vertices);
  state_->edges = std::move(edges);
  state_->coedges = std::move(coedges);
  state_->loops = std::move(loops);
  state_->faces = std::move(faces);
  state_->shells = std::move(shells);
  state_->bodies = std::move(bodies);
  created_vertices_ = std::move(created_vertices);
  created_edges_ = std::move(created_edges);
  created_coedges_ = std::move(created_coedges);
  created_loops_ = std::move(created_loops);
  created_faces_ = std::move(created_faces);
  created_shells_ = std::move(created_shells);
  created_bodies_ = std::move(created_bodies);
  transaction_state_->original_coedge_pcurves =
      std::move(original_coedge_pcurves);
  transaction_state_->original_faces = std::move(original_faces);
  transaction_state_->original_shells = std::move(original_shells);
  transaction_state_->original_bodies = std::move(original_bodies);
  txn_deleted_faces_ = deleted_faces;
  txn_deleted_shells_ = deleted_shells;
  txn_deleted_bodies_ = deleted_bodies;
  txn_replaced_surfaces_ = replaced_surfaces;
  txn_coedge_pcurve_binds_ = coedge_pcurve_binds;
  txn_coedge_pcurve_clears_ = coedge_pcurve_clears;
  txn_write_ops_ = restored_write_operations;
  rebuild_topology_indices(*state_);
  discard_removed_body_runtime(*state_);

  transaction_state_->savepoints.erase(std::next(found),
                                        transaction_state_->savepoints.end());
  ++state_->topology_savepoint_rollback_count;
  state_->topology_savepoint_discarded_nested_count += nested_count;
  state_->topology_savepoint_rolled_back_write_operations_total +=
      rolled_back_write_operations;
  state_->topology_savepoint_last_rolled_back_write_operations =
      rolled_back_write_operations;
  return ok_void(state_->create_diagnostic("已回滚到拓扑事务保存点"));
}

Result<void>
TopologyTransaction::release_savepoint(TopologySavepoint savepoint) {
  if (const auto cancelled = observe_cancellation("释放事务保存点")) {
    return error_void(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxCommitFailure,
                               "释放事务保存点失败：事务已关闭",
                               "释放事务保存点失败");
  }
  if (transaction_state_->savepoints.empty() || savepoint.sequence_ == 0 ||
      savepoint.transaction_cookie_ != transaction_state_->transaction_cookie ||
      transaction_state_->savepoints.back().handle.transaction_cookie_ !=
          savepoint.transaction_cookie_ ||
      transaction_state_->savepoints.back().handle.sequence_ !=
          savepoint.sequence_) {
    return detail::failed_void(
        *state_, StatusCode::OperationFailed, diag_codes::kTxConflict,
        "释放事务保存点失败：仅可释放当前事务最内层的有效保存点",
        "释放事务保存点失败");
  }
  transaction_state_->savepoints.pop_back();
  ++state_->topology_savepoint_released_count;
  return ok_void(state_->create_diagnostic("已释放拓扑事务保存点"));
}

Result<std::uint64_t> TopologyTransaction::active_savepoint_count() const {
  return ok_result<std::uint64_t>(
      active_ ? static_cast<std::uint64_t>(
                    transaction_state_->savepoints.size())
              : 0,
      state_->create_diagnostic("已查询事务活动保存点数量"));
}

bool TopologyTransaction::token_cancellation_requested() const noexcept {
  return cancellation_token_.is_cancellation_requested();
}

void TopologyTransaction::record_cancellation_observed(
    bool rolled_back, std::uint64_t write_operations) {
  if (cancellation_observed_) {
    return;
  }
  cancellation_observed_ = true;
  cancelled_write_ops_ = write_operations;
  ++state_->topology_cancellation_observed_count;
  if (rolled_back) {
    ++state_->topology_cancellation_rollback_count;
    state_->topology_cancelled_write_operations_total += write_operations;
    state_->topology_last_cancelled_write_operations = write_operations;
  }
}

std::optional<DiagnosticId> TopologyTransaction::observe_cancellation(
    std::string_view operation) {
  if (!cancellation_observed_ &&
      (!active_ || !token_cancellation_requested())) {
    return std::nullopt;
  }
  if (active_) {
    record_cancellation_observed(true, txn_write_ops_);
    restore_model_and_close();
  }
  return detail::error_diag(
      *state_, std::string(operation) + "已取消",
      diag_codes::kTxCancellationRequested,
      std::string(operation) +
          "失败：协作式取消请求已生效，事务写入已完整回滚");
}

void TopologyTransaction::restore_model_and_close() {
  for (const auto &[id, record] : transaction_state_->original_bodies) {
    (void)record;
    detail::invalidate_eval_for_body(*state_, BodyId{id});
  }
  for (const auto id : created_bodies_) {
    detail::invalidate_eval_for_body(*state_, BodyId{id});
  }
  for (const auto &[id, pcurve_id] : transaction_state_->original_coedge_pcurves) {
    const auto it = state_->coedges.find(id);
    if (it != state_->coedges.end()) {
      it->second.pcurve_id = pcurve_id;
    }
  }
  for (const auto &[id, record] : transaction_state_->original_faces) {
    state_->faces[id] = record;
  }
  for (const auto &[id, record] : transaction_state_->original_shells) {
    state_->shells[id] = record;
  }
  for (const auto &[id, record] : transaction_state_->original_bodies) {
    state_->bodies[id] = record;
  }

  // 新建实体也可能因后续修改或级联删除进入快照。先恢复快照，再移除本事务
  // 创建的面/壳/体，避免快照将它们复活；保留现有触达/快照查询的语义。
  for (const auto id : created_bodies_) {
    state_->bodies.erase(id);
  }
  for (const auto id : created_shells_) {
    state_->shells.erase(id);
  }
  for (const auto id : created_faces_) {
    state_->faces.erase(id);
  }

  // 最后清理本事务创建且已无引用的低层实体，并重建反向索引。
  auto loop_referenced = [this](std::uint64_t loop_id) {
    for (const auto &[fid, face] : state_->faces) {
      (void)fid;
      if (face.outer_loop.value == loop_id) {
        return true;
      }
      if (std::any_of(face.inner_loops.begin(), face.inner_loops.end(),
                      [loop_id](LoopId inner) { return inner.value == loop_id; })) {
        return true;
      }
    }
    return false;
  };
  for (const auto id : created_loops_) {
    if (!loop_referenced(id)) {
      state_->loops.erase(id);
    }
  }
  auto coedge_referenced = [this](std::uint64_t coedge_id) {
    for (const auto &[lid, loop] : state_->loops) {
      (void)lid;
      if (std::any_of(loop.coedges.begin(), loop.coedges.end(),
                      [coedge_id](CoedgeId c) { return c.value == coedge_id; })) {
        return true;
      }
    }
    return false;
  };
  for (const auto id : created_coedges_) {
    if (!coedge_referenced(id)) {
      state_->coedges.erase(id);
    }
  }
  auto edge_referenced = [this](std::uint64_t edge_id) {
    for (const auto &[cid, co] : state_->coedges) {
      (void)cid;
      if (co.edge_id.value == edge_id) {
        return true;
      }
    }
    return false;
  };
  for (const auto id : created_edges_) {
    if (!edge_referenced(id)) {
      state_->edges.erase(id);
    }
  }
  auto vertex_referenced = [this](std::uint64_t vertex_id) {
    for (const auto &[eid, edge] : state_->edges) {
      (void)eid;
      if (edge.v0.value == vertex_id || edge.v1.value == vertex_id) {
        return true;
      }
    }
    return false;
  };
  for (const auto id : created_vertices_) {
    if (!vertex_referenced(id)) {
      state_->vertices.erase(id);
    }
  }
  rebuild_topology_indices(*state_);
  discard_removed_body_runtime(*state_);
  txn_deleted_faces_ = 0;
  txn_deleted_shells_ = 0;
  txn_deleted_bodies_ = 0;
  txn_replaced_surfaces_ = 0;
  txn_coedge_pcurve_binds_ = 0;
  txn_coedge_pcurve_clears_ = 0;
  txn_write_ops_ = 0;
  transaction_state_->savepoints.clear();
  if (const auto owner = state_->active_topology_transaction.lock();
      owner.get() == transaction_state_.get()) {
    state_->active_topology_transaction.reset();
  }
  active_ = false;
}

Result<void> TopologyTransaction::poll_cancellation() {
  if (const auto cancelled = observe_cancellation("轮询事务取消")) {
    return error_void(StatusCode::OperationFailed, *cancelled);
  }
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxCommitFailure,
                               "轮询事务取消失败：事务已关闭",
                               "轮询事务取消失败");
  }
  return ok_void(state_->create_diagnostic("事务尚未请求取消"));
}

Result<bool> TopologyTransaction::is_active() const {
  return ok_result(active_, state_->create_diagnostic("已查询事务活动状态"));
}

Result<bool> TopologyTransaction::cancellation_requested() const {
  return ok_result(token_cancellation_requested(),
                   state_->create_diagnostic("已查询事务取消请求状态"));
}

Result<bool> TopologyTransaction::cancellation_observed() const {
  return ok_result(cancellation_observed_,
                   state_->create_diagnostic("已查询事务取消观察状态"));
}

Result<std::uint64_t>
TopologyTransaction::cancelled_write_operation_count() const {
  return ok_result(cancelled_write_ops_,
                   state_->create_diagnostic("已查询事务取消回滚写次数"));
}

Result<std::uint64_t> TopologyTransaction::created_vertex_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_vertices_.size()),
      state_->create_diagnostic("已查询事务创建顶点数量"));
}

Result<std::uint64_t> TopologyTransaction::created_edge_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_edges_.size()),
      state_->create_diagnostic("已查询事务创建边数量"));
}

Result<std::uint64_t> TopologyTransaction::created_coedge_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_coedges_.size()),
      state_->create_diagnostic("已查询事务创建定向边数量"));
}

Result<std::uint64_t> TopologyTransaction::created_loop_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_loops_.size()),
      state_->create_diagnostic("已查询事务创建环数量"));
}

Result<std::uint64_t> TopologyTransaction::created_face_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_faces_.size()),
      state_->create_diagnostic("已查询事务创建面数量"));
}

Result<std::uint64_t> TopologyTransaction::created_shell_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_shells_.size()),
      state_->create_diagnostic("已查询事务创建壳数量"));
}

Result<std::uint64_t> TopologyTransaction::created_body_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_bodies_.size()),
      state_->create_diagnostic("已查询事务创建体数量"));
}

Result<std::uint64_t> TopologyTransaction::created_entity_count_total() const {
  const auto total = created_vertices_.size() + created_edges_.size() +
                     created_coedges_.size() + created_loops_.size() +
                     created_faces_.size() + created_shells_.size() +
                     created_bodies_.size();
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(total),
      state_->create_diagnostic("已查询事务创建实体总数"));
}

Result<std::uint64_t> TopologyTransaction::touched_face_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(transaction_state_->original_faces.size()),
      state_->create_diagnostic("已查询事务触达面数量"));
}

Result<std::uint64_t> TopologyTransaction::touched_shell_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(transaction_state_->original_shells.size()),
      state_->create_diagnostic("已查询事务触达壳数量"));
}

Result<std::uint64_t> TopologyTransaction::touched_body_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(transaction_state_->original_bodies.size()),
      state_->create_diagnostic("已查询事务触达体数量"));
}

Result<std::uint64_t> TopologyTransaction::deleted_face_count() const {
  return ok_result<std::uint64_t>(txn_deleted_faces_,
                                  state_->create_diagnostic("已查询事务删除面数量"));
}

Result<std::uint64_t> TopologyTransaction::deleted_shell_count() const {
  return ok_result<std::uint64_t>(txn_deleted_shells_,
                                  state_->create_diagnostic("已查询事务删除壳数量"));
}

Result<std::uint64_t> TopologyTransaction::deleted_body_count() const {
  return ok_result<std::uint64_t>(txn_deleted_bodies_,
                                  state_->create_diagnostic("已查询事务删除体数量"));
}

Result<std::uint64_t> TopologyTransaction::replaced_surface_count() const {
  return ok_result<std::uint64_t>(
      txn_replaced_surfaces_,
      state_->create_diagnostic("已查询事务替换曲面次数"));
}

Result<std::uint64_t> TopologyTransaction::coedge_pcurve_bind_count() const {
  return ok_result<std::uint64_t>(
      txn_coedge_pcurve_binds_,
      state_->create_diagnostic("已查询事务共边 PCurve 绑定次数"));
}

Result<std::uint64_t> TopologyTransaction::coedge_pcurve_clear_count() const {
  return ok_result<std::uint64_t>(
      txn_coedge_pcurve_clears_,
      state_->create_diagnostic("已查询事务共边 PCurve 清除次数"));
}

Result<std::uint64_t> TopologyTransaction::write_operation_count() const {
  return ok_result<std::uint64_t>(
      txn_write_ops_,
      state_->create_diagnostic("已查询事务拓扑写操作累计次数"));
}

Result<TopologyIsolationLevel>
TopologyTransaction::effective_isolation_level() const {
  return ok_result(TopologyIsolationLevel::SnapshotSerializable,
                   state_->create_diagnostic("已查询拓扑事务有效隔离级别"));
}

Result<bool> TopologyTransaction::has_created_vertex(VertexId vertex_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_vertices_), vertex_id),
      state_->create_diagnostic("已查询事务是否创建顶点"));
}

Result<bool> TopologyTransaction::has_created_edge(EdgeId edge_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_edges_), edge_id),
      state_->create_diagnostic("已查询事务是否创建边"));
}

Result<bool> TopologyTransaction::has_created_coedge(CoedgeId coedge_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_coedges_), coedge_id),
      state_->create_diagnostic("已查询事务是否创建定向边"));
}

Result<bool> TopologyTransaction::has_created_loop(LoopId loop_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_loops_), loop_id),
      state_->create_diagnostic("已查询事务是否创建环"));
}

Result<bool> TopologyTransaction::has_created_face(FaceId face_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_faces_), face_id),
      state_->create_diagnostic("已查询事务是否创建面"));
}

Result<bool> TopologyTransaction::has_created_shell(ShellId shell_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_shells_), shell_id),
      state_->create_diagnostic("已查询事务是否创建壳"));
}

Result<bool> TopologyTransaction::has_created_body(BodyId body_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_bodies_), body_id),
      state_->create_diagnostic("已查询事务是否创建体"));
}

Result<bool> TopologyTransaction::can_commit() const {
  return ok_result(active_ && !token_cancellation_requested(),
                   state_->create_diagnostic("已查询事务是否可提交"));
}

Result<VersionId> TopologyTransaction::preview_commit_version() const {
  if (active_ && token_cancellation_requested()) {
    return detail::failed_result<VersionId>(
        *state_, StatusCode::OperationFailed,
        diag_codes::kTxCancellationRequested,
        "事务预览失败：协作式取消请求等待事务边界处理",
        "事务预览失败");
  }
  if (cancellation_observed_) {
    return detail::failed_result<VersionId>(
        *state_, StatusCode::OperationFailed,
        diag_codes::kTxCancellationRequested,
        "事务预览失败：事务已响应协作式取消并回滚",
        "事务预览失败");
  }
  if (!active_) {
    return detail::failed_result<VersionId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "事务预览失败：事务已关闭", "事务预览失败");
  }
  return ok_result<VersionId>(state_->next_version,
                              state_->create_diagnostic("已预览事务提交版本"));
}

Result<bool> TopologyTransaction::has_snapshot_face(FaceId face_id) const {
  return ok_result(transaction_state_->original_faces.find(face_id.value) !=
                       transaction_state_->original_faces.end(),
                   state_->create_diagnostic("已查询事务是否快照面"));
}

Result<bool> TopologyTransaction::has_snapshot_shell(ShellId shell_id) const {
  return ok_result(transaction_state_->original_shells.find(shell_id.value) !=
                       transaction_state_->original_shells.end(),
                   state_->create_diagnostic("已查询事务是否快照壳"));
}

Result<bool> TopologyTransaction::has_snapshot_body(BodyId body_id) const {
  return ok_result(transaction_state_->original_bodies.find(body_id.value) !=
                       transaction_state_->original_bodies.end(),
                   state_->create_diagnostic("已查询事务是否快照体"));
}

Result<void> TopologyTransaction::clear_tracking_records() {
  if (active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxActiveTrackingClear,
                               "清空事务跟踪记录失败：请先提交或回滚活动事务",
                               "清空事务跟踪记录失败");
  }
  created_vertices_.clear();
  created_edges_.clear();
  created_coedges_.clear();
  created_loops_.clear();
  created_faces_.clear();
  created_shells_.clear();
  created_bodies_.clear();
  txn_deleted_faces_ = 0;
  txn_deleted_shells_ = 0;
  txn_deleted_bodies_ = 0;
  txn_replaced_surfaces_ = 0;
  txn_coedge_pcurve_binds_ = 0;
  txn_coedge_pcurve_clears_ = 0;
  txn_write_ops_ = 0;
  cancelled_write_ops_ = 0;
  transaction_state_->original_coedge_pcurves.clear();
  transaction_state_->original_faces.clear();
  transaction_state_->original_shells.clear();
  transaction_state_->original_bodies.clear();
  transaction_state_->savepoints.clear();
  return ok_void(state_->create_diagnostic("已清空事务跟踪记录"));
}

Result<std::vector<VertexId>> TopologyTransaction::created_vertices() const {
  std::vector<VertexId> out;
  out.reserve(created_vertices_.size());
  for (const auto id : created_vertices_) {
    out.push_back(VertexId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建顶点列表"));
}

Result<std::vector<EdgeId>> TopologyTransaction::created_edges() const {
  std::vector<EdgeId> out;
  out.reserve(created_edges_.size());
  for (const auto id : created_edges_) {
    out.push_back(EdgeId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建边列表"));
}

Result<std::vector<CoedgeId>> TopologyTransaction::created_coedges() const {
  std::vector<CoedgeId> out;
  out.reserve(created_coedges_.size());
  for (const auto id : created_coedges_) {
    out.push_back(CoedgeId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建定向边列表"));
}

Result<std::vector<LoopId>> TopologyTransaction::created_loops() const {
  std::vector<LoopId> out;
  out.reserve(created_loops_.size());
  for (const auto id : created_loops_) {
    out.push_back(LoopId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建环列表"));
}

Result<std::vector<FaceId>> TopologyTransaction::created_faces() const {
  std::vector<FaceId> out;
  out.reserve(created_faces_.size());
  for (const auto id : created_faces_) {
    out.push_back(FaceId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建面列表"));
}

Result<std::vector<ShellId>> TopologyTransaction::created_shells() const {
  std::vector<ShellId> out;
  out.reserve(created_shells_.size());
  for (const auto id : created_shells_) {
    out.push_back(ShellId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建壳列表"));
}

Result<std::vector<BodyId>> TopologyTransaction::created_bodies() const {
  std::vector<BodyId> out;
  out.reserve(created_bodies_.size());
  for (const auto id : created_bodies_) {
    out.push_back(BodyId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建体列表"));
}
