#include "axiom/geo/geometry_services.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <sstream>

#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"

namespace axiom {

namespace detail {
namespace {
struct Vec4 {
  Scalar x{}, y{}, z{}, w{};
};

Vec4 lerp4(const Vec4 &a, const Vec4 &b, Scalar s) {
  return Vec4{a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s,
              a.z + (b.z - a.z) * s, a.w + (b.w - a.w) * s};
}

Vec4 de_casteljau4(std::vector<Vec4> cps, Scalar t) {
  if (cps.empty()) return Vec4{};
  while (cps.size() > 1) {
    for (std::size_t i = 0; i + 1 < cps.size(); ++i) {
      cps[i] = lerp4(cps[i], cps[i + 1], t);
    }
    cps.pop_back();
  }
  return cps.front();
}
}  // namespace

void rational_bezier_eval_all(const std::vector<Point3> &poles,
                              const std::vector<Scalar> &weights_in, Scalar t,
                              Point3 &out_p, Vec3 &out_d1, Vec3 &out_d2) {
  out_p = Point3{0.0, 0.0, 0.0};
  out_d1 = Vec3{0.0, 0.0, 0.0};
  out_d2 = Vec3{0.0, 0.0, 0.0};
  if (poles.empty()) return;
  if (poles.size() != weights_in.size() || poles.size() < 2) {
    out_p = poles.front();
    return;
  }

  const auto clamp01 = [](Scalar v) {
    if (v < 0.0) return Scalar(0.0);
    if (v > 1.0) return Scalar(1.0);
    return v;
  };
  const auto tt = clamp01(t);

  std::vector<Vec4> cps;
  cps.reserve(poles.size());
  for (std::size_t i = 0; i < poles.size(); ++i) {
    const auto w = weights_in[i];
    cps.push_back(Vec4{poles[i].x * w, poles[i].y * w, poles[i].z * w, w});
  }

  const auto eval_point = [&](Scalar param) -> Point3 {
    const auto h = de_casteljau4(cps, param);
    const auto ww = std::abs(h.w) > 1e-18 ? h.w : 1e-18;
    return Point3{h.x / ww, h.y / ww, h.z / ww};
  };

  out_p = eval_point(tt);

  // Finite-difference derivatives (engineering grade placeholder for Stage 2/3).
  const auto h = std::max<Scalar>(1e-6, 1e-4 * (Scalar(1.0) - std::abs(tt - 0.5)));
  const auto t0 = clamp01(tt - h);
  const auto t1 = clamp01(tt + h);
  const auto tm = tt;
  const auto p0 = eval_point(t0);
  const auto p1 = eval_point(t1);
  out_d1 = Vec3{(p1.x - p0.x) / (t1 - t0), (p1.y - p0.y) / (t1 - t0),
                (p1.z - p0.z) / (t1 - t0)};

  const auto hm = std::max<Scalar>(1e-4, 10.0 * h);
  const auto ta = clamp01(tm - hm);
  const auto tb = tm;
  const auto tc = clamp01(tm + hm);
  const auto pa = eval_point(ta);
  const auto pb = eval_point(tb);
  const auto pc = eval_point(tc);
  const auto denom = (tc - ta) * 0.5;
  if (denom > 1e-18) {
    // second derivative approx from central difference on first derivative
    const Vec3 d_left{(pb.x - pa.x) / (tb - ta + 1e-18),
                      (pb.y - pa.y) / (tb - ta + 1e-18),
                      (pb.z - pa.z) / (tb - ta + 1e-18)};
    const Vec3 d_right{(pc.x - pb.x) / (tc - tb + 1e-18),
                       (pc.y - pb.y) / (tc - tb + 1e-18),
                       (pc.z - pb.z) / (tc - tb + 1e-18)};
    out_d2 = Vec3{(d_right.x - d_left.x) / denom,
                  (d_right.y - d_left.y) / denom,
                  (d_right.z - d_left.z) / denom};
  }
}
}  // namespace detail

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

// Rodrigues rotation: rotate vector `v` around unit axis `k` by angle `theta`.
Vec3 rotate_vec_around_axis(const Vec3 &k, const Vec3 &v, Scalar theta) {
  const auto c = std::cos(theta);
  const auto s = std::sin(theta);
  const auto omc = 1.0 - c;
  const auto kdotv = detail::dot(k, v);
  const auto kxv = detail::cross(k, v);
  return Vec3{v.x * c + kxv.x * s + k.x * kdotv * omc,
              v.y * c + kxv.y * s + k.y * kdotv * omc,
              v.z * c + kxv.z * s + k.z * kdotv * omc};
}

bool solve_2x2(Scalar a00, Scalar a01, Scalar a10, Scalar a11,
               Scalar b0, Scalar b1, Scalar &x0, Scalar &x1) {
  const auto det = a00 * a11 - a01 * a10;
  if (!std::isfinite(det) || std::abs(det) <= kEpsilon) {
    return false;
  }
  x0 = (b0 * a11 - a01 * b1) / det;
  x1 = (a00 * b1 - b0 * a10) / det;
  return std::isfinite(x0) && std::isfinite(x1);
}

Scalar squared_distance(const Point3& a, const Point3& b) {
  const auto dx = a.x - b.x;
  const auto dy = a.y - b.y;
  const auto dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz;
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

Vec3 polyline_first_derivative(const std::vector<Point3> &poles, Scalar t) {
  if (poles.size() < 2) {
    return {0.0, 0.0, 0.0};
  }
  const auto max_t = static_cast<Scalar>(poles.size() - 1);
  const auto ct = std::clamp(t, 0.0, max_t);
  const auto max_seg_index = poles.size() - 2;
  const auto segment = static_cast<std::size_t>(
      std::min<Scalar>(std::floor(ct), static_cast<Scalar>(max_seg_index)));
  return detail::subtract(poles[segment + 1], poles[segment]);
}

void bernstein_values(int n, Scalar t, std::vector<Scalar> &B) {
  if (n < 0) {
    B.clear();
    return;
  }
  B.assign(static_cast<std::size_t>(n) + 1, 0.0);
  t = std::clamp(t, 0.0, 1.0);
  for (int i = 0; i <= n; ++i) {
    Scalar binom = 1.0;
    for (int j = 1; j <= i; ++j) {
      binom *= static_cast<Scalar>(n - j + 1) / static_cast<Scalar>(j);
    }
    B[static_cast<std::size_t>(i)] =
        binom * std::pow(1.0 - t, static_cast<Scalar>(n - i)) *
        std::pow(t, static_cast<Scalar>(i));
  }
}

void bernstein_first_deriv(int n, Scalar t, std::vector<Scalar> &Bp) {
  Bp.assign(static_cast<std::size_t>(n) + 1, 0.0);
  if (n <= 0) {
    return;
  }
  std::vector<Scalar> b_nm1;
  bernstein_values(n - 1, t, b_nm1);
  const Scalar nn = static_cast<Scalar>(n);
  for (int i = 0; i <= n; ++i) {
    const Scalar left = (i == 0) ? 0.0 : b_nm1[static_cast<std::size_t>(i - 1)];
    const Scalar right = (i == n) ? 0.0 : b_nm1[static_cast<std::size_t>(i)];
    Bp[static_cast<std::size_t>(i)] = nn * (left - right);
  }
}

void bernstein_second_deriv(int n, Scalar t, std::vector<Scalar> &Bpp) {
  Bpp.assign(static_cast<std::size_t>(n) + 1, 0.0);
  if (n <= 1) {
    return;
  }
  std::vector<Scalar> bp_nm1;
  bernstein_first_deriv(n - 1, t, bp_nm1);
  const Scalar nn = static_cast<Scalar>(n);
  for (int i = 0; i <= n; ++i) {
    const Scalar left = (i == 0) ? 0.0 : bp_nm1[static_cast<std::size_t>(i - 1)];
    const Scalar right = (i == n) ? 0.0 : bp_nm1[static_cast<std::size_t>(i)];
    Bpp[static_cast<std::size_t>(i)] = nn * (left - right);
  }
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

Point3 de_casteljau_point(std::vector<Point3> pts, Scalar t) {
  if (pts.empty()) {
    return {};
  }
  if (pts.size() == 1) {
    return pts.front();
  }
  const auto ct = std::clamp(t, 0.0, 1.0);
  for (std::size_t k = pts.size(); k > 1; --k) {
    for (std::size_t i = 0; i + 1 < k; ++i) {
      pts[i] = lerp_point(pts[i], pts[i + 1], ct);
    }
  }
  return pts.front();
}

Vec3 bezier_first_derivative(std::span<const Point3> poles, Scalar t) {
  if (poles.size() < 2) {
    return {0.0, 0.0, 0.0};
  }
  std::vector<Point3> dp;
  dp.reserve(poles.size() - 1);
  const auto n = static_cast<Scalar>(poles.size() - 1);
  for (std::size_t i = 0; i + 1 < poles.size(); ++i) {
    const auto d = detail::subtract(poles[i + 1], poles[i]);
    dp.push_back(detail::add_point_vec(Point3{}, detail::scale(d, n)));
  }
  const auto p = de_casteljau_point(std::move(dp), t);
  return Vec3{p.x, p.y, p.z};
}

Vec3 bezier_second_derivative(std::span<const Point3> poles, Scalar t) {
  if (poles.size() < 3) {
    return {0.0, 0.0, 0.0};
  }
  std::vector<Point3> ddp;
  ddp.reserve(poles.size() - 2);
  const auto n = static_cast<Scalar>(poles.size() - 1);
  const auto scale = n * (n - 1.0);
  for (std::size_t i = 0; i + 2 < poles.size(); ++i) {
    // P_{i+2} - 2 P_{i+1} + P_i
    const Vec3 v = Vec3{
        poles[i + 2].x - 2.0 * poles[i + 1].x + poles[i].x,
        poles[i + 2].y - 2.0 * poles[i + 1].y + poles[i].y,
        poles[i + 2].z - 2.0 * poles[i + 1].z + poles[i].z,
    };
    ddp.push_back(detail::add_point_vec(Point3{}, detail::scale(v, scale)));
  }
  const auto p = de_casteljau_point(std::move(ddp), t);
  return Vec3{p.x, p.y, p.z};
}

Range1D curve_domain(const detail::CurveRecord &curve) {
  const auto inf = std::numeric_limits<Scalar>::infinity();
  const auto two_pi = std::acos(-1.0) * 2.0;
  switch (curve.kind) {
  case detail::CurveKind::Line:
    return Range1D{-inf, inf};
  case detail::CurveKind::LineSegment:
    return Range1D{0.0, 1.0};
  case detail::CurveKind::Circle:
  case detail::CurveKind::Ellipse:
    return Range1D{0.0, two_pi};
  case detail::CurveKind::Parabola:
  case detail::CurveKind::Hyperbola:
    return Range1D{-10.0, 10.0};
  case detail::CurveKind::BSpline:
  case detail::CurveKind::CompositePolyline:
    return Range1D{0.0, std::max<Scalar>(1.0, static_cast<Scalar>(curve.poles.size() - 1))};
  case detail::CurveKind::CompositeChain:
    return Range1D{0.0, std::max<Scalar>(1.0, static_cast<Scalar>(curve.children.size()))};
  case detail::CurveKind::Bezier:
  case detail::CurveKind::Nurbs:
  default:
    return Range1D{0.0, 1.0};
  }
}

Point3 evaluate_curve_point(const detail::CurveRecord &curve, Scalar t) {
  switch (curve.kind) {
  case detail::CurveKind::LineSegment: {
    if (curve.poles.size() >= 2) {
      return lerp_point(curve.poles.front(), curve.poles.back(),
                        std::clamp(t, 0.0, 1.0));
    }
    return curve.origin;
  }
  case detail::CurveKind::Parabola: {
    const auto p = std::max(curve.param_a, 1e-9);
    const auto x = t;
    const auto y = (x * x) / (4.0 * p);
    return Point3{curve.origin.x + curve.axis_u.x * x + curve.axis_v.x * y,
                  curve.origin.y + curve.axis_u.y * x + curve.axis_v.y * y,
                  curve.origin.z + curve.axis_u.z * x + curve.axis_v.z * y};
  }
  case detail::CurveKind::Hyperbola: {
    const auto a = std::max(curve.param_a, 1e-9);
    const auto b = std::max(curve.param_b, 1e-9);
    const auto x = a * std::cosh(t);
    const auto y = b * std::sinh(t);
    return Point3{curve.origin.x + curve.axis_u.x * x + curve.axis_v.x * y,
                  curve.origin.y + curve.axis_u.y * x + curve.axis_v.y * y,
                  curve.origin.z + curve.axis_u.z * x + curve.axis_v.z * y};
  }
  case detail::CurveKind::Bezier:
    return evaluate_bezier_point(std::vector<Point3>(curve.poles.begin(), curve.poles.end()), t);
  case detail::CurveKind::BSpline:
  case detail::CurveKind::CompositePolyline:
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
  case detail::SurfaceKind::Bezier:
    return Range2D{Range1D{0.0, 1.0}, Range1D{0.0, 1.0}};
  case detail::SurfaceKind::BSpline:
  case detail::SurfaceKind::Nurbs: {
    const auto [rows, cols] = infer_surface_grid_dims(surface.poles.size());
    return Range2D{
        Range1D{0.0, std::max<Scalar>(1.0, static_cast<Scalar>(rows - 1))},
        Range1D{0.0, std::max<Scalar>(1.0, static_cast<Scalar>(cols - 1))}};
  }
  case detail::SurfaceKind::Revolved:
    return Range2D{Range1D{0.0, std::max(surface.sweep_angle_rad, 0.0)},
                   Range1D{0.0, 1.0}};
  case detail::SurfaceKind::Swept:
    return Range2D{Range1D{0.0, 1.0}, Range1D{0.0, 1.0}};
  case detail::SurfaceKind::Trimmed:
    return Range2D{Range1D{surface.trim_u_min, surface.trim_u_max},
                   Range1D{surface.trim_v_min, surface.trim_v_max}};
  case detail::SurfaceKind::Offset:
    // Domain is inherited from base surface; fallback to [0,1] if missing.
    return Range2D{Range1D{0.0, 1.0}, Range1D{0.0, 1.0}};
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
  if (surface.kind == detail::SurfaceKind::Bezier) {
    // Minimal Bezier surface support: fallback to bilinear over the inferred grid
    // (industrial-grade implementation will use tensor-product de Casteljau).
    const auto domain = surface_domain(surface);
    const auto cu = std::clamp(u, domain.u.min, domain.u.max);
    const auto cv = std::clamp(v, domain.v.min, domain.v.max);
    const auto ru =
        std::min<std::size_t>(static_cast<std::size_t>(std::floor(cu * (rows - 1))), rows - 2);
    const auto rv =
        std::min<std::size_t>(static_cast<std::size_t>(std::floor(cv * (cols - 1))), cols - 2);
    const auto lu = cu * (rows - 1) - static_cast<Scalar>(ru);
    const auto lv = cv * (cols - 1) - static_cast<Scalar>(rv);
    const auto idx = [cols](std::size_t r, std::size_t c) { return r * cols + c; };
    const auto &p00 = surface.poles[idx(ru, rv)];
    const auto &p01 = surface.poles[idx(ru, rv + 1)];
    const auto &p10 = surface.poles[idx(ru + 1, rv)];
    const auto &p11 = surface.poles[idx(ru + 1, rv + 1)];
    return bilinear_point(p00, p01, p10, p11, lu, lv);
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
  case detail::CurveKind::LineSegment: {
    if (curve.poles.size() >= 2) {
      const auto &a = curve.poles.front();
      const auto &b = curve.poles.back();
      const Point3 min{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
      const Point3 max{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
      return detail::make_bbox(min, max);
    }
    return detail::bbox_from_center_radius(curve.origin, 1.0, 1.0, 1.0);
  }
  case detail::CurveKind::Circle:
    return bbox_from_span_vectors(
        curve.origin, detail::scale(curve.axis_u, curve.radius),
        detail::scale(curve.axis_v, curve.radius));
  case detail::CurveKind::Ellipse:
    return bbox_from_span_vectors(curve.origin, curve.axis_u, curve.axis_v);
  case detail::CurveKind::Parabola:
  case detail::CurveKind::Hyperbola:
    return detail::bbox_from_center_radius(curve.origin, 1000.0, 1000.0, 1000.0);
  case detail::CurveKind::Bezier:
  case detail::CurveKind::BSpline:
  case detail::CurveKind::Nurbs:
  case detail::CurveKind::CompositePolyline: {
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
  case detail::SurfaceKind::Bezier: {
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
  case detail::SurfaceKind::Revolved:
  case detail::SurfaceKind::Swept:
  case detail::SurfaceKind::Trimmed:
  case detail::SurfaceKind::Offset:
    return detail::bbox_from_center_radius(surface.origin, 50.0, 50.0, 50.0);
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
                                      std::vector<Scalar> knots_u = {},
                                      Scalar param_a = 0.0,
                                      Scalar param_b = 0.0,
                                      std::vector<CurveId> children = {}) {
  detail::CurveRecord record;
  record.kind = kind;
  record.origin = origin;
  record.direction = direction;
  record.radius = radius;
  record.param_a = param_a;
  record.param_b = param_b;
  record.normal = normal;
  record.axis_u = axis_u;
  record.axis_v = axis_v;
  record.poles = std::move(poles);
  record.weights = std::move(weights);
  record.knots_u = std::move(knots_u);
  record.children = std::move(children);
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
                                          std::vector<Scalar> knots_v = {},
                                          SurfaceId base_surface_id = {},
                                          CurveId profile_curve_id = {},
                                          Scalar sweep_angle_rad = 0.0,
                                          Scalar sweep_length = 0.0,
                                          Scalar trim_u_min = 0.0,
                                          Scalar trim_u_max = 1.0,
                                          Scalar trim_v_min = 0.0,
                                          Scalar trim_v_max = 1.0,
                                          Scalar offset_distance = 0.0) {
  detail::SurfaceRecord record;
  record.kind = kind;
  record.origin = origin;
  record.axis = axis;
  record.normal = normal;
  record.radius_a = radius_a;
  record.radius_b = radius_b;
  record.semi_angle = semi_angle;
  record.base_surface_id = base_surface_id;
  record.profile_curve_id = profile_curve_id;
  record.sweep_angle_rad = sweep_angle_rad;
  record.sweep_length = sweep_length;
  record.trim_u_min = trim_u_min;
  record.trim_u_max = trim_u_max;
  record.trim_v_min = trim_v_min;
  record.trim_v_max = trim_v_max;
  record.offset_distance = offset_distance;
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

Result<CurveId> CurveFactory::make_line_segment(const Point3 &a, const Point3 &b) {
  if (!finite_point(a) || !finite_point(b)) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "线段创建失败：输入包含非法数值", "线段创建失败");
  }
  if (detail::norm(detail::subtract(b, a)) <= kEpsilon) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "线段创建失败：端点不能重合", "线段创建失败");
  }
  const auto id = CurveId{state_->allocate_id()};
  state_->curves.emplace(
      id.value,
      make_curve_record(detail::CurveKind::LineSegment, a, {1.0, 0.0, 0.0}, 0.0,
                        {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
                        std::vector<Point3>{a, b}));
  return ok_result(id, state_->create_diagnostic("已创建线段"));
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

Result<CurveId> CurveFactory::make_parabola(const Point3 &origin, const Vec3 &axis_u,
                                            const Vec3 &axis_v, Scalar focal_param) {
  if (!finite_point(origin) || !finite_vec(axis_u) || !finite_vec(axis_v) ||
      !finite_scalar(focal_param)) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "抛物线创建失败：输入包含非法数值", "抛物线创建失败");
  }
  if (detail::norm(axis_u) <= kEpsilon || detail::norm(axis_v) <= kEpsilon) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "抛物线创建失败：轴向量必须非零", "抛物线创建失败");
  }
  if (detail::norm(detail::cross(axis_u, axis_v)) <= kEpsilon) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "抛物线创建失败：轴向量不能共线", "抛物线创建失败");
  }
  if (focal_param <= 0.0) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "抛物线创建失败：焦参数必须大于 0", "抛物线创建失败");
  }
  const auto id = CurveId{state_->allocate_id()};
  state_->curves.emplace(
      id.value,
      make_curve_record(detail::CurveKind::Parabola, origin, {1.0, 0.0, 0.0},
                        0.0, detail::normalize(detail::cross(axis_u, axis_v)),
                        axis_u, axis_v, {}, {}, {}, focal_param, 0.0));
  return ok_result(id, state_->create_diagnostic("已创建抛物线"));
}

