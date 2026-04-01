TopologyQueryService::TopologyQueryService(
    std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

Result<std::array<VertexId, 2>>
TopologyQueryService::vertices_of_edge(EdgeId edge_id) const {
  const auto it = state_->edges.find(edge_id.value);
  if (it == state_->edges.end()) {
    return detail::invalid_input_result<std::array<VertexId, 2>>(
        *state_, diag_codes::kCoreInvalidHandle, "边查询失败：目标边不存在",
        "边端点查询失败");
  }
  return ok_result(std::array<VertexId, 2>{it->second.v0, it->second.v1},
                   state_->create_diagnostic("已查询边端点"));
}

Result<std::vector<CoedgeId>>
TopologyQueryService::coedges_of_edge(EdgeId edge_id) const {
  if (!detail::has_edge(*state_, edge_id)) {
    return detail::invalid_input_result<std::vector<CoedgeId>>(
        *state_, diag_codes::kCoreInvalidHandle, "边查询失败：目标边不存在",
        "边定向边集合查询失败");
  }

  std::vector<CoedgeId> coedges;
  const auto index_it = state_->edge_to_coedges.find(edge_id.value);
  if (index_it != state_->edge_to_coedges.end()) {
    coedges.reserve(index_it->second.size());
    for (const auto coedge_value : index_it->second) {
      coedges.push_back(CoedgeId{coedge_value});
    }
  }
  return ok_result(coedges, state_->create_diagnostic("已查询边定向边集合"));
}

Result<std::vector<LoopId>>
TopologyQueryService::loops_of_edge(EdgeId edge_id) const {
  if (!detail::has_edge(*state_, edge_id)) {
    return detail::invalid_input_result<std::vector<LoopId>>(
        *state_, diag_codes::kCoreInvalidHandle, "边查询失败：目标边不存在",
        "边所属环集合查询失败");
  }

  std::vector<LoopId> loops;
  const auto coedges_result = coedges_of_edge(edge_id);
  if (coedges_result.status != StatusCode::Ok ||
      !coedges_result.value.has_value()) {
    return detail::failed_result<std::vector<LoopId>>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoCurveTopologyMismatch,
        "边查询失败：无法稳定解析边定向边集合", "边所属环集合查询失败");
  }
  for (const auto coedge_id : *coedges_result.value) {
    const auto loop_it = state_->coedge_to_loop.find(coedge_id.value);
    if (loop_it != state_->coedge_to_loop.end()) {
      append_unique(loops, LoopId{loop_it->second});
    }
  }
  return ok_result(loops, state_->create_diagnostic("已查询边所属环集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::faces_of_edge(EdgeId edge_id) const {
  if (!detail::has_edge(*state_, edge_id)) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kCoreInvalidHandle, "边查询失败：目标边不存在",
        "边所属面集合查询失败");
  }

  const auto loops_result = loops_of_edge(edge_id);
  if (loops_result.status != StatusCode::Ok ||
      !loops_result.value.has_value()) {
    return detail::failed_result<std::vector<FaceId>>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoCurveTopologyMismatch,
        "边查询失败：无法稳定解析边所属环", "边所属面集合查询失败");
  }

  std::vector<FaceId> faces;
  for (const auto loop_id : *loops_result.value) {
    const auto face_it = state_->loop_to_faces.find(loop_id.value);
    if (face_it == state_->loop_to_faces.end()) {
      continue;
    }
    for (const auto face_value : face_it->second) {
      append_unique(faces, FaceId{face_value});
    }
  }
  return ok_result(faces, state_->create_diagnostic("已查询边所属面集合"));
}

Result<std::vector<ShellId>>
TopologyQueryService::shells_of_edge(EdgeId edge_id) const {
  const auto faces_result = faces_of_edge(edge_id);
  if (faces_result.status != StatusCode::Ok ||
      !faces_result.value.has_value()) {
    return detail::failed_result<std::vector<ShellId>>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoCurveTopologyMismatch,
        "边查询失败：无法稳定解析边所属面集合", "边所属壳集合查询失败");
  }

  std::vector<ShellId> shells;
  for (const auto face_id : *faces_result.value) {
    const auto shell_it = state_->face_to_shells.find(face_id.value);
    if (shell_it == state_->face_to_shells.end()) {
      continue;
    }
    for (const auto shell_value : shell_it->second) {
      append_unique(shells, ShellId{shell_value});
    }
  }
  return ok_result(shells, state_->create_diagnostic("已查询边所属壳集合"));
}

