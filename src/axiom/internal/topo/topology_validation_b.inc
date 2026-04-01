Result<void>
TopologyValidationService::validate_shell_sources(ShellId shell_id) const {
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "壳来源验证失败：目标壳不存在", "壳来源验证失败");
  }
  if (has_duplicate_ids(
          std::span<const ShellId>(shell_it->second.source_shells)) ||
      !validate_source_shells_exist(*state_, shell_it->second.source_shells)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoSourceRefInvalid,
        "壳来源验证失败：来源壳引用非法", "壳来源验证失败");
  }
  if (has_duplicate_ids(
          std::span<const FaceId>(shell_it->second.source_faces)) ||
      !validate_source_faces_exist(*state_, shell_it->second.source_faces)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoSourceRefInvalid,
        "壳来源验证失败：来源面引用非法", "壳来源验证失败");
  }
  return ok_void(state_->create_diagnostic("壳来源验证通过"));
}

Result<void> TopologyValidationService::validate_shell(ShellId shell_id) const {
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "壳验证失败：目标壳不存在", "壳验证失败");
  }
  if (shell_it->second.faces.empty()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "壳验证失败：壳不包含任何面", "壳验证失败");
  }
  if (has_duplicate_ids(std::span<const FaceId>(shell_it->second.faces))) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoDuplicateFaceInShell,
                               "壳验证失败：壳包含重复面引用", "壳验证失败",
                               {shell_id.value});
  }
  // Shell ↔ face_to_shells: every face listed by the shell must reverse-index this shell.
  for (const auto face_id : shell_it->second.faces) {
    if (!detail::has_face(*state_, face_id)) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "壳验证失败：壳引用了不存在的面", "壳验证失败",
          {shell_id.value, face_id.value});
    }
    const auto fts_it = state_->face_to_shells.find(face_id.value);
    if (fts_it == state_->face_to_shells.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "壳验证失败：面在 face_to_shells 中缺少反向索引条目", "壳验证失败",
          {shell_id.value, face_id.value});
    }
    const bool lists_this_shell =
        std::any_of(fts_it->second.begin(), fts_it->second.end(),
                    [sv = shell_id.value](std::uint64_t x) { return x == sv; });
    if (!lists_this_shell) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "壳验证失败：face_to_shells 未列出本壳（索引与壳成员不一致）",
          "壳验证失败", {shell_id.value, face_id.value});
    }
  }
  for (const auto face_id : shell_it->second.faces) {
    const auto result = validate_face(face_id);
    if (result.status != StatusCode::Ok) {
      // Preserve the underlying diagnostic details from face validation,
      // instead of masking them as a generic shell error.
      return result;
    }
  }

  auto source_valid = validate_shell_sources(shell_id);
  if (source_valid.status != StatusCode::Ok) {
    return source_valid;
  }
  // 注意：TopoCore 的“结构校验”不强制壳必须闭合（闭合/非流形属于更强约束，
  // 在 Heal/Validation 的 Strict 模式中作为硬门禁）。这里给出可诊断的告警，避免静默通过。
  std::unordered_map<std::uint64_t, std::uint64_t> edge_use_count;
  std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> edge_to_faces;
  for (const auto face_id : shell_it->second.faces) {
    const auto face_it = state_->faces.find(face_id.value);
    if (face_it == state_->faces.end()) {
      continue;
    }
    std::vector<LoopId> loops;
    loops.push_back(face_it->second.outer_loop);
    loops.insert(loops.end(), face_it->second.inner_loops.begin(),
                 face_it->second.inner_loops.end());
    for (const auto loop_id : loops) {
      const auto loop_it = state_->loops.find(loop_id.value);
      if (loop_it == state_->loops.end()) {
        continue;
      }
      for (const auto coedge_id : loop_it->second.coedges) {
        const auto coedge_it = state_->coedges.find(coedge_id.value);
        if (coedge_it == state_->coedges.end()) {
          continue;
        }
        ++edge_use_count[coedge_it->second.edge_id.value];
        edge_to_faces[coedge_it->second.edge_id.value].push_back(face_id.value);
      }
    }
  }

  const auto diag = state_->create_diagnostic("壳验证通过");
  Result<void> out = ok_void(diag);

  // Duplicate face rule (stronger relation consistency, Stage 2):
  // Report (as warning) when two different FaceIds in the same shell share the same surface and the same boundary loops.
  // NOTE: Keep as warning to avoid masking Strict closedness diagnostics (e.g., non-manifold edges).
  {
    std::unordered_map<std::string, FaceId> seen;
    seen.reserve(shell_it->second.faces.size());
    for (const auto face_id : shell_it->second.faces) {
      const auto face_it = state_->faces.find(face_id.value);
      if (face_it == state_->faces.end()) {
        continue;
      }
      std::vector<std::uint64_t> inner;
      inner.reserve(face_it->second.inner_loops.size());
      for (const auto loop_id : face_it->second.inner_loops) {
        inner.push_back(loop_id.value);
      }
      std::sort(inner.begin(), inner.end());
      std::string sig;
      sig.reserve(64 + inner.size() * 12);
      sig += std::to_string(face_it->second.surface_id.value);
      sig += "|o=";
      sig += std::to_string(face_it->second.outer_loop.value);
      sig += "|i=";
      for (const auto v : inner) {
        sig += std::to_string(v);
        sig += ",";
      }
      const auto it = seen.find(sig);
      if (it != seen.end() && it->second.value != face_id.value) {
        const auto msg =
            "壳可能包含重复面：face_a=" + std::to_string(it->second.value) +
            ", face_b=" + std::to_string(face_id.value) +
            ", outer_loop=" + std::to_string(face_it->second.outer_loop.value);
        out.warnings.push_back(detail::make_warning(diag_codes::kTopoDuplicateFaceInShell, msg));
        auto issue = detail::make_warning_issue(diag_codes::kTopoDuplicateFaceInShell, msg);
        issue.related_entities = {shell_id.value, it->second.value, face_id.value,
                                  face_it->second.outer_loop.value};
        state_->append_diagnostic_issue(diag, std::move(issue));
      } else {
        seen.emplace(std::move(sig), face_id);
      }
    }
  }

  // Connectivity rule (industrial-kernel inspired, best-effort):
  // If the shell's faces form multiple disconnected components (by shared edges), report as warning.
  {
    std::unordered_map<std::uint64_t, std::size_t> face_index;
    face_index.reserve(shell_it->second.faces.size());
    for (std::size_t i = 0; i < shell_it->second.faces.size(); ++i) {
      face_index.emplace(shell_it->second.faces[i].value, i);
    }
    const auto n = shell_it->second.faces.size();
    std::vector<std::vector<std::size_t>> adj(n);
    for (auto& v : adj) v.reserve(4);

    for (auto& [edge_value, faces] : edge_to_faces) {
      if (faces.size() < 2) continue;
      // Dedup faces per edge (in case a face references same edge multiple times in degenerate data).
      std::sort(faces.begin(), faces.end());
      faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
      for (std::size_t i = 0; i + 1 < faces.size(); ++i) {
        for (std::size_t j = i + 1; j < faces.size(); ++j) {
          const auto it_i = face_index.find(faces[i]);
          const auto it_j = face_index.find(faces[j]);
          if (it_i == face_index.end() || it_j == face_index.end()) continue;
          adj[it_i->second].push_back(it_j->second);
          adj[it_j->second].push_back(it_i->second);
        }
      }
    }

    std::vector<char> visited(n, 0);
    std::size_t components = 0;
    for (std::size_t i = 0; i < n; ++i) {
      if (visited[i]) continue;
      ++components;
      std::vector<std::size_t> stack;
      stack.push_back(i);
      visited[i] = 1;
      while (!stack.empty()) {
        const auto u = stack.back();
        stack.pop_back();
        for (const auto v : adj[u]) {
          if (!visited[v]) {
            visited[v] = 1;
            stack.push_back(v);
          }
        }
      }
    }
    if (components > 1) {
      const auto msg = "壳可能不连通：components=" + std::to_string(components) +
                       ", face_count=" + std::to_string(n);
      out.warnings.push_back(detail::make_warning(diag_codes::kTopoShellDisconnected, msg));
      auto issue = detail::make_warning_issue(diag_codes::kTopoShellDisconnected, msg);
      issue.related_entities = {shell_id.value};
      state_->append_diagnostic_issue(diag, std::move(issue));
    }
  }

  for (const auto& [edge_value, use_count] : edge_use_count) {
    if (use_count < 2) {
      const auto msg = "壳可能未闭合：边 " + std::to_string(edge_value) +
                       " 使用次数为 " + std::to_string(use_count);
      out.warnings.push_back(detail::make_warning(diag_codes::kTopoOpenBoundary, msg));
      state_->append_diagnostic_issue(
          diag, detail::make_warning_issue(diag_codes::kTopoOpenBoundary, msg));
    } else if (use_count > 2) {
      const auto msg = "壳可能存在非流形边：边 " + std::to_string(edge_value) +
                       " 使用次数为 " + std::to_string(use_count);
      out.warnings.push_back(detail::make_warning(diag_codes::kTopoNonManifoldEdge, msg));
      state_->append_diagnostic_issue(
          diag, detail::make_warning_issue(diag_codes::kTopoNonManifoldEdge, msg));
    }
  }
  return out;
}

