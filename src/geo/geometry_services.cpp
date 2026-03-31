#include "axiom/geo/geometry_services.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <sstream>

#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"

namespace axiom {

namespace {

constexpr Scalar kEpsilon = 1e-12;
constexpr int kClosestParamRefineIters = 8;

bool finite_scalar(Scalar value) { return std::isfinite(value); }

bool finite_point(const Point3 &value) {
  return finite_scalar(value.x) && finite_scalar(value.y) &&
         finite_scalar(value.z);
}

bool finite_vec(const Vec3 &value) {
  return finite_scalar(value.x) && finite_scalar(value.y) &&
         finite_scalar(value.z);
}

bool finite_point2(const Point2 &value) {
  return finite_scalar(value.x) && finite_scalar(value.y);
}

Point2 lerp_point2(const Point2 &a, const Point2 &b, Scalar t) {
  return Point2{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

Vec2 subtract2(const Point2 &a, const Point2 &b) {
  return Vec2{a.x - b.x, a.y - b.y};
}

Scalar dot2(const Vec2 &a, const Vec2 &b) { return a.x * b.x + a.y * b.y; }

Scalar norm2(const Vec2 &a) { return std::sqrt(dot2(a, a)); }

Vec2 normalize2_or_default(const Vec2 &a, const Vec2 &fallback) {
  const auto n = norm2(a);
  if (!std::isfinite(n) || n <= kEpsilon) {
    return fallback;
  }
  return Vec2{a.x / n, a.y / n};
}

Scalar normalize_angle_0_2pi(Scalar angle) {
  const auto two_pi = std::acos(-1.0) * 2.0;
  if (!std::isfinite(angle) || two_pi <= 0.0) {
    return 0.0;
  }
  // Map to [0, 2pi)
  auto out = std::fmod(angle, two_pi);
  if (out < 0.0) {
    out += two_pi;
  }
  // Avoid returning exactly 2pi due to numerical noise.
  if (out >= two_pi) {
    out = 0.0;
  }
  return out;
}

Range1D pcurve_domain(const detail::PCurveRecord &pc) {
  switch (pc.kind) {
  case detail::PCurveKind::Polyline:
  default:
    return Range1D{0.0, pc.poles.size() > 1
                            ? static_cast<Scalar>(pc.poles.size() - 1)
                            : 1.0};
  }
}

Point2 evaluate_pcurve_point(const detail::PCurveRecord &pc, Scalar t) {
  if (pc.poles.empty()) {
    return Point2{0.0, 0.0};
  }
  if (pc.poles.size() == 1) {
    return pc.poles.front();
  }
  const auto domain = pcurve_domain(pc);
  const auto ct = std::clamp(t, domain.min, domain.max);
  const auto seg = std::min<std::size_t>(
      static_cast<std::size_t>(std::floor(ct)), pc.poles.size() - 2);
  const auto local_t = ct - static_cast<Scalar>(seg);
  return lerp_point2(pc.poles[seg], pc.poles[seg + 1], local_t);
}

Scalar approximate_pcurve_parameter(const detail::PCurveRecord &pc,
                                   const Point2 &point) {
  const auto domain = pcurve_domain(pc);
  const int samples = 64;
  Scalar best_t = domain.min;
  Scalar best_distance = std::numeric_limits<Scalar>::max();
  for (int i = 0; i <= samples; ++i) {
    const auto alpha = static_cast<Scalar>(i) / static_cast<Scalar>(samples);
    const auto t = domain.min + (domain.max - domain.min) * alpha;
    const auto sample = evaluate_pcurve_point(pc, t);
    const auto d = norm2(subtract2(point, sample));
    if (d < best_distance) {
      best_distance = d;
      best_t = t;
    }
  }
  const auto radius = (domain.max - domain.min) * 0.125;
  for (int iter = 0; iter < kClosestParamRefineIters; ++iter) {
    const auto step = radius / static_cast<Scalar>(1u << iter);
    const auto left_t = std::clamp(best_t - step, domain.min, domain.max);
    const auto right_t = std::clamp(best_t + step, domain.min, domain.max);
    const auto dl = norm2(subtract2(point, evaluate_pcurve_point(pc, left_t)));
    const auto dr = norm2(subtract2(point, evaluate_pcurve_point(pc, right_t)));
    if (dl < best_distance || dr < best_distance) {
      if (dl <= dr) {
        best_t = left_t;
        best_distance = dl;
      } else {
        best_t = right_t;
        best_distance = dr;
      }
    }
  }
  return best_t;
}

std::vector<Scalar> uniform_knot_vector(std::size_t count) {
  if (count <= 1) {
    return {0.0, 1.0};
  }
  std::vector<Scalar> knots(count);
  const auto inv = 1.0 / static_cast<Scalar>(count - 1);
  for (std::size_t i = 0; i < count; ++i) {
    knots[i] = static_cast<Scalar>(i) * inv;
  }
  return knots;
}

std::vector<Scalar> normalized_positive_weights(const std::vector<Scalar> &weights,
                                                std::size_t pole_count,
                                                bool &ok) {
  ok = true;
  if (weights.empty()) {
    return std::vector<Scalar>(pole_count, 1.0 / std::max<std::size_t>(1, pole_count));
  }
  if (weights.size() != pole_count) {
    ok = false;
    return {};
  }
  Scalar sum = 0.0;
  for (const auto w : weights) {
    if (!finite_scalar(w) || w <= 0.0) {
      ok = false;
      return {};
    }
    sum += w;
  }
  if (sum <= kEpsilon) {
    ok = false;
    return {};
  }
  std::vector<Scalar> out = weights;
  for (auto &w : out) {
    w /= sum;
  }
  return out;
}

std::string curve_eval_cache_key(CurveId curve_id, Scalar t, int deriv_order) {
  std::ostringstream oss;
  oss.precision(17);
  oss << curve_id.value << "|" << t << "|" << deriv_order;
  return oss.str();
}

std::string surface_eval_cache_key(SurfaceId surface_id, Scalar u, Scalar v,
                                   int deriv_order) {
  std::ostringstream oss;
  oss.precision(17);
  oss << surface_id.value << "|" << u << "|" << v << "|" << deriv_order;
  return oss.str();
}

struct OrthoFrame {
  Vec3 u{1.0, 0.0, 0.0};
  Vec3 v{0.0, 1.0, 0.0};
  Vec3 w{0.0, 0.0, 1.0};
};

OrthoFrame make_frame_from_axis(const Vec3 &axis) {
  const auto w = detail::norm(axis) > kEpsilon ? detail::normalize(axis)
                                               : Vec3{0.0, 0.0, 1.0};
  const auto ref =
      std::abs(w.z) < 0.9 ? Vec3{0.0, 0.0, 1.0} : Vec3{0.0, 1.0, 0.0};
  const auto u = detail::normalize(detail::cross(ref, w));
  const auto v = detail::normalize(detail::cross(w, u));
  return OrthoFrame{u, v, w};
}

Point3 point_from_local(const Point3 &origin, const OrthoFrame &frame, Scalar x,
                        Scalar y, Scalar z) {
  return Point3{origin.x + frame.u.x * x + frame.v.x * y + frame.w.x * z,
                origin.y + frame.u.y * x + frame.v.y * y + frame.w.y * z,
                origin.z + frame.u.z * x + frame.v.z * y + frame.w.z * z};
}

Vec3 vec_from_local(const OrthoFrame &frame, Scalar x, Scalar y, Scalar z) {
  return Vec3{frame.u.x * x + frame.v.x * y + frame.w.x * z,
              frame.u.y * x + frame.v.y * y + frame.w.y * z,
              frame.u.z * x + frame.v.z * y + frame.w.z * z};
}

Point3 transform_point(const Point3 &point, const Transform3 &transform) {
  return Point3{transform.m[0] * point.x + transform.m[1] * point.y +
                    transform.m[2] * point.z + transform.m[3],
                transform.m[4] * point.x + transform.m[5] * point.y +
                    transform.m[6] * point.z + transform.m[7],
                transform.m[8] * point.x + transform.m[9] * point.y +
                    transform.m[10] * point.z + transform.m[11]};
}

Vec3 transform_vec(const Vec3 &vector, const Transform3 &transform) {
  return Vec3{transform.m[0] * vector.x + transform.m[1] * vector.y +
                  transform.m[2] * vector.z,
              transform.m[4] * vector.x + transform.m[5] * vector.y +
                  transform.m[6] * vector.z,
              transform.m[8] * vector.x + transform.m[9] * vector.y +
                  transform.m[10] * vector.z};
}

std::array<Scalar, 3> to_local(const Vec3 &vec, const OrthoFrame &frame) {
  return {detail::dot(vec, frame.u), detail::dot(vec, frame.v),
          detail::dot(vec, frame.w)};
}

Vec3 normalize_or_default(const Vec3 &vec, const Vec3 &fallback) {
  return detail::norm(vec) > kEpsilon ? detail::normalize(vec) : fallback;
}

BoundingBox bbox_from_span_vectors(const Point3 &origin, const Vec3 &axis_u,
                                   const Vec3 &axis_v) {
  const auto ex =
      std::sqrt(axis_u.x * axis_u.x + axis_v.x * axis_v.x);
  const auto ey =
      std::sqrt(axis_u.y * axis_u.y + axis_v.y * axis_v.y);
  const auto ez =
      std::sqrt(axis_u.z * axis_u.z + axis_v.z * axis_v.z);
  return detail::bbox_from_center_radius(origin, ex, ey, ez);
}

Point3 lerp_point(const Point3 &lhs, const Point3 &rhs, Scalar t) {
  return Point3{lhs.x + (rhs.x - lhs.x) * t, lhs.y + (rhs.y - lhs.y) * t,
                lhs.z + (rhs.z - lhs.z) * t};
}

Point3 evaluate_bezier_point(std::vector<Point3> control_points, Scalar t) {
  if (control_points.empty()) {
    return {};
  }
  t = std::clamp(t, 0.0, 1.0);
  for (std::size_t level = control_points.size(); level > 1; --level) {
    for (std::size_t i = 0; i + 1 < level; ++i) {
      control_points[i] = lerp_point(control_points[i], control_points[i + 1], t);
    }
  }
  return control_points.front();
}

Point3 evaluate_rational_bezier_point(const std::vector<Point3> &poles,
                                      const std::vector<Scalar> &weights,
                                      Scalar t) {
  if (poles.empty()) {
    return {};
  }
  if (weights.size() != poles.size()) {
    return evaluate_bezier_point(std::vector<Point3>(poles.begin(), poles.end()), t);
  }
  t = std::clamp(t, 0.0, 1.0);
  const auto n = poles.size() - 1;
  auto binomial = std::vector<Scalar>(poles.size(), 1.0);
  for (std::size_t i = 1; i <= n; ++i) {
    binomial[i] = binomial[i - 1] * static_cast<Scalar>(n - i + 1) /
                  static_cast<Scalar>(i);
  }
  Scalar weighted_sum = 0.0;
  Point3 point{};
  for (std::size_t i = 0; i <= n; ++i) {
    const auto basis = binomial[i] * std::pow(1.0 - t, static_cast<Scalar>(n - i)) *
                       std::pow(t, static_cast<Scalar>(i));
    const auto weight = std::max(weights[i], kEpsilon) * basis;
    weighted_sum += weight;
    point.x += poles[i].x * weight;
    point.y += poles[i].y * weight;
    point.z += poles[i].z * weight;
  }
  if (weighted_sum <= kEpsilon) {
    return poles.front();
  }
  point.x /= weighted_sum;
  point.y /= weighted_sum;
  point.z /= weighted_sum;
  return point;
}

Point3 evaluate_polyline_point(const std::vector<Point3> &poles, Scalar t) {
  if (poles.empty()) {
    return {};
  }
  if (poles.size() == 1) {
    return poles.front();
  }
  const auto max_t = static_cast<Scalar>(poles.size() - 1);
  t = std::clamp(t, 0.0, max_t);
  const auto segment = static_cast<std::size_t>(std::min<Scalar>(std::floor(t), max_t - 1.0));
  const auto local_t = t - static_cast<Scalar>(segment);
  return lerp_point(poles[segment], poles[segment + 1], local_t);
}

Vec3 finite_difference_tangent(const std::function<Point3(Scalar)> &eval_fn,
                               Scalar t, Scalar t_min, Scalar t_max) {
  const auto step = std::max((t_max - t_min) * 1e-4, 1e-6);
  const auto t0 = std::clamp(t - step, t_min, t_max);
  const auto t1 = std::clamp(t + step, t_min, t_max);
  if (std::abs(t1 - t0) <= kEpsilon) {
    return {1.0, 0.0, 0.0};
  }
  return normalize_or_default(detail::subtract(eval_fn(t1), eval_fn(t0)),
                              Vec3{1.0, 0.0, 0.0});
}

Range1D curve_domain(const detail::CurveRecord &curve) {
  const auto inf = std::numeric_limits<Scalar>::infinity();
  const auto two_pi = std::acos(-1.0) * 2.0;
  switch (curve.kind) {
  case detail::CurveKind::Line:
    return Range1D{-inf, inf};
  case detail::CurveKind::Circle:
  case detail::CurveKind::Ellipse:
    return Range1D{0.0, two_pi};
  case detail::CurveKind::BSpline:
    return Range1D{0.0, std::max<Scalar>(1.0, static_cast<Scalar>(curve.poles.size() - 1))};
  case detail::CurveKind::Bezier:
  case detail::CurveKind::Nurbs:
  default:
    return Range1D{0.0, 1.0};
  }
}

Point3 evaluate_curve_point(const detail::CurveRecord &curve, Scalar t) {
  switch (curve.kind) {
  case detail::CurveKind::Bezier:
    return evaluate_bezier_point(std::vector<Point3>(curve.poles.begin(), curve.poles.end()), t);
  case detail::CurveKind::BSpline:
    return evaluate_polyline_point(curve.poles, t);
  case detail::CurveKind::Nurbs:
    return evaluate_rational_bezier_point(curve.poles, curve.weights, t);
  default:
    return curve.origin;
  }
}

Scalar approximate_curve_parameter(const detail::CurveRecord &curve,
                                   const Point3 &point) {
  const auto domain = curve_domain(curve);
  const int samples = 64;
  Scalar best_t = domain.min;
  Scalar best_distance = std::numeric_limits<Scalar>::max();
  for (int i = 0; i <= samples; ++i) {
    const auto alpha = static_cast<Scalar>(i) / static_cast<Scalar>(samples);
    const auto t = domain.min + (domain.max - domain.min) * alpha;
    const auto sample = evaluate_curve_point(curve, t);
    const auto distance = detail::norm(detail::subtract(point, sample));
    if (distance < best_distance) {
      best_distance = distance;
      best_t = t;
    }
  }
  const auto radius = (domain.max - domain.min) * 0.125;
  for (int iter = 0; iter < kClosestParamRefineIters; ++iter) {
    const auto step = radius / static_cast<Scalar>(1u << iter);
    const auto left_t = std::clamp(best_t - step, domain.min, domain.max);
    const auto right_t = std::clamp(best_t + step, domain.min, domain.max);
    const auto dl =
        detail::norm(detail::subtract(point, evaluate_curve_point(curve, left_t)));
    const auto dr =
        detail::norm(detail::subtract(point, evaluate_curve_point(curve, right_t)));
    if (dl < best_distance || dr < best_distance) {
      if (dl <= dr) {
        best_t = left_t;
        best_distance = dl;
      } else {
        best_t = right_t;
        best_distance = dr;
      }
    }
  }
  return best_t;
}

std::pair<std::size_t, std::size_t> infer_surface_grid_dims(
    std::size_t pole_count) {
  if (pole_count == 0) {
    return {0, 0};
  }
  std::size_t rows = static_cast<std::size_t>(
      std::floor(std::sqrt(static_cast<Scalar>(pole_count))));
  rows = std::max<std::size_t>(1, rows);
  while (rows > 1 && pole_count % rows != 0) {
    --rows;
  }
  return {rows, std::max<std::size_t>(1, pole_count / rows)};
}

Range2D surface_domain(const detail::SurfaceRecord &surface) {
  const auto inf = std::numeric_limits<Scalar>::infinity();
  const auto two_pi = std::acos(-1.0) * 2.0;
  switch (surface.kind) {
  case detail::SurfaceKind::Plane:
    return Range2D{Range1D{-inf, inf}, Range1D{-inf, inf}};
  case detail::SurfaceKind::Sphere:
    return Range2D{Range1D{0.0, two_pi}, Range1D{0.0, std::acos(-1.0)}};
  case detail::SurfaceKind::Cylinder:
    return Range2D{Range1D{0.0, two_pi}, Range1D{-inf, inf}};
  case detail::SurfaceKind::Cone:
    // v is axial distance from apex along the cone axis direction in our parameterization.
    return Range2D{Range1D{0.0, two_pi}, Range1D{0.0, inf}};
  case detail::SurfaceKind::Torus:
    return Range2D{Range1D{0.0, two_pi}, Range1D{0.0, two_pi}};
  case detail::SurfaceKind::BSpline:
  case detail::SurfaceKind::Nurbs: {
    const auto [rows, cols] = infer_surface_grid_dims(surface.poles.size());
    return Range2D{
        Range1D{0.0, std::max<Scalar>(1.0, static_cast<Scalar>(rows - 1))},
        Range1D{0.0, std::max<Scalar>(1.0, static_cast<Scalar>(cols - 1))}};
  }
  default:
    return Range2D{Range1D{0.0, 1.0}, Range1D{0.0, 1.0}};
  }
}

Point3 bilinear_point(const Point3 &p00, const Point3 &p01, const Point3 &p10,
                      const Point3 &p11, Scalar u, Scalar v) {
  return lerp_point(lerp_point(p00, p01, v), lerp_point(p10, p11, v), u);
}

Point3 rational_bilinear_point(const Point3 &p00, const Point3 &p01,
                               const Point3 &p10, const Point3 &p11, Scalar w00,
                               Scalar w01, Scalar w10, Scalar w11, Scalar u,
                               Scalar v) {
  const auto b00 = (1.0 - u) * (1.0 - v);
  const auto b01 = (1.0 - u) * v;
  const auto b10 = u * (1.0 - v);
  const auto b11 = u * v;
  const auto rw00 = std::max(w00, kEpsilon) * b00;
  const auto rw01 = std::max(w01, kEpsilon) * b01;
  const auto rw10 = std::max(w10, kEpsilon) * b10;
  const auto rw11 = std::max(w11, kEpsilon) * b11;
  const auto total = rw00 + rw01 + rw10 + rw11;
  if (total <= kEpsilon) {
    return bilinear_point(p00, p01, p10, p11, u, v);
  }
  return Point3{(p00.x * rw00 + p01.x * rw01 + p10.x * rw10 + p11.x * rw11) /
                    total,
                (p00.y * rw00 + p01.y * rw01 + p10.y * rw10 + p11.y * rw11) /
                    total,
                (p00.z * rw00 + p01.z * rw01 + p10.z * rw10 + p11.z * rw11) /
                    total};
}

Point3 evaluate_surface_point(const detail::SurfaceRecord &surface, Scalar u,
                              Scalar v) {
  if (surface.poles.empty()) {
    return surface.origin;
  }
  const auto [rows, cols] = infer_surface_grid_dims(surface.poles.size());
  if (rows == 0 || cols == 0) {
    return surface.origin;
  }
  if (rows == 1 || cols == 1) {
    return evaluate_polyline_point(surface.poles, cols == 1 ? u : v);
  }
  const auto domain = surface_domain(surface);
  const auto cu = std::clamp(u, domain.u.min, domain.u.max);
  const auto cv = std::clamp(v, domain.v.min, domain.v.max);
  const auto ru =
      std::min<std::size_t>(static_cast<std::size_t>(std::floor(cu)), rows - 2);
  const auto rv =
      std::min<std::size_t>(static_cast<std::size_t>(std::floor(cv)), cols - 2);
  const auto lu = cu - static_cast<Scalar>(ru);
  const auto lv = cv - static_cast<Scalar>(rv);
  const auto idx = [cols](std::size_t r, std::size_t c) { return r * cols + c; };
  const auto &p00 = surface.poles[idx(ru, rv)];
  const auto &p01 = surface.poles[idx(ru, rv + 1)];
  const auto &p10 = surface.poles[idx(ru + 1, rv)];
  const auto &p11 = surface.poles[idx(ru + 1, rv + 1)];
  if (surface.kind == detail::SurfaceKind::Nurbs &&
      surface.weights.size() == surface.poles.size()) {
    return rational_bilinear_point(
        p00, p01, p10, p11, surface.weights[idx(ru, rv)],
        surface.weights[idx(ru, rv + 1)], surface.weights[idx(ru + 1, rv)],
        surface.weights[idx(ru + 1, rv + 1)], lu, lv);
  }
  return bilinear_point(p00, p01, p10, p11, lu, lv);
}

std::pair<Scalar, Scalar> approximate_surface_uv(
    const detail::SurfaceRecord &surface, const Point3 &point) {
  const auto domain = surface_domain(surface);
  const int samples = 24;
  Scalar best_u = domain.u.min;
  Scalar best_v = domain.v.min;
  Scalar best_distance = std::numeric_limits<Scalar>::max();
  for (int ui = 0; ui <= samples; ++ui) {
    const auto au = static_cast<Scalar>(ui) / static_cast<Scalar>(samples);
    const auto u = domain.u.min + (domain.u.max - domain.u.min) * au;
    for (int vi = 0; vi <= samples; ++vi) {
      const auto av = static_cast<Scalar>(vi) / static_cast<Scalar>(samples);
      const auto v = domain.v.min + (domain.v.max - domain.v.min) * av;
      const auto sample = evaluate_surface_point(surface, u, v);
      const auto distance = detail::norm(detail::subtract(point, sample));
      if (distance < best_distance) {
        best_distance = distance;
        best_u = u;
        best_v = v;
      }
    }
  }
  return {best_u, best_v};
}

BoundingBox make_curve_bbox(const detail::CurveRecord &curve) {
  switch (curve.kind) {
  case detail::CurveKind::Line:
    return detail::bbox_from_center_radius(curve.origin, 1.0, 1.0, 1.0);
  case detail::CurveKind::Circle:
    return bbox_from_span_vectors(
        curve.origin, detail::scale(curve.axis_u, curve.radius),
        detail::scale(curve.axis_v, curve.radius));
  case detail::CurveKind::Ellipse:
    return bbox_from_span_vectors(curve.origin, curve.axis_u, curve.axis_v);
  case detail::CurveKind::Bezier:
  case detail::CurveKind::BSpline:
  case detail::CurveKind::Nurbs: {
    if (curve.poles.empty()) {
      return detail::bbox_from_center_radius(curve.origin, 1.0, 1.0, 1.0);
    }
    auto min = curve.poles.front();
    auto max = curve.poles.front();
    for (const auto &p : curve.poles) {
      min.x = std::min(min.x, p.x);
      min.y = std::min(min.y, p.y);
      min.z = std::min(min.z, p.z);
      max.x = std::max(max.x, p.x);
      max.y = std::max(max.y, p.y);
      max.z = std::max(max.z, p.z);
    }
    return detail::make_bbox(min, max);
  }
  default:
    return detail::bbox_from_center_radius(curve.origin, 1.0, 1.0, 1.0);
  }
}

BoundingBox make_surface_bbox(const detail::SurfaceRecord &surface) {
  switch (surface.kind) {
  case detail::SurfaceKind::Plane:
    return detail::bbox_from_center_radius(surface.origin, 1000.0, 1000.0, 0.0);
  case detail::SurfaceKind::Sphere:
    return detail::bbox_from_center_radius(surface.origin, surface.radius_a,
                                           surface.radius_a, surface.radius_a);
  case detail::SurfaceKind::Cylinder:
    return detail::bbox_from_center_radius(surface.origin, surface.radius_a,
                                           surface.radius_a, 1000.0);
  case detail::SurfaceKind::Cone:
    return detail::bbox_from_center_radius(surface.origin, 1000.0, 1000.0, 1000.0);
  case detail::SurfaceKind::Torus:
    return detail::bbox_from_center_radius(surface.origin, surface.radius_a + surface.radius_b,
                                           surface.radius_a + surface.radius_b, surface.radius_b);
  case detail::SurfaceKind::BSpline:
  case detail::SurfaceKind::Nurbs: {
    if (surface.poles.empty()) {
      return detail::bbox_from_center_radius(surface.origin, 10.0, 10.0, 10.0);
    }
    auto min = surface.poles.front();
    auto max = surface.poles.front();
    for (const auto &p : surface.poles) {
      min.x = std::min(min.x, p.x);
      min.y = std::min(min.y, p.y);
      min.z = std::min(min.z, p.z);
      max.x = std::max(max.x, p.x);
      max.y = std::max(max.y, p.y);
      max.z = std::max(max.z, p.z);
    }
    return detail::make_bbox(min, max);
  }
  default:
    return detail::bbox_from_center_radius(surface.origin, 10.0, 10.0, 10.0);
  }
}

detail::CurveRecord make_curve_record(detail::CurveKind kind,
                                      const Point3 &origin,
                                      const Vec3 &direction, Scalar radius,
                                      const Vec3 &normal, const Vec3 &axis_u,
                                      const Vec3 &axis_v,
                                      std::vector<Point3> poles = {},
                                      std::vector<Scalar> weights = {},
                                      std::vector<Scalar> knots_u = {}) {
  detail::CurveRecord record;
  record.kind = kind;
  record.origin = origin;
  record.direction = direction;
  record.radius = radius;
  record.normal = normal;
  record.axis_u = axis_u;
  record.axis_v = axis_v;
  record.poles = std::move(poles);
  record.weights = std::move(weights);
  record.knots_u = std::move(knots_u);
  return record;
}

detail::SurfaceRecord make_surface_record(detail::SurfaceKind kind,
                                          const Point3 &origin,
                                          const Vec3 &axis,
                                          const Vec3 &normal,
                                          Scalar radius_a, Scalar radius_b,
                                          Scalar semi_angle,
                                          std::vector<Point3> poles = {},
                                          std::vector<Scalar> weights = {},
                                          std::vector<Scalar> knots_u = {},
                                          std::vector<Scalar> knots_v = {}) {
  detail::SurfaceRecord record;
  record.kind = kind;
  record.origin = origin;
  record.axis = axis;
  record.normal = normal;
  record.radius_a = radius_a;
  record.radius_b = radius_b;
  record.semi_angle = semi_angle;
  record.poles = std::move(poles);
  record.weights = std::move(weights);
  record.knots_u = std::move(knots_u);
  record.knots_v = std::move(knots_v);
  return record;
}

} // namespace

CurveFactory::CurveFactory(std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

PCurveFactory::PCurveFactory(std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

Result<PCurveId> PCurveFactory::make_polyline(std::span<const Point2> poles) {
  if (poles.size() < 2) {
    return detail::invalid_input_result<PCurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "参数曲线创建失败：polyline 至少需要两个点", "参数曲线创建失败");
  }
  for (const auto &p : poles) {
    if (!finite_point2(p)) {
      return detail::invalid_input_result<PCurveId>(
          *state_, diag_codes::kGeoCurveCreationInvalid,
          "参数曲线创建失败：输入点包含非法数值", "参数曲线创建失败");
    }
  }
  const auto id = PCurveId{state_->allocate_id()};
  detail::PCurveRecord rec;
  rec.kind = detail::PCurveKind::Polyline;
  rec.poles.assign(poles.begin(), poles.end());
  state_->pcurves.emplace(id.value, std::move(rec));
  return ok_result(id, state_->create_diagnostic("已创建参数曲线"));
}

Result<CurveId> CurveFactory::make_line(const Point3 &origin,
                                        const Vec3 &direction) {
  if (!finite_point(origin) || !finite_vec(direction)) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "直线创建失败：输入包含非法数值", "直线创建失败");
  }
  if (detail::norm(direction) <= 0.0) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "直线创建失败：方向向量不能为零", "直线创建失败");
  }
  const auto id = CurveId{state_->allocate_id()};
  state_->curves.emplace(id.value,
                         make_curve_record(detail::CurveKind::Line, origin,
                                           detail::normalize(direction), 0.0,
                                           {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0},
                                           {0.0, 1.0, 0.0}));
  return ok_result(id, state_->create_diagnostic("已创建直线"));
}