Result<std::vector<EdgeId>>
TopologyQueryService::edges_of_loop(LoopId loop_id) const {
  const auto it = state_->loops.find(loop_id.value);
  if (it == state_->loops.end()) {
    return detail::invalid_input_result<std::vector<EdgeId>>(
        *state_, diag_codes::kTopoLoopNotClosed, "环查询失败：目标环不存在",
        "环边集合查询失败");
  }
  std::vector<EdgeId> edges;
  edges.reserve(it->second.coedges.size());
  for (const auto coedge_id : it->second.coedges) {
    const auto coedge_it = state_->coedges.find(coedge_id.value);
    if (coedge_it == state_->coedges.end() ||
        !detail::has_edge(*state_, coedge_it->second.edge_id)) {
      return detail::failed_result<std::vector<EdgeId>>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoLoopNotClosed,
          "环查询失败：环包含无效定向边或边引用", "环边集合查询失败");
    }
    edges.push_back(coedge_it->second.edge_id);
  }
  return ok_result(edges, state_->create_diagnostic("已查询环边集合"));
}

Result<std::vector<LoopId>>
TopologyQueryService::loops_of_face(FaceId face_id) const {
  const auto it = state_->faces.find(face_id.value);
  if (it == state_->faces.end()) {
    return detail::invalid_input_result<std::vector<LoopId>>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "面查询失败：目标面不存在", "面环集合查询失败");
  }
  auto loops = it->second.inner_loops;
  loops.insert(loops.begin(), it->second.outer_loop);
  return ok_result(loops, state_->create_diagnostic("已查询面环集合"));
}

Result<SurfaceId> TopologyQueryService::surface_of_face(FaceId face_id) const {
  const auto it = state_->faces.find(face_id.value);
  if (it == state_->faces.end()) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kTopoCurveTopologyMismatch,
        "面曲面查询失败：目标面不存在", "面曲面映射查询失败");
  }
  return ok_result(it->second.surface_id,
                   state_->create_diagnostic("已查询面曲面映射"));
}

Result<std::vector<ShellId>>
TopologyQueryService::shells_of_face(FaceId face_id) const {
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<std::vector<ShellId>>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "面查询失败：目标面不存在", "面所属壳集合查询失败");
  }

  std::vector<ShellId> shells;
  const auto shell_it = state_->face_to_shells.find(face_id.value);
  if (shell_it != state_->face_to_shells.end()) {
    for (const auto shell_value : shell_it->second) {
      shells.push_back(ShellId{shell_value});
    }
  }
  return ok_result(shells, state_->create_diagnostic("已查询面所属壳集合"));
}

Result<std::vector<BodyId>>
TopologyQueryService::bodies_of_face(FaceId face_id) const {
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<std::vector<BodyId>>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "面查询失败：目标面不存在", "面所属体集合查询失败");
  }

  std::vector<BodyId> bodies;
  const auto shells_result = shells_of_face(face_id);
  if (shells_result.status != StatusCode::Ok ||
      !shells_result.value.has_value()) {
    return detail::failed_result<std::vector<BodyId>>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "面查询失败：无法稳定解析面所属壳集合", "面所属体集合查询失败");
  }
  for (const auto shell_id : *shells_result.value) {
    const auto body_it = state_->shell_to_bodies.find(shell_id.value);
    if (body_it == state_->shell_to_bodies.end()) {
      continue;
    }
    for (const auto body_value : body_it->second) {
      append_unique(bodies, BodyId{body_value});
    }
  }
  return ok_result(bodies, state_->create_diagnostic("已查询面所属体集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::source_faces_of_face(FaceId face_id) const {
  const auto it = state_->faces.find(face_id.value);
  if (it == state_->faces.end()) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "面来源查询失败：目标面不存在", "面来源面集合查询失败");
  }
  return ok_result(it->second.source_faces,
                   state_->create_diagnostic("已查询面来源面集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::faces_of_shell(ShellId shell_id) const {
  const auto it = state_->shells.find(shell_id.value);
  if (it == state_->shells.end()) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kTopoShellNotClosed, "壳查询失败：目标壳不存在",
        "壳面集合查询失败");
  }
  return ok_result(it->second.faces,
                   state_->create_diagnostic("已查询壳面集合"));
}

Result<std::vector<BodyId>>
TopologyQueryService::bodies_of_shell(ShellId shell_id) const {
  if (!detail::has_shell(*state_, shell_id)) {
    return detail::invalid_input_result<std::vector<BodyId>>(
        *state_, diag_codes::kTopoShellNotClosed, "壳查询失败：目标壳不存在",
        "壳所属体集合查询失败");
  }

  std::vector<BodyId> bodies;
  const auto body_it = state_->shell_to_bodies.find(shell_id.value);
  if (body_it != state_->shell_to_bodies.end()) {
    for (const auto body_value : body_it->second) {
      bodies.push_back(BodyId{body_value});
    }
  }
  return ok_result(bodies, state_->create_diagnostic("已查询壳所属体集合"));
}

Result<std::vector<ShellId>>
TopologyQueryService::source_shells_of_shell(ShellId shell_id) const {
  const auto it = state_->shells.find(shell_id.value);
  if (it == state_->shells.end()) {
    return detail::invalid_input_result<std::vector<ShellId>>(
        *state_, diag_codes::kTopoShellNotClosed,
        "壳来源查询失败：目标壳不存在", "壳来源壳集合查询失败");
  }
  return ok_result(it->second.source_shells,
                   state_->create_diagnostic("已查询壳来源壳集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::source_faces_of_shell(ShellId shell_id) const {
  const auto it = state_->shells.find(shell_id.value);
  if (it == state_->shells.end()) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kTopoShellNotClosed,
        "壳来源查询失败：目标壳不存在", "壳来源面集合查询失败");
  }
  return ok_result(it->second.source_faces,
                   state_->create_diagnostic("已查询壳来源面集合"));
}