Result<void>
TopologyValidationService::validate_shell_closedness(ShellId shell_id) const {
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "壳闭合性验证失败：目标壳不存在",
                               "壳闭合性验证失败");
  }
  if (shell_it->second.faces.empty()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "壳闭合性验证失败：壳不包含任何面",
                               "壳闭合性验证失败");
  }

  // 复用与 validate_shell 相同的计数逻辑，但在此处作为硬门禁返回失败。
  std::unordered_map<std::uint64_t, std::uint64_t> edge_use_count;
  std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> edge_to_faces;
  for (const auto face_id : shell_it->second.faces) {
    const auto face_it = state_->faces.find(face_id.value);
    if (face_it == state_->faces.end()) {
      continue;
    }
    std::vector<LoopId> loops;
    loops.push_back(face_it->second.outer_loop);
    loops.insert(loops.end(), face_it->second.inner_loops.begin(),
                 face_it->second.inner_loops.end());
    for (const auto loop_id : loops) {
      const auto loop_it = state_->loops.find(loop_id.value);
      if (loop_it == state_->loops.end()) {
        continue;
      }
      for (const auto coedge_id : loop_it->second.coedges) {
        const auto coedge_it = state_->coedges.find(coedge_id.value);
        if (coedge_it == state_->coedges.end()) {
          continue;
        }
        ++edge_use_count[coedge_it->second.edge_id.value];
        edge_to_faces[coedge_it->second.edge_id.value].push_back(face_id.value);
      }
    }
  }

  const auto append_face_outer_loops = [&](std::uint64_t ev, std::vector<std::uint64_t>& rel) {
    const auto itf = edge_to_faces.find(ev);
    if (itf == edge_to_faces.end()) {
      return;
    }
    std::vector<std::uint64_t> vf = itf->second;
    std::sort(vf.begin(), vf.end());
    vf.erase(std::unique(vf.begin(), vf.end()), vf.end());
    const std::size_t lim = std::min<std::size_t>(vf.size(), 3);
    for (std::size_t i = 0; i < lim; ++i) {
      rel.push_back(vf[i]);
      const auto fi = state_->faces.find(vf[i]);
      if (fi != state_->faces.end()) {
        rel.push_back(fi->second.outer_loop.value);
      }
    }
  };

  for (const auto& [edge_value, use_count] : edge_use_count) {
    auto fit = edge_to_faces.find(edge_value);
    if (fit != edge_to_faces.end()) {
      auto& faces = fit->second;
      std::sort(faces.begin(), faces.end());
      faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
      if (faces.size() < 2) {
        const auto msg = "壳未闭合：边 " + std::to_string(edge_value) +
                         " 未被两侧不同拓扑面使用（unique_faces=" +
                         std::to_string(faces.size()) + ", use_count=" +
                         std::to_string(use_count) + ")";
        std::vector<std::uint64_t> related {shell_id.value, edge_value};
        append_face_outer_loops(edge_value, related);
        return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                   diag_codes::kTopoOpenBoundary, msg,
                                   "壳未闭合",
                                   std::move(related));
      }
      if (faces.size() > 2) {
        const auto msg = "壳非流形：边 " + std::to_string(edge_value) +
                         " 被超过两个拓扑面共享（unique_faces=" +
                         std::to_string(faces.size()) + ", use_count=" +
                         std::to_string(use_count) + ")";
        std::vector<std::uint64_t> related {shell_id.value, edge_value};
        append_face_outer_loops(edge_value, related);
        return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                   diag_codes::kTopoNonManifoldEdge, msg,
                                   "壳非流形",
                                   std::move(related));
      }
    }
    if (use_count < 2) {
      const auto msg = "壳未闭合：边 " + std::to_string(edge_value) +
                       " 使用次数为 " + std::to_string(use_count);
      std::vector<std::uint64_t> related {shell_id.value, edge_value};
      append_face_outer_loops(edge_value, related);
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoOpenBoundary, msg,
                                 "壳未闭合",
                                 std::move(related));
    }
    if (use_count > 2) {
      const auto msg = "壳非流形：边 " + std::to_string(edge_value) +
                       " 使用次数为 " + std::to_string(use_count);
      std::vector<std::uint64_t> related {shell_id.value, edge_value};
      append_face_outer_loops(edge_value, related);
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoNonManifoldEdge, msg,
                                 "壳非流形",
                                 std::move(related));
    }
  }
  return ok_void(state_->create_diagnostic("壳闭合性验证通过"));
}

