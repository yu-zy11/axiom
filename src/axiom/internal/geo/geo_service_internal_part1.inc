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

bool bspline_knots_valid_for_curve(const detail::CurveRecord &curve, int &p_out);

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
  case detail::CurveKind::BSpline: {
    const auto n = curve.poles.size();
    if (n < 2) {
      return Range1D{0.0, 1.0};
    }
    int pk = 0;
    if (bspline_knots_valid_for_curve(curve, pk)) {
      return Range1D{curve.knots_u[static_cast<std::size_t>(pk)], curve.knots_u[n]};
    }
    return Range1D{0.0, std::max<Scalar>(1.0, static_cast<Scalar>(n - 1))};
  }
  case detail::CurveKind::CompositePolyline:
    return Range1D{0.0, std::max<Scalar>(1.0, static_cast<Scalar>(curve.poles.size() - 1))};
  case detail::CurveKind::CompositeChain:
    return Range1D{0.0, std::max<Scalar>(1.0, static_cast<Scalar>(curve.children.size()))};
  case detail::CurveKind::Bezier:
    return Range1D{0.0, 1.0};
  case detail::CurveKind::Nurbs: {
    const auto n = curve.poles.size();
    if (n < 2) {
      return Range1D{0.0, 1.0};
    }
    int pk = 0;
    if (bspline_knots_valid_for_curve(curve, pk)) {
      return Range1D{curve.knots_u[static_cast<std::size_t>(pk)], curve.knots_u[n]};
    }
    return Range1D{0.0, 1.0};
  }
  default:
    return Range1D{0.0, 1.0};
  }
}

Scalar curve_curvature_from_rp_rpp(const Vec3 &rp, const Vec3 &rpp) {
  const Vec3 cr = detail::cross(rp, rpp);
  const Scalar lp = detail::norm(rp);
  if (!(lp > 1e-24) || !std::isfinite(lp)) {
    return 0.0;
  }
  const Scalar denom = lp * lp * lp;
  if (!(std::abs(denom) > 1e-40)) {
    return 0.0;
  }
  const Scalar k = detail::norm(cr) / denom;
  return std::isfinite(k) ? k : 0.0;
}

Scalar pcurve_curvature_signed(const Vec2 &rp, const Vec2 &rpp) {
  const Scalar num = rp.x * rpp.y - rp.y * rpp.x;
  const Scalar lp = std::hypot(rp.x, rp.y);
  if (!(lp > 1e-24) || !std::isfinite(lp)) {
    return 0.0;
  }
  const Scalar denom = lp * lp * lp;
  if (!(std::abs(denom) > 1e-40)) {
    return 0.0;
  }
  const Scalar k = num / denom;
  return std::isfinite(k) ? k : 0.0;
}

void curve_finite_diff_r12(const std::function<Point3(Scalar)> &f, Scalar t, Scalar tmin,
                           Scalar tmax, Vec3 &r1, Vec3 &r2) {
  const Scalar span = tmax - tmin;
  const Scalar h = std::max(span * 1e-4, 1e-6);
  const Scalar tm = std::clamp(t, tmin, tmax);
  const Scalar ta = std::clamp(tm - h, tmin, tmax);
  const Scalar tb = std::clamp(tm + h, tmin, tmax);
  const Point3 pa = f(ta);
  const Point3 pb = f(tb);
  const Scalar den = tb - ta;
  if (std::abs(den) > 1e-30) {
    r1 = detail::scale(detail::subtract(pb, pa), 1.0 / den);
  } else {
    r1 = Vec3{1.0, 0.0, 0.0};
  }
  const Scalar hh = std::max(span * 1e-3, 1e-5);
  const Scalar t0 = std::clamp(tm - hh, tmin, tmax);
  const Scalar t2 = std::clamp(tm + hh, tmin, tmax);
  const Point3 p0 = f(t0);
  const Point3 pm = f(tm);
  const Point3 p2b = f(t2);
  const Scalar d_left = tm - t0;
  const Scalar d_right = t2 - tm;
  const Scalar denom2 = d_left * d_right;
  if (std::abs(denom2) > 1e-30) {
    r2.x = (p2b.x - 2.0 * pm.x + p0.x) / denom2;
    r2.y = (p2b.y - 2.0 * pm.y + p0.y) / denom2;
    r2.z = (p2b.z - 2.0 * pm.z + p0.z) / denom2;
  } else {
    r2 = Vec3{0.0, 0.0, 0.0};
  }
}

