#include "axiom/ops/ops_services.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "axiom/heal/heal_services.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/eval_graph_invalidation.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/core/topology_materialization.h"

namespace axiom {

namespace {

constexpr Scalar kPi = 3.14159265358979323846;

enum class BBoxRelation {
    Disjoint,
    Touching,
    Overlapping,
    LhsContainsRhs,
    RhsContainsLhs
};

BodyId make_body(std::shared_ptr<detail::KernelState> state, detail::BodyRecord record, const std::string&) {
    if (record.kind == detail::BodyKind::BooleanResult || record.kind == detail::BodyKind::Modified ||
        record.kind == detail::BodyKind::BlendResult || record.kind == detail::BodyKind::Sweep) {
        detail::materialize_body_bbox_topology(*state, record);
    }
    const auto id = BodyId {state->allocate_id()};
    state->bodies.emplace(id.value, std::move(record));
    detail::rebuild_topology_links(*state);
    return id;
}

void append_unique_body(std::vector<BodyId>& ids, BodyId id) {
    if (std::none_of(ids.begin(), ids.end(), [id](BodyId current) { return current.value == id.value; })) {
        ids.push_back(id);
    }
}

void append_unique_face(std::vector<FaceId>& ids, FaceId id) {
    if (std::none_of(ids.begin(), ids.end(), [id](FaceId current) { return current.value == id.value; })) {
        ids.push_back(id);
    }
}

void append_unique_shell(std::vector<ShellId>& ids, ShellId id) {
    if (std::none_of(ids.begin(), ids.end(), [id](ShellId current) { return current.value == id.value; })) {
        ids.push_back(id);
    }
}

void append_shells_for_face(const detail::KernelState& state, std::vector<ShellId>& ids, FaceId face_id) {
    const auto shell_it = state.face_to_shells.find(face_id.value);
    if (shell_it == state.face_to_shells.end()) {
        return;
    }
    for (const auto shell_value : shell_it->second) {
        append_unique_shell(ids, ShellId {shell_value});
    }
}

void append_shell_provenance_for_body(const detail::KernelState& state, std::vector<ShellId>& ids, BodyId body_id) {
    const auto body_it = state.bodies.find(body_id.value);
    if (body_it == state.bodies.end()) {
        return;
    }
    if (!body_it->second.source_shells.empty()) {
        for (const auto shell_id : body_it->second.source_shells) {
            append_unique_shell(ids, shell_id);
        }
        return;
    }
    for (const auto shell_id : body_it->second.shells) {
        append_unique_shell(ids, shell_id);
    }
}

void append_face_provenance_for_body(const detail::KernelState& state, std::vector<FaceId>& ids, BodyId body_id) {
    const auto body_it = state.bodies.find(body_id.value);
    if (body_it == state.bodies.end()) {
        return;
    }
    for (const auto face_id : body_it->second.source_faces) {
        append_unique_face(ids, face_id);
    }
}

void detach_owned_topology(const detail::KernelState& state, detail::BodyRecord& record) {
    detail::inherit_source_topology_from_owned_shells(state, record);
    record.shells.clear();
}

void append_faces_for_edge(const detail::KernelState& state, std::vector<FaceId>& ids, EdgeId edge_id) {
    const auto coedge_it = state.edge_to_coedges.find(edge_id.value);
    if (coedge_it == state.edge_to_coedges.end()) {
        return;
    }
    for (const auto coedge_value : coedge_it->second) {
        const auto loop_it = state.coedge_to_loop.find(coedge_value);
        if (loop_it == state.coedge_to_loop.end()) {
            continue;
        }
        const auto face_it = state.loop_to_faces.find(loop_it->second);
        if (face_it == state.loop_to_faces.end()) {
            continue;
        }
        for (const auto face_value : face_it->second) {
            append_unique_face(ids, FaceId {face_value});
        }
    }
}

OpReport make_report(StatusCode status, BodyId output, DiagnosticId diagnostic_id, std::vector<Warning> warnings = {}) {
    OpReport report;
    report.status = status;
    report.output = output;
    report.diagnostic_id = diagnostic_id;
    report.warnings = std::move(warnings);
    return report;
}

BoundingBox box_bbox(const Point3& origin, Scalar dx, Scalar dy, Scalar dz) {
    return detail::make_bbox(origin, Point3 {origin.x + dx, origin.y + dy, origin.z + dz});
}

BoundingBox cylinder_bbox(const Point3& center, const Vec3& axis_unit, Scalar radius, Scalar height) {
    const auto hx = std::abs(axis_unit.x) * height * 0.5;
    const auto hy = std::abs(axis_unit.y) * height * 0.5;
    const auto hz = std::abs(axis_unit.z) * height * 0.5;
    return detail::make_bbox(
        {center.x - radius - hx, center.y - radius - hy, center.z - radius - hz},
        {center.x + radius + hx, center.y + radius + hy, center.z + radius + hz});
}

BoundingBox cone_bbox(const Point3& apex, const Vec3& axis_unit, Scalar semi_angle, Scalar height) {
    const auto radius = std::max<Scalar>(0.0, height * std::tan(semi_angle));
    const auto mid = detail::add_point_vec(apex, detail::scale(axis_unit, height * 0.5));
    const auto hx = std::abs(axis_unit.x) * height * 0.5;
    const auto hy = std::abs(axis_unit.y) * height * 0.5;
    const auto hz = std::abs(axis_unit.z) * height * 0.5;
    return detail::make_bbox(
        {mid.x - radius - hx, mid.y - radius - hy, mid.z - radius - hz},
        {mid.x + radius + hx, mid.y + radius + hy, mid.z + radius + hz});
}

MassProperties bbox_mass_properties(const BoundingBox& bbox) {
    MassProperties props {};
    if (!bbox.is_valid) {
        return props;
    }

    const auto dx = std::max(0.0, bbox.max.x - bbox.min.x);
    const auto dy = std::max(0.0, bbox.max.y - bbox.min.y);
    const auto dz = std::max(0.0, bbox.max.z - bbox.min.z);

    props.volume = dx * dy * dz;
    props.area = 2.0 * (dx * dy + dy * dz + dx * dz);
    props.centroid = Point3 {
        (bbox.min.x + bbox.max.x) * 0.5,
        (bbox.min.y + bbox.max.y) * 0.5,
        (bbox.min.z + bbox.max.z) * 0.5
    };
    return props;
}

Scalar interval_gap(Scalar a_min, Scalar a_max, Scalar b_min, Scalar b_max) {
    if (a_max < b_min) {
        return b_min - a_max;
    }
    if (b_max < a_min) {
        return a_min - b_max;
    }
    return 0.0;
}

bool contains_bbox(const BoundingBox& outer, const BoundingBox& inner) {
    return outer.is_valid && inner.is_valid &&
           outer.min.x <= inner.min.x && outer.max.x >= inner.max.x &&
           outer.min.y <= inner.min.y && outer.max.y >= inner.max.y &&
           outer.min.z <= inner.min.z && outer.max.z >= inner.max.z;
}

Scalar overlap_extent(Scalar a_min, Scalar a_max, Scalar b_min, Scalar b_max) {
    return std::min(a_max, b_max) - std::max(a_min, b_min);
}

std::vector<Point3> bbox_corners(const BoundingBox& bbox) {
    return {
        {bbox.min.x, bbox.min.y, bbox.min.z},
        {bbox.max.x, bbox.min.y, bbox.min.z},
        {bbox.max.x, bbox.max.y, bbox.min.z},
        {bbox.min.x, bbox.max.y, bbox.min.z},
        {bbox.min.x, bbox.min.y, bbox.max.z},
        {bbox.max.x, bbox.min.y, bbox.max.z},
        {bbox.max.x, bbox.max.y, bbox.max.z},
        {bbox.min.x, bbox.max.y, bbox.max.z},
    };
}

BoundingBox curve_bbox_for_query(const detail::CurveRecord& curve) {
    switch (curve.kind) {
        case detail::CurveKind::Line:
            return detail::bbox_from_center_radius(curve.origin, 1000.0, 1000.0, 1000.0);
        case detail::CurveKind::LineSegment: {
            if (curve.poles.size() >= 2) {
                const auto& a = curve.poles[0];
                const auto& b = curve.poles[1];
                return detail::make_bbox(
                    {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)},
                    {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)});
            }
            return detail::bbox_from_center_radius(curve.origin, 10.0, 10.0, 10.0);
        }
        case detail::CurveKind::Circle:
            return detail::bbox_from_center_radius(curve.origin, curve.radius, curve.radius, curve.radius);
        case detail::CurveKind::Ellipse:
            return detail::bbox_from_center_radius(curve.origin, detail::norm(curve.axis_u), detail::norm(curve.axis_v), 1.0);
        case detail::CurveKind::Parabola:
        case detail::CurveKind::Hyperbola:
        case detail::CurveKind::CompositePolyline:
            if (!curve.poles.empty()) {
                auto min = curve.poles.front();
                auto max = curve.poles.front();
                for (const auto& p : curve.poles) {
                    min.x = std::min(min.x, p.x);
                    min.y = std::min(min.y, p.y);
                    min.z = std::min(min.z, p.z);
                    max.x = std::max(max.x, p.x);
                    max.y = std::max(max.y, p.y);
                    max.z = std::max(max.z, p.z);
                }
                return detail::make_bbox(min, max);
            }
            return detail::bbox_from_center_radius(curve.origin, 10.0, 10.0, 10.0);
        default:
            return detail::bbox_from_center_radius(curve.origin, 10.0, 10.0, 10.0);
    }
}

