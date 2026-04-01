#include "axiom/ops/ops_services.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "axiom/heal/heal_services.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/eval_graph_invalidation.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/core/topology_materialization.h"

namespace axiom {

namespace {

constexpr Scalar kPi = 3.14159265358979323846;

bool is_primitive_body_kind(detail::BodyKind kind) {
    switch (kind) {
        case detail::BodyKind::Box:
        case detail::BodyKind::Sphere:
        case detail::BodyKind::Cylinder:
        case detail::BodyKind::Cone:
        case detail::BodyKind::Torus:
        case detail::BodyKind::Wedge:
            return true;
        default:
            return false;
    }
}

void clear_primitive_provenance_fields(detail::BodyRecord& record) {
    record.source_bodies.clear();
    record.source_shells.clear();
    record.source_faces.clear();
}

enum class BBoxRelation {
    Disjoint,
    Touching,
    Overlapping,
    LhsContainsRhs,
    RhsContainsLhs
};

BodyId make_body(std::shared_ptr<detail::KernelState> state, detail::BodyRecord record, const std::string&) {
    const bool is_derived = record.kind == detail::BodyKind::BooleanResult || record.kind == detail::BodyKind::Modified ||
                           record.kind == detail::BodyKind::BlendResult || record.kind == detail::BodyKind::Sweep;
    if (is_derived) {
        detail::materialize_body_bbox_topology(*state, record);
    } else if (is_primitive_body_kind(record.kind) && record.rep_kind == RepKind::ExactBRep && record.bbox.is_valid &&
               record.shells.empty()) {
        clear_primitive_provenance_fields(record);
        if (record.kind == detail::BodyKind::Wedge) {
            detail::materialize_body_wedge_shell(*state, record);
        } else if (record.kind == detail::BodyKind::Cylinder) {
            detail::materialize_body_prism_cylinder_shell(*state, record);
        } else {
            detail::materialize_body_bbox_shell(*state, record);
        }
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

// When a Face is referenced by multiple shells, provenance for modify operations
// should prefer shells owned by the source body.
void append_shells_for_face_owned_by_body(const detail::KernelState& state, std::vector<ShellId>& ids,
                                         FaceId face_id, BodyId body_id) {
    const auto body_it = state.bodies.find(body_id.value);
    if (body_it == state.bodies.end()) {
        return;
    }
    for (const auto shell_id : body_it->second.shells) {
        const auto sh_it = state.shells.find(shell_id.value);
        if (sh_it == state.shells.end()) {
            continue;
        }
        const auto& faces = sh_it->second.faces;
        if (std::any_of(faces.begin(), faces.end(),
                        [face_id](FaceId f) { return f.value == face_id.value; })) {
            append_unique_shell(ids, shell_id);
        }
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
    if (!is_primitive_body_kind(record.kind)) {
        detail::inherit_source_topology_from_owned_shells(state, record);
    }
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

bool same_unordered_face_ids(const std::vector<FaceId>& a, const std::vector<FaceId>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    std::vector<std::uint64_t> av;
    std::vector<std::uint64_t> bv;
    av.reserve(a.size());
    bv.reserve(b.size());
    for (const auto f : a) {
        av.push_back(f.value);
    }
    for (const auto f : b) {
        bv.push_back(f.value);
    }
    std::sort(av.begin(), av.end());
    std::sort(bv.begin(), bv.end());
    return av == bv;
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

struct FaceCandidatePair {
    FaceId lhs_face {};
    FaceId rhs_face {};
};

struct IntersectionCurve {
    CurveId curve {};
    FaceId lhs_face {};
    FaceId rhs_face {};
};

struct IntersectionSegment {
    CurveId curve {};
    FaceId lhs_face {};
    FaceId rhs_face {};
};

std::vector<FaceId> faces_for_body_boolean(const detail::KernelState& state, BodyId body_id) {
    std::vector<FaceId> faces;
    if (!detail::has_body(state, body_id)) {
        return faces;
    }
    const auto& body = state.bodies.at(body_id.value);
    const auto shells = shells_for_body(body);
    for (const auto shell_id : shells) {
        const auto shell_it = state.shells.find(shell_id.value);
        if (shell_it == state.shells.end()) {
            continue;
        }
        for (const auto face_id : shell_it->second.faces) {
            faces.push_back(face_id);
        }
    }
    return faces;
}

std::vector<FaceCandidatePair> build_face_candidates_for_boolean(const detail::KernelState& state,
                                                                 BodyId lhs,
                                                                 BodyId rhs) {
    std::vector<FaceCandidatePair> pairs;
    const auto lhs_faces = faces_for_body_boolean(state, lhs);
    const auto rhs_faces = faces_for_body_boolean(state, rhs);
    if (lhs_faces.empty() || rhs_faces.empty()) {
        return pairs;
    }

    std::vector<BoundingBox> lhs_boxes;
    lhs_boxes.reserve(lhs_faces.size());
    for (const auto face_id : lhs_faces) {
        lhs_boxes.push_back(face_bbox(state, face_id));
    }
    std::vector<BoundingBox> rhs_boxes;
    rhs_boxes.reserve(rhs_faces.size());
    for (const auto face_id : rhs_faces) {
        rhs_boxes.push_back(face_bbox(state, face_id));
    }

    for (std::size_t i = 0; i < lhs_faces.size(); ++i) {
        if (!lhs_boxes[i].is_valid) {
            continue;
        }
        for (std::size_t j = 0; j < rhs_faces.size(); ++j) {
            if (!rhs_boxes[j].is_valid) {
                continue;
            }
            if (!intersect_bbox(lhs_boxes[i], rhs_boxes[j]).is_valid) {
                continue;
            }
            pairs.push_back(FaceCandidatePair {lhs_faces[i], rhs_faces[j]});
        }
    }
    return pairs;
}

Vec3 safe_unit_normal_local(const Vec3& normal) {
    const auto n = detail::norm(normal);
    if (!(n > 0.0)) {
        return Vec3 {0.0, 0.0, 1.0};
    }
    return detail::scale(normal, 1.0 / n);
}

bool plane_plane_intersection_line(const detail::KernelState& state,
                                   FaceId lhs_face,
                                   FaceId rhs_face,
                                   Point3& out_origin,
                                   Vec3& out_dir) {
    const auto lhs_face_it = state.faces.find(lhs_face.value);
    const auto rhs_face_it = state.faces.find(rhs_face.value);
    if (lhs_face_it == state.faces.end() || rhs_face_it == state.faces.end()) {
        return false;
    }
    const auto lhs_surf_it = state.surfaces.find(lhs_face_it->second.surface_id.value);
    const auto rhs_surf_it = state.surfaces.find(rhs_face_it->second.surface_id.value);
    if (lhs_surf_it == state.surfaces.end() || rhs_surf_it == state.surfaces.end()) {
        return false;
    }
    const auto& s0 = lhs_surf_it->second;
    const auto& s1 = rhs_surf_it->second;
    if (s0.kind != detail::SurfaceKind::Plane || s1.kind != detail::SurfaceKind::Plane) {
        return false;
    }
    const auto n0 = safe_unit_normal_local(s0.normal);
    const auto n1 = safe_unit_normal_local(s1.normal);
    const auto dir = detail::cross(n0, n1);
    const auto dir_len2 = detail::dot(dir, dir);
    if (!(dir_len2 > 1e-14)) {
        return false;
    }
    const auto d0 = detail::dot(n0, Vec3 {s0.origin.x, s0.origin.y, s0.origin.z});
    const auto d1 = detail::dot(n1, Vec3 {s1.origin.x, s1.origin.y, s1.origin.z});
    // p = ((d0*n1 - d1*n0) x (n0 x n1)) / |n0 x n1|^2
    const Vec3 c {d0 * n1.x - d1 * n0.x, d0 * n1.y - d1 * n0.y, d0 * n1.z - d1 * n0.z};
    const auto p = detail::scale(detail::cross(c, dir), 1.0 / dir_len2);
    out_origin = Point3 {p.x, p.y, p.z};
    out_dir = detail::normalize(dir);
    return true;
}

std::vector<IntersectionCurve> compute_intersection_curves_for_candidates(detail::KernelState& state,
                                                                          std::span<const FaceCandidatePair> candidates) {
    std::vector<IntersectionCurve> curves;
    curves.reserve(candidates.size());
    for (const auto& pair : candidates) {
        Point3 origin {};
        Vec3 dir {};
        if (!plane_plane_intersection_line(state, pair.lhs_face, pair.rhs_face, origin, dir)) {
            continue;
        }
        detail::CurveRecord curve;
        curve.kind = detail::CurveKind::Line;
        curve.origin = origin;
        curve.direction = dir;
        const auto curve_id = CurveId {state.allocate_id()};
        state.curves.emplace(curve_id.value, std::move(curve));
        curves.push_back(IntersectionCurve {curve_id, pair.lhs_face, pair.rhs_face});
    }
    return curves;
}

std::vector<IntersectionSegment> clip_intersection_lines_to_face_overlap(detail::KernelState& state,
                                                                         std::span<const IntersectionCurve> curves) {
    std::vector<IntersectionSegment> segments;
    segments.reserve(curves.size());
    for (const auto& item : curves) {
        const auto lhs_box = face_bbox(state, item.lhs_face);
        const auto rhs_box = face_bbox(state, item.rhs_face);
        const auto overlap = intersect_bbox(lhs_box, rhs_box);
        if (!overlap.is_valid) {
            continue;
        }
        const auto curve_it = state.curves.find(item.curve.value);
        if (curve_it == state.curves.end() || curve_it->second.kind != detail::CurveKind::Line) {
            continue;
        }
        const auto o = curve_it->second.origin;
        const auto d = curve_it->second.direction;
        const auto d_len2 = detail::dot(d, d);
        if (!(d_len2 > 1e-14)) {
            continue;
        }

        const std::array<Point3, 8> corners {
            Point3 {overlap.min.x, overlap.min.y, overlap.min.z},
            Point3 {overlap.max.x, overlap.min.y, overlap.min.z},
            Point3 {overlap.max.x, overlap.max.y, overlap.min.z},
            Point3 {overlap.min.x, overlap.max.y, overlap.min.z},
            Point3 {overlap.min.x, overlap.min.y, overlap.max.z},
            Point3 {overlap.max.x, overlap.min.y, overlap.max.z},
            Point3 {overlap.max.x, overlap.max.y, overlap.max.z},
            Point3 {overlap.min.x, overlap.max.y, overlap.max.z},
        };

        Scalar tmin = 0.0;
        Scalar tmax = 0.0;
        bool initialized = false;
        for (const auto& p : corners) {
            const Vec3 op {p.x - o.x, p.y - o.y, p.z - o.z};
            const auto t = detail::dot(op, d) / d_len2;
            if (!initialized) {
                tmin = t;
                tmax = t;
                initialized = true;
            } else {
                tmin = std::min(tmin, t);
                tmax = std::max(tmax, t);
            }
        }
        if (!initialized || !(tmax - tmin > 1e-9)) {
            continue;
        }

        const Point3 p0 {o.x + d.x * tmin, o.y + d.y * tmin, o.z + d.z * tmin};
        const Point3 p1 {o.x + d.x * tmax, o.y + d.y * tmax, o.z + d.z * tmax};

        detail::CurveRecord seg;
        seg.kind = detail::CurveKind::LineSegment;
        seg.poles = {p0, p1};
        const auto seg_id = CurveId {state.allocate_id()};
        state.curves.emplace(seg_id.value, std::move(seg));
        segments.push_back(IntersectionSegment {seg_id, item.lhs_face, item.rhs_face});
    }
    return segments;
}

CurveId longest_intersection_segment_curve(const detail::KernelState& state,
                                           std::span<const IntersectionSegment> segments) {
    CurveId best {};
    Scalar best_len2 = -1.0;
    for (const auto& s : segments) {
        const auto it = state.curves.find(s.curve.value);
        if (it == state.curves.end() || it->second.kind != detail::CurveKind::LineSegment ||
            it->second.poles.size() < 2) {
            continue;
        }
        const auto dv = detail::subtract(it->second.poles.back(), it->second.poles.front());
        const auto l2 = detail::dot(dv, dv);
        if (l2 > best_len2) {
            best_len2 = l2;
            best = s.curve;
        }
    }
    return best;
}

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

const char* bbox_relation_name(BBoxRelation relation) {
    switch (relation) {
        case BBoxRelation::Disjoint:
            return "Disjoint";
        case BBoxRelation::Touching:
            return "Touching";
        case BBoxRelation::Overlapping:
            return "Overlapping";
        case BBoxRelation::LhsContainsRhs:
            return "LhsContainsRhs";
        case BBoxRelation::RhsContainsLhs:
            return "RhsContainsLhs";
    }
    return "Unknown";
}

const char* boolean_op_name(BooleanOp op) {
    switch (op) {
        case BooleanOp::Union:
            return "Union";
        case BooleanOp::Intersect:
            return "Intersect";
        case BooleanOp::Subtract:
            return "Subtract";
        case BooleanOp::Split:
            return "Split";
    }
    return "Unknown";
}

void append_boolean_run_stage_issue(detail::KernelState& state,
                                    DiagnosticId diag,
                                    BooleanOp op,
                                    BBoxRelation relation,
                                    const BooleanPrepStats& prep,
                                    BodyId lhs,
                                    BodyId rhs,
                                    BodyId output) {
    if (diag.value == 0) {
        return;
    }
    std::ostringstream msg;
    msg << "布尔阶段摘要 op=" << boolean_op_name(op) << " bbox_relation=" << bbox_relation_name(relation)
        << " lhs_regions=" << prep.lhs_regions << " rhs_regions=" << prep.rhs_regions
        << " overlap_candidates=" << prep.overlap_candidates
        << " local_clip=" << (prep.local_clip_applied ? "true" : "false");
    auto issue = detail::make_info_issue(diag_codes::kBoolRunStageSummary, msg.str());
    issue.related_entities = {lhs.value, rhs.value, output.value};
    state.append_diagnostic_issue(diag, std::move(issue));
}

void append_boolean_stage_issue(detail::KernelState& state,
                                DiagnosticId diag,
                                std::string_view code,
                                std::string message,
                                std::vector<std::uint64_t> related_entities) {
    if (diag.value == 0) {
        return;
    }
    auto issue = detail::make_info_issue(code, std::move(message));
    issue.related_entities = std::move(related_entities);
    state.append_diagnostic_issue(diag, std::move(issue));
}

void append_boolean_face_candidate_issue(detail::KernelState& state,
                                        DiagnosticId diag,
                                        BodyId lhs,
                                        BodyId rhs,
                                        std::size_t count) {
    if (diag.value == 0) {
        return;
    }
    std::ostringstream msg;
    msg << "布尔面级候选对已生成: face_candidates=" << count;
    auto issue = detail::make_info_issue(diag_codes::kBoolFaceCandidatesBuilt, msg.str());
    issue.related_entities = {lhs.value, rhs.value};
    state.append_diagnostic_issue(diag, std::move(issue));
}

void append_boolean_intersection_curve_issue(detail::KernelState& state,
                                             DiagnosticId diag,
                                             BodyId lhs,
                                             BodyId rhs,
                                             std::span<const IntersectionCurve> curves) {
    if (diag.value == 0) {
        return;
    }
    std::ostringstream msg;
    msg << "布尔精确求交（解析）已生成交线: curves=" << curves.size();
    auto issue = detail::make_info_issue(diag_codes::kBoolIntersectionCurvesBuilt, msg.str());
    issue.related_entities = {lhs.value, rhs.value};
    const std::size_t kMaxCurves = 8;
    for (std::size_t i = 0; i < std::min(kMaxCurves, curves.size()); ++i) {
        issue.related_entities.push_back(curves[i].curve.value);
    }
    state.append_diagnostic_issue(diag, std::move(issue));
}

void append_boolean_intersection_segment_issue(detail::KernelState& state,
                                               DiagnosticId diag,
                                               BodyId lhs,
                                               BodyId rhs,
                                               std::span<const IntersectionSegment> segments) {
    if (diag.value == 0) {
        return;
    }
    std::ostringstream msg;
    msg << "布尔交线裁剪完成: segments=" << segments.size();
    auto issue = detail::make_info_issue(diag_codes::kBoolIntersectionSegmentsBuilt, msg.str());
    issue.related_entities = {lhs.value, rhs.value};
    const std::size_t kMaxSeg = 8;
    for (std::size_t i = 0; i < std::min(kMaxSeg, segments.size()); ++i) {
        issue.related_entities.push_back(segments[i].curve.value);
    }
    state.append_diagnostic_issue(diag, std::move(issue));
}

void append_boolean_intersection_stored_issue(detail::KernelState& state,
                                              DiagnosticId diag,
                                              BodyId lhs,
                                              BodyId rhs,
                                              IntersectionId intersection_id,
                                              std::size_t curve_count) {
    if (diag.value == 0) {
        return;
    }
    std::ostringstream msg;
    msg << "布尔交线集合已保存: intersection_id=" << intersection_id.value
        << " curve_count=" << curve_count;
    auto issue = detail::make_info_issue(diag_codes::kBoolIntersectionWiresStored, msg.str());
    issue.related_entities = {lhs.value, rhs.value, intersection_id.value};
    state.append_diagnostic_issue(diag, std::move(issue));
}

std::optional<std::array<VertexId, 2>> oriented_vertices_for_coedge_local(const detail::KernelState& state, CoedgeId coedge_id) {
    const auto coedge_it = state.coedges.find(coedge_id.value);
    if (coedge_it == state.coedges.end()) {
        return std::nullopt;
    }
    const auto edge_it = state.edges.find(coedge_it->second.edge_id.value);
    if (edge_it == state.edges.end()) {
        return std::nullopt;
    }
    if (!detail::has_vertex(state, edge_it->second.v0) || !detail::has_vertex(state, edge_it->second.v1) ||
        edge_it->second.v0.value == edge_it->second.v1.value) {
        return std::nullopt;
    }
    if (coedge_it->second.reversed) {
        return std::array<VertexId, 2> {edge_it->second.v1, edge_it->second.v0};
    }
    return std::array<VertexId, 2> {edge_it->second.v0, edge_it->second.v1};
}

bool coedge_reversed_for_oriented_edge(const detail::KernelState& state, EdgeId edge_id, VertexId start, VertexId end, bool& out_reversed) {
    const auto edge_it = state.edges.find(edge_id.value);
    if (edge_it == state.edges.end()) {
        return false;
    }
    const auto v0 = edge_it->second.v0;
    const auto v1 = edge_it->second.v1;
    if (v0.value == start.value && v1.value == end.value) {
        out_reversed = false;
        return true;
    }
    if (v1.value == start.value && v0.value == end.value) {
        out_reversed = true;
        return true;
    }
    return false;
}

struct RectFrame {
    Point3 origin {};
    Vec3 u_unit {1.0, 0.0, 0.0};
    Vec3 v_unit {0.0, 1.0, 0.0};
    Scalar u_len {0.0};
    Scalar v_len {0.0};
};

bool build_rect_frame_from_loop(const detail::KernelState& state,
                                LoopId loop_id,
                                std::array<VertexId, 4>& out_verts,
                                RectFrame& out_frame) {
    const auto loop_it = state.loops.find(loop_id.value);
    if (loop_it == state.loops.end() || loop_it->second.coedges.size() != 4) {
        return false;
    }
    for (std::size_t i = 0; i < 4; ++i) {
        const auto oriented = oriented_vertices_for_coedge_local(state, loop_it->second.coedges[i]);
        if (!oriented.has_value()) {
            return false;
        }
        out_verts[i] = (*oriented)[0];
    }
    const auto v0_it = state.vertices.find(out_verts[0].value);
    const auto v1_it = state.vertices.find(out_verts[1].value);
    const auto v3_it = state.vertices.find(out_verts[3].value);
    if (v0_it == state.vertices.end() || v1_it == state.vertices.end() || v3_it == state.vertices.end()) {
        return false;
    }
    const auto u_vec = detail::subtract(v1_it->second.point, v0_it->second.point);
    const auto v_vec = detail::subtract(v3_it->second.point, v0_it->second.point);
    const auto u_len = detail::norm(u_vec);
    const auto v_len = detail::norm(v_vec);
    if (!(u_len > 1e-12) || !(v_len > 1e-12)) {
        return false;
    }
    out_frame.origin = v0_it->second.point;
    out_frame.u_unit = detail::scale(u_vec, 1.0 / u_len);
    out_frame.v_unit = detail::scale(v_vec, 1.0 / v_len);
    out_frame.u_len = u_len;
    out_frame.v_len = v_len;
    return true;
}

std::array<Scalar, 2> to_uv(const RectFrame& f, const Point3& p) {
    const Vec3 op {p.x - f.origin.x, p.y - f.origin.y, p.z - f.origin.z};
    return {detail::dot(op, f.u_unit), detail::dot(op, f.v_unit)};
}

Point3 from_uv(const RectFrame& f, Scalar u, Scalar v) {
    const auto pu = detail::scale(f.u_unit, u);
    const auto pv = detail::scale(f.v_unit, v);
    return Point3 {f.origin.x + pu.x + pv.x, f.origin.y + pu.y + pv.y, f.origin.z + pu.z + pv.z};
}

bool uv_inside_rect(const RectFrame& f, Scalar u, Scalar v, Scalar eps) {
    return u >= -eps && u <= f.u_len + eps && v >= -eps && v <= f.v_len + eps;
}

struct RectIntersection {
    Scalar u {0.0};
    Scalar v {0.0};
    int boundary {-1}; // 0: u=0, 1: u=u_len, 2: v=0, 3: v=v_len
};

std::vector<RectIntersection> intersect_line_with_rect_uv(const RectFrame& f,
                                                          const std::array<Scalar, 2>& p0,
                                                          const std::array<Scalar, 2>& p1) {
    std::vector<RectIntersection> hits;
    const auto du = p1[0] - p0[0];
    const auto dv = p1[1] - p0[1];
    const auto eps = 1e-9 * std::max<Scalar>(1.0, std::max(f.u_len, f.v_len));

    auto add_hit = [&hits, eps](RectIntersection h) {
        for (const auto& e : hits) {
            if (std::abs(e.u - h.u) <= eps && std::abs(e.v - h.v) <= eps) {
                return;
            }
        }
        hits.push_back(h);
    };

    auto try_u = [&](Scalar u_target, int boundary) {
        if (std::abs(du) <= 1e-14) {
            return;
        }
        const auto t = (u_target - p0[0]) / du;
        const auto v = p0[1] + t * dv;
        if (uv_inside_rect(f, u_target, v, eps)) {
            add_hit(RectIntersection {u_target, v, boundary});
        }
    };
    auto try_v = [&](Scalar v_target, int boundary) {
        if (std::abs(dv) <= 1e-14) {
            return;
        }
        const auto t = (v_target - p0[1]) / dv;
        const auto u = p0[0] + t * du;
        if (uv_inside_rect(f, u, v_target, eps)) {
            add_hit(RectIntersection {u, v_target, boundary});
        }
    };

    try_u(0.0, 0);
    try_u(f.u_len, 1);
    try_v(0.0, 2);
    try_v(f.v_len, 3);
    return hits;
}

EdgeId create_line_edge(detail::KernelState& state, VertexId a, VertexId b) {
    const auto a_it = state.vertices.find(a.value);
    const auto b_it = state.vertices.find(b.value);
    detail::CurveRecord curve;
    curve.kind = detail::CurveKind::Line;
    curve.origin = a_it->second.point;
    curve.direction = detail::normalize(detail::subtract(b_it->second.point, a_it->second.point));
    const auto curve_id = CurveId {state.allocate_id()};
    state.curves.emplace(curve_id.value, std::move(curve));
    const auto edge_id = EdgeId {state.allocate_id()};
    state.edges.emplace(edge_id.value, detail::EdgeRecord {curve_id, a, b});
    return edge_id;
}

struct SplitEdgeResult {
    EdgeId e0 {}; // start -> mid
    EdgeId e1 {}; // mid -> end
    VertexId start {};
    VertexId end {};
};

// Split an existing edge inside a shell: replace each coedge in that shell by two coedges referencing new edges.
std::optional<SplitEdgeResult> split_edge_in_shell(detail::KernelState& state, ShellId shell_id, EdgeId edge_id, VertexId mid_vertex) {
    const auto edge_it = state.edges.find(edge_id.value);
    if (edge_it == state.edges.end()) {
        return std::nullopt;
    }
    const auto v0 = edge_it->second.v0;
    const auto v1 = edge_it->second.v1;
    if (v0.value == mid_vertex.value || v1.value == mid_vertex.value) {
        return std::nullopt;
    }
    if (!detail::has_vertex(state, v0) || !detail::has_vertex(state, v1) || !detail::has_vertex(state, mid_vertex)) {
        return std::nullopt;
    }

    const auto e0 = create_line_edge(state, v0, mid_vertex);
    const auto e1 = create_line_edge(state, mid_vertex, v1);

    // Prefer scanning the shell topology directly. This is more robust than relying on
    // `edge_to_coedges`, which may be stale/partial during in-place imprint operations.
    std::vector<std::uint64_t> target_coedges;
    {
        const auto shell_it = state.shells.find(shell_id.value);
        if (shell_it != state.shells.end()) {
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
                        if (coedge_it->second.edge_id.value == edge_id.value) {
                            target_coedges.push_back(coedge_id.value);
                        }
                    }
                }
            }
        }
    }

    // Fall back to the edge index if the scan couldn't find any coedges.
    if (target_coedges.empty()) {
        const auto it = state.edge_to_coedges.find(edge_id.value);
        if (it == state.edge_to_coedges.end()) {
            return SplitEdgeResult {e0, e1, v0, v1};
        }
        target_coedges.assign(it->second.begin(), it->second.end());
    }

    for (const auto coedge_value : target_coedges) {
        const auto loop_map_it = state.coedge_to_loop.find(coedge_value);
        if (loop_map_it == state.coedge_to_loop.end()) {
            continue;
        }
        const auto loop_id = LoopId {loop_map_it->second};
        const auto loop_it = state.loops.find(loop_id.value);
        if (loop_it == state.loops.end()) {
            continue;
        }
        const auto coedge_it = state.coedges.find(coedge_value);
        if (coedge_it == state.coedges.end()) {
            continue;
        }
        const bool reversed = coedge_it->second.reversed;

        // Create two new coedges, ordering depends on the original coedge orientation.
        const auto c0 = CoedgeId {state.allocate_id()};
        const auto c1 = CoedgeId {state.allocate_id()};
        if (!reversed) {
            state.coedges.emplace(c0.value, detail::CoedgeRecord {e0, false});
            state.coedges.emplace(c1.value, detail::CoedgeRecord {e1, false});
        } else {
            state.coedges.emplace(c0.value, detail::CoedgeRecord {e1, true});
            state.coedges.emplace(c1.value, detail::CoedgeRecord {e0, true});
        }

        auto& coedges = loop_it->second.coedges;
        for (std::size_t i = 0; i < coedges.size(); ++i) {
            if (coedges[i].value == coedge_value) {
                coedges[i] = c0;
                coedges.insert(coedges.begin() + static_cast<std::ptrdiff_t>(i + 1), c1);
                break;
            }
        }
    }
    return SplitEdgeResult {e0, e1, v0, v1};
}

// Segment-driven imprint: split a rectangular plane face using the intersection segment direction.
// Supports the common "opposite edges" cut (u=0 with u=u_len) or (v=0 with v=v_len).
bool imprint_split_rect_face_by_segment(detail::KernelState& state,
                                        ShellId shell_id,
                                        FaceId face_id,
                                        CurveId segment_curve_id) {
    const auto shell_it = state.shells.find(shell_id.value);
    const auto face_it = state.faces.find(face_id.value);
    if (shell_it == state.shells.end() || face_it == state.faces.end()) {
        return false;
    }
    const auto surf_it = state.surfaces.find(face_it->second.surface_id.value);
    if (surf_it == state.surfaces.end() || surf_it->second.kind != detail::SurfaceKind::Plane) {
        return false;
    }
    const auto loop_id = face_it->second.outer_loop;

    std::array<VertexId, 4> verts {};
    RectFrame frame {};
    if (!build_rect_frame_from_loop(state, loop_id, verts, frame)) {
        return false;
    }

    const auto seg_it = state.curves.find(segment_curve_id.value);
    if (seg_it == state.curves.end() || seg_it->second.kind != detail::CurveKind::LineSegment || seg_it->second.poles.size() < 2) {
        return false;
    }
    const auto uv0 = to_uv(frame, seg_it->second.poles.front());
    const auto uv1 = to_uv(frame, seg_it->second.poles.back());
    auto hits = intersect_line_with_rect_uv(frame, uv0, uv1);
    if (hits.size() < 2) {
        return false;
    }

    // Pick two hits on opposite boundaries.
    std::optional<RectIntersection> h0;
    std::optional<RectIntersection> h1;
    auto pick = [&](int b0, int b1) -> bool {
        std::optional<RectIntersection> a;
        std::optional<RectIntersection> b;
        for (const auto& h : hits) {
            if (h.boundary == b0 && !a.has_value()) a = h;
            if (h.boundary == b1 && !b.has_value()) b = h;
        }
        if (a.has_value() && b.has_value()) {
            h0 = a;
            h1 = b;
            return true;
        }
        return false;
    };
    const bool ok_lr = pick(0, 1);
    const bool ok_bt = ok_lr ? false : pick(2, 3);
    if (!ok_lr && !ok_bt) {
        return false;
    }

    // Map boundaries to rectangle edges indices (from loop order v0->v1->v2->v3->v0):
    // boundary 0(u=0)   => edge v3->v0 (edges[3])
    // boundary 1(u=uLen)=> edge v1->v2 (edges[1])
    // boundary 2(v=0)   => edge v0->v1 (edges[0])
    // boundary 3(v=vLen)=> edge v2->v3 (edges[2])
    const auto loop_it = state.loops.find(loop_id.value);
    std::array<std::uint64_t, 4> eids {};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto coe_it = state.coedges.find(loop_it->second.coedges[i].value);
        if (coe_it == state.coedges.end()) return false;
        eids[i] = coe_it->second.edge_id.value;
    }

    auto make_split_vertex = [&](const RectIntersection& h) -> VertexId {
        const auto p = from_uv(frame, h.u, h.v);
        const auto vid = VertexId {state.allocate_id()};
        state.vertices.emplace(vid.value, detail::VertexRecord {p});
        return vid;
    };
    const auto va = make_split_vertex(*h0);
    const auto vb = make_split_vertex(*h1);

    // Split edges and build two quad loops.
    // For left/right: cut between edges[3] (v3-v0) and edges[1] (v1-v2)
    // For bottom/top: cut between edges[0] (v0-v1) and edges[2] (v2-v3)
    std::array<EdgeId, 2> split_edges {};
    std::array<VertexId, 2> split_end_a {};
    std::array<VertexId, 2> split_end_b {};
    if (ok_lr) {
        split_edges = {EdgeId {eids[3]}, EdgeId {eids[1]}};
        split_end_a = {verts[3], verts[0]};
        split_end_b = {verts[1], verts[2]};
    } else {
        split_edges = {EdgeId {eids[0]}, EdgeId {eids[2]}};
        split_end_a = {verts[0], verts[1]};
        split_end_b = {verts[2], verts[3]};
    }

    // Split the two hit edges in-place across the whole shell to preserve closedness.
    const auto split_a = split_edge_in_shell(state, shell_id, split_edges[0], va);
    const auto split_b = split_edge_in_shell(state, shell_id, split_edges[1], vb);
    if (!split_a.has_value() || !split_b.has_value()) {
        return false;
    }
    const auto cut = create_line_edge(state, va, vb);

    // Pick correct segment edge ids for the target loops by oriented endpoints.
    auto pick_segment = [&](const SplitEdgeResult& s, VertexId start, VertexId end) -> EdgeId {
        bool rev = false;
        if (coedge_reversed_for_oriented_edge(state, s.e0, start, end, rev)) {
            return s.e0;
        }
        if (coedge_reversed_for_oriented_edge(state, s.e1, start, end, rev)) {
            return s.e1;
        }
        return {};
    };
    // For loop construction we need four boundary segments:
    EdgeId seg_a_to_0 {};
    EdgeId seg_3_to_a {};
    EdgeId seg_1_to_b {};
    EdgeId seg_b_to_2 {};
    EdgeId seg_0_to_a {};
    EdgeId seg_a_to_1 {};
    EdgeId seg_2_to_b {};
    EdgeId seg_b_to_3 {};
    if (ok_lr) {
        // left edge between v3 and v0 split at va
        seg_3_to_a = pick_segment(*split_a, verts[3], va);
        seg_a_to_0 = pick_segment(*split_a, va, verts[0]);
        // right edge between v1 and v2 split at vb
        seg_1_to_b = pick_segment(*split_b, verts[1], vb);
        seg_b_to_2 = pick_segment(*split_b, vb, verts[2]);
        if (seg_3_to_a.value == 0 || seg_a_to_0.value == 0 || seg_1_to_b.value == 0 || seg_b_to_2.value == 0) {
            return false;
        }
    } else {
        // bottom edge between v0 and v1 split at va
        seg_0_to_a = pick_segment(*split_a, verts[0], va);
        seg_a_to_1 = pick_segment(*split_a, va, verts[1]);
        // top edge between v2 and v3 split at vb
        seg_2_to_b = pick_segment(*split_b, verts[2], vb);
        seg_b_to_3 = pick_segment(*split_b, vb, verts[3]);
        if (seg_0_to_a.value == 0 || seg_a_to_1.value == 0 || seg_2_to_b.value == 0 || seg_b_to_3.value == 0) {
            return false;
        }
    }

    auto make_coedge = [&](EdgeId e, VertexId s, VertexId t) -> CoedgeId {
        bool rev = false;
        if (!coedge_reversed_for_oriented_edge(state, e, s, t, rev)) {
            return {};
        }
        const auto cid = CoedgeId {state.allocate_id()};
        state.coedges.emplace(cid.value, detail::CoedgeRecord {e, rev});
        return cid;
    };

    // Build two loops depending on cut orientation.
    LoopId loop_a {};
    LoopId loop_b {};
    if (ok_lr) {
        // loop_a: v0 -> v1 -> vb -> va -> v0
        const auto c01 = make_coedge(EdgeId {eids[0]}, verts[0], verts[1]);
        const auto c1b = make_coedge(seg_1_to_b, verts[1], vb);
        const auto cba = make_coedge(cut, vb, va);
        const auto ca0 = make_coedge(seg_a_to_0, va, verts[0]);
        if (c01.value == 0 || c1b.value == 0 || cba.value == 0 || ca0.value == 0) return false;
        loop_a = LoopId {state.allocate_id()};
        state.loops.emplace(loop_a.value, detail::LoopRecord {std::vector<CoedgeId> {c01, c1b, cba, ca0}});

        // loop_b: va -> vb -> v2 -> v3 -> va
        const auto cab = make_coedge(cut, va, vb);
        const auto cb2 = make_coedge(seg_b_to_2, vb, verts[2]);
        const auto c23 = make_coedge(EdgeId {eids[2]}, verts[2], verts[3]);
        const auto c3a = make_coedge(seg_3_to_a, verts[3], va);
        if (cab.value == 0 || cb2.value == 0 || c23.value == 0 || c3a.value == 0) return false;
        loop_b = LoopId {state.allocate_id()};
        state.loops.emplace(loop_b.value, detail::LoopRecord {std::vector<CoedgeId> {cab, cb2, c23, c3a}});
    } else {
        // bottom/top cut:
        // loop_a: v0 -> va -> vb -> v3 -> v0 (uses eA0, cut, eB1, edge v3->v0)
        const auto c0a = make_coedge(seg_0_to_a, verts[0], va);
        const auto cab = make_coedge(cut, va, vb);
        const auto cb3 = make_coedge(seg_b_to_3, vb, verts[3]);
        const auto c30 = make_coedge(EdgeId {eids[3]}, verts[3], verts[0]);
        if (c0a.value == 0 || cab.value == 0 || cb3.value == 0 || c30.value == 0) return false;
        loop_a = LoopId {state.allocate_id()};
        state.loops.emplace(loop_a.value, detail::LoopRecord {std::vector<CoedgeId> {c0a, cab, cb3, c30}});

        // loop_b: va -> v1 -> v2 -> vb -> va
        const auto ca1 = make_coedge(seg_a_to_1, va, verts[1]);
        const auto c12 = make_coedge(EdgeId {eids[1]}, verts[1], verts[2]);
        const auto c2b = make_coedge(seg_2_to_b, verts[2], vb);
        const auto cba = make_coedge(cut, vb, va);
        if (ca1.value == 0 || c12.value == 0 || c2b.value == 0 || cba.value == 0) return false;
        loop_b = LoopId {state.allocate_id()};
        state.loops.emplace(loop_b.value, detail::LoopRecord {std::vector<CoedgeId> {ca1, c12, c2b, cba}});
    }

    // Create two new faces on same surface.
    const auto face_a = FaceId {state.allocate_id()};
    const auto face_b = FaceId {state.allocate_id()};
    detail::FaceRecord fa;
    fa.surface_id = face_it->second.surface_id;
    fa.outer_loop = loop_a;
    fa.source_faces = face_it->second.source_faces.empty() ? std::vector<FaceId> {face_id} : face_it->second.source_faces;
    detail::FaceRecord fb;
    fb.surface_id = face_it->second.surface_id;
    fb.outer_loop = loop_b;
    fb.source_faces = face_it->second.source_faces.empty() ? std::vector<FaceId> {face_id} : face_it->second.source_faces;
    state.faces.emplace(face_a.value, std::move(fa));
    state.faces.emplace(face_b.value, std::move(fb));

    // Replace face in shell with the two faces.
    auto& faces = shell_it->second.faces;
    const auto it = std::find_if(faces.begin(), faces.end(), [face_id](FaceId f) { return f.value == face_id.value; });
    if (it == faces.end()) {
        return false;
    }
    *it = face_a;
    faces.push_back(face_b);
    return true;
}

// Minimal imprint: split a rectangular plane face into two triangle faces along a diagonal.
// If prefer_diag_02 is true, split along (v0 -> v2); otherwise split along (v1 -> v3).
bool imprint_split_rect_face_diagonal(detail::KernelState& state, ShellId shell_id, FaceId face_id, bool prefer_diag_02) {
    const auto shell_it = state.shells.find(shell_id.value);
    const auto face_it = state.faces.find(face_id.value);
    if (shell_it == state.shells.end() || face_it == state.faces.end()) {
        return false;
    }
    const auto loop_it = state.loops.find(face_it->second.outer_loop.value);
    if (loop_it == state.loops.end() || loop_it->second.coedges.size() != 4) {
        return false;
    }
    const auto surf_it = state.surfaces.find(face_it->second.surface_id.value);
    if (surf_it == state.surfaces.end() || surf_it->second.kind != detail::SurfaceKind::Plane) {
        return false;
    }

    std::array<CoedgeId, 4> coedges {
        loop_it->second.coedges[0],
        loop_it->second.coedges[1],
        loop_it->second.coedges[2],
        loop_it->second.coedges[3],
    };
    std::array<std::uint64_t, 4> edges {};
    std::array<VertexId, 4> verts {};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto coe_it = state.coedges.find(coedges[i].value);
        if (coe_it == state.coedges.end()) {
            return false;
        }
        edges[i] = coe_it->second.edge_id.value;
        const auto oriented = oriented_vertices_for_coedge_local(state, coedges[i]);
        if (!oriented.has_value()) {
            return false;
        }
        verts[i] = (*oriented)[0];
    }
    const auto v0 = verts[0];
    const auto v1 = verts[1];
    const auto v2 = verts[2];
    const auto v3 = verts[3];

    const auto diag_start = prefer_diag_02 ? v0 : v1;
    const auto diag_end = prefer_diag_02 ? v2 : v3;

    const auto v0_it = state.vertices.find(diag_start.value);
    const auto v2_it = state.vertices.find(diag_end.value);
    if (v0_it == state.vertices.end() || v2_it == state.vertices.end()) {
        return false;
    }
    // Create diagonal edge.
    detail::CurveRecord diag_curve;
    diag_curve.kind = detail::CurveKind::Line;
    diag_curve.origin = v0_it->second.point;
    diag_curve.direction = detail::normalize(detail::subtract(v2_it->second.point, v0_it->second.point));
    const auto diag_curve_id = CurveId {state.allocate_id()};
    state.curves.emplace(diag_curve_id.value, std::move(diag_curve));

    const auto diag_edge_id = EdgeId {state.allocate_id()};
    state.edges.emplace(diag_edge_id.value, detail::EdgeRecord {diag_curve_id, diag_start, diag_end});

    auto make_tri_loop = [&state](std::array<EdgeId, 3> tri_edges, std::array<bool, 3> tri_rev) -> LoopId {
        std::vector<CoedgeId> coeds;
        coeds.reserve(3);
        for (int i = 0; i < 3; ++i) {
            const auto cid = CoedgeId {state.allocate_id()};
            state.coedges.emplace(cid.value, detail::CoedgeRecord {tri_edges[static_cast<std::size_t>(i)], tri_rev[static_cast<std::size_t>(i)]});
            coeds.push_back(cid);
        }
        const auto lid = LoopId {state.allocate_id()};
        state.loops.emplace(lid.value, detail::LoopRecord {std::move(coeds)});
        return lid;
    };

    LoopId loop_a {};
    LoopId loop_b {};
    if (prefer_diag_02) {
        // Diagonal v0 -> v2 (original behavior).
        bool rev01 = false, rev12 = false, rev20 = false;
        if (!coedge_reversed_for_oriented_edge(state, EdgeId {edges[0]}, v0, v1, rev01) ||
            !coedge_reversed_for_oriented_edge(state, EdgeId {edges[1]}, v1, v2, rev12) ||
            !coedge_reversed_for_oriented_edge(state, diag_edge_id, v2, v0, rev20)) {
            return false;
        }
        loop_a = make_tri_loop({EdgeId {edges[0]}, EdgeId {edges[1]}, diag_edge_id}, {rev01, rev12, rev20});

        bool rev02 = false, rev23 = false, rev30 = false;
        if (!coedge_reversed_for_oriented_edge(state, diag_edge_id, v0, v2, rev02) ||
            !coedge_reversed_for_oriented_edge(state, EdgeId {edges[2]}, v2, v3, rev23) ||
            !coedge_reversed_for_oriented_edge(state, EdgeId {edges[3]}, v3, v0, rev30)) {
            return false;
        }
        loop_b = make_tri_loop({diag_edge_id, EdgeId {edges[2]}, EdgeId {edges[3]}}, {rev02, rev23, rev30});
    } else {
        // Diagonal v1 -> v3.
        bool rev12 = false, rev23 = false, rev31 = false;
        if (!coedge_reversed_for_oriented_edge(state, EdgeId {edges[1]}, v1, v2, rev12) ||
            !coedge_reversed_for_oriented_edge(state, EdgeId {edges[2]}, v2, v3, rev23) ||
            !coedge_reversed_for_oriented_edge(state, diag_edge_id, v3, v1, rev31)) {
            return false;
        }
        loop_a = make_tri_loop({EdgeId {edges[1]}, EdgeId {edges[2]}, diag_edge_id}, {rev12, rev23, rev31});

        bool rev13 = false, rev30 = false, rev01 = false;
        if (!coedge_reversed_for_oriented_edge(state, diag_edge_id, v1, v3, rev13) ||
            !coedge_reversed_for_oriented_edge(state, EdgeId {edges[3]}, v3, v0, rev30) ||
            !coedge_reversed_for_oriented_edge(state, EdgeId {edges[0]}, v0, v1, rev01)) {
            return false;
        }
        loop_b = make_tri_loop({diag_edge_id, EdgeId {edges[3]}, EdgeId {edges[0]}}, {rev13, rev30, rev01});
    }

    // Create two new faces on same surface.
    const auto face_a = FaceId {state.allocate_id()};
    const auto face_b = FaceId {state.allocate_id()};
    detail::FaceRecord fa;
    fa.surface_id = face_it->second.surface_id;
    fa.outer_loop = loop_a;
    fa.source_faces = face_it->second.source_faces.empty() ? std::vector<FaceId> {face_id} : face_it->second.source_faces;
    detail::FaceRecord fb;
    fb.surface_id = face_it->second.surface_id;
    fb.outer_loop = loop_b;
    fb.source_faces = face_it->second.source_faces.empty() ? std::vector<FaceId> {face_id} : face_it->second.source_faces;
    state.faces.emplace(face_a.value, std::move(fa));
    state.faces.emplace(face_b.value, std::move(fb));

    // Replace face in shell with the two faces.
    auto& faces = shell_it->second.faces;
    const auto it = std::find_if(faces.begin(), faces.end(), [face_id](FaceId f) { return f.value == face_id.value; });
    if (it == faces.end()) {
        return false;
    }
    *it = face_a;
    faces.push_back(face_b);
    return true;
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

Result<BodyId> PrimitiveService::wedge(const Point3& origin, Scalar dx, Scalar dy, Scalar dz) {
    if (dx <= 0.0 || dy <= 0.0 || dz <= 0.0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "楔体创建失败：尺寸必须大于 0", "楔体创建失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Wedge;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "wedge";
    record.origin = origin;
    record.a = dx;
    record.b = dy;
    record.c = dz;
    record.bbox = box_bbox(origin, dx, dy, dz);
    return ok_result(make_body(state_, record, "已创建楔体"), state_->create_diagnostic("已创建楔体"));
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
    record.axis = dir;
    record.b = distance;
    if (!profile.polygon_xyz.empty()) {
        if (profile.polygon_xyz.size() < 3) {
            return detail::invalid_input_result<BodyId>(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "拉伸失败：polygon 轮廓点数不足（至少 3 个点）", "拉伸失败");
        }
        BoundingBox bbox {};
        auto extend = [&](const Point3& p) {
            if (!bbox.is_valid) {
                bbox.min = p;
                bbox.max = p;
                bbox.is_valid = true;
                return;
            }
            bbox.min.x = std::min(bbox.min.x, p.x);
            bbox.min.y = std::min(bbox.min.y, p.y);
            bbox.min.z = std::min(bbox.min.z, p.z);
            bbox.max.x = std::max(bbox.max.x, p.x);
            bbox.max.y = std::max(bbox.max.y, p.y);
            bbox.max.z = std::max(bbox.max.z, p.z);
        };
        for (const auto& p : profile.polygon_xyz) {
            extend(p);
            extend(detail::add_point_vec(p, detail::scale(dir, distance)));
        }
        if (!bbox.is_valid) {
            return detail::invalid_input_result<BodyId>(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "拉伸失败：polygon 轮廓点非法，无法形成有效包围盒", "拉伸失败");
        }
        record.bbox = bbox;
    } else {
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
    }
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
    if (op == BooleanOp::Subtract) {
        append_shell_provenance_for_body(*state_, record.source_shells, lhs);
        append_face_provenance_for_body(*state_, record.source_faces, lhs);
    } else {
        append_shell_provenance_for_body(*state_, record.source_shells, lhs);
        append_shell_provenance_for_body(*state_, record.source_shells, rhs);
        append_face_provenance_for_body(*state_, record.source_faces, lhs);
        append_face_provenance_for_body(*state_, record.source_faces, rhs);
    }
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

    BodyId output = make_body(state_, record, "已完成布尔操作");
    detail::invalidate_eval_for_bodies(*state_, {lhs, rhs});
    const auto diag = boolean_options.diagnostics ? state_->create_diagnostic("布尔操作完成") : DiagnosticId {};
    append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageCandidates,
                               "布尔候选构建阶段开始：已进入壳/区域级候选统计流程",
                               {lhs.value, rhs.value});
    const auto face_candidates = build_face_candidates_for_boolean(*state_, lhs, rhs);
    append_boolean_face_candidate_issue(*state_, diag, lhs, rhs, face_candidates.size());
    const auto intersection_curves = compute_intersection_curves_for_candidates(*state_, face_candidates);
    append_boolean_intersection_curve_issue(*state_, diag, lhs, rhs, intersection_curves);
    const auto intersection_segments = clip_intersection_lines_to_face_overlap(*state_, intersection_curves);
    append_boolean_intersection_segment_issue(*state_, diag, lhs, rhs, intersection_segments);
    if (diag.value != 0 && !intersection_segments.empty()) {
        append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageSplit,
                                  "布尔切分阶段开始：已准备执行 imprint/split 占位路径",
                                  {lhs.value, rhs.value, output.value});
        std::vector<CurveId> curves;
        curves.reserve(intersection_segments.size());
        for (const auto& seg : intersection_segments) {
            curves.push_back(seg.curve);
        }
        // Stage 2: store intersection wires for later imprint/split/classify.
        const auto iid = store_intersection(state_, "boolean_intersection_wires", record.bbox, std::move(curves), {});
        append_boolean_intersection_stored_issue(*state_, diag, lhs, rhs, iid, intersection_segments.size());

        // Stage 2 minimal imprint: mutate output owned topology (split one rectangular face) to enter real split/imprint development.
        const auto out_it = state_->bodies.find(output.value);
        if (out_it != state_->bodies.end() && !out_it->second.shells.empty()) {
            const auto shell_id = out_it->second.shells.front();
            const auto shell_it = state_->shells.find(shell_id.value);
            if (shell_it != state_->shells.end()) {
                FaceId target {};
                for (const auto fid : shell_it->second.faces) {
                    const auto fit = state_->faces.find(fid.value);
                    if (fit == state_->faces.end()) {
                        continue;
                    }
                    const auto lit = state_->loops.find(fit->second.outer_loop.value);
                    if (lit == state_->loops.end()) {
                        continue;
                    }
                    if (lit->second.coedges.size() == 4) {
                        target = fid;
                        break;
                    }
                }
                if (target.value != 0) {
                    auto seg_curve = longest_intersection_segment_curve(*state_, intersection_segments);
                    if (seg_curve.value == 0) {
                        seg_curve = intersection_segments.front().curve;
                    }
                    bool applied = false;
                    if (imprint_split_rect_face_by_segment(*state_, shell_id, target, seg_curve)) {
                        applied = true;
                        auto issue = detail::make_info_issue(diag_codes::kBoolImprintSegmentApplied,
                                                             "布尔切分/imprint 已应用：输出壳的矩形面已按交线段切分为两四边形");
                        issue.related_entities = {lhs.value, rhs.value, output.value, shell_id.value, target.value};
                        state_->append_diagnostic_issue(diag, std::move(issue));
                    } else {
                        bool prefer_diag_02 = true;
                        const auto curve_it = state_->curves.find(seg_curve.value);
                        if (curve_it != state_->curves.end() && curve_it->second.kind == detail::CurveKind::LineSegment &&
                            curve_it->second.poles.size() >= 2) {
                            const auto seg_dir = detail::normalize(detail::subtract(curve_it->second.poles.back(),
                                                                                   curve_it->second.poles.front()));
                            const auto face_it = state_->faces.find(target.value);
                            if (face_it != state_->faces.end()) {
                                const auto loop_it = state_->loops.find(face_it->second.outer_loop.value);
                                if (loop_it != state_->loops.end() && loop_it->second.coedges.size() == 4) {
                                    std::array<VertexId, 4> verts {};
                                    for (std::size_t i = 0; i < 4; ++i) {
                                        const auto oriented = oriented_vertices_for_coedge_local(*state_, loop_it->second.coedges[i]);
                                        if (!oriented.has_value()) {
                                            break;
                                        }
                                        verts[i] = (*oriented)[0];
                                    }
                                    const auto v0_it = state_->vertices.find(verts[0].value);
                                    const auto v1_it = state_->vertices.find(verts[1].value);
                                    const auto v2_it = state_->vertices.find(verts[2].value);
                                    const auto v3_it = state_->vertices.find(verts[3].value);
                                    if (v0_it != state_->vertices.end() && v1_it != state_->vertices.end() &&
                                        v2_it != state_->vertices.end() && v3_it != state_->vertices.end()) {
                                        const auto d02 = detail::normalize(detail::subtract(v2_it->second.point, v0_it->second.point));
                                        const auto d13 = detail::normalize(detail::subtract(v3_it->second.point, v1_it->second.point));
                                        const auto s02 = std::abs(detail::dot(d02, seg_dir));
                                        const auto s13 = std::abs(detail::dot(d13, seg_dir));
                                        prefer_diag_02 = s02 >= s13;
                                    }
                                }
                            }
                        }
                        if (imprint_split_rect_face_diagonal(*state_, shell_id, target, prefer_diag_02)) {
                            applied = true;
                            auto issue = detail::make_info_issue(diag_codes::kBoolImprintApplied,
                                                                 "布尔切分/imprint 已应用：输出壳的矩形面已沿对角线切分");
                            issue.related_entities = {lhs.value, rhs.value, output.value, shell_id.value, target.value};
                            state_->append_diagnostic_issue(diag, std::move(issue));
                        }
                    }

                    if (applied) {
                        detail::rebuild_topology_links(*state_);
                    }
                }
            }
        }
    }
    append_boolean_prep_candidate_issue(*state_, diag, lhs, rhs, prep);
    append_boolean_run_stage_issue(*state_, diag, op, relation, prep, lhs, rhs, output);
    append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageOutputMaterialized,
                               "布尔输出占位物化完成：最小 owned topology / 回退链路已执行",
                               {lhs.value, rhs.value, output.value});

