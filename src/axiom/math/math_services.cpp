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
  if (!detail::finite_vec3(lhs) || !detail::finite_vec3(rhs)) {
    return std::numeric_limits<Scalar>::quiet_NaN();
  }
  const long double s =
      static_cast<long double>(lhs.x) * static_cast<long double>(rhs.x) +
      static_cast<long double>(lhs.y) * static_cast<long double>(rhs.y) +
      static_cast<long double>(lhs.z) * static_cast<long double>(rhs.z);
  if (!std::isfinite(static_cast<double>(s))) {
    return std::numeric_limits<Scalar>::quiet_NaN();
  }
  return static_cast<Scalar>(s);
}

Vec3 LinearAlgebraService::cross(const Vec3 &lhs, const Vec3 &rhs) const {
  const auto nan_v = Vec3{std::numeric_limits<Scalar>::quiet_NaN(),
                          std::numeric_limits<Scalar>::quiet_NaN(),
                          std::numeric_limits<Scalar>::quiet_NaN()};
  if (!detail::finite_vec3(lhs) || !detail::finite_vec3(rhs)) {
    return nan_v;
  }
  const long double lx = static_cast<long double>(lhs.x);
  const long double ly = static_cast<long double>(lhs.y);
  const long double lz = static_cast<long double>(lhs.z);
  const long double rx = static_cast<long double>(rhs.x);
  const long double ry = static_cast<long double>(rhs.y);
  const long double rz = static_cast<long double>(rhs.z);
  const long double ox = ly * rz - lz * ry;
  const long double oy = lz * rx - lx * rz;
  const long double oz = lx * ry - ly * rx;
  if (!std::isfinite(static_cast<double>(ox)) || !std::isfinite(static_cast<double>(oy)) ||
      !std::isfinite(static_cast<double>(oz))) {
    return nan_v;
  }
  return Vec3{static_cast<Scalar>(ox), static_cast<Scalar>(oy), static_cast<Scalar>(oz)};
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
  const auto s2 = squared_norm(value);
  if (!std::isfinite(s2) || s2 < 0.0) {
    return std::numeric_limits<Scalar>::infinity();
  }
  return std::sqrt(s2);
}

Scalar LinearAlgebraService::squared_norm(const Vec3 &value) const {
  if (!detail::finite_vec3(value)) {
    return std::numeric_limits<Scalar>::infinity();
  }
  // 用 long double 累加平方，降低大尺度坐标下的中间溢出相对 `x*x+y*y+z*z` 直接按 double 累加的风险。
  const long double sx = static_cast<long double>(value.x);
  const long double sy = static_cast<long double>(value.y);
  const long double sz = static_cast<long double>(value.z);
  const long double s2 = sx * sx + sy * sy + sz * sz;
  if (!std::isfinite(static_cast<double>(s2))) {
    return std::numeric_limits<Scalar>::infinity();
  }
  return static_cast<Scalar>(s2);
}

Vec3 LinearAlgebraService::normalize(const Vec3 &value) const {
  const auto nan_v = Vec3{std::numeric_limits<Scalar>::quiet_NaN(),
                          std::numeric_limits<Scalar>::quiet_NaN(),
                          std::numeric_limits<Scalar>::quiet_NaN()};
  if (!detail::finite_vec3(value)) {
    return nan_v;
  }
  const auto s2 = squared_norm(value);
  if (!std::isfinite(s2)) {
    return nan_v;
  }
  if (s2 <= 0.0) {
    return Vec3{0.0, 0.0, 0.0};
  }
  return scale(value, 1.0 / std::sqrt(s2));
}

Scalar LinearAlgebraService::distance(const Point3 &lhs,
                                      const Point3 &rhs) const {
  const auto s2 = squared_distance(lhs, rhs);
  if (!std::isfinite(s2) || s2 < 0.0) {
    return std::numeric_limits<Scalar>::infinity();
  }
  return std::sqrt(s2);
}