BoundingBox surface_bbox_for_query(const detail::SurfaceRecord& surface) {
    switch (surface.kind) {
        case detail::SurfaceKind::Plane:
            return detail::bbox_from_center_radius(surface.origin, 1000.0, 1000.0, 0.0);
        case detail::SurfaceKind::Sphere:
            return detail::bbox_from_center_radius(surface.origin, surface.radius_a, surface.radius_a, surface.radius_a);
        case detail::SurfaceKind::Cylinder:
            return detail::bbox_from_center_radius(surface.origin, surface.radius_a, surface.radius_a, 1000.0);
        case detail::SurfaceKind::Cone:
            return detail::bbox_from_center_radius(surface.origin, 1000.0, 1000.0, 1000.0);
        case detail::SurfaceKind::Torus:
            return detail::bbox_from_center_radius(surface.origin, surface.radius_a + surface.radius_b,
                                                   surface.radius_a + surface.radius_b, surface.radius_b);
        case detail::SurfaceKind::Bezier:
        case detail::SurfaceKind::BSpline:
        case detail::SurfaceKind::Nurbs:
            if (!surface.poles.empty()) {
                auto min = surface.poles.front();
                auto max = surface.poles.front();
                for (const auto& p : surface.poles) {
                    min.x = std::min(min.x, p.x);
                    min.y = std::min(min.y, p.y);
                    min.z = std::min(min.z, p.z);
                    max.x = std::max(max.x, p.x);
                    max.y = std::max(max.y, p.y);
                    max.z = std::max(max.z, p.z);
                }
                return detail::make_bbox(min, max);
            }
            return detail::bbox_from_center_radius(surface.origin, 10.0, 10.0, 10.0);
        default:
            return detail::bbox_from_center_radius(surface.origin, 10.0, 10.0, 10.0);
    }
}

Scalar plane_signed_distance(const Plane& plane, const Point3& point) {
    const auto dx = point.x - plane.origin.x;
    const auto dy = point.y - plane.origin.y;
    const auto dz = point.z - plane.origin.z;
    return dx * plane.normal.x + dy * plane.normal.y + dz * plane.normal.z;
}

bool intersects_plane(const BoundingBox& bbox, const Plane& plane) {
    if (!bbox.is_valid) {
        return false;
    }

    bool has_positive = false;
    bool has_negative = false;
    for (const auto& corner : bbox_corners(bbox)) {
        const auto distance = plane_signed_distance(plane, corner);
        has_positive = has_positive || distance >= 0.0;
        has_negative = has_negative || distance <= 0.0;
    }
    return has_positive && has_negative;
}

BoundingBox union_bbox(const BoundingBox& lhs, const BoundingBox& rhs) {
    return detail::make_bbox(
        {std::min(lhs.min.x, rhs.min.x), std::min(lhs.min.y, rhs.min.y), std::min(lhs.min.z, rhs.min.z)},
        {std::max(lhs.max.x, rhs.max.x), std::max(lhs.max.y, rhs.max.y), std::max(lhs.max.z, rhs.max.z)});
}

BoundingBox intersect_bbox(const BoundingBox& lhs, const BoundingBox& rhs) {
    const Point3 min {
        std::max(lhs.min.x, rhs.min.x),
        std::max(lhs.min.y, rhs.min.y),
        std::max(lhs.min.z, rhs.min.z),
    };
    const Point3 max {
        std::min(lhs.max.x, rhs.max.x),
        std::min(lhs.max.y, rhs.max.y),
        std::min(lhs.max.z, rhs.max.z),
    };
    if (min.x > max.x || min.y > max.y || min.z > max.z) {
        return {};
    }
    return detail::make_bbox(min, max);
}

BBoxRelation classify_bbox_relation(const BoundingBox& lhs, const BoundingBox& rhs) {
    const auto ix = overlap_extent(lhs.min.x, lhs.max.x, rhs.min.x, rhs.max.x);
    const auto iy = overlap_extent(lhs.min.y, lhs.max.y, rhs.min.y, rhs.max.y);
    const auto iz = overlap_extent(lhs.min.z, lhs.max.z, rhs.min.z, rhs.max.z);
    if (ix < 0.0 || iy < 0.0 || iz < 0.0) {
        return BBoxRelation::Disjoint;
    }
    if (contains_bbox(lhs, rhs)) {
        return BBoxRelation::LhsContainsRhs;
    }
    if (contains_bbox(rhs, lhs)) {
        return BBoxRelation::RhsContainsLhs;
    }
    if (ix == 0.0 || iy == 0.0 || iz == 0.0) {
        return BBoxRelation::Touching;
    }
    return BBoxRelation::Overlapping;
}