    if (diag.value != 0) {
        append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageClassify,
                                  "布尔分类阶段开始：将对输出面执行最小可解释分类统计",
                                  {lhs.value, rhs.value, output.value});
        // Stage 2 classification v1: point classification against analytic RHS primitive when available.
        auto point_in_cylinder = [&](const detail::BodyRecord& cyl, const Point3& p, Scalar eps) -> int {
            // returns: 1 inside, 0 on, -1 outside
            const auto C = cyl.origin;
            const auto a = cyl.axis;
            const auto r = cyl.a;
            const auto h = cyl.b;
            const Vec3 cp {p.x - C.x, p.y - C.y, p.z - C.z};
            const auto t = detail::dot(cp, a);
            const auto half = h * 0.5;
            if (t < -half - eps || t > half + eps) {
                return -1;
            }
            const Vec3 proj {a.x * t, a.y * t, a.z * t};
            const Vec3 radial {cp.x - proj.x, cp.y - proj.y, cp.z - proj.z};
            const auto rr = detail::dot(radial, radial);
            const auto r2 = r * r;
            if (rr > r2 + eps) {
                return -1;
            }
            if (std::abs(rr - r2) <= eps || std::abs(t - half) <= eps || std::abs(t + half) <= eps) {
                return 0;
            }
            return 1;
        };

