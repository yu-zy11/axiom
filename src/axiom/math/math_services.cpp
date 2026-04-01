#include "axiom/math/math_services.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "axiom/geo/geometry_services.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/math/math_internal_utils.h"

namespace axiom {

LinearAlgebraService::LinearAlgebraService(
    std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

Scalar LinearAlgebraService::dot(const Vec3 &lhs, const Vec3 &rhs) const {
  return detail::dot(lhs, rhs);
}

Vec3 LinearAlgebraService::cross(const Vec3 &lhs, const Vec3 &rhs) const {
  return detail::cross(lhs, rhs);
}

Vec3 LinearAlgebraService::add(const Vec3 &lhs, const Vec3 &rhs) const {
  return Vec3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 LinearAlgebraService::subtract(const Vec3 &lhs, const Vec3 &rhs) const {
  return Vec3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 LinearAlgebraService::scale(const Vec3 &value, Scalar factor) const {
  return Vec3{value.x * factor, value.y * factor, value.z * factor};
}

Vec3 LinearAlgebraService::hadamard(const Vec3 &lhs, const Vec3 &rhs) const {
  return Vec3{lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z};
}

Scalar LinearAlgebraService::norm(const Vec3 &value) const {
  return detail::norm(value);
}

Scalar LinearAlgebraService::squared_norm(const Vec3 &value) const {
  return value.x * value.x + value.y * value.y + value.z * value.z;
}

Vec3 LinearAlgebraService::normalize(const Vec3 &value) const {
  return detail::normalize(value);
}

Scalar LinearAlgebraService::distance(const Point3 &lhs,
                                      const Point3 &rhs) const {
  return detail::safe_norm(Vec3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z});
}

Scalar LinearAlgebraService::squared_distance(const Point3 &lhs,
                                              const Point3 &rhs) const {
  const auto dx = lhs.x - rhs.x;
  const auto dy = lhs.y - rhs.y;
  const auto dz = lhs.z - rhs.z;
  return dx * dx + dy * dy + dz * dz;
}

Scalar LinearAlgebraService::manhattan_distance(const Point3 &lhs,
                                                const Point3 &rhs) const {
  return std::abs(lhs.x - rhs.x) + std::abs(lhs.y - rhs.y) +
         std::abs(lhs.z - rhs.z);
}

Point3 LinearAlgebraService::midpoint(const Point3 &lhs,
                                      const Point3 &rhs) const {
  return Point3{(lhs.x + rhs.x) * 0.5, (lhs.y + rhs.y) * 0.5,
                (lhs.z + rhs.z) * 0.5};
}

Point3 LinearAlgebraService::lerp(const Point3 &lhs, const Point3 &rhs,
                                  Scalar t) const {
  return Point3{lhs.x + (rhs.x - lhs.x) * t, lhs.y + (rhs.y - lhs.y) * t,
                lhs.z + (rhs.z - lhs.z) * t};
}

Scalar LinearAlgebraService::angle_between(const Vec3 &lhs,
                                           const Vec3 &rhs) const {
  return detail::vec_angle_between(lhs, rhs);
}

Scalar LinearAlgebraService::scalar_triple_product(const Vec3 &a, const Vec3 &b,
                                                   const Vec3 &c) const {
  return detail::dot(detail::cross(a, b), c);
}

Scalar
LinearAlgebraService::distance_point_to_line(const Point3 &point,
                                             const Point3 &line_origin,
                                             const Vec3 &line_direction) const {
  const auto dir_norm = detail::safe_norm(line_direction);
  if (dir_norm <= 0.0) {
    return std::numeric_limits<Scalar>::infinity();
  }
  const auto diff = Vec3{point.x - line_origin.x, point.y - line_origin.y,
                         point.z - line_origin.z};
  const auto cross_v = detail::cross(diff, line_direction);
  return detail::safe_norm(cross_v) / dir_norm;
}

Scalar
LinearAlgebraService::distance_point_to_plane(const Point3 &point,
                                              const Point3 &plane_origin,
                                              const Vec3 &plane_normal) const {
  const auto n_norm = detail::safe_norm(plane_normal);
  if (n_norm <= 0.0) {
    return std::numeric_limits<Scalar>::infinity();
  }
  const auto diff = Vec3{point.x - plane_origin.x, point.y - plane_origin.y,
                         point.z - plane_origin.z};
  return std::abs(detail::dot(diff, plane_normal)) / n_norm;
}

Scalar LinearAlgebraService::triangle_area(const Point3 &a, const Point3 &b,
                                           const Point3 &c) const {
  const auto ab = Vec3{b.x - a.x, b.y - a.y, b.z - a.z};
  const auto ac = Vec3{c.x - a.x, c.y - a.y, c.z - a.z};
  return 0.5 * detail::safe_norm(detail::cross(ab, ac));
}

Scalar LinearAlgebraService::tetrahedron_signed_volume(const Point3 &a,
                                                       const Point3 &b,
                                                       const Point3 &c,
                                                       const Point3 &d) const {
  const auto ab = Vec3{b.x - a.x, b.y - a.y, b.z - a.z};
  const auto ac = Vec3{c.x - a.x, c.y - a.y, c.z - a.z};
  const auto ad = Vec3{d.x - a.x, d.y - a.y, d.z - a.z};
  return scalar_triple_product(ab, ac, ad) / 6.0;
}

Vec3 LinearAlgebraService::project(const Vec3 &lhs, const Vec3 &rhs) const {
  return detail::project_vec(lhs, rhs);
}

Vec3 LinearAlgebraService::reject(const Vec3 &lhs, const Vec3 &rhs) const {
  return detail::reject_vec(lhs, rhs);
}

Vec3 LinearAlgebraService::clamp_norm(const Vec3 &value,
                                      Scalar max_norm) const {
  const auto safe_max = std::max(max_norm, 0.0);
  const auto value_norm = detail::safe_norm(value);
  if (value_norm <= safe_max || value_norm <= 0.0) {
    return value;
  }
  return scale(value, safe_max / value_norm);
}

std::optional<std::pair<Vec3, Vec3>>
LinearAlgebraService::orthonormal_basis(const Vec3 &normal) const {
  const auto n = normalize(normal);
  if (detail::safe_norm(n) <= 0.0) {
    return std::nullopt;
  }
  const Vec3 helper =
      std::abs(n.z) < 0.9 ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, 0.0, 0.0};
  auto u = normalize(cross(helper, n));
  if (detail::safe_norm(u) <= 0.0) {
    return std::nullopt;
  }
  auto v = normalize(cross(n, u));
  if (detail::safe_norm(v) <= 0.0) {
    return std::nullopt;
  }
  return std::make_optional(std::make_pair(u, v));
}

bool LinearAlgebraService::is_near_zero(const Vec3 &value, Scalar eps) const {
  return detail::safe_norm(value) <= std::max(eps, 0.0);
}

bool LinearAlgebraService::is_finite(const Vec3 &value) const {
  return detail::finite_vec3(value);
}

Transform3 LinearAlgebraService::identity_transform() const {
  return detail::identity_transform();
}

Transform3 LinearAlgebraService::compose(const Transform3 &lhs,
                                         const Transform3 &rhs) const {
  return detail::compose_transform(lhs, rhs);
}

Transform3 LinearAlgebraService::make_translation(const Vec3 &delta) const {
  return detail::translation_transform(delta);
}

Transform3 LinearAlgebraService::make_scale(Scalar sx, Scalar sy,
                                            Scalar sz) const {
  return detail::scale_transform(sx, sy, sz);
}

Transform3
LinearAlgebraService::make_rotation_axis_angle(const Vec3 &axis,
                                               Scalar angle_rad) const {
  return detail::rotation_axis_angle_transform(axis, angle_rad);
}

bool LinearAlgebraService::invert_affine(const Transform3 &in, Transform3 &out,
                                         Scalar eps) const {
  return detail::invert_affine_transform(in, out, eps);
}

std::vector<Point3>
LinearAlgebraService::transform_points(std::span<const Point3> points,
                                       const Transform3 &transform) const {
  std::vector<Point3> out;
  out.reserve(points.size());
  for (const auto &p : points) {
    out.push_back(this->transform(p, transform));
  }
  return out;
}

std::vector<Vec3>
LinearAlgebraService::transform_vectors(std::span<const Vec3> vectors,
                                        const Transform3 &transform) const {
  std::vector<Vec3> out;
  out.reserve(vectors.size());
  for (const auto &v : vectors) {
    out.push_back(this->transform(v, transform));
  }
  return out;
}

Point3 LinearAlgebraService::transform(const Point3 &point,
                                       const Transform3 &transform) const {
  return Point3{transform.m[0] * point.x + transform.m[1] * point.y +
                    transform.m[2] * point.z + transform.m[3],
                transform.m[4] * point.x + transform.m[5] * point.y +
                    transform.m[6] * point.z + transform.m[7],
                transform.m[8] * point.x + transform.m[9] * point.y +
                    transform.m[10] * point.z + transform.m[11]};
}

Vec3 LinearAlgebraService::transform(const Vec3 &vector,
                                     const Transform3 &transform) const {
  return Vec3{transform.m[0] * vector.x + transform.m[1] * vector.y +
                  transform.m[2] * vector.z,
              transform.m[4] * vector.x + transform.m[5] * vector.y +
                  transform.m[6] * vector.z,
              transform.m[8] * vector.x + transform.m[9] * vector.y +
                  transform.m[10] * vector.z};
}

Point3 LinearAlgebraService::centroid(std::span<const Point3> points) const {
  if (points.empty()) {
    return Point3{0.0, 0.0, 0.0};
  }
  long double sx = 0.0L, sy = 0.0L, sz = 0.0L;
  for (const auto &p : points) {
    sx += static_cast<long double>(p.x);
    sy += static_cast<long double>(p.y);
    sz += static_cast<long double>(p.z);
  }
  const long double inv = 1.0L / static_cast<long double>(points.size());
  return Point3{static_cast<Scalar>(sx * inv), static_cast<Scalar>(sy * inv),
                static_cast<Scalar>(sz * inv)};
}

Vec3 LinearAlgebraService::average(std::span<const Vec3> vectors) const {
  if (vectors.empty()) {
    return Vec3{0.0, 0.0, 0.0};
  }
  long double sx = 0.0L, sy = 0.0L, sz = 0.0L;
  for (const auto &v : vectors) {
    sx += static_cast<long double>(v.x);
    sy += static_cast<long double>(v.y);
    sz += static_cast<long double>(v.z);
  }
  const long double inv = 1.0L / static_cast<long double>(vectors.size());
  return Vec3{static_cast<Scalar>(sx * inv), static_cast<Scalar>(sy * inv),
              static_cast<Scalar>(sz * inv)};
}

PredicateService::PredicateService(std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

Sign PredicateService::orient2d(const Point2 &a, const Point2 &b,
                                const Point2 &c) const {
  const Scalar user_eps =
      detail::resolve_linear_tolerance(0.0, state_->config.tolerance);
  return orient2d_tol(a, b, c, user_eps);
}

Sign PredicateService::orient3d(const Point3 &a, const Point3 &b,
                                const Point3 &c, const Point3 &d) const {
  const Scalar user_eps =
      detail::resolve_linear_tolerance(0.0, state_->config.tolerance);
  return orient3d_tol(a, b, c, d, user_eps);
}

Sign PredicateService::orient2d_tol(const Point2 &a, const Point2 &b,
                                    const Point2 &c, Scalar eps) const {
  if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) ||
      !std::isfinite(b.y) || !std::isfinite(c.x) || !std::isfinite(c.y)) {
    return Sign::Uncertain;
  }

  // Use long double to reduce overflow and cancellation risk.
  const long double bax = static_cast<long double>(b.x) - static_cast<long double>(a.x);
  const long double bay = static_cast<long double>(b.y) - static_cast<long double>(a.y);
  const long double cax = static_cast<long double>(c.x) - static_cast<long double>(a.x);
  const long double cay = static_cast<long double>(c.y) - static_cast<long double>(a.y);
  const long double det = bax * cay - bay * cax;
  if (!std::isfinite(det)) {
    return Sign::Uncertain;
  }

  const auto user_tol = std::max(eps, 0.0);
  // Scale-aware tolerance for determinant magnitude.
  const long double scale =
      std::max({std::abs(bax), std::abs(bay), std::abs(cax), std::abs(cay), 1.0L});
  const long double auto_tol =
      16.0L * static_cast<long double>(std::numeric_limits<Scalar>::epsilon()) * scale * scale;
  const long double tol = std::max(static_cast<long double>(user_tol), auto_tol);
  if (std::abs(det) <= tol) {
    return Sign::Zero;
  }
  return det > 0.0L ? Sign::Positive : Sign::Negative;
}

Sign PredicateService::orient3d_tol(const Point3 &a, const Point3 &b,
                                    const Point3 &c, const Point3 &d,
                                    Scalar eps) const {
  if (!detail::finite_point3(a) || !detail::finite_point3(b) ||
      !detail::finite_point3(c) || !detail::finite_point3(d)) {
    return Sign::Uncertain;
  }
  const auto ab = detail::subtract(b, a);
  const auto ac = detail::subtract(c, a);
  const auto ad = detail::subtract(d, a);
  if (!detail::finite_vec3(ab) || !detail::finite_vec3(ac) || !detail::finite_vec3(ad)) {
    return Sign::Uncertain;
  }

  const long double abx = static_cast<long double>(ab.x);
  const long double aby = static_cast<long double>(ab.y);
  const long double abz = static_cast<long double>(ab.z);
  const long double acx = static_cast<long double>(ac.x);
  const long double acy = static_cast<long double>(ac.y);
  const long double acz = static_cast<long double>(ac.z);
  const long double adx = static_cast<long double>(ad.x);
  const long double ady = static_cast<long double>(ad.y);
  const long double adz = static_cast<long double>(ad.z);
  const long double cx = aby * acz - abz * acy;
  const long double cy = abz * acx - abx * acz;
  const long double cz = abx * acy - aby * acx;
  const long double det = cx * adx + cy * ady + cz * adz;
  if (!std::isfinite(det)) {
    return Sign::Uncertain;
  }

  const auto user_tol = std::max(eps, 0.0);
  const long double scale = std::max(
      {std::abs(abx), std::abs(aby), std::abs(abz), std::abs(acx), std::abs(acy),
       std::abs(acz), std::abs(adx), std::abs(ady), std::abs(adz), 1.0L});
  const long double auto_tol =
      64.0L * static_cast<long double>(std::numeric_limits<Scalar>::epsilon()) * scale * scale *
      scale;
  const long double tol = std::max(static_cast<long double>(user_tol), auto_tol);
  if (std::abs(det) <= tol) {
    return Sign::Zero;
  }
  return det > 0.0L ? Sign::Positive : Sign::Negative;
}

bool PredicateService::aabb_intersects(const BoundingBox &lhs,
                                       const BoundingBox &rhs,
                                       Scalar tolerance) const {
  return detail::bbox_intersects_tol(lhs, rhs, tolerance);
}

bool PredicateService::point_in_bbox(const Point3 &point,
                                     const BoundingBox &bbox,
                                     Scalar tolerance) const {
  return detail::point_in_bbox_tol(point, bbox, tolerance);
}

bool PredicateService::point_equal_tol(const Point3 &lhs, const Point3 &rhs,
                                       Scalar tolerance) const {
  return detail::point_equal_tol(lhs, rhs, tolerance);
}

bool PredicateService::bbox_contains(const BoundingBox &outer,
                                     const BoundingBox &inner,
                                     Scalar tolerance) const {
  return detail::bbox_contains_tol(outer, inner, tolerance);
}

bool PredicateService::bbox_valid(const BoundingBox &bbox) const {
  return detail::valid_bbox(bbox);
}

std::optional<BoundingBox> PredicateService::bbox_intersection(
    const BoundingBox &lhs, const BoundingBox &rhs, Scalar tolerance) const {
  if (!bbox_valid(lhs) || !bbox_valid(rhs)) {
    return std::nullopt;
  }
  const auto tol = std::max(tolerance, 0.0);
  const Point3 min_p{std::max(lhs.min.x, rhs.min.x) - tol,
                     std::max(lhs.min.y, rhs.min.y) - tol,
                     std::max(lhs.min.z, rhs.min.z) - tol};
  const Point3 max_p{std::min(lhs.max.x, rhs.max.x) + tol,
                     std::min(lhs.max.y, rhs.max.y) + tol,
                     std::min(lhs.max.z, rhs.max.z) + tol};
  if (min_p.x > max_p.x || min_p.y > max_p.y || min_p.z > max_p.z) {
    return std::nullopt;
  }
  return std::make_optional(BoundingBox{min_p, max_p, true});
}

Scalar PredicateService::bbox_overlap_ratio(const BoundingBox &lhs,
                                            const BoundingBox &rhs,
                                            Scalar tolerance) const {
  const auto inter = bbox_intersection(lhs, rhs, tolerance);
  if (!inter.has_value() || !bbox_valid(lhs) || !bbox_valid(rhs)) {
    return 0.0;
  }
  const auto volume_of = [](const BoundingBox &box) -> Scalar {
    return std::max(0.0, box.max.x - box.min.x) *
           std::max(0.0, box.max.y - box.min.y) *
           std::max(0.0, box.max.z - box.min.z);
  };
  const auto union_volume = volume_of(lhs) + volume_of(rhs) - volume_of(*inter);
  if (union_volume <= 0.0) {
    return 0.0;
  }
  return std::clamp(volume_of(*inter) / union_volume, 0.0, 1.0);
}

bool PredicateService::bbox_center_in(const BoundingBox &inner,
                                      const BoundingBox &outer,
                                      Scalar tolerance) const {
  if (!bbox_valid(inner) || !bbox_valid(outer)) {
    return false;
  }
  const Point3 center{(inner.min.x + inner.max.x) * 0.5,
                      (inner.min.y + inner.max.y) * 0.5,
                      (inner.min.z + inner.max.z) * 0.5};
  return point_in_bbox(center, outer, tolerance);
}

bool PredicateService::range1d_overlap(const Range1D &lhs, const Range1D &rhs,
                                       Scalar tolerance) const {
  const auto tol = std::max(tolerance, 0.0);
  return (lhs.max + tol >= rhs.min) && (rhs.max + tol >= lhs.min);
}

bool PredicateService::range2d_overlap(const Range2D &lhs, const Range2D &rhs,
                                       Scalar tolerance) const {
  return range1d_overlap(lhs.u, rhs.u, tolerance) &&
         range1d_overlap(lhs.v, rhs.v, tolerance);
}

bool PredicateService::point_in_sphere(const Point3 &point,
                                       const Point3 &center, Scalar radius,
                                       Scalar tolerance) const {
  if (!detail::finite_point3(point) || !detail::finite_point3(center)) {
    return false;
  }
  if (radius < 0.0) {
    return false;
  }
  const auto tol = std::max(tolerance, 0.0);
  const auto diff =
      Vec3{point.x - center.x, point.y - center.y, point.z - center.z};
  return detail::safe_norm(diff) <= radius + tol;
}

bool PredicateService::point_in_cylinder_approx(const Point3 &point,
                                                const Point3 &origin,
                                                const Vec3 &axis, Scalar radius,
                                                Scalar height,
                                                Scalar tolerance) const {
  if (!detail::finite_point3(point) || !detail::finite_point3(origin) ||
      !detail::finite_vec3(axis)) {
    return false;
  }
  if (radius < 0.0 || height < 0.0) {
    return false;
  }
  const auto axis_len = detail::safe_norm(axis);
  if (axis_len <= 0.0) {
    return false;
  }
  const auto tol = std::max(tolerance, 0.0);
  const auto axis_dir =
      Vec3{axis.x / axis_len, axis.y / axis_len, axis.z / axis_len};
  const auto op =
      Vec3{point.x - origin.x, point.y - origin.y, point.z - origin.z};
  const auto axial = detail::dot(op, axis_dir);
  if (axial < -tol || axial > height + tol) {
    return false;
  }
  const auto radial = detail::reject_vec(op, axis_dir);
  return detail::safe_norm(radial) <= radius + tol;
}

bool PredicateService::vec_parallel(const Vec3 &lhs, const Vec3 &rhs,
                                    Scalar angular_tolerance) const {
  return detail::vec_parallel_tol(lhs, rhs, angular_tolerance);
}

bool PredicateService::vec_orthogonal(const Vec3 &lhs, const Vec3 &rhs,
                                      Scalar angular_tolerance) const {
  return detail::vec_orthogonal_tol(lhs, rhs, angular_tolerance);
}

Result<bool> PredicateService::point_on_curve(const Point3 &point,
                                              CurveId curve_id,
                                              Scalar tolerance) const {
  if (!detail::finite_point3(point)) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "点在曲线上判断失败：输入点非法", "点在曲线上判断失败");
  }
  if (tolerance < 0.0) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "点在曲线上判断失败：容差不能为负", "点在曲线上判断失败");
  }
  if (!detail::has_curve(*state_, curve_id)) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreInvalidHandle,
        "点在曲线上判断失败：目标曲线不存在", "点在曲线上判断失败");
  }
  CurveService curve_service{state_};
  const auto cp = curve_service.closest_point(curve_id, point);
  if (cp.status != StatusCode::Ok || !cp.value.has_value()) {
    return detail::failed_result<bool>(*state_, StatusCode::OperationFailed,
                                       diag_codes::kGeoClosestPointFailure,
                                       "点在曲线上判断失败：最近点求解不可用",
                                       "点在曲线上判断失败");
  }
  const auto dist = detail::safe_norm(detail::subtract(*cp.value, point));
  const auto tol = detail::resolve_linear_tolerance(tolerance, state_->config.tolerance);
  return ok_result(dist <= tol,
                   state_->create_diagnostic("已完成点在曲线上判断"));
}

