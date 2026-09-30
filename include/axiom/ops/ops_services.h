#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
}

class PrimitiveService {
public:
    explicit PrimitiveService(std::shared_ptr<detail::KernelState> state);

    Result<BodyId> box(const Point3& origin, Scalar dx, Scalar dy, Scalar dz);
    Result<BodyId> sphere(const Point3& center, Scalar radius);
    Result<BodyId> cylinder(const Point3& center, const Vec3& axis, Scalar radius, Scalar height);
    Result<BodyId> cone(const Point3& apex, const Vec3& axis, Scalar semi_angle, Scalar height);
    Result<BodyId> torus(const Point3& center, const Vec3& axis, Scalar major_r, Scalar minor_r);
    Result<BodyId> wedge(const Point3& origin, Scalar dx, Scalar dy, Scalar dz);

private:
    std::shared_ptr<detail::KernelState> state_;
};

/// A key in a piecewise-linear straight extrusion law. Angles are radians about
/// the extrusion direction, and scales act about the caller's coplanar center.
struct ExtrusionLawStation {
    Scalar fraction {};
    Scalar scale {1.0};
    Scalar twist_angle {};
};

/// A positive uniform section scale at a normalized sampled rail arc length.
struct SweepScaleStation {
    Scalar fraction {};
    Scalar scale {1.0};
};

/// Uniform scale and signed radians of section roll at a sampled rail arc length.
struct SweepLawStation {
    Scalar fraction {};
    Scalar scale {1.0};
    Scalar twist_angle {};
};

class SweepService {
public:
    explicit SweepService(std::shared_ptr<detail::KernelState> state);