Result<std::vector<ShellId>>
TopologyQueryService::shells_of_body(BodyId body_id) const {
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<std::vector<ShellId>>(
        *state_, diag_codes::kCoreInvalidHandle, "体查询失败：目标实体不存在",
        "体壳集合查询失败");
  }
  return ok_result(it->second.shells,
                   state_->create_diagnostic("已查询体壳集合"));
}

Result<std::vector<BodyId>>
TopologyQueryService::source_bodies_of_body(BodyId body_id) const {
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<std::vector<BodyId>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体来源查询失败：目标实体不存在", "体来源体集合查询失败");
  }
  return ok_result(it->second.source_bodies,
                   state_->create_diagnostic("已查询体来源体集合"));
}

Result<std::vector<ShellId>>
TopologyQueryService::source_shells_of_body(BodyId body_id) const {
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<std::vector<ShellId>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体来源查询失败：目标实体不存在", "体来源壳集合查询失败");
  }
  return ok_result(it->second.source_shells,
                   state_->create_diagnostic("已查询体来源壳集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::source_faces_of_body(BodyId body_id) const {
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体来源查询失败：目标实体不存在", "体来源面集合查询失败");
  }
  return ok_result(it->second.source_faces,
                   state_->create_diagnostic("已查询体来源面集合"));
}

Result<TopologySummary>
TopologyQueryService::summary_of_shell(ShellId shell_id) const {
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::invalid_input_result<TopologySummary>(
        *state_, diag_codes::kTopoShellNotClosed,
        "壳拓扑摘要查询失败：目标壳不存在", "壳拓扑摘要查询失败");
  }
  TopologySummary summary{};
  summary.shell_count = 1;
  summary.face_count =
      static_cast<std::uint64_t>(shell_it->second.faces.size());
  std::unordered_set<std::uint64_t> loops;
  std::unordered_set<std::uint64_t> edges;
  std::unordered_set<std::uint64_t> vertices;
  for (const auto face_id : shell_it->second.faces) {
    const auto face_it = state_->faces.find(face_id.value);
    if (face_it == state_->faces.end()) {
      continue;
    }
    loops.insert(face_it->second.outer_loop.value);
    for (const auto loop_id : face_it->second.inner_loops)
      loops.insert(loop_id.value);
  }
  for (const auto loop_value : loops) {
    const auto loop_it = state_->loops.find(loop_value);
    if (loop_it == state_->loops.end())
      continue;
    for (const auto coedge_id : loop_it->second.coedges) {
      const auto coedge_it = state_->coedges.find(coedge_id.value);
      if (coedge_it == state_->coedges.end())
        continue;
      const auto edge_id = coedge_it->second.edge_id.value;
      edges.insert(edge_id);
      const auto edge_it = state_->edges.find(edge_id);
      if (edge_it == state_->edges.end())
        continue;
      vertices.insert(edge_it->second.v0.value);
      vertices.insert(edge_it->second.v1.value);
    }
  }
  summary.loop_count = static_cast<std::uint64_t>(loops.size());
  summary.edge_count = static_cast<std::uint64_t>(edges.size());
  summary.vertex_count = static_cast<std::uint64_t>(vertices.size());
  return ok_result(summary, state_->create_diagnostic("已查询壳拓扑摘要"));
}

Result<TopologySummary>
TopologyQueryService::summary_of_body(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::invalid_input_result<TopologySummary>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体拓扑摘要查询失败：目标体不存在", "体拓扑摘要查询失败");
  }
  TopologySummary summary{};
  summary.shell_count =
      static_cast<std::uint64_t>(body_it->second.shells.size());
  for (const auto shell_id : body_it->second.shells) {
    const auto shell_summary = summary_of_shell(shell_id);
    if (shell_summary.status != StatusCode::Ok ||
        !shell_summary.value.has_value()) {
      return error_result<TopologySummary>(shell_summary.status,
                                           shell_summary.diagnostic_id);
    }
    summary.face_count += shell_summary.value->face_count;
    summary.loop_count += shell_summary.value->loop_count;
    summary.edge_count += shell_summary.value->edge_count;
    summary.vertex_count += shell_summary.value->vertex_count;
  }
  return ok_result(summary, state_->create_diagnostic("已查询体拓扑摘要"));
}

Result<std::uint64_t>
TopologyQueryService::edge_count_of_loop(LoopId loop_id) const {
  const auto edges = edges_of_loop(loop_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(edges.value->size()),
      state_->create_diagnostic("已查询环边数量"));
}