int bspline_degree_for_control_count(std::size_t n) {
  if (n <= 1) {
    return 0;
  }
  if (n <= 3) {
    return 1;
  }
  return std::min(3, static_cast<int>(n) - 1);
}

std::vector<Scalar> bspline_open_uniform_knots(std::size_t n, int p) {
  const int ni = static_cast<int>(n);
  const int m = ni + p + 1;
  std::vector<Scalar> U(static_cast<std::size_t>(m));
  for (int j = 0; j <= p; ++j) {
    U[static_cast<std::size_t>(j)] = 0.0;
  }
  for (int j = ni; j < m; ++j) {
    U[static_cast<std::size_t>(j)] = static_cast<Scalar>(ni - p);
  }
  for (int j = p + 1; j < ni; ++j) {
    U[static_cast<std::size_t>(j)] = static_cast<Scalar>(j - p);
  }
  return U;
}

void bspline_affine_scale_knots_to_span(std::vector<Scalar> &U, int p, std::size_t n,
                                        Scalar target_end) {
  if (U.empty()) {
    return;
  }
  const Scalar u0 = U[static_cast<std::size_t>(p)];
  const Scalar u1 = U[n];
  const Scalar den = u1 - u0;
  if (!(std::abs(den) > 1e-24)) {
    return;
  }
  const Scalar s = target_end / den;
  for (auto &x : U) {
    x = (x - u0) * s;
  }
}

/// 单段 clamped NURBS（与 `poles.size()-1` 次有理 Bézier 段同构）：结点前 `p+1` 个为 0、后 `p+1` 个为 1。
std::vector<Scalar> nurbs_clamped_bezier_knots(std::size_t n) {
  if (n <= 1) {
    return {0.0, 1.0};
  }
  const int ni = static_cast<int>(n);
  const int p = ni - 1;
  const int m = ni + p + 1;
  std::vector<Scalar> U(static_cast<std::size_t>(m), 0.0);
  for (int j = ni; j < m; ++j) {
    U[static_cast<std::size_t>(j)] = 1.0;
  }
  return U;
}

int bspline_find_span(std::size_t n, int p, Scalar u, const std::vector<Scalar> &U) {
  const int ni = static_cast<int>(n);
  if (ni <= 0 || static_cast<int>(U.size()) < ni + p + 1) {
    return p;
  }
  const Scalar ulo = U[static_cast<std::size_t>(p)];
  const Scalar uhi = U[n];
  if (u <= ulo) {
    return p;
  }
  if (u >= uhi) {
    return ni - 1;
  }
  int low = p;
  int high = ni;
  int mid = (low + high) / 2;
  while (u < U[static_cast<std::size_t>(mid)] ||
         u >= U[static_cast<std::size_t>(mid + 1)]) {
    if (u < U[static_cast<std::size_t>(mid)]) {
      high = mid;
    } else {
      low = mid;
    }
    mid = (low + high) / 2;
  }
  return mid;
}

Vec3 lerp_vec3(const Vec3 &a, const Vec3 &b, Scalar alpha) {
  return Vec3{a.x + (b.x - a.x) * alpha, a.y + (b.y - a.y) * alpha,
              a.z + (b.z - a.z) * alpha};
}

Point3 bspline_de_boor_point(std::span<const Point3> poles, const std::vector<Scalar> &U, int p,
                             int k, Scalar u) {
  if (p == 0) {
    return poles[static_cast<std::size_t>(k)];
  }
  std::vector<Point3> d(static_cast<std::size_t>(p + 1));
  for (int i = 0; i <= p; ++i) {
    d[static_cast<std::size_t>(i)] = poles[static_cast<std::size_t>(k - p + i)];
  }
  for (int r = 1; r <= p; ++r) {
    for (int i = p; i >= r; --i) {
      const int idx = k - p + i;
      const Scalar left = U[static_cast<std::size_t>(idx)];
      const Scalar right = U[static_cast<std::size_t>(idx + p - r + 1)];
      const Scalar denom = right - left;
      Scalar alpha = 0.0;
      if (std::abs(denom) > 1e-24) {
        alpha = (u - left) / denom;
      }
      d[static_cast<std::size_t>(i)] =
          lerp_point(d[static_cast<std::size_t>(i - 1)], d[static_cast<std::size_t>(i)], alpha);
    }
  }
  return d[static_cast<std::size_t>(p)];
}