Scalar bbox_min_extent(const BoundingBox& bbox) {
    return std::min({bbox.max.x - bbox.min.x, bbox.max.y - bbox.min.y, bbox.max.z - bbox.min.z});
}

BoundingBox offset_bbox(const BoundingBox& bbox, Scalar distance) {
    return detail::make_bbox(
        {bbox.min.x - distance, bbox.min.y - distance, bbox.min.z - distance},
        {bbox.max.x + distance, bbox.max.y + distance, bbox.max.z + distance});
}

bool valid_axis(const Vec3& axis) {
    return detail::norm(axis) > 0.0;
}

bool valid_face_ids(const detail::KernelState& state, std::span<const FaceId> faces) {
    return std::all_of(faces.begin(), faces.end(), [&state](const FaceId face_id) {
        return detail::has_face(state, face_id);
    });
}

bool valid_edge_ids(const detail::KernelState& state, std::span<const EdgeId> edges) {
    return std::all_of(edges.begin(), edges.end(), [&state](const EdgeId edge_id) {
        return detail::has_edge(state, edge_id);
    });
}

BoundingBox face_bbox(const detail::KernelState& state, FaceId face_id) {
    const auto face_it = state.faces.find(face_id.value);
    if (face_it == state.faces.end()) {
        return {};
    }
    std::vector<LoopId> loops;
    loops.push_back(face_it->second.outer_loop);
    loops.insert(loops.end(), face_it->second.inner_loops.begin(), face_it->second.inner_loops.end());

    BoundingBox bbox;
    bool initialized = false;
    for (const auto loop_id : loops) {
        const auto loop_it = state.loops.find(loop_id.value);
        if (loop_it == state.loops.end()) {
            return {};
        }
        for (const auto coedge_id : loop_it->second.coedges) {
            const auto coedge_it = state.coedges.find(coedge_id.value);
            if (coedge_it == state.coedges.end()) {
                return {};
            }
            const auto edge_it = state.edges.find(coedge_it->second.edge_id.value);
            if (edge_it == state.edges.end()) {
                return {};
            }
            const auto v0_it = state.vertices.find(edge_it->second.v0.value);
            const auto v1_it = state.vertices.find(edge_it->second.v1.value);
            if (v0_it == state.vertices.end() || v1_it == state.vertices.end()) {
                return {};
            }
            for (const auto& point : {v0_it->second.point, v1_it->second.point}) {
                if (!initialized) {
                    bbox = detail::make_bbox(point, point);
                    initialized = true;
                } else {
                    bbox.min.x = std::min(bbox.min.x, point.x);
                    bbox.min.y = std::min(bbox.min.y, point.y);
                    bbox.min.z = std::min(bbox.min.z, point.z);
                    bbox.max.x = std::max(bbox.max.x, point.x);
                    bbox.max.y = std::max(bbox.max.y, point.y);
                    bbox.max.z = std::max(bbox.max.z, point.z);
                }
            }
        }
    }
    return initialized ? bbox : BoundingBox {};
}

BoundingBox shell_bbox(const detail::KernelState& state, ShellId shell_id) {
    const auto shell_it = state.shells.find(shell_id.value);
    if (shell_it == state.shells.end()) {
        return {};
    }
    BoundingBox bbox;
    bool initialized = false;
    for (const auto face_id : shell_it->second.faces) {
        const auto face_it = state.faces.find(face_id.value);
        if (face_it == state.faces.end()) {
            return {};
        }
        std::vector<LoopId> loops;
        loops.push_back(face_it->second.outer_loop);
        loops.insert(loops.end(), face_it->second.inner_loops.begin(), face_it->second.inner_loops.end());
        for (const auto loop_id : loops) {
            const auto loop_it = state.loops.find(loop_id.value);
            if (loop_it == state.loops.end()) {
                return {};
            }
            for (const auto coedge_id : loop_it->second.coedges) {
                const auto coedge_it = state.coedges.find(coedge_id.value);
                if (coedge_it == state.coedges.end()) {
                    return {};
                }
                const auto edge_it = state.edges.find(coedge_it->second.edge_id.value);
                if (edge_it == state.edges.end()) {
                    return {};
                }
                const auto v0_it = state.vertices.find(edge_it->second.v0.value);
                const auto v1_it = state.vertices.find(edge_it->second.v1.value);
                if (v0_it == state.vertices.end() || v1_it == state.vertices.end()) {
                    return {};
                }
                for (const auto& point : {v0_it->second.point, v1_it->second.point}) {
                    if (!initialized) {
                        bbox = detail::make_bbox(point, point);
                        initialized = true;
                    } else {
                        bbox.min.x = std::min(bbox.min.x, point.x);
                        bbox.min.y = std::min(bbox.min.y, point.y);
                        bbox.min.z = std::min(bbox.min.z, point.z);
                        bbox.max.x = std::max(bbox.max.x, point.x);
                        bbox.max.y = std::max(bbox.max.y, point.y);
                        bbox.max.z = std::max(bbox.max.z, point.z);
                    }
                }
            }
        }
    }
    return initialized ? bbox : BoundingBox {};
}

std::vector<ShellId> shells_for_body(const detail::BodyRecord& body) {
    if (!body.shells.empty()) {
        return body.shells;
    }
    return body.source_shells;
}

struct BooleanPrepStats {
    std::size_t lhs_regions {0};
    std::size_t rhs_regions {0};
    std::size_t overlap_candidates {0};
    Scalar overlap_volume_sum {0.0};
    BoundingBox local_overlap_bbox {};
    bool local_clip_applied {false};
};

std::vector<BoundingBox> body_regions_for_boolean(const detail::KernelState& state, BodyId body_id) {
    std::vector<BoundingBox> regions;
    if (!detail::has_body(state, body_id)) {
        return regions;
    }
    const auto& body = state.bodies.at(body_id.value);
    const auto shells = shells_for_body(body);
    for (const auto shell_id : shells) {
        const auto bbox = shell_bbox(state, shell_id);
        if (bbox.is_valid) {
            regions.push_back(bbox);
        }
    }
    if (regions.empty() && body.bbox.is_valid) {
        regions.push_back(body.bbox);
    }
    return regions;
}

Scalar bbox_volume(const BoundingBox& bbox) {
    if (!bbox.is_valid) {
        return 0.0;
    }
    const auto dx = std::max(0.0, bbox.max.x - bbox.min.x);
    const auto dy = std::max(0.0, bbox.max.y - bbox.min.y);
    const auto dz = std::max(0.0, bbox.max.z - bbox.min.z);
    return dx * dy * dz;
}