        auto face_representative_point = [&](FaceId face_id, bool& ok) -> Point3 {
            ok = false;
            const auto face_it = state_->faces.find(face_id.value);
            if (face_it == state_->faces.end()) {
                return {};
            }
            const auto loop_it = state_->loops.find(face_it->second.outer_loop.value);
            if (loop_it == state_->loops.end() || loop_it->second.coedges.empty()) {
                return {};
            }
            Point3 sum {0.0, 0.0, 0.0};
            std::size_t count = 0;
            for (const auto coedge_id : loop_it->second.coedges) {
                const auto oriented = oriented_vertices_for_coedge_local(*state_, coedge_id);
                if (!oriented.has_value()) {
                    continue;
                }
                const auto v_it = state_->vertices.find((*oriented)[0].value);
                if (v_it == state_->vertices.end()) {
                    continue;
                }
                sum.x += v_it->second.point.x;
                sum.y += v_it->second.point.y;
                sum.z += v_it->second.point.z;
                ++count;
            }
            if (count == 0) {
                return {};
            }
            ok = true;
            return Point3 {sum.x / static_cast<Scalar>(count),
                           sum.y / static_cast<Scalar>(count),
                           sum.z / static_cast<Scalar>(count)};
        };

        std::size_t face_total = 0;
        std::size_t classified_inside = 0;
        std::size_t classified_on = 0;
        std::size_t classified_outside = 0;
        std::size_t classified_unknown = 0;
        std::string method = "bbox_fallback";
        std::unordered_map<std::uint64_t, int> face_cls;