Vec3 bspline_de_boor_vec(std::span<const Vec3> coeffs, const std::vector<Scalar> &U, int p, int k,
                         Scalar u) {
  if (p == 0) {
    return coeffs[static_cast<std::size_t>(k)];
  }
  std::vector<Vec3> d(static_cast<std::size_t>(p + 1));
  for (int i = 0; i <= p; ++i) {
    d[static_cast<std::size_t>(i)] = coeffs[static_cast<std::size_t>(k - p + i)];
  }
  for (int r = 1; r <= p; ++r) {
    for (int i = p; i >= r; --i) {
      const int idx = k - p + i;
      const Scalar left = U[static_cast<std::size_t>(idx)];
      const Scalar right = U[static_cast<std::size_t>(idx + p - r + 1)];
      const Scalar denom = right - left;
      Scalar alpha = 0.0;
      if (std::abs(denom) > 1e-24) {
        alpha = (u - left) / denom;
      }
      d[static_cast<std::size_t>(i)] =
          lerp_vec3(d[static_cast<std::size_t>(i - 1)], d[static_cast<std::size_t>(i)], alpha);
    }
  }
  return d[static_cast<std::size_t>(p)];
}

Scalar bspline_de_boor_scalar(std::span<const Scalar> coeffs, const std::vector<Scalar> &U, int p, int k,
                              Scalar u) {
  if (p == 0) {
    return coeffs[static_cast<std::size_t>(k)];
  }
  std::vector<Scalar> d(static_cast<std::size_t>(p + 1));
  for (int i = 0; i <= p; ++i) {
    d[static_cast<std::size_t>(i)] = coeffs[static_cast<std::size_t>(k - p + i)];
  }
  for (int r = 1; r <= p; ++r) {
    for (int i = p; i >= r; --i) {
      const int idx = k - p + i;
      const Scalar left = U[static_cast<std::size_t>(idx)];
      const Scalar right = U[static_cast<std::size_t>(idx + p - r + 1)];
      const Scalar denom = right - left;
      Scalar alpha = 0.0;
      if (std::abs(denom) > 1e-24) {
        alpha = (u - left) / denom;
      }
      d[static_cast<std::size_t>(i)] =
          d[static_cast<std::size_t>(i - 1)] +
          (d[static_cast<std::size_t>(i)] - d[static_cast<std::size_t>(i - 1)]) * alpha;
    }
  }
  return d[static_cast<std::size_t>(p)];
}

std::vector<Scalar> bspline_trim_knots_for_derivative(const std::vector<Scalar> &U);

void bspline_scalar_curve_eval_all_impl(std::span<const Scalar> coeffs, const std::vector<Scalar> &U, int p,
                                        Scalar t, Scalar &out_s, Scalar &out_ds, Scalar &out_d2s) {
  out_ds = 0.0;
  out_d2s = 0.0;
  const std::size_t n = coeffs.size();
  if (n == 0) {
    out_s = 0.0;
    return;
  }
  if (n == 1) {
    out_s = coeffs[0];
    return;
  }
  if (p < 0 || n + static_cast<std::size_t>(p) + 1 != U.size()) {
    out_s = coeffs[0];
    return;
  }
  const Scalar ulo = U[static_cast<std::size_t>(p)];
  const Scalar uhi = U[n];
  const Scalar uu = std::clamp(t, ulo, uhi);
  const int k = bspline_find_span(n, p, uu, U);
  out_s = bspline_de_boor_scalar(coeffs, U, p, k, uu);
  if (p < 1) {
    return;
  }
  std::vector<Scalar> qw;
  qw.reserve(n - 1);
  for (std::size_t i = 0; i + 1 < n; ++i) {
    const Scalar den = U[i + static_cast<std::size_t>(p) + 1] - U[i + 1];
    if (std::abs(den) < 1e-24) {
      qw.push_back(0.0);
    } else {
      qw.push_back(static_cast<Scalar>(p) * (coeffs[i + 1] - coeffs[i]) / den);
    }
  }
  std::vector<Scalar> Uk = bspline_trim_knots_for_derivative(U);
  const int p1 = p - 1;
  const int n1 = static_cast<int>(n) - 1;
  const int k1 = bspline_find_span(static_cast<std::size_t>(n1), p1, uu, Uk);
  out_ds = bspline_de_boor_scalar(std::span<const Scalar>(qw.data(), qw.size()), Uk, p1, k1, uu);
  if (p < 2) {
    return;
  }
  std::vector<Scalar> rw;
  rw.reserve(static_cast<std::size_t>(n1 - 1));
  for (int i = 0; i < n1 - 1; ++i) {
    const Scalar den =
        Uk[static_cast<std::size_t>(i + p1 + 1)] - Uk[static_cast<std::size_t>(i + 1)];
    if (std::abs(den) < 1e-24) {
      rw.push_back(0.0);
    } else {
      rw.push_back(static_cast<Scalar>(p1) * (qw[static_cast<std::size_t>(i + 1)] -
                                               qw[static_cast<std::size_t>(i)]) /
                   den);
    }
  }
  std::vector<Scalar> Uk2 = bspline_trim_knots_for_derivative(Uk);
  const int p2 = p - 2;
  const int n2 = n1 - 1;
  const int k2 = bspline_find_span(static_cast<std::size_t>(n2), p2, uu, Uk2);
  out_d2s = bspline_de_boor_scalar(std::span<const Scalar>(rw.data(), rw.size()), Uk2, p2, k2, uu);
}