BooleanPrepStats compute_boolean_prep_stats(const detail::KernelState& state, BodyId lhs, BodyId rhs) {
    BooleanPrepStats stats;
    const auto lhs_regions = body_regions_for_boolean(state, lhs);
    const auto rhs_regions = body_regions_for_boolean(state, rhs);
    stats.lhs_regions = lhs_regions.size();
    stats.rhs_regions = rhs_regions.size();

    for (const auto& lhs_bbox : lhs_regions) {
        for (const auto& rhs_bbox : rhs_regions) {
            const auto overlap = intersect_bbox(lhs_bbox, rhs_bbox);
            if (!overlap.is_valid) {
                continue;
            }
            ++stats.overlap_candidates;
            stats.overlap_volume_sum += bbox_volume(overlap);
            if (!stats.local_overlap_bbox.is_valid) {
                stats.local_overlap_bbox = overlap;
            } else {
                stats.local_overlap_bbox = union_bbox(stats.local_overlap_bbox, overlap);
            }
        }
    }
    stats.local_clip_applied = stats.local_overlap_bbox.is_valid;
    return stats;
}

void append_boolean_prep_candidate_issue(detail::KernelState& state,
                                         DiagnosticId diag,
                                         BodyId lhs,
                                         BodyId rhs,
                                         const BooleanPrepStats& prep) {
    if (diag.value == 0) {
        return;
    }
    std::ostringstream message;
    message << "布尔预处理候选片段构建完成: lhs_regions=" << prep.lhs_regions
            << ", rhs_regions=" << prep.rhs_regions
            << ", overlap_candidates=" << prep.overlap_candidates
            << ", overlap_volume_sum=" << prep.overlap_volume_sum;
    auto issue = detail::make_info_issue(diag_codes::kBoolPrepCandidatesBuilt, message.str());
    issue.related_entities = {lhs.value, rhs.value};
    state.append_diagnostic_issue(diag, std::move(issue));

    if (prep.local_clip_applied) {
        auto clip_issue = detail::make_info_issue(
            diag_codes::kBoolLocalClipApplied,
            "布尔局部裁剪已应用：结果 bbox 已按候选重叠区域收缩");
        clip_issue.related_entities = {lhs.value, rhs.value};
        state.append_diagnostic_issue(diag, std::move(clip_issue));
    }
}

IntersectionId store_intersection(std::shared_ptr<detail::KernelState> state,
                                  std::string label,
                                  BoundingBox bbox,
                                  std::vector<CurveId> curves = {},
                                  std::vector<SurfaceId> surfaces = {}) {
    const auto id = IntersectionId {state->allocate_id()};
    detail::IntersectionRecord record;
    record.label = std::move(label);
    record.bbox = bbox;
    record.curves = std::move(curves);
    record.surfaces = std::move(surfaces);
    state->intersections.emplace(id.value, std::move(record));
    return id;
}

}  // namespace