        const auto rhs_it = state_->bodies.find(rhs.value);
        const auto out_it = state_->bodies.find(output.value);
        if (rhs_it != state_->bodies.end() && out_it != state_->bodies.end() && !out_it->second.shells.empty()) {
            const auto& rhs_body = rhs_it->second;
            const auto& out_body = out_it->second;
            const Scalar eps = std::max<Scalar>(state_->config.tolerance.linear, 1e-6);
            const bool use_cylinder = rhs_body.kind == detail::BodyKind::Cylinder && rhs_body.rep_kind == RepKind::ExactBRep &&
                                      rhs_body.a > 0.0 && rhs_body.b > 0.0;
            if (use_cylinder) {
                method = "cylinder_point_classification";
            }
            for (const auto shell_id : out_body.shells) {
                const auto shell_it = state_->shells.find(shell_id.value);
                if (shell_it == state_->shells.end()) {
                    continue;
                }
                for (const auto face_id : shell_it->second.faces) {
                    ++face_total;
                    bool ok = false;
                    const auto p = face_representative_point(face_id, ok);
                    if (!ok) {
                        face_cls[face_id.value] = -2;
                        ++classified_unknown;
                        continue;
                    }
                    int cls = -2;
                    if (use_cylinder) {
                        cls = point_in_cylinder(rhs_body, p, eps);
                    } else {
                        // Fallback: bbox-based point inclusion.
                        if (!rhs_bbox.is_valid) {
                            cls = -2;
                        } else if (p.x >= rhs_bbox.min.x - eps && p.x <= rhs_bbox.max.x + eps &&
                                   p.y >= rhs_bbox.min.y - eps && p.y <= rhs_bbox.max.y + eps &&
                                   p.z >= rhs_bbox.min.z - eps && p.z <= rhs_bbox.max.z + eps) {
                            cls = 1;
                        } else {
                            cls = -1;
                        }
                    }
                    face_cls[face_id.value] = cls;
                    if (cls == 1) {
                        ++classified_inside;
                    } else if (cls == 0) {
                        ++classified_on;
                    } else if (cls == -1) {
                        ++classified_outside;
                    } else {
                        ++classified_unknown;
                    }
                }
            }
        }