Result<std::vector<bool>> PredicateService::point_on_curve_batch(
    std::span<const Point3> points, CurveId curve_id, Scalar tolerance) const {
  if (points.empty()) {
    return detail::invalid_input_result<std::vector<bool>>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "批量点在曲线上判断失败：输入点集合为空", "批量点在曲线上判断失败");
  }
  std::vector<bool> out;
  out.reserve(points.size());
  for (const auto &point : points) {
    const auto single = point_on_curve(point, curve_id, tolerance);
    if (single.status != StatusCode::Ok || !single.value.has_value()) {
      return error_result<std::vector<bool>>(single.status,
                                             single.diagnostic_id);
    }
    out.push_back(*single.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已完成批量点在曲线上判断"));
}

Result<bool> PredicateService::point_on_surface(const Point3 &point,
                                                SurfaceId surface_id,
                                                Scalar tolerance) const {
  if (!detail::finite_point3(point)) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "点在曲面上判断失败：输入点非法", "点在曲面上判断失败");
  }
  if (tolerance < 0.0) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "点在曲面上判断失败：容差不能为负", "点在曲面上判断失败");
  }
  if (!detail::has_surface(*state_, surface_id)) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreInvalidHandle,
        "点在曲面上判断失败：目标曲面不存在", "点在曲面上判断失败");
  }
  SurfaceService surface_service{state_};
  const auto cp = surface_service.closest_point(surface_id, point);
  if (cp.status != StatusCode::Ok || !cp.value.has_value()) {
    return detail::failed_result<bool>(*state_, StatusCode::OperationFailed,
                                       diag_codes::kGeoClosestPointFailure,
                                       "点在曲面上判断失败：最近点求解不可用",
                                       "点在曲面上判断失败");
  }
  const auto dist = detail::safe_norm(detail::subtract(*cp.value, point));
  const auto tol = detail::resolve_linear_tolerance(tolerance, state_->config.tolerance);
  return ok_result(dist <= tol,
                   state_->create_diagnostic("已完成点在曲面上判断"));
}