Result<CurveId> CurveFactory::make_circle(const Point3 &center,
                                          const Vec3 &normal, Scalar radius) {
  if (!finite_point(center) || !finite_vec(normal) || !finite_scalar(radius)) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "圆创建失败：输入包含非法数值", "圆创建失败");
  }
  if (detail::norm(normal) <= kEpsilon) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "圆创建参数非法：法向量不能为零", "圆创建失败");
  }
  if (radius <= 0.0) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "圆创建参数非法：半径必须大于 0", "圆创建失败");
  }
  const auto frame = make_frame_from_axis(normal);
  const auto id = CurveId{state_->allocate_id()};
  state_->curves.emplace(id.value,
                         make_curve_record(detail::CurveKind::Circle, center,
                                           {1.0, 0.0, 0.0}, radius, frame.w,
                                           frame.u, frame.v));
  return ok_result(id, state_->create_diagnostic("已创建圆"));
}

Result<CurveId> CurveFactory::make_ellipse(const Point3 &center,
                                           const Vec3 &axis_u,
                                           const Vec3 &axis_v) {
  if (!finite_point(center) || !finite_vec(axis_u) || !finite_vec(axis_v)) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "椭圆创建失败：输入包含非法数值", "椭圆创建失败");
  }
  if (detail::norm(axis_u) <= kEpsilon || detail::norm(axis_v) <= kEpsilon) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "椭圆创建失败：长短轴向量都必须非零", "椭圆创建失败");
  }
  if (detail::norm(detail::cross(axis_u, axis_v)) <= kEpsilon) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "椭圆创建失败：长短轴向量不能共线", "椭圆创建失败");
  }
  const auto id = CurveId{state_->allocate_id()};
  state_->curves.emplace(
      id.value,
      make_curve_record(detail::CurveKind::Ellipse, center, {1.0, 0.0, 0.0},
                        1.0, detail::normalize(detail::cross(axis_u, axis_v)),
                        axis_u, axis_v));
  return ok_result(id, state_->create_diagnostic("已创建椭圆"));
}