Result<CurveId> CurveFactory::make_hyperbola(const Point3 &origin, const Vec3 &axis_u,
                                             const Vec3 &axis_v, Scalar a, Scalar b) {
  if (!finite_point(origin) || !finite_vec(axis_u) || !finite_vec(axis_v) ||
      !finite_scalar(a) || !finite_scalar(b)) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "双曲线创建失败：输入包含非法数值", "双曲线创建失败");
  }
  if (detail::norm(axis_u) <= kEpsilon || detail::norm(axis_v) <= kEpsilon) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "双曲线创建失败：轴向量必须非零", "双曲线创建失败");
  }
  if (detail::norm(detail::cross(axis_u, axis_v)) <= kEpsilon) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "双曲线创建失败：轴向量不能共线", "双曲线创建失败");
  }
  if (a <= 0.0 || b <= 0.0) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "双曲线创建失败：a 与 b 必须大于 0", "双曲线创建失败");
  }
  const auto id = CurveId{state_->allocate_id()};
  state_->curves.emplace(
      id.value,
      make_curve_record(detail::CurveKind::Hyperbola, origin, {1.0, 0.0, 0.0},
                        0.0, detail::normalize(detail::cross(axis_u, axis_v)),
                        axis_u, axis_v, {}, {}, {}, a, b));
  return ok_result(id, state_->create_diagnostic("已创建双曲线"));
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