Scalar LinearAlgebraService::squared_distance(const Point3 &lhs,
                                              const Point3 &rhs) const {
  if (!detail::finite_point3(lhs) || !detail::finite_point3(rhs)) {
    return std::numeric_limits<Scalar>::infinity();
  }
  const long double dx =
      static_cast<long double>(lhs.x) - static_cast<long double>(rhs.x);
  const long double dy =
      static_cast<long double>(lhs.y) - static_cast<long double>(rhs.y);
  const long double dz =
      static_cast<long double>(lhs.z) - static_cast<long double>(rhs.z);
  const long double s2 = dx * dx + dy * dy + dz * dz;
  if (!std::isfinite(static_cast<double>(s2))) {
    return std::numeric_limits<Scalar>::infinity();
  }
  return static_cast<Scalar>(s2);
}

Scalar LinearAlgebraService::manhattan_distance(const Point3 &lhs,
                                                const Point3 &rhs) const {
  if (!detail::finite_point3(lhs) || !detail::finite_point3(rhs)) {
    return std::numeric_limits<Scalar>::infinity();
  }
  const long double sx =
      std::abs(static_cast<long double>(lhs.x) - static_cast<long double>(rhs.x));
  const long double sy =
      std::abs(static_cast<long double>(lhs.y) - static_cast<long double>(rhs.y));
  const long double sz =
      std::abs(static_cast<long double>(lhs.z) - static_cast<long double>(rhs.z));
  const long double s = sx + sy + sz;
  if (!std::isfinite(static_cast<double>(s))) {
    return std::numeric_limits<Scalar>::infinity();
  }
  return static_cast<Scalar>(s);
}

Point3 LinearAlgebraService::midpoint(const Point3 &lhs,
                                      const Point3 &rhs) const {
  const auto nan_p =
      Point3{std::numeric_limits<Scalar>::quiet_NaN(),
             std::numeric_limits<Scalar>::quiet_NaN(),
             std::numeric_limits<Scalar>::quiet_NaN()};
  if (!detail::finite_point3(lhs) || !detail::finite_point3(rhs)) {
    return nan_p;
  }
  return Point3{(lhs.x + rhs.x) * 0.5, (lhs.y + rhs.y) * 0.5,
                (lhs.z + rhs.z) * 0.5};
}

Point3 LinearAlgebraService::lerp(const Point3 &lhs, const Point3 &rhs,
                                  Scalar t) const {
  const auto nan_p =
      Point3{std::numeric_limits<Scalar>::quiet_NaN(),
             std::numeric_limits<Scalar>::quiet_NaN(),
             std::numeric_limits<Scalar>::quiet_NaN()};
  if (!detail::finite_point3(lhs) || !detail::finite_point3(rhs) ||
      !std::isfinite(t)) {
    return nan_p;
  }
  return Point3{lhs.x + (rhs.x - lhs.x) * t, lhs.y + (rhs.y - lhs.y) * t,
                lhs.z + (rhs.z - lhs.z) * t};
}

Scalar LinearAlgebraService::angle_between(const Vec3 &lhs,
                                           const Vec3 &rhs) const {
  if (!detail::finite_vec3(lhs) || !detail::finite_vec3(rhs)) {
    return std::numeric_limits<Scalar>::quiet_NaN();
  }
  return detail::vec_angle_between(lhs, rhs);
}

Scalar LinearAlgebraService::scalar_triple_product(const Vec3 &a, const Vec3 &b,
                                                   const Vec3 &c) const {
  if (!detail::finite_vec3(a) || !detail::finite_vec3(b) || !detail::finite_vec3(c)) {
    return std::numeric_limits<Scalar>::quiet_NaN();
  }
  const long double ax = static_cast<long double>(a.x);
  const long double ay = static_cast<long double>(a.y);
  const long double az = static_cast<long double>(a.z);
  const long double bx = static_cast<long double>(b.x);
  const long double by = static_cast<long double>(b.y);
  const long double bz = static_cast<long double>(b.z);
  const long double cx = static_cast<long double>(c.x);
  const long double cy = static_cast<long double>(c.y);
  const long double cz = static_cast<long double>(c.z);
  const long double rx = ay * bz - az * by;
  const long double ry = az * bx - ax * bz;
  const long double rz = ax * by - ay * bx;
  const long double det = rx * cx + ry * cy + rz * cz;
  if (!std::isfinite(static_cast<double>(det))) {
    return std::numeric_limits<Scalar>::quiet_NaN();
  }
  return static_cast<Scalar>(det);
}

