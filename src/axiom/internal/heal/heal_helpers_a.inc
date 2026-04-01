void append_unique_shell(std::vector<axiom::ShellId>& ids, axiom::ShellId id) {
    if (std::none_of(ids.begin(), ids.end(), [id](axiom::ShellId current) { return current.value == id.value; })) {
        ids.push_back(id);
    }
}

void append_unique_face(std::vector<axiom::FaceId>& ids, axiom::FaceId id) {
    if (std::none_of(ids.begin(), ids.end(), [id](axiom::FaceId current) { return current.value == id.value; })) {
        ids.push_back(id);
    }
}

template <typename Id>
bool has_duplicate_ids(std::span<const Id> ids) {
    std::unordered_set<std::uint64_t> seen;
    for (const auto id : ids) {
        if (!seen.insert(id.value).second) {
            return true;
        }
    }
    return false;
}

bool has_valid_bbox(const detail::BodyRecord& body) {
    return body.bbox.is_valid &&
           body.bbox.max.x >= body.bbox.min.x &&
           body.bbox.max.y >= body.bbox.min.y &&
           body.bbox.max.z >= body.bbox.min.z;
}

bool point3_components_finite(const Point3& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

bool expects_materialized_closed_shell(const detail::BodyRecord& body) {
    return body.kind == detail::BodyKind::BooleanResult ||
           body.kind == detail::BodyKind::Modified ||
           body.kind == detail::BodyKind::BlendResult ||
           body.label == "sewn_body";
}

bool face_belongs_to_shells(const detail::KernelState& state, FaceId face_id, std::span<const ShellId> shell_ids) {
    for (const auto shell_id : shell_ids) {
        const auto shell_it = state.shells.find(shell_id.value);
        if (shell_it == state.shells.end()) {
            continue;
        }
        if (std::any_of(shell_it->second.faces.begin(), shell_it->second.faces.end(),
                        [face_id](FaceId current) { return current.value == face_id.value; })) {
            return true;
        }
    }
    return false;
}

bool validate_source_reference_consistency(const detail::KernelState& state,
                                           const detail::BodyRecord& body,
                                           std::string& reason,
                                           std::string_view& code) {
    if (has_duplicate_ids(std::span<const BodyId>(body.source_bodies))) {
        reason = "来源体引用存在重复项";
        code = diag_codes::kTopoSourceRefInvalid;
        return false;
    }
    if (has_duplicate_ids(std::span<const ShellId>(body.source_shells))) {
        reason = "来源壳引用存在重复项";
        code = diag_codes::kTopoSourceRefInvalid;
        return false;
    }
    if (has_duplicate_ids(std::span<const FaceId>(body.source_faces))) {
        reason = "来源面引用存在重复项";
        code = diag_codes::kTopoSourceRefInvalid;
        return false;
    }
    if (!std::all_of(body.source_bodies.begin(), body.source_bodies.end(),
                     [&state](BodyId id) { return detail::has_body(state, id); })) {
        reason = "来源体引用包含不存在的实体";
        code = diag_codes::kTopoSourceRefInvalid;
        return false;
    }
    if (!std::all_of(body.source_shells.begin(), body.source_shells.end(),
                     [&state](ShellId id) { return detail::has_shell(state, id); })) {
        reason = "来源壳引用包含不存在的实体";
        code = diag_codes::kTopoSourceRefInvalid;
        return false;
    }
    if (!std::all_of(body.source_faces.begin(), body.source_faces.end(),
                     [&state](FaceId id) { return detail::has_face(state, id); })) {
        reason = "来源面引用包含不存在的实体";
        code = diag_codes::kTopoSourceRefInvalid;
        return false;
    }
    if (!body.source_shells.empty() && !body.source_faces.empty()) {
        for (const auto face_id : body.source_faces) {
            if (!face_belongs_to_shells(state, face_id, body.source_shells)) {
                reason = "来源面集合与来源壳集合不一致";
                code = diag_codes::kTopoSourceRefMismatch;
                return false;
            }
        }
    }
    return true;
}

void fill_edge_adjacent_faces_and_outer_loops(
    const detail::KernelState& state,
    const std::unordered_map<std::uint64_t, std::vector<std::uint64_t>>& edge_to_faces,
    std::uint64_t edge_value,
    std::vector<std::uint64_t>* out) {
    if (out == nullptr) {
        return;
    }
    out->clear();
    const auto it = edge_to_faces.find(edge_value);
    if (it == edge_to_faces.end()) {
        return;
    }
    std::vector<std::uint64_t> faces = it->second;
    std::sort(faces.begin(), faces.end());
    faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
    const std::size_t lim = std::min<std::size_t>(faces.size(), 3);
    for (std::size_t i = 0; i < lim; ++i) {
        out->push_back(faces[i]);
        const auto fi = state.faces.find(faces[i]);
        if (fi != state.faces.end()) {
            out->push_back(fi->second.outer_loop.value);
        }
    }
}

bool validate_strict_owned_topology_edge_coedge_index(const detail::KernelState& state,
                                                     BodyId body_id,
                                                     std::string& reason,
                                                     std::string_view& code,
                                                     std::uint64_t& related_edge,
                                                     std::uint64_t& related_shell) {
    related_edge = 0;
    related_shell = 0;
    const auto body_it = state.bodies.find(body_id.value);
    if (body_it == state.bodies.end()) {
        return true;
    }
    for (const auto shell_id : body_it->second.shells) {
        const auto shell_it = state.shells.find(shell_id.value);
        if (shell_it == state.shells.end()) {
            continue;
        }
        for (const auto face_id : shell_it->second.faces) {
            const auto face_it = state.faces.find(face_id.value);
            if (face_it == state.faces.end()) {
                continue;
            }
            std::vector<LoopId> loops;
            loops.push_back(face_it->second.outer_loop);
            loops.insert(loops.end(), face_it->second.inner_loops.begin(), face_it->second.inner_loops.end());
            for (const auto loop_id : loops) {
                const auto loop_it = state.loops.find(loop_id.value);
                if (loop_it == state.loops.end()) {
                    continue;
                }
                for (const auto coedge_id : loop_it->second.coedges) {
                    const auto coedge_it = state.coedges.find(coedge_id.value);
                    if (coedge_it == state.coedges.end()) {
                        reason = "环引用了不存在的定向边";
                        code = diag_codes::kTopoInvariantBroken;
                        related_shell = shell_id.value;
                        return false;
                    }
                    const auto ev = coedge_it->second.edge_id.value;
                    const auto eit = state.edge_to_coedges.find(ev);
                    if (eit == state.edge_to_coedges.end() || eit->second.empty()) {
                        reason =
                            "owned 拓扑中边缺少共边反向索引（悬挂边或索引损坏）：edge=" + std::to_string(ev);
                        code = diag_codes::kTopoDanglingEdge;
                        related_edge = ev;
                        related_shell = shell_id.value;
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

bool validate_closed_shell_edge_usage(const detail::KernelState& state,
                                     ShellId shell_id,
                                     std::string& reason,
                                     std::string_view& code,
                                     std::uint64_t& related_edge,
                                     std::vector<std::uint64_t>* related_extra_entities = nullptr) {
    const auto shell_it = state.shells.find(shell_id.value);
    if (shell_it == state.shells.end()) {
        reason = "目标壳不存在";
        related_edge = 0;
        return false;
    }

    std::unordered_map<std::uint64_t, std::size_t> edge_use_count;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> edge_to_faces;
    for (const auto face_id : shell_it->second.faces) {
        const auto face_it = state.faces.find(face_id.value);
        if (face_it == state.faces.end()) {
            reason = "壳包含不存在的面";
            return false;
        }
        std::vector<LoopId> loops;
        loops.push_back(face_it->second.outer_loop);
        loops.insert(loops.end(), face_it->second.inner_loops.begin(), face_it->second.inner_loops.end());
        for (const auto loop_id : loops) {
            const auto loop_it = state.loops.find(loop_id.value);
            if (loop_it == state.loops.end()) {
                reason = "面引用了不存在的环";
                return false;
            }
            for (const auto coedge_id : loop_it->second.coedges) {
                const auto coedge_it = state.coedges.find(coedge_id.value);
                if (coedge_it == state.coedges.end()) {
                    reason = "环引用了不存在的定向边";
                    return false;
                }
                ++edge_use_count[coedge_it->second.edge_id.value];
                edge_to_faces[coedge_it->second.edge_id.value].push_back(face_id.value);
            }
        }
    }

    if (edge_use_count.empty()) {
        reason = "壳没有可统计的边使用信息";
        return false;
    }

    for (const auto& [edge_value, use_count] : edge_use_count) {
        auto fit = edge_to_faces.find(edge_value);
        if (fit != edge_to_faces.end()) {
            auto& faces = fit->second;
            std::sort(faces.begin(), faces.end());
            faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
            // Stronger closedness: each edge must be used by exactly 2 distinct faces.
            // This catches "self-glued" cases even if coedge use_count happens to be 2.
            if (faces.size() < 2) {
                reason = "壳存在开放边界，边 " + std::to_string(edge_value) +
                         " 未被两侧不同拓扑面使用（unique_faces=" + std::to_string(faces.size()) +
                         ", use_count=" + std::to_string(use_count) + ")";
                code = diag_codes::kTopoOpenBoundary;
                related_edge = edge_value;
                fill_edge_adjacent_faces_and_outer_loops(state, edge_to_faces, edge_value,
                                                         related_extra_entities);
                return false;
            }
            if (faces.size() > 2) {
                reason = "壳存在非流形边，边 " + std::to_string(edge_value) +
                         " 被超过两个拓扑面共享（unique_faces=" + std::to_string(faces.size()) +
                         ", use_count=" + std::to_string(use_count) + ")";
                code = diag_codes::kTopoNonManifoldEdge;
                related_edge = edge_value;
                fill_edge_adjacent_faces_and_outer_loops(state, edge_to_faces, edge_value,
                                                         related_extra_entities);
                return false;
            }
        }
        if (use_count < 2) {
            reason = "壳存在开放边界，边 " + std::to_string(edge_value) + " 仅被使用 " + std::to_string(use_count) + " 次";
            code = diag_codes::kTopoOpenBoundary;
            related_edge = edge_value;
            fill_edge_adjacent_faces_and_outer_loops(state, edge_to_faces, edge_value, related_extra_entities);
            return false;
        }
        if (use_count > 2) {
            reason = "壳存在非流形边，边 " + std::to_string(edge_value) + " 被使用 " + std::to_string(use_count) + " 次";
            code = diag_codes::kTopoNonManifoldEdge;
            related_edge = edge_value;
            fill_edge_adjacent_faces_and_outer_loops(state, edge_to_faces, edge_value, related_extra_entities);
            return false;
        }
    }

    return true;
}

bool strict_check_shell_duplicate_faces_and_connectivity(const detail::KernelState& state,
                                                        ShellId shell_id,
                                                        std::string_view& code,
                                                        std::string& reason,
                                                        std::vector<std::uint64_t>& related_entities) {
    const auto shell_it = state.shells.find(shell_id.value);
    if (shell_it == state.shells.end()) {
        code = diag_codes::kTopoShellNotClosed;
        reason = "目标壳不存在";
        related_entities = {shell_id.value};
        return false;
    }
    if (shell_it->second.faces.empty()) {
        code = diag_codes::kTopoShellNotClosed;
        reason = "壳不包含任何面";
        related_entities = {shell_id.value};
        return false;
    }

    // Duplicate face signature (same surface + same boundary loops).
    std::unordered_map<std::string, FaceId> seen;
    seen.reserve(shell_it->second.faces.size());
    for (const auto face_id : shell_it->second.faces) {
        const auto face_it = state.faces.find(face_id.value);
        if (face_it == state.faces.end()) {
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
            code = diag_codes::kTopoDuplicateFaceInShell;
            reason = "壳存在重复面（相同曲面与边界环签名）";
            related_entities = {shell_id.value, it->second.value, face_id.value,
                                face_it->second.outer_loop.value};
            return false;
        }
        seen.emplace(std::move(sig), face_id);
    }

    // Connectivity by shared edges (face adjacency).
    std::unordered_map<std::uint64_t, std::size_t> face_index;
    face_index.reserve(shell_it->second.faces.size());
    for (std::size_t i = 0; i < shell_it->second.faces.size(); ++i) {
        face_index.emplace(shell_it->second.faces[i].value, i);
    }
    const auto n = shell_it->second.faces.size();
    std::vector<std::vector<std::size_t>> adj(n);
    for (auto& v : adj) v.reserve(4);

    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> edge_to_faces;
    for (const auto face_id : shell_it->second.faces) {
        const auto face_it = state.faces.find(face_id.value);
        if (face_it == state.faces.end()) continue;
        std::vector<LoopId> loops;
        loops.push_back(face_it->second.outer_loop);
        loops.insert(loops.end(), face_it->second.inner_loops.begin(), face_it->second.inner_loops.end());
        for (const auto loop_id : loops) {
            const auto loop_it = state.loops.find(loop_id.value);
            if (loop_it == state.loops.end()) continue;
            for (const auto coedge_id : loop_it->second.coedges) {
                const auto coedge_it = state.coedges.find(coedge_id.value);
                if (coedge_it == state.coedges.end()) continue;
                edge_to_faces[coedge_it->second.edge_id.value].push_back(face_id.value);
            }
        }
    }

    for (auto& [edge_value, faces] : edge_to_faces) {
        if (faces.size() < 2) continue;
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
        code = diag_codes::kTopoShellDisconnected;
        reason = "壳不连通：components=" + std::to_string(components) +
                 ", face_count=" + std::to_string(n);
        related_entities = {shell_id.value};
        return false;
    }

    return true;
}

Issue with_related_entities(Issue issue, std::initializer_list<std::uint64_t> related_entities) {
    if (!issue.related_entities.empty()) {
        return issue;
    }
    issue.related_entities.assign(related_entities.begin(), related_entities.end());
    return issue;
}

Scalar min_extent(const BoundingBox& bbox) {
    return std::min({bbox.max.x - bbox.min.x, bbox.max.y - bbox.min.y, bbox.max.z - bbox.min.z});
}

Scalar max_extent(const BoundingBox& bbox) {
    return std::max({bbox.max.x - bbox.min.x, bbox.max.y - bbox.min.y, bbox.max.z - bbox.min.z});
}

Scalar adaptive_linear_threshold(const detail::KernelState& state, BodyId body_id, Scalar input, RepairMode mode) {
    if (!detail::has_body(state, body_id)) {
        return input;
    }
    const auto& body = state.bodies.at(body_id.value);
    const auto extent = std::max(0.0, min_extent(body.bbox));
    const auto mode_scale = mode == RepairMode::Aggressive ? 0.02 : mode == RepairMode::Safe ? 0.01 : 0.0;
    const auto extent_floor = extent * mode_scale;
    const auto tolerance_floor =
        detail::resolve_linear_tolerance(0.0, state.config.tolerance) *
        (mode == RepairMode::Aggressive ? 10.0 : 5.0);
    return std::max({input, extent_floor, tolerance_floor});
}

Scalar adaptive_angle_threshold(const detail::KernelState& state, BodyId body_id, Scalar input, RepairMode mode) {
    if (!detail::has_body(state, body_id)) {
        return input;
    }
    const auto& body = state.bodies.at(body_id.value);
    const auto extent = std::max(0.0, min_extent(body.bbox));
    const auto extent_factor = mode == RepairMode::Aggressive ? 0.5 : 0.25;
    const auto ang_base = detail::resolve_angular_tolerance(0.0, state.config.tolerance);
    const auto tolerance_floor = ang_base * (mode == RepairMode::Aggressive ? 3.0 : 2.0);
    const auto extent_floor = extent * extent_factor * ang_base;
    return std::max({input, tolerance_floor, extent_floor});
}

OpReport make_repair_report(StatusCode status, BodyId output, DiagnosticId diagnostic_id, std::vector<Warning> warnings = {}) {
    return OpReport {status, output, diagnostic_id, std::move(warnings)};
}

void append_repair_pipeline_trace(detail::KernelState& state, DiagnosticId diag, std::string_view op_name,
                                  std::vector<std::uint64_t> related_entities) {
    Issue issue;
    issue.code = std::string(diag_codes::kHealRepairPipelineTrace);
    issue.severity = IssueSeverity::Info;
    issue.message.assign(op_name.begin(), op_name.end());
    issue.related_entities = std::move(related_entities);
    issue.stage = "heal.repair_pipeline";
    state.append_diagnostic_issue(diag, std::move(issue));
}

std::string collect_repair_pipeline_ops_ordered(const std::vector<Issue>& issues) {
    std::string ops;
    ops.reserve(issues.size() * 24);
    for (const auto& issue : issues) {
        if (issue.code != diag_codes::kHealRepairPipelineTrace) {
            continue;
        }
        if (!ops.empty()) {
            ops += ',';
        }
        ops += issue.message;
    }
    return ops;
}

void append_repair_replay_summary_issue(detail::KernelState& state, DiagnosticId diag) {
    const auto dit = state.diagnostics.find(diag.value);
    if (dit == state.diagnostics.end()) {
        return;
    }
    for (const auto& issue : dit->second.issues) {
        if (issue.code == diag_codes::kHealRepairReplaySummary) {
            return;
        }
    }
    const std::string ops = collect_repair_pipeline_ops_ordered(dit->second.issues);
    if (ops.empty()) {
        return;
    }
    Issue summary;
    summary.code = std::string(diag_codes::kHealRepairReplaySummary);
    summary.severity = IssueSeverity::Info;
    summary.message = ops;
    summary.stage = "heal.repair_replay";
    state.append_diagnostic_issue(diag, std::move(summary));
}

BodyId clone_modified_body(std::shared_ptr<detail::KernelState> state, BodyId source, std::string label, const BoundingBox& bbox) {
    auto record = state->bodies[source.value];
    record.kind = detail::BodyKind::Modified;
    record.label = std::move(label);
    record.bbox = bbox;
    detail::inherit_source_topology_from_owned_shells(*state, record);
    record.shells.clear();
    if (record.source_shells.empty()) {
        for (const auto shell_id : state->bodies.at(source.value).shells) {
            record.source_shells.push_back(shell_id);
        }
    }
    if (std::none_of(record.source_bodies.begin(), record.source_bodies.end(),
                     [source](BodyId current) { return current.value == source.value; })) {
        record.source_bodies.push_back(source);
    }
    detail::materialize_body_bbox_topology(*state, record);
    const auto id = BodyId {state->allocate_id()};
    state->bodies.emplace(id.value, std::move(record));
    detail::rebuild_topology_links(*state);
    return id;
}

// Strict：遍历 owned 壳上的面，校验支撑曲面 UV 定义域为合法开区间（有限则 min<max；含无穷时仍须可比较有序）。
bool validate_strict_owned_surface_domains(const std::shared_ptr<detail::KernelState>& state,
                                           BodyId body_id,
                                           std::string& reason,
                                           std::vector<std::uint64_t>& related) {
    related.clear();
    const auto& body = state->bodies.at(body_id.value);
    if (body.rep_kind != RepKind::ExactBRep || body.shells.empty()) {
        return true;
    }
    SurfaceService surfaces(state);
    std::unordered_set<std::uint64_t> seen_surface;
    const auto open_interval = [](const Range1D& r) { return std::isless(r.min, r.max); };
    for (const auto shell_id : body.shells) {
        const auto shell_it = state->shells.find(shell_id.value);
        if (shell_it == state->shells.end()) {
            continue;
        }
        for (const auto face_id : shell_it->second.faces) {
            const auto face_it = state->faces.find(face_id.value);
            if (face_it == state->faces.end()) {
                continue;
            }
            const SurfaceId surface_id = face_it->second.surface_id;
            if (!detail::has_surface(*state, surface_id)) {
                reason = "面引用了不存在的支撑曲面";
                related = {body_id.value, face_id.value};
                return false;
            }
            if (!seen_surface.insert(surface_id.value).second) {
                continue;
            }
            const auto dom = surfaces.domain(surface_id);
            if (dom.status != StatusCode::Ok || !dom.value.has_value()) {
                reason = "支撑曲面参数域查询失败";
                related = {body_id.value, face_id.value, surface_id.value};
                return false;
            }
            const Range2D& d = *dom.value;
            if (!open_interval(d.u) || !open_interval(d.v)) {
                reason = "支撑曲面 UV 参数域退化或无效";
                related = {body_id.value, face_id.value, surface_id.value};
                return false;
            }
        }
    }
    return true;
}

// Strict：遍历 owned 壳上的环与 coedge，校验边三维曲线与（若存在）面内 pcurve 的 t/uv 参数域为合法开区间。
bool validate_strict_owned_curve_and_pcurve_domains(const std::shared_ptr<detail::KernelState>& state,
                                                    BodyId body_id,
                                                    std::string& reason,
                                                    std::vector<std::uint64_t>& related) {
    related.clear();
    const auto& body = state->bodies.at(body_id.value);
    if (body.rep_kind != RepKind::ExactBRep || body.shells.empty()) {
        return true;
    }
    CurveService curves(state);
    PCurveService pcurves(state);
    const auto open_interval = [](const Range1D& r) { return std::isless(r.min, r.max); };
    std::unordered_set<std::uint64_t> seen_curve;
    std::unordered_set<std::uint64_t> seen_pcurve;

    auto check_curve = [&](CurveId cid, std::uint64_t face_val, std::uint64_t edge_val) -> bool {
        if (cid.value == 0) {
            reason = "边未绑定有效三维曲线句柄";
            related = {body_id.value, face_val, edge_val};
            return false;
        }
        if (!seen_curve.insert(cid.value).second) {
            return true;
        }
        if (!detail::has_curve(*state, cid)) {
            reason = "边引用了不存在的三维曲线";
            related = {body_id.value, face_val, edge_val, cid.value};
            return false;
        }
        const auto dom = curves.domain(cid);
        if (dom.status != StatusCode::Ok || !dom.value.has_value()) {
            reason = "支撑曲线参数域查询失败";
            related = {body_id.value, face_val, edge_val, cid.value};
            return false;
        }
        const Range1D& d = *dom.value;
        if (!open_interval(d)) {
            reason = "支撑曲线参数域退化或无效";
            related = {body_id.value, face_val, edge_val, cid.value};
            return false;
        }
        return true;
    };

    auto check_pcurve = [&](PCurveId pid, std::uint64_t face_val, std::uint64_t coedge_val) -> bool {
        if (pid.value == 0) {
            return true;
        }
        if (!seen_pcurve.insert(pid.value).second) {
            return true;
        }
        if (!detail::has_pcurve(*state, pid)) {
            reason = "共边引用了不存在的参数曲线";
            related = {body_id.value, face_val, coedge_val};
            return false;
        }
        const auto dom = pcurves.domain(pid);
        if (dom.status != StatusCode::Ok || !dom.value.has_value()) {
            reason = "参数曲线定义域查询失败";
            related = {body_id.value, face_val, coedge_val, pid.value};
            return false;
        }
        const Range1D& d = *dom.value;
        if (!open_interval(d)) {
            reason = "参数曲线定义域退化或无效";
            related = {body_id.value, face_val, coedge_val, pid.value};
            return false;
        }
        return true;
    };

    for (const auto shell_id : body.shells) {
        const auto shell_it = state->shells.find(shell_id.value);
        if (shell_it == state->shells.end()) {
            continue;
        }
        for (const auto face_id : shell_it->second.faces) {
            const auto face_it = state->faces.find(face_id.value);
            if (face_it == state->faces.end()) {
                continue;
            }
            const auto& face = face_it->second;
            std::vector<LoopId> loop_ids;
            if (detail::has_loop(*state, face.outer_loop)) {
                loop_ids.push_back(face.outer_loop);
            }
            for (const auto& inner : face.inner_loops) {
                if (detail::has_loop(*state, inner)) {
                    loop_ids.push_back(inner);
                }
            }
            for (const auto loop_id : loop_ids) {
                const auto loop_it = state->loops.find(loop_id.value);
                if (loop_it == state->loops.end()) {
                    reason = "面引用了不存在的环";
                    related = {body_id.value, face_id.value, loop_id.value};
                    return false;
                }
                for (const auto coedge_id : loop_it->second.coedges) {
                    const auto co_it = state->coedges.find(coedge_id.value);
                    if (co_it == state->coedges.end()) {
                        reason = "环引用了不存在的共边";
                        related = {body_id.value, face_id.value, coedge_id.value};
                        return false;
                    }
                    const auto edge_id = co_it->second.edge_id;
                    const auto e_it = state->edges.find(edge_id.value);
                    if (e_it == state->edges.end()) {
                        reason = "共边引用了不存在的边";
                        related = {body_id.value, face_id.value, coedge_id.value};
                        return false;
                    }
                    if (!check_curve(e_it->second.curve_id, face_id.value, edge_id.value)) {
                        return false;
                    }
                    if (!check_pcurve(co_it->second.pcurve_id, face_id.value, coedge_id.value)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

// Strict：owned B-Rep 上出现的顶点世界坐标须为有限值，避免下游求值/网格/自交分析出现不可诊断行为。
bool validate_strict_owned_vertices_finite(const std::shared_ptr<detail::KernelState>& state,
                                             BodyId body_id,
                                             std::string& reason,
                                             std::vector<std::uint64_t>& related) {
    related.clear();
    const auto& body = state->bodies.at(body_id.value);
    if (body.rep_kind != RepKind::ExactBRep || body.shells.empty()) {
        return true;
    }
    std::unordered_set<std::uint64_t> seen_vertex;
    for (const auto shell_id : body.shells) {
        const auto shell_it = state->shells.find(shell_id.value);
        if (shell_it == state->shells.end()) {
            continue;
        }
        for (const auto face_id : shell_it->second.faces) {
            const auto face_it = state->faces.find(face_id.value);
            if (face_it == state->faces.end()) {
                continue;
            }
            const auto& face = face_it->second;
            std::vector<LoopId> loop_ids;
            if (detail::has_loop(*state, face.outer_loop)) {
                loop_ids.push_back(face.outer_loop);
            }
            for (const auto& inner : face.inner_loops) {
                if (detail::has_loop(*state, inner)) {
                    loop_ids.push_back(inner);
                }
            }
            for (const auto loop_id : loop_ids) {
                const auto loop_it = state->loops.find(loop_id.value);
                if (loop_it == state->loops.end()) {
                    reason = "面引用了不存在的环";
                    related = {body_id.value, face_id.value, loop_id.value};
                    return false;
                }
                for (const auto coedge_id : loop_it->second.coedges) {
                    const auto co_it = state->coedges.find(coedge_id.value);
                    if (co_it == state->coedges.end()) {
                        reason = "环引用了不存在的共边";
                        related = {body_id.value, face_id.value, coedge_id.value};
                        return false;
                    }
                    const auto edge_id = co_it->second.edge_id;
                    const auto e_it = state->edges.find(edge_id.value);
                    if (e_it == state->edges.end()) {
                        reason = "共边引用了不存在的边";
                        related = {body_id.value, face_id.value, coedge_id.value};
                        return false;
                    }
                    for (const auto vid : {e_it->second.v0, e_it->second.v1}) {
                        if (vid.value == 0 || !seen_vertex.insert(vid.value).second) {
                            continue;
                        }
                        if (!detail::has_vertex(*state, vid)) {
                            reason = "边引用了不存在的顶点";
                            related = {body_id.value, edge_id.value, vid.value};
                            return false;
                        }
                        const auto& pt = state->vertices.at(vid.value).point;
                        if (!point3_components_finite(pt)) {
                            reason = "顶点坐标非有限值（NaN/Inf）";
                            related = {body_id.value, vid.value};
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

// Strict：owned B-Rep 上不同顶点世界坐标过近（相对线性容差），对齐工业「近重复/焊接容差」可解释门禁。
bool validate_strict_owned_near_duplicate_vertices(const std::shared_ptr<detail::KernelState>& state,
                                                   BodyId body_id,
                                                   std::string& reason,
                                                   std::vector<std::uint64_t>& related) {
    related.clear();
    const auto& body = state->bodies.at(body_id.value);
    if (body.rep_kind != RepKind::ExactBRep || body.shells.empty()) {
        return true;
    }
    const Scalar lin = detail::resolve_linear_tolerance(0.0, state->config.tolerance);
    if (!std::isfinite(lin) || lin <= 0.0) {
        reason = "线性容差无效，无法进行近重复顶点门控";
        related = {body_id.value};
        return false;
    }
    const Scalar dist_thr = lin * Scalar(1e-2);
    const Scalar dist_thr_sq = dist_thr * dist_thr;

    std::unordered_set<std::uint64_t> seen_vertex;
    for (const auto shell_id : body.shells) {
        const auto shell_it = state->shells.find(shell_id.value);
        if (shell_it == state->shells.end()) {
            continue;
        }
        for (const auto face_id : shell_it->second.faces) {
            const auto face_it = state->faces.find(face_id.value);
            if (face_it == state->faces.end()) {
                continue;
            }
            const auto& face = face_it->second;
            std::vector<LoopId> loop_ids;
            if (detail::has_loop(*state, face.outer_loop)) {
                loop_ids.push_back(face.outer_loop);
            }
            for (const auto& inner : face.inner_loops) {
                if (detail::has_loop(*state, inner)) {
                    loop_ids.push_back(inner);
                }
            }
            for (const auto loop_id : loop_ids) {
                const auto loop_it = state->loops.find(loop_id.value);
                if (loop_it == state->loops.end()) {
                    continue;
                }
                for (const auto coedge_id : loop_it->second.coedges) {
                    const auto co_it = state->coedges.find(coedge_id.value);
                    if (co_it == state->coedges.end()) {
                        continue;
                    }
                    const auto e_it = state->edges.find(co_it->second.edge_id.value);
                    if (e_it == state->edges.end()) {
                        continue;
                    }
                    for (const auto vid : {e_it->second.v0, e_it->second.v1}) {
                        if (vid.value != 0) {
                            seen_vertex.insert(vid.value);
                        }
                    }
                }
            }
        }
    }

    std::vector<std::uint64_t> verts(seen_vertex.begin(), seen_vertex.end());
    std::sort(verts.begin(), verts.end());
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const auto vi = verts[i];
        if (!detail::has_vertex(*state, VertexId{vi})) {
            reason = "近重复扫描引用了不存在的顶点";
            related = {body_id.value, vi};
            return false;
        }
        const Point3& pi = state->vertices.at(vi).point;
        for (std::size_t j = i + 1; j < verts.size(); ++j) {
            const auto vj = verts[j];
            const Point3& pj = state->vertices.at(vj).point;
            const Scalar dx = pj.x - pi.x;
            const Scalar dy = pj.y - pi.y;
            const Scalar dz = pj.z - pi.z;
            const Scalar d2 = dx * dx + dy * dy + dz * dz;
            if (!std::isfinite(d2)) {
                reason = "顶点间距计算非有限值";
                related = {body_id.value, vi, vj};
                return false;
            }
            if (d2 < dist_thr_sq) {
                reason = "不同顶点世界坐标间距小于相对线性容差门控（近重复）";
                related = {body_id.value, vi, vj};
                return false;
            }
        }
    }
    return true;
}

// Strict：owned B-Rep 边端点间距相对线性容差过小（含不同 VertexId 但坐标重合），避免工业链路中的零长度边。
bool validate_strict_owned_edges_nondegenerate(const std::shared_ptr<detail::KernelState>& state,
                                                 BodyId body_id,
                                                 std::string& reason,
                                                 std::vector<std::uint64_t>& related) {
    related.clear();
    const auto& body = state->bodies.at(body_id.value);
    if (body.rep_kind != RepKind::ExactBRep || body.shells.empty()) {
        return true;
    }
    const Scalar lin = detail::resolve_linear_tolerance(0.0, state->config.tolerance);
    if (!std::isfinite(lin) || lin <= 0.0) {
        reason = "线性容差无效，无法进行边长度退化门控";
        related = {body_id.value};
        return false;
    }
    const Scalar char_scale = std::max(max_extent(body.bbox), lin);
    const Scalar abs_thr = lin * Scalar(1e-6);
    const Scalar len_thr_sq =
        std::max<Scalar>(abs_thr * abs_thr,
                         std::numeric_limits<Scalar>::epsilon() * char_scale * char_scale * Scalar(10.0));
    std::unordered_set<std::uint64_t> seen_edge;
    for (const auto shell_id : body.shells) {
        const auto shell_it = state->shells.find(shell_id.value);
        if (shell_it == state->shells.end()) {
            continue;
        }
        for (const auto face_id : shell_it->second.faces) {
            const auto face_it = state->faces.find(face_id.value);
            if (face_it == state->faces.end()) {
                continue;
            }
            const auto& face = face_it->second;
            std::vector<LoopId> loop_ids;
            if (detail::has_loop(*state, face.outer_loop)) {
                loop_ids.push_back(face.outer_loop);
            }
            for (const auto& inner : face.inner_loops) {
                if (detail::has_loop(*state, inner)) {
                    loop_ids.push_back(inner);
                }
            }
            for (const auto loop_id : loop_ids) {
                const auto loop_it = state->loops.find(loop_id.value);
                if (loop_it == state->loops.end()) {
                    continue;
                }
                for (const auto coedge_id : loop_it->second.coedges) {
                    const auto co_it = state->coedges.find(coedge_id.value);
                    if (co_it == state->coedges.end()) {
                        continue;
                    }
                    const EdgeId edge_id = co_it->second.edge_id;
                    if (!seen_edge.insert(edge_id.value).second) {
                        continue;
                    }
                    const auto e_it = state->edges.find(edge_id.value);
                    if (e_it == state->edges.end()) {
                        reason = "环引用了不存在的边";
                        related = {body_id.value, face_id.value, coedge_id.value};
                        return false;
                    }
                    const VertexId v0 = e_it->second.v0;
                    const VertexId v1 = e_it->second.v1;
                    if (!detail::has_vertex(*state, v0) || !detail::has_vertex(*state, v1)) {
                        reason = "边引用了不存在的顶点";
                        related = {body_id.value, edge_id.value};
                        return false;
                    }
                    const Point3& p0 = state->vertices.at(v0.value).point;
                    const Point3& p1 = state->vertices.at(v1.value).point;
                    const Scalar dx = p1.x - p0.x;
                    const Scalar dy = p1.y - p0.y;
                    const Scalar dz = p1.z - p0.z;
                    const Scalar len_sq = dx * dx + dy * dy + dz * dz;
                    if (!(std::isfinite(len_sq)) || len_sq <= len_thr_sq) {
                        reason = "边端点间距相对线性容差过小（零长度或近零长度边）";
                        related = {body_id.value, edge_id.value, v0.value, v1.value};
                        return false;
                    }
                }
            }
        }
    }
    return true;
}
