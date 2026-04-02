TopologyValidationService::TopologyValidationService(
    std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

Result<void>
TopologyValidationService::validate_vertex(VertexId vertex_id) const {
  const auto vertex_it = state_->vertices.find(vertex_id.value);
  if (vertex_it == state_->vertices.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kCoreInvalidHandle,
                               "顶点验证失败：目标顶点不存在", "顶点验证失败");
  }
  const auto &p = vertex_it->second.point;
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kCoreParameterOutOfRange,
                               "顶点验证失败：顶点坐标非法", "顶点验证失败");
  }
  const bool used_by_edge = std::any_of(
      state_->edges.begin(), state_->edges.end(),
      [vid = vertex_id.value](const auto &entry) {
        const auto &e = entry.second;
        return e.v0.value == vid || e.v1.value == vid;
      });
  if (!used_by_edge) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoDanglingVertex,
                               "顶点验证失败：悬挂顶点（未作为任何边的端点）",
                               "顶点验证失败", {vertex_id.value});
  }
  return ok_void(state_->create_diagnostic("顶点验证通过"));
}

Result<void> TopologyValidationService::validate_edge(EdgeId edge_id) const {
  const auto edge_it = state_->edges.find(edge_id.value);
  if (edge_it == state_->edges.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoCurveTopologyMismatch,
                               "边验证失败：目标边不存在", "边验证失败",
                               {edge_id.value});
  }
  {
    const auto owners_it = state_->edge_to_coedges.find(edge_id.value);
    if (owners_it == state_->edge_to_coedges.end() || owners_it->second.empty()) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoDanglingEdge,
                                 "边验证失败：检测到悬挂边（未被任何定向边使用）",
                                 "边验证失败", {edge_id.value});
    }
  }
  if (!detail::has_curve(*state_, edge_it->second.curve_id)) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoCurveTopologyMismatch,
                               "边验证失败：边引用的曲线不存在", "边验证失败",
                               {edge_id.value});
  }
  if (!detail::has_vertex(*state_, edge_it->second.v0) ||
      !detail::has_vertex(*state_, edge_it->second.v1)) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoCurveTopologyMismatch,
                               "边验证失败：边引用的端点不存在", "边验证失败",
                               {edge_id.value, edge_it->second.v0.value,
                                edge_it->second.v1.value});
  }
  if (edge_it->second.v0.value == edge_it->second.v1.value) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoCurveTopologyMismatch,
                               "边验证失败：边的两个端点不能相同",
                               "边验证失败", {edge_id.value, edge_it->second.v0.value});
  }
  return ok_void(state_->create_diagnostic("边验证通过"));
}

Result<void>
TopologyValidationService::validate_coedge(CoedgeId coedge_id) const {
  const auto coedge_it = state_->coedges.find(coedge_id.value);
  if (coedge_it == state_->coedges.end()) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kCoreInvalidHandle,
        "定向边验证失败：目标定向边不存在", "定向边验证失败");
  }
  const auto edge_valid = validate_edge(coedge_it->second.edge_id);
  if (edge_valid.status != StatusCode::Ok) {
    return edge_valid;
  }
  // Reverse indices: must match edge_to_coedges / coedge_to_loop / loop membership
  // (same invariants as validate_indices_consistency, scoped to one coedge).
  const auto owners_it =
      state_->edge_to_coedges.find(coedge_it->second.edge_id.value);
  if (owners_it == state_->edge_to_coedges.end()) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoRelationInconsistent,
        "定向边验证失败：边缺少 edge_to_coedges 反向索引", "定向边验证失败",
        {coedge_id.value, coedge_it->second.edge_id.value});
  }
  const bool on_edge_list =
      std::any_of(owners_it->second.begin(), owners_it->second.end(),
                  [cv = coedge_id.value](std::uint64_t x) { return x == cv; });
  if (!on_edge_list) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoRelationInconsistent,
        "定向边验证失败：edge_to_coedges 未列出本定向边", "定向边验证失败",
        {coedge_id.value, coedge_it->second.edge_id.value});
  }
  // 尚未加入任何环的定向边（事务中途）不会有 coedge_to_loop；全量门禁由
  // validate_indices_consistency / validate_loop 承担。
  const auto cl = state_->coedge_to_loop.find(coedge_id.value);
  if (cl != state_->coedge_to_loop.end()) {
    const auto lit = state_->loops.find(cl->second);
    if (lit == state_->loops.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoRelationInconsistent,
          "定向边验证失败：coedge_to_loop 指向不存在的环", "定向边验证失败",
          {coedge_id.value, cl->second});
    }
    const bool in_loop = std::any_of(
        lit->second.coedges.begin(), lit->second.coedges.end(),
        [cv = coedge_id.value](CoedgeId cid) { return cid.value == cv; });
    if (!in_loop) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoRelationInconsistent,
          "定向边验证失败：所属环成员列表不包含本定向边", "定向边验证失败",
          {coedge_id.value, cl->second});
    }
  }
  if (coedge_it->second.pcurve_id.value != 0 &&
      !detail::has_pcurve(*state_, coedge_it->second.pcurve_id)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoCurveTopologyMismatch,
        "定向边验证失败：定向边引用的参数曲线不存在", "定向边验证失败");
  }
  return ok_void(state_->create_diagnostic("定向边验证通过"));
}

Result<void> TopologyValidationService::validate_loop(LoopId loop_id) const {
  std::string reason;
  if (!validate_loop_id(*state_, loop_id, reason)) {
    std::string_view code = diag_codes::kTopoLoopNotClosed;
    if (reason == "环包含重复边引用" || reason == "环包含重复定向边引用") {
      code = diag_codes::kTopoLoopDuplicateEdge;
    }
    return detail::failed_void(*state_, StatusCode::InvalidTopology, code,
                               "环验证失败：" + reason, "环验证失败");
  }
  const auto loop_it = state_->loops.find(loop_id.value);
  if (loop_it == state_->loops.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoLoopNotClosed,
                               "环验证失败：目标环不存在", "环验证失败");
  }
  // 已绑定拓扑面（loop_to_faces 有非空条目）的边界环：默认至少三条共边。
  // 例外：两条共边且共用同一条空间曲线（典型为圆/柱/球上的闭合双弧 digon），保留 trim repair 等用例。
  {
    const auto ltf_bound = state_->loop_to_faces.find(loop_id.value);
    if (ltf_bound != state_->loop_to_faces.end() &&
        !ltf_bound->second.empty() &&
        loop_it->second.coedges.size() < 3) {
      bool allow_two_coedge_digon = false;
      if (loop_it->second.coedges.size() == 2) {
        const auto c0_it =
            state_->coedges.find(loop_it->second.coedges[0].value);
        const auto c1_it =
            state_->coedges.find(loop_it->second.coedges[1].value);
        if (c0_it != state_->coedges.end() && c1_it != state_->coedges.end()) {
          const auto e0_it =
              state_->edges.find(c0_it->second.edge_id.value);
          const auto e1_it =
              state_->edges.find(c1_it->second.edge_id.value);
          if (e0_it != state_->edges.end() && e1_it != state_->edges.end() &&
              e0_it->second.curve_id.value != 0 &&
              e0_it->second.curve_id.value == e1_it->second.curve_id.value) {
            allow_two_coedge_digon = true;
          }
        }
      }
      if (!allow_two_coedge_digon) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoLoopNotClosed,
            "环验证失败：已绑定面的边界环至少需要三条共边", "环验证失败",
            {loop_id.value});
      }
    }
  }
  // coedge_to_loop 必须与环成员一致（索引损坏或跨环复用定向边时给出可诊断失败）。
  for (const auto coedge_id : loop_it->second.coedges) {
    const auto cl = state_->coedge_to_loop.find(coedge_id.value);
    if (cl == state_->coedge_to_loop.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoDanglingCoedge,
          "环验证失败：定向边缺少 coedge_to_loop 反向索引", "环验证失败",
          {loop_id.value, coedge_id.value});
    }
    if (cl->second != loop_id.value) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoCoedgeAlreadyOwned,
          "环验证失败：定向边索引所属环与当前环不一致（coedge=" +
              std::to_string(coedge_id.value) + ", indexed_loop=" +
              std::to_string(cl->second) + ")",
          "环验证失败",
          {loop_id.value, coedge_id.value, cl->second});
    }
  }
  for (const auto coedge_id : loop_it->second.coedges) {
    const auto coedge_valid = validate_coedge(coedge_id);
    if (coedge_valid.status != StatusCode::Ok) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoLoopNotClosed,
                                 "环验证失败：环包含非法定向边", "环验证失败");
    }
  }
  // loop_to_faces：若索引中已有本环条目，必须与面记录互指（与 validate_indices_consistency
  // 对齐；无条目时表示环尚未被任何面引用，事务中间态不判错）。
  const auto ltf_loop = state_->loop_to_faces.find(loop_id.value);
  if (ltf_loop != state_->loop_to_faces.end() &&
      !ltf_loop->second.empty()) {
    std::unordered_set<std::uint64_t> seen_owner_face;
    seen_owner_face.reserve(ltf_loop->second.size());
    for (const auto face_value : ltf_loop->second) {
      if (!seen_owner_face.insert(face_value).second) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "环验证失败：loop_to_faces 存在重复面条目", "环验证失败",
            {loop_id.value, face_value});
      }
      const auto fit = state_->faces.find(face_value);
      if (fit == state_->faces.end()) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
            "环验证失败：loop_to_faces 引用不存在的面", "环验证失败",
            {loop_id.value, face_value});
      }
      if (!face_record_references_loop(fit->second, loop_id.value)) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "环验证失败：loop_to_faces 与面记录不一致", "环验证失败",
            {loop_id.value, face_value});
      }
    }
    if (ltf_loop->second.size() > 1U) {
      auto it = ltf_loop->second.begin();
      const std::uint64_t fa = *it++;
      const std::uint64_t fb = *it;
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "环验证失败：同一环被多个面引用（loop_to_faces）", "环验证失败",
          {loop_id.value, fa, fb});
    }
  }
  return ok_void(state_->create_diagnostic("环验证通过"));
}

