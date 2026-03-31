#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <span>
#include <vector>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
}

class LinearAlgebraService {
public:
    explicit LinearAlgebraService(std::shared_ptr<detail::KernelState> state);

    Scalar dot(const Vec3& lhs, const Vec3& rhs) const;
    Vec3 cross(const Vec3& lhs, const Vec3& rhs) const;
    Vec3 add(const Vec3& lhs, const Vec3& rhs) const;
    Vec3 subtract(const Vec3& lhs, const Vec3& rhs) const;
    Vec3 scale(const Vec3& value, Scalar factor) const;
    Vec3 hadamard(const Vec3& lhs, const Vec3& rhs) const;
    Scalar norm(const Vec3& value) const;
    Scalar squared_norm(const Vec3& value) const;
    Vec3 normalize(const Vec3& value) const;
    Scalar distance(const Point3& lhs, const Point3& rhs) const;
    Scalar squared_distance(const Point3& lhs, const Point3& rhs) const;
    Scalar manhattan_distance(const Point3& lhs, const Point3& rhs) const;
    Point3 midpoint(const Point3& lhs, const Point3& rhs) const;
    Point3 lerp(const Point3& lhs, const Point3& rhs, Scalar t) const;
    Scalar angle_between(const Vec3& lhs, const Vec3& rhs) const;
    Scalar scalar_triple_product(const Vec3& a, const Vec3& b, const Vec3& c) const;
    Scalar distance_point_to_line(const Point3& point, const Point3& line_origin, const Vec3& line_direction) const;
    Scalar distance_point_to_plane(const Point3& point, const Point3& plane_origin, const Vec3& plane_normal) const;
    Scalar triangle_area(const Point3& a, const Point3& b, const Point3& c) const;
    Scalar tetrahedron_signed_volume(const Point3& a, const Point3& b, const Point3& c, const Point3& d) const;
    Vec3 project(const Vec3& lhs, const Vec3& rhs) const;
    Vec3 reject(const Vec3& lhs, const Vec3& rhs) const;
    Vec3 clamp_norm(const Vec3& value, Scalar max_norm) const;
    std::optional<std::pair<Vec3, Vec3>> orthonormal_basis(const Vec3& normal) const;
    bool is_near_zero(const Vec3& value, Scalar eps) const;
    bool is_finite(const Vec3& value) const;
    Transform3 identity_transform() const;
    Transform3 compose(const Transform3& lhs, const Transform3& rhs) const;
    Transform3 make_translation(const Vec3& delta) const;
    Transform3 make_scale(Scalar sx, Scalar sy, Scalar sz) const;
    Transform3 make_rotation_axis_angle(const Vec3& axis, Scalar angle_rad) const;
    bool invert_affine(const Transform3& in, Transform3& out, Scalar eps) const;
    std::vector<Point3> transform_points(std::span<const Point3> points, const Transform3& transform) const;
    std::vector<Vec3> transform_vectors(std::span<const Vec3> vectors, const Transform3& transform) const;
    Point3 transform(const Point3& point, const Transform3& transform) const;
    Vec3 transform(const Vec3& vector, const Transform3& transform) const;
    Point3 centroid(std::span<const Point3> points) const;
    Vec3 average(std::span<const Vec3> vectors) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class PredicateService {
public:
    explicit PredicateService(std::shared_ptr<detail::KernelState> state);

    Sign orient2d(const Point2& a, const Point2& b, const Point2& c) const;
    Sign orient3d(const Point3& a, const Point3& b, const Point3& c, const Point3& d) const;
    Sign orient2d_tol(const Point2& a, const Point2& b, const Point2& c, Scalar eps) const;
    Sign orient3d_tol(const Point3& a, const Point3& b, const Point3& c, const Point3& d, Scalar eps) const;
    bool aabb_intersects(const BoundingBox& lhs, const BoundingBox& rhs, Scalar tolerance) const;
    bool point_in_bbox(const Point3& point, const BoundingBox& bbox, Scalar tolerance) const;
    bool point_equal_tol(const Point3& lhs, const Point3& rhs, Scalar tolerance) const;
    bool bbox_contains(const BoundingBox& outer, const BoundingBox& inner, Scalar tolerance) const;
    bool bbox_valid(const BoundingBox& bbox) const;
    std::optional<BoundingBox> bbox_intersection(const BoundingBox& lhs, const BoundingBox& rhs, Scalar tolerance) const;
    Scalar bbox_overlap_ratio(const BoundingBox& lhs, const BoundingBox& rhs, Scalar tolerance) const;
    bool bbox_center_in(const BoundingBox& inner, const BoundingBox& outer, Scalar tolerance) const;
    bool range1d_overlap(const Range1D& lhs, const Range1D& rhs, Scalar tolerance) const;
    bool range2d_overlap(const Range2D& lhs, const Range2D& rhs, Scalar tolerance) const;
    bool point_in_sphere(const Point3& point, const Point3& center, Scalar radius, Scalar tolerance) const;
    bool point_in_cylinder_approx(
        const Point3& point, const Point3& origin, const Vec3& axis, Scalar radius, Scalar height, Scalar tolerance) const;
    bool vec_parallel(const Vec3& lhs, const Vec3& rhs, Scalar angular_tolerance) const;
    bool vec_orthogonal(const Vec3& lhs, const Vec3& rhs, Scalar angular_tolerance) const;
    Result<bool> point_on_curve(const Point3& point, CurveId curve_id, Scalar tolerance) const;
    Result<std::vector<bool>> point_on_curve_batch(std::span<const Point3> points, CurveId curve_id, Scalar tolerance) const;
    Result<bool> point_on_surface(const Point3& point, SurfaceId surface_id, Scalar tolerance) const;
    Result<std::vector<bool>> point_on_surface_batch(std::span<const Point3> points, SurfaceId surface_id, Scalar tolerance) const;
    Result<bool> point_in_body(const Point3& point, BodyId body_id, Scalar tolerance) const;
    Result<std::vector<bool>> point_in_body_batch(std::span<const Point3> points, BodyId body_id, Scalar tolerance) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class ToleranceService {
public:
    explicit ToleranceService(std::shared_ptr<detail::KernelState> state);

    TolerancePolicy global_policy() const;
    TolerancePolicy policy_for_body(BodyId body_id) const;
    TolerancePolicy override_policy(const TolerancePolicy& base, Scalar linear) const;
    TolerancePolicy clamp_policy(const TolerancePolicy& base) const;
    TolerancePolicy scale_policy(const TolerancePolicy& base, Scalar factor) const;
    TolerancePolicy scale_policy_for_body_nonlinear(const TolerancePolicy& base, BodyId body_id) const;
    TolerancePolicy merge_policy(const TolerancePolicy& primary, const TolerancePolicy& fallback) const;
    TolerancePolicy with_angular(const TolerancePolicy& base, Scalar angular) const;
    TolerancePolicy loosen_policy(const TolerancePolicy& base, Scalar factor) const;
    TolerancePolicy tighten_policy(const TolerancePolicy& base, Scalar factor) const;
    TolerancePolicy choose_body_or_global(BodyId body_id) const;
    Scalar effective_linear(Scalar requested) const;
    Scalar effective_angular(Scalar requested) const;
    Scalar normalize_linear_request(Scalar requested) const;
    Scalar normalize_angular_request(Scalar requested) const;
    Scalar resolve_linear_for_scale(Scalar requested, Scalar model_scale) const;
    Scalar resolve_angular_for_scale(Scalar requested, Scalar model_scale) const;
    int compare_linear(Scalar lhs, Scalar rhs, Scalar tolerance) const;
    int compare_angular(Scalar lhs, Scalar rhs, Scalar tolerance) const;
    bool within_linear(Scalar lhs, Scalar rhs, Scalar tolerance) const;
    bool within_angular(Scalar lhs, Scalar rhs, Scalar tolerance) const;
    bool is_valid_policy(const TolerancePolicy& policy) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

}  // namespace axiom