std::vector<Scalar> bspline_trim_knots_for_derivative(const std::vector<Scalar> &U) {
  std::vector<Scalar> out;
  if (U.size() < 3) {
    return out;
  }
  out.reserve(U.size() - 2);
  for (std::size_t i = 1; i + 1 < U.size(); ++i) {
    out.push_back(U[i]);
  }
  return out;
}

void bspline_curve_eval_all_impl(std::span<const Point3> poles, const std::vector<Scalar> &U, int p,
                                 Scalar t, Point3 &out_p, Vec3 &out_d1, Vec3 &out_d2) {
  out_d1 = Vec3{0.0, 0.0, 0.0};
  out_d2 = Vec3{0.0, 0.0, 0.0};
  const std::size_t n = poles.size();
  if (n == 0) {
    out_p = Point3{};
    return;
  }
  if (n == 1) {
    out_p = poles[0];
    return;
  }
  if (p < 0 || static_cast<std::size_t>(n) + static_cast<std::size_t>(p) + 1 != U.size()) {
    out_p = poles[0];
    return;
  }
  const Scalar ulo = U[static_cast<std::size_t>(p)];
  const Scalar uhi = U[n];
  const Scalar uu = std::clamp(t, ulo, uhi);
  const int k = bspline_find_span(n, p, uu, U);
  out_p = bspline_de_boor_point(poles, U, p, k, uu);
  if (p < 1) {
    return;
  }
  std::vector<Vec3> qw;
  qw.reserve(n - 1);
  for (std::size_t i = 0; i + 1 < n; ++i) {
    const Scalar den = U[i + static_cast<std::size_t>(p) + 1] - U[i + 1];
    if (std::abs(den) < 1e-24) {
      qw.push_back(Vec3{0.0, 0.0, 0.0});
    } else {
      const Point3 &P0 = poles[i];
      const Point3 &P1 = poles[i + 1];
      const Vec3 diff{P1.x - P0.x, P1.y - P0.y, P1.z - P0.z};
      qw.push_back(detail::scale(diff, static_cast<Scalar>(p) / den));
    }
  }
  std::vector<Scalar> Uk = bspline_trim_knots_for_derivative(U);
  const int p1 = p - 1;
  const int n1 = static_cast<int>(n) - 1;
  const int k1 = bspline_find_span(static_cast<std::size_t>(n1), p1, uu, Uk);
  out_d1 = bspline_de_boor_vec(std::span<const Vec3>(qw.data(), qw.size()), Uk, p1, k1, uu);
  if (p < 2) {
    return;
  }
  std::vector<Vec3> rw;
  rw.reserve(static_cast<std::size_t>(n1 - 1));
  for (int i = 0; i < n1 - 1; ++i) {
    const Scalar den =
        Uk[static_cast<std::size_t>(i + p1 + 1)] - Uk[static_cast<std::size_t>(i + 1)];
    if (std::abs(den) < 1e-24) {
      rw.push_back(Vec3{0.0, 0.0, 0.0});
    } else {
      const Vec3 &Q0 = qw[static_cast<std::size_t>(i)];
      const Vec3 &Q1 = qw[static_cast<std::size_t>(i + 1)];
      const Vec3 ddiff{Q1.x - Q0.x, Q1.y - Q0.y, Q1.z - Q0.z};
      rw.push_back(detail::scale(ddiff, static_cast<Scalar>(p1) / den));
    }
  }
  std::vector<Scalar> Uk2 = bspline_trim_knots_for_derivative(Uk);
  const int p2 = p - 2;
  const int n2 = n1 - 1;
  const int k2 = bspline_find_span(static_cast<std::size_t>(n2), p2, uu, Uk2);
  out_d2 = bspline_de_boor_vec(std::span<const Vec3>(rw.data(), rw.size()), Uk2, p2, k2, uu);
}