PrimitiveService::PrimitiveService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<BodyId> PrimitiveService::box(const Point3& origin, Scalar dx, Scalar dy, Scalar dz) {
    if (dx <= 0.0 || dy <= 0.0 || dz <= 0.0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "盒体创建失败：边长必须大于 0", "盒体创建失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Box;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "box";
    record.origin = origin;
    record.a = dx;
    record.b = dy;
    record.c = dz;
    record.bbox = box_bbox(origin, dx, dy, dz);
    return ok_result(make_body(state_, record, "已创建盒体"), state_->create_diagnostic("已创建盒体"));
}

Result<BodyId> PrimitiveService::sphere(const Point3& center, Scalar radius) {
    if (radius <= 0.0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "球体创建失败：半径必须大于 0", "球体创建失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sphere;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "sphere";
    record.origin = center;
    record.a = radius;
    record.bbox = detail::bbox_from_center_radius(center, radius, radius, radius);
    return ok_result(make_body(state_, record, "已创建球体"), state_->create_diagnostic("已创建球体"));
}

Result<BodyId> PrimitiveService::cylinder(const Point3& center, const Vec3& axis, Scalar radius, Scalar height) {
    if (radius <= 0.0 || height <= 0.0 || !valid_axis(axis)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "圆柱体创建失败：轴向量必须有效且半径、高度必须大于 0", "圆柱体创建失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Cylinder;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "cylinder";
    record.origin = center;
    record.axis = detail::normalize(axis);
    record.a = radius;
    record.b = height;
    record.bbox = cylinder_bbox(center, record.axis, radius, height);
    return ok_result(make_body(state_, record, "已创建圆柱体"), state_->create_diagnostic("已创建圆柱体"));
}

Result<BodyId> PrimitiveService::cone(const Point3& apex, const Vec3& axis, Scalar semi_angle, Scalar height) {
    if (height <= 0.0 || semi_angle <= 0.0 || !valid_axis(axis)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "圆锥体创建失败：轴向量必须有效，半角和高度必须大于 0", "圆锥体创建失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Cone;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "cone";
    record.origin = apex;
    record.axis = detail::normalize(axis);
    record.a = semi_angle;
    record.b = height;
    record.bbox = cone_bbox(apex, record.axis, semi_angle, height);
    return ok_result(make_body(state_, record, "已创建圆锥体"), state_->create_diagnostic("已创建圆锥体"));
}

Result<BodyId> PrimitiveService::torus(const Point3& center, const Vec3& axis, Scalar major_r, Scalar minor_r) {
    if (major_r <= 0.0 || minor_r <= 0.0 || major_r <= minor_r || !valid_axis(axis)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "环体创建失败：轴向量必须有效，且主半径必须大于副半径并同时大于 0", "环体创建失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Torus;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "torus";
    record.origin = center;
    record.axis = detail::normalize(axis);
    record.a = major_r;
    record.b = minor_r;
    record.bbox = detail::bbox_from_center_radius(center, major_r + minor_r, major_r + minor_r, minor_r);
    return ok_result(make_body(state_, record, "已创建环体"), state_->create_diagnostic("已创建环体"));
}

SweepService::SweepService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<BodyId> SweepService::extrude(const ProfileRef& profile, const Vec3& direction, Scalar distance) {
    if (profile.label.empty() || distance <= 0.0 || !valid_axis(direction)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "拉伸失败：轮廓不能为空，方向必须有效且距离必须大于 0", "拉伸失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "extrude:" + profile.label;
    const auto dir = detail::normalize(direction);
    const auto p0 = Point3{0.0, 0.0, 0.0};
    const auto p1 = detail::add_point_vec(p0, detail::scale(dir, distance));
    // minimal profile extent: unit square around origin
    const auto minx = std::min(p0.x, p1.x) - 0.5;
    const auto maxx = std::max(p0.x, p1.x) + 0.5;
    const auto miny = std::min(p0.y, p1.y) - 0.5;
    const auto maxy = std::max(p0.y, p1.y) + 0.5;
    const auto minz = std::min(p0.z, p1.z) - 0.5;
    const auto maxz = std::max(p0.z, p1.z) + 0.5;
    record.bbox = detail::make_bbox({minx, miny, minz}, {maxx, maxy, maxz});
    return ok_result(make_body(state_, record, "已完成拉伸"), state_->create_diagnostic("已完成拉伸"));
}

Result<BodyId> SweepService::revolve(const ProfileRef& profile, const Axis3& axis, Scalar angle) {
    if (profile.label.empty() || angle <= 0.0 || !valid_axis(axis.direction)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "旋转失败：轮廓不能为空，旋转轴必须有效且角度必须大于 0", "旋转失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "revolve:" + profile.label;
    // minimal revolve bbox: assume profile within unit radius around axis origin
    record.bbox = detail::bbox_from_center_radius(axis.origin, 1.0, 1.0, 1.0);
    return ok_result(make_body(state_, record, "已完成旋转"), state_->create_diagnostic("已完成旋转"));
}

Result<BodyId> SweepService::sweep(const ProfileRef& profile, CurveId rail) {
    if (profile.label.empty() || !detail::has_curve(*state_, rail)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreInvalidHandle,
            "扫描失败：轮廓为空或导轨曲线不存在", "扫描失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "sweep:" + profile.label;
    const auto& curve = state_->curves.at(rail.value);
    auto bbox = curve_bbox_for_query(curve);
    if (bbox.is_valid) {
        bbox = offset_bbox(bbox, 0.5);
    }
    record.bbox = bbox.is_valid ? bbox : detail::make_bbox({0.0, 0.0, 0.0}, {2.0, 2.0, 2.0});
    return ok_result(make_body(state_, record, "已完成扫描"), state_->create_diagnostic("已完成扫描"));
}

Result<BodyId> SweepService::loft(std::span<const ProfileRef> profiles) {
    if (profiles.size() < 2 || std::any_of(profiles.begin(), profiles.end(), [](const ProfileRef& profile) { return profile.label.empty(); })) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "放样失败：至少需要两个有效截面", "放样失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "loft";
    // minimal loft bbox: size scales with profile count
    const auto extent = std::max<Scalar>(2.0, static_cast<Scalar>(profiles.size()));
    record.bbox = detail::make_bbox({0.0, 0.0, 0.0}, {extent, extent, extent});
    return ok_result(make_body(state_, record, "已完成放样"), state_->create_diagnostic("已完成放样"));
}

Result<BodyId> SweepService::thicken(FaceId face_id, Scalar distance) {
    if (state_->faces.find(face_id.value) == state_->faces.end() || distance <= 0.0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kModShellFailure,
            "加厚失败：目标面不存在或厚度非法", "加厚失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "thicken";
    append_unique_face(record.source_faces, face_id);
    append_shells_for_face(*state_, record.source_shells, face_id);
    auto bbox = face_bbox(*state_, face_id);
    if (!bbox.is_valid) {
        bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});
    }
    record.bbox = offset_bbox(bbox, distance);
    return ok_result(make_body(state_, record, "已完成加厚"), state_->create_diagnostic("已完成加厚"));
}

BooleanService::BooleanService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<OpReport> BooleanService::run(BooleanOp op, BodyId lhs, BodyId rhs, const BooleanOptions& boolean_options) {
    if (!detail::has_body(*state_, lhs) || !detail::has_body(*state_, rhs)) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kBoolInvalidInput,
            "布尔运算失败：输入实体不存在或无效", "布尔运算失败");
    }

    detail::BodyRecord record;
    record.kind = detail::BodyKind::BooleanResult;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "boolean";
    append_unique_body(record.source_bodies, lhs);
    append_unique_body(record.source_bodies, rhs);
    append_shell_provenance_for_body(*state_, record.source_shells, lhs);
    append_shell_provenance_for_body(*state_, record.source_shells, rhs);
    append_face_provenance_for_body(*state_, record.source_faces, lhs);
    append_face_provenance_for_body(*state_, record.source_faces, rhs);
    const auto& lhs_bbox = state_->bodies[lhs.value].bbox;
    const auto& rhs_bbox = state_->bodies[rhs.value].bbox;
    std::vector<Warning> warnings;
    const auto relation = classify_bbox_relation(lhs_bbox, rhs_bbox);
    const auto prep = compute_boolean_prep_stats(*state_, lhs, rhs);

    switch (op) {
        case BooleanOp::Union:
            record.bbox = union_bbox(lhs_bbox, rhs_bbox);
            if (relation == BBoxRelation::Disjoint) {
                warnings.push_back(detail::make_warning(diag_codes::kBoolNearDegenerateWarning,
                                                        "并运算输入互不重叠，结果当前仍以单体包围盒语义表示"));
            }
            break;
        case BooleanOp::Intersect:
            if (relation == BBoxRelation::Disjoint) {
                return detail::failed_result<OpReport>(
                    *state_, StatusCode::OperationFailed, diag_codes::kBoolIntersectionFailure,
                    "布尔交集失败：两个输入体的包围盒不相交", "布尔交集失败");
            }
            record.bbox = prep.local_overlap_bbox.is_valid
                              ? prep.local_overlap_bbox
                              : relation == BBoxRelation::LhsContainsRhs ? rhs_bbox
                              : relation == BBoxRelation::RhsContainsLhs ? lhs_bbox
                                                                         : intersect_bbox(lhs_bbox, rhs_bbox);
            if (!prep.local_overlap_bbox.is_valid && relation != BBoxRelation::Disjoint) {
                warnings.push_back(detail::make_warning(
                    diag_codes::kBoolPrepNoCandidateWarning,
                    "布尔交集未构建到局部候选重叠区域，已回退为全局 bbox 交叠语义"));
            }
            if (relation == BBoxRelation::Touching) {
                warnings.push_back(detail::make_warning(diag_codes::kBoolNearDegenerateWarning,
                                                        "交集结果仅在包围盒层面接触，可能退化为低维结果"));
            } else {
                warnings.push_back(detail::make_warning(diag_codes::kBoolNearDegenerateWarning,
                                                        "交集结果可能接近退化边界"));
            }
            break;
        case BooleanOp::Subtract:
            record.bbox = lhs_bbox;
            if (relation == BBoxRelation::Disjoint) {
                warnings.push_back(detail::make_warning(diag_codes::kBoolNearDegenerateWarning, "减运算输入未发生空间重叠，结果与左体近似一致"));
            } else if (prep.overlap_candidates == 0) {
                warnings.push_back(detail::make_warning(
                    diag_codes::kBoolPrepNoCandidateWarning,
                    "减运算未发现局部重叠候选，当前仅按全局语义保留左体"));
            } else if (relation == BBoxRelation::RhsContainsLhs) {
                return detail::failed_result<OpReport>(
                    *state_, StatusCode::OperationFailed, diag_codes::kBoolClassificationFailure,
                    "布尔减运算失败：右体近似完全包含左体，当前阶段无法稳定表达空结果", "布尔减运算失败");
            }
            break;
        case BooleanOp::Split:
            record.bbox = union_bbox(lhs_bbox, rhs_bbox);
            warnings.push_back(detail::make_warning(diag_codes::kBoolNearDegenerateWarning, "Split 目前返回合并包围盒语义，尚未生成真实分割片段"));
            break;
    }

    const auto output = make_body(state_, record, "已完成布尔操作");
    detail::invalidate_eval_for_bodies(*state_, {lhs, rhs});
    const auto diag = boolean_options.diagnostics ? state_->create_diagnostic("布尔操作完成") : DiagnosticId {};
    append_boolean_prep_candidate_issue(*state_, diag, lhs, rhs, prep);
    if (diag.value != 0) {
        for (const auto& warning : warnings) {
            state_->diagnostics[diag.value].issues.push_back(
                detail::make_warning_issue(warning.code, warning.message));
        }
    }

    return ok_result(make_report(StatusCode::Ok, output, diag, warnings), diag);
}

Result<void> BooleanService::export_boolean_prep_stats(BodyId lhs, BodyId rhs, std::string_view path) const {
    if (!detail::has_body(*state_, lhs) || !detail::has_body(*state_, rhs) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kBoolInvalidInput,
            "布尔预处理统计导出失败：输入实体无效或输出路径为空", "布尔预处理统计导出失败");
    }
    const auto stats = compute_boolean_prep_stats(*state_, lhs, rhs);
    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kBoolInvalidInput,
            "布尔预处理统计导出失败：无法打开输出文件", "布尔预处理统计导出失败");
    }
    out << "{";
    out << "\"lhs_regions\":" << stats.lhs_regions << ",";
    out << "\"rhs_regions\":" << stats.rhs_regions << ",";
    out << "\"overlap_candidates\":" << stats.overlap_candidates << ",";
    out << "\"overlap_volume_sum\":" << stats.overlap_volume_sum << ",";
    out << "\"local_clip_applied\":" << (stats.local_clip_applied ? "true" : "false");
    if (stats.local_overlap_bbox.is_valid) {
        out << ",\"local_overlap_bbox\":{"
            << "\"min_x\":" << stats.local_overlap_bbox.min.x << ","
            << "\"min_y\":" << stats.local_overlap_bbox.min.y << ","
            << "\"min_z\":" << stats.local_overlap_bbox.min.z << ","
            << "\"max_x\":" << stats.local_overlap_bbox.max.x << ","
            << "\"max_y\":" << stats.local_overlap_bbox.max.y << ","
            << "\"max_z\":" << stats.local_overlap_bbox.max.z << "}";
    }
    out << "}";
    return ok_void(state_->create_diagnostic("已导出布尔预处理统计"));
}

ModifyService::ModifyService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<OpReport> ModifyService::offset_body(BodyId body_id, Scalar distance, const TolerancePolicy&) {
    if (!detail::has_body(*state_, body_id) || distance == 0.0) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kModOffsetInvalid,
            "偏置失败：目标实体不存在或偏置距离不能为 0", "偏置失败");
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    if (distance < 0.0 && std::abs(distance) * 2.0 >= bbox_min_extent(record.bbox)) {
        return detail::failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kModOffsetSelfIntersection,
            "偏置失败：负偏置过大，结果会产生自交或完全塌缩", "偏置失败");
    }
    record.kind = detail::BodyKind::Modified;
    record.label = "offset";
    append_unique_body(record.source_bodies, body_id);
    record.bbox = offset_bbox(record.bbox, distance);
    const auto output = make_body(state_, record, "已完成偏置");
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("偏置操作完成");
    return ok_result(make_report(StatusCode::Ok, output, diag), diag);
}