Result<std::uint64_t>
TopologyQueryService::loop_count_of_face(FaceId face_id) const {
  const auto loops = loops_of_face(face_id);
  if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
    return error_result<std::uint64_t>(loops.status, loops.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(loops.value->size()),
      state_->create_diagnostic("已查询面环数量"));
}

Result<std::uint64_t>
TopologyQueryService::face_count_of_shell(ShellId shell_id) const {
  const auto faces = faces_of_shell(shell_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<std::uint64_t>(faces.status, faces.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(faces.value->size()),
      state_->create_diagnostic("已查询壳面数量"));
}

Result<std::uint64_t>
TopologyQueryService::shell_count_of_body(BodyId body_id) const {
  const auto shells = shells_of_body(body_id);
  if (shells.status != StatusCode::Ok || !shells.value.has_value()) {
    return error_result<std::uint64_t>(shells.status, shells.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(shells.value->size()),
      state_->create_diagnostic("已查询体壳数量"));
}

Result<std::uint64_t>
TopologyQueryService::coedge_count_of_edge(EdgeId edge_id) const {
  const auto coedges = coedges_of_edge(edge_id);
  if (coedges.status != StatusCode::Ok || !coedges.value.has_value()) {
    return error_result<std::uint64_t>(coedges.status, coedges.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(coedges.value->size()),
      state_->create_diagnostic("已查询边定向边数量"));
}

Result<std::uint64_t>
TopologyQueryService::owner_count_of_edge(EdgeId edge_id) const {
  const auto shells = shells_of_edge(edge_id);
  if (shells.status != StatusCode::Ok || !shells.value.has_value()) {
    return error_result<std::uint64_t>(shells.status, shells.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(shells.value->size()),
      state_->create_diagnostic("已查询边壳归属数量"));
}

Result<std::uint64_t>
TopologyQueryService::owner_count_of_face(FaceId face_id) const {
  const auto shells = shells_of_face(face_id);
  if (shells.status != StatusCode::Ok || !shells.value.has_value()) {
    return error_result<std::uint64_t>(shells.status, shells.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(shells.value->size()),
      state_->create_diagnostic("已查询面壳归属数量"));
}

Result<std::uint64_t>
TopologyQueryService::owner_count_of_shell(ShellId shell_id) const {
  const auto bodies = bodies_of_shell(shell_id);
  if (bodies.status != StatusCode::Ok || !bodies.value.has_value()) {
    return error_result<std::uint64_t>(bodies.status, bodies.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(bodies.value->size()),
      state_->create_diagnostic("已查询壳体归属数量"));
}

Result<bool> TopologyQueryService::has_vertex(VertexId vertex_id) const {
  return ok_result(detail::has_vertex(*state_, vertex_id),
                   state_->create_diagnostic("已查询顶点存在性"));
}

Result<bool> TopologyQueryService::has_edge(EdgeId edge_id) const {
  return ok_result(detail::has_edge(*state_, edge_id),
                   state_->create_diagnostic("已查询边存在性"));
}

Result<bool> TopologyQueryService::has_loop(LoopId loop_id) const {
  return ok_result(detail::has_loop(*state_, loop_id),
                   state_->create_diagnostic("已查询环存在性"));
}

Result<bool> TopologyQueryService::has_face(FaceId face_id) const {
  return ok_result(detail::has_face(*state_, face_id),
                   state_->create_diagnostic("已查询面存在性"));
}

Result<bool> TopologyQueryService::has_shell(ShellId shell_id) const {
  return ok_result(detail::has_shell(*state_, shell_id),
                   state_->create_diagnostic("已查询壳存在性"));
}

Result<bool> TopologyQueryService::has_body(BodyId body_id) const {
  return ok_result(detail::has_body(*state_, body_id),
                   state_->create_diagnostic("已查询体存在性"));
}

Result<bool> TopologyQueryService::is_edge_boundary(EdgeId edge_id) const {
  const auto count = owner_count_of_edge(edge_id);
  if (count.status != StatusCode::Ok || !count.value.has_value()) {
    return error_result<bool>(count.status, count.diagnostic_id);
  }
  return ok_result(*count.value == 1,
                   state_->create_diagnostic("已查询边是否边界边"));
}

Result<bool> TopologyQueryService::is_edge_non_manifold(EdgeId edge_id) const {
  const auto count = owner_count_of_edge(edge_id);
  if (count.status != StatusCode::Ok || !count.value.has_value()) {
    return error_result<bool>(count.status, count.diagnostic_id);
  }
  return ok_result(*count.value > 2,
                   state_->create_diagnostic("已查询边是否非流形"));
}

Result<bool> TopologyQueryService::is_face_orphan(FaceId face_id) const {
  const auto owners = owner_count_of_face(face_id);
  if (owners.status != StatusCode::Ok || !owners.value.has_value()) {
    return error_result<bool>(owners.status, owners.diagnostic_id);
  }
  return ok_result(*owners.value == 0,
                   state_->create_diagnostic("已查询面是否孤立"));
}

Result<bool> TopologyQueryService::is_shell_orphan(ShellId shell_id) const {
  const auto owners = owner_count_of_shell(shell_id);
  if (owners.status != StatusCode::Ok || !owners.value.has_value()) {
    return error_result<bool>(owners.status, owners.diagnostic_id);
  }
  return ok_result(*owners.value == 0,
                   state_->create_diagnostic("已查询壳是否孤立"));
}

Result<bool> TopologyQueryService::is_body_derived(BodyId body_id) const {
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体派生查询失败：目标实体不存在", "体派生查询失败");
  }
  const bool derived = !it->second.source_bodies.empty() ||
                       !it->second.source_shells.empty() ||
                       !it->second.source_faces.empty();
  return ok_result(derived, state_->create_diagnostic("已查询体派生属性"));
}

Result<BoundingBox> TopologyQueryService::bbox_of_face(FaceId face_id) const {
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<BoundingBox>(
        *state_, diag_codes::kCoreInvalidHandle,
        "面包围盒查询失败：目标面不存在", "面包围盒查询失败");
  }
  BoundingBox bbox{};
  if (!append_face_bbox(*state_, face_id, bbox) || !bbox.is_valid) {
    return detail::failed_result<BoundingBox>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoFaceOuterLoopInvalid, "面包围盒查询失败：面拓扑不完整",
        "面包围盒查询失败");
  }
  return ok_result(bbox, state_->create_diagnostic("已查询面包围盒"));
}

Result<BoundingBox>
TopologyQueryService::bbox_of_shell(ShellId shell_id) const {
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::invalid_input_result<BoundingBox>(
        *state_, diag_codes::kCoreInvalidHandle,
        "壳包围盒查询失败：目标壳不存在", "壳包围盒查询失败");
  }
  BoundingBox bbox{};
  for (const auto face_id : shell_it->second.faces) {
    if (!append_face_bbox(*state_, face_id, bbox)) {
      return detail::failed_result<BoundingBox>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "壳包围盒查询失败：壳面拓扑不完整", "壳包围盒查询失败");
    }
  }
  if (!bbox.is_valid) {
    return detail::failed_result<BoundingBox>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "壳包围盒查询失败：壳不包含可用拓扑", "壳包围盒查询失败");
  }
  return ok_result(bbox, state_->create_diagnostic("已查询壳包围盒"));
}