Result<void>
TopologyValidationService::validate_body_sources(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kCoreInvalidHandle,
        "体来源验证失败：目标体不存在", "体来源验证失败");
  }
  if (has_duplicate_ids(
          std::span<const BodyId>(body_it->second.source_bodies)) ||
      !validate_source_bodies_exist(*state_, body_it->second.source_bodies)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoSourceRefInvalid,
        "体来源验证失败：来源体引用非法", "体来源验证失败");
  }
  if (has_duplicate_ids(
          std::span<const ShellId>(body_it->second.source_shells)) ||
      !validate_source_shells_exist(*state_, body_it->second.source_shells)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoSourceRefInvalid,
        "体来源验证失败：来源壳引用非法", "体来源验证失败");
  }
  if (has_duplicate_ids(
          std::span<const FaceId>(body_it->second.source_faces)) ||
      !validate_source_faces_exist(*state_, body_it->second.source_faces)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoSourceRefInvalid,
        "体来源验证失败：来源面引用非法", "体来源验证失败");
  }
  return ok_void(state_->create_diagnostic("体来源验证通过"));
}

Result<void>
TopologyValidationService::validate_body_bbox(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kCoreInvalidHandle,
        "体包围盒验证失败：目标体不存在", "体包围盒验证失败");
  }
  const auto bbox = compute_body_bbox(*state_, body_it->second.shells);
  if (!bbox.is_valid) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "体包围盒验证失败：无法从壳推导有效包围盒", "体包围盒验证失败");
  }
  return ok_void(state_->create_diagnostic("体包围盒验证通过"));
}