Result<CurveId> CurveFactory::make_composite_polyline(std::span<const Point3> poles) {
  if (poles.size() < 2) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "复合曲线创建失败：polyline 至少需要两个点", "复合曲线创建失败");
  }
  for (const auto &p : poles) {
    if (!finite_point(p)) {
      return detail::invalid_input_result<CurveId>(
          *state_, diag_codes::kGeoCurveCreationInvalid,
          "复合曲线创建失败：输入点包含非法数值", "复合曲线创建失败");
    }
  }
  const auto id = CurveId{state_->allocate_id()};
  state_->curves.emplace(
      id.value,
      make_curve_record(detail::CurveKind::CompositePolyline, poles.front(),
                        {1.0, 0.0, 0.0}, 0.0, {0.0, 0.0, 1.0},
                        {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
                        std::vector<Point3>(poles.begin(), poles.end())));
  return ok_result(id, state_->create_diagnostic("已创建复合曲线(polyline)"));
}

Result<CurveId> CurveFactory::make_composite_chain(std::span<const CurveId> children) {
  if (children.empty()) {
    return detail::invalid_input_result<CurveId>(
        *state_, diag_codes::kGeoCurveCreationInvalid,
        "复合曲线创建失败：子曲线集合为空", "复合曲线创建失败");
  }
  std::vector<CurveId> ids;
  ids.reserve(children.size());
  for (const auto child : children) {
    if (!detail::has_curve(*state_, child)) {
      return detail::invalid_input_result<CurveId>(
          *state_, diag_codes::kCoreInvalidHandle,
          "复合曲线创建失败：子曲线句柄无效", "复合曲线创建失败");
    }
    ids.push_back(child);
  }
  const auto id = CurveId{state_->allocate_id()};
  const auto origin = state_->curves.at(ids.front().value).origin;
  state_->curves.emplace(
      id.value,
      make_curve_record(detail::CurveKind::CompositeChain, origin, {1.0, 0.0, 0.0},
                        0.0, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0},
                        {0.0, 1.0, 0.0}, {}, {}, {}, 0.0, 0.0,
                        std::move(ids)));
  return ok_result(id, state_->create_diagnostic("已创建复合曲线(chain)"));
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

Result<SurfaceId> SurfaceFactory::make_bezier(std::span<const Point3> poles) {
  if (poles.empty()) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "Bezier 曲面创建失败：控制点不能为空", "Bezier 曲面创建失败");
  }
  for (const auto &p : poles) {
    if (!finite_point(p)) {
      return detail::invalid_input_result<SurfaceId>(
          *state_, diag_codes::kGeoSurfaceCreationInvalid,
          "Bezier 曲面创建失败：输入包含非法数值", "Bezier 曲面创建失败");
    }
  }
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(
      id.value,
      make_surface_record(detail::SurfaceKind::Bezier, poles.front(),
                          {0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}, 0.0, 0.0, 0.0,
                          std::vector<Point3>(poles.begin(), poles.end())));
  return ok_result(id, state_->create_diagnostic("已创建Bezier曲面"));
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

Result<SurfaceId> SurfaceFactory::make_revolved(CurveId generatrix,
                                                const Axis3 &axis,
                                                Scalar sweep_angle_radians) {
  if (!finite_point(axis.origin) || !finite_vec(axis.direction) ||
      !finite_scalar(sweep_angle_radians)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "旋转面创建失败：输入包含非法数值", "旋转面创建失败");
  }
  if (detail::norm(axis.direction) <= kEpsilon) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "旋转面创建失败：旋转轴方向不能为零", "旋转面创建失败");
  }
  if (!detail::has_curve(*state_, generatrix)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "旋转面创建失败：母线曲线不存在", "旋转面创建失败");
  }
  if (sweep_angle_radians <= 0.0 ||
      sweep_angle_radians > std::acos(-1.0) * 64.0) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "旋转面创建失败：扫描角必须在 (0, 64π] 弧度内", "旋转面创建失败");
  }
  const auto ax = detail::normalize(axis.direction);
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(
      id.value,
      make_surface_record(detail::SurfaceKind::Revolved, axis.origin, ax, ax,
                          0.0, 0.0, 0.0, {}, {}, {}, {}, {}, generatrix,
                          sweep_angle_radians));
  return ok_result(id, state_->create_diagnostic("已创建旋转面"));
}

Result<SurfaceId> SurfaceFactory::make_swept_linear(CurveId profile,
                                                    const Vec3 &direction,
                                                    Scalar sweep_length) {
  if (!finite_vec(direction) || !finite_scalar(sweep_length)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "扫掠面创建失败：输入包含非法数值", "扫掠面创建失败");
  }
  if (detail::norm(direction) <= kEpsilon) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "扫掠面创建失败：扫掠方向不能为零", "扫掠面创建失败");
  }
  if (!detail::has_curve(*state_, profile)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "扫掠面创建失败：轮廓曲线不存在", "扫掠面创建失败");
  }
  if (sweep_length <= 0.0) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "扫掠面创建失败：扫掠长度必须大于 0", "扫掠面创建失败");
  }
  const auto dir = detail::normalize(direction);
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(
      id.value,
      make_surface_record(detail::SurfaceKind::Swept, {0.0, 0.0, 0.0}, dir, dir,
                          0.0, 0.0, 0.0, {}, {}, {}, {}, {}, profile, 0.0,
                          sweep_length));
  return ok_result(id, state_->create_diagnostic("已创建扫掠面(线性)"));
}

