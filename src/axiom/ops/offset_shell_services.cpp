#include "axiom/ops/ops_services.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <unordered_set>

#include "axiom/heal/heal_services.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/eval_graph_invalidation.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/core/topology_materialization.h"
#include "axiom/internal/math/math_internal_utils.h"

namespace axiom {
namespace {

Scalar coordinate(const Point3& point, int axis) {
    return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
}

bool finite_point(const Point3& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

// Certify the complete current owned boundary, including all six surface sides,
// twelve actual straight edges and eight distinct corners. Creation parameters
// and the bbox alone never establish the supported input domain.
bool certify_modify_box(const detail::KernelState& state, const detail::BodyRecord& body,
                        Scalar tolerance, std::array<FaceId, 6>& sides) {
    if (body.rep_kind != RepKind::ExactBRep || body.shells.size() != 1 || !body.bbox.is_valid)
        return false;
    const auto shell = state.shells.find(body.shells.front().value);
    if (shell == state.shells.end() || shell->second.faces.size() != 6) return false;
    std::map<std::uint64_t, int> corners;
    std::unordered_set<std::uint64_t> edges;
    std::unordered_set<int> masks;
    for (const auto face_id : shell->second.faces) {
        const auto face = state.faces.find(face_id.value);
        if (face == state.faces.end() || !face->second.inner_loops.empty() || face->second.mass_boundary_proxy)
            return false;
        const auto surface = state.surfaces.find(face->second.surface_id.value);
        const auto loop = state.loops.find(face->second.outer_loop.value);
        if (surface == state.surfaces.end() || surface->second.kind != detail::SurfaceKind::Plane ||
            loop == state.loops.end() || loop->second.coedges.size() != 4) return false;
        std::unordered_set<int> face_masks;
        for (const auto coedge_id : loop->second.coedges) {
            const auto coedge = state.coedges.find(coedge_id.value);
            if (coedge == state.coedges.end()) return false;
            const auto edge = state.edges.find(coedge->second.edge_id.value);
            if (edge == state.edges.end()) return false;
            const auto curve = state.curves.find(edge->second.curve_id.value);
            if (curve == state.curves.end() || (curve->second.kind != detail::CurveKind::Line &&
                curve->second.kind != detail::CurveKind::LineSegment)) return false;
            edges.insert(coedge->second.edge_id.value);
            std::array<int, 2> endpoint_masks {};
            for (int endpoint = 0; endpoint < 2; ++endpoint) {
                const auto vertex_id = endpoint == 0 ? edge->second.v0 : edge->second.v1;
                const auto vertex = state.vertices.find(vertex_id.value);
                if (vertex == state.vertices.end() || !finite_point(vertex->second.point)) return false;
                int mask = 0;
                for (int axis = 0; axis < 3; ++axis) {
                    const auto value = coordinate(vertex->second.point, axis);
                    if (std::abs(value - coordinate(body.bbox.min, axis)) <= tolerance) continue;
                    if (std::abs(value - coordinate(body.bbox.max, axis)) > tolerance) return false;
                    mask |= 1 << axis;
                }
                corners.emplace(vertex_id.value, mask);
                masks.insert(mask);
                endpoint_masks[endpoint] = mask;
            }
            const int difference = endpoint_masks[0] ^ endpoint_masks[1];
            if (difference != 1 && difference != 2 && difference != 4) return false;
            face_masks.insert(endpoint_masks[coedge->second.reversed ? 1 : 0]);
        }
        if (face_masks.size() != 4) return false;
        int side = -1;
        for (int axis = 0; axis < 3; ++axis) {
            const int bit = *face_masks.begin() & (1 << axis);
            if (std::all_of(face_masks.begin(), face_masks.end(), [&](int mask) {
                    return (mask & (1 << axis)) == bit;
                })) {
                if (side != -1) return false;
                side = axis * 2 + (bit != 0);
                const auto normal = detail::normalize(surface->second.normal);
                const auto expected = detail::axis_normal(axis, bit != 0);
                if (!finite_point(surface->second.origin) || !finite_point({normal.x, normal.y, normal.z}) ||
                    std::hypot(normal.x - expected.x, normal.y - expected.y, normal.z - expected.z) > 1e-10 ||
                    std::abs(coordinate(surface->second.origin, axis) -
                             coordinate(bit ? body.bbox.max : body.bbox.min, axis)) > tolerance) return false;
            }
        }
        if (side < 0 || sides[side].value != 0) return false;
        sides[side] = face_id;
    }
    return corners.size() == 8 && masks.size() == 8 && edges.size() == 12;
}

Result<OpReport> modify_box(const std::shared_ptr<detail::KernelState>& state, BodyId body_id,
                           Scalar amount, const TolerancePolicy& policy,
                           std::span<const FaceId> removed_faces, bool shelling) {
    const std::string prefix = shelling ? "modify.shell." : "modify.offset.";
    const std::string title = shelling ? "抽壳" : "偏置";
    const auto input_code = shelling ? diag_codes::kModShellFailure : diag_codes::kModOffsetInvalid;
    const auto validation_code = shelling ? diag_codes::kModShellValidateFailed : diag_codes::kModOffsetValidateFailed;
    const auto failure = [&](StatusCode status, std::string_view code, std::string message, std::string_view stage) {
        auto issue = detail::make_error_issue(code, title + "失败：" + message, {body_id.value});
        issue.stage = prefix + std::string(stage);
        const auto diag = state->create_diagnostic(title + "失败", {std::move(issue)});
        return error_result<OpReport>(status, diag);
    };
    if (!detail::has_body(*state, body_id) || !std::isfinite(amount) || amount == 0 || (shelling && amount < 0))
        return failure(StatusCode::InvalidInput, input_code, "目标实体或有限非零参数无效", "input_gate");
    if (!std::isfinite(policy.linear) || !std::isfinite(policy.angular) ||
        !std::isfinite(policy.min_local) || !std::isfinite(policy.max_local) ||
        !detail::valid_tolerance_policy(policy))
        return failure(StatusCode::InvalidInput, input_code, "容差策略无效", "input_gate");
    const auto tolerance = detail::resolve_linear_tolerance(0.0, policy);
    if (!std::isfinite(tolerance) || !(tolerance > 0))
        return failure(StatusCode::InvalidInput, input_code, "线性容差须为有限正数", "input_gate");
    const auto& input = state->bodies.at(body_id.value);
    std::unordered_set<std::uint64_t> owned_faces;
    for (const auto shell_id : input.shells) {
        const auto shell = state->shells.find(shell_id.value);
        if (shell != state->shells.end())
            for (const auto face_id : shell->second.faces) owned_faces.insert(face_id.value);
    }
    std::unordered_set<std::uint64_t> selected;
    for (const auto face_id : removed_faces)
        if (!detail::has_face(*state, face_id) || !owned_faces.contains(face_id.value) ||
            !selected.insert(face_id.value).second)
            return failure(StatusCode::InvalidInput, input_code, "移除面无效、重复或不属于目标当前边界", "invalid_faces");
    if (removed_faces.size() > 1)
        return failure(StatusCode::NotImplemented, diag_codes::kModUnsupportedGeometry,
                       "仅支持无开口或单面开口抽壳", "support_gate");
    if (!(std::abs(amount) > tolerance))
        return failure(StatusCode::DegenerateGeometry, diag_codes::kModDegenerateGeometry,
                       "距离或壁厚不大于线性容差", "geometry_gate");
    if (!input.bbox.is_valid || !finite_point(input.bbox.min) || !finite_point(input.bbox.max))
        return failure(StatusCode::DegenerateGeometry, diag_codes::kModDegenerateGeometry,
                       "目标包围盒坐标无效", "geometry_gate");
    for (int axis = 0; axis < 3; ++axis) {
        const auto extent = coordinate(input.bbox.max, axis) - coordinate(input.bbox.min, axis);
        if (!std::isfinite(extent) || !(extent > 2 * tolerance))
            return failure(StatusCode::DegenerateGeometry, diag_codes::kModDegenerateGeometry,
                           "目标尺寸溢出或退化", "geometry_gate");
    }
    std::array<FaceId, 6> sides {};
    if (!certify_modify_box(*state, input, tolerance, sides))
        return failure(StatusCode::NotImplemented, diag_codes::kModUnsupportedGeometry,
                       "支持域为当前完整轴对齐六平面矩形闭壳", "support_gate");
    int opening = -1;
    if (!removed_faces.empty())
        opening = static_cast<int>(std::find(sides.begin(), sides.end(), removed_faces.front()) - sides.begin());
    BoundingBox boundary = input.bbox;
    std::array<Scalar, 3> low {input.bbox.min.x, input.bbox.min.y, input.bbox.min.z};
    std::array<Scalar, 3> high {input.bbox.max.x, input.bbox.max.y, input.bbox.max.z};
    for (int axis = 0; axis < 3; ++axis) {
        const Scalar shift = shelling ? amount : -amount;
        const Scalar low_shift = opening == 2 * axis ? 0 : shift;
        const Scalar high_shift = opening == 2 * axis + 1 ? 0 : shift;
        const auto original_low = low[axis], original_high = high[axis];
        low[axis] += low_shift;
        high[axis] -= high_shift;
        const auto extent = high[axis] - low[axis];
        if (shelling && (!(extent > 0) || !std::isfinite(low_shift + high_shift)))
            return failure(StatusCode::OperationFailed, diag_codes::kModShellFailure,
                           "壁厚过大，内部空腔塌缩", "thickness");
        if (!shelling && amount < 0 && !(extent > tolerance))
            return failure(StatusCode::OperationFailed, diag_codes::kModOffsetSelfIntersection,
                           "负偏置产生自交或完全塌缩", "self_intersection");
        if (shelling && !(extent > tolerance))
            return failure(StatusCode::OperationFailed, diag_codes::kModShellFailure,
                           "内部空腔逼近线性容差阈值", "cavity_tolerance");
        if (!std::isfinite(low[axis]) || !std::isfinite(high[axis]) || !std::isfinite(extent) ||
            std::abs((low[axis] - original_low) - low_shift) > tolerance ||
            std::abs((original_high - high[axis]) - high_shift) > tolerance)
            return failure(StatusCode::DegenerateGeometry, diag_codes::kModDegenerateGeometry,
                           "边界位移数值溢出或不足以表达请求厚度/距离", "geometry_gate");
    }
    const BoundingBox inset {{low[0], low[1], low[2]}, {high[0], high[1], high[2]}, true};
    if (!shelling) boundary = inset;

    // Scratch stores isolate validation meshes/caches, model IDs and adjacency.
    // The current owned source is validated independently of obsolete historical
    // provenance, which may refer to deleted older generations. Real input
    // provenance is never edited and output refers only to immediate owned input.
    auto staged = std::make_shared<detail::KernelState>(state->config);
    staged->next_id = state->next_id;
    staged->curves = state->curves;
    staged->pcurves = state->pcurves;
    staged->surfaces = state->surfaces;
    staged->vertices = state->vertices;
    staged->edges = state->edges;
    staged->coedges = state->coedges;
    staged->loops = state->loops;
    staged->faces = state->faces;
    staged->shells = state->shells;
    staged->bodies = state->bodies;
    staged->edge_to_coedges = state->edge_to_coedges;
    staged->coedge_to_loop = state->coedge_to_loop;
    staged->loop_to_faces = state->loop_to_faces;
    staged->face_to_shells = state->face_to_shells;
    staged->shell_to_bodies = state->shell_to_bodies;
    auto& source_record = staged->bodies.at(body_id.value);
    source_record.source_bodies.clear();
    source_record.source_shells.clear();
    source_record.source_faces.clear();
    for (const auto shell_id : input.shells) {
        auto& source_shell = staged->shells.at(shell_id.value);
        source_shell.source_shells.clear();
        source_shell.source_faces.clear();
        for (const auto face_id : source_shell.faces)
            staged->faces.at(face_id.value).source_faces.clear();
    }
    ValidationService validation {staged};
    if (validation.validate_all(body_id, ValidationMode::Strict).status != StatusCode::Ok)
        return failure(StatusCode::InvalidTopology, validation_code, "当前源边界未通过 Strict", "source_validate");
    source_record = input;
    for (const auto shell_id : input.shells) {
        staged->shells.at(shell_id.value) = state->shells.at(shell_id.value);
        for (const auto face_id : state->shells.at(shell_id.value).faces)
            staged->faces.at(face_id.value) = state->faces.at(face_id.value);
    }
    const auto first_id = state->next_id;
    const auto make_vertices = [&](const BoundingBox& bbox) {
        std::array<VertexId, 8> vertices {};
        for (int mask = 0; mask < 8; ++mask) {
            const auto id = VertexId {staged->allocate_id()};
            staged->vertices.emplace(id.value, detail::VertexRecord {{
                mask & 1 ? bbox.max.x : bbox.min.x,
                mask & 2 ? bbox.max.y : bbox.min.y,
                mask & 4 ? bbox.max.z : bbox.min.z}});
            vertices[mask] = id;
        }
        return vertices;
    };
    // Counterclockwise viewed along each outward side's positive normal.
    const auto side_masks = [](int side) {
        const int axis = side / 2, u = (axis + 1) % 3, v = (axis + 2) % 3;
        const int start = side % 2 ? 1 << axis : 0;
        std::array<int, 4> masks {start, start | (1 << u), start | (1 << u) | (1 << v), start | (1 << v)};
        if (side % 2 == 0) std::reverse(masks.begin(), masks.end());
        return masks;
    };
    std::map<std::pair<std::uint64_t, std::uint64_t>, EdgeId> shared_edges;
    const auto make_face = [&](std::array<VertexId, 4> vertices, const Vec3& normal, FaceId source_face) {
        detail::LoopRecord loop;
        for (int corner = 0; corner < 4; ++corner) {
            const auto start = vertices[corner], end = vertices[(corner + 1) % 4];
            const auto key = std::minmax(start.value, end.value);
            const auto found = shared_edges.find(key);
            EdgeId edge_id;
            if (found == shared_edges.end()) {
                const auto p0 = staged->vertices.at(start.value).point;
                const auto p1 = staged->vertices.at(end.value).point;
                const auto curve = detail::create_materialized_line(*staged, p0, p1);
                edge_id = EdgeId {staged->allocate_id()};
                staged->edges.emplace(edge_id.value, detail::EdgeRecord {curve, start, end, true, 0,
                                                                       detail::norm(detail::subtract(p1, p0))});
                shared_edges.emplace(key, edge_id);
            } else edge_id = found->second;
            const auto coedge_id = CoedgeId {staged->allocate_id()};
            staged->coedges.emplace(coedge_id.value, detail::CoedgeRecord {
                edge_id, staged->edges.at(edge_id.value).v0 != start, {}});
            loop.coedges.push_back(coedge_id);
        }
        const auto loop_id = LoopId {staged->allocate_id()};
        staged->loops.emplace(loop_id.value, std::move(loop));
        detail::SurfaceRecord surface;
        surface.kind = detail::SurfaceKind::Plane;
        surface.origin = staged->vertices.at(vertices.front().value).point;
        surface.normal = normal;
        const auto surface_id = SurfaceId {staged->allocate_id()};
        staged->surfaces.emplace(surface_id.value, std::move(surface));
        const auto face_id = FaceId {staged->allocate_id()};
        staged->faces.emplace(face_id.value, detail::FaceRecord {surface_id, loop_id, {}, {source_face}, false});
        return face_id;
    };
    const auto outer_vertices = make_vertices(boundary);
    std::array<VertexId, 8> inner_vertices {};
    if (shelling) inner_vertices = make_vertices(inset);
    detail::ShellRecord outer_shell, inner_shell;
    outer_shell.source_shells = inner_shell.source_shells = input.shells;
    outer_shell.source_faces = inner_shell.source_faces = std::vector<FaceId>(sides.begin(), sides.end());
    for (int side = 0; side < 6; ++side) {
        if (side == opening) continue;
        const auto masks = side_masks(side);
        std::array<VertexId, 4> outer {}, inner {};
        for (int corner = 0; corner < 4; ++corner) {
            outer[corner] = outer_vertices[masks[corner]];
            if (shelling) inner[corner] = inner_vertices[masks[corner]];
        }
        const auto normal = detail::axis_normal(side / 2, side % 2 != 0);
        outer_shell.faces.push_back(make_face(outer, normal, sides[side]));
        if (shelling) {
            std::reverse(inner.begin(), inner.end());
            const auto face = make_face(inner, detail::scale(normal, -1), sides[side]);
            if (opening >= 0) outer_shell.faces.push_back(face);
            else inner_shell.faces.push_back(face);
        }
    }
    if (opening >= 0) {
        const auto masks = side_masks(opening);
        const auto normal = detail::axis_normal(opening / 2, opening % 2 != 0);
        for (int corner = 0; corner < 4; ++corner) {
            const int a = masks[corner], b = masks[(corner + 1) % 4];
            outer_shell.faces.push_back(make_face({outer_vertices[a], outer_vertices[b],
                                                   inner_vertices[b], inner_vertices[a]}, normal, sides[opening]));
        }
    }
    detail::BodyRecord output_record;
    // Generic denotes a complete real polyhedral boundary for mass/region queries.
    output_record.kind = detail::BodyKind::Generic;
    output_record.label = shelling ? "shell" : "offset";
    output_record.bbox = boundary;
    output_record.source_bodies = {body_id};
    output_record.source_shells = input.shells;
    output_record.source_faces = std::vector<FaceId>(sides.begin(), sides.end());
    const auto outer_id = ShellId {staged->allocate_id()};
    staged->shells.emplace(outer_id.value, std::move(outer_shell));
    output_record.shells.push_back(outer_id);
    if (shelling && opening < 0) {
        const auto inner_id = ShellId {staged->allocate_id()};
        staged->shells.emplace(inner_id.value, std::move(inner_shell));
        output_record.shells.push_back(inner_id);
    }
    const auto output = BodyId {staged->allocate_id()};
    staged->bodies.emplace(output.value, std::move(output_record));
    detail::rebuild_topology_links(*staged);
    const auto valid = validation.validate_all(output, ValidationMode::Strict);
    if (valid.status != StatusCode::Ok) {
        std::string reason = "真实结果边界未通过 Strict";
        const auto report = staged->diagnostics.find(valid.diagnostic_id.value);
        if (report != staged->diagnostics.end() && !report->second.issues.empty())
            reason += "：" + report->second.issues.front().message;
        return failure(StatusCode::InvalidTopology, validation_code, reason, "validate");
    }
    // Append only newly owned model records and indices. Scratch validation
    // diagnostics, representation meshes and runtime caches are discarded.
    const auto publish = [&](auto& destination, auto& source) {
        for (auto& [id, record] : source)
            if (id >= first_id) destination.emplace(id, std::move(record));
    };
    publish(state->curves, staged->curves);
    publish(state->surfaces, staged->surfaces);
    publish(state->vertices, staged->vertices);
    publish(state->edges, staged->edges);
    publish(state->coedges, staged->coedges);
    publish(state->loops, staged->loops);
    publish(state->faces, staged->faces);
    publish(state->shells, staged->shells);
    publish(state->bodies, staged->bodies);
    publish(state->edge_to_coedges, staged->edge_to_coedges);
    publish(state->coedge_to_loop, staged->coedge_to_loop);
    publish(state->loop_to_faces, staged->loop_to_faces);
    publish(state->face_to_shells, staged->face_to_shells);
    publish(state->shell_to_bodies, staged->shell_to_bodies);
    state->next_id = staged->next_id;
    if (!state->active_topology_transaction.expired())
        state->topology_service_allocation_ranges.emplace_back(first_id, state->next_id);
    // Successful Modify operations invalidate the source's Eval consumers;
    // failure paths above leave the live graph and warm geometry caches intact.
    detail::invalidate_eval_for_bodies(*state, {body_id});
    auto complete = detail::make_info_issue(diag_codes::kModCompleted, title + "真实解析边界完成");
    complete.stage = prefix + "complete";
    complete.related_entities = {body_id.value, output.value};
    const auto diag = state->create_diagnostic(title + "完成", {std::move(complete)});
    return ok_result(OpReport {StatusCode::Ok, output, diag, {}}, diag);
}

}  // namespace

Result<OpReport> ModifyService::offset_body(BodyId body, Scalar distance, const TolerancePolicy& tolerance) {
    return modify_box(state_, body, distance, tolerance, {}, false);
}

Result<OpReport> ModifyService::shell_body(BodyId body, std::span<const FaceId> removed_faces, Scalar thickness) {
    return modify_box(state_, body, thickness, state_->config.tolerance, removed_faces, true);
}

}  // namespace axiom