void bspline_curve_eval_all(std::span<const Point3> poles, Scalar t, Point3 &out_p, Vec3 &out_d1,
                            Vec3 &out_d2) {
  const std::size_t n = poles.size();
  if (n == 0) {
    out_p = Point3{};
    out_d1 = out_d2 = Vec3{0.0, 0.0, 0.0};
    return;
  }
  if (n == 1) {
    out_p = poles[0];
    out_d1 = out_d2 = Vec3{0.0, 0.0, 0.0};
    return;
  }
  const int p = bspline_degree_for_control_count(n);
  std::vector<Scalar> U = bspline_open_uniform_knots(n, p);
  const Scalar target_end = std::max<Scalar>(1.0, static_cast<Scalar>(n - 1));
  bspline_affine_scale_knots_to_span(U, p, n, target_end);
  bspline_curve_eval_all_impl(poles, U, p, t, out_p, out_d1, out_d2);
}

bool bspline_knots_valid_for_curve(const detail::CurveRecord &curve, int &p_out) {
  const std::size_t n = curve.poles.size();
  if (n < 2) {
    return false;
  }
  const auto &U = curve.knots_u;
  if (U.size() < n + 2) {
    return false;
  }
  const int p = static_cast<int>(U.size()) - static_cast<int>(n) - 1;
  if (p < 1 || static_cast<std::size_t>(n) + static_cast<std::size_t>(p) + 1 != U.size()) {
    return false;
  }
  if (p >= static_cast<int>(n)) {
    return false;
  }
  for (std::size_t i = 1; i < U.size(); ++i) {
    if (!std::isfinite(U[i]) || U[i] < U[i - 1] - static_cast<Scalar>(1e-14)) {
      return false;
    }
  }
  p_out = p;
  return true;
}

void bspline_curve_eval_record(const detail::CurveRecord &curve, Scalar t, Point3 &out_p, Vec3 &out_d1,
                               Vec3 &out_d2) {
  const std::size_t n = curve.poles.size();
  if (n == 0) {
    out_p = Point3{};
    out_d1 = out_d2 = Vec3{0.0, 0.0, 0.0};
    return;
  }
  if (n == 1) {
    out_p = curve.poles[0];
    out_d1 = out_d2 = Vec3{0.0, 0.0, 0.0};
    return;
  }
  int p = 0;
  if (bspline_knots_valid_for_curve(curve, p)) {
    bspline_curve_eval_all_impl(std::span<const Point3>(curve.poles.data(), curve.poles.size()),
                                curve.knots_u, p, t, out_p, out_d1, out_d2);
  } else {
    bspline_curve_eval_all(std::span<const Point3>(curve.poles.data(), curve.poles.size()), t, out_p,
                           out_d1, out_d2);
  }
}

Point3 nurbs_de_boor_point(std::span<const Point3> poles, std::span<const Scalar> w,
                           const std::vector<Scalar> &U, int p, int k, Scalar u) {
  struct R4 {
    Scalar x{}, y{}, z{}, ww{};
  };
  auto lerp_r4 = [](const R4 &a, const R4 &b, Scalar s) -> R4 {
    return R4{a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s, a.z + (b.z - a.z) * s,
              a.ww + (b.ww - a.ww) * s};
  };
  std::vector<R4> d(static_cast<std::size_t>(p + 1));
  for (int i = 0; i <= p; ++i) {
    const int idx = k - p + i;
    const Scalar ww = w[static_cast<std::size_t>(idx)];
    const auto &P = poles[static_cast<std::size_t>(idx)];
    d[static_cast<std::size_t>(i)] = {P.x * ww, P.y * ww, P.z * ww, ww};
  }
  for (int r = 1; r <= p; ++r) {
    for (int i = p; i >= r; --i) {
      const int idx = k - p + i;
      const Scalar left = U[static_cast<std::size_t>(idx)];
      const Scalar right = U[static_cast<std::size_t>(idx + p - r + 1)];
      Scalar denom = right - left;
      Scalar alpha = 0.0;
      if (std::abs(denom) > 1e-24) {
        alpha = (u - left) / denom;
      }
      d[static_cast<std::size_t>(i)] =
          lerp_r4(d[static_cast<std::size_t>(i - 1)], d[static_cast<std::size_t>(i)], alpha);
    }
  }
  const auto &h = d[static_cast<std::size_t>(p)];
  const Scalar wout = std::abs(h.ww) > 1e-18 ? h.ww : 1e-18;
  return Point3{h.x / wout, h.y / wout, h.z / wout};
}

