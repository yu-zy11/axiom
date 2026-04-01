#include "axiom/heal/heal_services.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "axiom/geo/geometry_services.h"
#include "axiom/topo/topology_service.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/eval_graph_invalidation.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/core/topology_materialization.h"

namespace axiom {

namespace {

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

Scalar adaptive_linear_threshold(const detail::KernelState& state, BodyId body_id, Scalar input, RepairMode mode) {
    if (!detail::has_body(state, body_id)) {
        return input;
    }
    const auto& body = state.bodies.at(body_id.value);
    const auto extent = std::max(0.0, min_extent(body.bbox));
    const auto mode_scale = mode == RepairMode::Aggressive ? 0.02 : mode == RepairMode::Safe ? 0.01 : 0.0;
    const auto extent_floor = extent * mode_scale;
    const auto tolerance_floor = state.config.tolerance.linear * (mode == RepairMode::Aggressive ? 10.0 : 5.0);
    return std::max({input, extent_floor, tolerance_floor});
}

Scalar adaptive_angle_threshold(const detail::KernelState& state, BodyId body_id, Scalar input, RepairMode mode) {
    if (!detail::has_body(state, body_id)) {
        return input;
    }
    const auto& body = state.bodies.at(body_id.value);
    const auto extent = std::max(0.0, min_extent(body.bbox));
    const auto extent_factor = mode == RepairMode::Aggressive ? 0.5 : 0.25;
    const auto tolerance_floor = state.config.tolerance.angular * (mode == RepairMode::Aggressive ? 3.0 : 2.0);
    const auto extent_floor = extent * extent_factor * state.config.tolerance.angular;
    return std::max({input, tolerance_floor, extent_floor});
}

OpReport make_repair_report(StatusCode status, BodyId output, DiagnosticId diagnostic_id, std::vector<Warning> warnings = {}) {
    return OpReport {status, output, diagnostic_id, std::move(warnings)};
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

}  // namespace

ValidationService::ValidationService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<void> ValidationService::validate_geometry(BodyId body_id, ValidationMode) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_void(*state_, StatusCode::InvalidInput, diag_codes::kValDegenerateGeometry,
                                   "几何验证失败：目标实体不存在", "几何验证失败");
    }
    const auto& body = state_->bodies.at(body_id.value);
    if (!has_valid_bbox(body) || min_extent(body.bbox) <= 0.0) {
        return detail::failed_void(*state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                                   "几何验证失败：目标实体包围盒退化或无效", "几何验证失败");
    }
    return ok_void(state_->create_diagnostic("几何验证通过"));
}