Result<void> TopologyValidationService::validate_body(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "体验证失败：目标实体不存在", "体验证失败");
  }
  if (body_it->second.rep_kind == RepKind::ExactBRep &&
      body_it->second.shells.empty()) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "体验证失败：精确 B-Rep 体不包含任何壳", "体验证失败");
  }
  if (has_duplicate_ids(std::span<const ShellId>(body_it->second.shells))) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "体验证失败：体包含重复壳引用", "体验证失败");
  }
  // Body ↔ shell_to_bodies: each shell owned by the body must reverse-index this body.
  for (const auto shell_id : body_it->second.shells) {
    if (!detail::has_shell(*state_, shell_id)) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "体验证失败：体引用了不存在的壳", "体验证失败",
          {body_id.value, shell_id.value});
    }
    const auto stb_it = state_->shell_to_bodies.find(shell_id.value);
    if (stb_it == state_->shell_to_bodies.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "体验证失败：壳在 shell_to_bodies 中缺少反向索引条目", "体验证失败",
          {body_id.value, shell_id.value});
    }
    const bool lists_this_body =
        std::any_of(stb_it->second.begin(), stb_it->second.end(),
                    [bv = body_id.value](std::uint64_t x) { return x == bv; });
    if (!lists_this_body) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "体验证失败：shell_to_bodies 未列出本体（索引与体成员不一致）",
          "体验证失败", {body_id.value, shell_id.value});
    }
  }
  for (const auto shell_id : body_it->second.shells) {
    const auto result = validate_shell(shell_id);
    if (result.status != StatusCode::Ok) {
      // Preserve the underlying diagnostic details from shell/face validation.
      return result;
    }
  }
  auto source_valid = validate_body_sources(body_id);
  if (source_valid.status != StatusCode::Ok) {
    return source_valid;
  }
  auto bbox_valid = validate_body_bbox(body_id);
  if (bbox_valid.status != StatusCode::Ok) {
    return bbox_valid;
  }
  const auto idx_valid = validate_body_topology_indices(body_id);
  if (idx_valid.status != StatusCode::Ok) {
    return idx_valid;
  }
  return ok_void(state_->create_diagnostic("体验证通过"));
}