    /// Simple planar polygons (including concave profiles and disjoint holes) produce a triangulated closed prism.
    /// Requires positive finite distance and a direction transverse to the profile plane.
    Result<BodyId> extrude(const ProfileRef& profile, const Vec3& direction, Scalar distance);
    /// Explicit planar polygon, optionally with holes: straight extrusion with uniform section scaling.
    /// At t in [0,1], p becomes center + (1+t*(end_scale-1))*(p-center) + t*unit(direction)*distance.
    /// The finite center must lie in the profile plane; distance is finite and positive, end_scale is finite and nonnegative.
    /// Positive scales preserve concavities/holes. Zero scale closes a hole-free convex/concave profile
    /// at one shared apex (center + unit(direction)*distance); holes and numerical degeneracy are rejected.
    Result<BodyId> extrude_scaled(const ProfileRef& profile, const Vec3& direction, Scalar distance,
                                 const Point3& center, Scalar end_scale);
    /// Extrude an explicit planar polygon while rotating its section uniformly about the
    /// extrusion axis. The finite center lies in the profile plane, direction must be normal
    /// to that plane, distance is positive, and the signed twist is limited to one full turn.
    /// For inputs satisfying this normal-direction contract, zero twist is equivalent to extrude.
    /// Positive/negative twists, concavities and disjoint holes are conservatively subdivided into
    /// an owned triangulated closed BRep. The result is a sampled polyhedron rather than an analytic
    /// helicoidal surface.
    Result<BodyId> extrude_twisted(const ProfileRef& profile, const Vec3& direction, Scalar distance,
                                  const Point3& center, Scalar twist_angle);
    /// Extrude an explicit planar polygon with a piecewise-linear scale/twist law.
    /// Requires a positive finite distance, a direction normal to the profile and a finite
    /// coplanar center. At least two keys must strictly increase in fraction from 0 to 1;
    /// the first key has scale 1 and angle 0. Scales must remain finite and strictly positive.
    /// Expansion/contraction, twist plateaus and reversals are supported, with total absolute
    /// angular travel at most 2*pi. Convex/concave outlines and disjoint non-nested holes keep
    /// their correspondence. Each interval is subdivided to at most 7.5 degrees of twist
    /// and at most 25% of its smaller endpoint scale per step; all keys are retained.
    /// Up to 4096 sampled intervals are accepted. The owned triangulated BRep represents
    /// the sampled law, including intermediate bounds and polyhedral mass properties;
    /// rounded section degeneracy and intersecting or tolerance-touching nonadjacent walls
    /// fail before allocation.
    /// Zero/apex, reflected sections, analytic helicoidal surfaces and to-plane termination
    /// are not supported. Failure leaves model stores and any active topology transaction intact.
    Result<BodyId> extrude_with_law(const ProfileRef& profile, const Vec3& direction, Scalar distance,
                                   const Point3& center, std::span<const ExtrusionLawStation> stations);
    /// Extrude an explicit planar polygon (including concavities/holes) along direction to a plane.
    /// Every boundary point must reach the plane strictly forward, beyond the planarity tolerance.
    /// Direction must be transverse to both planes; normal sign and vector magnitudes are immaterial.
    /// Produces an actual planar closed BRep; touching/crossing planes and numerical degeneracy are rejected.
    Result<BodyId> extrude_to_plane(const ProfileRef& profile, const Vec3& direction, const Plane& end_plane);
    /// Revolve a profile from its current position through a positive angle no greater than one full turn.
    /// Explicit planar polygons are conservatively subdivided in angle and produce an
    /// owned triangulated closed BRep when the axis lies in the profile plane. Partial
    /// and full turns support a profile strictly separated from the axis, including
    /// concave outlines and disjoint interior holes. A hole-free profile may instead
    /// meet the axis along one boundary edge. Profiles that cross the axis, touch it
    /// at an isolated point, have a holed region touching the axis, or are numerically
    /// near-degenerate are rejected before allocation.
    Result<BodyId> revolve(const ProfileRef& profile, const Axis3& axis, Scalar angle);
    /// Revolve an explicit profile over the directed angular interval [start_angle, end_angle].
    /// Angles are radians about axis.direction. The finite, nonzero signed span must have magnitude
    /// no greater than one full turn; decreasing intervals sweep in the opposite direction. This
    /// supports offset and symmetric feature placement without pre-rotating or copying the profile.
    /// The same planar-region, axis-clearance, conservative subdivision and transactional guarantees
    /// as revolve apply. A full-turn interval is independent of its start angle.
    Result<BodyId> revolve_between(const ProfileRef& profile, const Axis3& axis,
                                   Scalar start_angle, Scalar end_angle);
    /// Explicit polygons along a bounded rail, producing owned triangulated closed topology.
    /// Line segments and CompositePolyline keep the profile in world space and require every
    /// segment to advance through its plane. Bezier, B-spline and NURBS rails use a sampled
    /// rotation-minimizing frame. A CompositeChain may join bounded line, circle/ellipse arc,
    /// Bezier, B-spline and NURBS children when adjacent endpoints and tangent directions agree;
    /// closed chains, endpoint/tangent-continuous closed splines and Circle/Ellipse rails are
    /// periodic and have no caps. Curve-following profiles
    /// must start on the rail in a plane normal to its tangent. Concavities and holes are supported;
    /// gaps, tangent-discontinuous joints, cusps, nested chains, excessive curvature and
    /// self-approaching rails are rejected without allocating a body or owned topology.
    Result<BodyId> sweep(const ProfileRef& profile, CurveId rail);
    /// Sweep an explicit polygon along a supported rail while uniformly scaling each transported
    /// section from 1 at the rail start to end_scale at the rail end. Non-unit scaling requires
    /// an open rail, is linear in sampled arc length and is centered on the rail. Line and CompositePolyline
    /// rails require their start point to lie in the profile plane; curved rails retain the
    /// rotation-minimizing frame contract of sweep. The terminal scale must be finite and
    /// strictly positive. Periodic rails only accept 1 because unequal seam sections cannot
    /// form a closed shell; zero/apex and negative/reflected sections are not supported.
    Result<BodyId> sweep_scaled(const ProfileRef& profile, CurveId rail, Scalar end_scale);
    /// Sweep an explicit convex/concave polygon, optionally with disjoint non-nested
    /// holes, with a piecewise-linear positive scale law centered on the rail.
    /// Keys strictly increase from fraction 0 to 1, with initial scale 1. Fractions
    /// refer to the existing sampled rail's chord arc length, not its parameter or
    /// analytic arc length. Intermediate expansions, contractions and plateaus are
    /// retained, along with every original rail station. Curved rails transport the
    /// section in rotation-minimizing frames; inserted frames interpolate transport
    /// between those stations. Linear/polyline rails keep the section in world space
    /// and must advance strictly through its plane with a coplanar starting point.
    /// Periodic curved rails accept varying scales when the final key is exactly 1;
    /// the repeated end ring is welded to the start and receives no cap.
    /// Each step changes scale by at most 25% of its smaller endpoint scale, with
    /// at most 4096 intervals including original stations and law keys. Rounded
    /// section degeneracy, folds and contacts between triangles without shared
    /// vertices fail before allocation. Existing conservative curvature/clearance
    /// gates use the largest requested scale. Curve-wall validation accepts at
    /// most 2,000,000 broad-phase candidate pairs. Numerically coincident law keys
    /// are rejected; a key coincident with an original rail station shares that
    /// station's position and frame. Sampling/materialization diagnostics include
    /// their stage and requested scale bounds. Results are sampled polyhedral BReps
    /// with actual topology, intermediate bounds and polyhedral mass properties.
    /// Zero/negative scales, apexes, nested composite rails and analytic sweep
    /// surfaces are unsupported. Failure preserves stores and active transactions.
    Result<BodyId> sweep_with_scale_law(const ProfileRef& profile, CurveId rail,
                                      std::span<const SweepScaleStation> stations);
    /// Sweep with piecewise-linear positive scale and signed section twist laws.
    /// Keys strictly increase in sampled chord arc-length fraction from (0,1,0)
    /// to fraction 1. All original rail stations and caller keys are retained.
    /// Roll is relative to the transported frame, about the local forward tangent
    /// for curved rails. Linear/polyline rails retain parallel section planes and
    /// roll about the initial profile normal oriented toward the rail's net advance;
    /// every segment must advance through that plane, and the start is coplanar.
    /// Supports convex/concave outlines and disjoint non-nested holes, expansion,
    /// contraction, twist pauses and reversals. Total absolute roll travel is at
    /// most 2*pi; each step has at most 7.5 degrees of roll and a scale change of
    /// at most 25% of its smaller endpoint, with at most 4096 combined intervals.
    /// Periodic curved rails require final scale 1 and final roll exactly 0 or
    /// +/-2*pi (within 1e-10 radians). The last ring is welded to the first without
    /// caps; no symmetry-based vertex permutation is inferred. Numerically close
    /// distinct keys fail; keys coincident with rail stations share their frames.
    /// Uses the scale-law curvature/clearance and contact-candidate gates, validates
    /// every rounded section and wall before allocation, and returns queryable
    /// sampled polyhedral topology, bounds and mass properties. Alternating wall
    /// diagonals reduce systematic twist volume bias. Zero/negative scale, apexes,
    /// nested composite rails and analytic sweep surfaces remain unsupported.
    /// Failure leaves model stores and active topology transactions intact.
    Result<BodyId> sweep_with_law(const ProfileRef& profile, CurveId rail,
                                std::span<const SweepLawStation> stations);
    /// Loft two or more explicit planar polygon sections into an owned triangulated closed BRep.
    /// Sections may be concave and may carry corresponding disjoint holes. Outer rings and each
    /// same-index hole must keep the same vertex count, because vertices define the ruled-wall
    /// correspondence; winding is independent. Sections must be strictly ordered along a common
    /// transverse direction and retain a valid interpolated region. Unlike ring topology,
    /// automatic vertex matching, branching and collapsed/apex sections are rejected before allocation.
    Result<BodyId> loft(std::span<const ProfileRef> profiles);
    Result<BodyId> thicken(FaceId face_id, Scalar distance);

private:
    Result<BodyId> sweep_impl(const ProfileRef& profile, CurveId rail, Scalar end_scale,
                              std::span<const SweepScaleStation> stations,
                              std::span<const SweepLawStation> section_stations = {});
    std::shared_ptr<detail::KernelState> state_;
};