Result<void> ValidationService::validate_topology(BodyId body_id, ValidationMode mode) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_void(*state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
                                   "拓扑验证失败：目标实体不存在", "拓扑验证失败");
    }
    const auto& body = state_->bodies.at(body_id.value);
    if (body.rep_kind == RepKind::ExactBRep && body.kind == detail::BodyKind::Imported && !body.bbox.is_valid) {
        return detail::failed_void(*state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
                                   "拓扑验证失败：导入体缺少有效拓扑包围盒信息", "拓扑验证失败");
    }
    if (body.rep_kind == RepKind::ExactBRep && body.shells.empty() &&
        (!body.source_shells.empty() || !body.source_faces.empty())) {
        return detail::failed_void(*state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
                                   mode == ValidationMode::Strict
                                       ? "拓扑验证失败：结果体仅保留来源拓扑引用，尚未物化为可拥有的 B-Rep 拓扑"
                                       : "拓扑验证失败：结果体来源拓扑存在，但 owned topology 已丢失或未正确建立",
                                   "拓扑验证失败");
    }
    if (body.rep_kind == RepKind::ExactBRep && !body.shells.empty()) {
        TopologyValidationService topology_validation {state_};
        const auto topology_result = topology_validation.validate_body(body_id);
        if (topology_result.status != StatusCode::Ok) {
            return topology_result;
        }
    }
    if (mode == ValidationMode::Strict && body.rep_kind == RepKind::ExactBRep && !body.shells.empty()) {
        std::string dangle_reason;
        std::string_view dangle_code = diag_codes::kTopoDanglingEdge;
        std::uint64_t dangle_edge = 0;
        std::uint64_t dangle_shell = 0;
        if (!validate_strict_owned_topology_edge_coedge_index(*state_, body_id, dangle_reason, dangle_code,
                                                                dangle_edge, dangle_shell)) {
            std::vector<std::uint64_t> rel;
            rel.push_back(body_id.value);
            if (dangle_shell != 0) {
                rel.push_back(dangle_shell);
            }
            if (dangle_edge != 0) {
                rel.push_back(dangle_edge);
            }
            return detail::failed_void(*state_, StatusCode::InvalidTopology, dangle_code,
                                       "拓扑验证失败：" + dangle_reason, "拓扑验证失败",
                                       std::move(rel));
        }
    }
    if (mode == ValidationMode::Strict && body.rep_kind == RepKind::ExactBRep) {
        std::string reason;
        std::string_view issue_code = diag_codes::kTopoShellNotClosed;
        if (!validate_source_reference_consistency(*state_, body, reason, issue_code)) {
            return detail::failed_void(*state_, StatusCode::InvalidTopology, issue_code,
                                       "拓扑验证失败：" + reason, "拓扑验证失败");
        }
    }
    if (mode == ValidationMode::Strict && body.rep_kind == RepKind::ExactBRep) {
        // Stage 2 trim bridge: ensure face trim (UV pcurves) is consistent when present.
        TopologyValidationService topology_validation {state_};
        const auto trim_batch = topology_validation.validate_body_trim_consistency(body_id);
        if (trim_batch.status != StatusCode::Ok) {
            return trim_batch;
        }
    }
    if (body.rep_kind == RepKind::ExactBRep && !body.shells.empty()) {
        for (const auto shell_id : body.shells) {
            const auto shell_it = state_->shells.find(shell_id.value);
            if (shell_it == state_->shells.end()) {
                return detail::failed_void(*state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
                                           "拓扑验证失败：体引用了不存在的壳", "拓扑验证失败");
            }
            if (expects_materialized_closed_shell(body) && shell_it->second.faces.size() < 6) {
                return detail::failed_void(*state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
                                           "拓扑验证失败：最小物化结果壳骨架已被破坏，缺少必要闭合面", "拓扑验证失败");
            }
            if (mode == ValidationMode::Strict) {
                std::string reason;
                std::string_view issue_code = diag_codes::kTopoShellNotClosed;
                std::uint64_t related_edge = 0;
                std::vector<std::uint64_t> edge_related_extra;
                if (!validate_closed_shell_edge_usage(*state_, shell_id, reason, issue_code, related_edge,
                                                      &edge_related_extra)) {
                    std::vector<std::uint64_t> related;
                    related.push_back(body_id.value);
                    related.push_back(shell_id.value);
                    if (related_edge != 0) {
                        related.push_back(related_edge);
                    }
                    related.insert(related.end(), edge_related_extra.begin(), edge_related_extra.end());
                    return detail::failed_void(*state_, StatusCode::InvalidTopology, issue_code,
                                               "拓扑验证失败：" + reason, "拓扑验证失败",
                                               std::move(related));
                }

                // Stage 2+ strict relation consistency (after closedness/manifold checks).
                std::string_view rel_code = diag_codes::kTopoShellNotClosed;
                std::string rel_reason;
                std::vector<std::uint64_t> rel_entities;
                if (!strict_check_shell_duplicate_faces_and_connectivity(*state_, shell_id, rel_code, rel_reason, rel_entities)) {
                    rel_entities.insert(rel_entities.begin(), body_id.value);
                    return detail::failed_void(*state_, StatusCode::InvalidTopology, rel_code,
                                               "拓扑验证失败：" + rel_reason, "拓扑验证失败",
                                               std::move(rel_entities));
                }
            }
        }
    }

    if (mode == ValidationMode::Strict && body.rep_kind == RepKind::ExactBRep) {
        // Stage 2+: body-level owned topology consistency:
        // Within one Body, a Face must not be shared by multiple owned Shells.
        // This prevents cross-shell self-gluing and makes manifold/closedness reasoning stable.
        std::unordered_map<std::uint64_t, std::uint64_t> face_owner_shell;
        for (const auto shell_id : body.shells) {
            const auto shell_it = state_->shells.find(shell_id.value);
            if (shell_it == state_->shells.end()) {
                continue;
            }
            for (const auto face_id : shell_it->second.faces) {
                const auto it = face_owner_shell.find(face_id.value);
                if (it == face_owner_shell.end()) {
                    face_owner_shell.emplace(face_id.value, shell_id.value);
                    continue;
                }
                if (it->second != shell_id.value) {
                    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                               diag_codes::kTopoRelationInconsistent,
                                               "拓扑验证失败：体内检测到跨壳共享面（face=" + std::to_string(face_id.value) +
                                                   ", shell_a=" + std::to_string(it->second) +
                                                   ", shell_b=" + std::to_string(shell_id.value) + ")",
                                               "拓扑验证失败",
                                               {body_id.value, face_id.value, it->second, shell_id.value});
                }
            }
        }
    }
    return ok_void(state_->create_diagnostic("拓扑验证通过"));
}

Result<void> ValidationService::validate_self_intersection(BodyId body_id, ValidationMode) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_void(*state_, StatusCode::InvalidInput, diag_codes::kValSelfIntersection,
                                   "自交检查失败：目标实体不存在", "自交检查失败");
    }
    const auto& body = state_->bodies.at(body_id.value);
    if (body.label == "offset" && min_extent(body.bbox) <= 0.0) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kValSelfIntersection,
                                   "自交检查失败：偏置结果已退化，疑似发生自交", "自交检查失败");
    }
    return ok_void(state_->create_diagnostic("自交检查通过"));
}

Result<void> ValidationService::validate_tolerance(BodyId body_id, ValidationMode) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_void(*state_, StatusCode::ToleranceConflict, diag_codes::kValToleranceConflict,
                                   "容差验证失败：目标实体不存在", "容差验证失败");
    }
    const auto& body = state_->bodies.at(body_id.value);
    if (!has_valid_bbox(body)) {
        return detail::failed_void(*state_, StatusCode::ToleranceConflict, diag_codes::kValToleranceConflict,
                                   "容差验证失败：目标实体无有效尺寸范围", "容差验证失败");
    }
    return ok_void(state_->create_diagnostic("容差验证通过"));
}