Result<void>
TopologyValidationService::validate_body_topology_indices(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "体索引验证失败：目标实体不存在", "体索引验证失败");
  }
  std::unordered_set<std::uint64_t> face_ids;
  for (const auto shell_id : body_it->second.shells) {
    const auto shell_it = state_->shells.find(shell_id.value);
    if (shell_it == state_->shells.end()) {
      continue;
    }
    for (const auto face_id : shell_it->second.faces) {
      face_ids.insert(face_id.value);
    }
  }
  if (face_ids.empty()) {
    return ok_void(state_->create_diagnostic("体索引验证跳过（无拓扑面）"));
  }
  std::unordered_set<std::uint64_t> loop_ids;
  for (const auto face_value : face_ids) {
    const auto fit = state_->faces.find(face_value);
    if (fit == state_->faces.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "体索引验证失败：壳引用了不存在的面", "体索引验证失败",
          {body_id.value, face_value});
    }
    loop_ids.insert(fit->second.outer_loop.value);
    for (const auto inner : fit->second.inner_loops) {
      loop_ids.insert(inner.value);
    }
  }
  std::unordered_set<std::uint64_t> coedge_ids;
  std::unordered_set<std::uint64_t> edge_ids;
  for (const auto loop_value : loop_ids) {
    const auto lit = state_->loops.find(loop_value);
    if (lit == state_->loops.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "体索引验证失败：面引用了不存在的环", "体索引验证失败",
          {body_id.value, loop_value});
    }
    for (const auto coedge_id : lit->second.coedges) {
      coedge_ids.insert(coedge_id.value);
      const auto cit = state_->coedges.find(coedge_id.value);
      if (cit == state_->coedges.end()) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
            "体索引验证失败：环引用了不存在的定向边", "体索引验证失败",
            {loop_value, coedge_id.value});
      }
      const auto cl = state_->coedge_to_loop.find(coedge_id.value);
      if (cl == state_->coedge_to_loop.end() || cl->second != loop_value) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "体索引验证失败：coedge_to_loop 与环成员不一致", "体索引验证失败",
            {coedge_id.value, loop_value});
      }
      edge_ids.insert(cit->second.edge_id.value);
    }
  }
  for (const auto loop_value : loop_ids) {
    std::uint64_t owner_face = 0;
    unsigned owner_count = 0;
    for (const auto face_value : face_ids) {
      const auto& fr = state_->faces.at(face_value);
      if (face_record_references_loop(fr, loop_value)) {
        ++owner_count;
        owner_face = face_value;
      }
    }
    if (owner_count != 1U) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          owner_count == 0 ? diag_codes::kTopoOrphanLoop
                           : diag_codes::kTopoInvariantBroken,
          "体索引验证失败：环在体的面集合中缺少唯一所属面", "体索引验证失败",
          {body_id.value, loop_value});
    }
    const auto ltf = state_->loop_to_faces.find(loop_value);
    if (ltf == state_->loop_to_faces.end() || ltf->second.empty()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoOrphanLoop,
          "体索引验证失败：loop_to_faces 缺少环条目", "体索引验证失败",
          {loop_value});
    }
    std::unordered_set<std::uint64_t> listed;
    for (const auto fv : ltf->second) {
      if (!listed.insert(fv).second) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "体索引验证失败：loop_to_faces 存在重复面", "体索引验证失败",
            {loop_value, fv});
      }
    }
    if (listed.size() != 1U || listed.count(owner_face) == 0U) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "体索引验证失败：loop_to_faces 与体的所属面不一致", "体索引验证失败",
          {loop_value, owner_face});
    }
  }
  for (const auto edge_value : edge_ids) {
    if (state_->edges.find(edge_value) == state_->edges.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoCurveTopologyMismatch,
          "体索引验证失败：定向边引用了不存在的边", "体索引验证失败",
          {edge_value});
    }
    const auto ec_it = state_->edge_to_coedges.find(edge_value);
    if (ec_it == state_->edge_to_coedges.end() || ec_it->second.empty()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoDanglingEdge,
          "体索引验证失败：边缺少 edge_to_coedges 反向索引", "体索引验证失败",
          {edge_value, body_id.value});
    }
    std::unordered_set<std::uint64_t> rev_seen;
    rev_seen.reserve(ec_it->second.size());
    for (const auto co_value : ec_it->second) {
      if (!rev_seen.insert(co_value).second) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "体索引验证失败：edge_to_coedges 存在重复定向边", "体索引验证失败",
            {edge_value, co_value});
      }
    }
    for (const auto co_value : coedge_ids) {
      const auto cit = state_->coedges.find(co_value);
      if (cit == state_->coedges.end() ||
          cit->second.edge_id.value != edge_value) {
        continue;
      }
      if (rev_seen.count(co_value) == 0U) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "体索引验证失败：体的共边未列入 edge_to_coedges", "体索引验证失败",
            {edge_value, co_value});
      }
    }
  }
  // Shell ↔ face_to_shells: every (body shell, face) membership must appear in reverse index.
  for (const auto face_value : face_ids) {
    for (const auto shell_id : body_it->second.shells) {
      const auto shell_it = state_->shells.find(shell_id.value);
      if (shell_it == state_->shells.end()) {
        continue;
      }
      const bool shell_has_face = std::any_of(
          shell_it->second.faces.begin(), shell_it->second.faces.end(),
          [face_value](FaceId fid) { return fid.value == face_value; });
      if (!shell_has_face) {
        continue;
      }
      const auto fts_it = state_->face_to_shells.find(face_value);
      if (fts_it == state_->face_to_shells.end()) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "体索引验证失败：face_to_shells 缺少面条目（壳已引用该面）",
            "体索引验证失败", {body_id.value, face_value, shell_id.value});
      }
      const bool lists_shell = std::any_of(
          fts_it->second.begin(), fts_it->second.end(),
          [sv = shell_id.value](std::uint64_t x) { return x == sv; });
      if (!lists_shell) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "体索引验证失败：face_to_shells 未列出体的壳", "体索引验证失败",
            {body_id.value, face_value, shell_id.value});
      }
    }
  }
  return ok_void(state_->create_diagnostic("体拓扑索引验证通过"));
}

Result<void>
TopologyValidationService::validate_body_closedness(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "体闭合性验证失败：目标实体不存在",
                               "体闭合性验证失败");
  }
  for (const auto shell_id : body_it->second.shells) {
    const auto r = validate_shell_closedness(shell_id);
    if (r.status != StatusCode::Ok) {
      return r;
    }
  }
  return ok_void(state_->create_diagnostic("体闭合性验证通过"));
}
