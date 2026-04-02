Result<void> TopologyValidationService::validate_indices_consistency() const {
  for (const auto &[edge_value, coedge_values] : state_->edge_to_coedges) {
    if (state_->edges.find(edge_value) == state_->edges.end()) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoCurveTopologyMismatch,
                                 "索引验证失败：edge_to_coedges 包含失效边",
                                 "拓扑索引验证失败");
    }
    for (const auto coedge_value : coedge_values) {
      const auto coedge_it = state_->coedges.find(coedge_value);
      if (coedge_it == state_->coedges.end() ||
          coedge_it->second.edge_id.value != edge_value) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoCurveTopologyMismatch,
            "索引验证失败：edge_to_coedges 与 coedges 不一致",
            "拓扑索引验证失败");
      }
    }
  }
  for (const auto &[face_value, shell_values] : state_->face_to_shells) {
    if (state_->faces.find(face_value) == state_->faces.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "索引验证失败：face_to_shells 包含失效面", "拓扑索引验证失败");
    }
    std::unordered_set<std::uint64_t> seen_shell;
    seen_shell.reserve(shell_values.size());
    for (const auto shell_value : shell_values) {
      if (!seen_shell.insert(shell_value).second) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "索引验证失败：face_to_shells 存在重复壳条目", "拓扑索引验证失败",
            {face_value, shell_value});
      }
      const auto shell_it = state_->shells.find(shell_value);
      if (shell_it == state_->shells.end() ||
          std::none_of(
              shell_it->second.faces.begin(), shell_it->second.faces.end(),
              [face_value](FaceId id) { return id.value == face_value; })) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoShellNotClosed,
            "索引验证失败：face_to_shells 与 shells 不一致",
            "拓扑索引验证失败");
      }
    }
  }
  for (const auto &[shell_value, body_values] : state_->shell_to_bodies) {
    if (state_->shells.find(shell_value) == state_->shells.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "索引验证失败：shell_to_bodies 包含失效壳", "拓扑索引验证失败");
    }
    std::unordered_set<std::uint64_t> seen_body;
    seen_body.reserve(body_values.size());
    for (const auto body_value : body_values) {
      if (!seen_body.insert(body_value).second) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "索引验证失败：shell_to_bodies 存在重复体条目", "拓扑索引验证失败",
            {shell_value, body_value});
      }
      const auto body_it = state_->bodies.find(body_value);
      if (body_it == state_->bodies.end() ||
          std::none_of(
              body_it->second.shells.begin(), body_it->second.shells.end(),
              [shell_value](ShellId id) { return id.value == shell_value; })) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoShellNotClosed,
            "索引验证失败：shell_to_bodies 与 bodies 不一致",
            "拓扑索引验证失败");
      }
    }
  }

  // Forward containment: duplicate FaceId in shell.faces / ShellId in body.shells
  // (index corruption or partial writes; mirrors validate_shell / validate_body).
  for (const auto &[shell_value, shell] : state_->shells) {
    if (has_duplicate_ids(std::span<const FaceId>(shell.faces))) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoDuplicateFaceInShell,
          "索引验证失败：壳的面列表存在重复条目", "拓扑索引验证失败",
          {shell_value});
    }
  }
  for (const auto &[body_value, body] : state_->bodies) {
    if (has_duplicate_ids(std::span<const ShellId>(body.shells))) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "索引验证失败：体的壳列表存在重复条目", "拓扑索引验证失败",
          {body_value});
    }
  }

  for (const auto &[face_value, face_rec] : state_->faces) {
    if (has_duplicate_ids(std::span<const LoopId>(face_rec.inner_loops))) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoFaceOuterLoopInvalid,
          "索引验证失败：面内环列表存在重复引用", "拓扑索引验证失败",
          {face_value});
    }
    for (const auto inner_loop_id : face_rec.inner_loops) {
      if (inner_loop_id.value == face_rec.outer_loop.value) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoFaceInnerLoopInvalid,
            "索引验证失败：内环列表包含外环", "拓扑索引验证失败",
            {face_value, face_rec.outer_loop.value});
      }
    }
    const auto fts = state_->face_to_shells.find(face_value);
    if (fts == state_->face_to_shells.end() || fts->second.empty()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoOrphanFace,
          "索引验证失败：面未被任何壳引用（孤立面）", "拓扑索引验证失败",
          {face_value});
    }
  }

  for (const auto &[coedge_value, loop_value] : state_->coedge_to_loop) {
    if (state_->coedges.find(coedge_value) == state_->coedges.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "索引验证失败：coedge_to_loop 包含失效定向边", "拓扑索引验证失败",
          {coedge_value, loop_value});
    }
    const auto lit = state_->loops.find(loop_value);
    if (lit == state_->loops.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "索引验证失败：coedge_to_loop 指向不存在的环", "拓扑索引验证失败",
          {coedge_value, loop_value});
    }
    const bool listed = std::any_of(
        lit->second.coedges.begin(), lit->second.coedges.end(),
        [coedge_value](CoedgeId cid) { return cid.value == coedge_value; });
    if (!listed) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "索引验证失败：定向边索引与环成员列表不一致", "拓扑索引验证失败",
          {coedge_value, loop_value});
    }
  }

  for (const auto &[coedge_value, coedge] : state_->coedges) {
    (void)coedge;
    if (state_->coedge_to_loop.find(coedge_value) ==
        state_->coedge_to_loop.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoDanglingCoedge,
          "索引验证失败：定向边未被任何环引用", "拓扑索引验证失败",
          {coedge_value});
    }
  }

  // Reverse index: every Edge record must be referenced by at least one Coedge
  // (edge_to_coedges rebuilt from coedges). Catches committed "edge-only" writes
  // and index corruption after partial updates.
  for (const auto &[edge_value, edge] : state_->edges) {
    if (!detail::has_vertex(*state_, edge.v0) ||
        !detail::has_vertex(*state_, edge.v1)) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoCurveTopologyMismatch,
          "索引验证失败：边记录引用了不存在的顶点", "拓扑索引验证失败",
          {edge_value, edge.v0.value, edge.v1.value});
    }
    const auto owners_it = state_->edge_to_coedges.find(edge_value);
    if (owners_it == state_->edge_to_coedges.end() ||
        owners_it->second.empty()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoDanglingEdge,
          "索引验证失败：边在 edge_to_coedges 中无定向边引用（悬挂边）",
          "拓扑索引验证失败", {edge_value});
    }
    std::unordered_set<std::uint64_t> seen_coedge;
    seen_coedge.reserve(owners_it->second.size());
    for (const auto coedge_value : owners_it->second) {
      if (!seen_coedge.insert(coedge_value).second) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "索引验证失败：edge_to_coedges 存在重复定向边条目",
            "拓扑索引验证失败", {edge_value, coedge_value});
      }
    }
  }

  for (const auto &[loop_value, loop] : state_->loops) {
    (void)loop;
    const auto ltf = state_->loop_to_faces.find(loop_value);
    if (ltf == state_->loop_to_faces.end() || ltf->second.empty()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoOrphanLoop,
          "索引验证失败：环未被任何面引用", "拓扑索引验证失败",
          {loop_value});
    }
  }

  for (const auto &[loop_value, face_values] : state_->loop_to_faces) {
    if (state_->loops.find(loop_value) == state_->loops.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "索引验证失败：loop_to_faces 包含失效环", "拓扑索引验证失败",
          {loop_value});
    }
    std::unordered_set<std::uint64_t> loop_face_owners;
    loop_face_owners.reserve(face_values.size());
    for (const auto face_value : face_values) {
      if (!loop_face_owners.insert(face_value).second) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "索引验证失败：loop_to_faces 存在重复面条目", "拓扑索引验证失败",
            {loop_value, face_value});
      }
    }
    if (loop_face_owners.size() > 1U) {
      auto it = loop_face_owners.begin();
      const std::uint64_t fa = *it++;
      const std::uint64_t fb = *it;
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "索引验证失败：同一环被多个面引用（loop_to_faces）", "拓扑索引验证失败",
          {loop_value, fa, fb});
    }
    for (const auto face_value : face_values) {
      const auto fit = state_->faces.find(face_value);
      if (fit == state_->faces.end()) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
            "索引验证失败：loop_to_faces 引用不存在的面", "拓扑索引验证失败",
            {loop_value, face_value});
      }
      if (!face_record_references_loop(fit->second, loop_value)) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "索引验证失败：loop_to_faces 与面记录不一致", "拓扑索引验证失败",
            {loop_value, face_value});
      }
    }
  }

  for (const auto &[vertex_value, vertex] : state_->vertices) {
    (void)vertex;
    const bool used_by_edge = std::any_of(
        state_->edges.begin(), state_->edges.end(),
        [vertex_value](const auto &entry) {
          const auto &e = entry.second;
          return e.v0.value == vertex_value || e.v1.value == vertex_value;
        });
    if (!used_by_edge) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoDanglingVertex,
          "索引验证失败：顶点未被任何边引用（悬挂顶点）", "拓扑索引验证失败",
          {vertex_value});
    }
  }

  return ok_void(state_->create_diagnostic("拓扑索引验证通过"));
}