Result<CurveId> CurveFactory::make_bezier(std::span<const Point3> poles) {
  if (poles.empty()) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "Bezier 曲线创建失败：控制点不能为空", "Bezier 曲线创建失败");
  }
  const auto id = CurveId{state_->allocate_id()};
  state_->curves.emplace(
      id.value,
      make_curve_record(detail::CurveKind::Bezier, poles.front(),
                        {1.0, 0.0, 0.0}, 0.0, {0.0, 0.0, 1.0},
                        {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
                        std::vector<Point3>(poles.begin(), poles.end())));
  return ok_result(id, state_->create_diagnostic("已创建Bezier曲线"));
}

Result<CurveId> CurveFactory::make_bspline(const BSplineCurveDesc &desc) {
  if (desc.poles.empty()) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "BSpline 曲线创建失败：控制点不能为空", "BSpline 曲线创建失败");
  }
  const auto id = CurveId{state_->allocate_id()};
  state_->curves.emplace(
      id.value,
      make_curve_record(detail::CurveKind::BSpline, desc.poles.front(),
                        {1.0, 0.0, 0.0}, 0.0, {0.0, 0.0, 1.0},
                        {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, desc.poles, {},
                        uniform_knot_vector(desc.poles.size())));
  return ok_result(id, state_->create_diagnostic("已创建BSpline曲线"));
}