Result<OpReport> ModifyService::shell_body(BodyId body_id, std::span<const FaceId> removed_faces, Scalar thickness) {
    if (!detail::has_body(*state_, body_id) || thickness <= 0.0) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kModShellFailure,
            "抽壳失败：目标实体不存在或壁厚必须大于 0", "抽壳失败");
    }
    if (!removed_faces.empty() && !valid_face_ids(*state_, removed_faces)) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kModShellFailure,
            "抽壳失败：待移除面集合包含无效面", "抽壳失败");
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    const auto min_extent = bbox_min_extent(record.bbox);
    if (thickness * 2.0 >= min_extent) {
        return detail::failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kModShellFailure,
            "抽壳失败：壁厚过大，内部空腔将塌缩", "抽壳失败");
    }
    const auto cavity_margin = min_extent - thickness * 2.0;
    if (cavity_margin <= state_->config.tolerance.linear) {
        return detail::failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kModShellFailure,
            "抽壳失败：壁厚逼近容差阈值，内部空腔不稳定", "抽壳失败");
    }
    record.kind = detail::BodyKind::Modified;
    record.label = "shell";
    append_unique_body(record.source_bodies, body_id);
    for (const auto face_id : removed_faces) {
        append_unique_face(record.source_faces, face_id);
        append_shells_for_face(*state_, record.source_shells, face_id);
    }
    record.bbox = offset_bbox(record.bbox, -thickness * 0.5);
    const auto output = make_body(state_, record, "已完成抽壳");
    ValidationService validation {state_};
    const auto validation_result = validation.validate_all(output, ValidationMode::Standard);
    if (validation_result.status != StatusCode::Ok) {
        // Roll back just-created result body to keep modify failure side-effect free.
        state_->bodies.erase(output.value);
        detail::rebuild_topology_links(*state_);
        return detail::failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kTxRollbackFailure,
            "抽壳失败：结果校验未通过，已执行回滚", "抽壳失败");
    }
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("抽壳操作完成");
    return ok_result(make_report(StatusCode::Ok, output, diag), diag);
}

Result<OpReport> ModifyService::draft_faces(BodyId body_id, std::span<const FaceId> faces, const Vec3& pull_dir, Scalar angle) {
    if (!detail::has_body(*state_, body_id) || angle == 0.0 || !valid_axis(pull_dir) || !valid_face_ids(*state_, faces)) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "拔模失败：目标实体不存在、面集合无效、拉拔方向无效或角度不能为 0", "拔模失败");
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    record.kind = detail::BodyKind::Modified;
    record.label = "draft";
    append_unique_body(record.source_bodies, body_id);
    for (const auto face_id : faces) {
        append_unique_face(record.source_faces, face_id);
        append_shells_for_face(*state_, record.source_shells, face_id);
    }
    record.bbox = offset_bbox(record.bbox, std::abs(angle) * 0.01);
    const auto output = make_body(state_, record, "已完成拔模");
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("拔模操作完成");
    return ok_result(make_report(StatusCode::Ok, output, diag), diag);
}

Result<OpReport> ModifyService::replace_face(BodyId body_id, FaceId target, SurfaceId replacement) {
    if (!detail::has_body(*state_, body_id) || state_->faces.find(target.value) == state_->faces.end() || !detail::has_surface(*state_, replacement)) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kModReplaceFaceIncompatible,
            "替换面失败：目标实体、目标面或替换曲面无效", "替换面失败");
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    record.kind = detail::BodyKind::Modified;
    record.label = "replace_face";
    append_unique_body(record.source_bodies, body_id);
    append_unique_face(record.source_faces, target);
    const auto output = make_body(state_, record, "已完成替换面");
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("替换面操作完成");
    return ok_result(make_report(StatusCode::Ok, output, diag), diag);
}