Result<void>
TopologyValidationService::validate_loop_pcurve_closedness(LoopId loop_id) const {
  const auto loop_it = state_->loops.find(loop_id.value);
  if (loop_it == state_->loops.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoLoopNotClosed,
                               "参数环验证失败：目标环不存在", "参数环验证失败");
  }
  if (loop_it->second.coedges.size() < 2) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoLoopNotClosed,
                               "参数环验证失败：环共边数量不足", "参数环验证失败");
  }

  const auto tol = detail::resolve_linear_tolerance(0.0, state_->config.tolerance);
  const auto dist2 = [](const Point2 &a, const Point2 &b) -> Scalar {
    const auto dx = a.x - b.x;
    const auto dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
  };

  // Build ordered UV endpoints per coedge.
  std::vector<std::pair<Point2, Point2>> segs;
  std::vector<CoedgeId> coedge_ids;
  segs.reserve(loop_it->second.coedges.size());
  coedge_ids.reserve(loop_it->second.coedges.size());
  for (const auto coedge_id : loop_it->second.coedges) {
    const auto coedge_it = state_->coedges.find(coedge_id.value);
    if (coedge_it == state_->coedges.end()) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoLoopNotClosed,
                                 "参数环验证失败：环引用的定向边不存在",
                                 "参数环验证失败");
    }
    const auto pc = coedge_it->second.pcurve_id;
    if (pc.value == 0) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoCurveTopologyMismatch,
                                 "参数环验证失败：定向边未绑定参数曲线",
                                 "参数环验证失败",
                                 {loop_id.value, coedge_id.value});
    }
    if (!detail::has_pcurve(*state_, pc)) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoCurveTopologyMismatch,
                                 "参数环验证失败：定向边绑定的参数曲线不存在",
                                 "参数环验证失败",
                                 {loop_id.value, coedge_id.value, pc.value});
    }
    const auto &rec = state_->pcurves.at(pc.value);
    if (rec.poles.size() < 2) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoCurveTopologyMismatch,
                                 "参数环验证失败：参数曲线点数不足",
                                 "参数环验证失败",
                                 {loop_id.value, coedge_id.value, pc.value});
    }
    const auto seg_tol = std::max<Scalar>(tol * static_cast<Scalar>(1e-9),
                                         static_cast<Scalar>(1e-18));
    for (std::size_t pi = 1; pi < rec.poles.size(); ++pi) {
      const auto dx = rec.poles[pi].x - rec.poles[pi - 1].x;
      const auto dy = rec.poles[pi].y - rec.poles[pi - 1].y;
      const auto seg_len = std::sqrt(dx * dx + dy * dy);
      if (!std::isfinite(seg_len) || seg_len <= seg_tol) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoCurveTopologyMismatch,
            "参数环验证失败：参数曲线折线存在退化段（相邻控制点过近）",
            "参数环验证失败",
            {loop_id.value, coedge_id.value, pc.value});
      }
    }
    Point2 a = rec.poles.front();
    Point2 b = rec.poles.back();
    if (coedge_it->second.reversed) {
      std::swap(a, b);
    }
    segs.push_back({a, b});
    coedge_ids.push_back(coedge_id);
  }

  for (std::size_t i = 0; i < segs.size(); ++i) {
    const auto &cur = segs[i];
    const auto &next = segs[(i + 1) % segs.size()];
    const auto d = dist2(cur.second, next.first);
    if (d > tol) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoLoopNotClosed,
                                 "参数环验证失败：参数环不连续或未闭合（loop=" +
                                     std::to_string(loop_id.value) +
                                     ", break_at_coedge=" +
                                     std::to_string(coedge_ids[i].value) +
                                     " -> next_coedge=" +
                                     std::to_string(
                                         coedge_ids[(i + 1) % coedge_ids.size()]
                                             .value) +
                                     ", gap=" + std::to_string(d) + ")",
                                 "参数环验证失败",
                                 {loop_id.value, coedge_ids[i].value,
                                  coedge_ids[(i + 1) % coedge_ids.size()].value});
    }
  }
  return ok_void(state_->create_diagnostic("参数环验证通过"));
}