        {
            std::ostringstream msg;
            msg << "布尔分类阶段完成: method=" << method
                << " face_total=" << face_total
                << " inside=" << classified_inside
                << " on=" << classified_on
                << " outside=" << classified_outside
                << " unknown=" << classified_unknown;
            auto issue = detail::make_info_issue(diag_codes::kBoolClassificationCompleted, msg.str());
            issue.related_entities = {lhs.value, rhs.value, output.value};
            state_->append_diagnostic_issue(diag, std::move(issue));
        }

        bool subtract_shell_rebuild_applied = false;
        bool subtract_shell_rebuild_rolled_back = false;
        if (op == BooleanOp::Subtract && prep.overlap_candidates > 0 && out_it != state_->bodies.end() &&
            !out_it->second.shells.empty() && !face_cls.empty()) {
            append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageRebuild,
                                      "布尔重建阶段开始：将尝试按分类结果裁剪/重建输出壳（占位策略）",
                                      {lhs.value, rhs.value, output.value});
            const auto shell_id = out_it->second.shells.front();
            const auto shell_it = state_->shells.find(shell_id.value);
            if (shell_it != state_->shells.end()) {
                std::vector<FaceId> seed;
                seed.reserve(shell_it->second.faces.size());
                for (const auto face_id : shell_it->second.faces) {
                    const auto it = face_cls.find(face_id.value);
                    const int c = (it == face_cls.end()) ? -2 : it->second;
                    if (c != 1) {
                        seed.push_back(face_id);
                    }
                }
                auto closed = detail::build_closed_face_region_from_source_faces(*state_, shell_id, seed);
                auto& shell_faces = shell_it->second.faces;
                if (!closed.empty() && closed.size() >= 6 && !same_unordered_face_ids(closed, shell_faces)) {
                    auto backup_faces = shell_faces;
                    shell_faces = std::move(closed);
                    detail::rebuild_topology_links(*state_);
                    ValidationService validation_after_trim {state_};
                    const auto trim_strict = validation_after_trim.validate_topology(output, ValidationMode::Strict);
                    if (trim_strict.status != StatusCode::Ok) {
                        shell_faces = std::move(backup_faces);
                        detail::rebuild_topology_links(*state_);
                        subtract_shell_rebuild_rolled_back = true;
                    } else {
                        subtract_shell_rebuild_applied = true;
                    }
                }
            }
        }