Result<CurveId> CurveFactory::make_nurbs(const NURBSCurveDesc &desc) {
  if (desc.poles.empty()) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "NURBS 曲线创建失败：控制点不能为空", "NURBS 曲线创建失败");
  }
  if (!desc.weights.empty() && desc.weights.size() != desc.poles.size()) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "NURBS 曲线创建失败：权重数量必须与控制点数量一致",
        "NURBS 曲线创建失败");
  }
  bool ok_weights = true;
  auto normalized_weights =
      normalized_positive_weights(desc.weights, desc.poles.size(), ok_weights);
  if (!ok_weights) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "NURBS 曲线创建失败：权重必须为有限正数且总和大于 0",
        "NURBS 曲线创建失败");
  }
  const auto id = CurveId{state_->allocate_id()};
  state_->curves.emplace(
      id.value,
      make_curve_record(detail::CurveKind::Nurbs, desc.poles.front(),
                        {1.0, 0.0, 0.0}, 0.0, {0.0, 0.0, 1.0},
                        {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, desc.poles,
                        std::move(normalized_weights),
                        uniform_knot_vector(desc.poles.size())));
  return ok_result(id, state_->create_diagnostic("已创建NURBS曲线"));
}

SurfaceFactory::SurfaceFactory(std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

Result<SurfaceId> SurfaceFactory::make_plane(const Point3 &origin,
                                             const Vec3 &normal) {
  if (!finite_point(origin) || !finite_vec(normal)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "平面创建失败：输入包含非法数值", "平面创建失败");
  }
  if (detail::norm(normal) <= 0.0) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "平面创建失败：法向量不能为零", "平面创建失败");
  }
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(id.value,
                           make_surface_record(detail::SurfaceKind::Plane,
                                               origin, {0.0, 0.0, 1.0},
                                               detail::normalize(normal), 0.0,
                                               0.0, 0.0));
  return ok_result(id, state_->create_diagnostic("已创建平面"));
}

Result<SurfaceId> SurfaceFactory::make_cylinder(const Point3 &origin,
                                                const Vec3 &axis,
                                                Scalar radius) {
  if (!finite_point(origin) || !finite_vec(axis) || !finite_scalar(radius)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "圆柱面创建失败：输入包含非法数值", "圆柱面创建失败");
  }
  if (detail::norm(axis) <= kEpsilon) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "圆柱面创建失败：轴向量不能为零", "圆柱面创建失败");
  }
  if (radius <= 0.0) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "圆柱面创建失败：半径必须大于 0", "圆柱面创建失败");
  }
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(
      id.value,
      make_surface_record(detail::SurfaceKind::Cylinder, origin,
                          detail::normalize(axis), detail::normalize(axis),
                          radius, 0.0, 0.0));
  return ok_result(id, state_->create_diagnostic("已创建圆柱面"));
}

Result<SurfaceId> SurfaceFactory::make_cone(const Point3 &apex,
                                            const Vec3 &axis,
                                            Scalar semi_angle) {
  if (!finite_point(apex) || !finite_vec(axis) || !finite_scalar(semi_angle)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "圆锥面创建失败：输入包含非法数值", "圆锥面创建失败");
  }
  if (detail::norm(axis) <= kEpsilon) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "圆锥面创建失败：轴向量不能为零", "圆锥面创建失败");
  }
  if (semi_angle <= 0.0 || semi_angle >= std::acos(-1.0) * 0.5) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "圆锥面创建失败：半角必须位于 (0, pi/2) 区间内", "圆锥面创建失败");
  }
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(
      id.value,
      make_surface_record(detail::SurfaceKind::Cone, apex,
                          detail::normalize(axis), detail::normalize(axis),
                          0.0, 0.0, semi_angle));
  return ok_result(id, state_->create_diagnostic("已创建圆锥面"));
}

Result<SurfaceId> SurfaceFactory::make_sphere(const Point3 &center,
                                              Scalar radius) {
  if (!finite_point(center) || !finite_scalar(radius)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "球面创建失败：输入包含非法数值", "球面创建失败");
  }
  if (radius <= 0.0) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "球面创建失败：半径必须大于 0", "球面创建失败");
  }
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(id.value,
                           make_surface_record(detail::SurfaceKind::Sphere,
                                               center, {0.0, 0.0, 1.0},
                                               {0.0, 0.0, 1.0}, radius, radius,
                                               0.0));
  return ok_result(id, state_->create_diagnostic("已创建球面"));
}

Result<SurfaceId> SurfaceFactory::make_torus(const Point3 &center,
                                             const Vec3 &axis, Scalar major_r,
                                             Scalar minor_r) {
  if (!finite_point(center) || !finite_vec(axis) || !finite_scalar(major_r) ||
      !finite_scalar(minor_r)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "环面创建失败：输入包含非法数值", "环面创建失败");
  }
  if (detail::norm(axis) <= kEpsilon) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "环面创建失败：轴向量不能为零", "环面创建失败");
  }
  if (major_r <= 0.0 || minor_r <= 0.0) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "环面创建失败：主半径和副半径必须大于 0", "环面创建失败");
  }
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(
      id.value,
      make_surface_record(detail::SurfaceKind::Torus, center,
                          detail::normalize(axis), detail::normalize(axis),
                          major_r, minor_r, 0.0));
  return ok_result(id, state_->create_diagnostic("已创建环面"));
}

Result<SurfaceId> SurfaceFactory::make_bspline(const BSplineSurfaceDesc &desc) {
  if (desc.poles.empty()) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "BSpline 曲面创建失败：控制点不能为空", "BSpline 曲面创建失败");
  }
  const auto [rows, cols] = infer_surface_grid_dims(desc.poles.size());
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(
      id.value,
      make_surface_record(detail::SurfaceKind::BSpline, desc.poles.front(),
                          {0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}, 0.0, 0.0, 0.0,
                          desc.poles, {}, uniform_knot_vector(rows),
                          uniform_knot_vector(cols)));
  return ok_result(id, state_->create_diagnostic("已创建BSpline曲面"));
}

Result<SurfaceId> SurfaceFactory::make_nurbs(const NURBSSurfaceDesc &desc) {
  if (desc.poles.empty()) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "NURBS 曲面创建失败：控制点不能为空", "NURBS 曲面创建失败");
  }
  if (!desc.weights.empty() && desc.weights.size() != desc.poles.size()) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "NURBS 曲面创建失败：权重数量必须与控制点数量一致",
        "NURBS 曲面创建失败");
  }
  bool ok_weights = true;
  auto normalized_weights =
      normalized_positive_weights(desc.weights, desc.poles.size(), ok_weights);
  if (!ok_weights) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "NURBS 曲面创建失败：权重必须为有限正数且总和大于 0",
        "NURBS 曲面创建失败");
  }
  const auto [rows, cols] = infer_surface_grid_dims(desc.poles.size());
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(
      id.value,
      make_surface_record(detail::SurfaceKind::Nurbs, desc.poles.front(),
                          {0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}, 0.0, 0.0, 0.0,
                          desc.poles, std::move(normalized_weights),
                          uniform_knot_vector(rows), uniform_knot_vector(cols)));
  return ok_result(id, state_->create_diagnostic("已创建NURBS曲面"));
}