Result<std::vector<bool>>
PredicateService::point_on_surface_batch(std::span<const Point3> points,
                                         SurfaceId surface_id,
                                         Scalar tolerance) const {
  if (points.empty()) {
    return detail::invalid_input_result<std::vector<bool>>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "批量点在曲面上判断失败：输入点集合为空", "批量点在曲面上判断失败");
  }
  std::vector<bool> out;
  out.reserve(points.size());
  for (const auto &point : points) {
    const auto single = point_on_surface(point, surface_id, tolerance);
    if (single.status != StatusCode::Ok || !single.value.has_value()) {
      return error_result<std::vector<bool>>(single.status,
                                             single.diagnostic_id);
    }
    out.push_back(*single.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已完成批量点在曲面上判断"));
}

Result<bool> PredicateService::point_in_body(const Point3 &point,
                                             BodyId body_id,
                                             Scalar tolerance) const {
  if (!detail::finite_point3(point)) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "点在体内判断失败：输入点非法", "点在体内判断失败");
  }
  if (tolerance < 0.0) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "点在体内判断失败：容差不能为负", "点在体内判断失败");
  }
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreInvalidHandle,
        "点在体内判断失败：目标体不存在", "点在体内判断失败");
  }
  const auto &bbox = it->second.bbox;
  const auto tol = detail::resolve_linear_tolerance(tolerance, state_->config.tolerance);
  const bool inside = detail::point_in_bbox_tol(point, bbox, tol);
  return ok_result(inside, state_->create_diagnostic("已完成点在体内判断"));
}