Scalar
LinearAlgebraService::distance_point_to_line(const Point3 &point,
                                             const Point3 &line_origin,
                                             const Vec3 &line_direction) const {
  if (!detail::finite_point3(point) || !detail::finite_point3(line_origin) ||
      !detail::finite_vec3(line_direction)) {
    return std::numeric_limits<Scalar>::infinity();
  }
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
  if (!detail::finite_point3(point) || !detail::finite_point3(plane_origin) ||
      !detail::finite_vec3(plane_normal)) {
    return std::numeric_limits<Scalar>::infinity();
  }
  const auto n_norm = detail::safe_norm(plane_normal);
  if (n_norm <= 0.0) {
    return std::numeric_limits<Scalar>::infinity();
  }
  const auto diff = Vec3{point.x - plane_origin.x, point.y - plane_origin.y,
                         point.z - plane_origin.z};
  const auto d = dot(diff, plane_normal);
  if (!std::isfinite(d)) {
    return std::numeric_limits<Scalar>::infinity();
  }
  return std::abs(d) / n_norm;
}

Scalar LinearAlgebraService::distance_point_to_segment(const Point3 &point,
                                                       const Point3 &seg_a,
                                                       const Point3 &seg_b) const {
  const auto s2 = detail::squared_distance_point_to_segment(point, seg_a, seg_b);
  if (!std::isfinite(s2) || s2 < 0.0) {
    return std::numeric_limits<Scalar>::infinity();
  }
  return static_cast<Scalar>(std::sqrt(static_cast<long double>(s2)));
}

Scalar LinearAlgebraService::triangle_area(const Point3 &a, const Point3 &b,
                                           const Point3 &c) const {
  if (!detail::finite_point3(a) || !detail::finite_point3(b) ||
      !detail::finite_point3(c)) {
    return std::numeric_limits<Scalar>::quiet_NaN();
  }
  const long double abx = static_cast<long double>(b.x) - static_cast<long double>(a.x);
  const long double aby = static_cast<long double>(b.y) - static_cast<long double>(a.y);
  const long double abz = static_cast<long double>(b.z) - static_cast<long double>(a.z);
  const long double acx = static_cast<long double>(c.x) - static_cast<long double>(a.x);
  const long double acy = static_cast<long double>(c.y) - static_cast<long double>(a.y);
  const long double acz = static_cast<long double>(c.z) - static_cast<long double>(a.z);
  const long double rx = aby * acz - abz * acy;
  const long double ry = abz * acx - abx * acz;
  const long double rz = abx * acy - aby * acx;
  const long double cross_mag =
      std::sqrt(rx * rx + ry * ry + rz * rz);
  if (!std::isfinite(static_cast<double>(cross_mag))) {
    return std::numeric_limits<Scalar>::quiet_NaN();
  }
  return static_cast<Scalar>(0.5L * cross_mag);
}

Scalar LinearAlgebraService::tetrahedron_signed_volume(const Point3 &a,
                                                       const Point3 &b,
                                                       const Point3 &c,
                                                       const Point3 &d) const {
  if (!detail::finite_point3(a) || !detail::finite_point3(b) ||
      !detail::finite_point3(c) || !detail::finite_point3(d)) {
    return std::numeric_limits<Scalar>::quiet_NaN();
  }
  const long double abx = static_cast<long double>(b.x) - static_cast<long double>(a.x);
  const long double aby = static_cast<long double>(b.y) - static_cast<long double>(a.y);
  const long double abz = static_cast<long double>(b.z) - static_cast<long double>(a.z);
  const long double acx = static_cast<long double>(c.x) - static_cast<long double>(a.x);
  const long double acy = static_cast<long double>(c.y) - static_cast<long double>(a.y);
  const long double acz = static_cast<long double>(c.z) - static_cast<long double>(a.z);
  const long double adx = static_cast<long double>(d.x) - static_cast<long double>(a.x);
  const long double ady = static_cast<long double>(d.y) - static_cast<long double>(a.y);
  const long double adz = static_cast<long double>(d.z) - static_cast<long double>(a.z);
  const long double cx = aby * acz - abz * acy;
  const long double cy = abz * acx - abx * acz;
  const long double cz = abx * acy - aby * acx;
  const long double det = cx * adx + cy * ady + cz * adz;
  if (!std::isfinite(static_cast<double>(det))) {
    return std::numeric_limits<Scalar>::quiet_NaN();
  }
  const long double v = det / 6.0L;
  if (!std::isfinite(static_cast<double>(v))) {
    return std::numeric_limits<Scalar>::quiet_NaN();
  }
  return static_cast<Scalar>(v);
}