Result<OpReport> ModifyService::delete_face_and_heal(BodyId body_id, FaceId target) {
    if (!detail::has_body(*state_, body_id) || state_->faces.find(target.value) == state_->faces.end()) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kModDeleteFaceHealFailure,
            "删除面补面失败：目标实体或目标面无效", "删除面补面失败");
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    record.kind = detail::BodyKind::Modified;
    record.label = "delete_face_and_heal";
    append_unique_body(record.source_bodies, body_id);
    append_unique_face(record.source_faces, target);
    const auto output = make_body(state_, record, "已完成删除面补面");
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("删除面补面操作完成");
    auto report = make_report(StatusCode::Ok, output, diag,
                              {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "局部面删除后已执行简化补面")});
    state_->append_diagnostic_issue(diag,
                                    detail::make_warning_issue(diag_codes::kHealFeatureRemovedWarning, "局部面删除后执行了补面简化"));
    return ok_result(report, diag);
}

BlendService::BlendService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<OpReport> BlendService::fillet_edges(BodyId body_id, std::span<const EdgeId> edges, Scalar radius) {
    if (!detail::has_body(*state_, body_id) || edges.empty() || radius <= 0.0 || !valid_edge_ids(*state_, edges)) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kBlendParameterTooLarge,
            "圆角失败：目标实体无效、边集合为空、边句柄无效或半径非法", "圆角失败");
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    if (radius * 2.0 >= bbox_min_extent(record.bbox)) {
        return detail::failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kBlendParameterTooLarge,
            "圆角失败：圆角半径过大，超过局部可容纳特征尺寸", "圆角失败");
    }
    record.kind = detail::BodyKind::BlendResult;
    record.label = "fillet";
    append_unique_body(record.source_bodies, body_id);
    for (const auto edge_id : edges) {
        append_faces_for_edge(*state_, record.source_faces, edge_id);
    }
    for (const auto face_id : record.source_faces) {
        append_shells_for_face(*state_, record.source_shells, face_id);
    }
    const auto output = make_body(state_, record, "已完成圆角");
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("圆角操作完成");
    return ok_result(make_report(StatusCode::Ok, output, diag), diag);
}

Result<OpReport> BlendService::chamfer_edges(BodyId body_id, std::span<const EdgeId> edges, Scalar distance) {
    if (!detail::has_body(*state_, body_id) || edges.empty() || distance <= 0.0 || !valid_edge_ids(*state_, edges)) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kBlendInvalidTarget,
            "倒角失败：目标实体无效、边集合为空、边句柄无效或距离非法", "倒角失败");
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    if (distance >= bbox_min_extent(record.bbox)) {
        return detail::failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kBlendParameterTooLarge,
            "倒角失败：倒角距离过大，超过局部可容纳特征尺寸", "倒角失败");
    }
    record.kind = detail::BodyKind::BlendResult;
    record.label = "chamfer";
    append_unique_body(record.source_bodies, body_id);
    for (const auto edge_id : edges) {
        append_faces_for_edge(*state_, record.source_faces, edge_id);
    }
    for (const auto face_id : record.source_faces) {
        append_shells_for_face(*state_, record.source_shells, face_id);
    }
    const auto output = make_body(state_, record, "已完成倒角");
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("倒角操作完成");
    return ok_result(make_report(StatusCode::Ok, output, diag), diag);
}

QueryService::QueryService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<IntersectionId> QueryService::intersect(CurveId curve_id, SurfaceId surface_id) const {
    if (!detail::has_curve(*state_, curve_id) || !detail::has_surface(*state_, surface_id)) {
        return detail::failed_result<IntersectionId>(
            *state_, StatusCode::InvalidInput, diag_codes::kQueryClosestPointFailure,
            "曲线曲面求交失败：输入曲线或曲面不存在", "曲线曲面求交失败");
    }
    const auto& curve = state_->curves.at(curve_id.value);
    const auto& surface = state_->surfaces.at(surface_id.value);

    if (curve.kind == detail::CurveKind::Line && surface.kind == detail::SurfaceKind::Plane) {
        const auto denom = detail::dot(curve.direction, surface.normal);
        const auto delta = detail::subtract(surface.origin, curve.origin);
        if (std::abs(denom) <= 1e-12) {
            if (std::abs(detail::dot(delta, surface.normal)) > 1e-9) {
                return detail::failed_result<IntersectionId>(
                    *state_, StatusCode::OperationFailed, diag_codes::kQueryClosestPointFailure,
                    "曲线曲面求交失败：直线与平面平行且不共面", "曲线曲面求交失败");
            }
            const auto id = store_intersection(state_, "line_plane_coincident", curve_bbox_for_query(curve), {curve_id}, {surface_id});
            return ok_result(id, state_->create_diagnostic("已完成曲线曲面求交"));
        }

        const auto t = detail::dot(delta, surface.normal) / denom;
        const auto point = detail::add_point_vec(curve.origin, detail::scale(curve.direction, t));
        const auto id = store_intersection(state_, "line_plane_point", detail::make_bbox(point, point), {curve_id}, {surface_id});
        return ok_result(id, state_->create_diagnostic("已完成曲线曲面求交"));
    }

    if (curve.kind == detail::CurveKind::Line && surface.kind == detail::SurfaceKind::Sphere) {
        const auto oc = detail::subtract(curve.origin, surface.origin);
        const auto a = detail::dot(curve.direction, curve.direction);
        const auto b = 2.0 * detail::dot(oc, curve.direction);
        const auto c = detail::dot(oc, oc) - surface.radius_a * surface.radius_a;
        const auto discriminant = b * b - 4.0 * a * c;
        if (discriminant < 0.0) {
            return detail::failed_result<IntersectionId>(
                *state_, StatusCode::OperationFailed, diag_codes::kQueryClosestPointFailure,
                "曲线曲面求交失败：直线与球面不相交", "曲线曲面求交失败");
        }
        const auto id = store_intersection(state_, "line_sphere", surface_bbox_for_query(surface), {curve_id}, {surface_id});
        return ok_result(id, state_->create_diagnostic("已完成曲线曲面求交"));
    }

    const auto bbox = intersect_bbox(curve_bbox_for_query(curve), surface_bbox_for_query(surface));
    if (!bbox.is_valid) {
        return detail::failed_result<IntersectionId>(
            *state_, StatusCode::OperationFailed, diag_codes::kQueryClosestPointFailure,
            "曲线曲面求交失败：输入对象包围盒不相交", "曲线曲面求交失败");
    }
    const auto id = store_intersection(state_, "curve_surface_bbox_overlap", bbox, {curve_id}, {surface_id});
    return ok_result(id, state_->create_diagnostic("已完成曲线曲面求交"));
}