Result<std::vector<bool>>
PredicateService::point_in_body_batch(std::span<const Point3> points,
                                      BodyId body_id, Scalar tolerance) const {
  if (points.empty()) {
    return detail::invalid_input_result<std::vector<bool>>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "批量点在体内判断失败：输入点集合为空", "批量点在体内判断失败");
  }
  std::vector<bool> out;
  out.reserve(points.size());
  for (const auto &p : points) {
    const auto single = point_in_body(p, body_id, tolerance);
    if (single.status != StatusCode::Ok || !single.value.has_value()) {
      return error_result<std::vector<bool>>(single.status,
                                             single.diagnostic_id);
    }
    out.push_back(*single.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已完成批量点在体内判断"));
}

ToleranceService::ToleranceService(std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

TolerancePolicy ToleranceService::global_policy() const {
  return state_->config.tolerance;
}

TolerancePolicy ToleranceService::policy_for_body(BodyId body_id) const {
  auto policy = state_->config.tolerance;
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return policy;
  }
  const auto &bbox = it->second.bbox;
  if (!detail::valid_bbox(bbox)) {
    return policy;
  }
  const auto model_scale = detail::bbox_characteristic_length(bbox);
  const auto scale = std::clamp(model_scale * 0.01, 0.1, 10.0);
  return detail::scale_tolerance_policy(policy, scale);
}

TolerancePolicy ToleranceService::override_policy(const TolerancePolicy &base,
                                                  Scalar linear) const {
  auto policy = base;
  policy.linear = linear;
  return clamp_policy(policy);
}

TolerancePolicy
ToleranceService::clamp_policy(const TolerancePolicy &base) const {
  return detail::clamp_tolerance_policy(base);
}

TolerancePolicy ToleranceService::scale_policy(const TolerancePolicy &base,
                                               Scalar factor) const {
  return detail::scale_tolerance_policy(base, factor);
}

TolerancePolicy
ToleranceService::scale_policy_for_body_nonlinear(const TolerancePolicy &base,
                                                  BodyId body_id) const {
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end() || !detail::valid_bbox(it->second.bbox)) {
    return clamp_policy(base);
  }
  const auto &bbox = it->second.bbox;
  const auto model_scale = detail::bbox_characteristic_length(bbox);
  const auto factor = std::clamp(std::sqrt(std::max(model_scale, 0.0)) * 0.1, 0.1, 10.0);
  return detail::scale_tolerance_policy(base, factor);
}