Vec3 LinearAlgebraService::project(const Vec3 &lhs, const Vec3 &rhs) const {
  const auto nan_v = Vec3{std::numeric_limits<Scalar>::quiet_NaN(),
                          std::numeric_limits<Scalar>::quiet_NaN(),
                          std::numeric_limits<Scalar>::quiet_NaN()};
  if (!detail::finite_vec3(lhs) || !detail::finite_vec3(rhs)) {
    return nan_v;
  }
  const long double rrx = static_cast<long double>(rhs.x);
  const long double rry = static_cast<long double>(rhs.y);
  const long double rrz = static_cast<long double>(rhs.z);
  const long double lrx = static_cast<long double>(lhs.x);
  const long double lry = static_cast<long double>(lhs.y);
  const long double lrz = static_cast<long double>(lhs.z);
  const long double denom = rrx * rrx + rry * rry + rrz * rrz;
  if (!std::isfinite(static_cast<double>(denom)) ||
      denom <= static_cast<long double>(std::numeric_limits<Scalar>::epsilon())) {
    return Vec3{0.0, 0.0, 0.0};
  }
  const long double num = lrx * rrx + lry * rry + lrz * rrz;
  if (!std::isfinite(static_cast<double>(num))) {
    return nan_v;
  }
  const long double fac = num / denom;
  if (!std::isfinite(static_cast<double>(fac))) {
    return nan_v;
  }
  const auto out = scale(rhs, static_cast<Scalar>(fac));
  if (!detail::finite_vec3(out)) {
    return nan_v;
  }
  return out;
}

Vec3 LinearAlgebraService::reject(const Vec3 &lhs, const Vec3 &rhs) const {
  const auto nan_v = Vec3{std::numeric_limits<Scalar>::quiet_NaN(),
                          std::numeric_limits<Scalar>::quiet_NaN(),
                          std::numeric_limits<Scalar>::quiet_NaN()};
  if (!detail::finite_vec3(lhs) || !detail::finite_vec3(rhs)) {
    return nan_v;
  }
  const auto p = project(lhs, rhs);
  if (!detail::finite_vec3(p)) {
    return nan_v;
  }
  return subtract(lhs, p);
}

Vec3 LinearAlgebraService::clamp_norm(const Vec3 &value,
                                      Scalar max_norm) const {
  const auto nan_v = Vec3{std::numeric_limits<Scalar>::quiet_NaN(),
                          std::numeric_limits<Scalar>::quiet_NaN(),
                          std::numeric_limits<Scalar>::quiet_NaN()};
  if (!detail::finite_vec3(value) || !std::isfinite(max_norm)) {
    return nan_v;
  }
  const auto safe_max = std::max(max_norm, 0.0);
  const auto value_norm = detail::safe_norm(value);
  if (value_norm <= safe_max || value_norm <= 0.0) {
    return value;
  }
  return scale(value, safe_max / value_norm);
}