Result<void>
TopologyValidationService::validate_face_trim_consistency(FaceId face_id) const {
  const auto face_it = state_->faces.find(face_id.value);
  if (face_it == state_->faces.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面修剪验证失败：目标面不存在", "面修剪验证失败");
  }
  const auto inspect_loop = [this](LoopId loop_id, std::size_t &with_pc,
                                   std::size_t &without_pc,
                                   std::string &reason) -> bool {
    const auto loop_it = state_->loops.find(loop_id.value);
    if (loop_it == state_->loops.end()) {
      reason = "面引用了不存在的环";
      return false;
    }
    for (const auto coedge_id : loop_it->second.coedges) {
      const auto coedge_it = state_->coedges.find(coedge_id.value);
      if (coedge_it == state_->coedges.end()) {
        reason = "环引用了不存在的定向边";
        return false;
      }
      if (coedge_it->second.pcurve_id.value == 0) {
        ++without_pc;
      } else {
        ++with_pc;
      }
    }
    return true;
  };

  std::size_t with_pc = 0;
  std::size_t without_pc = 0;
  std::string reason;
  if (!inspect_loop(face_it->second.outer_loop, with_pc, without_pc, reason)) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面修剪验证失败：" + reason, "面修剪验证失败");
  }
  for (const auto loop_id : face_it->second.inner_loops) {
    if (!inspect_loop(loop_id, with_pc, without_pc, reason)) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoFaceOuterLoopInvalid,
                                 "面修剪验证失败：" + reason, "面修剪验证失败");
    }
  }

  // If no pcurves are provided at all, treat trim as "not available" and pass
  // (backward compatible with current Stage 2 state).
  if (with_pc == 0) {
    return ok_void(state_->create_diagnostic("面修剪验证通过（未提供参数曲线）"));
  }

  // If partially provided, it's inconsistent (cannot form a valid trim ring).
  if (without_pc != 0) {
    std::vector<std::uint64_t> related;
    related.push_back(face_id.value);
    auto collect_missing_pc_coedges = [&](LoopId loop_id) {
      const auto loop_it = state_->loops.find(loop_id.value);
      if (loop_it == state_->loops.end()) {
        return;
      }
      for (const auto coedge_id : loop_it->second.coedges) {
        const auto coedge_it = state_->coedges.find(coedge_id.value);
        if (coedge_it == state_->coedges.end()) {
          continue;
        }
        if (coedge_it->second.pcurve_id.value == 0) {
          related.push_back(coedge_id.value);
        }
      }
    };
    collect_missing_pc_coedges(face_it->second.outer_loop);
    for (const auto loop_id : face_it->second.inner_loops) {
      collect_missing_pc_coedges(loop_id);
    }
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoCurveTopologyMismatch,
                               "面修剪验证失败：参数曲线绑定不完整",
                               "面修剪验证失败",
                               std::move(related));
  }

  // All coedges have pcurves: enforce UV closedness for each loop.
  auto outer = validate_loop_pcurve_closedness(face_it->second.outer_loop);
  if (outer.status != StatusCode::Ok) {
    // Preserve detailed loop/coedge break information for engineering debugging.
    return outer;
  }
  for (const auto loop_id : face_it->second.inner_loops) {
    auto inner = validate_loop_pcurve_closedness(loop_id);
    if (inner.status != StatusCode::Ok) {
      return inner;
    }
  }

  // UV winding / signed-area: required when inner loops exist (hole orientation rule).
  // For outer-only full trim, also require a non-degenerate UV area when the outer loop has
  // at least three coedges. Two-coedge digons (cylinder/sphere caps) skip this gate — the same
  // digon exception as validate_loop() for bound faces.
  const auto tol_ori = detail::resolve_linear_tolerance(0.0, state_->config.tolerance);
  const auto compute_loop_signed_uv_area =
      [&](LoopId loop_id, Scalar &out_area, std::string &out_reason) -> bool {
      const auto lit = state_->loops.find(loop_id.value);
      if (lit == state_->loops.end()) {
        out_reason = "环不存在";
        return false;
      }
      std::vector<Point2> ring;
      ring.reserve(lit->second.coedges.size() + 1);
      for (const auto coedge_id : lit->second.coedges) {
        const auto ce_it = state_->coedges.find(coedge_id.value);
        if (ce_it == state_->coedges.end()) {
          out_reason = "定向边不存在";
          return false;
        }
        const auto pc_id = ce_it->second.pcurve_id;
        if (pc_id.value == 0 || !detail::has_pcurve(*state_, pc_id)) {
          out_reason = "缺少参数曲线";
          return false;
        }
        const auto &rec = state_->pcurves.at(pc_id.value);
        if (rec.poles.size() < 2) {
          out_reason = "参数曲线退化";
          return false;
        }
        Point2 a = rec.poles.front();
        Point2 b = rec.poles.back();
        if (ce_it->second.reversed) {
          std::swap(a, b);
        }
        if (ring.empty()) {
          ring.push_back(a);
          ring.push_back(b);
        } else {
          const auto dx = ring.back().x - a.x;
          const auto dy = ring.back().y - a.y;
          if (std::sqrt(dx * dx + dy * dy) > tol_ori) {
            out_reason = "环参数边界不连续";
            return false;
          }
          ring.push_back(b);
        }
      }
      if (ring.size() < 4) {
        out_reason = "环边数不足";
        return false;
      }
      const auto dx0 = ring.front().x - ring.back().x;
      const auto dy0 = ring.front().y - ring.back().y;
      if (std::sqrt(dx0 * dx0 + dy0 * dy0) > tol_ori) {
        out_reason = "环未闭合";
        return false;
      }
      Scalar sum2 = 0.0;
      for (std::size_t i = 0; i + 1 < ring.size(); ++i) {
        sum2 += ring[i].x * ring[i + 1].y - ring[i + 1].x * ring[i].y;
      }
      out_area = sum2 * 0.5;
      if (std::abs(out_area) <= tol_ori * tol_ori * static_cast<Scalar>(10)) {
        out_reason = "环有符号面积过小";
        return false;
      }
      return true;
    };

  const auto outer_lit = state_->loops.find(face_it->second.outer_loop.value);
  const std::size_t outer_coedge_n =
      outer_lit != state_->loops.end() ? outer_lit->second.coedges.size() : 0;
  const bool require_outer_uv_area =
      !face_it->second.inner_loops.empty() || outer_coedge_n >= 3;

  if (require_outer_uv_area) {
    Scalar outer_area = 0.0;
    std::string area_reason;
    if (!compute_loop_signed_uv_area(face_it->second.outer_loop, outer_area,
                                     area_reason)) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoFaceOuterLoopInvalid,
                                 "面修剪验证失败：外环方向不可判定（" + area_reason + "）",
                                 "面修剪验证失败",
                                 {face_id.value, face_it->second.outer_loop.value});
    }
    for (const auto inner_loop_id : face_it->second.inner_loops) {
      Scalar inner_area = 0.0;
      if (!compute_loop_signed_uv_area(inner_loop_id, inner_area, area_reason)) {
        return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                   diag_codes::kTopoFaceInnerLoopInvalid,
                                   "面修剪验证失败：内环方向不可判定（" + area_reason + "）",
                                   "面修剪验证失败",
                                   {face_id.value, inner_loop_id.value});
      }
      if ((outer_area > 0.0 && inner_area > 0.0) ||
          (outer_area < 0.0 && inner_area < 0.0)) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoLoopOrientationMismatch,
            "面修剪验证失败：内外环方向不符合规则（应相反）"
            " (face=" +
                std::to_string(face_id.value) + ", outer_loop=" +
                std::to_string(face_it->second.outer_loop.value) +
                ", inner_loop=" + std::to_string(inner_loop_id.value) + ")",
            "面修剪验证失败",
            {face_id.value, face_it->second.outer_loop.value, inner_loop_id.value});
      }
    }
  }

  // 全量 trim 且存在内环：内环 UV 多边形质心须落在外环 UV 区域内（与 Geo `Trimmed` 孔洞语义一致；
  // 周期性参数域上在端点一致但仍“绕开”有效修剪域时提供额外门禁）。
  if (!face_it->second.inner_loops.empty()) {
    const auto build_simple_uv_polygon =
        [&](LoopId loop_id, std::vector<Point2> &poly, std::string &r) -> bool {
      const auto lit = state_->loops.find(loop_id.value);
      if (lit == state_->loops.end()) {
        r = "环不存在";
        return false;
      }
      poly.clear();
      for (const auto coedge_id : lit->second.coedges) {
        const auto ce_it = state_->coedges.find(coedge_id.value);
        if (ce_it == state_->coedges.end()) {
          r = "定向边不存在";
          return false;
        }
        const auto pc_id = ce_it->second.pcurve_id;
        if (pc_id.value == 0 || !detail::has_pcurve(*state_, pc_id)) {
          r = "缺少参数曲线";
          return false;
        }
        const auto &rec = state_->pcurves.at(pc_id.value);
        if (rec.poles.size() < 2) {
          r = "参数曲线退化";
          return false;
        }
        Point2 a = rec.poles.front();
        Point2 b = rec.poles.back();
        if (ce_it->second.reversed) {
          std::swap(a, b);
        }
        if (poly.empty()) {
          poly.push_back(a);
          poly.push_back(b);
        } else {
          const auto dx = poly.back().x - a.x;
          const auto dy = poly.back().y - a.y;
          if (std::sqrt(dx * dx + dy * dy) > tol_ori) {
            r = "环参数边界不连续";
            return false;
          }
          poly.push_back(b);
        }
      }
      if (poly.size() < 4) {
        r = "环边数不足";
        return false;
      }
      const auto dx0 = poly.front().x - poly.back().x;
      const auto dy0 = poly.front().y - poly.back().y;
      if (std::sqrt(dx0 * dx0 + dy0 * dy0) > tol_ori) {
        r = "环未闭合";
        return false;
      }
      poly.pop_back();
      if (poly.size() < 3) {
        r = "环顶点不足";
        return false;
      }
      return true;
    };

    std::vector<Point2> outer_poly;
    std::string build_reason;
    if (!build_simple_uv_polygon(face_it->second.outer_loop, outer_poly, build_reason)) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoFaceOuterLoopInvalid,
          "面修剪验证失败：外环 UV 多边形不可构造（" + build_reason + "）",
          "面修剪验证失败", {face_id.value, face_it->second.outer_loop.value});
    }

    const auto uv_centroid = [](const std::vector<Point2> &v) -> Point2 {
      Scalar sx = 0.0;
      Scalar sy = 0.0;
      for (const auto &p : v) {
        sx += p.x;
        sy += p.y;
      }
      const Scalar inv =
          1.0 / static_cast<Scalar>(v.empty() ? 1 : v.size());
      return Point2{sx * inv, sy * inv};
    };

    for (const auto inner_loop_id : face_it->second.inner_loops) {
      std::vector<Point2> inner_poly;
      std::string ir;
      if (!build_simple_uv_polygon(inner_loop_id, inner_poly, ir)) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoFaceInnerLoopInvalid,
            "面修剪验证失败：内环 UV 多边形不可构造（" + ir + "）", "面修剪验证失败",
            {face_id.value, inner_loop_id.value});
      }
      const Point2 c = uv_centroid(inner_poly);
      if (!detail::point_in_polygon_2d_raycast(
              c, std::span<const Point2>(outer_poly.data(), outer_poly.size()))) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoFaceInnerLoopInvalid,
            "面修剪验证失败：内环 UV 代表点落在外环 UV 修剪域之外"
            " (face=" +
                std::to_string(face_id.value) + ", inner_loop=" +
                std::to_string(inner_loop_id.value) + ")",
            "面修剪验证失败",
            {face_id.value, face_it->second.outer_loop.value, inner_loop_id.value});
      }
    }
  }

  // PCurve <-> 3D：端点与内点采样一致性；全量 trim 下求值/反求失败视为硬失败（不再静默跳过）。
  SurfaceService surface_service{state_};
  PCurveService pcurve_service{state_};
  CurveService curve_service{state_};
  const auto tol = detail::resolve_linear_tolerance(0.0, state_->config.tolerance);
  const auto tol3 = tol;
  const auto dist2 = [](const Point2 &a, const Point2 &b) -> Scalar {
    const auto dx = a.x - b.x;
    const auto dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
  };
  const auto dist3 = [](const Point3 &a, const Point3 &b) -> Scalar {
    const auto dx = a.x - b.x;
    const auto dy = a.y - b.y;
    const auto dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  };
  auto check_loop = [&](LoopId loop_id) -> Result<void> {
    const auto loop_it = state_->loops.find(loop_id.value);
    if (loop_it == state_->loops.end()) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoFaceOuterLoopInvalid,
                                 "面修剪验证失败：面引用了不存在的环",
                                 "面修剪验证失败");
    }
    for (const auto coedge_id : loop_it->second.coedges) {
      const auto coedge_it = state_->coedges.find(coedge_id.value);
      if (coedge_it == state_->coedges.end()) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
            "面修剪验证失败：环成员缺少定向边记录", "面修剪验证失败",
            {face_id.value, loop_id.value, coedge_id.value});
      }
      const auto pc_id = coedge_it->second.pcurve_id;
      if (pc_id.value == 0 || !detail::has_pcurve(*state_, pc_id)) {
        continue;
      }
      const auto edge_it = state_->edges.find(coedge_it->second.edge_id.value);
      if (edge_it == state_->edges.end()) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoCurveTopologyMismatch,
            "面修剪验证失败：工业修剪要求 PCurve 共边具备有效拓扑边记录",
            "面修剪验证失败",
            {face_id.value, loop_id.value, coedge_id.value});
      }
      const auto curve_id = edge_it->second.curve_id;
      if (curve_id.value == 0 || !detail::has_curve(*state_, curve_id)) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoCurveTopologyMismatch,
            "面修剪验证失败：工业修剪要求 PCurve 共边绑定有效空间曲线",
            "面修剪验证失败",
            {face_id.value, loop_id.value, coedge_id.value,
             edge_it->first, pc_id.value});
      }
      const auto v0_it = state_->vertices.find(edge_it->second.v0.value);
      const auto v1_it = state_->vertices.find(edge_it->second.v1.value);
      if (v0_it == state_->vertices.end() || v1_it == state_->vertices.end()) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoCurveTopologyMismatch,
            "面修剪验证失败：边端点顶点缺失，无法校验 PCurve↔3D",
            "面修剪验证失败",
            {face_id.value, loop_id.value, coedge_id.value, edge_it->first});
      }
      const auto start3 =
          coedge_it->second.reversed ? v1_it->second.point : v0_it->second.point;
      const auto end3 =
          coedge_it->second.reversed ? v0_it->second.point : v1_it->second.point;

      const auto uv_start = surface_service.closest_uv(face_it->second.surface_id, start3);
      const auto uv_end = surface_service.closest_uv(face_it->second.surface_id, end3);
      if (uv_start.status != StatusCode::Ok || uv_end.status != StatusCode::Ok ||
          !uv_start.value.has_value() || !uv_end.value.has_value()) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoCurveTopologyMismatch,
            "面修剪验证失败：边端点无法在 trims 曲面上解析最近 UV（PCurve↔3D 闭环前提）",
            "面修剪验证失败",
            {face_id.value, loop_id.value, coedge_id.value, edge_it->first,
             pc_id.value});
      }

      const auto &pc = state_->pcurves.at(pc_id.value);
      if (pc.poles.size() < 2) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoCurveTopologyMismatch,
            "面修剪验证失败：参数曲线控制点不足", "面修剪验证失败",
            {face_id.value, loop_id.value, coedge_id.value, pc_id.value});
      }
      const Point2 pc_start = pc.poles.front();
      const Point2 pc_end = pc.poles.back();
      const Point2 s2{uv_start.value->first, uv_start.value->second};
      const Point2 e2{uv_end.value->first, uv_end.value->second};
      const auto ds = dist2(pc_start, s2);
      const auto de = dist2(pc_end, e2);
      if (ds > tol || de > tol) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoCurveTopologyMismatch,
            "面修剪验证失败：参数曲线端点与 3D 边端点的 UV 映射不一致"
            " (face=" +
                std::to_string(face_id.value) + ", loop=" +
                std::to_string(loop_id.value) + ", coedge=" +
                std::to_string(coedge_id.value) + ", edge=" +
                std::to_string(coedge_it->second.edge_id.value) +
                ", pcurve=" + std::to_string(pc_id.value) +
                ", ds=" + std::to_string(ds) + ", de=" + std::to_string(de) +
                ")",
            "面修剪验证失败",
            {face_id.value, loop_id.value, coedge_id.value,
             coedge_it->second.edge_id.value, pc_id.value});
      }

      // Also enforce that pcurve endpoints map back to the 3D edge endpoints on the surface.
      // (This catches cases where vertices are consistent in UV but the underlying 3D curve deviates from trimming.)
      {
        auto eval_surf_at = [&](const Point2 &uv) -> std::optional<Point3> {
          const auto ev = surface_service.eval(face_it->second.surface_id, uv.x, uv.y, 0);
          if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
            return std::nullopt;
          }
          return ev.value->point;
        };
        const auto ps = eval_surf_at(pc_start);
        const auto pe = eval_surf_at(pc_end);
        if (ps.has_value() && dist3(*ps, start3) > tol3) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoCurveTopologyMismatch,
              "面修剪验证失败：参数曲线起点映射的 3D 点与边起点不一致"
              " (face=" +
                  std::to_string(face_id.value) + ", loop=" +
                  std::to_string(loop_id.value) + ", coedge=" +
                  std::to_string(coedge_id.value) + ", edge=" +
                  std::to_string(coedge_it->second.edge_id.value) +
                  ", pcurve=" + std::to_string(pc_id.value) + ")",
              "面修剪验证失败",
              {face_id.value, loop_id.value, coedge_id.value,
               coedge_it->second.edge_id.value, pc_id.value});
        }
        if (pe.has_value() && dist3(*pe, end3) > tol3) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoCurveTopologyMismatch,
              "面修剪验证失败：参数曲线终点映射的 3D 点与边终点不一致"
              " (face=" +
                  std::to_string(face_id.value) + ", loop=" +
                  std::to_string(loop_id.value) + ", coedge=" +
                  std::to_string(coedge_id.value) + ", edge=" +
                  std::to_string(coedge_it->second.edge_id.value) +
                  ", pcurve=" + std::to_string(pc_id.value) + ")",
              "面修剪验证失败",
              {face_id.value, loop_id.value, coedge_id.value,
               coedge_it->second.edge_id.value, pc_id.value});
        }
      }

      // Segment sampling consistency (best-effort):
      // Compare a few samples along the PCurve mapped to surface 3D points against the 3D edge curve.
      const auto domain = pcurve_service.domain(pc_id);
      if (domain.status != StatusCode::Ok || !domain.value.has_value() ||
          !(domain.value->max >= domain.value->min) ||
          !std::isfinite(domain.value->min) || !std::isfinite(domain.value->max)) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoCurveTopologyMismatch,
            "面修剪验证失败：参数曲线定义域无效，无法完成 PCurve↔3D 采样一致性校验",
            "面修剪验证失败",
            {face_id.value, loop_id.value, coedge_id.value,
             coedge_it->second.edge_id.value, pc_id.value});
      }
      const auto tmin = domain.value->min;
      const auto tmax = domain.value->max;
      // Dense interior sampling so piecewise-linear UV cannot hide large gaps.
      const std::array<Scalar, 7> alphas{{1.0 / 8.0, 2.0 / 8.0, 3.0 / 8.0, 4.0 / 8.0,
                                          5.0 / 8.0, 6.0 / 8.0, 7.0 / 8.0}};
      for (const auto alpha : alphas) {
        const auto t_alpha =
            coedge_it->second.reversed ? (tmin + (tmax - tmin) * (1.0 - alpha))
                                       : (tmin + (tmax - tmin) * alpha);
        const auto pc_eval = pcurve_service.eval(pc_id, t_alpha, 0);
        if (pc_eval.status != StatusCode::Ok || !pc_eval.value.has_value()) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoCurveTopologyMismatch,
              "面修剪验证失败：参数曲线内点求值失败，无法完成 PCurve↔3D 采样一致性校验"
              " (face=" +
                  std::to_string(face_id.value) + ", loop=" +
                  std::to_string(loop_id.value) + ", coedge=" +
                  std::to_string(coedge_id.value) + ", pcurve=" +
                  std::to_string(pc_id.value) +
                  ", alpha=" + std::to_string(alpha) + ")",
              "面修剪验证失败",
              {face_id.value, loop_id.value, coedge_id.value,
               coedge_it->second.edge_id.value, pc_id.value});
        }
        const Point2 pc2{pc_eval.value->point.x, pc_eval.value->point.y};
        const auto surf_ev = surface_service.eval(face_it->second.surface_id, pc2.x, pc2.y, 0);
        if (surf_ev.status != StatusCode::Ok || !surf_ev.value.has_value()) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoCurveTopologyMismatch,
              "面修剪验证失败：曲面在 PCurve 内点 UV 处求值失败，无法完成 PCurve↔3D 采样一致性校验"
              " (face=" +
                  std::to_string(face_id.value) + ", loop=" +
                  std::to_string(loop_id.value) + ", coedge=" +
                  std::to_string(coedge_id.value) + ", pcurve=" +
                  std::to_string(pc_id.value) +
                  ", alpha=" + std::to_string(alpha) + ")",
              "面修剪验证失败",
              {face_id.value, loop_id.value, coedge_id.value,
               coedge_it->second.edge_id.value, pc_id.value});
        }
        const auto target3 = surf_ev.value->point;
        const auto cp = curve_service.closest_point(curve_id, target3);
        if (cp.status != StatusCode::Ok || !cp.value.has_value()) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoCurveTopologyMismatch,
              "面修剪验证失败：边空间曲线无法对 PCurve→曲面 3D 点反求最近点，无法完成采样一致性校验"
              " (face=" +
                  std::to_string(face_id.value) + ", loop=" +
                  std::to_string(loop_id.value) + ", coedge=" +
                  std::to_string(coedge_id.value) + ", edge=" +
                  std::to_string(coedge_it->second.edge_id.value) +
                  ", pcurve=" + std::to_string(pc_id.value) +
                  ", alpha=" + std::to_string(alpha) + ")",
              "面修剪验证失败",
              {face_id.value, loop_id.value, coedge_id.value,
               coedge_it->second.edge_id.value, pc_id.value});
        }
        const auto d3 = dist3(*cp.value, target3);
        if (d3 > tol3) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoCurveTopologyMismatch,
              "面修剪验证失败：参数曲线映射到曲面后的 3D 位置与边 3D 曲线不一致"
              " (face=" +
                  std::to_string(face_id.value) + ", loop=" +
                  std::to_string(loop_id.value) + ", coedge=" +
                  std::to_string(coedge_id.value) + ", edge=" +
                  std::to_string(coedge_it->second.edge_id.value) +
                  ", pcurve=" + std::to_string(pc_id.value) +
                  ", alpha=" + std::to_string(alpha) +
                  ", d3=" + std::to_string(d3) + ")",
              "面修剪验证失败",
              {face_id.value, loop_id.value, coedge_id.value,
               coedge_it->second.edge_id.value, pc_id.value});
        }
      }
    }
    return ok_void(state_->create_diagnostic("面修剪端点一致性校验通过"));
  };

  auto outer_check = check_loop(face_it->second.outer_loop);
  if (outer_check.status != StatusCode::Ok) {
    return outer_check;
  }
  for (const auto loop_id : face_it->second.inner_loops) {
    auto inner_check = check_loop(loop_id);
    if (inner_check.status != StatusCode::Ok) {
      return inner_check;
    }
  }

  // 全量 PCurve 且无内孔、外环≥3：对**平面基曲面**，用外环 UV 折线映射到 3D 后的 Newell 与平面法向对齐。
  // 补「仅 UV 有符号面积阈值」不足以约束外向（顺时针 UV 仍可满足面积幅值）的洞；与 validate_face 无 PCurve
  // 平面绕向规则同精神。解析柱/球/锥/环在 2-coedge digon 上跳过（outer_coedge_n < 3）。
  if (face_it->second.inner_loops.empty() && outer_coedge_n >= 3 &&
      outer_lit != state_->loops.end()) {
    const SurfaceId base_sid =
        underlying_trim_base_surface_id(*state_, face_it->second.surface_id);
    const auto base_it = state_->surfaces.find(base_sid.value);
    if (base_it != state_->surfaces.end() &&
        base_it->second.kind == detail::SurfaceKind::Plane) {
      std::vector<Point2> ring;
      ring.reserve(outer_coedge_n + 1);
      for (const auto coedge_id : outer_lit->second.coedges) {
        const auto ce_it = state_->coedges.find(coedge_id.value);
        if (ce_it == state_->coedges.end()) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
              "面修剪验证失败：外环引用缺失的定向边", "面修剪验证失败",
              {face_id.value, face_it->second.outer_loop.value, coedge_id.value});
        }
        const auto pc_id = ce_it->second.pcurve_id;
        if (pc_id.value == 0 || !detail::has_pcurve(*state_, pc_id)) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoCurveTopologyMismatch,
              "面修剪验证失败：外环缺少参数曲线（平面绕向校验）", "面修剪验证失败",
              {face_id.value, face_it->second.outer_loop.value, coedge_id.value});
        }
        const auto &rec = state_->pcurves.at(pc_id.value);
        if (rec.poles.size() < 2) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoCurveTopologyMismatch,
              "面修剪验证失败：外环参数曲线退化（平面绕向校验）", "面修剪验证失败",
              {face_id.value, face_it->second.outer_loop.value, coedge_id.value,
               pc_id.value});
        }
        Point2 a = rec.poles.front();
        Point2 b = rec.poles.back();
        if (ce_it->second.reversed) {
          std::swap(a, b);
        }
        if (ring.empty()) {
          ring.push_back(a);
          ring.push_back(b);
        } else {
          const auto dx = ring.back().x - a.x;
          const auto dy = ring.back().y - a.y;
          if (std::sqrt(dx * dx + dy * dy) > tol_ori) {
            return detail::failed_void(
                *state_, StatusCode::InvalidTopology,
                diag_codes::kTopoFaceOuterLoopInvalid,
                "面修剪验证失败：外环 UV 折线不连续（平面绕向校验）", "面修剪验证失败",
                {face_id.value, face_it->second.outer_loop.value});
          }
          ring.push_back(b);
        }
      }
      if (ring.size() < 4) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoFaceOuterLoopInvalid,
            "面修剪验证失败：外环 UV 折线不足以构成多边形（平面绕向校验）",
            "面修剪验证失败",
            {face_id.value, face_it->second.outer_loop.value});
      }
      const auto dx0 = ring.front().x - ring.back().x;
      const auto dy0 = ring.front().y - ring.back().y;
      if (std::sqrt(dx0 * dx0 + dy0 * dy0) > tol_ori) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoFaceOuterLoopInvalid,
            "面修剪验证失败：外环 UV 未闭合（平面绕向校验）", "面修剪验证失败",
            {face_id.value, face_it->second.outer_loop.value});
      }
      const std::size_t n_pts = ring.size() - 1;
      if (n_pts < 3) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoFaceOuterLoopInvalid,
            "面修剪验证失败：外环 UV 顶点数不足（平面绕向校验）", "面修剪验证失败",
            {face_id.value, face_it->second.outer_loop.value});
      }
      std::vector<Point3> poly3;
      poly3.reserve(n_pts);
      for (std::size_t i = 0; i < n_pts; ++i) {
        const auto ev = surface_service.eval(face_it->second.surface_id, ring[i].x,
                                               ring[i].y, 1);
        if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoCurveTopologyMismatch,
              "面修剪验证失败：外环 UV 顶点处曲面求值失败（平面绕向校验）", "面修剪验证失败",
              {face_id.value, face_it->second.outer_loop.value});
        }
        poly3.push_back(ev.value->point);
      }
      const auto newell_pl = newell_normal_unnormalized(poly3);
      const auto tol_pl =
          detail::resolve_linear_tolerance(0.0, state_->config.tolerance);
      const auto newell_thresh_pl = tol_pl * tol_pl * static_cast<Scalar>(100);
      if (detail::norm(newell_pl) > newell_thresh_pl) {
        const auto nn_pl = detail::normalize(newell_pl);
        const auto pn = detail::normalize(base_it->second.normal);
        const auto dot_tol_pl =
            std::max<Scalar>(static_cast<Scalar>(1e-8),
                           tol_pl * static_cast<Scalar>(1e-6));
        if (detail::dot(nn_pl, pn) <= dot_tol_pl) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoLoopOrientationMismatch,
              "面修剪验证失败：全量 PCurve 时平面 UV 外环在 3D 的绕向与基平面法向不一致",
              "面修剪验证失败",
              {face_id.value, face_it->second.outer_loop.value});
        }
      }
    }
  }

  return ok_void(state_->create_diagnostic("面修剪验证通过"));
}