Result<void> ValidationService::validate_all(BodyId body_id, ValidationMode mode) const {
    auto geometry = validate_geometry(body_id, mode);
    if (geometry.status != StatusCode::Ok) {
        return geometry;
    }
    auto topology = validate_topology(body_id, mode);
    if (topology.status != StatusCode::Ok) {
        return topology;
    }
    return ok_void(state_->create_diagnostic("全量验证通过"));
}

Result<void> ValidationService::validate_geometry_many(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  if (body_ids.empty()) {
    return detail::invalid_input_void(*state_, diag_codes::kValDegenerateGeometry,
                                      "几何批量验证失败：输入为空", "几何批量验证失败");
  }
  for (const auto body_id : body_ids) {
    const auto r = validate_geometry(body_id, mode);
    if (r.status != StatusCode::Ok) return r;
  }
  return ok_void(state_->create_diagnostic("几何批量验证通过"));
}

Result<void> ValidationService::validate_topology_many(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  if (body_ids.empty()) {
    return detail::invalid_input_void(*state_, diag_codes::kTopoShellNotClosed,
                                      "拓扑批量验证失败：输入为空", "拓扑批量验证失败");
  }
  for (const auto body_id : body_ids) {
    const auto r = validate_topology(body_id, mode);
    if (r.status != StatusCode::Ok) return r;
  }
  return ok_void(state_->create_diagnostic("拓扑批量验证通过"));
}

Result<void> ValidationService::validate_all_many(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  if (body_ids.empty()) {
    return detail::invalid_input_void(*state_, diag_codes::kValDegenerateGeometry,
                                      "全量批量验证失败：输入为空", "全量批量验证失败");
  }
  for (const auto body_id : body_ids) {
    const auto r = validate_all(body_id, mode);
    if (r.status != StatusCode::Ok) return r;
  }
  return ok_void(state_->create_diagnostic("全量批量验证通过"));
}

Result<bool> ValidationService::is_geometry_valid(BodyId body_id,
                                                  ValidationMode mode) const {
  const auto r = validate_geometry(body_id, mode);
  if (r.status == StatusCode::InvalidInput) {
    return error_result<bool>(r.status, r.diagnostic_id);
  }
  return ok_result(r.status == StatusCode::Ok,
                   state_->create_diagnostic("已查询几何有效性"));
}

Result<bool> ValidationService::is_topology_valid(BodyId body_id,
                                                  ValidationMode mode) const {
  const auto r = validate_topology(body_id, mode);
  if (r.status == StatusCode::InvalidInput) {
    return error_result<bool>(r.status, r.diagnostic_id);
  }
  return ok_result(r.status == StatusCode::Ok,
                   state_->create_diagnostic("已查询拓扑有效性"));
}

Result<bool> ValidationService::is_valid(BodyId body_id, ValidationMode mode) const {
  const auto r = validate_all(body_id, mode);
  if (r.status == StatusCode::InvalidInput) {
    return error_result<bool>(r.status, r.diagnostic_id);
  }
  return ok_result(r.status == StatusCode::Ok,
                   state_->create_diagnostic("已查询体有效性"));
}

Result<void> ValidationService::validate_bbox(BodyId body_id) const {
  if (!detail::has_body(*state_, body_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kValDegenerateGeometry,
                                      "包围盒验证失败：目标实体不存在", "包围盒验证失败");
  }
  const auto& body = state_->bodies.at(body_id.value);
  if (!has_valid_bbox(body)) {
    return detail::failed_void(*state_, StatusCode::DegenerateGeometry,
                               diag_codes::kValDegenerateGeometry,
                               "包围盒验证失败：目标实体包围盒无效", "包围盒验证失败");
  }
  return ok_void(state_->create_diagnostic("包围盒验证通过"));
}

Result<void> ValidationService::validate_bbox_many(
    std::span<const BodyId> body_ids) const {
  if (body_ids.empty()) {
    return detail::invalid_input_void(*state_, diag_codes::kValDegenerateGeometry,
                                      "包围盒批量验证失败：输入为空", "包围盒批量验证失败");
  }
  for (const auto body_id : body_ids) {
    const auto r = validate_bbox(body_id);
    if (r.status != StatusCode::Ok) return r;
  }
  return ok_void(state_->create_diagnostic("包围盒批量验证通过"));
}

Result<std::uint64_t> ValidationService::count_invalid_in(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  std::uint64_t count = 0;
  for (const auto body_id : body_ids) {
    const auto r = validate_all(body_id, mode);
    if (r.status != StatusCode::Ok) ++count;
  }
  return ok_result(count, state_->create_diagnostic("已统计无效体数量"));
}

Result<BodyId> ValidationService::first_invalid_in(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  for (const auto body_id : body_ids) {
    const auto r = validate_all(body_id, mode);
    if (r.status != StatusCode::Ok) {
      return ok_result(body_id, state_->create_diagnostic("已找到首个无效体"));
    }
  }
  return detail::failed_result<BodyId>(
      *state_, StatusCode::OperationFailed, diag_codes::kValDegenerateGeometry,
      "无效体查询失败：输入集合中不存在无效体", "无效体查询失败");
}