TolerancePolicy
ToleranceService::merge_policy(const TolerancePolicy &primary,
                               const TolerancePolicy &fallback) const {
  return detail::merge_tolerance_policy(primary, fallback);
}

TolerancePolicy ToleranceService::with_angular(const TolerancePolicy &base,
                                               Scalar angular) const {
  return detail::tolerance_with_angular(base, angular);
}

TolerancePolicy ToleranceService::loosen_policy(const TolerancePolicy &base,
                                                Scalar factor) const {
  return scale_policy(base, std::max(factor, 1.0));
}

TolerancePolicy ToleranceService::tighten_policy(const TolerancePolicy &base,
                                                 Scalar factor) const {
  return scale_policy(base, 1.0 / std::max(factor, 1.0));
}

TolerancePolicy ToleranceService::choose_body_or_global(BodyId body_id) const {
  if (!detail::has_body(*state_, body_id)) {
    return global_policy();
  }
  return policy_for_body(body_id);
}

Scalar ToleranceService::effective_linear(Scalar requested) const {
  return detail::resolve_linear_tolerance(requested, state_->config.tolerance);
}

Scalar ToleranceService::effective_angular(Scalar requested) const {
  return detail::resolve_angular_tolerance(requested, state_->config.tolerance);
}

Scalar ToleranceService::normalize_linear_request(Scalar requested) const {
  return effective_linear(requested);
}

