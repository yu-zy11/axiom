#include "axiom/ops/ops_services.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
bool certify_direct_edit_box(const detail::KernelState& state, const detail::BodyRecord& body,
                             Scalar tolerance, std::array<FaceId, 6>& sides) {
    if (body.rep_kind != RepKind::ExactBRep || body.shells.size() != 1 || !body.bbox.is_valid)
        return false;
    const auto shell = state.shells.find(body.shells.front().value);
    if (shell == state.shells.end() || shell->second.faces.size() != 6) return false;
    for (const auto& [id, other] : state.bodies)
        if (&other != &body && std::find(other.shells.begin(), other.shells.end(), body.shells.front()) != other.shells.end())
            return false;
    std::map<std::uint64_t, int> corners;
    std::unordered_set<std::uint64_t> edges;
    std::unordered_set<std::uint64_t> boundary_coedges;
    std::unordered_set<int> masks;
    for (const auto face_id : shell->second.faces) {
        const auto face = state.faces.find(face_id.value);
        if (face == state.faces.end() || !face->second.inner_loops.empty() || face->second.mass_boundary_proxy)
            return false;
        for (const auto& [id, other] : state.shells)
            if (id != body.shells.front().value &&
                std::find(other.faces.begin(), other.faces.end(), face_id) != other.faces.end()) return false;
        for (const auto& [id, other] : state.faces)
            if (id != face_id.value && (other.outer_loop == face->second.outer_loop ||
                std::find(other.inner_loops.begin(), other.inner_loops.end(), face->second.outer_loop) != other.inner_loops.end()))
                return false;
        const auto surface = state.surfaces.find(face->second.surface_id.value);
        const auto loop = state.loops.find(face->second.outer_loop.value);
        if (surface == state.surfaces.end() || surface->second.kind != detail::SurfaceKind::Plane ||
            loop == state.loops.end() || loop->second.coedges.size() != 4) return false;
        std::unordered_set<int> face_masks;
        for (const auto coedge_id : loop->second.coedges) {
            const auto coedge = state.coedges.find(coedge_id.value);
            if (coedge == state.coedges.end()) return false;
            boundary_coedges.insert(coedge_id.value);
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
                const auto raw = surface->second.normal;
                const auto normal = detail::normalize(raw);
                for (int other_axis = 0; other_axis < 3; ++other_axis)
                    if (other_axis != axis && coordinate({raw.x, raw.y, raw.z}, other_axis) != 0) return false;
                const auto expected = detail::axis_normal(axis, bit != 0);
                if (!finite_point(surface->second.origin) || !finite_point({normal.x, normal.y, normal.z}) ||
                    (normal.x != expected.x || normal.y != expected.y || normal.z != expected.z) ||
                    std::abs(coordinate(surface->second.origin, axis) -
                             coordinate(bit ? body.bbox.max : body.bbox.min, axis)) > tolerance) return false;
            }
        }
        if (side < 0 || sides[side].value != 0) return false;
        sides[side] = face_id;
    }
    for (const auto& [id, edge] : state.edges)
        if (!edges.contains(id) && (corners.contains(edge.v0.value) || corners.contains(edge.v1.value))) return false;
    for (const auto& [id, coedge] : state.coedges)
        if (edges.contains(coedge.edge_id.value) && !boundary_coedges.contains(id)) return false;
    for (const auto& [id, loop] : state.loops)
        for (const auto coedge : loop.coedges)
            if (boundary_coedges.contains(coedge.value)) {
                const auto owner = state.coedge_to_loop.find(coedge.value);
                if (owner == state.coedge_to_loop.end() || owner->second != id) return false;
            }
    return corners.size() == 8 && masks.size() == 8 && edges.size() == 12;
}

enum class DirectEditKind { Move, Replace, Delete };