Result<IntersectionId> QueryService::intersect(SurfaceId lhs, SurfaceId rhs) const {
    if (!detail::has_surface(*state_, lhs) || !detail::has_surface(*state_, rhs)) {
        return detail::failed_result<IntersectionId>(
            *state_, StatusCode::InvalidInput, diag_codes::kQueryClosestPointFailure,
            "曲面曲面求交失败：输入曲面不存在", "曲面曲面求交失败");
    }
    const auto& lhs_surface = state_->surfaces.at(lhs.value);
    const auto& rhs_surface = state_->surfaces.at(rhs.value);

    if (lhs_surface.kind == detail::SurfaceKind::Plane && rhs_surface.kind == detail::SurfaceKind::Plane) {
        const auto cross = detail::cross(lhs_surface.normal, rhs_surface.normal);
        const auto cross_norm = detail::norm(cross);
        const auto offset = detail::dot(detail::subtract(rhs_surface.origin, lhs_surface.origin), lhs_surface.normal);
        if (cross_norm <= 1e-12) {
            if (std::abs(offset) > 1e-9) {
                return detail::failed_result<IntersectionId>(
                    *state_, StatusCode::OperationFailed, diag_codes::kQueryClosestPointFailure,
                    "曲面曲面求交失败：两个平面平行且不重合", "曲面曲面求交失败");
            }
            const auto id = store_intersection(state_, "plane_plane_coincident", surface_bbox_for_query(lhs_surface), {}, {lhs, rhs});
            return ok_result(id, state_->create_diagnostic("已完成曲面曲面求交"));
        }
        const auto bbox = union_bbox(surface_bbox_for_query(lhs_surface), surface_bbox_for_query(rhs_surface));
        const auto id = store_intersection(state_, "plane_plane_line", bbox, {}, {lhs, rhs});
        return ok_result(id, state_->create_diagnostic("已完成曲面曲面求交"));
    }

    if (lhs_surface.kind == detail::SurfaceKind::Sphere && rhs_surface.kind == detail::SurfaceKind::Sphere) {
        const auto center_delta = detail::subtract(lhs_surface.origin, rhs_surface.origin);
        const auto center_distance = detail::norm(center_delta);
        const auto radius_sum = lhs_surface.radius_a + rhs_surface.radius_a;
        const auto radius_diff = std::abs(lhs_surface.radius_a - rhs_surface.radius_a);
        if (center_distance > radius_sum || center_distance < radius_diff) {
            return detail::failed_result<IntersectionId>(
                *state_, StatusCode::OperationFailed, diag_codes::kQueryClosestPointFailure,
                "曲面曲面求交失败：两个球面不存在稳定交线", "曲面曲面求交失败");
        }
        const auto bbox = intersect_bbox(surface_bbox_for_query(lhs_surface), surface_bbox_for_query(rhs_surface));
        const auto id = store_intersection(state_, "sphere_sphere_circle", bbox, {}, {lhs, rhs});
        return ok_result(id, state_->create_diagnostic("已完成曲面曲面求交"));
    }

    const auto bbox = intersect_bbox(surface_bbox_for_query(lhs_surface), surface_bbox_for_query(rhs_surface));
    if (!bbox.is_valid) {
        return detail::failed_result<IntersectionId>(
            *state_, StatusCode::OperationFailed, diag_codes::kQueryClosestPointFailure,
            "曲面曲面求交失败：输入曲面包围盒不相交", "曲面曲面求交失败");
    }
    const auto id = store_intersection(state_, "surface_surface_bbox_overlap", bbox, {}, {lhs, rhs});
    return ok_result(id, state_->create_diagnostic("已完成曲面曲面求交"));
}

Result<MeshId> QueryService::section(BodyId body_id, const Plane& plane) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_result<MeshId>(
            *state_, StatusCode::InvalidInput, diag_codes::kQuerySectionFailure,
            "截面计算失败：目标实体不存在", "截面计算失败");
    }

    const auto& body = state_->bodies[body_id.value];
    if (!intersects_plane(body.bbox, plane)) {
        return detail::failed_result<MeshId>(
            *state_, StatusCode::OperationFailed, diag_codes::kQuerySectionFailure,
            "截面计算失败：截面平面与目标体包围盒不相交", "截面计算失败");
    }

    const auto mesh_id = MeshId {state_->allocate_id()};
    detail::MeshRecord mesh;
    mesh.source_body = body_id;
    mesh.label = "section";
    mesh.bbox = body.bbox;
    mesh.vertices = bbox_corners(body.bbox);
    mesh.indices = {0, 1, 2, 0, 2, 3};
    state_->meshes.emplace(mesh_id.value, std::move(mesh));
    return ok_result(mesh_id, state_->create_diagnostic("已完成截面计算"));
}

Result<MassProperties> QueryService::mass_properties(BodyId body_id) const {
    const auto it = state_->bodies.find(body_id.value);
    if (it == state_->bodies.end()) {
        return detail::failed_result<MassProperties>(
            *state_, StatusCode::InvalidInput, diag_codes::kQueryMassPropertiesFailure,
            "质量属性计算失败：目标实体不存在", "质量属性计算失败");
    }

    MassProperties props {};
    const auto& body = it->second;
    switch (body.kind) {
        case detail::BodyKind::Box:
            props.volume = body.a * body.b * body.c;
            props.area = 2.0 * (body.a * body.b + body.b * body.c + body.a * body.c);
            props.centroid = Point3 {body.origin.x + body.a * 0.5, body.origin.y + body.b * 0.5, body.origin.z + body.c * 0.5};
            break;
        case detail::BodyKind::Sphere:
            props.volume = 4.0 / 3.0 * kPi * body.a * body.a * body.a;
            props.area = 4.0 * kPi * body.a * body.a;
            props.centroid = body.origin;
            break;
        case detail::BodyKind::Cylinder:
            props.volume = kPi * body.a * body.a * body.b;
            props.area = 2.0 * kPi * body.a * (body.b + body.a);
            props.centroid = body.origin;
            break;
        case detail::BodyKind::Cone: {
            const auto radius = body.b * std::tan(body.a);
            const auto slant = std::sqrt(radius * radius + body.b * body.b);
            props.volume = kPi * radius * radius * body.b / 3.0;
            props.area = kPi * radius * (radius + slant);
            props.centroid = detail::add_point_vec(body.origin, detail::scale(body.axis, body.b * 0.75));
            break;
        }
        case detail::BodyKind::Torus:
            props.volume = 2.0 * kPi * kPi * body.a * body.b * body.b;
            props.area = 4.0 * kPi * kPi * body.a * body.b;
            props.centroid = body.origin;
            break;
        default:
            props = bbox_mass_properties(body.bbox);
            break;
    }

    return ok_result(props, state_->create_diagnostic("已完成质量属性计算"));
}

Result<Scalar> QueryService::min_distance(BodyId lhs, BodyId rhs) const {
    if (!detail::has_body(*state_, lhs) || !detail::has_body(*state_, rhs)) {
        return detail::failed_result<Scalar>(
            *state_, StatusCode::InvalidInput, diag_codes::kQueryClosestPointFailure,
            "最短距离计算失败：输入实体不存在", "最短距离计算失败");
    }
    const auto& lb = state_->bodies[lhs.value].bbox;
    const auto& rb = state_->bodies[rhs.value].bbox;
    const auto dx = interval_gap(lb.min.x, lb.max.x, rb.min.x, rb.max.x);
    const auto dy = interval_gap(lb.min.y, lb.max.y, rb.min.y, rb.max.y);
    const auto dz = interval_gap(lb.min.z, lb.max.z, rb.min.z, rb.max.z);
    return ok_result<Scalar>(std::sqrt(dx * dx + dy * dy + dz * dz), state_->create_diagnostic("已完成最短距离计算"));
}

}  // namespace axiom
