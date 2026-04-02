#pragma once

#include <span>

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
/// 点 `point` 到闭线段 \([seg_a, seg_b]\) 的欧氏距离的平方；输入非有限或几何退化时不可定义则返回 `+Inf`（与 `LinearAlgebraService::distance_point_to_segment` 一致）。
Scalar squared_distance_point_to_segment(const Point3& point, const Point3& seg_a, const Point3& seg_b);
/// 欧氏距离门控：`sqrt(squared_distance_point_to_segment) <= max(tolerance,0)`；`tolerance` 非有限 → `false`。退化段与 `point_equal_tol(point, seg_a, tolerance)` 对齐。
bool point_on_segment_tol(const Point3& point, const Point3& seg_a, const Point3& seg_b, Scalar tolerance);
Scalar bbox_characteristic_length(const BoundingBox& bbox);
Scalar effective_tolerance(Scalar requested, Scalar fallback);
Scalar clamp_local_tolerance(Scalar value, const TolerancePolicy& policy);
/// 将调用方请求的线性容差与 `TolerancePolicy` 合成并钳制到 `[min_local,max_local]`。
/// `requested <= 0` 时回退到 `policy.linear`（与 `ToleranceService::effective_linear` 一致）。
Scalar resolve_linear_tolerance(Scalar requested, const TolerancePolicy& policy);
/// 角度容差；语义同 `ToleranceService::effective_angular`。
Scalar resolve_angular_tolerance(Scalar requested, const TolerancePolicy& policy);
Scalar resolve_linear_tolerance_for_scale(Scalar requested, const TolerancePolicy& policy, Scalar model_scale);
Scalar resolve_angular_tolerance_for_scale(Scalar requested, const TolerancePolicy& policy, Scalar model_scale);
bool within_tolerance(Scalar lhs, Scalar rhs, Scalar tolerance);
/// 与 `ToleranceService::nearly_equal_*` 对齐：lhs/rhs 或任一容差项非有限则返回 false。
bool nearly_equal_rel_abs(Scalar lhs, Scalar rhs, Scalar abs_tol, Scalar rel_tol);
/// 非有限操作数或容差时返回 0，避免 `NaN` 比较产生伪顺序。
int compare_rel_abs(Scalar lhs, Scalar rhs, Scalar abs_tol, Scalar rel_tol);
TolerancePolicy clamp_tolerance_policy(const TolerancePolicy& base);
TolerancePolicy scale_tolerance_policy(const TolerancePolicy& base, Scalar factor);
TolerancePolicy merge_tolerance_policy(const TolerancePolicy& primary, const TolerancePolicy& fallback);
TolerancePolicy tolerance_with_angular(const TolerancePolicy& base, Scalar angular);
bool valid_tolerance_policy(const TolerancePolicy& policy);

/// 2D 射线法判定点是否在简单多边形内部（奇偶规则）；`poly` 为有序顶点，首尾不重复。
/// 顶点数 \<3 时返回 `false`（无定义区域）。
inline bool point_in_polygon_2d_raycast(Point2 p, std::span<const Point2> poly) {
  if (poly.size() < 3) {
    return false;
  }
  bool inside = false;
  const std::size_t n = poly.size();
  for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
    const auto &a = poly[i];
    const auto &b = poly[j];
    if ((a.y > p.y) != (b.y > p.y)) {
      const Scalar xint =
          (b.x - a.x) * (p.y - a.y) / (b.y - a.y + static_cast<Scalar>(1e-30)) + a.x;
      if (p.x < xint) {
        inside = !inside;
      }
    }
  }
  return inside;
}

}  // namespace axiom::detail