Result<void>
TopologyValidationService::validate_shell_trim_consistency(
    ShellId shell_id) const {
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "壳面修剪验证失败：目标壳不存在",
                               "壳面修剪验证失败");
  }
  for (const auto face_id : shell_it->second.faces) {
    auto r = validate_face_trim_consistency(face_id);
    if (r.status != StatusCode::Ok) {
      return r;
    }
  }
  return ok_void(state_->create_diagnostic("壳面修剪验证通过"));
}

Result<void>
TopologyValidationService::validate_body_trim_consistency(
    BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "体面修剪验证失败：目标体不存在",
                               "体面修剪验证失败");
  }
  for (const auto shell_id : body_it->second.shells) {
    auto r = validate_shell_trim_consistency(shell_id);
    if (r.status != StatusCode::Ok) {
      return r;
    }
  }
  return ok_void(state_->create_diagnostic("体面修剪验证通过"));
}

Result<void>
TopologyValidationService::validate_face_sources(FaceId face_id) const {
  const auto face_it = state_->faces.find(face_id.value);
  if (face_it == state_->faces.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面来源验证失败：目标面不存在",
                               "面来源验证失败");
  }
  if (has_duplicate_ids(
          std::span<const FaceId>(face_it->second.source_faces)) ||
      !validate_source_faces_exist(*state_, face_it->second.source_faces)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoSourceRefInvalid,
        "面来源验证失败：来源面引用非法", "面来源验证失败");
  }
  return ok_void(state_->create_diagnostic("面来源验证通过"));
}