Result<std::vector<BodyId>> ValidationService::filter_valid_bodies(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  std::vector<BodyId> out;
  for (const auto body_id : body_ids) {
    const auto r = validate_all(body_id, mode);
    if (r.status == StatusCode::Ok) out.push_back(body_id);
  }
  return ok_result(std::move(out), state_->create_diagnostic("已过滤有效体"));
}

Result<std::vector<BodyId>> ValidationService::filter_invalid_bodies(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  std::vector<BodyId> out;
  for (const auto body_id : body_ids) {
    const auto r = validate_all(body_id, mode);
    if (r.status != StatusCode::Ok) out.push_back(body_id);
  }
  return ok_result(std::move(out), state_->create_diagnostic("已过滤无效体"));
}

RepairService::RepairService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<void> RepairService::repair_face_trim_pcurves(FaceId face_id, RepairMode mode) {
    if (!detail::has_face(*state_, face_id)) {
        return detail::invalid_input_void(*state_, diag_codes::kHealAutoRepairFailure,
                                          "修剪参数曲线修复失败：目标面不存在", "修剪参数曲线修复失败");
    }
    const auto& face = state_->faces.at(face_id.value);
    if (!detail::has_surface(*state_, face.surface_id)) {
        return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                   diag_codes::kTopoFaceOuterLoopInvalid,
                                   "修剪参数曲线修复失败：面引用的曲面不存在", "修剪参数曲线修复失败",
                                   {face_id.value, face.surface_id.value});
    }
    const auto& surf = state_->surfaces.at(face.surface_id.value);
    if (surf.kind != detail::SurfaceKind::Plane &&
        surf.kind != detail::SurfaceKind::Cylinder &&
        surf.kind != detail::SurfaceKind::Sphere) {
        return detail::failed_void(*state_, StatusCode::NotImplemented,
                                   diag_codes::kCoreOperationUnsupported,
                                   "修剪参数曲线修复失败：暂仅支持平面/圆柱/球面修剪面", "修剪参数曲线修复失败",
                                   {face_id.value, face.surface_id.value});
    }
    if (mode == RepairMode::ReportOnly) {
        return ok_void(state_->create_diagnostic("修剪参数曲线修复预检完成（ReportOnly）"));
    }

    SurfaceService surface_service{state_};
    CurveService curve_service{state_};
    TopologyValidationService topo_validation{state_};

    auto oriented_vertices_for_coedge = [&](CoedgeId coedge_id) -> std::optional<std::array<VertexId, 2>> {
        const auto ce_it = state_->coedges.find(coedge_id.value);
        if (ce_it == state_->coedges.end()) return std::nullopt;
        const auto e_it = state_->edges.find(ce_it->second.edge_id.value);
        if (e_it == state_->edges.end()) return std::nullopt;
        if (!detail::has_vertex(*state_, e_it->second.v0) || !detail::has_vertex(*state_, e_it->second.v1) ||
            e_it->second.v0.value == e_it->second.v1.value) return std::nullopt;
        if (ce_it->second.reversed) {
            return std::array<VertexId, 2>{e_it->second.v1, e_it->second.v0};
        }
        return std::array<VertexId, 2>{e_it->second.v0, e_it->second.v1};
    };

    auto rebuild_loop = [&](LoopId loop_id) -> Result<void> {
        const auto loop_it = state_->loops.find(loop_id.value);
        if (loop_it == state_->loops.end()) {
            return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                       diag_codes::kTopoLoopNotClosed,
                                       "修剪参数曲线修复失败：面引用的环不存在", "修剪参数曲线修复失败",
                                       {face_id.value, loop_id.value});
        }
        for (const auto coedge_id : loop_it->second.coedges) {
            const auto oriented = oriented_vertices_for_coedge(coedge_id);
            if (!oriented.has_value()) continue;
            const auto v0_it = state_->vertices.find((*oriented)[0].value);
            const auto v1_it = state_->vertices.find((*oriented)[1].value);
            if (v0_it == state_->vertices.end() || v1_it == state_->vertices.end()) continue;
            const auto ce_it = state_->coedges.find(coedge_id.value);
            if (ce_it == state_->coedges.end()) continue;
            const auto e_it = state_->edges.find(ce_it->second.edge_id.value);
            if (e_it == state_->edges.end() || e_it->second.curve_id.value == 0) continue;

            const auto curve_id = e_it->second.curve_id;
            if (!detail::has_curve(*state_, curve_id)) continue;

            // Estimate curve parameter interval between edge endpoints.
            const auto t0r = curve_service.closest_parameter(curve_id, v0_it->second.point);
            const auto t1r = curve_service.closest_parameter(curve_id, v1_it->second.point);
            if (t0r.status != StatusCode::Ok || t1r.status != StatusCode::Ok ||
                !t0r.value.has_value() || !t1r.value.has_value()) {
                continue;
            }
            const auto t0 = *t0r.value;
            const auto t1 = *t1r.value;

            struct TrimSample {
                Scalar t{};
                Point3 p{};
                Point2 uv{};
            };
            auto dist3_pt = [](const Point3& a, const Point3& b) -> Scalar {
                const auto dx = a.x - b.x;
                const auto dy = a.y - b.y;
                const auto dz = a.z - b.z;
                return std::sqrt(dx * dx + dy * dy + dz * dz);
            };
            auto lerp3 = [](const Point3& a, const Point3& b, Scalar s) -> Point3 {
                return Point3{a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s,
                              a.z + (b.z - a.z) * s};
            };

            std::vector<TrimSample> chain;
            auto append_endpoint = [&](Scalar tt) -> bool {
                const auto ev = curve_service.eval(curve_id, tt, 0);
                if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
                    return false;
                }
                const auto uv = surface_service.closest_uv(face.surface_id, ev.value->point);
                if (uv.status != StatusCode::Ok || !uv.value.has_value()) {
                    return false;
                }
                chain.push_back(
                    TrimSample{tt, ev.value->point, Point2{uv.value->first, uv.value->second}});
                return true;
            };
            if (!append_endpoint(t0) || !append_endpoint(t1)) {
                continue;
            }

            const Scalar chord_tol =
                std::max(state_->config.tolerance.linear * Scalar(0.32), Scalar(1e-10));
            const std::size_t max_pts =
                mode == RepairMode::Aggressive ? std::size_t{384} : std::size_t{192};
            bool changed = true;
            int guard = 0;
            while (changed && chain.size() < max_pts && guard < 64) {
                changed = false;
                ++guard;
                for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
                    const auto& sa = chain[i];
                    const auto& sb = chain[i + 1];
                    const auto span = std::abs(sb.t - sa.t);
                    if (span <= Scalar(1e-14)) {
                        continue;
                    }
                    const Scalar tm = (sa.t + sb.t) * Scalar(0.5);
                    const auto evm = curve_service.eval(curve_id, tm, 0);
                    if (evm.status != StatusCode::Ok || !evm.value.has_value()) {
                        continue;
                    }
                    const Point3& pm = evm.value->point;
                    const Scalar err_c = dist3_pt(pm, lerp3(sa.p, sb.p, Scalar(0.5)));
                    const Point2 uv_lin{(sa.uv.x + sb.uv.x) * Scalar(0.5),
                                        (sa.uv.y + sb.uv.y) * Scalar(0.5)};
                    const auto surf_mid =
                        surface_service.eval(face.surface_id, uv_lin.x, uv_lin.y, 0);
                    Scalar err_uv = Scalar(0);
                    if (surf_mid.status == StatusCode::Ok && surf_mid.value.has_value()) {
                        err_uv = dist3_pt(surf_mid.value->point, pm);
                    }
                    const Scalar err = std::max(err_c, err_uv);
                    if (err <= chord_tol) {
                        continue;
                    }
                    const auto uvm = surface_service.closest_uv(face.surface_id, pm);
                    if (uvm.status != StatusCode::Ok || !uvm.value.has_value()) {
                        continue;
                    }
                    TrimSample mid{tm, pm, Point2{uvm.value->first, uvm.value->second}};
                    chain.insert(chain.begin() + static_cast<std::ptrdiff_t>(i + 1), mid);
                    changed = true;
                    break;
                }
            }

            std::vector<Point2> uv_poly;
            uv_poly.reserve(chain.size());
            for (const auto& s : chain) {
                if (!uv_poly.empty()) {
                    const auto dx = uv_poly.back().x - s.uv.x;
                    const auto dy = uv_poly.back().y - s.uv.y;
                    if (std::sqrt(dx * dx + dy * dy) <=
                        std::max<Scalar>(state_->config.tolerance.linear, 1e-12)) {
                        continue;
                    }
                }
                uv_poly.push_back(s.uv);
            }
            if (uv_poly.size() < 2) {
                // Fall back to endpoints only.
                const auto uv0 = surface_service.closest_uv(face.surface_id, v0_it->second.point);
                const auto uv1 = surface_service.closest_uv(face.surface_id, v1_it->second.point);
                if (uv0.status != StatusCode::Ok || uv1.status != StatusCode::Ok ||
                    !uv0.value.has_value() || !uv1.value.has_value()) {
                    continue;
                }
                uv_poly = {
                    Point2{uv0.value->first, uv0.value->second},
                    Point2{uv1.value->first, uv1.value->second},
                };
            }

            detail::PCurveRecord pc;
            pc.kind = detail::PCurveKind::Polyline;
            pc.poles = std::move(uv_poly);
            const auto pc_id = PCurveId{state_->allocate_id()};
            state_->pcurves.emplace(pc_id.value, std::move(pc));
            state_->coedges[coedge_id.value].pcurve_id = pc_id;
        }
        return ok_void(state_->create_diagnostic("已重建环参数曲线"));
    };

    auto r0 = rebuild_loop(face.outer_loop);
    if (r0.status != StatusCode::Ok) return r0;
    for (const auto inner : face.inner_loops) {
        auto ri = rebuild_loop(inner);
        if (ri.status != StatusCode::Ok) return ri;
    }

    detail::rebuild_topology_links(*state_);

    const auto post = topo_validation.validate_face_trim_consistency(face_id);
    if (post.status != StatusCode::Ok) {
        return detail::failed_void(*state_, StatusCode::OperationFailed,
                                   diag_codes::kHealAutoRepairFailure,
                                   "修剪参数曲线修复失败：重建后仍未通过修剪一致性验证", "修剪参数曲线修复失败",
                                   {face_id.value, face.surface_id.value});
    }
    return ok_void(state_->create_diagnostic("修剪参数曲线修复完成"));
}