Point3 nurbs_curve_eval_point(std::span<const Point3> poles, std::span<const Scalar> w,
                              const std::vector<Scalar> &U, int p, Scalar u) {
  const std::size_t n = poles.size();
  if (n == 0) {
    return Point3{};
  }
  if (n == 1) {
    return poles[0];
  }
  if (w.size() != n || p < 0 ||
      static_cast<std::size_t>(n) + static_cast<std::size_t>(p) + 1 != U.size()) {
    return poles[0];
  }
  const Scalar ulo = U[static_cast<std::size_t>(p)];
  const Scalar uhi = U[n];
  const Scalar uu = std::clamp(u, ulo, uhi);
  const int k = bspline_find_span(n, p, uu, U);
  return nurbs_de_boor_point(poles, w, U, p, k, uu);
}

void nurbs_curve_eval_all_record(const detail::CurveRecord &curve, Scalar t, Point3 &out_p, Vec3 &out_d1,
                                 Vec3 &out_d2) {
  out_d1 = Vec3{0.0, 0.0, 0.0};
  out_d2 = Vec3{0.0, 0.0, 0.0};
  const std::size_t n = curve.poles.size();
  if (n == 0) {
    out_p = Point3{};
    return;
  }
  if (n == 1) {
    out_p = curve.poles[0];
    return;
  }
  if (curve.weights.size() != n) {
    out_p = curve.poles[0];
    return;
  }
  int p = 0;
  if (!bspline_knots_valid_for_curve(curve, p)) {
    detail::rational_bezier_eval_all(curve.poles, curve.weights, t, out_p, out_d1, out_d2);
    return;
  }
  const auto &U = curve.knots_u;
  const Scalar ulo = U[static_cast<std::size_t>(p)];
  const Scalar uhi = U[n];
  const Scalar uu = std::clamp(t, ulo, uhi);

  std::vector<Scalar> cx(n);
  std::vector<Scalar> cy(n);
  std::vector<Scalar> cz(n);
  std::vector<Scalar> cw(n);
  for (std::size_t i = 0; i < n; ++i) {
    const Scalar wi = curve.weights[i];
    const auto &Pi = curve.poles[i];
    cx[i] = Pi.x * wi;
    cy[i] = Pi.y * wi;
    cz[i] = Pi.z * wi;
    cw[i] = wi;
  }
  Scalar X{}, Xd{}, Xdd{};
  Scalar Y{}, Yd{}, Ydd{};
  Scalar Z{}, Zd{}, Zdd{};
  Scalar W{}, Wd{}, Wdd{};
  bspline_scalar_curve_eval_all_impl(std::span<const Scalar>(cx.data(), cx.size()), U, p, uu, X, Xd,
                                     Xdd);
  bspline_scalar_curve_eval_all_impl(std::span<const Scalar>(cy.data(), cy.size()), U, p, uu, Y, Yd,
                                     Ydd);
  bspline_scalar_curve_eval_all_impl(std::span<const Scalar>(cz.data(), cz.size()), U, p, uu, Z, Zd,
                                     Zdd);
  bspline_scalar_curve_eval_all_impl(std::span<const Scalar>(cw.data(), cw.size()), U, p, uu, W, Wd,
                                     Wdd);

  const Scalar w_safe =
      std::abs(W) > static_cast<Scalar>(1e-30)
          ? W
          : (W >= static_cast<Scalar>(0) ? static_cast<Scalar>(1e-30) : static_cast<Scalar>(-1e-30));
  const Scalar w2 = w_safe * w_safe;
  const Scalar w4 = w2 * w2;
  const Scalar two_wwd = static_cast<Scalar>(2.0) * w_safe * Wd;
  out_p = Point3{X / w_safe, Y / w_safe, Z / w_safe};

  const Scalar t1x = Xd * w_safe - X * Wd;
  const Scalar t1y = Yd * w_safe - Y * Wd;
  const Scalar t1z = Zd * w_safe - Z * Wd;
  out_d1 = Vec3{t1x / w2, t1y / w2, t1z / w2};

  const Scalar t2x = Xdd * w_safe - X * Wdd;
  const Scalar t2y = Ydd * w_safe - Y * Wdd;
  const Scalar t2z = Zdd * w_safe - Z * Wdd;
  out_d2 = Vec3{(t2x * w2 - t1x * two_wwd) / w4, (t2y * w2 - t1y * two_wwd) / w4,
                (t2z * w2 - t1z * two_wwd) / w4};
}