Scalar ToleranceService::normalize_angular_request(Scalar requested) const {
  return effective_angular(requested);
}

Scalar ToleranceService::resolve_linear_for_scale(Scalar requested,
                                                  Scalar model_scale) const {
  return detail::resolve_linear_tolerance_for_scale(requested, state_->config.tolerance, model_scale);
}

Scalar ToleranceService::resolve_angular_for_scale(Scalar requested,
                                                   Scalar model_scale) const {
  return detail::resolve_angular_tolerance_for_scale(requested, state_->config.tolerance, model_scale);
}

int ToleranceService::compare_linear(Scalar lhs, Scalar rhs,
                                     Scalar tolerance) const {
  if (within_linear(lhs, rhs, tolerance)) {
    return 0;
  }
  return lhs < rhs ? -1 : 1;
}

int ToleranceService::compare_angular(Scalar lhs, Scalar rhs,
                                      Scalar tolerance) const {
  if (within_angular(lhs, rhs, tolerance)) {
    return 0;
  }
  return lhs < rhs ? -1 : 1;
}

bool ToleranceService::within_linear(Scalar lhs, Scalar rhs,
                                     Scalar tolerance) const {
  return detail::within_tolerance(lhs, rhs, effective_linear(tolerance));
}

bool ToleranceService::within_angular(Scalar lhs, Scalar rhs,
                                      Scalar tolerance) const {
  return detail::within_tolerance(lhs, rhs, effective_angular(tolerance));
}

bool ToleranceService::is_valid_policy(const TolerancePolicy &policy) const {
  return detail::valid_tolerance_policy(policy);
}

} // namespace axiom