Result<OpReport> RepairService::sew_faces(std::span<const FaceId> faces, Scalar, RepairMode) {
    if (faces.empty()) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kHealSewFailure,
            "缝合失败：输入面集合为空", "缝合失败");
    }
    if (!std::all_of(faces.begin(), faces.end(), [this](const FaceId face_id) { return detail::has_face(*state_, face_id); })) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kHealSewFailure,
            "缝合失败：输入面集合包含无效面", "缝合失败");
    }
    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Generic;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "sewn_body";
    record.bbox = detail::compute_faces_bbox(*state_, faces);
    if (!record.bbox.is_valid) {
        return detail::failed_result<OpReport>(
            *state_, StatusCode::InvalidTopology, diag_codes::kHealSewFailure,
            "缝合失败：无法从输入面推导有效包围盒，拓扑链路可能不完整", "缝合失败");
    }
    for (const auto face_id : faces) {
        append_unique_face(record.source_faces, face_id);
        const auto shell_it = state_->face_to_shells.find(face_id.value);
        if (shell_it == state_->face_to_shells.end()) {
            continue;
        }
        for (const auto shell_value : shell_it->second) {
            append_unique_shell(record.source_shells, ShellId {shell_value});
        }
    }
    detail::materialize_body_bbox_topology(*state_, record);
    state_->bodies.emplace(body_id.value, record);
    detail::rebuild_topology_links(*state_);
    detail::invalidate_eval_for_faces(*state_, faces);
    const auto diag = state_->create_diagnostic("已完成缝合");
    return ok_result(OpReport {StatusCode::Ok, body_id, diag, {}}, diag);
}