class BooleanService {
public:
    explicit BooleanService(std::shared_ptr<detail::KernelState> state);

    Result<OpReport> run(BooleanOp op, BodyId lhs, BodyId rhs, const BooleanOptions& options);
    Result<void> export_boolean_prep_stats(BodyId lhs, BodyId rhs, std::string_view path) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class ModifyService {
public:
    explicit ModifyService(std::shared_ptr<detail::KernelState> state);

    Result<OpReport> offset_body(BodyId body_id, Scalar distance, const TolerancePolicy& tolerance);
    Result<OpReport> shell_body(BodyId body_id, std::span<const FaceId> removed_faces, Scalar thickness);
    Result<OpReport> draft_faces(BodyId body_id, std::span<const FaceId> faces, const Vec3& pull_dir, Scalar angle);
    Result<OpReport> replace_face(BodyId body_id, FaceId target, SurfaceId replacement);
    Result<OpReport> delete_face_and_heal(BodyId body_id, FaceId target);

private:
    std::shared_ptr<detail::KernelState> state_;
};

class BlendService {
public:
    explicit BlendService(std::shared_ptr<detail::KernelState> state);

    Result<OpReport> fillet_edges(BodyId body_id, std::span<const EdgeId> edges, Scalar radius);
    Result<OpReport> chamfer_edges(BodyId body_id, std::span<const EdgeId> edges, Scalar distance);

private:
    std::shared_ptr<detail::KernelState> state_;
};

class QueryService {
public:
    explicit QueryService(std::shared_ptr<detail::KernelState> state);

    Result<IntersectionId> intersect(CurveId curve_id, SurfaceId surface_id) const;
    Result<IntersectionId> intersect(SurfaceId lhs, SurfaceId rhs) const;
    Result<MeshId> section(BodyId body_id, const Plane& plane) const;
    Result<MassProperties> mass_properties(BodyId body_id) const;
    Result<Scalar> min_distance(BodyId lhs, BodyId rhs) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

}  // namespace axiom