Result<void> TopologyValidationService::validate_face(FaceId face_id) const {
  const auto face_it = state_->faces.find(face_id.value);
  if (face_it == state_->faces.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面验证失败：目标面不存在", "面验证失败");
  }
  if (!detail::has_surface(*state_, face_it->second.surface_id)) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面验证失败：面引用的曲面不存在", "面验证失败");
  }

  auto outer_loop_valid = validate_loop(face_it->second.outer_loop);
  if (outer_loop_valid.status != StatusCode::Ok) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面验证失败：外环非法", "面验证失败");
  }
  if (has_duplicate_ids(std::span<const LoopId>(face_it->second.inner_loops))) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面验证失败：内环列表包含重复引用",
                               "面验证失败");
  }
  for (const auto inner_loop_id : face_it->second.inner_loops) {
    if (inner_loop_id.value == face_it->second.outer_loop.value) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoFaceInnerLoopInvalid,
          "面验证失败：内环列表包含外环（内外环不能相同）", "面验证失败",
          {face_id.value, face_it->second.outer_loop.value});
    }
  }

  // loop_to_faces 必须与面记录互指（与 validate_indices_consistency 对齐；单面验证即可暴露索引损坏）。
  {
    const auto ensure_loop_to_faces = [&](LoopId loop_id) -> Result<void> {
      const auto ltf = state_->loop_to_faces.find(loop_id.value);
      if (ltf == state_->loop_to_faces.end() || ltf->second.empty()) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "面验证失败：环在 loop_to_faces 中无条目（与面引用不一致）",
            "面验证失败", {face_id.value, loop_id.value});
      }
      std::uint64_t hits = 0;
      for (const auto fv : ltf->second) {
        if (fv == face_id.value) {
          ++hits;
        }
      }
      if (hits != 1) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            hits == 0 ? "面验证失败：loop_to_faces 未列出本面（与面记录冲突）"
                      : "面验证失败：loop_to_faces 对本面条目重复",
            "面验证失败", {face_id.value, loop_id.value});
      }
      return ok_void({});
    };
    auto ltf_outer = ensure_loop_to_faces(face_it->second.outer_loop);
    if (ltf_outer.status != StatusCode::Ok) {
      return ltf_outer;
    }
    for (const auto inner_loop_id : face_it->second.inner_loops) {
      auto ltf_inner = ensure_loop_to_faces(inner_loop_id);
      if (ltf_inner.status != StatusCode::Ok) {
        return ltf_inner;
      }
    }
  }

  // face_to_shells：若索引中已有本面条目，每个壳必须在 shells.faces 中列出本面
  // （与 validate_shell 从壳侧的检查对称；孤立面无条目，不强制入壳）。
  {
    const auto fts = state_->face_to_shells.find(face_id.value);
    if (fts != state_->face_to_shells.end() && !fts->second.empty()) {
      std::unordered_set<std::uint64_t> seen_shell;
      seen_shell.reserve(fts->second.size());
      for (const auto shell_value : fts->second) {
        if (!seen_shell.insert(shell_value).second) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoRelationInconsistent,
              "面验证失败：face_to_shells 存在重复壳条目", "面验证失败",
              {face_id.value, shell_value});
        }
        const auto sit = state_->shells.find(shell_value);
        if (sit == state_->shells.end()) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
              "面验证失败：face_to_shells 引用不存在的壳", "面验证失败",
              {face_id.value, shell_value});
        }
        const bool listed_in_shell = std::any_of(
            sit->second.faces.begin(), sit->second.faces.end(),
            [fv = face_id.value](FaceId id) { return id.value == fv; });
        if (!listed_in_shell) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoRelationInconsistent,
              "面验证失败：face_to_shells 列出壳但该壳未包含本面", "面验证失败",
              {face_id.value, shell_value});
        }
      }
    }
  }

  // Stronger consistency: within one Face, the same underlying Edge must not be referenced
  // by more than one loop (outer/inner). This avoids self-gluing trims and ambiguous regions.
  {
    std::unordered_set<std::uint64_t> used_edges;
    auto consume_loop_edges = [&](LoopId loop_id) -> Result<void> {
      const auto loop_it = state_->loops.find(loop_id.value);
      if (loop_it == state_->loops.end()) {
        return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                   diag_codes::kTopoFaceOuterLoopInvalid,
                                   "面验证失败：面引用了不存在的环", "面验证失败",
                                   {face_id.value, loop_id.value});
      }
      for (const auto coedge_id : loop_it->second.coedges) {
        const auto coedge_it = state_->coedges.find(coedge_id.value);
        if (coedge_it == state_->coedges.end()) {
          return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                     diag_codes::kTopoFaceOuterLoopInvalid,
                                     "面验证失败：环引用了不存在的定向边",
                                     "面验证失败",
                                     {face_id.value, loop_id.value, coedge_id.value});
        }
        const auto edge_value = coedge_it->second.edge_id.value;
        if (!used_edges.insert(edge_value).second) {
          // Emit kTopoLoopDuplicateEdge so validation callers can treat it as a stable, specific defect.
          // This is used by regression tests to ensure cross-loop duplicate edges are surfaced.
          return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                     diag_codes::kTopoLoopDuplicateEdge,
                                     "面验证失败：面内存在跨环复用的边（edge=" +
                                         std::to_string(edge_value) + ")",
                                     "面验证失败",
                                     {face_id.value, loop_id.value, edge_value});
        }
      }
      return ok_void(state_->create_diagnostic("面边界边集一致性校验通过"));
    };
    auto outer_edges = consume_loop_edges(face_it->second.outer_loop);
    if (outer_edges.status != StatusCode::Ok) {
      return outer_edges;
    }
    for (const auto loop_id : face_it->second.inner_loops) {
      auto inner_edges = consume_loop_edges(loop_id);
      if (inner_edges.status != StatusCode::Ok) {
        return inner_edges;
      }
    }
  }

  // Planar faces without PCurve trim: (1) outer 3D winding must align with Surface normal
  // (Newell vs plane normal). (2) With holes, outer/inner signed areas in plane basis must
  // oppose (standard hole orientation).
  {
    bool any_pcurve = false;
    auto scan_loop_for_pcurve = [&](LoopId loop_id) {
      const auto loop_it = state_->loops.find(loop_id.value);
      if (loop_it == state_->loops.end()) return;
      for (const auto coedge_id : loop_it->second.coedges) {
        const auto coedge_it = state_->coedges.find(coedge_id.value);
        if (coedge_it != state_->coedges.end() && coedge_it->second.pcurve_id.value != 0) {
          any_pcurve = true;
          return;
        }
      }
    };
    scan_loop_for_pcurve(face_it->second.outer_loop);
    for (const auto loop_id : face_it->second.inner_loops) {
      scan_loop_for_pcurve(loop_id);
    }

    const auto surf_it = state_->surfaces.find(face_it->second.surface_id.value);
    if (!any_pcurve && surf_it != state_->surfaces.end() &&
        surf_it->second.kind == detail::SurfaceKind::Plane) {
      const auto n = detail::normalize(surf_it->second.normal);
      const auto tol = detail::resolve_linear_tolerance(0.0, state_->config.tolerance);

      std::vector<Point3> outer_pts;
      std::string chain_reason;
      if (!loop_vertex_chain_3d(*state_, face_it->second.outer_loop, outer_pts,
                                 chain_reason)) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoFaceOuterLoopInvalid,
            "面验证失败：外环无法提取 3D 边界（" + chain_reason + "）",
            "面验证失败",
            {face_id.value, face_it->second.outer_loop.value});
      }
      if (outer_pts.size() < 3) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoFaceOuterLoopInvalid,
            "面验证失败：外环顶点数不足", "面验证失败",
            {face_id.value, face_it->second.outer_loop.value});
      }
      const auto newell_o = newell_normal_unnormalized(outer_pts);
      const auto newell_thresh = tol * tol * static_cast<Scalar>(100);
      // Only enforce winding-vs-normal when Newell vector is stable.
      // Degenerate/intermediate planar loops skip this gate to avoid false negatives.
      if (detail::norm(newell_o) > newell_thresh) {
        const auto nn = detail::normalize(newell_o);
        const auto dot_align = detail::dot(nn, n);
        const auto dot_tol = std::max<Scalar>(static_cast<Scalar>(1e-8),
                                              tol * static_cast<Scalar>(1e-6));
        if (dot_align <= dot_tol) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoLoopOrientationMismatch,
              "面验证失败：平面外环绕序与曲面法向不一致（右手系）"
              " (face=" +
                  std::to_string(face_id.value) + ", outer_loop=" +
                  std::to_string(face_it->second.outer_loop.value) + ")",
              "面验证失败",
              {face_id.value, face_it->second.outer_loop.value});
        }
      }

      if (!face_it->second.inner_loops.empty()) {
        Vec3 ref = (std::abs(n.z) < 0.9) ? Vec3 {0.0, 0.0, 1.0} : Vec3 {1.0, 0.0, 0.0};
        auto u = detail::cross(ref, n);
        if (detail::norm(u) <= 1e-14) {
          ref = Vec3 {0.0, 1.0, 0.0};
          u = detail::cross(ref, n);
        }
        u = detail::normalize(u);
        const auto v = detail::cross(n, u);
        const auto o = surf_it->second.origin;

        auto signed_area_loop =
            [&](LoopId loop_id, Scalar &out_area,
                std::string &out_reason) -> bool {
          const auto loop_it = state_->loops.find(loop_id.value);
          if (loop_it == state_->loops.end() ||
              loop_it->second.coedges.size() < 3) {
            out_reason = "环不存在或边数不足";
            return false;
          }
          std::vector<Point2> ring;
          ring.reserve(loop_it->second.coedges.size() + 1);
          for (const auto coedge_id : loop_it->second.coedges) {
            const auto ov = oriented_vertices(*state_, coedge_id);
            if (!ov.has_value()) {
              out_reason = "定向边无效";
              return false;
            }
            const auto vit = state_->vertices.find((*ov)[0].value);
            if (vit == state_->vertices.end()) {
              out_reason = "顶点不存在";
              return false;
            }
            const auto p = vit->second.point;
            const Vec3 d = detail::subtract(p, o);
            const Scalar uu = detail::dot(d, u);
            const Scalar vv = detail::dot(d, v);
            ring.push_back(Point2 {uu, vv});
          }
          ring.push_back(ring.front());
          Scalar sum2 = 0.0;
          for (std::size_t i = 0; i + 1 < ring.size(); ++i) {
            sum2 += ring[i].x * ring[i + 1].y - ring[i + 1].x * ring[i].y;
          }
          out_area = sum2 * 0.5;
          if (std::abs(out_area) <= tol * tol * static_cast<Scalar>(10)) {
            out_reason = "环面积过小";
            return false;
          }
          return true;
        };

        Scalar outer_area = 0.0;
        std::string area_reason;
        if (!signed_area_loop(face_it->second.outer_loop, outer_area,
                              area_reason)) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoFaceOuterLoopInvalid,
              "面验证失败：外环方向不可判定（" + area_reason + "）",
              "面验证失败",
              {face_id.value, face_it->second.outer_loop.value});
        }
        for (const auto inner_loop_id : face_it->second.inner_loops) {
          Scalar inner_area = 0.0;
          if (!signed_area_loop(inner_loop_id, inner_area, area_reason)) {
            return detail::failed_void(
                *state_, StatusCode::InvalidTopology,
                diag_codes::kTopoFaceInnerLoopInvalid,
                "面验证失败：内环方向不可判定（" + area_reason + "）",
                "面验证失败",
                {face_id.value, inner_loop_id.value});
          }
          if ((outer_area > 0.0 && inner_area > 0.0) ||
              (outer_area < 0.0 && inner_area < 0.0)) {
            return detail::failed_void(
                *state_, StatusCode::InvalidTopology,
                diag_codes::kTopoLoopOrientationMismatch,
                "面验证失败：内外环方向不符合孔洞规则（应相反）"
                " (face=" +
                    std::to_string(face_id.value) + ", outer_loop=" +
                    std::to_string(face_it->second.outer_loop.value) +
                    ", inner_loop=" + std::to_string(inner_loop_id.value) + ")",
                "面验证失败",
                {face_id.value, face_it->second.outer_loop.value,
                 inner_loop_id.value});
          }
        }
      }
    }
    if (!any_pcurve && surf_it != state_->surfaces.end() &&
        surf_it->second.kind == detail::SurfaceKind::Sphere &&
        face_it->second.inner_loops.empty()) {
      const auto tol = detail::resolve_linear_tolerance(0.0, state_->config.tolerance);
      std::vector<Point3> outer_pts;
      std::string chain_reason;
      if (!loop_vertex_chain_3d(*state_, face_it->second.outer_loop, outer_pts,
                                 chain_reason)) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoFaceOuterLoopInvalid,
            "面验证失败：球面外环无法提取 3D 边界（" + chain_reason + "）",
            "面验证失败",
            {face_id.value, face_it->second.outer_loop.value});
      }
      if (outer_pts.size() < 3) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoFaceOuterLoopInvalid,
            "面验证失败：球面外环顶点数不足", "面验证失败",
            {face_id.value, face_it->second.outer_loop.value});
      }
      const auto newell_o = newell_normal_unnormalized(outer_pts);
      const auto newell_thresh = tol * tol * static_cast<Scalar>(100);
      if (detail::norm(newell_o) > newell_thresh) {
        const auto nn = detail::normalize(newell_o);
        const Vec3 rv =
            detail::subtract(outer_pts.front(), surf_it->second.origin);
        if (detail::norm(rv) > tol) {
          const auto out_r = detail::normalize(rv);
          const auto dot_align = detail::dot(nn, out_r);
          const auto dot_tol = std::max<Scalar>(static_cast<Scalar>(1e-8),
                                                tol * static_cast<Scalar>(1e-6));
          if (dot_align <= dot_tol) {
            return detail::failed_void(
                *state_, StatusCode::InvalidTopology,
                diag_codes::kTopoLoopOrientationMismatch,
                "面验证失败：球面外环绕序与径向不一致（应与球心径向对齐）"
                " (face=" +
                    std::to_string(face_id.value) + ", outer_loop=" +
                    std::to_string(face_it->second.outer_loop.value) + ")",
                "面验证失败",
                {face_id.value, face_it->second.outer_loop.value});
          }
        }
      }
    }
    // Cylinder / Cone lateral patch (no PCurve, single outer): Newell vs outward
    // radial in the plane perpendicular to axis (origin = cylinder base center or cone apex).
    const bool surf_axis_radial_wall =
        surf_it != state_->surfaces.end() &&
        (surf_it->second.kind == detail::SurfaceKind::Cylinder ||
         surf_it->second.kind == detail::SurfaceKind::Cone);
    if (!any_pcurve && surf_axis_radial_wall &&
        face_it->second.inner_loops.empty()) {
      const char *wall_label =
          surf_it->second.kind == detail::SurfaceKind::Cone ? "圆锥" : "圆柱";
      const auto tol_cyl =
          detail::resolve_linear_tolerance(0.0, state_->config.tolerance);
      std::vector<Point3> outer_pts_cyl;
      std::string chain_reason_cyl;
      if (!loop_vertex_chain_3d(*state_, face_it->second.outer_loop, outer_pts_cyl,
                                chain_reason_cyl)) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoFaceOuterLoopInvalid,
            std::string("面验证失败：") + wall_label + "外环无法提取 3D 边界（" +
                chain_reason_cyl + "）",
            "面验证失败",
            {face_id.value, face_it->second.outer_loop.value});
      }
      if (outer_pts_cyl.size() < 3) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoFaceOuterLoopInvalid,
            std::string("面验证失败：") + wall_label + "外环顶点数不足",
            "面验证失败",
            {face_id.value, face_it->second.outer_loop.value});
      }
      const auto newell_cyl = newell_normal_unnormalized(outer_pts_cyl);
      const auto newell_thresh_cyl = tol_cyl * tol_cyl * static_cast<Scalar>(100);
      if (detail::norm(newell_cyl) > newell_thresh_cyl) {
        const auto nn_cyl = detail::normalize(newell_cyl);
        const auto na_cyl = detail::norm(surf_it->second.axis);
        if (na_cyl > tol_cyl * static_cast<Scalar>(1e-9)) {
          const auto a_cyl = detail::normalize(surf_it->second.axis);
          Vec3 r_sum {0.0, 0.0, 0.0};
          for (const auto &p : outer_pts_cyl) {
            const Vec3 d_cyl = detail::subtract(p, surf_it->second.origin);
            const Vec3 r_perp = detail::reject_vec(d_cyl, a_cyl);
            const Scalar nr_cyl = detail::norm(r_perp);
            if (nr_cyl > tol_cyl) {
              const auto ru = detail::normalize(r_perp);
              r_sum.x += ru.x;
              r_sum.y += ru.y;
              r_sum.z += ru.z;
            }
          }
          std::optional<Vec3> rad_ref;
          const Scalar rsn = detail::norm(r_sum);
          if (rsn > tol_cyl * static_cast<Scalar>(1e-6)) {
            rad_ref = detail::normalize(r_sum);
          } else {
            // Symmetric loops can cancel the average radial; fall back to the first
            // vertex with a stable perpendicular component (same spirit as sphere rule).
            for (const auto &p : outer_pts_cyl) {
              const Vec3 d_cyl = detail::subtract(p, surf_it->second.origin);
              const Vec3 r_perp = detail::reject_vec(d_cyl, a_cyl);
              const Scalar nr_cyl = detail::norm(r_perp);
              if (nr_cyl > tol_cyl) {
                rad_ref = detail::normalize(r_perp);
                break;
              }
            }
          }
          if (rad_ref.has_value()) {
            const auto dot_align_cyl = detail::dot(nn_cyl, *rad_ref);
            const auto dot_tol_cyl =
                std::max<Scalar>(static_cast<Scalar>(1e-8),
                               tol_cyl * static_cast<Scalar>(1e-6));
            if (dot_align_cyl <= dot_tol_cyl) {
              return detail::failed_void(
                  *state_, StatusCode::InvalidTopology,
                  diag_codes::kTopoLoopOrientationMismatch,
                  std::string("面验证失败：") + wall_label +
                      "外环绕序与径向不一致（外法向应与截面径向同向）"
                      " (face=" +
                      std::to_string(face_id.value) + ", outer_loop=" +
                      std::to_string(face_it->second.outer_loop.value) + ")",
                  "面验证失败",
                  {face_id.value, face_it->second.outer_loop.value});
            }
          }
        }
      }
    }
    // Torus tube patch (no PCurve, single outer): Newell vs outward minor-circle
    // direction — from the spine point on the major circle (origin + R * radial_in_plane)
    // toward the vertex. Matches `SurfaceKind::Torus` eval normal convention.
    if (!any_pcurve && surf_it != state_->surfaces.end() &&
        surf_it->second.kind == detail::SurfaceKind::Torus &&
        face_it->second.inner_loops.empty()) {
      const auto tol_tor =
          detail::resolve_linear_tolerance(0.0, state_->config.tolerance);
      const Scalar R_major = surf_it->second.radius_a;
      const Scalar r_minor = surf_it->second.radius_b;
      if (R_major > tol_tor && r_minor > tol_tor) {
        std::vector<Point3> outer_pts_tor;
        std::string chain_reason_tor;
        if (!loop_vertex_chain_3d(*state_, face_it->second.outer_loop, outer_pts_tor,
                                  chain_reason_tor)) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoFaceOuterLoopInvalid,
              "面验证失败：环面外环无法提取 3D 边界（" + chain_reason_tor + "）",
              "面验证失败",
              {face_id.value, face_it->second.outer_loop.value});
        }
        if (outer_pts_tor.size() < 3) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoFaceOuterLoopInvalid,
              "面验证失败：环面外环顶点数不足", "面验证失败",
              {face_id.value, face_it->second.outer_loop.value});
        }
        const auto newell_tor = newell_normal_unnormalized(outer_pts_tor);
        const auto newell_thresh_tor =
            tol_tor * tol_tor * static_cast<Scalar>(100);
        if (detail::norm(newell_tor) > newell_thresh_tor) {
          const auto nn_tor = detail::normalize(newell_tor);
          const auto na_tor = detail::norm(surf_it->second.axis);
          if (na_tor > tol_tor * static_cast<Scalar>(1e-9)) {
            const auto a_tor = detail::normalize(surf_it->second.axis);
            Vec3 out_sum {0.0, 0.0, 0.0};
            for (const auto &p : outer_pts_tor) {
              const Vec3 d_tor = detail::subtract(p, surf_it->second.origin);
              const Vec3 d_perp = detail::reject_vec(d_tor, a_tor);
              const Scalar nperp = detail::norm(d_perp);
              if (nperp > tol_tor) {
                const auto ru = detail::normalize(d_perp);
                Point3 spine {surf_it->second.origin.x + R_major * ru.x,
                              surf_it->second.origin.y + R_major * ru.y,
                              surf_it->second.origin.z + R_major * ru.z};
                const Vec3 tube = detail::subtract(p, spine);
                const Scalar nt = detail::norm(tube);
                if (nt > tol_tor * static_cast<Scalar>(1e-3)) {
                  const auto ou = detail::normalize(tube);
                  out_sum.x += ou.x;
                  out_sum.y += ou.y;
                  out_sum.z += ou.z;
                }
              }
            }
            std::optional<Vec3> out_ref;
            const Scalar osn = detail::norm(out_sum);
            if (osn > tol_tor * static_cast<Scalar>(1e-6)) {
              out_ref = detail::normalize(out_sum);
            } else {
              for (const auto &p : outer_pts_tor) {
                const Vec3 d_tor = detail::subtract(p, surf_it->second.origin);
                const Vec3 d_perp = detail::reject_vec(d_tor, a_tor);
                const Scalar nperp = detail::norm(d_perp);
                if (nperp > tol_tor) {
                  const auto ru = detail::normalize(d_perp);
                  Point3 spine {surf_it->second.origin.x + R_major * ru.x,
                                surf_it->second.origin.y + R_major * ru.y,
                                surf_it->second.origin.z + R_major * ru.z};
                  const Vec3 tube = detail::subtract(p, spine);
                  const Scalar nt = detail::norm(tube);
                  if (nt > tol_tor * static_cast<Scalar>(1e-3)) {
                    out_ref = detail::normalize(tube);
                    break;
                  }
                }
              }
            }
            if (out_ref.has_value()) {
              const auto dot_align_tor = detail::dot(nn_tor, *out_ref);
              const auto dot_tol_tor =
                  std::max<Scalar>(static_cast<Scalar>(1e-8),
                                 tol_tor * static_cast<Scalar>(1e-6));
              if (dot_align_tor <= dot_tol_tor) {
                return detail::failed_void(
                    *state_, StatusCode::InvalidTopology,
                    diag_codes::kTopoLoopOrientationMismatch,
                    "面验证失败：环面外环绕序与管向径向不一致（外法向应沿截面圆外向）"
                    " (face=" +
                        std::to_string(face_id.value) + ", outer_loop=" +
                        std::to_string(face_it->second.outer_loop.value) + ")",
                    "面验证失败",
                    {face_id.value, face_it->second.outer_loop.value});
              }
            }
          }
        }
      }
    }
  }

  for (const auto loop_id : face_it->second.inner_loops) {
    if (loop_id.value == face_it->second.outer_loop.value) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoFaceOuterLoopInvalid,
                                 "面验证失败：内环不能与外环相同",
                                 "面验证失败");
    }
    auto inner_loop_valid = validate_loop(loop_id);
    if (inner_loop_valid.status != StatusCode::Ok) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoFaceOuterLoopInvalid,
                                 "面验证失败：内环非法", "面验证失败");
    }
  }
  auto source_valid = validate_face_sources(face_id);
  if (source_valid.status != StatusCode::Ok) {
    return source_valid;
  }
  return ok_void(state_->create_diagnostic("面验证通过"));
}