Result<OpReport> RepairService::remove_small_edges(BodyId body_id, Scalar threshold, RepairMode mode) {
    if (!detail::has_body(*state_, body_id) || threshold <= 0.0) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kHealSmallEdgeFailure,
            "小边清理失败：目标实体不存在或阈值非法", "小边清理失败");
    }
    const auto& source = state_->bodies.at(body_id.value);
    auto bbox = source.bbox;
    if (mode != RepairMode::ReportOnly) {
        bbox = detail::make_bbox(bbox.min, {bbox.max.x - threshold * 0.1, bbox.max.y, bbox.max.z});
    }
    const auto output = mode == RepairMode::ReportOnly ? body_id : clone_modified_body(state_, body_id, "remove_small_edges", bbox);
    if (mode != RepairMode::ReportOnly) {
        detail::invalidate_eval_for_bodies(*state_, {body_id});
    }
    const auto diag = state_->create_diagnostic("已完成小边清理");
    state_->append_diagnostic_issue(diag,
                                    detail::make_warning_issue(diag_codes::kHealFeatureRemovedWarning, "已清理局部小边，局部特征可能被简化"));
    return ok_result(make_repair_report(StatusCode::Ok, output, diag,
                                        {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "已清理局部小边")}),
                     diag);
}

Result<OpReport> RepairService::remove_small_faces(BodyId body_id, Scalar threshold, RepairMode mode) {
    if (!detail::has_body(*state_, body_id) || threshold <= 0.0) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kHealSmallFaceFailure,
            "小面清理失败：目标实体不存在或阈值非法", "小面清理失败");
    }
    const auto& source = state_->bodies.at(body_id.value);
    const auto effective_threshold = adaptive_linear_threshold(*state_, body_id, threshold, mode);
    auto bbox = source.bbox;
    if (mode == RepairMode::Aggressive) {
        bbox = detail::make_bbox(bbox.min, {bbox.max.x, bbox.max.y - effective_threshold * 0.1, bbox.max.z});
    }
    const auto output = mode == RepairMode::ReportOnly ? body_id : clone_modified_body(state_, body_id, "remove_small_faces", bbox);
    if (mode != RepairMode::ReportOnly) {
        detail::invalidate_eval_for_bodies(*state_, {body_id});
    }
    const auto diag = state_->create_diagnostic("已完成小面清理");
    if (effective_threshold > threshold) {
        state_->append_diagnostic_issue(
            diag,
            detail::make_warning_issue(
                diag_codes::kHealFeatureRemovedWarning,
                "小面清理阈值已按体尺度与容差自适应放大"));
    }
    state_->append_diagnostic_issue(diag,
                                    detail::make_warning_issue(diag_codes::kHealFeatureRemovedWarning, "已清理局部小面，局部几何可能被简化"));
    return ok_result(make_repair_report(StatusCode::Ok, output, diag,
                                        {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "已清理局部小面")}),
                     diag);
}

Result<OpReport> RepairService::merge_near_coplanar_faces(BodyId body_id, Scalar angle_tolerance, RepairMode mode) {
    if (!detail::has_body(*state_, body_id) || angle_tolerance <= 0.0) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kHealAutoRepairFailure,
            "近共面面合并失败：目标实体不存在或角度阈值非法", "近共面面合并失败");
    }
    const auto effective_angle = adaptive_angle_threshold(*state_, body_id, angle_tolerance, mode);
    const auto output = mode == RepairMode::ReportOnly
                            ? body_id
                            : clone_modified_body(state_, body_id, "merge_near_coplanar_faces", state_->bodies.at(body_id.value).bbox);
    if (mode != RepairMode::ReportOnly) {
        detail::invalidate_eval_for_bodies(*state_, {body_id});
    }
    const auto diag = state_->create_diagnostic("已完成近共面面合并");
    std::vector<Warning> warnings;
    if (effective_angle > angle_tolerance) {
        warnings.push_back(detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "近共面阈值已按体尺度与容差自适应放大"));
        state_->append_diagnostic_issue(
            diag,
            detail::make_warning_issue(
                diag_codes::kHealFeatureRemovedWarning,
                "近共面阈值已按体尺度与容差自适应放大"));
    }
    return ok_result(make_repair_report(StatusCode::Ok, output, diag, std::move(warnings)), diag);
}