Result<void> TopologyValidationService::validate_face_many(
    std::span<const FaceId> face_ids) const {
  if (face_ids.empty()) {
    return detail::invalid_input_void(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "面批量验证失败：输入为空", "面批量验证失败");
  }
  for (const auto face_id : face_ids) {
    const auto r = validate_face(face_id);
    if (r.status != StatusCode::Ok)
      return r;
  }
  return ok_void(state_->create_diagnostic("面批量验证通过"));
}

Result<void> TopologyValidationService::validate_shell_many(
    std::span<const ShellId> shell_ids) const {
  if (shell_ids.empty()) {
    return detail::invalid_input_void(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "壳批量验证失败：输入为空", "壳批量验证失败");
  }
  for (const auto shell_id : shell_ids) {
    const auto r = validate_shell(shell_id);
    if (r.status != StatusCode::Ok)
      return r;
  }
  return ok_void(state_->create_diagnostic("壳批量验证通过"));
}

Result<void> TopologyValidationService::validate_body_many(
    std::span<const BodyId> body_ids) const {
  if (body_ids.empty()) {
    return detail::invalid_input_void(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "体批量验证失败：输入为空", "体批量验证失败");
  }
  for (const auto body_id : body_ids) {
    const auto r = validate_body(body_id);
    if (r.status != StatusCode::Ok)
      return r;
  }
  return ok_void(state_->create_diagnostic("体批量验证通过"));
}