Result<BoundingBox>
TopologyQueryService::bbox_of_body_from_topology(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::invalid_input_result<BoundingBox>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体拓扑包围盒查询失败：目标体不存在", "体拓扑包围盒查询失败");
  }
  const auto bbox = compute_body_bbox(*state_, body_it->second.shells);
  if (!bbox.is_valid) {
    return detail::failed_result<BoundingBox>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "体拓扑包围盒查询失败：体拓扑不完整", "体拓扑包围盒查询失败");
  }
  return ok_result(bbox, state_->create_diagnostic("已查询体拓扑包围盒"));
}

Result<std::vector<FaceId>>
TopologyQueryService::faces_of_body(BodyId body_id) const {
  const auto shells = shells_of_body(body_id);
  if (shells.status != StatusCode::Ok || !shells.value.has_value()) {
    return error_result<std::vector<FaceId>>(shells.status,
                                             shells.diagnostic_id);
  }
  std::vector<FaceId> out;
  for (const auto shell_id : *shells.value) {
    const auto faces = faces_of_shell(shell_id);
    if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
      return error_result<std::vector<FaceId>>(faces.status,
                                               faces.diagnostic_id);
    }
    for (const auto face_id : *faces.value)
      append_unique(out, face_id);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询体的全部面"));
}

Result<std::vector<LoopId>>
TopologyQueryService::loops_of_body(BodyId body_id) const {
  const auto faces = faces_of_body(body_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<std::vector<LoopId>>(faces.status, faces.diagnostic_id);
  }
  std::vector<LoopId> out;
  for (const auto face_id : *faces.value) {
    const auto loops = loops_of_face(face_id);
    if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
      return error_result<std::vector<LoopId>>(loops.status,
                                               loops.diagnostic_id);
    }
    for (const auto loop_id : *loops.value)
      append_unique(out, loop_id);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询体的全部环"));
}

Result<std::vector<EdgeId>>
TopologyQueryService::edges_of_body(BodyId body_id) const {
  const auto loops = loops_of_body(body_id);
  if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
    return error_result<std::vector<EdgeId>>(loops.status, loops.diagnostic_id);
  }
  std::vector<EdgeId> out;
  for (const auto loop_id : *loops.value) {
    const auto edges = edges_of_loop(loop_id);
    if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
      return error_result<std::vector<EdgeId>>(edges.status,
                                               edges.diagnostic_id);
    }
    for (const auto edge_id : *edges.value)
      append_unique(out, edge_id);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询体的全部边"));
}