Result<OpReport> direct_edit(const std::shared_ptr<detail::KernelState>& state, BodyId body_id,
                            FaceId target, SurfaceId replacement, Scalar distance, DirectEditKind kind) {
    const bool moving = kind == DirectEditKind::Move;
    const bool deleting = kind == DirectEditKind::Delete;
    const std::string label = moving ? "move_face" : deleting ? "delete_face" : "replace_face";
    const std::string title = moving ? "移动面" : deleting ? "删除面补面" : "替换面";
    const std::string prefix = "modify." + label + ".";
    const auto input_code = moving ? diag_codes::kModMoveFaceInvalid : deleting
        ? diag_codes::kModDeleteFaceHealFailure : diag_codes::kModReplaceFaceIncompatible;
    const auto failure = [&](StatusCode status, std::string_view code, std::string message, std::string_view stage) {
        auto issue = detail::make_error_issue(code, title + "失败：" + message, {body_id.value, target.value});
        issue.stage = prefix + std::string(stage);
        const auto diagnostic = state->create_diagnostic(title + "失败", {std::move(issue)});
        return error_result<OpReport>(status, diagnostic);
    };
    if (!detail::has_body(*state, body_id) || !detail::has_face(*state, target) ||
        (moving && (!std::isfinite(distance) || distance == 0)) ||
        (!moving && !deleting && !detail::has_surface(*state, replacement)))
        return failure(StatusCode::InvalidInput, input_code, "目标实体、目标面或编辑参数无效", "input_gate");
    const auto& input = state->bodies.at(body_id.value);
    bool owned = false;
    for (const auto shell_id : input.shells) {
        const auto shell = state->shells.find(shell_id.value);
        if (shell != state->shells.end() &&
            std::find(shell->second.faces.begin(), shell->second.faces.end(), target) != shell->second.faces.end())
            owned = true;
    }
    if (!owned)
        return failure(StatusCode::InvalidInput, input_code, "目标面不属于实体当前边界", "input_gate");
    // A six-plane box has no redundant face whose deletion admits a certified
    // extension/intersection repair. Do not report a copied boundary as healing.
    if (deleting)
        return failure(StatusCode::NotImplemented, diag_codes::kModUnsupportedGeometry,
                       "删除补面尚无受认证的几何支持路径", "support_gate");
    const auto& policy = state->config.tolerance;
    if (!std::isfinite(policy.linear) || !std::isfinite(policy.angular) ||
        !std::isfinite(policy.min_local) || !std::isfinite(policy.max_local) ||
        !detail::valid_tolerance_policy(policy))
        return failure(StatusCode::InvalidInput, input_code, "容差策略无效", "input_gate");
    const auto tolerance = detail::resolve_linear_tolerance(0.0, policy);
    if (!std::isfinite(tolerance) || !(tolerance > 0))
        return failure(StatusCode::InvalidInput, input_code, "线性容差须为有限正数", "input_gate");
    if (!input.bbox.is_valid || !finite_point(input.bbox.min) || !finite_point(input.bbox.max))
        return failure(StatusCode::DegenerateGeometry, diag_codes::kModDegenerateGeometry,
                       "当前边界坐标无效", "geometry_gate");
    for (int axis = 0; axis < 3; ++axis) {
        const auto extent = coordinate(input.bbox.max, axis) - coordinate(input.bbox.min, axis);
        if (!std::isfinite(extent) || !(extent > 2 * tolerance))
            return failure(StatusCode::DegenerateGeometry, diag_codes::kModDegenerateGeometry,
                           "当前尺寸溢出或退化", "geometry_gate");
    }
    std::array<FaceId, 6> sides {};
    if (!certify_direct_edit_box(*state, input, tolerance, sides))
        return failure(StatusCode::NotImplemented, diag_codes::kModUnsupportedGeometry,
                       "仅支持当前完整且独占的轴对齐六平面矩形闭壳", "support_gate");
    const int side = static_cast<int>(std::find(sides.begin(), sides.end(), target) - sides.begin());
    const int axis = side / 2;
    const bool positive = side % 2 != 0;
    const auto original = coordinate(positive ? input.bbox.max : input.bbox.min, axis);
    Scalar updated = original;
    if (moving) {
        if (!(std::abs(distance) > tolerance))
            return failure(StatusCode::DegenerateGeometry, diag_codes::kModDegenerateGeometry,
                           "移动距离不大于线性容差", "geometry_gate");
        const auto requested = positive ? distance : -distance;
        updated += requested;
        const long double actual = static_cast<long double>(updated) - original;
        if (!std::isfinite(updated) || std::abs(actual) <= tolerance ||
            std::abs(actual - requested) > tolerance)
            return failure(StatusCode::DegenerateGeometry, diag_codes::kModDegenerateGeometry,
                           "坐标不能表示请求位移", "geometry_gate");
    } else {
        const auto& plane = state->surfaces.at(replacement.value);
        const auto raw = plane.normal;
        if (plane.kind != detail::SurfaceKind::Plane || !finite_point(plane.origin) ||
            !finite_point({raw.x, raw.y, raw.z}) || coordinate({raw.x, raw.y, raw.z}, axis) == 0)
            return failure(StatusCode::InvalidInput, diag_codes::kModReplaceFaceIncompatible,
                           "替换曲面须为有限且与目标面平行的轴对齐 Plane", "support_gate");
        for (int other_axis = 0; other_axis < 3; ++other_axis)
            if (other_axis != axis && coordinate({raw.x, raw.y, raw.z}, other_axis) != 0)
                return failure(StatusCode::InvalidInput, diag_codes::kModReplaceFaceIncompatible,
                               "替换 Plane 不平行于目标面", "support_gate");
        // Exact axis parallelism makes the full plane equation independent of
        // transverse origin/extent; either normal sign defines the same plane.
        updated = coordinate(plane.origin, axis);
        const long double actual = static_cast<long double>(updated) - original;
        if (!std::isfinite(actual) || std::abs(actual) <= tolerance)
            return failure(StatusCode::DegenerateGeometry, diag_codes::kModDegenerateGeometry,
                           "替换面位移不大于线性容差或无法表示", "geometry_gate");
    }
    BoundingBox boundary = input.bbox;
    Point3& endpoint = positive ? boundary.max : boundary.min;
    if (axis == 0) endpoint.x = updated;
    else if (axis == 1) endpoint.y = updated;
    else endpoint.z = updated;
    std::array<Scalar, 3> extents {};
    for (int direction = 0; direction < 3; ++direction) {
        extents[direction] = coordinate(boundary.max, direction) - coordinate(boundary.min, direction);
        if (!std::isfinite(extents[direction]) || !(extents[direction] > 2 * tolerance))
            return failure(StatusCode::DegenerateGeometry, diag_codes::kModDegenerateGeometry,
                           "编辑导致边界交叉、塌缩或接近容差", "geometry_gate");
    }
    const long double x = extents[0], y = extents[1], z = extents[2];
    const long double volume = x * y * z, area = 2 * (x * y + y * z + z * x);
    const long double maximum = std::numeric_limits<Scalar>::max();
    if (!std::isfinite(volume) || !std::isfinite(area) || volume > maximum || area > maximum)
        return failure(StatusCode::DegenerateGeometry, diag_codes::kModDegenerateGeometry,
                       "编辑结果质量超出可表示范围", "geometry_gate");

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
    const auto source_valid = validation.validate_all(body_id, ValidationMode::Strict);
    if (source_valid.status != StatusCode::Ok) {
        std::string reason = "当前源边界未通过 Strict";
        const auto report = staged->diagnostics.find(source_valid.diagnostic_id.value);
        if (report != staged->diagnostics.end() && !report->second.issues.empty())
            reason += "：" + report->second.issues.front().message;
        return failure(StatusCode::InvalidTopology, diag_codes::kModDirectEditValidateFailed, reason, "source_validate");
    }
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
    const auto vertices = make_vertices(boundary);
    detail::ShellRecord shell;
    shell.source_shells = input.shells;
    shell.source_faces = std::vector<FaceId>(sides.begin(), sides.end());
    for (int output_side = 0; output_side < 6; ++output_side) {
        const auto masks = side_masks(output_side);
        std::array<VertexId, 4> corners {};
        for (int corner = 0; corner < 4; ++corner) corners[corner] = vertices[masks[corner]];
        shell.faces.push_back(make_face(corners, detail::axis_normal(output_side / 2, output_side % 2 != 0),
                                        sides[output_side]));
    }
    const auto shell_id = ShellId {staged->allocate_id()};
    staged->shells.emplace(shell_id.value, std::move(shell));
    detail::BodyRecord output_record;
    // Generic/ExactBRep forces representation and queries to consume the real
    // owned boundary, without retaining primitive creation parameters/caches.
    output_record.kind = detail::BodyKind::Generic;
    output_record.label = label;
    output_record.bbox = boundary;
    output_record.shells = {shell_id};
    output_record.source_bodies = {body_id};
    output_record.source_shells = input.shells;
    output_record.source_faces = std::vector<FaceId>(sides.begin(), sides.end());
    const auto output = BodyId {staged->allocate_id()};
    staged->bodies.emplace(output.value, std::move(output_record));
    detail::rebuild_topology_links(*staged);
    const auto valid = validation.validate_all(output, ValidationMode::Strict);
    if (valid.status != StatusCode::Ok) {
        std::string reason = "真实编辑结果未通过 Strict";
        const auto report = staged->diagnostics.find(valid.diagnostic_id.value);
        if (report != staged->diagnostics.end() && !report->second.issues.empty())
            reason += "：" + report->second.issues.front().message;
        return failure(StatusCode::InvalidTopology, diag_codes::kModDirectEditValidateFailed, reason, "validate");
    }
    // Publish only appended owned records and their indices; discard scratch
    // validation diagnostics, meshes and caches. All failures above are isolated.
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
    detail::invalidate_eval_for_bodies(*state, {body_id});
    auto complete = detail::make_info_issue(diag_codes::kModCompleted, title + "真实边界完成");
    complete.stage = prefix + "complete";
    complete.related_entities = {body_id.value, target.value, output.value};
    const auto diagnostic = state->create_diagnostic(title + "完成", {std::move(complete)});
    return ok_result(OpReport {StatusCode::Ok, output, diagnostic, {}}, diagnostic);
}

}  // namespace

Result<OpReport> ModifyService::move_face(BodyId body, FaceId target, Scalar signed_distance) {
    return direct_edit(state_, body, target, {}, signed_distance, DirectEditKind::Move);
}

Result<OpReport> ModifyService::replace_face(BodyId body, FaceId target, SurfaceId replacement) {
    return direct_edit(state_, body, target, replacement, 0, DirectEditKind::Replace);
}

Result<OpReport> ModifyService::delete_face_and_heal(BodyId body, FaceId target) {
    return direct_edit(state_, body, target, {}, 0, DirectEditKind::Delete);
}

}  // namespace axiom