std::optional<std::pair<Vec3, Vec3>>
LinearAlgebraService::orthonormal_basis(const Vec3 &normal) const {
  if (!detail::finite_vec3(normal)) {
    return std::nullopt;
  }
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
  if (!detail::finite_vec3(value) || !std::isfinite(eps)) {
    return false;
  }
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
  const Point3 nan_p{std::numeric_limits<Scalar>::quiet_NaN(),
                     std::numeric_limits<Scalar>::quiet_NaN(),
                     std::numeric_limits<Scalar>::quiet_NaN()};
  if (!detail::finite_point3(point)) {
    return nan_p;
  }
  for (int k = 0; k < 12; ++k) {
    if (!std::isfinite(transform.m[k])) {
      return nan_p;
    }
  }
  const long double px = static_cast<long double>(point.x);
  const long double py = static_cast<long double>(point.y);
  const long double pz = static_cast<long double>(point.z);
  const long double ox =
      static_cast<long double>(transform.m[0]) * px +
      static_cast<long double>(transform.m[1]) * py +
      static_cast<long double>(transform.m[2]) * pz +
      static_cast<long double>(transform.m[3]);
  const long double oy =
      static_cast<long double>(transform.m[4]) * px +
      static_cast<long double>(transform.m[5]) * py +
      static_cast<long double>(transform.m[6]) * pz +
      static_cast<long double>(transform.m[7]);
  const long double oz =
      static_cast<long double>(transform.m[8]) * px +
      static_cast<long double>(transform.m[9]) * py +
      static_cast<long double>(transform.m[10]) * pz +
      static_cast<long double>(transform.m[11]);
  if (!std::isfinite(static_cast<double>(ox)) || !std::isfinite(static_cast<double>(oy)) ||
      !std::isfinite(static_cast<double>(oz))) {
    return nan_p;
  }
  return Point3{static_cast<Scalar>(ox), static_cast<Scalar>(oy), static_cast<Scalar>(oz)};
}

Vec3 LinearAlgebraService::transform(const Vec3 &vector,
                                     const Transform3 &transform) const {
  const Vec3 nan_v{std::numeric_limits<Scalar>::quiet_NaN(),
                   std::numeric_limits<Scalar>::quiet_NaN(),
                   std::numeric_limits<Scalar>::quiet_NaN()};
  if (!detail::finite_vec3(vector)) {
    return nan_v;
  }
  for (int k = 0; k < 12; ++k) {
    if (!std::isfinite(transform.m[k])) {
      return nan_v;
    }
  }
  const long double vx = static_cast<long double>(vector.x);
  const long double vy = static_cast<long double>(vector.y);
  const long double vz = static_cast<long double>(vector.z);
  const long double ox = static_cast<long double>(transform.m[0]) * vx +
                         static_cast<long double>(transform.m[1]) * vy +
                         static_cast<long double>(transform.m[2]) * vz;
  const long double oy = static_cast<long double>(transform.m[4]) * vx +
                         static_cast<long double>(transform.m[5]) * vy +
                         static_cast<long double>(transform.m[6]) * vz;
  const long double oz = static_cast<long double>(transform.m[8]) * vx +
                         static_cast<long double>(transform.m[9]) * vy +
                         static_cast<long double>(transform.m[10]) * vz;
  if (!std::isfinite(static_cast<double>(ox)) || !std::isfinite(static_cast<double>(oy)) ||
      !std::isfinite(static_cast<double>(oz))) {
    return nan_v;
  }
  return Vec3{static_cast<Scalar>(ox), static_cast<Scalar>(oy), static_cast<Scalar>(oz)};
}

Point3 LinearAlgebraService::centroid(std::span<const Point3> points) const {
  if (points.empty()) {
    return Point3{0.0, 0.0, 0.0};
  }
  const auto nan_p =
      Point3{std::numeric_limits<Scalar>::quiet_NaN(),
             std::numeric_limits<Scalar>::quiet_NaN(),
             std::numeric_limits<Scalar>::quiet_NaN()};
  long double sx = 0.0L, sy = 0.0L, sz = 0.0L;
  for (const auto &p : points) {
    if (!detail::finite_point3(p)) {
      return nan_p;
    }
    sx += static_cast<long double>(p.x);
    sy += static_cast<long double>(p.y);
    sz += static_cast<long double>(p.z);
  }
  const long double inv = 1.0L / static_cast<long double>(points.size());
  const long double cx = sx * inv;
  const long double cy = sy * inv;
  const long double cz = sz * inv;
  if (!std::isfinite(static_cast<double>(cx)) || !std::isfinite(static_cast<double>(cy)) ||
      !std::isfinite(static_cast<double>(cz))) {
    return nan_p;
  }
  return Point3{static_cast<Scalar>(cx), static_cast<Scalar>(cy), static_cast<Scalar>(cz)};
}