Point3 evaluate_bspline_curve_point(std::span<const Point3> poles, Scalar t) {
  Point3 p{};
  Vec3 d1{};
  Vec3 d2{};
  bspline_curve_eval_all(poles, t, p, d1, d2);
  return p;
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
  case detail::CurveKind::BSpline: {
    if (curve.poles.empty()) {
      return curve.origin;
    }
    Point3 ep{};
    Vec3 e1{}, e2{};
    bspline_curve_eval_record(curve, t, ep, e1, e2);
    return ep;
  }
  case detail::CurveKind::CompositePolyline:
    return evaluate_polyline_point(curve.poles, t);
  case detail::CurveKind::Nurbs: {
    Point3 ep{};
    Vec3 e1{}, e2{};
    nurbs_curve_eval_all_record(curve, t, ep, e1, e2);
    return ep;
  }
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

bool bspline_knot_vector_non_decreasing(std::span<const Scalar> U) {
  for (std::size_t i = 1; i < U.size(); ++i) {
    if (!(std::isfinite(U[i - 1]) && std::isfinite(U[i])) ||
        U[i] + static_cast<Scalar>(1e-30) < U[i - 1]) {
      return false;
    }
  }
  return true;
}

int infer_bspline_degree_from_knot_length(std::size_t knot_len, int n_ctrl) {
  if (n_ctrl < 2 || knot_len < 2) {
    return -1;
  }
  const int p = static_cast<int>(knot_len) - n_ctrl - 1;
  if (p < 0 || p >= n_ctrl) {
    return -1;
  }
  return p;
}

// 生成或校验单轴结点，并仿射对齐到与 `surface_domain` 一致的跨度 [0, max(1,n-1)]。
bool resolve_bspline_surface_axis(int n_ctrl, int degree_hint,
                                  const std::vector<Scalar> &knots_in,
                                  std::vector<Scalar> &knots_out, int &p_out) {
  if (n_ctrl < 2) {
    return false;
  }
  const int n = n_ctrl;
  const Scalar span_end = std::max<Scalar>(1.0, static_cast<Scalar>(n - 1));
  if (!knots_in.empty()) {
    p_out = infer_bspline_degree_from_knot_length(knots_in.size(), n);
    if (p_out < 0) {
      return false;
    }
    if (degree_hint >= 0 && degree_hint != p_out) {
      return false;
    }
    knots_out = knots_in;
    if (!bspline_knot_vector_non_decreasing(knots_out)) {
      return false;
    }
    bspline_affine_scale_knots_to_span(knots_out, p_out, static_cast<std::size_t>(n),
                                       span_end);
    return true;
  }
  p_out = degree_hint >= 0 ? degree_hint
                           : bspline_degree_for_control_count(static_cast<std::size_t>(n));
  if (p_out < 0 || p_out >= n) {
    return false;
  }
  knots_out = bspline_open_uniform_knots(static_cast<std::size_t>(n), p_out);
  bspline_affine_scale_knots_to_span(knots_out, p_out, static_cast<std::size_t>(n),
                                     span_end);
  return true;
}


bool point_in_uv_polygon_raycast(Point2 p, std::span<const Point2> poly) {
  if (poly.size() < 3) {
    return true;
  }
  return detail::point_in_polygon_2d_raycast(p, poly);
}

Point2 closest_point_on_segment_2d(Point2 p, Point2 a, Point2 b) {
  const Scalar abx = b.x - a.x;
  const Scalar aby = b.y - a.y;
  const Scalar apx = p.x - a.x;
  const Scalar apy = p.y - a.y;
  const Scalar den = abx * abx + aby * aby;
  Scalar t = den > static_cast<Scalar>(1e-30) ? (apx * abx + apy * aby) / den : 0.0;
  t = std::clamp(t, static_cast<Scalar>(0.0), static_cast<Scalar>(1.0));
  return Point2{a.x + t * abx, a.y + t * aby};
}

void apply_trimmed_surface_uv_in_parameter_space(const detail::SurfaceRecord &surf,
                                                   Scalar &u, Scalar &v) {
  u = std::clamp(u, surf.trim_u_min, surf.trim_u_max);
  v = std::clamp(v, surf.trim_v_min, surf.trim_v_max);
  if (surf.trim_uv_loop.size() < 3) {
    return;
  }
  const Point2 p{u, v};
  const bool in_outer = point_in_uv_polygon_raycast(p, surf.trim_uv_loop);
  bool in_hole = false;
  for (const auto &hole : surf.trim_uv_holes) {
    if (hole.size() >= 3 && point_in_uv_polygon_raycast(p, hole)) {
      in_hole = true;
      break;
    }
  }
  if (in_outer && !in_hole) {
    return;
  }

  auto project_to_loop_boundary = [&](std::span<const Point2> poly, Scalar &bu, Scalar &bv) {
    Scalar best_d2 = std::numeric_limits<Scalar>::infinity();
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
      const Point2 q = closest_point_on_segment_2d(p, poly[i], poly[(i + 1) % n]);
      const Scalar dx = p.x - q.x;
      const Scalar dy = p.y - q.y;
      const Scalar d2 = dx * dx + dy * dy;
      if (d2 < best_d2) {
        best_d2 = d2;
        bu = q.x;
        bv = q.y;
      }
    }
  };

  Scalar bu = u;
  Scalar bv = v;
  if (!in_outer) {
    project_to_loop_boundary(surf.trim_uv_loop, bu, bv);
  } else {
    Scalar best_d2 = std::numeric_limits<Scalar>::infinity();
    for (const auto &hole : surf.trim_uv_holes) {
      if (hole.size() < 3) {
        continue;
      }
      Scalar hu = u;
      Scalar hv = v;
      project_to_loop_boundary(std::span<const Point2>(hole.data(), hole.size()), hu, hv);
      const Scalar dx = p.x - hu;
      const Scalar dy = p.y - hv;
      const Scalar d2 = dx * dx + dy * dy;
      if (d2 < best_d2) {
        best_d2 = d2;
        bu = hu;
        bv = hv;
      }
    }
  }
  u = std::clamp(bu, surf.trim_u_min, surf.trim_u_max);
  v = std::clamp(bv, surf.trim_v_min, surf.trim_v_max);
}