Result<std::vector<VertexId>>
TopologyQueryService::vertices_of_body(BodyId body_id) const {
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::vector<VertexId>>(edges.status,
                                               edges.diagnostic_id);
  }
  std::vector<VertexId> out;
  for (const auto edge_id : *edges.value) {
    const auto vertices = vertices_of_edge(edge_id);
    if (vertices.status != StatusCode::Ok || !vertices.value.has_value()) {
      return error_result<std::vector<VertexId>>(vertices.status,
                                                 vertices.diagnostic_id);
    }
    append_unique(out, vertices.value->at(0));
    append_unique(out, vertices.value->at(1));
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询体的全部顶点"));
}

Result<std::uint64_t>
TopologyQueryService::face_count_of_body(BodyId body_id) const {
  const auto faces = faces_of_body(body_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<std::uint64_t>(faces.status, faces.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(faces.value->size()),
      state_->create_diagnostic("已查询体面数量"));
}

Result<std::uint64_t>
TopologyQueryService::loop_count_of_body(BodyId body_id) const {
  const auto loops = loops_of_body(body_id);
  if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
    return error_result<std::uint64_t>(loops.status, loops.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(loops.value->size()),
      state_->create_diagnostic("已查询体环数量"));
}

Result<std::uint64_t>
TopologyQueryService::edge_count_of_body(BodyId body_id) const {
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(edges.value->size()),
      state_->create_diagnostic("已查询体边数量"));
}

Result<std::uint64_t>
TopologyQueryService::vertex_count_of_body(BodyId body_id) const {
  const auto vertices = vertices_of_body(body_id);
  if (vertices.status != StatusCode::Ok || !vertices.value.has_value()) {
    return error_result<std::uint64_t>(vertices.status, vertices.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(vertices.value->size()),
      state_->create_diagnostic("已查询体顶点数量"));
}

Result<bool> TopologyQueryService::body_has_face(BodyId body_id,
                                                 FaceId face_id) const {
  const auto faces = faces_of_body(body_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<bool>(faces.status, faces.diagnostic_id);
  }
  return ok_result(
      std::any_of(faces.value->begin(), faces.value->end(),
                  [face_id](FaceId id) { return id.value == face_id.value; }),
      state_->create_diagnostic("已查询体是否包含面"));
}

Result<bool> TopologyQueryService::shell_has_face(ShellId shell_id,
                                                  FaceId face_id) const {
  const auto faces = faces_of_shell(shell_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<bool>(faces.status, faces.diagnostic_id);
  }
  return ok_result(
      std::any_of(faces.value->begin(), faces.value->end(),
                  [face_id](FaceId id) { return id.value == face_id.value; }),
      state_->create_diagnostic("已查询壳是否包含面"));
}

Result<bool> TopologyQueryService::face_has_loop(FaceId face_id,
                                                 LoopId loop_id) const {
  const auto loops = loops_of_face(face_id);
  if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
    return error_result<bool>(loops.status, loops.diagnostic_id);
  }
  return ok_result(
      std::any_of(loops.value->begin(), loops.value->end(),
                  [loop_id](LoopId id) { return id.value == loop_id.value; }),
      state_->create_diagnostic("已查询面是否包含环"));
}

Result<bool> TopologyQueryService::loop_has_edge(LoopId loop_id,
                                                 EdgeId edge_id) const {
  const auto edges = edges_of_loop(loop_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<bool>(edges.status, edges.diagnostic_id);
  }
  return ok_result(
      std::any_of(edges.value->begin(), edges.value->end(),
                  [edge_id](EdgeId id) { return id.value == edge_id.value; }),
      state_->create_diagnostic("已查询环是否包含边"));
}

Result<bool> TopologyQueryService::edge_has_vertex(EdgeId edge_id,
                                                   VertexId vertex_id) const {
  const auto vertices = vertices_of_edge(edge_id);
  if (vertices.status != StatusCode::Ok || !vertices.value.has_value()) {
    return error_result<bool>(vertices.status, vertices.diagnostic_id);
  }
  return ok_result(vertices.value->at(0).value == vertex_id.value ||
                       vertices.value->at(1).value == vertex_id.value,
                   state_->create_diagnostic("已查询边是否包含顶点"));
}