Vec3 LinearAlgebraService::average(std::span<const Vec3> vectors) const {
  if (vectors.empty()) {
    return Vec3{0.0, 0.0, 0.0};
  }
  const auto nan_v = Vec3{std::numeric_limits<Scalar>::quiet_NaN(),
                          std::numeric_limits<Scalar>::quiet_NaN(),
                          std::numeric_limits<Scalar>::quiet_NaN()};
  long double sx = 0.0L, sy = 0.0L, sz = 0.0L;
  for (const auto &v : vectors) {
    if (!detail::finite_vec3(v)) {
      return nan_v;
    }
    sx += static_cast<long double>(v.x);
    sy += static_cast<long double>(v.y);
    sz += static_cast<long double>(v.z);
  }
  const long double inv = 1.0L / static_cast<long double>(vectors.size());
  const long double ax = sx * inv;
  const long double ay = sy * inv;
  const long double az = sz * inv;
  if (!std::isfinite(static_cast<double>(ax)) || !std::isfinite(static_cast<double>(ay)) ||
      !std::isfinite(static_cast<double>(az))) {
    return nan_v;
  }
  return Vec3{static_cast<Scalar>(ax), static_cast<Scalar>(ay), static_cast<Scalar>(az)};
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
  if (!std::isfinite(eps)) {
    return Sign::Uncertain;
  }
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
  if (!std::isfinite(eps)) {
    return Sign::Uncertain;
  }
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

Sign PredicateService::orient2d_effective(const Point2 &a, const Point2 &b, const Point2 &c,
                                          Scalar tolerance_requested) const {
  if (!std::isfinite(tolerance_requested)) {
    return Sign::Uncertain;
  }
  const Scalar eps = detail::resolve_linear_tolerance(tolerance_requested,
                                                      state_->config.tolerance);
  return orient2d_tol(a, b, c, eps);
}

Sign PredicateService::orient3d_effective(const Point3 &a, const Point3 &b, const Point3 &c,
                                            const Point3 &d, Scalar tolerance_requested) const {
  if (!std::isfinite(tolerance_requested)) {
    return Sign::Uncertain;
  }
  const Scalar eps = detail::resolve_linear_tolerance(tolerance_requested,
                                                      state_->config.tolerance);
  return orient3d_tol(a, b, c, d, eps);
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

bool PredicateService::point_equal_effective(const Point3 &lhs,
                                           const Point3 &rhs,
                                           Scalar tolerance_requested) const {
  if (!std::isfinite(tolerance_requested)) {
    return false;
  }
  const Scalar tol = detail::resolve_linear_tolerance(
      tolerance_requested, state_->config.tolerance);
  return detail::point_equal_tol(lhs, rhs, tol);
}

bool PredicateService::point_on_segment_tol(const Point3 &point, const Point3 &seg_a,
                                            const Point3 &seg_b,
                                            Scalar tolerance) const {
  return detail::point_on_segment_tol(point, seg_a, seg_b, tolerance);
}

bool PredicateService::point_on_segment_effective(const Point3 &point, const Point3 &seg_a,
                                                  const Point3 &seg_b,
                                                  Scalar tolerance_requested) const {
  if (!std::isfinite(tolerance_requested)) {
    return false;
  }
  const Scalar tol =
      detail::resolve_linear_tolerance(tolerance_requested, state_->config.tolerance);
  return detail::point_on_segment_tol(point, seg_a, seg_b, tol);
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
  if (!bbox_valid(lhs) || !bbox_valid(rhs) || !std::isfinite(tolerance)) {
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
  if (!std::isfinite(tolerance)) {
    return 0.0;
  }
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
  if (!std::isfinite(lhs.min) || !std::isfinite(lhs.max) || !std::isfinite(rhs.min) ||
      !std::isfinite(rhs.max) || !std::isfinite(tolerance)) {
    return false;
  }
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
  if (!std::isfinite(tolerance)) {
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
  if (!std::isfinite(tolerance)) {
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

bool PredicateService::vec_parallel_effective(const Vec3 &lhs, const Vec3 &rhs,
                                            Scalar angular_requested) const {
  if (!std::isfinite(angular_requested)) {
    return false;
  }
  const Scalar tol = detail::resolve_angular_tolerance(
      angular_requested, state_->config.tolerance);
  return detail::vec_parallel_tol(lhs, rhs, tol);
}

bool PredicateService::vec_orthogonal_effective(
    const Vec3 &lhs, const Vec3 &rhs, Scalar angular_requested) const {
  if (!std::isfinite(angular_requested)) {
    return false;
  }
  const Scalar tol = detail::resolve_angular_tolerance(
      angular_requested, state_->config.tolerance);
  return detail::vec_orthogonal_tol(lhs, rhs, tol);
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
  if (!std::isfinite(lhs) || !std::isfinite(rhs) ||
      !std::isfinite(tolerance)) {
    return 0;
  }
  if (within_linear(lhs, rhs, tolerance)) {
    return 0;
  }
  return lhs < rhs ? -1 : 1;
}

int ToleranceService::compare_angular(Scalar lhs, Scalar rhs,
                                      Scalar tolerance) const {
  if (!std::isfinite(lhs) || !std::isfinite(rhs) ||
      !std::isfinite(tolerance)) {
    return 0;
  }
  if (within_angular(lhs, rhs, tolerance)) {
    return 0;
  }
  return lhs < rhs ? -1 : 1;
}

bool ToleranceService::within_linear(Scalar lhs, Scalar rhs,
                                     Scalar tolerance) const {
  if (!std::isfinite(lhs) || !std::isfinite(rhs) ||
      !std::isfinite(tolerance)) {
    return false;
  }
  const Scalar tol = effective_linear(tolerance);
  if (!std::isfinite(tol)) {
    return false;
  }
  return detail::within_tolerance(lhs, rhs, tol);
}

bool ToleranceService::within_angular(Scalar lhs, Scalar rhs,
                                      Scalar tolerance) const {
  if (!std::isfinite(lhs) || !std::isfinite(rhs) ||
      !std::isfinite(tolerance)) {
    return false;
  }
  const Scalar tol = effective_angular(tolerance);
  if (!std::isfinite(tol)) {
    return false;
  }
  return detail::within_tolerance(lhs, rhs, tol);
}

bool ToleranceService::nearly_equal_linear(Scalar lhs, Scalar rhs,
                                         Scalar abs_requested,
                                         Scalar rel_requested) const {
  if (!std::isfinite(lhs) || !std::isfinite(rhs) ||
      !std::isfinite(abs_requested) || !std::isfinite(rel_requested)) {
    return false;
  }
  const Scalar abs_tol = effective_linear(abs_requested);
  const Scalar rel_tol = std::max(rel_requested, 0.0);
  if (!std::isfinite(abs_tol)) {
    return false;
  }
  return detail::nearly_equal_rel_abs(lhs, rhs, abs_tol, rel_tol);
}

int ToleranceService::compare_linear_rel_abs(Scalar lhs, Scalar rhs,
                                             Scalar abs_requested,
                                             Scalar rel_requested) const {
  if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
    return 0;
  }
  if (nearly_equal_linear(lhs, rhs, abs_requested, rel_requested)) {
    return 0;
  }
  return lhs < rhs ? -1 : 1;
}

bool ToleranceService::nearly_equal_angular(Scalar lhs, Scalar rhs,
                                          Scalar abs_requested,
                                          Scalar rel_requested) const {
  if (!std::isfinite(lhs) || !std::isfinite(rhs) ||
      !std::isfinite(abs_requested) || !std::isfinite(rel_requested)) {
    return false;
  }
  const Scalar abs_tol = effective_angular(abs_requested);
  const Scalar rel_tol = std::max(rel_requested, 0.0);
  if (!std::isfinite(abs_tol)) {
    return false;
  }
  return detail::nearly_equal_rel_abs(lhs, rhs, abs_tol, rel_tol);
}

int ToleranceService::compare_angular_rel_abs(Scalar lhs, Scalar rhs,
                                              Scalar abs_requested,
                                              Scalar rel_requested) const {
  if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
    return 0;
  }
  if (nearly_equal_angular(lhs, rhs, abs_requested, rel_requested)) {
    return 0;
  }
  return lhs < rhs ? -1 : 1;
}

bool ToleranceService::is_valid_policy(const TolerancePolicy &policy) const {
  return detail::valid_tolerance_policy(policy);
}

} // namespace axiom