bool trimmed_surface_uv_feasible(Scalar u, Scalar v,
                                 const detail::SurfaceRecord &surf,
                                 Scalar tol = static_cast<Scalar>(1e-7)) {
  if (u + tol < surf.trim_u_min || u - tol > surf.trim_u_max || v + tol < surf.trim_v_min ||
      v - tol > surf.trim_v_max) {
    return false;
  }
  if (surf.trim_uv_loop.size() < 3) {
    return true;
  }
  const Point2 p{u, v};
  const bool in_outer = point_in_uv_polygon_raycast(p, surf.trim_uv_loop);
  if (!in_outer) {
    const auto &poly = surf.trim_uv_loop;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
      const Point2 q = closest_point_on_segment_2d(p, poly[i], poly[(i + 1) % n]);
      if (std::hypot(p.x - q.x, p.y - q.y) <= tol * static_cast<Scalar>(50.0)) {
        return true;
      }
    }
    return false;
  }
  for (const auto &hole : surf.trim_uv_holes) {
    if (hole.size() < 3) {
      continue;
    }
    if (point_in_uv_polygon_raycast(p, hole)) {
      const std::size_t n = hole.size();
      for (std::size_t i = 0; i < n; ++i) {
        const Point2 q = closest_point_on_segment_2d(p, hole[i], hole[(i + 1) % n]);
        if (std::hypot(p.x - q.x, p.y - q.y) <= tol * static_cast<Scalar>(50.0)) {
          return true;
        }
      }
      return false;
    }
  }
  return true;
}