Result<std::uint64_t>
TopologyQueryService::shared_face_count_of_body(BodyId body_id) const {
  const auto faces = faces_of_body(body_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<std::uint64_t>(faces.status, faces.diagnostic_id);
  }
  std::uint64_t count = 0;
  for (const auto face_id : *faces.value) {
    const auto owners = owner_count_of_face(face_id);
    if (owners.status != StatusCode::Ok || !owners.value.has_value()) {
      return error_result<std::uint64_t>(owners.status, owners.diagnostic_id);
    }
    if (*owners.value > 1)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计体共享面数量"));
}

Result<std::uint64_t>
TopologyQueryService::shared_edge_count_of_body(BodyId body_id) const {
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  std::uint64_t count = 0;
  for (const auto edge_id : *edges.value) {
    const auto owners = owner_count_of_edge(edge_id);
    if (owners.status != StatusCode::Ok || !owners.value.has_value()) {
      return error_result<std::uint64_t>(owners.status, owners.diagnostic_id);
    }
    if (*owners.value > 1)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计体共享边数量"));
}

Result<std::uint64_t>
TopologyQueryService::boundary_edge_count_of_body(BodyId body_id) const {
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  std::uint64_t count = 0;
  for (const auto edge_id : *edges.value) {
    const auto is_boundary = is_edge_boundary(edge_id);
    if (is_boundary.status != StatusCode::Ok ||
        !is_boundary.value.has_value()) {
      return error_result<std::uint64_t>(is_boundary.status,
                                         is_boundary.diagnostic_id);
    }
    if (*is_boundary.value)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计体边界边数量"));
}

Result<std::uint64_t>
TopologyQueryService::non_manifold_edge_count_of_body(BodyId body_id) const {
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  std::uint64_t count = 0;
  for (const auto edge_id : *edges.value) {
    const auto is_non_manifold = is_edge_non_manifold(edge_id);
    if (is_non_manifold.status != StatusCode::Ok ||
        !is_non_manifold.value.has_value()) {
      return error_result<std::uint64_t>(is_non_manifold.status,
                                         is_non_manifold.diagnostic_id);
    }
    if (*is_non_manifold.value)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计体非流形边数量"));
}

Result<bool>
TopologyQueryService::is_body_topology_empty(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体拓扑空性查询失败：目标体不存在", "体拓扑空性查询失败");
  }
  return ok_result(body_it->second.shells.empty(),
                   state_->create_diagnostic("已查询体拓扑是否为空"));
}

Result<PCurveId> TopologyQueryService::pcurve_of_coedge(CoedgeId coedge_id) const {
  const auto it = state_->coedges.find(coedge_id.value);
  if (it == state_->coedges.end()) {
    return detail::invalid_input_result<PCurveId>(
        *state_, diag_codes::kCoreInvalidHandle,
        "共边参数曲线查询失败：目标定向边不存在", "共边参数曲线查询失败");
  }
  return ok_result(it->second.pcurve_id,
                   state_->create_diagnostic("已查询共边参数曲线"));
}


Result<SurfaceId>
TopologyQueryService::underlying_surface_for_face_trim(FaceId face_id) const {
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kCoreInvalidHandle,
        "面修剪基曲面查询失败：目标面不存在", "面修剪基曲面查询失败");
  }
  const auto &face = state_->faces.at(face_id.value);
  if (!detail::has_surface(*state_, face.surface_id)) {
    return error_result<SurfaceId>(
        StatusCode::InvalidTopology,
        detail::error_diag(
            *state_, "面修剪基曲面查询失败", diag_codes::kTopoFaceOuterLoopInvalid,
            "面修剪基曲面查询失败：面引用的曲面不存在",
            std::vector<std::uint64_t>{face_id.value, face.surface_id.value}));
  }
  const SurfaceId base =
      underlying_trim_base_surface_id(*state_, face.surface_id);
  return ok_result(base, state_->create_diagnostic("已查询面修剪用底层曲面"));
}

Result<Range2D>
TopologyQueryService::face_outer_loop_uv_bounds(FaceId face_id) const {
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<Range2D>(
        *state_, diag_codes::kCoreInvalidHandle,
        "面外环 UV 包围查询失败：目标面不存在", "面外环 UV 包围查询失败");
  }
  const auto &face = state_->faces.at(face_id.value);
  const auto loop_it = state_->loops.find(face.outer_loop.value);
  if (loop_it == state_->loops.end() || loop_it->second.coedges.empty()) {
    return error_result<Range2D>(
        StatusCode::InvalidTopology,
        detail::error_diag(
            *state_, "面外环 UV 包围查询失败", diag_codes::kTopoFaceOuterLoopInvalid,
            "面外环 UV 包围查询失败：外环不存在或为空",
            std::vector<std::uint64_t>{face_id.value, face.outer_loop.value}));
  }
  bool initialized = false;
  Scalar u_min = 0.0;
  Scalar u_max = 0.0;
  Scalar v_min = 0.0;
  Scalar v_max = 0.0;
  for (const auto coedge_id : loop_it->second.coedges) {
    const auto ce_it = state_->coedges.find(coedge_id.value);
    if (ce_it == state_->coedges.end()) {
      return error_result<Range2D>(
          StatusCode::InvalidTopology,
          detail::error_diag(
              *state_, "面外环 UV 包围查询失败", diag_codes::kTopoFaceOuterLoopInvalid,
              "面外环 UV 包围查询失败：共边记录缺失",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value}));
    }
    const PCurveId pc_id = ce_it->second.pcurve_id;
    if (pc_id.value == 0 || !detail::has_pcurve(*state_, pc_id)) {
      return error_result<Range2D>(
          StatusCode::InvalidTopology,
          detail::error_diag(
              *state_, "面外环 UV 包围查询失败", diag_codes::kTopoRelationInconsistent,
              "面外环 UV 包围查询失败：共边未绑定有效 PCurve",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value,
                                           pc_id.value}));
    }
    const auto &pc = state_->pcurves.at(pc_id.value);
    if (!accumulate_polyline_pcurve_uv_bounds(pc, initialized, u_min, u_max,
                                               v_min, v_max)) {
      return error_result<Range2D>(
          StatusCode::NotImplemented,
          detail::error_diag(
              *state_, "面外环 UV 包围查询失败", diag_codes::kCoreOperationUnsupported,
              "面外环 UV 包围查询失败：暂仅支持折线型 PCurve 且须含有效顶点",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value,
                                           pc_id.value}));
    }
  }
  if (!initialized) {
    return error_result<Range2D>(
        StatusCode::DegenerateGeometry,
        detail::error_diag(
            *state_, "面外环 UV 包围查询失败", diag_codes::kGeoDegenerateGeometry,
            "面外环 UV 包围查询失败：未能从 PCurve 得到有效范围",
            std::vector<std::uint64_t>{face_id.value}));
  }
  ensure_strict_increasing_range(u_min, u_max);
  ensure_strict_increasing_range(v_min, v_max);
  return ok_result(Range2D{Range1D{u_min, u_max}, Range1D{v_min, v_max}},
                   state_->create_diagnostic("已计算面外环 UV 轴对齐包围"));
}

