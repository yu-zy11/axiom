#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "axiom/core/result.h"
#include "axiom/core/types.h"
#include "axiom/internal/core/kernel_state.h"

namespace axiom {
namespace ops_internal {

constexpr Scalar kPi = 3.14159265358979323846;

bool is_primitive_body_kind(detail::BodyKind kind);

void clear_primitive_provenance_fields(detail::BodyRecord& record);

enum class BBoxRelation {
    Disjoint,
    Touching,
    Overlapping,
    LhsContainsRhs,
    RhsContainsLhs
};

BodyId make_body(std::shared_ptr<detail::KernelState> state, detail::BodyRecord record, const std::string&);

void append_unique_body(std::vector<BodyId>& ids, BodyId id);

void append_unique_face(std::vector<FaceId>& ids, FaceId id);

void append_unique_shell(std::vector<ShellId>& ids, ShellId id);

void append_shells_for_face(const detail::KernelState& state, std::vector<ShellId>& ids, FaceId face_id);

void append_shells_for_face_owned_by_body(const detail::KernelState& state, std::vector<ShellId>& ids,
                                         FaceId face_id, BodyId body_id);

void append_shell_provenance_for_body(const detail::KernelState& state, std::vector<ShellId>& ids, BodyId body_id);

void append_face_provenance_for_body(const detail::KernelState& state, std::vector<FaceId>& ids, BodyId body_id);

void detach_owned_topology(const detail::KernelState& state, detail::BodyRecord& record);

void append_faces_for_edge(const detail::KernelState& state, std::vector<FaceId>& ids, EdgeId edge_id);

bool same_unordered_face_ids(const std::vector<FaceId>& a, const std::vector<FaceId>& b);

OpReport make_report(StatusCode status, BodyId output, DiagnosticId diagnostic_id, std::vector<Warning> warnings = {});

BoundingBox box_bbox(const Point3& origin, Scalar dx, Scalar dy, Scalar dz);

BoundingBox cylinder_bbox(const Point3& center, const Vec3& axis_unit, Scalar radius, Scalar height);

BoundingBox cone_bbox(const Point3& apex, const Vec3& axis_unit, Scalar semi_angle, Scalar height);

MassProperties bbox_mass_properties(const BoundingBox& bbox);

void set_box_inertia_about_centroid(MassProperties& props, Scalar dx, Scalar dy, Scalar dz);

bool bbox_corners_almost_equal(const BoundingBox& a, const BoundingBox& b, Scalar eps);

MassProperties mass_properties_from_box_body_record(const detail::BodyRecord& b);

void add_inertia_parallel_axis(std::array<Scalar, 9>& I, Scalar m, const Point3& cm, const Point3& ref);

void set_wedge_inertia_about_centroid(MassProperties& props, Scalar dx, Scalar dy, Scalar dz);

void set_sphere_inertia_about_center(MassProperties& props, Scalar radius);

void set_axisymmetric_inertia_world(MassProperties& props, const Vec3& axis, Scalar i_transverse, Scalar i_axial);

void set_cylinder_inertia_about_center(MassProperties& props, const Vec3& axis, Scalar radius, Scalar height);

void set_cone_inertia_about_centroid(MassProperties& props, const Vec3& axis, Scalar semi_angle, Scalar height);

void set_torus_inertia_about_center(MassProperties& props, const Vec3& axis, Scalar major_r, Scalar minor_r);

bool try_primitive_analytic_mass_properties(const detail::BodyRecord& body, MassProperties& props);

Scalar interval_gap(Scalar a_min, Scalar a_max, Scalar b_min, Scalar b_max);

bool contains_bbox(const BoundingBox& outer, const BoundingBox& inner);

Scalar overlap_extent(Scalar a_min, Scalar a_max, Scalar b_min, Scalar b_max);

std::vector<Point3> bbox_corners(const BoundingBox& bbox);

BoundingBox curve_bbox_for_query(const detail::CurveRecord& curve);

BoundingBox surface_bbox_for_query(const detail::SurfaceRecord& surface);

Scalar plane_signed_distance(const Plane& plane, const Point3& point);

bool intersects_plane(const BoundingBox& bbox, const Plane& plane);

BoundingBox union_bbox(const BoundingBox& lhs, const BoundingBox& rhs);

BoundingBox intersect_bbox(const BoundingBox& lhs, const BoundingBox& rhs);

BBoxRelation classify_bbox_relation(const BoundingBox& lhs, const BoundingBox& rhs);

Scalar bbox_min_extent(const BoundingBox& bbox);

BoundingBox offset_bbox(const BoundingBox& bbox, Scalar distance);

Scalar bbox_max_axis_rectangle_area(const BoundingBox& fb);

Scalar distance_point_to_unit_axis(const Point3& p, const Point3& origin, const Vec3& u);

struct PappusRevolveMass {
    Scalar volume {};
    Scalar profile_area {};
};

std::optional<PappusRevolveMass> try_pappus_revolve_mass(std::span<const Point3> poly, const Point3& O,
                                                         const Vec3& axis_u, Scalar angle);

bool try_sweep_body_mass_properties(const detail::BodyRecord& body, MassProperties& props);

bool try_boolean_operand_mass_properties(const detail::BodyRecord& body, MassProperties& m);

Point3 rotate_point_around_unit_axis(const Point3& p, const Point3& origin, const Vec3& u, Scalar cos_t,
                                     Scalar sin_t);

bool valid_axis(const Vec3& axis);

bool valid_face_ids(const detail::KernelState& state, std::span<const FaceId> faces);

bool valid_edge_ids(const detail::KernelState& state, std::span<const EdgeId> edges);

BoundingBox face_bbox(const detail::KernelState& state, FaceId face_id);

BoundingBox shell_bbox(const detail::KernelState& state, ShellId shell_id);

std::vector<ShellId> shells_for_body(const detail::BodyRecord& body);

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

std::vector<FaceId> faces_for_body_boolean(const detail::KernelState& state, BodyId body_id);

std::vector<FaceCandidatePair> build_face_candidates_for_boolean(const detail::KernelState& state,
                                                                 BodyId lhs,
                                                                 BodyId rhs);

Vec3 safe_unit_normal_local(const Vec3& normal);

bool plane_plane_intersection_line(const detail::KernelState& state,
                                   FaceId lhs_face,
                                   FaceId rhs_face,
                                   Point3& out_origin,
                                   Vec3& out_dir);

std::vector<IntersectionCurve> compute_intersection_curves_for_candidates(detail::KernelState& state,
                                                                          std::span<const FaceCandidatePair> candidates);

std::vector<IntersectionSegment> clip_intersection_lines_to_face_overlap(detail::KernelState& state,
                                                                         std::span<const IntersectionCurve> curves);

CurveId longest_intersection_segment_curve(const detail::KernelState& state,
                                           std::span<const IntersectionSegment> segments);

std::vector<BoundingBox> body_regions_for_boolean(const detail::KernelState& state, BodyId body_id);

Scalar bbox_volume(const BoundingBox& bbox);

void combine_mass_properties_sum(const MassProperties& m0, const MassProperties& m1, MassProperties& out);

bool mass_properties_axis_aligned_bbox_solid(const BoundingBox& bb, MassProperties& out);

bool try_subtract_nested_axis_aligned_box_mass_properties(const MassProperties& m_big, const MassProperties& m_small,
                                                            MassProperties& out);

bool try_boolean_two_operand_mass_properties(const detail::KernelState& state, const detail::BodyRecord& body,
                                             MassProperties& out);

BooleanPrepStats compute_boolean_prep_stats(const detail::KernelState& state, BodyId lhs, BodyId rhs);

void set_boolean_diagnostic_stage(Issue& issue, std::string_view code);

void append_boolean_prep_candidate_issue(detail::KernelState& state,
                                         DiagnosticId diag,
                                         BodyId lhs,
                                         BodyId rhs,
                                         const BooleanPrepStats& prep);

const char* bbox_relation_name(BBoxRelation relation);

const char* boolean_op_name(BooleanOp op);

void append_boolean_run_stage_issue(detail::KernelState& state,
                                    DiagnosticId diag,
                                    BooleanOp op,
                                    BBoxRelation relation,
                                    const BooleanPrepStats& prep,
                                    BodyId lhs,
                                    BodyId rhs,
                                    BodyId output);

void append_boolean_stage_issue(detail::KernelState& state,
                                DiagnosticId diag,
                                std::string_view code,
                                std::string message,
                                std::vector<std::uint64_t> related_entities);

void append_boolean_face_candidate_issue(detail::KernelState& state,
                                        DiagnosticId diag,
                                        BodyId lhs,
                                        BodyId rhs,
                                        std::size_t count);

void append_boolean_intersection_curve_issue(detail::KernelState& state,
                                             DiagnosticId diag,
                                             BodyId lhs,
                                             BodyId rhs,
                                             std::span<const IntersectionCurve> curves);

void append_boolean_intersection_segment_issue(detail::KernelState& state,
                                               DiagnosticId diag,
                                               BodyId lhs,
                                               BodyId rhs,
                                               std::span<const IntersectionSegment> segments);

void append_boolean_intersection_stored_issue(detail::KernelState& state,
                                              DiagnosticId diag,
                                              BodyId lhs,
                                              BodyId rhs,
                                              IntersectionId intersection_id,
                                              std::size_t curve_count);

std::optional<std::array<VertexId, 2>> oriented_vertices_for_coedge_local(const detail::KernelState& state, CoedgeId coedge_id);

bool coedge_reversed_for_oriented_edge(const detail::KernelState& state, EdgeId edge_id, VertexId start, VertexId end, bool& out_reversed);

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
                                RectFrame& out_frame);

std::array<Scalar, 2> to_uv(const RectFrame& f, const Point3& p);

Point3 from_uv(const RectFrame& f, Scalar u, Scalar v);

bool uv_inside_rect(const RectFrame& f, Scalar u, Scalar v, Scalar eps);

struct RectIntersection {
    Scalar u {0.0};
    Scalar v {0.0};
    int boundary {-1}; // 0: u=0, 1: u=u_len, 2: v=0, 3: v=v_len
};

std::vector<RectIntersection> intersect_line_with_rect_uv(const RectFrame& f,
                                                          const std::array<Scalar, 2>& p0,
                                                          const std::array<Scalar, 2>& p1);

EdgeId create_line_edge(detail::KernelState& state, VertexId a, VertexId b);

struct SplitEdgeResult {
    EdgeId e0 {}; // start -> mid
    EdgeId e1 {}; // mid -> end
    VertexId start {};
    VertexId end {};
};

std::optional<SplitEdgeResult> split_edge_in_shell(detail::KernelState& state, ShellId shell_id, EdgeId edge_id, VertexId mid_vertex);

bool imprint_split_rect_face_by_segment(detail::KernelState& state,
                                        ShellId shell_id,
                                        FaceId face_id,
                                        CurveId segment_curve_id);

bool imprint_split_rect_face_diagonal(detail::KernelState& state, ShellId shell_id, FaceId face_id, bool prefer_diag_02);

IntersectionId store_intersection(std::shared_ptr<detail::KernelState> state,
                                  std::string label,
                                  BoundingBox bbox,
                                  std::vector<CurveId> curves = {},
                                  std::vector<SurfaceId> surfaces = {});

}  // namespace ops_internal
}  // namespace axiom