CurveService::CurveService(std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

PCurveService::PCurveService(std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

Result<PCurveEvalResult> PCurveService::eval(PCurveId pcurve_id, Scalar t,
                                             int deriv_order) const {
  if (!finite_scalar(t) || !std::isfinite(static_cast<Scalar>(deriv_order))) {
    return detail::invalid_input_result<PCurveEvalResult>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "参数曲线求值失败：参数包含非法数值", "参数曲线求值失败");
  }
  if (deriv_order < 0) {
    return detail::invalid_input_result<PCurveEvalResult>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "参数曲线求值失败：导数阶数不能为负", "参数曲线求值失败");
  }
  const auto it = state_->pcurves.find(pcurve_id.value);
  if (it == state_->pcurves.end()) {
    return detail::invalid_input_result<PCurveEvalResult>(
        *state_, diag_codes::kCoreInvalidHandle,
        "参数曲线求值失败：目标参数曲线不存在", "参数曲线求值失败");
  }
  const auto &pc = it->second;
  const auto domain = pcurve_domain(pc);
  const auto ct = std::clamp(t, domain.min, domain.max);
  PCurveEvalResult out{};
  out.point = evaluate_pcurve_point(pc, ct);
  const auto step = std::max((domain.max - domain.min) * 1e-4, 1e-6);
  const auto t0 = std::clamp(ct - step, domain.min, domain.max);
  const auto t1 = std::clamp(ct + step, domain.min, domain.max);
  const auto p0 = evaluate_pcurve_point(pc, t0);
  const auto p1 = evaluate_pcurve_point(pc, t1);
  out.tangent = normalize2_or_default(subtract2(p1, p0), Vec2{1.0, 0.0});
  const auto order = std::max(0, deriv_order);
  out.derivatives.resize(static_cast<std::size_t>(order), Vec2{0.0, 0.0});
  if (order > 0) {
    out.derivatives[0] = out.tangent;
  }
  return ok_result(out, state_->create_diagnostic("参数曲线求值完成"));
}

Result<Scalar> PCurveService::closest_parameter(PCurveId pcurve_id,
                                                const Point2 &point) const {
  if (!finite_point2(point)) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "参数曲线最近参数求解失败：输入点包含非法数值",
        "参数曲线最近参数求解失败");
  }
  const auto it = state_->pcurves.find(pcurve_id.value);
  if (it == state_->pcurves.end()) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreInvalidHandle,
        "参数曲线最近参数求解失败：目标参数曲线不存在",
        "参数曲线最近参数求解失败");
  }
  const auto &pc = it->second;
  const auto domain = pcurve_domain(pc);
  const auto t = std::clamp(approximate_pcurve_parameter(pc, point), domain.min,
                            domain.max);
  return ok_result(t, state_->create_diagnostic("参数曲线最近参数已求解"));
}

Result<Point2> PCurveService::closest_point(PCurveId pcurve_id,
                                            const Point2 &point) const {
  const auto t = closest_parameter(pcurve_id, point);
  if (t.status != StatusCode::Ok || !t.value.has_value()) {
    return error_result<Point2>(t.status, t.diagnostic_id);
  }
  const auto eval_result = eval(pcurve_id, *t.value, 1);
  if (eval_result.status != StatusCode::Ok || !eval_result.value.has_value()) {
    return error_result<Point2>(eval_result.status, eval_result.diagnostic_id);
  }
  return ok_result(eval_result.value->point, eval_result.diagnostic_id);
}

Result<Range1D> PCurveService::domain(PCurveId pcurve_id) const {
  const auto it = state_->pcurves.find(pcurve_id.value);
  if (it == state_->pcurves.end()) {
    return detail::invalid_input_result<Range1D>(
        *state_, diag_codes::kCoreInvalidHandle,
        "参数曲线定义域查询失败：目标参数曲线不存在",
        "参数曲线定义域查询失败");
  }
  return ok_result(pcurve_domain(it->second),
                   state_->create_diagnostic("已返回参数曲线定义域"));
}

Result<BoundingBox> PCurveService::bbox(PCurveId pcurve_id) const {
  const auto it = state_->pcurves.find(pcurve_id.value);
  if (it == state_->pcurves.end()) {
    return detail::invalid_input_result<BoundingBox>(
        *state_, diag_codes::kCoreInvalidHandle,
        "参数曲线包围盒查询失败：目标参数曲线不存在",
        "参数曲线包围盒查询失败");
  }
  const auto &pc = it->second;
  if (pc.poles.empty()) {
    return ok_result(BoundingBox{}, state_->create_diagnostic("已返回参数曲线包围盒"));
  }
  Scalar minx = pc.poles.front().x, maxx = pc.poles.front().x;
  Scalar miny = pc.poles.front().y, maxy = pc.poles.front().y;
  for (const auto &p : pc.poles) {
    minx = std::min(minx, p.x);
    maxx = std::max(maxx, p.x);
    miny = std::min(miny, p.y);
    maxy = std::max(maxy, p.y);
  }
  return ok_result(detail::make_bbox(Point3{minx, miny, 0.0},
                                     Point3{maxx, maxy, 0.0}),
                   state_->create_diagnostic("已返回参数曲线包围盒"));
}

Result<CurveEvalResult> CurveService::eval(CurveId curve_id, Scalar t,
                                           int deriv_order) const {
  if (!finite_scalar(t) || !std::isfinite(static_cast<Scalar>(deriv_order))) {
    return detail::invalid_input_result<CurveEvalResult>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲线求值失败：参数包含非法数值", "曲线求值失败");
  }
  if (deriv_order < 0) {
    return detail::invalid_input_result<CurveEvalResult>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲线求值失败：导数阶数不能为负", "曲线求值失败");
  }
  if (state_->config.enable_cache) {
    const auto key = curve_eval_cache_key(curve_id, t, deriv_order);
    const auto it_cache = state_->curve_eval_cache.find(key);
    if (it_cache != state_->curve_eval_cache.end()) {
      return ok_result(it_cache->second, state_->create_diagnostic("曲线求值缓存命中"));
    }
  }
  const auto it = state_->curves.find(curve_id.value);
  if (it == state_->curves.end()) {
    return detail::invalid_input_result<CurveEvalResult>(
        *state_, diag_codes::kCoreInvalidHandle,
        "曲线求值失败：目标曲线不存在", "曲线求值失败");
  }

  CurveEvalResult result{};
  const auto &curve = it->second;
  switch (curve.kind) {
  case detail::CurveKind::Line:
    result.point =
        detail::add_point_vec(curve.origin, detail::scale(curve.direction, t));
    result.tangent = detail::normalize(curve.direction);
    break;
  case detail::CurveKind::Circle:
    result.point = point_from_local(
        curve.origin, OrthoFrame{curve.axis_u, curve.axis_v, curve.normal},
        curve.radius * std::cos(t), curve.radius * std::sin(t), 0.0);
    result.tangent = normalize_or_default(
        vec_from_local(OrthoFrame{curve.axis_u, curve.axis_v, curve.normal},
                       -curve.radius * std::sin(t),
                       curve.radius * std::cos(t), 0.0),
        curve.axis_u);
    break;
  case detail::CurveKind::Ellipse:
    result.point = Point3{
        curve.origin.x + curve.axis_u.x * std::cos(t) + curve.axis_v.x * std::sin(t),
        curve.origin.y + curve.axis_u.y * std::cos(t) + curve.axis_v.y * std::sin(t),
        curve.origin.z + curve.axis_u.z * std::cos(t) + curve.axis_v.z * std::sin(t)};
    result.tangent = detail::normalize(
        Vec3{-curve.axis_u.x * std::sin(t) + curve.axis_v.x * std::cos(t),
             -curve.axis_u.y * std::sin(t) + curve.axis_v.y * std::cos(t),
             -curve.axis_u.z * std::sin(t) + curve.axis_v.z * std::cos(t)});
    break;
  case detail::CurveKind::Bezier:
  case detail::CurveKind::BSpline:
  case detail::CurveKind::Nurbs: {
    if (!curve.poles.empty()) {
      const auto domain = curve_domain(curve);
      const auto eval_fn = [&curve](Scalar value) {
        return evaluate_curve_point(curve, value);
      };
      const auto clamped_t = std::clamp(t, domain.min, domain.max);
      result.point = eval_fn(clamped_t);
      result.tangent =
          finite_difference_tangent(eval_fn, clamped_t, domain.min, domain.max);
    } else {
      result.point = curve.origin;
      result.tangent = {1.0, 0.0, 0.0};
    }
    break;
  }
  default:
    result.point = curve.origin;
    result.tangent = {1.0, 0.0, 0.0};
    break;
  }

  const auto order = std::max(0, deriv_order);
  result.derivatives.resize(static_cast<std::size_t>(order), Vec3{0.0, 0.0, 0.0});
  if (order > 0) {
    result.derivatives[0] = result.tangent;
  }
  if (state_->config.enable_cache) {
    state_->curve_eval_cache.emplace(
        curve_eval_cache_key(curve_id, t, deriv_order), result);
  }
  return ok_result(result, state_->create_diagnostic("曲线求值完成"));
}

Result<std::vector<CurveEvalResult>>
CurveService::eval_batch(CurveId curve_id, std::span<const Scalar> ts,
                         int deriv_order) const {
  if (ts.empty()) {
    return detail::invalid_input_result<std::vector<CurveEvalResult>>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲线批量求值失败：参数集合为空", "曲线批量求值失败");
  }
  std::vector<CurveEvalResult> out;
  out.reserve(ts.size());
  for (const auto t : ts) {
    const auto single = eval(curve_id, t, deriv_order);
    if (single.status != StatusCode::Ok || !single.value.has_value()) {
      return error_result<std::vector<CurveEvalResult>>(single.status,
                                                        single.diagnostic_id);
    }
    out.push_back(*single.value);
  }
  return ok_result(std::move(out), state_->create_diagnostic("曲线批量求值完成"));
}