Result<std::vector<Point2>>
TopologyQueryService::face_outer_loop_uv_polyline(FaceId face_id) const {
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<std::vector<Point2>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "面外环 UV 折线查询失败：目标面不存在", "面外环 UV 折线查询失败");
  }
  const auto &face = state_->faces.at(face_id.value);
  const auto loop_it = state_->loops.find(face.outer_loop.value);
  if (loop_it == state_->loops.end() || loop_it->second.coedges.empty()) {
    return error_result<std::vector<Point2>>(
        StatusCode::InvalidTopology,
        detail::error_diag(
            *state_, "面外环 UV 折线查询失败", diag_codes::kTopoFaceOuterLoopInvalid,
            "面外环 UV 折线查询失败：外环不存在或为空",
            std::vector<std::uint64_t>{face_id.value, face.outer_loop.value}));
  }
  std::vector<Point2> out;
  out.reserve(32);
  for (const auto coedge_id : loop_it->second.coedges) {
    const auto ce_it = state_->coedges.find(coedge_id.value);
    if (ce_it == state_->coedges.end()) {
      return error_result<std::vector<Point2>>(
          StatusCode::InvalidTopology,
          detail::error_diag(
              *state_, "面外环 UV 折线查询失败", diag_codes::kTopoFaceOuterLoopInvalid,
              "面外环 UV 折线查询失败：共边记录缺失",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value}));
    }
    const PCurveId pc_id = ce_it->second.pcurve_id;
    if (pc_id.value == 0 || !detail::has_pcurve(*state_, pc_id)) {
      return error_result<std::vector<Point2>>(
          StatusCode::InvalidTopology,
          detail::error_diag(
              *state_, "面外环 UV 折线查询失败", diag_codes::kTopoRelationInconsistent,
              "面外环 UV 折线查询失败：共边未绑定有效 PCurve",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value, pc_id.value}));
    }
    const auto &pc = state_->pcurves.at(pc_id.value);
    if (pc.kind != detail::PCurveKind::Polyline || pc.poles.empty()) {
      return error_result<std::vector<Point2>>(
          StatusCode::NotImplemented,
          detail::error_diag(
              *state_, "面外环 UV 折线查询失败", diag_codes::kCoreOperationUnsupported,
              "面外环 UV 折线查询失败：暂仅支持折线型 PCurve",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value, pc_id.value}));
    }
    for (const auto &q : pc.poles) {
      if (!std::isfinite(q.x) || !std::isfinite(q.y)) {
        return error_result<std::vector<Point2>>(
            StatusCode::DegenerateGeometry,
            detail::error_diag(
                *state_, "面外环 UV 折线查询失败", diag_codes::kGeoDegenerateGeometry,
                "面外环 UV 折线查询失败：PCurve 顶点含非法数值",
                std::vector<std::uint64_t>{face_id.value, pc_id.value}));
      }
    }
    append_coedge_polyline_to_uv_path(pc, ce_it->second.reversed, out);
  }
  if (out.size() >= 2) {
    const auto &a = out.front();
    const auto &b = out.back();
    if (std::hypot(a.x - b.x, a.y - b.y) < 1e-12) {
      out.pop_back();
    }
  }
  if (out.size() < 3) {
    return error_result<std::vector<Point2>>(
        StatusCode::DegenerateGeometry,
        detail::error_diag(
            *state_, "面外环 UV 折线查询失败", diag_codes::kGeoDegenerateGeometry,
            "面外环 UV 折线查询失败：有效顶点不足 3 个",
            std::vector<std::uint64_t>{face_id.value}));
  }
  return ok_result(std::move(out), state_->create_diagnostic("已串联面外环 UV 折线"));
}
