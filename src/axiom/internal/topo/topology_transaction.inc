TopologyTransaction::TopologyTransaction(
    std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)),
      transaction_state_(std::make_shared<detail::TopologyTransactionState>()) {
}

Result<VertexId> TopologyTransaction::create_vertex(const Point3 &point) {
  if (!active_) {
    return detail::failed_result<VertexId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "创建顶点失败：事务已关闭", "创建顶点失败");
  }
  const auto id = VertexId{state_->allocate_id()};
  state_->vertices.emplace(id.value, detail::VertexRecord{point});
  created_vertices_.push_back(id.value);
  ++txn_write_ops_;
  return ok_result(id, state_->create_diagnostic("已创建顶点"));
}

Result<EdgeId> TopologyTransaction::create_edge(CurveId curve_id, VertexId v0,
                                                VertexId v1) {
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

Result<CoedgeId> TopologyTransaction::create_coedge(EdgeId edge_id,
                                                    bool reversed) {
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
  const auto id = LoopId{state_->allocate_id()};
  detail::LoopRecord loop{
      std::vector<CoedgeId>(coedges.begin(), coedges.end())};
  std::string reason;
  if (!validate_loop_record(*state_, loop, reason)) {
    std::string_view fail_code = diag_codes::kTopoLoopNotClosed;
    if (reason == "环包含重复边引用" || reason == "环包含重复定向边引用") {
      fail_code = diag_codes::kTopoLoopDuplicateEdge;
    }
    return detail::failed_result<LoopId>(*state_, StatusCode::InvalidTopology, fail_code,
                                         "创建环失败：" + reason, "创建环失败");
  }
  state_->loops.emplace(id.value, std::move(loop));
  created_loops_.push_back(id.value);
  rebuild_topology_indices(*state_);
  ++txn_write_ops_;
  return ok_result(id, state_->create_diagnostic("已创建环"));
}

Result<FaceId>
TopologyTransaction::create_face(SurfaceId surface_id, LoopId outer_loop,
                                 std::span<const LoopId> inner_loops) {
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
  const auto id = BodyId{state_->allocate_id()};
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
  record.bbox = compute_body_bbox(*state_, shells);
  if (!record.bbox.is_valid) {
    return detail::failed_result<BodyId>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "创建体失败：无法从输入壳推导有效包围盒，拓扑引用可能不完整",
        "创建体失败");
  }
  state_->bodies.emplace(id.value, record);
  created_bodies_.push_back(id.value);
  rebuild_topology_indices(*state_);
  ++txn_write_ops_;
  return ok_result(id, state_->create_diagnostic("已创建体"));
}

Result<void> TopologyTransaction::delete_face(FaceId face_id) {
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
    const auto bbox = compute_body_bbox(*state_, body.shells);
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
    const auto old_size = body.shells.size();
    body.shells.erase(std::remove_if(body.shells.begin(), body.shells.end(),
                                     [shell_id](ShellId current) {
                                       return current.value == shell_id.value;
                                     }),
                      body.shells.end());
    if (body.shells.empty() && body.shells.size() != old_size) {
      bodies_to_remove.push_back(BodyId{body_value});
    } else {
      const auto bbox = compute_body_bbox(*state_, body.shells);
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
  it->second.surface_id = replacement;
  rebuild_topology_indices(*state_);
  ++txn_replaced_surfaces_;
  return ok_void(state_->create_diagnostic("已替换面曲面"));
}

Result<VersionId> TopologyTransaction::commit() {
  if (!active_) {
    return detail::failed_result<VersionId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "事务提交失败：事务已关闭", "事务提交失败");
  }
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
  for (const auto id : created_bodies_) {
    state_->bodies.erase(id);
  }
  for (const auto id : created_shells_) {
    state_->shells.erase(id);
  }
  for (const auto id : created_faces_) {
    state_->faces.erase(id);
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

  // 恢复面/壳/体之后再删除本事务创建的环/共边/边/顶点，且仅删除已不再被当前状态引用的项，
  // 否则会出现“面仍引用环 id，但环记录已被擦除”的断裂（例如同事务内 delete_face 后 rollback）。
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
  txn_deleted_faces_ = 0;
  txn_deleted_shells_ = 0;
  txn_deleted_bodies_ = 0;
  txn_replaced_surfaces_ = 0;
  txn_coedge_pcurve_binds_ = 0;
  txn_coedge_pcurve_clears_ = 0;
  txn_write_ops_ = 0;
  active_ = false;
  return ok_void(state_->create_diagnostic("事务已回滚"));
}

Result<bool> TopologyTransaction::is_active() const {
  return ok_result(active_, state_->create_diagnostic("已查询事务活动状态"));
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
  return ok_result(active_, state_->create_diagnostic("已查询事务是否可提交"));
}

Result<VersionId> TopologyTransaction::preview_commit_version() const {
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
  transaction_state_->original_faces.clear();
  transaction_state_->original_shells.clear();
  transaction_state_->original_bodies.clear();
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
