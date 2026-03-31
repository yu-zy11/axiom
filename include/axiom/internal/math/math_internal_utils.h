#pragma once

#include "axiom/core/types.h"

namespace axiom::detail {

Scalar safe_norm(const Vec3& value);
Scalar clamp_cosine(Scalar value);
bool finite_vec3(const Vec3& value);
bool finite_point3(const Point3& value);
bool valid_bbox(const BoundingBox& bbox);
bool point_in_bbox_tol(const Point3& point, const BoundingBox& bbox, Scalar tolerance);
Scalar vec_angle_between(const Vec3& lhs, const Vec3& rhs);
Vec3 project_vec(const Vec3& lhs, const Vec3& rhs);
Vec3 reject_vec(const Vec3& lhs, const Vec3& rhs);
Transform3 identity_transform();
Transform3 compose_transform(const Transform3& lhs, const Transform3& rhs);
Transform3 translation_transform(const Vec3& delta);
Transform3 scale_transform(Scalar sx, Scalar sy, Scalar sz);
Transform3 rotation_axis_angle_transform(const Vec3& axis, Scalar angle_rad);
bool invert_affine_transform(const Transform3& in, Transform3& out, Scalar eps);
bool vec_parallel_tol(const Vec3& lhs, const Vec3& rhs, Scalar angular_tolerance);
bool vec_orthogonal_tol(const Vec3& lhs, const Vec3& rhs, Scalar angular_tolerance);
bool bbox_intersects_tol(const BoundingBox& lhs, const BoundingBox& rhs, Scalar tolerance);
bool bbox_contains_tol(const BoundingBox& outer, const BoundingBox& inner, Scalar tolerance);
bool point_equal_tol(const Point3& lhs, const Point3& rhs, Scalar tolerance);
Scalar effective_tolerance(Scalar requested, Scalar fallback);
Scalar clamp_local_tolerance(Scalar value, const TolerancePolicy& policy);
Scalar resolve_linear_tolerance(Scalar requested, const TolerancePolicy& policy);
Scalar resolve_angular_tolerance(Scalar requested, const TolerancePolicy& policy);
Scalar resolve_linear_tolerance_for_scale(Scalar requested, const TolerancePolicy& policy, Scalar model_scale);
Scalar resolve_angular_tolerance_for_scale(Scalar requested, const TolerancePolicy& policy, Scalar model_scale);
bool within_tolerance(Scalar lhs, Scalar rhs, Scalar tolerance);
TolerancePolicy clamp_tolerance_policy(const TolerancePolicy& base);
TolerancePolicy scale_tolerance_policy(const TolerancePolicy& base, Scalar factor);
TolerancePolicy merge_tolerance_policy(const TolerancePolicy& primary, const TolerancePolicy& fallback);
TolerancePolicy tolerance_with_angular(const TolerancePolicy& base, Scalar angular);
bool valid_tolerance_policy(const TolerancePolicy& policy);

}  // namespace axiom::detail