Result<Scalar> CurveService::closest_parameter(CurveId curve_id,
                                               const Point3 &point) const {
  if (!finite_point(point)) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲线最近参数求解失败：输入点包含非法数值", "曲线最近参数求解失败");
  }
  if (!detail::has_curve(*state_, curve_id)) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::InvalidInput, diag_codes::kGeoParameterSolveFailure,
        "曲线最近参数求解失败：目标曲线不存在", "曲线最近参数求解失败");
  }
  const auto &curve = state_->curves.at(curve_id.value);
  switch (curve.kind) {
  case detail::CurveKind::Line: {
    const auto dir = curve.direction;
    const auto denom = detail::dot(dir, dir);
    if (denom <= 0.0) {
      return detail::failed_result<Scalar>(
          *state_, StatusCode::OperationFailed, diag_codes::kGeoParameterSolveFailure,
          "曲线最近参数求解失败：直线方向无效", "曲线最近参数求解失败");
    }
    const auto op = detail::subtract(point, curve.origin);
    return ok_result<Scalar>(detail::dot(op, dir) / denom,
                             state_->create_diagnostic("曲线最近参数已求解"));
  }
  case detail::CurveKind::Circle: {
    const auto rel = detail::subtract(point, curve.origin);
    const auto pu = detail::dot(rel, curve.axis_u);
    const auto pv = detail::dot(rel, curve.axis_v);
    return ok_result<Scalar>(normalize_angle_0_2pi(std::atan2(pv, pu)),
                             state_->create_diagnostic("曲线最近参数已求解"));
  }
  case detail::CurveKind::Ellipse: {
    const auto rel = detail::subtract(point, curve.origin);
    const auto pu = detail::dot(rel, curve.axis_u) /
                    std::max(detail::dot(curve.axis_u, curve.axis_u), 1e-12);
    const auto pv = detail::dot(rel, curve.axis_v) /
                    std::max(detail::dot(curve.axis_v, curve.axis_v), 1e-12);
    return ok_result<Scalar>(normalize_angle_0_2pi(std::atan2(pv, pu)),
                             state_->create_diagnostic("曲线最近参数已求解"));
  }
  default:
    break;
  }

  if (curve.kind == detail::CurveKind::Bezier ||
      curve.kind == detail::CurveKind::BSpline ||
      curve.kind == detail::CurveKind::Nurbs) {
    const auto domain = curve_domain(curve);
    const auto t = std::clamp(approximate_curve_parameter(curve, point),
                              domain.min, domain.max);
    return ok_result<Scalar>(t, state_->create_diagnostic("曲线最近参数已求解"));
  }

  const auto eval0 = eval(curve_id, 0.0, 1);
  const auto eval1 = eval(curve_id, 1.0, 1);
  if (eval0.status != StatusCode::Ok || eval1.status != StatusCode::Ok ||
      !eval0.value.has_value() || !eval1.value.has_value()) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::OperationFailed, diag_codes::kGeoParameterSolveFailure,
        "曲线最近参数求解失败：端点求值不可用", "曲线最近参数求解失败");
  }

  const auto d0 = detail::norm(detail::subtract(point, eval0.value->point));
  const auto d1 = detail::norm(detail::subtract(point, eval1.value->point));
  return ok_result<Scalar>(d0 <= d1 ? 0.0 : 1.0,
                           state_->create_diagnostic("曲线最近参数已求解"));
}

Result<std::vector<Scalar>>
CurveService::closest_parameters_batch(CurveId curve_id,
                                       std::span<const Point3> points) const {
  if (points.empty()) {
    return detail::invalid_input_result<std::vector<Scalar>>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲线批量最近参数求解失败：输入点集合为空",
        "曲线批量最近参数求解失败");
  }
  std::vector<Scalar> out;
  out.reserve(points.size());
  for (const auto &point : points) {
    const auto single = closest_parameter(curve_id, point);
    if (single.status != StatusCode::Ok || !single.value.has_value()) {
      return error_result<std::vector<Scalar>>(single.status,
                                               single.diagnostic_id);
    }
    out.push_back(*single.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("曲线批量最近参数已求解"));
}

Result<Point3> CurveService::closest_point(CurveId curve_id,
                                           const Point3 &point) const {
  if (!finite_point(point)) {
    return detail::invalid_input_result<Point3>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲线最近点求解失败：输入点包含非法数值", "曲线最近点求解失败");
  }
  const auto t = closest_parameter(curve_id, point);
  if (t.status != StatusCode::Ok || !t.value.has_value()) {
    return detail::failed_result<Point3>(
        *state_, StatusCode::OperationFailed, diag_codes::kGeoClosestPointFailure,
        "曲线最近点求解失败：最近参数不可用", "曲线最近点求解失败");
  }

  const auto eval_result = eval(curve_id, *t.value, 1);
  if (eval_result.status != StatusCode::Ok || !eval_result.value.has_value()) {
    return error_result<Point3>(StatusCode::OperationFailed,
                                eval_result.diagnostic_id);
  }
  return ok_result(eval_result.value->point, eval_result.diagnostic_id);
}