Result<OpReport> RepairService::auto_repair(BodyId body_id, RepairMode) {
    if (!detail::has_body(*state_, body_id)) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kHealAutoRepairFailure,
            "自动修复失败：目标实体不存在", "自动修复失败");
    }

    ValidationService validation {state_};
    const auto pre_validation = validation.validate_all(body_id, ValidationMode::Standard);
    auto bbox = state_->bodies.at(body_id.value).bbox;
    if (!has_valid_bbox(state_->bodies.at(body_id.value)) || min_extent(state_->bodies.at(body_id.value).bbox) <= 0.0) {
        bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});
    }
    const auto output = clone_modified_body(state_, body_id, "auto_repair", bbox);
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("已完成自动修复");
    if (pre_validation.status != StatusCode::Ok && pre_validation.diagnostic_id.value != 0) {
        const auto pre_diag_it = state_->diagnostics.find(pre_validation.diagnostic_id.value);
        if (pre_diag_it != state_->diagnostics.end()) {
            for (const auto& issue : pre_diag_it->second.issues) {
                state_->append_diagnostic_issue(diag, with_related_entities(issue, {body_id.value}));
            }
        }
    }
    auto feature_removed_issue = detail::make_warning_issue(diag_codes::kHealFeatureRemovedWarning, "自动修复可能改变局部小特征");
    feature_removed_issue.related_entities = {body_id.value, output.value};
    state_->append_diagnostic_issue(diag, std::move(feature_removed_issue));
    const auto post_validation = validation.validate_all(output, ValidationMode::Standard);
    if (post_validation.status == StatusCode::Ok) {
        Issue validated_issue;
        validated_issue.code = std::string(diag_codes::kHealRepairValidated);
        validated_issue.severity = IssueSeverity::Info;
        validated_issue.message = "自动修复后验证通过";
        validated_issue.related_entities = {body_id.value, output.value};
        state_->append_diagnostic_issue(diag, std::move(validated_issue));
        return ok_result(make_repair_report(StatusCode::Ok, output, diag,
                                            {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "自动修复可能改变局部小特征")}),
                         diag);
    }

    if (post_validation.diagnostic_id.value != 0) {
        const auto post_diag_it = state_->diagnostics.find(post_validation.diagnostic_id.value);
        if (post_diag_it != state_->diagnostics.end()) {
            for (const auto& issue : post_diag_it->second.issues) {
                state_->append_diagnostic_issue(diag, with_related_entities(issue, {output.value}));
            }
        }
    }
    Issue failure_issue;
    failure_issue.code = std::string(diag_codes::kHealAutoRepairFailure);
    failure_issue.severity = IssueSeverity::Error;
    failure_issue.message = "自动修复后模型仍未通过验证";
    failure_issue.related_entities = {body_id.value, output.value};
    state_->append_diagnostic_issue(diag, std::move(failure_issue));
    return error_result<OpReport>(
        StatusCode::OperationFailed, diag,
        {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "自动修复可能改变局部小特征")});
}

Result<Scalar> RepairService::estimate_adaptive_linear_threshold(
    BodyId body_id, Scalar input, RepairMode mode) const {
  if (!detail::has_body(*state_, body_id) || input <= 0.0) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kHealSmallFaceFailure,
        "线性阈值估计失败：目标体不存在或输入阈值非法", "线性阈值估计失败");
  }
  return ok_result(adaptive_linear_threshold(*state_, body_id, input, mode),
                   state_->create_diagnostic("已估计线性阈值"));
}

Result<Scalar> RepairService::estimate_adaptive_angle_threshold(
    BodyId body_id, Scalar input, RepairMode mode) const {
  if (!detail::has_body(*state_, body_id) || input <= 0.0) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kHealAutoRepairFailure,
        "角度阈值估计失败：目标体不存在或输入阈值非法", "角度阈值估计失败");
  }
  return ok_result(adaptive_angle_threshold(*state_, body_id, input, mode),
                   state_->create_diagnostic("已估计角度阈值"));
}

Result<OpReport> RepairService::sew_faces_default(std::span<const FaceId> faces) {
  return sew_faces(faces, state_->config.tolerance.linear, RepairMode::Safe);
}

Result<OpReport> RepairService::remove_small_edges_default(BodyId body_id,
                                                           Scalar threshold) {
  return remove_small_edges(body_id, threshold, RepairMode::Safe);
}

Result<OpReport> RepairService::remove_small_faces_default(BodyId body_id,
                                                           Scalar threshold) {
  return remove_small_faces(body_id, threshold, RepairMode::Safe);
}

Result<OpReport> RepairService::merge_near_coplanar_faces_default(
    BodyId body_id, Scalar angle_tolerance) {
  return merge_near_coplanar_faces(body_id, angle_tolerance, RepairMode::Safe);
}