Result<bool> TopologyValidationService::is_face_valid(FaceId face_id) const {
  const auto r = validate_face(face_id);
  if (r.status == StatusCode::InvalidInput)
    return error_result<bool>(r.status, r.diagnostic_id);
  return ok_result(r.status == StatusCode::Ok,
                   state_->create_diagnostic("已查询面有效性"));
}

Result<bool> TopologyValidationService::is_shell_valid(ShellId shell_id) const {
  const auto r = validate_shell(shell_id);
  if (r.status == StatusCode::InvalidInput)
    return error_result<bool>(r.status, r.diagnostic_id);
  return ok_result(r.status == StatusCode::Ok,
                   state_->create_diagnostic("已查询壳有效性"));
}

Result<bool> TopologyValidationService::is_body_valid(BodyId body_id) const {
  const auto r = validate_body(body_id);
  if (r.status == StatusCode::InvalidInput)
    return error_result<bool>(r.status, r.diagnostic_id);
  return ok_result(r.status == StatusCode::Ok,
                   state_->create_diagnostic("已查询体有效性"));
}

Result<std::uint64_t> TopologyValidationService::count_invalid_faces(
    std::span<const FaceId> face_ids) const {
  std::uint64_t count = 0;
  for (const auto face_id : face_ids) {
    if (validate_face(face_id).status != StatusCode::Ok)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计无效面数量"));
}

Result<std::uint64_t> TopologyValidationService::count_invalid_shells(
    std::span<const ShellId> shell_ids) const {
  std::uint64_t count = 0;
  for (const auto shell_id : shell_ids) {
    if (validate_shell(shell_id).status != StatusCode::Ok)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计无效壳数量"));
}

Result<std::uint64_t> TopologyValidationService::count_invalid_bodies(
    std::span<const BodyId> body_ids) const {
  std::uint64_t count = 0;
  for (const auto body_id : body_ids) {
    if (validate_body(body_id).status != StatusCode::Ok)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计无效体数量"));
}

Result<FaceId> TopologyValidationService::first_invalid_face(
    std::span<const FaceId> face_ids) const {
  for (const auto face_id : face_ids) {
    if (validate_face(face_id).status != StatusCode::Ok) {
      return ok_result(face_id, state_->create_diagnostic("已找到首个无效面"));
    }
  }
  return detail::failed_result<FaceId>(*state_, StatusCode::OperationFailed,
                                       diag_codes::kCoreParameterOutOfRange,
                                       "无效面查询失败：输入中不存在无效面",
                                       "无效面查询失败");
}

Result<ShellId> TopologyValidationService::first_invalid_shell(
    std::span<const ShellId> shell_ids) const {
  for (const auto shell_id : shell_ids) {
    if (validate_shell(shell_id).status != StatusCode::Ok) {
      return ok_result(shell_id, state_->create_diagnostic("已找到首个无效壳"));
    }
  }
  return detail::failed_result<ShellId>(*state_, StatusCode::OperationFailed,
                                        diag_codes::kCoreParameterOutOfRange,
                                        "无效壳查询失败：输入中不存在无效壳",
                                        "无效壳查询失败");
}

Result<BodyId> TopologyValidationService::first_invalid_body(
    std::span<const BodyId> body_ids) const {
  for (const auto body_id : body_ids) {
    if (validate_body(body_id).status != StatusCode::Ok) {
      return ok_result(body_id, state_->create_diagnostic("已找到首个无效体"));
    }
  }
  return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed,
                                       diag_codes::kCoreParameterOutOfRange,
                                       "无效体查询失败：输入中不存在无效体",
                                       "无效体查询失败");
}