Result<SurfaceId> SurfaceFactory::make_trimmed(SurfaceId base_surface, Scalar u_min,
                                               Scalar u_max, Scalar v_min,
                                               Scalar v_max) {
  if (!detail::has_surface(*state_, base_surface) || !finite_scalar(u_min) ||
      !finite_scalar(u_max) || !finite_scalar(v_min) || !finite_scalar(v_max)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "修剪面创建失败：基曲面不存在或参数包含非法数值", "修剪面创建失败");
  }
  if (!(u_min < u_max) || !(v_min < v_max)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "修剪面创建失败：修剪参数范围必须满足 min < max", "修剪面创建失败");
  }
  const auto &base = state_->surfaces.at(base_surface.value);
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(
      id.value,
      make_surface_record(detail::SurfaceKind::Trimmed, base.origin, base.axis,
                          base.normal, base.radius_a, base.radius_b,
                          base.semi_angle, {}, {}, {}, {}, base_surface, {},
                          0.0, 0.0, u_min, u_max, v_min, v_max));
  return ok_result(id, state_->create_diagnostic("已创建修剪面"));
}

Result<SurfaceId> SurfaceFactory::make_offset(SurfaceId base_surface,
                                              Scalar offset_distance) {
  if (!detail::has_surface(*state_, base_surface) ||
      !finite_scalar(offset_distance)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "偏置面创建失败：基曲面不存在或偏置距离非法", "偏置面创建失败");
  }
  const auto &base = state_->surfaces.at(base_surface.value);
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(
      id.value,
      make_surface_record(detail::SurfaceKind::Offset, base.origin, base.axis,
                          base.normal, base.radius_a, base.radius_b,
                          base.semi_angle, {}, {}, {}, {}, base_surface, {},
                          0.0, 0.0, 0.0, 1.0, 0.0, 1.0, offset_distance));
  return ok_result(id, state_->create_diagnostic("已创建偏置面"));
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
  case detail::CurveKind::LineSegment: {
    if (curve.poles.size() >= 2) {
      const auto ct = std::clamp(t, 0.0, 1.0);
      result.point = lerp_point(curve.poles.front(), curve.poles.back(), ct);
      result.tangent =
          normalize_or_default(detail::subtract(curve.poles.back(), curve.poles.front()),
                               Vec3{1.0, 0.0, 0.0});
    } else {
      result.point = curve.origin;
      result.tangent = {1.0, 0.0, 0.0};
    }
    break;
  }
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
      const auto clamped_t = std::clamp(t, domain.min, domain.max);
      if (curve.kind == detail::CurveKind::Bezier) {
        result.point = evaluate_bezier_point(
            std::vector<Point3>(curve.poles.begin(), curve.poles.end()), clamped_t);
        const auto d1 = bezier_first_derivative(curve.poles, clamped_t);
        result.tangent = normalize_or_default(d1, Vec3{1.0, 0.0, 0.0});
      } else if (curve.kind == detail::CurveKind::BSpline) {
        result.point = evaluate_polyline_point(curve.poles, clamped_t);
        const auto d1 = polyline_first_derivative(curve.poles, clamped_t);
        result.tangent = normalize_or_default(d1, Vec3{1.0, 0.0, 0.0});
      } else {
        Vec3 d1{};
        Vec3 d2{};
        detail::rational_bezier_eval_all(curve.poles, curve.weights, clamped_t, result.point, d1, d2);
        result.tangent = normalize_or_default(d1, Vec3{1.0, 0.0, 0.0});
        (void)d2;
      }
    } else {
      result.point = curve.origin;
      result.tangent = {1.0, 0.0, 0.0};
    }
    break;
  }
  case detail::CurveKind::Parabola:
  case detail::CurveKind::Hyperbola:
  case detail::CurveKind::CompositePolyline: {
    const auto domain = curve_domain(curve);
    const auto eval_fn = [&curve](Scalar value) {
      return evaluate_curve_point(curve, value);
    };
    const auto clamped_t = std::clamp(t, domain.min, domain.max);
    result.point = eval_fn(clamped_t);
    result.tangent =
        finite_difference_tangent(eval_fn, clamped_t, domain.min, domain.max);
    break;
  }
  case detail::CurveKind::CompositeChain: {
    if (curve.children.empty()) {
      result.point = curve.origin;
      result.tangent = {1.0, 0.0, 0.0};
      break;
    }
    const auto domain = curve_domain(curve);
    const auto ct = std::clamp(t, domain.min, domain.max);
    const auto max_index = static_cast<Scalar>(curve.children.size());
    const auto clamped = std::min(ct, std::nextafter(max_index, 0.0));
    const auto idx = static_cast<std::size_t>(std::max<Scalar>(0.0, std::floor(clamped)));
    const auto local_t = clamped - static_cast<Scalar>(idx);
    const auto child_id = curve.children[std::min(idx, curve.children.size() - 1)];
    const auto child_eval = eval(child_id, local_t, deriv_order);
    if (child_eval.status != StatusCode::Ok || !child_eval.value.has_value()) {
      return error_result<CurveEvalResult>(child_eval.status, child_eval.diagnostic_id);
    }
    result = *child_eval.value;
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
  if (order > 1 && curve.kind == detail::CurveKind::Bezier && !curve.poles.empty()) {
    const auto domain = curve_domain(curve);
    const auto clamped_t = std::clamp(t, domain.min, domain.max);
    result.derivatives[1] = bezier_second_derivative(curve.poles, clamped_t);
  }
  if (order > 1 && curve.kind == detail::CurveKind::BSpline && !curve.poles.empty()) {
    // C1 polyline: second derivative is 0 on segment interiors (kink model).
    result.derivatives[1] = {0.0, 0.0, 0.0};
  }
  if (order > 1 && curve.kind == detail::CurveKind::Nurbs && !curve.poles.empty()) {
    const auto domain = curve_domain(curve);
    const auto clamped_t = std::clamp(t, domain.min, domain.max);
    Point3 p0{};
    Vec3 d1{};
    Vec3 d2{};
    detail::rational_bezier_eval_all(curve.poles, curve.weights, clamped_t, p0, d1, d2);
    result.derivatives[1] = d2;
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
  case detail::CurveKind::LineSegment: {
    if (curve.poles.size() < 2) {
      return detail::failed_result<Scalar>(
          *state_, StatusCode::OperationFailed, diag_codes::kGeoParameterSolveFailure,
          "曲线最近参数求解失败：线段数据缺失", "曲线最近参数求解失败");
    }
    const auto a = curve.poles.front();
    const auto b = curve.poles.back();
    const auto ab = detail::subtract(b, a);
    const auto denom = detail::dot(ab, ab);
    if (denom <= kEpsilon) {
      return detail::failed_result<Scalar>(
          *state_, StatusCode::OperationFailed, diag_codes::kGeoParameterSolveFailure,
          "曲线最近参数求解失败：线段退化", "曲线最近参数求解失败");
    }
    const auto ap = detail::subtract(point, a);
    const auto tproj = detail::dot(ap, ab) / denom;
    return ok_result<Scalar>(std::clamp(tproj, 0.0, 1.0),
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
      curve.kind == detail::CurveKind::Nurbs ||
      curve.kind == detail::CurveKind::Parabola ||
      curve.kind == detail::CurveKind::Hyperbola ||
      curve.kind == detail::CurveKind::CompositePolyline) {
    const auto domain = curve_domain(curve);
    Scalar t = std::clamp(approximate_curve_parameter(curve, point),
                          domain.min, domain.max);
    // Damped Gauss-Newton (LM-style) 1D refinement:
    // minimize ||C(t) - P||^2 with clamp + descent-only acceptance.
    auto best_cost = std::numeric_limits<Scalar>::max();
    {
      const auto ev0 = eval(curve_id, t, 0);
      if (ev0.status == StatusCode::Ok && ev0.value.has_value()) {
        best_cost = squared_distance(ev0.value->point, point);
      }
    }
    Scalar lambda = 1e-6;
    for (int iter = 0; iter < 20; ++iter) {
      const auto ct = std::clamp(t, domain.min, domain.max);
      const auto ev = eval(curve_id, ct, 1);
      if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
        break;
      }
      const auto &r = *ev.value;
      const Vec3 err = detail::subtract(r.point, point);
      const auto jtj = detail::dot(r.tangent, r.tangent);
      const auto g = detail::dot(r.tangent, err);
      if (!(jtj > 1e-18) || !std::isfinite(jtj) || !std::isfinite(g)) {
        break;
      }
      bool accepted = false;
      for (int attempt = 0; attempt < 8; ++attempt) {
        const auto scale = std::max<Scalar>(1.0, jtj);
        const auto lam = lambda * scale;
        const auto denom = jtj + lam;
        if (!(denom > 0.0) || !std::isfinite(denom)) {
          lambda *= 10.0;
          continue;
        }
        const auto step = -g / denom;
        if (!std::isfinite(step)) {
          lambda *= 10.0;
          continue;
        }
        const auto next_t = std::clamp(ct + step, domain.min, domain.max);
        const auto ev_next = eval(curve_id, next_t, 0);
        if (ev_next.status != StatusCode::Ok || !ev_next.value.has_value()) {
          lambda *= 10.0;
          continue;
        }
        const auto next_cost = squared_distance(ev_next.value->point, point);
        if (next_cost <= best_cost) {
          best_cost = next_cost;
          t = next_t;
          lambda = std::max<Scalar>(1e-12, lambda * 0.3);
          accepted = true;
          if (std::abs(step) < 1e-12) {
            iter = 9999;
          }
          break;
        }
        lambda *= 10.0;
      }
      if (!accepted) {
        break;
      }
    }
    return ok_result<Scalar>(std::clamp(t, domain.min, domain.max),
                             state_->create_diagnostic("曲线最近参数已求解"));
  }

  if (curve.kind == detail::CurveKind::CompositeChain) {
    if (curve.children.empty()) {
      return detail::failed_result<Scalar>(
          *state_, StatusCode::OperationFailed, diag_codes::kGeoParameterSolveFailure,
          "曲线最近参数求解失败：复合链不包含任何子曲线", "曲线最近参数求解失败");
    }
    const auto domain = curve_domain(curve);
    // Coarse: try each child and pick the best mapped parameter.
    Scalar best_t = domain.min;
    Scalar best_cost = std::numeric_limits<Scalar>::max();
    for (std::size_t i = 0; i < curve.children.size(); ++i) {
      const auto child_id = curve.children[i];
      if (!detail::has_curve(*state_, child_id)) {
        continue;
      }
      const auto child_t = closest_parameter(child_id, point);
      if (child_t.status != StatusCode::Ok || !child_t.value.has_value()) {
        continue;
      }
      const auto child_eval = eval(child_id, *child_t.value, 0);
      if (child_eval.status != StatusCode::Ok || !child_eval.value.has_value()) {
        continue;
      }
      const auto cost = squared_distance(child_eval.value->point, point);
      if (cost < best_cost) {
        best_cost = cost;
        const auto local = std::clamp(*child_t.value, 0.0, 1.0);
        best_t = std::clamp(static_cast<Scalar>(i) + local, domain.min, domain.max);
      }
    }

    // Refine on the composite chain parameter directly (uses chain eval/tangent).
    Scalar t = std::clamp(best_t, domain.min, domain.max);
    Scalar lambda = 1e-6;
    for (int iter = 0; iter < 25; ++iter) {
      const auto ct = std::clamp(t, domain.min, domain.max);
      const auto ev = eval(curve_id, ct, 1);
      if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
        break;
      }
      const auto &r = *ev.value;
      const Vec3 err = detail::subtract(r.point, point);
      const auto jtj = detail::dot(r.tangent, r.tangent);
      const auto g = detail::dot(r.tangent, err);
      if (!(jtj > 1e-18) || !std::isfinite(jtj) || !std::isfinite(g)) {
        break;
      }
      bool accepted = false;
      for (int attempt = 0; attempt < 8; ++attempt) {
        const auto scale = std::max<Scalar>(1.0, jtj);
        const auto lam = lambda * scale;
        const auto denom = jtj + lam;
        if (!(denom > 0.0) || !std::isfinite(denom)) {
          lambda *= 10.0;
          continue;
        }
        const auto step = -g / denom;
        if (!std::isfinite(step)) {
          lambda *= 10.0;
          continue;
        }
        const auto next_t = std::clamp(ct + step, domain.min, domain.max);
        const auto ev_next = eval(curve_id, next_t, 0);
        if (ev_next.status != StatusCode::Ok || !ev_next.value.has_value()) {
          lambda *= 10.0;
          continue;
        }
        const auto next_cost = squared_distance(ev_next.value->point, point);
        if (next_cost <= best_cost) {
          best_cost = next_cost;
          t = next_t;
          lambda = std::max<Scalar>(1e-12, lambda * 0.3);
          accepted = true;
          if (std::abs(step) < 1e-12) {
            iter = 9999;
          }
          break;
        }
        lambda *= 10.0;
      }
      if (!accepted) {
        break;
      }
    }
    return ok_result<Scalar>(std::clamp(t, domain.min, domain.max),
                             state_->create_diagnostic("曲线最近参数已求解"));
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
  if (it->second.kind == detail::CurveKind::CompositeChain) {
    const auto &curve = it->second;
    if (curve.children.empty()) {
      return ok_result(detail::bbox_from_center_radius(curve.origin, 1.0, 1.0, 1.0),
                       state_->create_diagnostic("已返回曲线包围盒"));
    }
    auto first_bbox = bbox(curve.children.front());
    if (first_bbox.status != StatusCode::Ok || !first_bbox.value.has_value()) {
      return error_result<BoundingBox>(first_bbox.status, first_bbox.diagnostic_id);
    }
    auto out = *first_bbox.value;
    for (std::size_t i = 1; i < curve.children.size(); ++i) {
      const auto b = bbox(curve.children[i]);
      if (b.status != StatusCode::Ok || !b.value.has_value()) {
        return error_result<BoundingBox>(b.status, b.diagnostic_id);
      }
      out.min.x = std::min(out.min.x, b.value->min.x);
      out.min.y = std::min(out.min.y, b.value->min.y);
      out.min.z = std::min(out.min.z, b.value->min.z);
      out.max.x = std::max(out.max.x, b.value->max.x);
      out.max.y = std::max(out.max.y, b.value->max.y);
      out.max.z = std::max(out.max.z, b.value->max.z);
      out.is_valid = out.is_valid && b.value->is_valid;
    }
    return ok_result(out, state_->create_diagnostic("已返回曲线包围盒"));
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
    result.du = Vec3{-surface.radius_a * std::sin(u) * std::sin(v),
                     surface.radius_a * std::cos(u) * std::sin(v),
                     0.0};
    result.dv = Vec3{surface.radius_a * std::cos(u) * std::cos(v),
                     surface.radius_a * std::sin(u) * std::cos(v),
                     -surface.radius_a * std::sin(v)};
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
    // Cone is developable: one principal curvature is zero.
    // Provide a minimal stable curvature for the circumferential direction.
    // Our parameterization uses v as axial distance from apex; radius = v * tan(semi_angle).
    const auto radius = std::max(radial, kEpsilon);
    const auto sa = std::sin(surface.semi_angle);
    const auto ca = std::cos(surface.semi_angle);
    // Avoid division by ~0 when angle is near 0.
    const auto denom = std::max(radius, kEpsilon) * std::max(sa, kEpsilon);
    // This matches cylinder-like behavior in spirit (curvature decreases with radius).
    result.k1 = 0.0;
    result.k2 = (ca / denom);
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
    // Principal curvatures for a standard torus parameterization:
    // k_v (minor circle direction) = 1 / r
    // k_u (major circle direction) = cos(v) / (R + r cos(v))
    // (Signs depend on chosen normal; we keep a consistent convention using this formula.)
    const auto r = std::max(surface.radius_b, kEpsilon);
    const auto R = surface.radius_a;
    const auto denom = (R + r * std::cos(v));
    result.k1 = 1.0 / r;
    result.k2 = std::abs(denom) > kEpsilon ? (std::cos(v) / denom) : 0.0;
    break;
  }
  case detail::SurfaceKind::Bezier: {
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
  case detail::SurfaceKind::Revolved: {
    if (!detail::has_curve(*state_, surface.profile_curve_id)) {
      result.point = surface.origin;
      result.du = {1.0, 0.0, 0.0};
      result.dv = {0.0, 1.0, 0.0};
      result.normal = {0.0, 0.0, 1.0};
      result.k1 = 0.0;
      result.k2 = 0.0;
      break;
    }
    const auto dom = surface_domain(surface);
    const auto cu = std::clamp(u, dom.u.min, dom.u.max);
    const auto cv = std::clamp(v, dom.v.min, dom.v.max);
    const auto k = normalize_or_default(surface.axis, Vec3{0.0, 0.0, 1.0});
    const auto O = surface.origin;
    CurveService curves{state_};
    const auto &gen_rec = state_->curves.at(surface.profile_curve_id.value);
    const auto gen_dom = curve_domain(gen_rec);
    const auto tv = gen_dom.min + cv * (gen_dom.max - gen_dom.min);
    const auto base_ev = curves.eval(surface.profile_curve_id, tv, 1);
    if (base_ev.status != StatusCode::Ok || !base_ev.value.has_value()) {
      result.point = surface.origin;
      result.du = {1.0, 0.0, 0.0};
      result.dv = {0.0, 1.0, 0.0};
      result.normal = {0.0, 0.0, 1.0};
      result.k1 = 0.0;
      result.k2 = 0.0;
      break;
    }
    const auto p = base_ev.value->point;
    const auto r = detail::subtract(p, O);
    const Vec3 rv{r.x, r.y, r.z};
    const auto r_rot = rotate_vec_around_axis(k, rv, cu);
    result.point = detail::add_point_vec(O, {r_rot.x, r_rot.y, r_rot.z});

    const auto step_u = std::max((dom.u.max - dom.u.min) * 1e-5, 1e-7);
    const auto step_v = std::max((dom.v.max - dom.v.min) * 1e-5, 1e-7);
    const auto eval_pt = [&](Scalar uu, Scalar vv) -> Point3 {
      const auto cuu = std::clamp(uu, dom.u.min, dom.u.max);
      const auto cvv = std::clamp(vv, dom.v.min, dom.v.max);
      const auto tvv = gen_dom.min + cvv * (gen_dom.max - gen_dom.min);
      const auto ev = curves.eval(surface.profile_curve_id, tvv, 1);
      if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
        return O;
      }
      const auto pp = ev.value->point;
      const auto rr = detail::subtract(pp, O);
      const Vec3 rvv{rr.x, rr.y, rr.z};
      const auto rr_rot = rotate_vec_around_axis(k, rvv, cuu);
      return detail::add_point_vec(O, {rr_rot.x, rr_rot.y, rr_rot.z});
    };
    const auto u0 = std::clamp(cu - step_u, dom.u.min, dom.u.max);
    const auto u1 = std::clamp(cu + step_u, dom.u.min, dom.u.max);
    const auto v0 = std::clamp(cv - step_v, dom.v.min, dom.v.max);
    const auto v1 = std::clamp(cv + step_v, dom.v.min, dom.v.max);
    result.du = normalize_or_default(detail::subtract(eval_pt(u1, cv), eval_pt(u0, cv)),
                                     Vec3{1.0, 0.0, 0.0});
    result.dv = normalize_or_default(detail::subtract(eval_pt(cu, v1), eval_pt(cu, v0)),
                                     Vec3{0.0, 1.0, 0.0});
    result.normal = normalize_or_default(detail::cross(result.du, result.dv),
                                         Vec3{0.0, 0.0, 1.0});
    result.k1 = 0.0;
    result.k2 = 0.0;
    break;
  }
  case detail::SurfaceKind::Swept: {
    if (!detail::has_curve(*state_, surface.profile_curve_id)) {
      result.point = surface.origin;
      result.du = {1.0, 0.0, 0.0};
      result.dv = {0.0, 1.0, 0.0};
      result.normal = {0.0, 0.0, 1.0};
      result.k1 = 0.0;
      result.k2 = 0.0;
      break;
    }
    const auto dom = surface_domain(surface);
    const auto cu = std::clamp(u, dom.u.min, dom.u.max);
    const auto cv = std::clamp(v, dom.v.min, dom.v.max);
    const auto dir = normalize_or_default(surface.axis, Vec3{0.0, 0.0, 1.0});
    const auto L = surface.sweep_length;
    CurveService curves{state_};
    const auto &prof_rec = state_->curves.at(surface.profile_curve_id.value);
    const auto prof_dom = curve_domain(prof_rec);
    const auto tv = prof_dom.min + cv * (prof_dom.max - prof_dom.min);
    const auto base_ev = curves.eval(surface.profile_curve_id, tv, 1);
    if (base_ev.status != StatusCode::Ok || !base_ev.value.has_value()) {
      result.point = surface.origin;
      result.du = detail::scale(dir, L);
      result.dv = {0.0, 1.0, 0.0};
      result.normal = {0.0, 0.0, 1.0};
      result.k1 = 0.0;
      result.k2 = 0.0;
      break;
    }
    result.point = detail::add_point_vec(base_ev.value->point, detail::scale(dir, cu * L));
    result.du = detail::scale(dir, L);
    result.dv = detail::scale(base_ev.value->tangent, (prof_dom.max - prof_dom.min));
    result.normal = normalize_or_default(detail::cross(result.du, result.dv),
                                         Vec3{0.0, 0.0, 1.0});
    result.k1 = 0.0;
    result.k2 = 0.0;
    break;
  }
  case detail::SurfaceKind::Trimmed: {
    if (!detail::has_surface(*state_, surface.base_surface_id)) {
      result.point = surface.origin;
      result.du = {1.0, 0.0, 0.0};
      result.dv = {0.0, 1.0, 0.0};
      result.normal = {0.0, 0.0, 1.0};
      result.k1 = 0.0;
      result.k2 = 0.0;
      break;
    }
    SurfaceService base{state_};
    const auto dom = surface_domain(surface);
    const auto cu = std::clamp(u, dom.u.min, dom.u.max);
    const auto cv = std::clamp(v, dom.v.min, dom.v.max);
    const auto ev = base.eval(surface.base_surface_id, cu, cv, deriv_order);
    if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
      result.point = surface.origin;
      result.du = {1.0, 0.0, 0.0};
      result.dv = {0.0, 1.0, 0.0};
      result.normal = {0.0, 0.0, 1.0};
      result.k1 = 0.0;
      result.k2 = 0.0;
      break;
    }
    result = *ev.value;
    break;
  }
  case detail::SurfaceKind::Offset: {
    if (!detail::has_surface(*state_, surface.base_surface_id)) {
      result.point = surface.origin;
      result.du = {1.0, 0.0, 0.0};
      result.dv = {0.0, 1.0, 0.0};
      result.normal = {0.0, 0.0, 1.0};
      result.k1 = 0.0;
      result.k2 = 0.0;
      break;
    }
    SurfaceService base{state_};
    const auto base_dom = base.domain(surface.base_surface_id);
    Range2D dom = base_dom.value.has_value() ? *base_dom.value : Range2D{Range1D{0.0, 1.0}, Range1D{0.0, 1.0}};
    const auto cu = std::clamp(u, dom.u.min, dom.u.max);
    const auto cv = std::clamp(v, dom.v.min, dom.v.max);
    const auto ev = base.eval(surface.base_surface_id, cu, cv, deriv_order);
    if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
      result.point = surface.origin;
      result.du = {1.0, 0.0, 0.0};
      result.dv = {0.0, 1.0, 0.0};
      result.normal = {0.0, 0.0, 1.0};
      result.k1 = 0.0;
      result.k2 = 0.0;
      break;
    }
    result = *ev.value;
    result.point = detail::add_point_vec(result.point, detail::scale(result.normal, surface.offset_distance));
    break;
  }
  default:
    result.point = surface.origin;
    result.du = {1.0, 0.0, 0.0};
    result.dv = {0.0, 1.0, 0.0};
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
      surface.kind == detail::SurfaceKind::Bezier ||
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
  case detail::SurfaceKind::Bezier:
  case detail::SurfaceKind::Nurbs:
  {
    const auto domain = surface_domain(surface);
    auto uv = approximate_surface_uv(surface, point);
    // Damped Gauss-Newton (LM-style) refinement using local tangents (du/dv).
    auto best_cost = std::numeric_limits<Scalar>::max();
    {
      const auto ev0 = eval(surface_id, std::clamp(uv.first, domain.u.min, domain.u.max),
                            std::clamp(uv.second, domain.v.min, domain.v.max), 0);
      if (ev0.status == StatusCode::Ok && ev0.value.has_value()) {
        best_cost = squared_distance(ev0.value->point, point);
      }
    }
    Scalar lambda = 1e-6;
    for (int iter = 0; iter < 15; ++iter) {
      const auto cu = std::clamp(uv.first, domain.u.min, domain.u.max);
      const auto cv = std::clamp(uv.second, domain.v.min, domain.v.max);
      const auto ev = eval(surface_id, cu, cv, 1);
      if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
        break;
      }
      const auto &r = *ev.value;
      const Vec3 err = detail::subtract(r.point, point);
      const auto j00 = detail::dot(r.du, r.du);
      const auto j01 = detail::dot(r.du, r.dv);
      const auto j11 = detail::dot(r.dv, r.dv);
      const auto g0 = detail::dot(r.du, err);
      const auto g1 = detail::dot(r.dv, err);
      // Try a few lambda values to ensure descent.
      bool accepted = false;
      for (int attempt = 0; attempt < 6; ++attempt) {
        Scalar du_step = 0.0;
        Scalar dv_step = 0.0;
        const auto scale = std::max<Scalar>(1.0, j00 + j11);
        const auto lam = lambda * scale;
        if (!solve_2x2(j00 + lam, j01, j01, j11 + lam, -g0, -g1, du_step, dv_step)) {
          lambda *= 10.0;
          continue;
        }
        const auto next_u = std::clamp(cu + du_step, domain.u.min, domain.u.max);
        const auto next_v = std::clamp(cv + dv_step, domain.v.min, domain.v.max);
        const auto ev_next = eval(surface_id, next_u, next_v, 0);
        if (ev_next.status != StatusCode::Ok || !ev_next.value.has_value()) {
          lambda *= 10.0;
          continue;
        }
        const auto next_cost = squared_distance(ev_next.value->point, point);
        if (next_cost <= best_cost) {
          best_cost = next_cost;
          uv.first = next_u;
          uv.second = next_v;
          lambda = std::max<Scalar>(1e-12, lambda * 0.3);
          accepted = true;
          if (std::abs(du_step) + std::abs(dv_step) < 1e-10) {
            iter = 9999;
          }
          break;
        }
        lambda *= 10.0;
      }
      if (!accepted) {
        break;
      }
    }
    return ok_result(std::make_pair(std::clamp(uv.first, domain.u.min, domain.u.max),
                                    std::clamp(uv.second, domain.v.min, domain.v.max)),
                     state_->create_diagnostic("已求出曲面最近参数"));
  }
  case detail::SurfaceKind::Trimmed: {
    if (!detail::has_surface(*state_, surface.base_surface_id)) {
      return detail::failed_result<std::pair<Scalar, Scalar>>(
          *state_, StatusCode::InvalidInput, diag_codes::kGeoParameterSolveFailure,
          "曲面参数反求失败：基曲面不存在", "曲面参数反求失败");
    }
    SurfaceService base{state_};
    const auto base_uv = base.closest_uv(surface.base_surface_id, point);
    if (base_uv.status != StatusCode::Ok || !base_uv.value.has_value()) {
      return error_result<std::pair<Scalar, Scalar>>(base_uv.status, base_uv.diagnostic_id);
    }
    const auto dom = surface_domain(surface);
    return ok_result(std::make_pair(std::clamp(base_uv.value->first, dom.u.min, dom.u.max),
                                    std::clamp(base_uv.value->second, dom.v.min, dom.v.max)),
                     state_->create_diagnostic("已求出曲面最近参数"));
  }
  case detail::SurfaceKind::Revolved:
  case detail::SurfaceKind::Swept:
  case detail::SurfaceKind::Offset: {
    const auto dom = domain(surface_id);
    if (dom.status != StatusCode::Ok || !dom.value.has_value()) {
      return detail::failed_result<std::pair<Scalar, Scalar>>(
          *state_, StatusCode::OperationFailed, diag_codes::kGeoParameterSolveFailure,
          "曲面参数反求失败：曲面定义域不可用", "曲面参数反求失败");
    }
    const auto d = *dom.value;
    const int samples = 28;
    Scalar best_u = d.u.min;
    Scalar best_v = d.v.min;
    Scalar best_dist = std::numeric_limits<Scalar>::max();
    for (int ui = 0; ui <= samples; ++ui) {
      const auto au = static_cast<Scalar>(ui) / static_cast<Scalar>(samples);
      const auto uu = d.u.min + (d.u.max - d.u.min) * au;
      for (int vi = 0; vi <= samples; ++vi) {
        const auto av = static_cast<Scalar>(vi) / static_cast<Scalar>(samples);
        const auto vv = d.v.min + (d.v.max - d.v.min) * av;
        const auto ev = eval(surface_id, uu, vv, 0);
        if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
          return detail::failed_result<std::pair<Scalar, Scalar>>(
              *state_, StatusCode::OperationFailed, diag_codes::kGeoParameterSolveFailure,
              "曲面参数反求失败：曲面求值不可用", "曲面参数反求失败");
        }
        const auto dist = detail::norm(detail::subtract(point, ev.value->point));
        if (dist < best_dist) {
          best_dist = dist;
          best_u = uu;
          best_v = vv;
        }
      }
    }
    // Damped Gauss-Newton refinement (best-effort).
    Scalar best_cost = best_dist * best_dist;
    Scalar lambda = 1e-6;
    for (int iter = 0; iter < 15; ++iter) {
      const auto cu = std::clamp(best_u, d.u.min, d.u.max);
      const auto cv = std::clamp(best_v, d.v.min, d.v.max);
      const auto ev = eval(surface_id, cu, cv, 1);
      if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
        break;
      }
      const auto &r = *ev.value;
      const Vec3 err = detail::subtract(r.point, point);
      const auto j00 = detail::dot(r.du, r.du);
      const auto j01 = detail::dot(r.du, r.dv);
      const auto j11 = detail::dot(r.dv, r.dv);
      const auto g0 = detail::dot(r.du, err);
      const auto g1 = detail::dot(r.dv, err);
      bool accepted = false;
      for (int attempt = 0; attempt < 6; ++attempt) {
        Scalar du_step = 0.0;
        Scalar dv_step = 0.0;
        const auto scale = std::max<Scalar>(1.0, j00 + j11);
        const auto lam = lambda * scale;
        if (!solve_2x2(j00 + lam, j01, j01, j11 + lam, -g0, -g1, du_step, dv_step)) {
          lambda *= 10.0;
          continue;
        }
        const auto next_u = std::clamp(cu + du_step, d.u.min, d.u.max);
        const auto next_v = std::clamp(cv + dv_step, d.v.min, d.v.max);
        const auto ev_next = eval(surface_id, next_u, next_v, 0);
        if (ev_next.status != StatusCode::Ok || !ev_next.value.has_value()) {
          lambda *= 10.0;
          continue;
        }
        const auto next_cost = squared_distance(ev_next.value->point, point);
        if (next_cost <= best_cost) {
          best_cost = next_cost;
          best_u = next_u;
          best_v = next_v;
          lambda = std::max<Scalar>(1e-12, lambda * 0.3);
          accepted = true;
          if (std::abs(du_step) + std::abs(dv_step) < 1e-10) {
            iter = 9999;
          }
          break;
        }
        lambda *= 10.0;
      }
      if (!accepted) {
        break;
      }
    }
    return ok_result(std::make_pair(std::clamp(best_u, d.u.min, d.u.max),
                                    std::clamp(best_v, d.v.min, d.v.max)),
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

GeometryIntersectionService::GeometryIntersectionService(
    std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

namespace {

Result<std::vector<CurveSurfaceIntersection>>
unsupported_intersection(detail::KernelState &state,
                         std::string_view detail_message) {
  return detail::failed_result<std::vector<CurveSurfaceIntersection>>(
      state, StatusCode::NotImplemented, diag_codes::kCoreOperationUnsupported,
      std::string(detail_message), "曲线-曲面求交失败");
}

} // namespace

Result<std::vector<CurveSurfaceIntersection>>
GeometryIntersectionService::intersect_curve_surface(CurveId curve_id,
                                                     SurfaceId surface_id) const {
  if (!detail::has_curve(*state_, curve_id) || !detail::has_surface(*state_, surface_id)) {
    return detail::invalid_input_result<std::vector<CurveSurfaceIntersection>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "曲线-曲面求交失败：曲线或曲面不存在", "曲线-曲面求交失败");
  }

  const auto &curve = state_->curves.at(curve_id.value);
  const auto &surface = state_->surfaces.at(surface_id.value);

  // Delegate wrappers (minimal industrial semantics).
  if (surface.kind == detail::SurfaceKind::Trimmed) {
    if (!detail::has_surface(*state_, surface.base_surface_id)) {
      return detail::failed_result<std::vector<CurveSurfaceIntersection>>(
          *state_, StatusCode::OperationFailed, diag_codes::kGeoIntersectionFailure,
          "曲线-曲面求交失败：Trimmed 基曲面不存在", "曲线-曲面求交失败");
    }
    GeometryIntersectionService base{state_};
    auto hits = base.intersect_curve_surface(curve_id, surface.base_surface_id);
    if (hits.status != StatusCode::Ok || !hits.value.has_value()) {
      return error_result<std::vector<CurveSurfaceIntersection>>(hits.status, hits.diagnostic_id);
    }
    std::vector<CurveSurfaceIntersection> filtered;
    filtered.reserve(hits.value->size());
    for (const auto &h : *hits.value) {
      if (h.surface_u >= surface.trim_u_min - 1e-12 &&
          h.surface_u <= surface.trim_u_max + 1e-12 &&
          h.surface_v >= surface.trim_v_min - 1e-12 &&
          h.surface_v <= surface.trim_v_max + 1e-12) {
        filtered.push_back(h);
      }
    }
    return ok_result(std::move(filtered),
                     state_->create_diagnostic("曲线-修剪曲面求交完成"));
  }
  if (surface.kind == detail::SurfaceKind::Offset ||
      surface.kind == detail::SurfaceKind::Revolved ||
      surface.kind == detail::SurfaceKind::Swept ||
      surface.kind == detail::SurfaceKind::Bezier ||
      surface.kind == detail::SurfaceKind::BSpline ||
      surface.kind == detail::SurfaceKind::Nurbs ||
      surface.kind == detail::SurfaceKind::Cone ||
      surface.kind == detail::SurfaceKind::Torus) {
    return unsupported_intersection(*state_,
                                    "曲线-曲面求交暂不支持该曲面类型（后续将接入通用数值/裁剪链路）");
  }

  const auto frame = make_frame_from_axis(surface.normal);

  auto add_hit = [](std::vector<CurveSurfaceIntersection> &out, const Point3 &p,
                    Scalar t, Scalar u, Scalar v) {
    CurveSurfaceIntersection h;
    h.point = p;
    h.curve_t = t;
    h.surface_u = u;
    h.surface_v = v;
    out.push_back(h);
  };

  std::vector<CurveSurfaceIntersection> out;

  // Helpers to map 3D point to (u,v) for analytic surfaces.
  auto uv_on_plane = [&](const Point3 &p) -> std::pair<Scalar, Scalar> {
    const auto local = to_local(detail::subtract(p, surface.origin), frame);
    return {local[0], local[1]};
  };
  auto uv_on_sphere = [&](const Point3 &p) -> std::pair<Scalar, Scalar> {
    const auto rel = detail::subtract(p, surface.origin);
    const auto radius = std::max(surface.radius_a, 1e-12);
    const auto u = normalize_angle_0_2pi(std::atan2(rel.y, rel.x));
    const auto cos_v = std::clamp(rel.z / radius, -1.0, 1.0);
    const auto v = std::acos(cos_v);
    return {u, v};
  };
  auto uv_on_cylinder = [&](const Point3 &p) -> std::pair<Scalar, Scalar> {
    const auto local = to_local(detail::subtract(p, surface.origin), frame);
    const auto u = normalize_angle_0_2pi(std::atan2(local[1], local[0]));
    return {u, local[2]};
  };

  const auto surface_dom = surface_domain(surface);
  auto in_surface_domain = [&](Scalar u, Scalar v) {
    return u >= surface_dom.u.min - 1e-9 && u <= surface_dom.u.max + 1e-9 &&
           v >= surface_dom.v.min - 1e-9 && v <= surface_dom.v.max + 1e-9;
  };

  // Curve: Line / Segment analytic intersections.
  if (curve.kind == detail::CurveKind::Line || curve.kind == detail::CurveKind::LineSegment) {
    Point3 o = curve.origin;
    Vec3 d = curve.direction;
    if (curve.kind == detail::CurveKind::LineSegment) {
      if (curve.poles.size() < 2) {
        return detail::failed_result<std::vector<CurveSurfaceIntersection>>(
            *state_, StatusCode::OperationFailed, diag_codes::kGeoIntersectionFailure,
            "曲线-曲面求交失败：线段数据缺失", "曲线-曲面求交失败");
      }
      const auto a = curve.poles.front();
      const auto b = curve.poles.back();
      o = a;
      d = detail::subtract(b, a);
    }
    const auto dd = detail::dot(d, d);
    if (!(dd > kEpsilon)) {
      return detail::failed_result<std::vector<CurveSurfaceIntersection>>(
          *state_, StatusCode::OperationFailed, diag_codes::kGeoDegenerateGeometry,
          "曲线-曲面求交失败：直线方向退化", "曲线-曲面求交失败");
    }

    const auto t_min = (curve.kind == detail::CurveKind::LineSegment) ? 0.0 : -std::numeric_limits<Scalar>::infinity();
    const auto t_max = (curve.kind == detail::CurveKind::LineSegment) ? 1.0 :  std::numeric_limits<Scalar>::infinity();

    if (surface.kind == detail::SurfaceKind::Plane) {
      const auto n = surface.normal;
      const auto denom = detail::dot(n, d);
      const auto num = detail::dot(n, detail::subtract(surface.origin, o));
      if (std::abs(denom) <= 1e-12) {
        // Parallel or coplanar: treat as no hit for minimal API.
        return ok_result(out, state_->create_diagnostic("曲线-平面求交完成（无交或共面）"));
      }
      const auto t = num / denom;
      if (t < t_min - 1e-12 || t > t_max + 1e-12 || !finite_scalar(t)) {
        return ok_result(out, state_->create_diagnostic("曲线-平面求交完成（无交）"));
      }
      const auto p = detail::add_point_vec(o, detail::scale(d, t));
      const auto [u, v] = uv_on_plane(p);
      if (in_surface_domain(u, v)) {
        add_hit(out, p, t, u, v);
      }
      return ok_result(out, state_->create_diagnostic("曲线-平面求交完成"));
    }

    if (surface.kind == detail::SurfaceKind::Sphere) {
      const auto c = surface.origin;
      const auto r = surface.radius_a;
      if (!(r > kEpsilon) || !std::isfinite(r)) {
        return detail::failed_result<std::vector<CurveSurfaceIntersection>>(
            *state_, StatusCode::OperationFailed, diag_codes::kGeoDegenerateGeometry,
            "曲线-曲面求交失败：球半径退化", "曲线-曲面求交失败");
      }
      const Vec3 oc = detail::subtract(o, c);
      const auto a = dd;
      const auto b = 2.0 * detail::dot(oc, d);
      const auto c0 = detail::dot(oc, oc) - r * r;
      const auto disc = b * b - 4.0 * a * c0;
      if (disc < 0.0) {
        return ok_result(out, state_->create_diagnostic("曲线-球面求交完成（无交）"));
      }
      const auto sqrt_disc = std::sqrt(std::max<Scalar>(0.0, disc));
      const auto inv = 1.0 / (2.0 * a);
      const auto t0 = (-b - sqrt_disc) * inv;
      const auto t1 = (-b + sqrt_disc) * inv;
      for (auto t : {t0, t1}) {
        if (!finite_scalar(t) || t < t_min - 1e-12 || t > t_max + 1e-12) continue;
        const auto p = detail::add_point_vec(o, detail::scale(d, t));
        const auto [u, v] = uv_on_sphere(p);
        if (in_surface_domain(u, v)) {
          add_hit(out, p, t, u, v);
        }
      }
      return ok_result(out, state_->create_diagnostic("曲线-球面求交完成"));
    }

    if (surface.kind == detail::SurfaceKind::Cylinder) {
      if (!(surface.radius_a > kEpsilon) || !std::isfinite(surface.radius_a)) {
        return detail::failed_result<std::vector<CurveSurfaceIntersection>>(
            *state_, StatusCode::OperationFailed, diag_codes::kGeoDegenerateGeometry,
            "曲线-曲面求交失败：圆柱半径退化", "曲线-曲面求交失败");
      }
      // Work in cylinder local frame: x^2 + y^2 = r^2.
      const auto oL = to_local(detail::subtract(o, surface.origin), frame);
      const auto dL = to_local(d, frame);
      const auto a = dL[0] * dL[0] + dL[1] * dL[1];
      const auto b = 2.0 * (oL[0] * dL[0] + oL[1] * dL[1]);
      const auto c0 = oL[0] * oL[0] + oL[1] * oL[1] - surface.radius_a * surface.radius_a;
      if (a <= 1e-18) {
        // Parallel to axis: either no hit or infinite; treat as no hit.
        return ok_result(out, state_->create_diagnostic("曲线-圆柱面求交完成（无交或共线）"));
      }
      const auto disc = b * b - 4.0 * a * c0;
      if (disc < 0.0) {
        return ok_result(out, state_->create_diagnostic("曲线-圆柱面求交完成（无交）"));
      }
      const auto sqrt_disc = std::sqrt(std::max<Scalar>(0.0, disc));
      const auto inv = 1.0 / (2.0 * a);
      const auto t0 = (-b - sqrt_disc) * inv;
      const auto t1 = (-b + sqrt_disc) * inv;
      for (auto t : {t0, t1}) {
        if (!finite_scalar(t) || t < t_min - 1e-12 || t > t_max + 1e-12) continue;
        const auto p = detail::add_point_vec(o, detail::scale(d, t));
        const auto [u, v] = uv_on_cylinder(p);
        if (in_surface_domain(u, v)) {
          add_hit(out, p, t, u, v);
        }
      }
      return ok_result(out, state_->create_diagnostic("曲线-圆柱面求交完成"));
    }
  }

  // Curve: Circle with Plane intersection.
  if (curve.kind == detail::CurveKind::Circle && surface.kind == detail::SurfaceKind::Plane) {
    // Circle paramization: p(t)=c + r*(cos t * U + sin t * V)
    const auto c = curve.origin;
    const auto U = curve.axis_u;
    const auto V = curve.axis_v;
    const auto r = curve.radius;
    if (!(r > kEpsilon) || !std::isfinite(r)) {
      return detail::failed_result<std::vector<CurveSurfaceIntersection>>(
          *state_, StatusCode::OperationFailed, diag_codes::kGeoDegenerateGeometry,
          "曲线-曲面求交失败：圆半径退化", "曲线-曲面求交失败");
    }
    const auto n = surface.normal;
    const auto w = detail::subtract(c, surface.origin);
    const auto A = r * detail::dot(n, U);
    const auto B = r * detail::dot(n, V);
    const auto C = detail::dot(n, w);
    const auto R = std::hypot(A, B);
    if (R <= 1e-14) {
      // Circle plane parallel to surface plane normal -> either coplanar or no hit.
      if (std::abs(C) <= 1e-10) {
        return unsupported_intersection(*state_, "曲线-平面求交：圆与平面共面（交线为整圆，暂不返回曲线交集）");
      }
      return ok_result(out, state_->create_diagnostic("曲线-平面求交完成（无交）"));
    }
    // A cos t + B sin t + C = 0 -> R cos(t - phi) = -C
    const auto phi = std::atan2(B, A);
    const auto rhs = -C / R;
    if (rhs < -1.0 - 1e-12 || rhs > 1.0 + 1e-12) {
      return ok_result(out, state_->create_diagnostic("曲线-平面求交完成（无交）"));
    }
    const auto clamped = std::clamp(rhs, -1.0, 1.0);
    const auto alpha = std::acos(clamped);
    const auto t0 = normalize_angle_0_2pi(phi + alpha);
    const auto t1 = normalize_angle_0_2pi(phi - alpha);
    const std::array<Scalar, 2> ts{{t0, t1}};
    auto add_vec = [](const Vec3& a, const Vec3& b) -> Vec3 {
      return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
    };
    for (const auto t : ts) {
      const auto p = detail::add_point_vec(c,
                                           add_vec(detail::scale(U, r * std::cos(t)),
                                                   detail::scale(V, r * std::sin(t))));
      const auto [u, v] = uv_on_plane(p);
      if (in_surface_domain(u, v)) {
        add_hit(out, p, t, u, v);
      }
    }
    return ok_result(out, state_->create_diagnostic("曲线-平面求交完成"));
  }

  return unsupported_intersection(*state_, "曲线-曲面求交暂不支持该曲线类型或曲面类型组合");
}

} // namespace axiom