        ValidationService validation {state_};
        append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageValidate,
                                  "布尔验证阶段开始：将对输出执行 Strict 拓扑验证",
                                  {lhs.value, rhs.value, output.value});
        auto strict_result = validation.validate_topology(output, ValidationMode::Strict);
        bool auto_repair_used = false;
        if (strict_result.status != StatusCode::Ok && boolean_options.auto_repair) {
            append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageRepair,
                                      "布尔修复阶段开始：Strict 未通过，将尝试 auto_repair(Safe)",
                                      {lhs.value, rhs.value, output.value});
            RepairService repair {state_};
            const auto repaired = repair.auto_repair(output, RepairMode::Safe);
            if (repaired.status == StatusCode::Ok && repaired.value.has_value()) {
                output = repaired.value->output;
                strict_result = validation.validate_topology(output, ValidationMode::Strict);
                auto_repair_used = true;
            }
        }
        {
            std::ostringstream msg;
            msg << "布尔重建阶段完成: strict_ok=" << (strict_result.status == StatusCode::Ok ? "true" : "false")
                << " subtract_shell_rebuild_applied=" << (subtract_shell_rebuild_applied ? "true" : "false")
                << " subtract_shell_rebuild_rollback=" << (subtract_shell_rebuild_rolled_back ? "true" : "false")
                << " auto_repair=" << (auto_repair_used ? "true" : "false");
            auto issue = detail::make_info_issue(diag_codes::kBoolRebuildCompleted, msg.str());
            issue.related_entities = {lhs.value, rhs.value, output.value};
            state_->append_diagnostic_issue(diag, std::move(issue));
        }
    }
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
    append_shells_for_face_owned_by_body(*state_, record.source_shells, target, body_id);
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
    append_shells_for_face(*state_, record.source_shells, target);
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
    return ok_result(
        make_report(StatusCode::Ok, output, diag,
                    {detail::make_warning(diag_codes::kBlendApproximatePlaceholder,
                                          "圆角：当前为拓扑占位与参数门禁，工业级滚球/角区/变半径未实现")}),
        diag);
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
    auto report = make_report(StatusCode::Ok, output, diag,
                              {detail::make_warning(diag_codes::kBlendApproximatePlaceholder,
                                                    "倒角：当前为拓扑占位与参数门禁，工业级角区/变距几何未实现")});
    return ok_result(std::move(report), diag);
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