Result<std::vector<Point3>>
CurveService::closest_points_batch(CurveId curve_id,
                                   std::span<const Point3> points) const {
  if (points.empty()) {
    return detail::invalid_input_result<std::vector<Point3>>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲线批量最近点求解失败：输入点集合为空", "曲线批量最近点求解失败");
  }
  std::vector<Point3> out;
  out.reserve(points.size());
  for (const auto &point : points) {
    const auto single = closest_point(curve_id, point);
    if (single.status != StatusCode::Ok || !single.value.has_value()) {
      return error_result<std::vector<Point3>>(single.status,
                                               single.diagnostic_id);
    }
    out.push_back(*single.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("曲线批量最近点已求解"));
}

Result<Range1D> CurveService::domain(CurveId curve_id) const {
  if (!detail::has_curve(*state_, curve_id)) {
    return detail::invalid_input_result<Range1D>(
        *state_, diag_codes::kCoreInvalidHandle,
        "曲线定义域查询失败：目标曲线不存在", "曲线定义域查询失败");
  }
  return ok_result(curve_domain(state_->curves.at(curve_id.value)),
                   state_->create_diagnostic("已返回曲线定义域"));
}

Result<BoundingBox> CurveService::bbox(CurveId curve_id) const {
  const auto it = state_->curves.find(curve_id.value);
  if (it == state_->curves.end()) {
    return detail::invalid_input_result<BoundingBox>(
        *state_, diag_codes::kCoreInvalidHandle,
        "曲线包围盒查询失败：目标曲线不存在", "曲线包围盒查询失败");
  }
  return ok_result(make_curve_bbox(it->second),
                   state_->create_diagnostic("已返回曲线包围盒"));
}

Result<std::vector<BoundingBox>>
CurveService::bbox_batch(std::span<const CurveId> curve_ids) const {
  if (curve_ids.empty()) {
    return detail::invalid_input_result<std::vector<BoundingBox>>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲线批量包围盒查询失败：输入曲线集合为空", "曲线批量包围盒查询失败");
  }
  std::vector<BoundingBox> out;
  out.reserve(curve_ids.size());
  for (const auto curve_id : curve_ids) {
    const auto single = bbox(curve_id);
    if (single.status != StatusCode::Ok || !single.value.has_value()) {
      return error_result<std::vector<BoundingBox>>(single.status,
                                                    single.diagnostic_id);
    }
    out.push_back(*single.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已返回曲线批量包围盒"));
}

SurfaceService::SurfaceService(std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

Result<SurfaceEvalResult> SurfaceService::eval(SurfaceId surface_id, Scalar u,
                                               Scalar v, int deriv_order) const {
  if (!finite_scalar(u) || !finite_scalar(v) ||
      !std::isfinite(static_cast<Scalar>(deriv_order))) {
    return detail::invalid_input_result<SurfaceEvalResult>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲面求值失败：参数包含非法数值", "曲面求值失败");
  }
  if (deriv_order < 0) {
    return detail::invalid_input_result<SurfaceEvalResult>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲面求值失败：导数阶数不能为负", "曲面求值失败");
  }
  if (state_->config.enable_cache) {
    const auto key = surface_eval_cache_key(surface_id, u, v, deriv_order);
    const auto it_cache = state_->surface_eval_cache.find(key);
    if (it_cache != state_->surface_eval_cache.end()) {
      return ok_result(it_cache->second, state_->create_diagnostic("曲面求值缓存命中"));
    }
  }
  const auto it = state_->surfaces.find(surface_id.value);
  if (it == state_->surfaces.end()) {
    return detail::invalid_input_result<SurfaceEvalResult>(
        *state_, diag_codes::kCoreInvalidHandle,
        "曲面求值失败：目标曲面不存在", "曲面求值失败");
  }

  SurfaceEvalResult result{};
  const auto &surface = it->second;
  const auto frame = make_frame_from_axis(surface.normal);
  switch (surface.kind) {
  case detail::SurfaceKind::Plane:
    result.point = point_from_local(surface.origin, frame, u, v, 0.0);
    result.du = frame.u;
    result.dv = frame.v;
    result.normal = frame.w;
    result.k1 = 0.0;
    result.k2 = 0.0;
    break;
  case detail::SurfaceKind::Sphere:
    result.point =
        Point3{surface.origin.x + surface.radius_a * std::cos(u) * std::sin(v),
               surface.origin.y + surface.radius_a * std::sin(u) * std::sin(v),
               surface.origin.z + surface.radius_a * std::cos(v)};
    result.normal =
        detail::normalize(detail::subtract(result.point, surface.origin));
    result.k1 = surface.radius_a > 0.0 ? 1.0 / surface.radius_a : 0.0;
    result.k2 = result.k1;
    break;
  case detail::SurfaceKind::Cylinder:
    result.point = point_from_local(surface.origin, frame,
                                    surface.radius_a * std::cos(u),
                                    surface.radius_a * std::sin(u), v);
    result.du = vec_from_local(frame, -surface.radius_a * std::sin(u),
                               surface.radius_a * std::cos(u), 0.0);
    result.dv = frame.w;
    result.normal =
        vec_from_local(frame, std::cos(u), std::sin(u), 0.0);
    result.k1 = surface.radius_a > 0.0 ? 1.0 / surface.radius_a : 0.0;
    result.k2 = 0.0;
    break;
  case detail::SurfaceKind::Cone: {
    const auto slope = std::tan(surface.semi_angle);
    const auto radial = std::tan(surface.semi_angle) * v;
    const auto radial_dir =
        vec_from_local(frame, std::cos(u), std::sin(u), 0.0);
    result.point = point_from_local(surface.origin, frame, radial * std::cos(u),
                                    radial * std::sin(u), v);
    result.du =
        vec_from_local(frame, -radial * std::sin(u), radial * std::cos(u), 0.0);
    result.dv = vec_from_local(frame, slope * std::cos(u), slope * std::sin(u),
                               1.0);
    result.normal = normalize_or_default(detail::cross(result.du, result.dv),
                                         radial_dir);
    if (detail::dot(result.normal, radial_dir) < 0.0) {
      result.normal = detail::scale(result.normal, -1.0);
    }
    result.k1 = 0.0;
    result.k2 = 0.0;
    break;
  }
  case detail::SurfaceKind::Torus: {
    const auto ring_radius = surface.radius_a + surface.radius_b * std::cos(v);
    result.point = point_from_local(surface.origin, frame,
                                    ring_radius * std::cos(u),
                                    ring_radius * std::sin(u),
                                    surface.radius_b * std::sin(v));
    result.du = vec_from_local(frame, -ring_radius * std::sin(u),
                               ring_radius * std::cos(u), 0.0);
    result.dv = vec_from_local(frame, -surface.radius_b * std::sin(v) * std::cos(u),
                               -surface.radius_b * std::sin(v) * std::sin(u),
                               surface.radius_b * std::cos(v));
    result.normal = normalize_or_default(
        vec_from_local(frame, std::cos(v) * std::cos(u),
                       std::cos(v) * std::sin(u), std::sin(v)),
        frame.u);
    result.k1 = surface.radius_b > 0.0 ? 1.0 / surface.radius_b : 0.0;
    result.k2 = surface.radius_a > 0.0 ? 1.0 / surface.radius_a : 0.0;
    break;
  }
  case detail::SurfaceKind::BSpline:
  case detail::SurfaceKind::Nurbs: {
    const auto domain = surface_domain(surface);
    const auto eval_fn = [&surface](Scalar uu, Scalar vv) {
      return evaluate_surface_point(surface, uu, vv);
    };
    const auto cu = std::clamp(u, domain.u.min, domain.u.max);
    const auto cv = std::clamp(v, domain.v.min, domain.v.max);
    const auto step_u = std::max((domain.u.max - domain.u.min) * 1e-4, 1e-6);
    const auto step_v = std::max((domain.v.max - domain.v.min) * 1e-4, 1e-6);
    const auto u0 = std::clamp(cu - step_u, domain.u.min, domain.u.max);
    const auto u1 = std::clamp(cu + step_u, domain.u.min, domain.u.max);
    const auto v0 = std::clamp(cv - step_v, domain.v.min, domain.v.max);
    const auto v1 = std::clamp(cv + step_v, domain.v.min, domain.v.max);
    result.point = eval_fn(cu, cv);
    result.du = normalize_or_default(detail::subtract(eval_fn(u1, cv), eval_fn(u0, cv)),
                                     Vec3{1.0, 0.0, 0.0});
    result.dv = normalize_or_default(detail::subtract(eval_fn(cu, v1), eval_fn(cu, v0)),
                                     Vec3{0.0, 1.0, 0.0});
    result.normal = normalize_or_default(detail::cross(result.du, result.dv),
                                         Vec3{0.0, 0.0, 1.0});
    result.k1 = 0.0;
    result.k2 = 0.0;
    break;
  }
  default:
    result.point = surface.origin;
    result.normal = {0.0, 0.0, 1.0};
    result.k1 = 0.0;
    result.k2 = 0.0;
    break;
  }

  result.normal = normalize_or_default(result.normal,
                                       normalize_or_default(detail::cross(result.du, result.dv),
                                                            Vec3{0.0, 0.0, 1.0}));
  if (state_->config.enable_cache) {
    state_->surface_eval_cache.emplace(
        surface_eval_cache_key(surface_id, u, v, deriv_order), result);
  }
  return ok_result(result, state_->create_diagnostic("曲面求值完成"));
}

Result<std::vector<SurfaceEvalResult>> SurfaceService::eval_batch(
    SurfaceId surface_id, std::span<const std::pair<Scalar, Scalar>> uvs,
    int deriv_order) const {
  if (uvs.empty()) {
    return detail::invalid_input_result<std::vector<SurfaceEvalResult>>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲面批量求值失败：参数集合为空", "曲面批量求值失败");
  }
  std::vector<SurfaceEvalResult> out;
  out.reserve(uvs.size());
  for (const auto &[u, v] : uvs) {
    const auto single = eval(surface_id, u, v, deriv_order);
    if (single.status != StatusCode::Ok || !single.value.has_value()) {
      return error_result<std::vector<SurfaceEvalResult>>(single.status,
                                                          single.diagnostic_id);
    }
    out.push_back(*single.value);
  }
  return ok_result(std::move(out), state_->create_diagnostic("曲面批量求值完成"));
}

Result<Point3> SurfaceService::closest_point(SurfaceId surface_id,
                                             const Point3 &point) const {
  if (!finite_point(point)) {
    return detail::invalid_input_result<Point3>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲面最近点求解失败：输入点包含非法数值", "曲面最近点求解失败");
  }
  const auto it = state_->surfaces.find(surface_id.value);
  if (it == state_->surfaces.end()) {
    return detail::failed_result<Point3>(
        *state_, StatusCode::InvalidInput, diag_codes::kGeoClosestPointFailure,
        "曲面最近点求解失败：目标曲面不存在", "曲面最近点求解失败");
  }
  const auto &surface = it->second;
  const auto frame = make_frame_from_axis(surface.normal);
  if (surface.kind == detail::SurfaceKind::Plane) {
    const auto rel = detail::subtract(point, surface.origin);
    const auto local = to_local(rel, frame);
    return ok_result(point_from_local(surface.origin, frame, local[0], local[1], 0.0),
                     state_->create_diagnostic("已求出曲面最近点"));
  }
  if (surface.kind == detail::SurfaceKind::Sphere) {
    const auto dir =
        normalize_or_default(detail::subtract(point, surface.origin), surface.normal);
    return ok_result(detail::add_point_vec(surface.origin,
                                           detail::scale(dir, surface.radius_a)),
                     state_->create_diagnostic("已求出曲面最近点"));
  }
  if (surface.kind == detail::SurfaceKind::Cylinder) {
    const auto rel = detail::subtract(point, surface.origin);
    const auto local = to_local(rel, frame);
    const auto planar_len = std::hypot(local[0], local[1]);
    const auto cos_u = planar_len > kEpsilon ? local[0] / planar_len : 1.0;
    const auto sin_u = planar_len > kEpsilon ? local[1] / planar_len : 0.0;
    return ok_result(point_from_local(surface.origin, frame,
                                      surface.radius_a * cos_u,
                                      surface.radius_a * sin_u, local[2]),
                     state_->create_diagnostic("已求出曲面最近点"));
  }
  if (surface.kind == detail::SurfaceKind::Cone) {
    const auto rel = detail::subtract(point, surface.origin);
    const auto local = to_local(rel, frame);
    const auto planar_len = std::hypot(local[0], local[1]);
    const auto slope = std::tan(surface.semi_angle);
    if (!std::isfinite(slope) || slope <= kEpsilon) {
      return ok_result(surface.origin,
                       state_->create_diagnostic("已求出曲面最近点"));
    }
    const auto axial = std::max(0.0, (local[2] + slope * planar_len) /
                                         (1.0 + slope * slope));
    const auto radial = axial * slope;
    const auto radial_dir = planar_len > kEpsilon
                                ? vec_from_local(frame, local[0] / planar_len,
                                                 local[1] / planar_len, 0.0)
                                : frame.u;
    return ok_result(detail::add_point_vec(
                         detail::add_point_vec(surface.origin,
                                               detail::scale(frame.w, axial)),
                         detail::scale(radial_dir, radial)),
                     state_->create_diagnostic("已求出曲面最近点"));
  }
  if (surface.kind == detail::SurfaceKind::Torus) {
    const auto rel = detail::subtract(point, surface.origin);
    const auto local = to_local(rel, frame);
    const auto planar_len = std::hypot(local[0], local[1]);
    const auto cos_u = planar_len > kEpsilon ? local[0] / planar_len : 1.0;
    const auto sin_u = planar_len > kEpsilon ? local[1] / planar_len : 0.0;
    const auto dx = planar_len - surface.radius_a;
    const auto dz = local[2];
    const auto dist = std::hypot(dx, dz);
    if (surface.radius_b <= kEpsilon || !std::isfinite(dist)) {
      return ok_result(surface.origin,
                       state_->create_diagnostic("已求出曲面最近点"));
    }
    const auto scale = surface.radius_b / std::max(dist, kEpsilon);
    return ok_result(point_from_local(surface.origin, frame,
                                      cos_u * (surface.radius_a + dx * scale),
                                      sin_u * (surface.radius_a + dx * scale),
                                      dz * scale),
                     state_->create_diagnostic("已求出曲面最近点"));
  }
  if (surface.kind == detail::SurfaceKind::BSpline ||
      surface.kind == detail::SurfaceKind::Nurbs) {
    const auto uv = approximate_surface_uv(surface, point);
    const auto eval_result = eval(surface_id, uv.first, uv.second, 1);
    if (eval_result.status != StatusCode::Ok || !eval_result.value.has_value()) {
      return detail::failed_result<Point3>(
          *state_, StatusCode::OperationFailed, diag_codes::kGeoClosestPointFailure,
          "曲面最近点求解失败：样条曲面求值不可用", "曲面最近点求解失败");
    }
    return ok_result(eval_result.value->point,
                     state_->create_diagnostic("已求出曲面最近点"));
  }
  return ok_result(surface.origin, state_->create_diagnostic("已求出曲面最近点"));
}

Result<std::vector<Point3>>
SurfaceService::closest_points_batch(SurfaceId surface_id,
                                     std::span<const Point3> points) const {
  if (points.empty()) {
    return detail::invalid_input_result<std::vector<Point3>>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲面批量最近点求解失败：输入点集合为空", "曲面批量最近点求解失败");
  }
  std::vector<Point3> out;
  out.reserve(points.size());
  for (const auto &point : points) {
    const auto single = closest_point(surface_id, point);
    if (single.status != StatusCode::Ok || !single.value.has_value()) {
      return error_result<std::vector<Point3>>(single.status,
                                               single.diagnostic_id);
    }
    out.push_back(*single.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("曲面批量最近点已求解"));
}

Result<std::pair<Scalar, Scalar>>
SurfaceService::closest_uv(SurfaceId surface_id, const Point3 &point) const {
  if (!finite_point(point)) {
    return detail::invalid_input_result<std::pair<Scalar, Scalar>>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲面参数反求失败：输入点包含非法数值", "曲面参数反求失败");
  }
  if (!detail::has_surface(*state_, surface_id)) {
    return detail::failed_result<std::pair<Scalar, Scalar>>(
        *state_, StatusCode::InvalidInput, diag_codes::kGeoParameterSolveFailure,
        "曲面参数反求失败：目标曲面不存在", "曲面参数反求失败");
  }

  const auto &surface = state_->surfaces.at(surface_id.value);
  const auto frame = make_frame_from_axis(surface.normal);
  switch (surface.kind) {
  case detail::SurfaceKind::Plane: {
    const auto local = to_local(detail::subtract(point, surface.origin), frame);
    return ok_result(std::make_pair(local[0], local[1]),
                     state_->create_diagnostic("已求出曲面最近参数"));
  }
  case detail::SurfaceKind::Sphere: {
    const auto rel = detail::subtract(point, surface.origin);
    if (detail::norm(rel) <= kEpsilon) {
      return ok_result(std::make_pair(0.0, 0.0),
                       state_->create_diagnostic("已求出曲面最近参数"));
    }
    const auto radius = std::max(surface.radius_a, 1e-12);
    const auto u = normalize_angle_0_2pi(std::atan2(rel.y, rel.x));
    const auto cos_v = std::clamp(rel.z / radius, -1.0, 1.0);
    const auto v = std::acos(cos_v);
    return ok_result(std::make_pair(u, v),
                     state_->create_diagnostic("已求出曲面最近参数"));
  }
  case detail::SurfaceKind::Cylinder: {
    const auto local = to_local(detail::subtract(point, surface.origin), frame);
    const auto planar_len = std::hypot(local[0], local[1]);
    const auto u = planar_len <= kEpsilon
                       ? 0.0
                       : normalize_angle_0_2pi(std::atan2(local[1], local[0]));
    return ok_result(std::make_pair(u, local[2]),
                     state_->create_diagnostic("已求出曲面最近参数"));
  }
  case detail::SurfaceKind::Cone: {
    const auto local = to_local(detail::subtract(point, surface.origin), frame);
    const auto axial =
        std::max(0.0, (local[2] + std::tan(surface.semi_angle) *
                                      std::hypot(local[0], local[1])) /
                             (1.0 + std::tan(surface.semi_angle) *
                                        std::tan(surface.semi_angle)));
    return ok_result(std::make_pair(normalize_angle_0_2pi(std::atan2(local[1], local[0])), axial),
                     state_->create_diagnostic("已求出曲面最近参数"));
  }
  case detail::SurfaceKind::Torus: {
    const auto local = to_local(detail::subtract(point, surface.origin), frame);
    const auto planar_len = std::hypot(local[0], local[1]);
    const auto u = planar_len <= kEpsilon
                       ? 0.0
                       : normalize_angle_0_2pi(std::atan2(local[1], local[0]));
    const auto v = normalize_angle_0_2pi(std::atan2(local[2], planar_len - surface.radius_a));
    return ok_result(std::make_pair(u, v),
                     state_->create_diagnostic("已求出曲面最近参数"));
  }
  case detail::SurfaceKind::BSpline:
  case detail::SurfaceKind::Nurbs:
  {
    const auto domain = surface_domain(surface);
    const auto uv = approximate_surface_uv(surface, point);
    return ok_result(std::make_pair(std::clamp(uv.first, domain.u.min, domain.u.max),
                                    std::clamp(uv.second, domain.v.min, domain.v.max)),
                     state_->create_diagnostic("已求出曲面最近参数"));
  }
  default:
    break;
  }

  return ok_result(std::make_pair(0.0, 0.0),
                   state_->create_diagnostic("已求出曲面最近参数"));
}

Result<std::vector<std::pair<Scalar, Scalar>>>
SurfaceService::closest_uv_batch(SurfaceId surface_id,
                                 std::span<const Point3> points) const {
  if (points.empty()) {
    return detail::invalid_input_result<std::vector<std::pair<Scalar, Scalar>>>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲面批量最近参数求解失败：输入点集合为空",
        "曲面批量最近参数求解失败");
  }
  std::vector<std::pair<Scalar, Scalar>> out;
  out.reserve(points.size());
  for (const auto &point : points) {
    const auto single = closest_uv(surface_id, point);
    if (single.status != StatusCode::Ok || !single.value.has_value()) {
      return error_result<std::vector<std::pair<Scalar, Scalar>>>(
          single.status, single.diagnostic_id);
    }
    out.push_back(*single.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("曲面批量最近参数已求解"));
}

Result<Range2D> SurfaceService::domain(SurfaceId surface_id) const {
  if (!detail::has_surface(*state_, surface_id)) {
    return detail::invalid_input_result<Range2D>(
        *state_, diag_codes::kCoreInvalidHandle,
        "曲面定义域查询失败：目标曲面不存在", "曲面定义域查询失败");
  }
  return ok_result(surface_domain(state_->surfaces.at(surface_id.value)),
                   state_->create_diagnostic("已返回曲面定义域"));
}

Result<BoundingBox> SurfaceService::bbox(SurfaceId surface_id) const {
  const auto it = state_->surfaces.find(surface_id.value);
  if (it == state_->surfaces.end()) {
    return detail::invalid_input_result<BoundingBox>(
        *state_, diag_codes::kCoreInvalidHandle,
        "曲面包围盒查询失败：目标曲面不存在", "曲面包围盒查询失败");
  }
  return ok_result(make_surface_bbox(it->second),
                   state_->create_diagnostic("已返回曲面包围盒"));
}

Result<std::vector<BoundingBox>>
SurfaceService::bbox_batch(std::span<const SurfaceId> surface_ids) const {
  if (surface_ids.empty()) {
    return detail::invalid_input_result<std::vector<BoundingBox>>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲面批量包围盒查询失败：输入曲面集合为空", "曲面批量包围盒查询失败");
  }
  std::vector<BoundingBox> out;
  out.reserve(surface_ids.size());
  for (const auto surface_id : surface_ids) {
    const auto single = bbox(surface_id);
    if (single.status != StatusCode::Ok || !single.value.has_value()) {
      return error_result<std::vector<BoundingBox>>(single.status,
                                                    single.diagnostic_id);
    }
    out.push_back(*single.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已返回曲面批量包围盒"));
}

GeometryTransformService::GeometryTransformService(
    std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

Result<CurveId> GeometryTransformService::transform_curve(
    CurveId curve_id, const Transform3 &transform) {
  const auto it = state_->curves.find(curve_id.value);
  if (it == state_->curves.end()) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kCoreInvalidHandle,
        "曲线变换失败：目标曲线不存在", "曲线变换失败");
  }
  auto record = it->second;
  record.origin = transform_point(record.origin, transform);
  record.direction = normalize_or_default(transform_vec(record.direction, transform),
                                          Vec3{1.0, 0.0, 0.0});
  record.normal =
      normalize_or_default(transform_vec(record.normal, transform), record.normal);
  record.axis_u =
      normalize_or_default(transform_vec(record.axis_u, transform), record.axis_u);
  record.axis_v =
      normalize_or_default(transform_vec(record.axis_v, transform), record.axis_v);
  for (auto &p : record.poles) {
    p = transform_point(p, transform);
  }
  const auto id = CurveId{state_->allocate_id()};
  state_->curves.emplace(id.value, std::move(record));
  return ok_result(id, state_->create_diagnostic("已完成曲线变换"));
}

Result<SurfaceId> GeometryTransformService::transform_surface(
    SurfaceId surface_id, const Transform3 &transform) {
  const auto it = state_->surfaces.find(surface_id.value);
  if (it == state_->surfaces.end()) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kCoreInvalidHandle,
        "曲面变换失败：目标曲面不存在", "曲面变换失败");
  }
  auto record = it->second;
  record.origin = transform_point(record.origin, transform);
  record.axis = normalize_or_default(transform_vec(record.axis, transform), record.axis);
  record.normal =
      normalize_or_default(transform_vec(record.normal, transform), record.normal);
  for (auto &p : record.poles) {
    p = transform_point(p, transform);
  }
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(id.value, std::move(record));
  return ok_result(id, state_->create_diagnostic("已完成曲面变换"));
}

} // namespace axiom
