#include "axiom/ops/ops_services.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "axiom/heal/heal_services.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/core/topology_materialization.h"
#include "axiom/internal/math/math_internal_utils.h"

namespace axiom {
namespace {

constexpr Scalar kBlendQuarterTurn = 1.5707963267948966192313216916398;

// The certificate below is about the current owned boundary, never the body's
// creation parameters or its bbox alone. A modified rectangular blank is still
// supported if its complete boundary remains an axis-aligned box.
struct BlendBox {
    BoundingBox bbox;
    std::unordered_map<std::uint64_t, int> corners;
    std::unordered_set<std::uint64_t> edges;
};

Scalar coordinate(const Point3& point, int axis) {
    return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
}

bool finite_point(const Point3& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

bool certify_blend_box(const detail::KernelState& state, const detail::BodyRecord& body,
                       Scalar tolerance, BlendBox& box) {
    if (body.rep_kind != RepKind::ExactBRep || body.shells.size() != 1 || !body.bbox.is_valid ||
        !finite_point(body.bbox.min) || !finite_point(body.bbox.max)) return false;
    for (int axis = 0; axis < 3; ++axis) {
        const auto extent = coordinate(body.bbox.max, axis) - coordinate(body.bbox.min, axis);
        if (!std::isfinite(extent) || !(extent > tolerance)) return false;
    }
    box.bbox = body.bbox;
    const auto shell = state.shells.find(body.shells.front().value);
    if (shell == state.shells.end() || shell->second.faces.size() != 6) return false;
    std::unordered_set<int> face_sides, corner_masks;
    std::unordered_set<std::uint64_t> faces;
    for (const auto face_id : shell->second.faces) {
        const auto face = state.faces.find(face_id.value);
        if (!faces.insert(face_id.value).second || face == state.faces.end() ||
            !face->second.inner_loops.empty() || face->second.mass_boundary_proxy) return false;
        const auto surface = state.surfaces.find(face->second.surface_id.value);
        const auto loop = state.loops.find(face->second.outer_loop.value);
        if (surface == state.surfaces.end() || surface->second.kind != detail::SurfaceKind::Plane ||
            loop == state.loops.end() || loop->second.coedges.size() != 4) return false;
        std::vector<int> face_masks;
        for (const auto coedge_id : loop->second.coedges) {
            const auto coedge = state.coedges.find(coedge_id.value);
            if (coedge == state.coedges.end()) return false;
            const auto edge = state.edges.find(coedge->second.edge_id.value);
            if (edge == state.edges.end()) return false;
            const auto curve = state.curves.find(edge->second.curve_id.value);
            if (curve == state.curves.end() ||
                (curve->second.kind != detail::CurveKind::Line &&
                 curve->second.kind != detail::CurveKind::LineSegment)) return false;
            box.edges.insert(coedge->second.edge_id.value);
            std::array<int, 2> endpoint_masks {};
            for (int endpoint = 0; endpoint < 2; ++endpoint) {
                const auto vertex_id = endpoint == 0 ? edge->second.v0 : edge->second.v1;
                const auto vertex = state.vertices.find(vertex_id.value);
                if (vertex == state.vertices.end() || !finite_point(vertex->second.point)) return false;
                int mask = 0;
                for (int axis = 0; axis < 3; ++axis) {
                    const auto value = coordinate(vertex->second.point, axis);
                    if (std::abs(value - coordinate(box.bbox.min, axis)) <= tolerance) continue;
                    if (std::abs(value - coordinate(box.bbox.max, axis)) > tolerance) return false;
                    mask |= 1 << axis;
                }
                box.corners.emplace(vertex_id.value, mask);
                corner_masks.insert(mask);
                endpoint_masks[endpoint] = mask;
            }
            const int difference = endpoint_masks[0] ^ endpoint_masks[1];
            if (difference != 1 && difference != 2 && difference != 4) return false;
            face_masks.push_back(endpoint_masks[coedge->second.reversed ? 1 : 0]);
        }
        int side = -1;
        for (int axis = 0; axis < 3; ++axis) {
            const int bit = face_masks.front() & (1 << axis);
            if (std::all_of(face_masks.begin(), face_masks.end(), [&](int mask) {
                    return (mask & (1 << axis)) == bit;
                })) {
                if (side != -1) return false;
                side = 2 * axis + (bit != 0);
                const Vec3 normal = detail::normalize(surface->second.normal);
                const Vec3 expected = detail::axis_normal(axis, bit != 0);
                if (!finite_point(surface->second.origin) ||
                    !std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) ||
                    std::hypot(normal.x - expected.x, normal.y - expected.y, normal.z - expected.z) > 1e-10 ||
                    std::abs(coordinate(surface->second.origin, axis) -
                        coordinate(bit ? box.bbox.max : box.bbox.min, axis)) > tolerance) return false;
            }
        }
        if (side == -1 || !face_sides.insert(side).second) return false;
    }
    return box.edges.size() == 12 && box.corners.size() == 8 && corner_masks.size() == 8;
}

Result<OpReport> blend_box_edges(const std::shared_ptr<detail::KernelState>& state,
                               BodyId body_id, std::span<const EdgeId> selected,
                               Scalar amount, bool fillet) {
    const std::string prefix = fillet ? "blend.fillet." : "blend.chamfer.";
    const std::string title = fillet ? "圆角" : "倒角";
    const auto failure = [&](StatusCode status, std::string_view code,
                             std::string message, std::string_view stage) {
        auto issue = detail::make_error_issue(code, title + "失败：" + message, {body_id.value});
        issue.stage = prefix + std::string(stage);
        const auto diag = state->create_diagnostic(title + "失败", {std::move(issue)});
        return error_result<OpReport>(status, diag);
    };
    if (!detail::has_body(*state, body_id) || selected.empty() || !std::isfinite(amount) || !(amount > 0))
        return failure(StatusCode::InvalidInput, diag_codes::kBlendInvalidTarget,
                       "实体、边集合或半径/距离无效", "input_gate");
    std::unordered_set<std::uint64_t> unique_edges;
    for (const auto edge : selected)
        if (!detail::has_edge(*state, edge) || !unique_edges.insert(edge.value).second)
            return failure(StatusCode::InvalidInput, diag_codes::kBlendInvalidTarget,
                           "边句柄无效或重复", "input_gate");

    const auto tolerance = detail::resolve_linear_tolerance(0.0, state->config.tolerance);
    if (!std::isfinite(tolerance) || !(tolerance > 0))
        return failure(StatusCode::InvalidInput, diag_codes::kBlendInvalidTarget,
                       "线性容差策略无效", "input_gate");
    if (!(amount > tolerance))
        return failure(StatusCode::DegenerateGeometry, diag_codes::kBlendDegenerateGeometry,
                       "半径/距离不大于线性容差", "geometry_gate");
    const auto& input = state->bodies.at(body_id.value);
    // Foreign edges must fail as input errors even for an unsupported shape.
    std::unordered_set<std::uint64_t> owned_edges;
    for (const auto shell_id : input.shells) {
        const auto shell = state->shells.find(shell_id.value);
        if (shell == state->shells.end()) continue;
        for (const auto face_id : shell->second.faces) {
            const auto face = state->faces.find(face_id.value);
            if (face == state->faces.end()) continue;
            std::vector<LoopId> loops {face->second.outer_loop};
            loops.insert(loops.end(), face->second.inner_loops.begin(), face->second.inner_loops.end());
            for (const auto loop_id : loops) {
                const auto loop = state->loops.find(loop_id.value);
                if (loop == state->loops.end()) continue;
                for (const auto coedge_id : loop->second.coedges) {
                    const auto coedge = state->coedges.find(coedge_id.value);
                    if (coedge != state->coedges.end()) owned_edges.insert(coedge->second.edge_id.value);
                }
            }
        }
    }
    for (const auto edge : selected)
        if (!owned_edges.contains(edge.value))
            return failure(StatusCode::InvalidInput, diag_codes::kBlendInvalidTarget,
                           "选择边不属于目标实体的当前闭壳", "input_gate");
    if (input.bbox.is_valid) {
        if (!finite_point(input.bbox.min) || !finite_point(input.bbox.max))
            return failure(StatusCode::DegenerateGeometry, diag_codes::kBlendDegenerateGeometry,
                           "目标坐标不是有限值", "geometry_gate");
        for (int coordinate_axis = 0; coordinate_axis < 3; ++coordinate_axis) {
            const auto extent = coordinate(input.bbox.max, coordinate_axis) - coordinate(input.bbox.min, coordinate_axis);
            if (!std::isfinite(extent) || !(extent > tolerance))
                return failure(StatusCode::DegenerateGeometry, diag_codes::kBlendDegenerateGeometry,
                               "目标尺寸数值溢出或退化", "geometry_gate");
        }
    }
    BlendBox box;
    if (!certify_blend_box(*state, input, tolerance, box))
        return failure(StatusCode::NotImplemented, diag_codes::kBlendUnsupportedGeometry,
                       "支持边界为轴对齐矩形毛坯的当前六平面闭壳", "support_gate");

    int axis = -1;
    std::array<bool, 4> selected_corners {};
    constexpr std::array<int, 4> corner_u {0, 1, 1, 0};
    constexpr std::array<int, 4> corner_v {0, 0, 1, 1};
    for (const auto edge_id : selected) {
        const auto& edge = state->edges.at(edge_id.value);
        const auto first = box.corners.at(edge.v0.value);
        const auto second = box.corners.at(edge.v1.value);
        const int difference = first ^ second;
        const int current_axis = difference == 1 ? 0 : difference == 2 ? 1 : 2;
        if (axis != -1 && current_axis != axis)
            return failure(StatusCode::NotImplemented, diag_codes::kBlendIntersectingEdges,
                           "角区相交或非平行边组尚不支持", "intersection_gate");
        axis = current_axis;
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        for (int corner = 0; corner < 4; ++corner)
            if (((first >> u) & 1) == corner_u[corner] && ((first >> v) & 1) == corner_v[corner])
                selected_corners[corner] = true;
    }
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    const auto corner_point = [&](int corner) {
        std::array<Scalar, 3> coordinates {box.bbox.min.x, box.bbox.min.y, box.bbox.min.z};
        coordinates[u] = coordinate(corner_u[corner] ? box.bbox.max : box.bbox.min, u);
        coordinates[v] = coordinate(corner_v[corner] ? box.bbox.max : box.bbox.min, v);
        return Point3 {coordinates[0], coordinates[1], coordinates[2]};
    };
    for (int corner = 0; corner < 4; ++corner) {
        const int next = (corner + 1) % 4;
        const auto length = detail::norm(detail::subtract(corner_point(next), corner_point(corner)));
        const auto retreat = (int(selected_corners[corner]) + int(selected_corners[next])) * amount;
        if (!std::isfinite(retreat) || !(length - retreat > tolerance))
            return failure(StatusCode::OperationFailed, diag_codes::kBlendParameterTooLarge,
                           "相邻退让区重叠或剩余侧壁退化", fillet ? "radius_gate" : "distance_gate");
    }

    struct SectionVertex { Point3 point; bool arc; Point3 center; Vec3 radial_u; Vec3 radial_v; };
    std::vector<SectionVertex> section;
    for (int corner = 0; corner < 4; ++corner) {
        const auto point = corner_point(corner);
        if (!selected_corners[corner]) { section.push_back({point, false, {}, {}, {}}); continue; }
        const auto incoming = detail::normalize(detail::subtract(point, corner_point((corner + 3) % 4)));
        const auto outgoing = detail::normalize(detail::subtract(corner_point((corner + 1) % 4), point));
        const auto start = detail::add_point_vec(point, detail::scale(incoming, -amount));
        const auto end = detail::add_point_vec(point, detail::scale(outgoing, amount));
        const auto center = detail::add_point_vec(start, detail::scale(outgoing, amount));
        if (!finite_point(start) || !finite_point(end) || !finite_point(center) ||
            detail::norm(detail::subtract(start, end)) <= tolerance ||
            std::abs(detail::norm(detail::subtract(start, point)) - amount) > tolerance ||
            std::abs(detail::norm(detail::subtract(end, point)) - amount) > tolerance ||
            std::abs(detail::norm(detail::subtract(start, center)) - amount) > tolerance ||
            std::abs(detail::norm(detail::subtract(end, center)) - amount) > tolerance)
            return failure(StatusCode::DegenerateGeometry, diag_codes::kBlendDegenerateGeometry,
                           "退让坐标数值退化", "geometry_gate");
        section.push_back({start, fillet, center, detail::scale(outgoing, -1), incoming});
        section.push_back({end, false, {}, {}, {}});
    }
    const auto axial = detail::axis_normal(axis, true);
    const auto length = coordinate(box.bbox.max, axis) - coordinate(box.bbox.min, axis);
    const auto displacement = detail::scale(axial, length);
    for (std::size_t i = 0; i < section.size(); ++i) {
        const auto high = detail::add_point_vec(section[i].point, displacement);
        const auto segment_length = detail::norm(detail::subtract(section[(i + 1) % section.size()].point,
                                                                   section[i].point));
        if (!finite_point(high) || !std::isfinite(segment_length) || !(segment_length > tolerance) ||
            !std::isfinite(detail::norm(displacement)))
            return failure(StatusCode::DegenerateGeometry, diag_codes::kBlendDegenerateGeometry,
                           "结果边界坐标或区间数值退化", "geometry_gate");
    }
    // All model writes and validation caches stay in a private state. Failure
    // leaves source handles, warmed caches, Eval and the active writer unchanged.
    auto staged = std::make_shared<detail::KernelState>(state->config);
    // PluginRegistry owns unique plugin implementations and cannot be copied.
    // Validation of this analytic boundary needs model stores and topology
    // indices only; scratch runtime caches deliberately begin empty.
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
    // Preserve the caller's relation indices for source validation. Rebuilding
    // here would hide a corrupt adjacency and silently repair the input.
    staged->edge_to_coedges = state->edge_to_coedges;
    staged->coedge_to_loop = state->coedge_to_loop;
    staged->loop_to_faces = state->loop_to_faces;
    staged->face_to_shells = state->face_to_shells;
    staged->shell_to_bodies = state->shell_to_bodies;
    const auto first_id = state->next_id;
    ValidationService source_validation {staged};
    const auto source_valid = source_validation.validate_all(body_id, ValidationMode::Strict);
    if (source_valid.status != StatusCode::Ok)
        return failure(StatusCode::InvalidTopology, diag_codes::kBlendTopologyFailure,
                       "目标毛坯未通过 Strict 验证", "validation");
    const auto count = section.size();
    std::vector<VertexId> low_vertices, high_vertices;
    for (const auto& vertex : section) {
        const auto low = VertexId {staged->allocate_id()};
        const auto high = VertexId {staged->allocate_id()};
        staged->vertices.emplace(low.value, detail::VertexRecord {vertex.point});
        staged->vertices.emplace(high.value, detail::VertexRecord {detail::add_point_vec(vertex.point, displacement)});
        low_vertices.push_back(low);
        high_vertices.push_back(high);
    }
    const auto make_edge = [&](VertexId start, VertexId end, const SectionVertex* arc, bool high) {
        detail::CurveRecord curve;
        Scalar parameter_end = 0;
        if (arc && arc->arc) {
            curve.kind = detail::CurveKind::Circle;
            curve.origin = high ? detail::add_point_vec(arc->center, displacement) : arc->center;
            curve.normal = axial;
            curve.axis_u = arc->radial_u;
            curve.axis_v = arc->radial_v;
            curve.radius = amount;
            parameter_end = kBlendQuarterTurn;
        } else {
            curve.kind = detail::CurveKind::Line;
            curve.origin = staged->vertices.at(start.value).point;
            const auto difference = detail::subtract(staged->vertices.at(end.value).point, curve.origin);
            parameter_end = detail::norm(difference);
            curve.direction = detail::normalize(difference);
        }
        const auto curve_id = CurveId {staged->allocate_id()};
        staged->curves.emplace(curve_id.value, std::move(curve));
        const auto edge_id = EdgeId {staged->allocate_id()};
        staged->edges.emplace(edge_id.value, detail::EdgeRecord {curve_id, start, end, true, 0, parameter_end});
        return edge_id;
    };
    std::vector<EdgeId> low_edges, high_edges, axial_edges;
    for (std::size_t i = 0; i < count; ++i) {
        const auto next = (i + 1) % count;
        low_edges.push_back(make_edge(low_vertices[i], low_vertices[next], &section[i], false));
        high_edges.push_back(make_edge(high_vertices[i], high_vertices[next], &section[i], true));
        axial_edges.push_back(make_edge(low_vertices[i], high_vertices[i], nullptr, false));
    }
    const auto make_face = [&](detail::SurfaceRecord surface, const std::vector<std::pair<EdgeId, bool>>& boundary) {
        const auto surface_id = SurfaceId {staged->allocate_id()};
        staged->surfaces.emplace(surface_id.value, std::move(surface));
        detail::LoopRecord loop;
        for (const auto& [edge, reversed] : boundary) {
            const auto coedge_id = CoedgeId {staged->allocate_id()};
            staged->coedges.emplace(coedge_id.value, detail::CoedgeRecord {edge, reversed, {}});
            loop.coedges.push_back(coedge_id);
        }
        const auto loop_id = LoopId {staged->allocate_id()};
        staged->loops.emplace(loop_id.value, std::move(loop));
        const auto face_id = FaceId {staged->allocate_id()};
        staged->faces.emplace(face_id.value, detail::FaceRecord {surface_id, loop_id, {}, {}, false});
        return face_id;
    };
    detail::ShellRecord shell;
    for (std::size_t i = 0; i < count; ++i) {
        const auto next = (i + 1) % count;
        detail::SurfaceRecord surface;
        if (section[i].arc) {
            surface.kind = detail::SurfaceKind::Cylinder;
            surface.origin = section[i].center;
            surface.normal = surface.axis = axial;
            surface.radius_a = amount;
        } else {
            surface.kind = detail::SurfaceKind::Plane;
            surface.origin = section[i].point;
            surface.normal = detail::normalize(detail::cross(detail::subtract(section[next].point, section[i].point), axial));
        }
        shell.faces.push_back(make_face(surface, {{low_edges[i], false}, {axial_edges[next], false},
                                                  {high_edges[i], true}, {axial_edges[i], true}}));
    }
    detail::SurfaceRecord cap;
    cap.kind = detail::SurfaceKind::Plane;
    cap.origin = section.front().point;
    cap.normal = detail::scale(axial, -1);
    std::vector<std::pair<EdgeId, bool>> bottom, top;
    for (std::size_t i = 0; i < count; ++i) {
        bottom.emplace_back(low_edges[count - 1 - i], true);
        top.emplace_back(high_edges[i], false);
    }
    shell.faces.push_back(make_face(cap, bottom));
    cap.origin = detail::add_point_vec(cap.origin, displacement);
    cap.normal = axial;
    shell.faces.push_back(make_face(cap, top));
    const auto shell_id = ShellId {staged->allocate_id()};
    staged->shells.emplace(shell_id.value, std::move(shell));
    detail::BodyRecord output_record;
    output_record.kind = detail::BodyKind::BlendResult;
    output_record.label = fillet ? "fillet" : "chamfer";
    output_record.bbox = box.bbox;
    output_record.shells = {shell_id};
    output_record.source_bodies = {body_id};
    const auto output = BodyId {staged->allocate_id()};
    staged->bodies.emplace(output.value, std::move(output_record));
    detail::rebuild_topology_links(*staged);
    ValidationService validation {staged};
    const auto valid = validation.validate_all(output, ValidationMode::Strict);
    if (valid.status != StatusCode::Ok) {
        std::string reason = "真实闭壳未通过 Strict 验证";
        const auto report = staged->diagnostics.find(valid.diagnostic_id.value);
        if (report != staged->diagnostics.end() && !report->second.issues.empty())
            reason += "：" + report->second.issues.front().message;
        return failure(StatusCode::InvalidTopology, diag_codes::kBlendTopologyFailure, reason, "validation");
    }
    // Publish only newly owned model records, excluding validation diagnostics,
    // meshes and evaluation/tessellation caches. Existing runtime stays untouched.
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
    auto complete = detail::make_info_issue(diag_codes::kBlendCompleted, title + "真实解析闭壳完成");
    complete.stage = prefix + "complete";
    complete.related_entities = {body_id.value, output.value};
    const auto diag = state->create_diagnostic(title + "完成", {std::move(complete)});
    return ok_result(OpReport {StatusCode::Ok, output, diag, {}}, diag);
}

}  // namespace

BlendService::BlendService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<OpReport> BlendService::fillet_edges(BodyId body, std::span<const EdgeId> edges, Scalar radius) {
    return blend_box_edges(state_, body, edges, radius, true);
}

Result<OpReport> BlendService::chamfer_edges(BodyId body, std::span<const EdgeId> edges, Scalar distance) {
    return blend_box_edges(state_, body, edges, distance, false);
}

}  // namespace axiom