Result<OpReport> RepairService::auto_repair_default(BodyId body_id) {
  return auto_repair(body_id, RepairMode::Safe);
}

Result<std::vector<OpReport>> RepairService::repair_many_auto(
    std::span<const BodyId> body_ids, RepairMode mode) {
  std::vector<OpReport> out;
  out.reserve(body_ids.size());
  for (const auto body_id : body_ids) {
    const auto r = auto_repair(body_id, mode);
    if (r.status != StatusCode::Ok || !r.value.has_value()) {
      return error_result<std::vector<OpReport>>(r.status, r.diagnostic_id);
    }
    out.push_back(*r.value);
  }
  return ok_result(std::move(out), state_->create_diagnostic("已批量自动修复"));
}

Result<std::vector<OpReport>> RepairService::repair_many_remove_small_edges(
    std::span<const BodyId> body_ids, Scalar threshold, RepairMode mode) {
  std::vector<OpReport> out;
  out.reserve(body_ids.size());
  for (const auto body_id : body_ids) {
    const auto r = remove_small_edges(body_id, threshold, mode);
    if (r.status != StatusCode::Ok || !r.value.has_value()) {
      return error_result<std::vector<OpReport>>(r.status, r.diagnostic_id);
    }
    out.push_back(*r.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已批量执行小边清理"));
}

Result<std::vector<OpReport>> RepairService::repair_many_remove_small_faces(
    std::span<const BodyId> body_ids, Scalar threshold, RepairMode mode) {
  std::vector<OpReport> out;
  out.reserve(body_ids.size());
  for (const auto body_id : body_ids) {
    const auto r = remove_small_faces(body_id, threshold, mode);
    if (r.status != StatusCode::Ok || !r.value.has_value()) {
      return error_result<std::vector<OpReport>>(r.status, r.diagnostic_id);
    }
    out.push_back(*r.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已批量执行小面清理"));
}

Result<std::vector<OpReport>>
RepairService::repair_many_merge_near_coplanar_faces(
    std::span<const BodyId> body_ids, Scalar angle_tolerance, RepairMode mode) {
  std::vector<OpReport> out;
  out.reserve(body_ids.size());
  for (const auto body_id : body_ids) {
    const auto r = merge_near_coplanar_faces(body_id, angle_tolerance, mode);
    if (r.status != StatusCode::Ok || !r.value.has_value()) {
      return error_result<std::vector<OpReport>>(r.status, r.diagnostic_id);
    }
    out.push_back(*r.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已批量执行近共面合并"));
}

Result<bool> RepairService::was_modified_output(const OpReport& report) const {
  if (!detail::has_body(*state_, report.output)) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kHealAutoRepairFailure,
        "修复结果查询失败：输出体不存在", "修复结果查询失败");
  }
  return ok_result(state_->bodies.at(report.output.value).kind ==
                       detail::BodyKind::Modified,
                   state_->create_diagnostic("已查询修复输出类型"));
}

Result<bool> RepairService::output_is_new_body(const OpReport& report,
                                               BodyId input) const {
  if (!detail::has_body(*state_, report.output) || !detail::has_body(*state_, input)) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kHealAutoRepairFailure,
        "修复输出比较失败：输入或输出体不存在", "修复输出比较失败");
  }
  return ok_result(report.output.value != input.value,
                   state_->create_diagnostic("已比较修复输出是否新体"));
}

Result<Scalar> RepairService::body_bbox_shrink_ratio(BodyId before,
                                                     BodyId after) const {
  if (!detail::has_body(*state_, before) || !detail::has_body(*state_, after)) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kHealAutoRepairFailure,
        "修复体积比查询失败：输入体不存在", "修复体积比查询失败");
  }
  const auto v0 = min_extent(state_->bodies.at(before.value).bbox);
  const auto v1 = min_extent(state_->bodies.at(after.value).bbox);
  if (v0 <= 0.0) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::OperationFailed, diag_codes::kValDegenerateGeometry,
        "修复体积比查询失败：基准体尺度无效", "修复体积比查询失败");
  }
  return ok_result(v1 / v0, state_->create_diagnostic("已查询修复尺度比"));
}

Result<Scalar> RepairService::compare_bbox_extent_change(BodyId before,
                                                         BodyId after) const {
  if (!detail::has_body(*state_, before) || !detail::has_body(*state_, after)) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kHealAutoRepairFailure,
        "修复尺度变化查询失败：输入体不存在", "修复尺度变化查询失败");
  }
  const auto v0 = min_extent(state_->bodies.at(before.value).bbox);
  const auto v1 = min_extent(state_->bodies.at(after.value).bbox);
  return ok_result(v1 - v0, state_->create_diagnostic("已查询修复尺度变化"));
}

Result<void> RepairService::ensure_valid_after_repair(const OpReport& report,
                                                      ValidationMode mode) const {
  ValidationService validation {state_};
  return validation.validate_all(report.output, mode);
}

Result<std::string> RepairService::summarize_repair(const OpReport& report) const {
  std::ostringstream os;
  os << "status=" << static_cast<int>(report.status) << ", output="
     << report.output.value << ", warnings=" << report.warnings.size();
  return ok_result(os.str(), state_->create_diagnostic("已生成修复摘要"));
}

}  // namespace axiom
