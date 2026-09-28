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
  const auto span = domain.max - domain.min;
  const auto step = std::max(span * 1e-4, 1e-6);
  const auto ta = std::clamp(ct - step, domain.min, domain.max);
  const auto tb = std::clamp(ct + step, domain.min, domain.max);
  const auto pa = evaluate_pcurve_point(pc, ta);
  const auto pb = evaluate_pcurve_point(pc, tb);
  const Scalar den = tb - ta;
  Vec2 r1{1.0, 0.0};
  if (std::abs(den) > 1e-30) {
    r1 = Vec2{(pb.x - pa.x) / den, (pb.y - pa.y) / den};
  }
  const auto hh = std::max(span * 1e-3, 1e-5);
  const auto t0 = std::clamp(ct - hh, domain.min, domain.max);
  const auto t2 = std::clamp(ct + hh, domain.min, domain.max);
  const auto p0 = evaluate_pcurve_point(pc, t0);
  const auto p2 = evaluate_pcurve_point(pc, t2);
  const Scalar d_left = ct - t0;
  const Scalar d_right = t2 - ct;
  const Scalar denom2 = d_left * d_right;
  Vec2 r2{0.0, 0.0};
  if (std::abs(denom2) > 1e-30) {
    r2 = Vec2{(p2.x - 2.0 * out.point.x + p0.x) / denom2,
              (p2.y - 2.0 * out.point.y + p0.y) / denom2};
  }
  out.tangent = normalize2_or_default(r1, Vec2{1.0, 0.0});
  out.curvature = pcurve_curvature_signed(r1, r2);
  const auto order = std::max(0, deriv_order);
  out.derivatives.resize(static_cast<std::size_t>(order), Vec2{0.0, 0.0});
  if (order > 0) {
    out.derivatives[0] = r1;
  }
  if (order > 1) {
    out.derivatives[1] = r2;
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
  const auto t = std::clamp(closest_pcurve_parameter(pc, point), domain.min,
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
  Vec3 r1_raw{1.0, 0.0, 0.0};
  Vec3 r2_raw{0.0, 0.0, 0.0};
  bool have_r2 = false;
  bool skip_post = false;

  switch (curve.kind) {
  case detail::CurveKind::Line:
    result.point =
        detail::add_point_vec(curve.origin, detail::scale(curve.direction, t));
    r1_raw = curve.direction;
    have_r2 = true;
    break;
  case detail::CurveKind::LineSegment: {
    if (curve.poles.size() >= 2) {
      const auto ct = std::clamp(t, 0.0, 1.0);
      result.point = lerp_point(curve.poles.front(), curve.poles.back(), ct);
      r1_raw = detail::subtract(curve.poles.back(), curve.poles.front());
    } else {
      result.point = curve.origin;
      r1_raw = Vec3{1.0, 0.0, 0.0};
    }
    have_r2 = true;
    break;
  }
  case detail::CurveKind::Circle:
    result.point = point_from_local(
        curve.origin, OrthoFrame{curve.axis_u, curve.axis_v, curve.normal},
        curve.radius * std::cos(t), curve.radius * std::sin(t), 0.0);
    r1_raw = vec_from_local(OrthoFrame{curve.axis_u, curve.axis_v, curve.normal},
                            -curve.radius * std::sin(t), curve.radius * std::cos(t), 0.0);
    r2_raw = vec_from_local(OrthoFrame{curve.axis_u, curve.axis_v, curve.normal},
                            -curve.radius * std::cos(t), -curve.radius * std::sin(t), 0.0);
    have_r2 = true;
    break;
  case detail::CurveKind::Ellipse:
    result.point = Point3{
        curve.origin.x + curve.axis_u.x * std::cos(t) + curve.axis_v.x * std::sin(t),
        curve.origin.y + curve.axis_u.y * std::cos(t) + curve.axis_v.y * std::sin(t),
        curve.origin.z + curve.axis_u.z * std::cos(t) + curve.axis_v.z * std::sin(t)};
    r1_raw = Vec3{-curve.axis_u.x * std::sin(t) + curve.axis_v.x * std::cos(t),
                  -curve.axis_u.y * std::sin(t) + curve.axis_v.y * std::cos(t),
                  -curve.axis_u.z * std::sin(t) + curve.axis_v.z * std::cos(t)};
    r2_raw = Vec3{-curve.axis_u.x * std::cos(t) - curve.axis_v.x * std::sin(t),
                  -curve.axis_u.y * std::cos(t) - curve.axis_v.y * std::sin(t),
                  -curve.axis_u.z * std::cos(t) - curve.axis_v.z * std::sin(t)};
    have_r2 = true;
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
        r1_raw = bezier_first_derivative(curve.poles, clamped_t);
        r2_raw = bezier_second_derivative(curve.poles, clamped_t);
        have_r2 = true;
      } else if (curve.kind == detail::CurveKind::BSpline) {
        bspline_curve_eval_record(curve, clamped_t, result.point, r1_raw, r2_raw);
        have_r2 = true;
      } else {
        nurbs_curve_eval_all_record(curve, clamped_t, result.point, r1_raw, r2_raw);
        have_r2 = true;
      }
    } else {
      result.point = curve.origin;
      r1_raw = Vec3{1.0, 0.0, 0.0};
      have_r2 = true;
    }
    break;
  }
  case detail::CurveKind::Parabola: {
    const auto domain = curve_domain(curve);
    const auto ct = std::clamp(t, domain.min, domain.max);
    const auto focal = std::max(curve.param_a, 1e-9);
    const Scalar x = ct;
    const Scalar y = (x * x) / (4.0 * focal);
    result.point = Point3{curve.origin.x + curve.axis_u.x * x + curve.axis_v.x * y,
                           curve.origin.y + curve.axis_u.y * x + curve.axis_v.y * y,
                           curve.origin.z + curve.axis_u.z * x + curve.axis_v.z * y};
    const Scalar dydx = x / (2.0 * focal);
    r1_raw = Vec3{curve.axis_u.x + curve.axis_v.x * dydx,
                  curve.axis_u.y + curve.axis_v.y * dydx,
                  curve.axis_u.z + curve.axis_v.z * dydx};
    const Scalar d2ydx2 = 1.0 / (2.0 * focal);
    r2_raw = Vec3{curve.axis_v.x * d2ydx2, curve.axis_v.y * d2ydx2,
                  curve.axis_v.z * d2ydx2};
    have_r2 = true;
    break;
  }
  case detail::CurveKind::Hyperbola: {
    const auto domain = curve_domain(curve);
    const auto ct = std::clamp(t, domain.min, domain.max);
    const auto a = std::max(curve.param_a, 1e-9);
    const auto b = std::max(curve.param_b, 1e-9);
    const Scalar xh = a * std::cosh(ct);
    const Scalar yh = b * std::sinh(ct);
    result.point = Point3{curve.origin.x + curve.axis_u.x * xh + curve.axis_v.x * yh,
                           curve.origin.y + curve.axis_u.y * xh + curve.axis_v.y * yh,
                           curve.origin.z + curve.axis_u.z * xh + curve.axis_v.z * yh};
    const Scalar dx = a * std::sinh(ct);
    const Scalar dy = b * std::cosh(ct);
    r1_raw = Vec3{curve.axis_u.x * dx + curve.axis_v.x * dy,
                  curve.axis_u.y * dx + curve.axis_v.y * dy,
                  curve.axis_u.z * dx + curve.axis_v.z * dy};
    const Scalar ddx = a * std::cosh(ct);
    const Scalar ddy = b * std::sinh(ct);
    r2_raw = Vec3{curve.axis_u.x * ddx + curve.axis_v.x * ddy,
                  curve.axis_u.y * ddx + curve.axis_v.y * ddy,
                  curve.axis_u.z * ddx + curve.axis_v.z * ddy};
    have_r2 = true;
    break;
  }
  case detail::CurveKind::CompositePolyline: {
    const auto domain = curve_domain(curve);
    const auto eval_fn = [&curve](Scalar value) {
      return evaluate_curve_point(curve, value);
    };
    const auto clamped_t = std::clamp(t, domain.min, domain.max);
    result.point = eval_fn(clamped_t);
    curve_finite_diff_r12(eval_fn, clamped_t, domain.min, domain.max, r1_raw, r2_raw);
    have_r2 = true;
    break;
  }
  case detail::CurveKind::CompositeChain: {
    if (curve.children.empty()) {
      result.point = curve.origin;
      r1_raw = Vec3{1.0, 0.0, 0.0};
      have_r2 = true;
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
    skip_post = true;
    break;
  }
  default:
    result.point = curve.origin;
    r1_raw = Vec3{1.0, 0.0, 0.0};
    have_r2 = true;
    break;
  }

  const auto order = std::max(0, deriv_order);
  if (!skip_post) {
    result.tangent = normalize_or_default(r1_raw, Vec3{1.0, 0.0, 0.0});
    result.curvature =
        curve_curvature_from_rp_rpp(r1_raw, have_r2 ? r2_raw : Vec3{0.0, 0.0, 0.0});
    result.derivatives.resize(static_cast<std::size_t>(order), Vec3{0.0, 0.0, 0.0});
    if (order > 0) {
      result.derivatives[0] = r1_raw;
    }
    if (order > 1 && have_r2) {
      result.derivatives[1] = r2_raw;
    }
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

namespace {

long double closest_distance(const Point3 &lhs, const Point3 &rhs) {
  const long double dx = static_cast<long double>(lhs.x) - rhs.x;
  const long double dy = static_cast<long double>(lhs.y) - rhs.y;
  const long double dz = static_cast<long double>(lhs.z) - rhs.z;
  return std::hypot(dx, dy, dz);
}

long double vector_length(const Vec3 &value) {
  return std::hypot(static_cast<long double>(value.x),
                    static_cast<long double>(value.y),
                    static_cast<long double>(value.z));
}

long double point_delta_length(const Point3 &lhs, const Point3 &rhs) {
  return std::hypot(static_cast<long double>(lhs.x) - rhs.x,
                    static_cast<long double>(lhs.y) - rhs.y,
                    static_cast<long double>(lhs.z) - rhs.z);
}

bool closest_search_point(const detail::KernelState &state, CurveId curve_id,
                          Scalar t, Point3 &point, std::uint32_t depth = 0) {
  if (depth > 64) {
    return false;
  }
  const auto found = state.curves.find(curve_id.value);
  if (found == state.curves.end()) {
    return false;
  }
  const auto &curve = found->second;
  switch (curve.kind) {
  case detail::CurveKind::Line:
    point = detail::add_point_vec(curve.origin, detail::scale(curve.direction, t));
    break;
  case detail::CurveKind::Circle:
    point = Point3{curve.origin.x + curve.radius *
                                          (curve.axis_u.x * std::cos(t) +
                                           curve.axis_v.x * std::sin(t)),
                   curve.origin.y + curve.radius *
                                          (curve.axis_u.y * std::cos(t) +
                                           curve.axis_v.y * std::sin(t)),
                   curve.origin.z + curve.radius *
                                          (curve.axis_u.z * std::cos(t) +
                                           curve.axis_v.z * std::sin(t))};
    break;
  case detail::CurveKind::Ellipse:
    point = Point3{curve.origin.x + curve.axis_u.x * std::cos(t) +
                                          curve.axis_v.x * std::sin(t),
                   curve.origin.y + curve.axis_u.y * std::cos(t) +
                                          curve.axis_v.y * std::sin(t),
                   curve.origin.z + curve.axis_u.z * std::cos(t) +
                                          curve.axis_v.z * std::sin(t)};
    break;
  case detail::CurveKind::CompositeChain: {
    if (curve.children.empty()) {
      return false;
    }
    const auto count = static_cast<Scalar>(curve.children.size());
    const auto ct = std::clamp(t, 0.0, count);
    const auto mapped = std::min(ct, std::nextafter(count, 0.0));
    const auto child_index = std::min<std::size_t>(
        static_cast<std::size_t>(std::max<Scalar>(0.0, std::floor(mapped))),
        curve.children.size() - 1);
    return closest_search_point(state, curve.children[child_index],
                                mapped - static_cast<Scalar>(child_index), point,
                                depth + 1);
  }
  default:
    point = evaluate_curve_point(curve, t);
    break;
  }
  return finite_point(point);
}

long double spline_speed_bound(const detail::CurveRecord &curve) {
  const auto count = curve.poles.size();
  int degree = 0;
  if (count < 2 || !bspline_knots_valid_for_curve(curve, degree) || degree < 1) {
    return 0.0L;
  }
  const auto &knots = curve.knots_u;
  if (curve.kind != detail::CurveKind::Nurbs) {
    long double bound = 0.0L;
    for (std::size_t i = 0; i + 1 < count; ++i) {
      const long double denominator =
          static_cast<long double>(knots[i + static_cast<std::size_t>(degree) + 1]) -
          knots[i + 1];
      if (denominator > 0.0L) {
        bound = std::max(
            bound, static_cast<long double>(degree) *
                       point_delta_length(curve.poles[i + 1], curve.poles[i]) /
                       denominator);
      }
    }
    return bound;
  }

  if (curve.weights.size() != count) {
    return std::numeric_limits<long double>::infinity();
  }
  long double minimum_weight = std::numeric_limits<long double>::infinity();
  long double maximum_weighted_point = 0.0L;
  for (std::size_t i = 0; i < count; ++i) {
    const long double weight = curve.weights[i];
    minimum_weight = std::min(minimum_weight, weight);
    maximum_weighted_point = std::max(
        maximum_weighted_point,
        weight * std::hypot(static_cast<long double>(curve.poles[i].x),
                            static_cast<long double>(curve.poles[i].y),
                            static_cast<long double>(curve.poles[i].z)));
  }
  if (!(minimum_weight > 0.0L)) {
    return std::numeric_limits<long double>::infinity();
  }
  long double derivative_xyz = 0.0L;
  long double derivative_weight = 0.0L;
  for (std::size_t i = 0; i + 1 < count; ++i) {
    const long double denominator =
        static_cast<long double>(knots[i + static_cast<std::size_t>(degree) + 1]) -
        knots[i + 1];
    if (!(denominator > 0.0L)) {
      continue;
    }
    const long double factor = static_cast<long double>(degree) / denominator;
    const long double x0 = static_cast<long double>(curve.weights[i]) * curve.poles[i].x;
    const long double y0 = static_cast<long double>(curve.weights[i]) * curve.poles[i].y;
    const long double z0 = static_cast<long double>(curve.weights[i]) * curve.poles[i].z;
    const long double x1 = static_cast<long double>(curve.weights[i + 1]) * curve.poles[i + 1].x;
    const long double y1 = static_cast<long double>(curve.weights[i + 1]) * curve.poles[i + 1].y;
    const long double z1 = static_cast<long double>(curve.weights[i + 1]) * curve.poles[i + 1].z;
    derivative_xyz = std::max(
        derivative_xyz, factor * std::hypot(x1 - x0, y1 - y0, z1 - z0));
    derivative_weight = std::max(
        derivative_weight,
        factor * std::abs(static_cast<long double>(curve.weights[i + 1]) -
                          curve.weights[i]));
  }
  return derivative_xyz / minimum_weight +
         maximum_weighted_point * derivative_weight /
             (minimum_weight * minimum_weight);
}

long double closest_search_speed_bound(const detail::KernelState &state,
                                       CurveId curve_id, Scalar t0, Scalar t1,
                                       std::uint32_t depth = 0) {
  if (depth > 64) {
    return std::numeric_limits<long double>::infinity();
  }
  const auto found = state.curves.find(curve_id.value);
  if (found == state.curves.end()) {
    return std::numeric_limits<long double>::infinity();
  }
  const auto &curve = found->second;
  switch (curve.kind) {
  case detail::CurveKind::Line:
    return vector_length(curve.direction);
  case detail::CurveKind::LineSegment:
    return curve.poles.size() >= 2
               ? point_delta_length(curve.poles.back(), curve.poles.front())
               : 0.0L;
  case detail::CurveKind::Circle:
    return std::abs(static_cast<long double>(curve.radius)) *
           (vector_length(curve.axis_u) + vector_length(curve.axis_v));
  case detail::CurveKind::Ellipse:
    return vector_length(curve.axis_u) + vector_length(curve.axis_v);
  case detail::CurveKind::Parabola: {
    const long double focal = std::max<long double>(curve.param_a, 1e-9L);
    const long double max_t = std::max(std::abs(static_cast<long double>(t0)),
                                       std::abs(static_cast<long double>(t1)));
    return vector_length(curve.axis_u) +
           max_t * vector_length(curve.axis_v) / (2.0L * focal);
  }
  case detail::CurveKind::Hyperbola: {
    const long double max_t = std::max(std::abs(static_cast<long double>(t0)),
                                       std::abs(static_cast<long double>(t1)));
    return std::abs(static_cast<long double>(curve.param_a)) * std::sinh(max_t) *
               vector_length(curve.axis_u) +
           std::abs(static_cast<long double>(curve.param_b)) * std::cosh(max_t) *
               vector_length(curve.axis_v);
  }
  case detail::CurveKind::Bezier: {
    long double bound = 0.0L;
    const long double degree =
        curve.poles.empty() ? 0.0L : static_cast<long double>(curve.poles.size() - 1);
    for (std::size_t i = 0; i + 1 < curve.poles.size(); ++i) {
      bound = std::max(bound, degree *
                                   point_delta_length(curve.poles[i + 1], curve.poles[i]));
    }
    return bound;
  }
  case detail::CurveKind::BSpline:
  case detail::CurveKind::Nurbs:
    return spline_speed_bound(curve);
  case detail::CurveKind::CompositePolyline: {
    long double bound = 0.0L;
    for (std::size_t i = 0; i + 1 < curve.poles.size(); ++i) {
      bound = std::max(bound, point_delta_length(curve.poles[i + 1], curve.poles[i]));
    }
    return bound;
  }
  case detail::CurveKind::CompositeChain: {
    if (curve.children.empty()) {
      return 0.0L;
    }
    const auto midpoint = (t0 + t1) * 0.5;
    const auto child_index = std::min<std::size_t>(
        static_cast<std::size_t>(std::max<Scalar>(0.0, std::floor(midpoint))),
        curve.children.size() - 1);
    const auto offset = static_cast<Scalar>(child_index);
    return closest_search_speed_bound(state, curve.children[child_index],
                                      std::clamp(t0 - offset, 0.0, 1.0),
                                      std::clamp(t1 - offset, 0.0, 1.0),
                                      depth + 1);
  }
  default:
    return std::numeric_limits<long double>::infinity();
  }
}

bool closest_search_derivative(const detail::KernelState &state,
                               CurveId curve_id, Scalar t, Vec3 &derivative,
                               std::uint32_t depth = 0) {
  if (depth > 64) {
    return false;
  }
  const auto found = state.curves.find(curve_id.value);
  if (found == state.curves.end()) {
    return false;
  }
  const auto &curve = found->second;
  switch (curve.kind) {
  case detail::CurveKind::Line:
    derivative = curve.direction;
    break;
  case detail::CurveKind::LineSegment:
    if (curve.poles.size() < 2) {
      return false;
    }
    derivative = detail::subtract(curve.poles.back(), curve.poles.front());
    break;
  case detail::CurveKind::Circle:
    derivative = Vec3{
        curve.radius * (-curve.axis_u.x * std::sin(t) + curve.axis_v.x * std::cos(t)),
        curve.radius * (-curve.axis_u.y * std::sin(t) + curve.axis_v.y * std::cos(t)),
        curve.radius * (-curve.axis_u.z * std::sin(t) + curve.axis_v.z * std::cos(t))};
    break;
  case detail::CurveKind::Ellipse:
    derivative = Vec3{-curve.axis_u.x * std::sin(t) + curve.axis_v.x * std::cos(t),
                      -curve.axis_u.y * std::sin(t) + curve.axis_v.y * std::cos(t),
                      -curve.axis_u.z * std::sin(t) + curve.axis_v.z * std::cos(t)};
    break;
  case detail::CurveKind::Parabola: {
    const auto focal = std::max(curve.param_a, 1e-9);
    derivative = Vec3{curve.axis_u.x + curve.axis_v.x * t / (2.0 * focal),
                      curve.axis_u.y + curve.axis_v.y * t / (2.0 * focal),
                      curve.axis_u.z + curve.axis_v.z * t / (2.0 * focal)};
    break;
  }
  case detail::CurveKind::Hyperbola:
    derivative = Vec3{
        curve.axis_u.x * curve.param_a * std::sinh(t) +
            curve.axis_v.x * curve.param_b * std::cosh(t),
        curve.axis_u.y * curve.param_a * std::sinh(t) +
            curve.axis_v.y * curve.param_b * std::cosh(t),
        curve.axis_u.z * curve.param_a * std::sinh(t) +
            curve.axis_v.z * curve.param_b * std::cosh(t)};
    break;
  case detail::CurveKind::Bezier:
    derivative = bezier_first_derivative(curve.poles, t);
    break;
  case detail::CurveKind::BSpline: {
    Point3 evaluated{};
    Vec3 second{};
    bspline_curve_eval_record(curve, t, evaluated, derivative, second);
    break;
  }
  case detail::CurveKind::Nurbs: {
    Point3 evaluated{};
    Vec3 second{};
    nurbs_curve_eval_all_record(curve, t, evaluated, derivative, second);
    break;
  }
  case detail::CurveKind::CompositePolyline: {
    if (curve.poles.size() < 2) {
      return false;
    }
    const auto index = std::min<std::size_t>(
        static_cast<std::size_t>(std::max<Scalar>(0.0, std::floor(t))),
        curve.poles.size() - 2);
    derivative = detail::subtract(curve.poles[index + 1], curve.poles[index]);
    break;
  }
  case detail::CurveKind::CompositeChain: {
    if (curve.children.empty()) {
      return false;
    }
    const auto count = static_cast<Scalar>(curve.children.size());
    const auto mapped = std::min(std::clamp(t, 0.0, count),
                                 std::nextafter(count, 0.0));
    const auto child_index = std::min<std::size_t>(
        static_cast<std::size_t>(std::max<Scalar>(0.0, std::floor(mapped))),
        curve.children.size() - 1);
    return closest_search_derivative(
        state, curve.children[child_index],
        mapped - static_cast<Scalar>(child_index), derivative, depth + 1);
  }
  default:
    return false;
  }
  return finite_vec(derivative);
}

long double spline_acceleration_bound(const detail::CurveRecord &curve) {
  const auto count = curve.poles.size();
  int degree = 0;
  if (count < 2 || !bspline_knots_valid_for_curve(curve, degree) ||
      degree < 1) {
    return 0.0L;
  }
  if (degree == 1 && curve.kind != detail::CurveKind::Nurbs) {
    return 0.0L;
  }

  using HPoint = std::array<long double, 4>;
  std::vector<HPoint> homogeneous;
  homogeneous.reserve(count);
  const auto reference = curve.poles.front();
  long double minimum_weight = std::numeric_limits<long double>::infinity();
  long double maximum_weighted_point = 0.0L;
  for (std::size_t i = 0; i < count; ++i) {
    const long double weight = curve.kind == detail::CurveKind::Nurbs
                                   ? curve.weights[i]
                                   : 1.0L;
    const HPoint value{
        (static_cast<long double>(curve.poles[i].x) - reference.x) * weight,
        (static_cast<long double>(curve.poles[i].y) - reference.y) * weight,
        (static_cast<long double>(curve.poles[i].z) - reference.z) * weight,
        weight};
    homogeneous.push_back(value);
    minimum_weight = std::min(minimum_weight, weight);
    maximum_weighted_point = std::max(
        maximum_weighted_point,
        std::hypot(value[0], value[1], value[2]));
  }
  if (!(minimum_weight > 0.0L)) {
    return std::numeric_limits<long double>::infinity();
  }

  std::vector<HPoint> first(count - 1, HPoint{});
  long double first_xyz = 0.0L;
  long double first_weight = 0.0L;
  for (std::size_t i = 0; i + 1 < count; ++i) {
    const long double denominator =
        static_cast<long double>(curve.knots_u[i + static_cast<std::size_t>(degree) + 1]) -
        curve.knots_u[i + 1];
    if (!(denominator > 0.0L)) {
      continue;
    }
    const long double factor = static_cast<long double>(degree) / denominator;
    for (std::size_t component = 0; component < 4; ++component) {
      first[i][component] =
          factor * (homogeneous[i + 1][component] - homogeneous[i][component]);
    }
    first_xyz = std::max(
        first_xyz,
        std::hypot(first[i][0], first[i][1], first[i][2]));
    first_weight = std::max(first_weight, std::abs(first[i][3]));
  }

  long double second_xyz = 0.0L;
  long double second_weight = 0.0L;
  for (std::size_t i = 0; i + 1 < first.size(); ++i) {
    const long double denominator =
        static_cast<long double>(curve.knots_u[i + static_cast<std::size_t>(degree) + 1]) -
        curve.knots_u[i + 2];
    if (!(denominator > 0.0L)) {
      continue;
    }
    const long double factor = static_cast<long double>(degree - 1) / denominator;
    HPoint second{};
    for (std::size_t component = 0; component < 4; ++component) {
      second[component] = factor * (first[i + 1][component] - first[i][component]);
    }
    second_xyz =
        std::max(second_xyz, std::hypot(second[0], second[1], second[2]));
    second_weight = std::max(second_weight, std::abs(second[3]));
  }

  if (curve.kind != detail::CurveKind::Nurbs) {
    return second_xyz;
  }
  const long double inverse_weight = 1.0L / minimum_weight;
  return second_xyz * inverse_weight +
         maximum_weighted_point * second_weight * inverse_weight * inverse_weight +
         2.0L * first_xyz * first_weight * inverse_weight * inverse_weight +
         2.0L * maximum_weighted_point * first_weight * first_weight *
             inverse_weight * inverse_weight * inverse_weight;
}

long double closest_search_acceleration_bound(
    const detail::KernelState &state, CurveId curve_id, Scalar t0, Scalar t1,
    std::uint32_t depth = 0) {
  if (depth > 64) {
    return std::numeric_limits<long double>::infinity();
  }
  const auto found = state.curves.find(curve_id.value);
  if (found == state.curves.end()) {
    return std::numeric_limits<long double>::infinity();
  }
  const auto &curve = found->second;
  switch (curve.kind) {
  case detail::CurveKind::Line:
  case detail::CurveKind::LineSegment:
  case detail::CurveKind::CompositePolyline:
    return 0.0L;
  case detail::CurveKind::Circle:
    return std::abs(static_cast<long double>(curve.radius)) *
           (vector_length(curve.axis_u) + vector_length(curve.axis_v));
  case detail::CurveKind::Ellipse:
    return vector_length(curve.axis_u) + vector_length(curve.axis_v);
  case detail::CurveKind::Parabola:
    return vector_length(curve.axis_v) /
           (2.0L * std::max<long double>(curve.param_a, 1e-9L));
  case detail::CurveKind::Hyperbola: {
    const long double max_t = std::max(std::abs(static_cast<long double>(t0)),
                                       std::abs(static_cast<long double>(t1)));
    return std::abs(static_cast<long double>(curve.param_a)) * std::cosh(max_t) *
               vector_length(curve.axis_u) +
           std::abs(static_cast<long double>(curve.param_b)) * std::sinh(max_t) *
               vector_length(curve.axis_v);
  }
  case detail::CurveKind::Bezier: {
    if (curve.poles.size() < 3) {
      return 0.0L;
    }
    const long double factor = static_cast<long double>(curve.poles.size() - 1) *
                               static_cast<long double>(curve.poles.size() - 2);
    long double bound = 0.0L;
    for (std::size_t i = 0; i + 2 < curve.poles.size(); ++i) {
      const long double x = static_cast<long double>(curve.poles[i + 2].x) -
                            2.0L * curve.poles[i + 1].x + curve.poles[i].x;
      const long double y = static_cast<long double>(curve.poles[i + 2].y) -
                            2.0L * curve.poles[i + 1].y + curve.poles[i].y;
      const long double z = static_cast<long double>(curve.poles[i + 2].z) -
                            2.0L * curve.poles[i + 1].z + curve.poles[i].z;
      bound = std::max(bound, factor * std::hypot(x, y, z));
    }
    return bound;
  }
  case detail::CurveKind::BSpline:
  case detail::CurveKind::Nurbs:
    if (curve.kind == detail::CurveKind::Nurbs &&
        curve.weights.size() != curve.poles.size()) {
      return std::numeric_limits<long double>::infinity();
    }
    return spline_acceleration_bound(curve);
  case detail::CurveKind::CompositeChain: {
    if (curve.children.empty()) {
      return 0.0L;
    }
    const auto midpoint = (t0 + t1) * 0.5;
    const auto child_index = std::min<std::size_t>(
        static_cast<std::size_t>(std::max<Scalar>(0.0, std::floor(midpoint))),
        curve.children.size() - 1);
    const auto offset = static_cast<Scalar>(child_index);
    return closest_search_acceleration_bound(
        state, curve.children[child_index],
        std::clamp(t0 - offset, 0.0, 1.0),
        std::clamp(t1 - offset, 0.0, 1.0), depth + 1);
  }
  default:
    return std::numeric_limits<long double>::infinity();
  }
}

std::vector<Range1D> closest_search_spans(const detail::CurveRecord &curve) {
  const auto domain = curve_domain(curve);
  std::vector<Range1D> spans;
  if ((curve.kind == detail::CurveKind::BSpline ||
       curve.kind == detail::CurveKind::Nurbs) &&
      curve.knots_u.size() >= 2) {
    for (std::size_t i = 0; i + 1 < curve.knots_u.size(); ++i) {
      const auto begin = std::max(domain.min, curve.knots_u[i]);
      const auto end = std::min(domain.max, curve.knots_u[i + 1]);
      if (end > begin) {
        spans.push_back({begin, end});
      }
    }
  } else if (curve.kind == detail::CurveKind::CompositeChain) {
    for (std::size_t i = 0; i < curve.children.size(); ++i) {
      spans.push_back({static_cast<Scalar>(i), static_cast<Scalar>(i + 1)});
    }
  } else {
    const std::uint32_t pieces =
        curve.kind == detail::CurveKind::Ellipse ? 8u
        : (curve.kind == detail::CurveKind::Parabola ||
           curve.kind == detail::CurveKind::Hyperbola)
              ? 16u
              : 1u;
    for (std::uint32_t i = 0; i < pieces; ++i) {
      const auto a = static_cast<Scalar>(i) / static_cast<Scalar>(pieces);
      const auto b = static_cast<Scalar>(i + 1) / static_cast<Scalar>(pieces);
      spans.push_back({domain.min + (domain.max - domain.min) * a,
                       domain.min + (domain.max - domain.min) * b});
    }
  }
  return spans;
}

} // namespace

Result<CurveClosestPointResult> CurveService::closest_point_detailed(
    CurveId curve_id, const Point3 &point,
    const CurveClosestPointOptions &options) const {
  if (!finite_point(point) || !finite_scalar(options.distance_tolerance) ||
      !finite_scalar(options.parameter_tolerance) ||
      options.distance_tolerance < 0.0 || options.parameter_tolerance <= 0.0 ||
      options.max_evaluations < 3) {
    return detail::invalid_input_result<CurveClosestPointResult>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲线全域最近点求解失败：输入点或精度/预算选项非法",
        "曲线全域最近点求解失败");
  }
  const auto found = state_->curves.find(curve_id.value);
  if (found == state_->curves.end()) {
    return detail::failed_result<CurveClosestPointResult>(
        *state_, StatusCode::InvalidInput, diag_codes::kGeoParameterSolveFailure,
        "曲线全域最近点求解失败：目标曲线不存在",
        "曲线全域最近点求解失败");
  }
  const auto &curve = found->second;

  auto analytic_result = [&](Scalar parameter, const Point3 &nearest) {
    const auto distance = closest_distance(nearest, point);
    if (!std::isfinite(distance) ||
        distance > std::numeric_limits<Scalar>::max()) {
      return detail::failed_result<CurveClosestPointResult>(
          *state_, StatusCode::NumericalInstability,
          diag_codes::kGeoParameterSolveFailure,
          "曲线全域最近点求解失败：距离超出可表示范围",
          "曲线全域最近点求解失败");
    }
    CurveClosestPointResult result;
    result.parameter = parameter;
    result.point = nearest;
    result.distance = static_cast<Scalar>(distance);
    result.distance_lower_bound = result.distance;
    result.convergence = CurveClosestPointConvergence::Analytic;
    return ok_result(result, state_->create_diagnostic(
                                 "曲线全域最近点已由解析路径求解"));
  };

  if (curve.kind == detail::CurveKind::Line) {
    const long double dx = curve.direction.x;
    const long double dy = curve.direction.y;
    const long double dz = curve.direction.z;
    const long double denominator = dx * dx + dy * dy + dz * dz;
    if (!(denominator > 0.0L)) {
      return detail::failed_result<CurveClosestPointResult>(
          *state_, StatusCode::DegenerateGeometry,
          diag_codes::kGeoParameterSolveFailure,
          "曲线全域最近点求解失败：直线方向退化",
          "曲线全域最近点求解失败");
    }
    const long double parameter =
        ((static_cast<long double>(point.x) - curve.origin.x) * dx +
         (static_cast<long double>(point.y) - curve.origin.y) * dy +
         (static_cast<long double>(point.z) - curve.origin.z) * dz) /
        denominator;
    if (!std::isfinite(parameter) ||
        std::abs(parameter) > std::numeric_limits<Scalar>::max()) {
      return detail::failed_result<CurveClosestPointResult>(
          *state_, StatusCode::NumericalInstability,
          diag_codes::kGeoParameterSolveFailure,
          "曲线全域最近点求解失败：解析参数超出可表示范围",
          "曲线全域最近点求解失败");
    }
    Point3 nearest{};
    if (!closest_search_point(*state_, curve_id, static_cast<Scalar>(parameter),
                              nearest)) {
      return detail::failed_result<CurveClosestPointResult>(
          *state_, StatusCode::NumericalInstability,
          diag_codes::kGeoParameterSolveFailure,
          "曲线全域最近点求解失败：解析点不可表示",
          "曲线全域最近点求解失败");
    }
    return analytic_result(static_cast<Scalar>(parameter), nearest);
  }

  if (curve.kind == detail::CurveKind::LineSegment ||
      curve.kind == detail::CurveKind::CompositePolyline) {
    if (curve.poles.size() < 2) {
      return detail::failed_result<CurveClosestPointResult>(
          *state_, StatusCode::DegenerateGeometry,
          diag_codes::kGeoParameterSolveFailure,
          "曲线全域最近点求解失败：折线数据缺失",
          "曲线全域最近点求解失败");
    }
    long double best_distance = std::numeric_limits<long double>::infinity();
    Scalar best_parameter = 0.0;
    Point3 best_point{};
    const std::size_t segment_count = curve.kind == detail::CurveKind::LineSegment
                                          ? 1
                                          : curve.poles.size() - 1;
    for (std::size_t i = 0; i < segment_count; ++i) {
      const auto &a = curve.poles[curve.kind == detail::CurveKind::LineSegment ? 0 : i];
      const auto &b = curve.poles[curve.kind == detail::CurveKind::LineSegment
                                      ? curve.poles.size() - 1
                                      : i + 1];
      const long double dx = static_cast<long double>(b.x) - a.x;
      const long double dy = static_cast<long double>(b.y) - a.y;
      const long double dz = static_cast<long double>(b.z) - a.z;
      const long double qx = static_cast<long double>(point.x) - a.x;
      const long double qy = static_cast<long double>(point.y) - a.y;
      const long double qz = static_cast<long double>(point.z) - a.z;
      const long double denominator = dx * dx + dy * dy + dz * dz;
      const long double local = denominator > 0.0L
                                    ? std::clamp((qx * dx + qy * dy + qz * dz) /
                                                     denominator,
                                                 0.0L, 1.0L)
                                    : 0.0L;
      const Point3 candidate{
          static_cast<Scalar>(static_cast<long double>(a.x) + local * dx),
          static_cast<Scalar>(static_cast<long double>(a.y) + local * dy),
          static_cast<Scalar>(static_cast<long double>(a.z) + local * dz)};
      const auto distance = closest_distance(candidate, point);
      const auto parameter = curve.kind == detail::CurveKind::LineSegment
                                 ? static_cast<Scalar>(local)
                                 : static_cast<Scalar>(i) + static_cast<Scalar>(local);
      if (distance < best_distance ||
          (distance == best_distance && parameter < best_parameter)) {
        best_distance = distance;
        best_parameter = parameter;
        best_point = candidate;
      }
    }
    return analytic_result(best_parameter, best_point);
  }

  if (curve.kind == detail::CurveKind::Circle) {
    const auto relative = detail::subtract(point, curve.origin);
    const auto parameter = normalize_angle_0_2pi(
        std::atan2(detail::dot(relative, curve.axis_v),
                   detail::dot(relative, curve.axis_u)));
    Point3 nearest{};
    if (!closest_search_point(*state_, curve_id, parameter, nearest)) {
      return detail::failed_result<CurveClosestPointResult>(
          *state_, StatusCode::NumericalInstability,
          diag_codes::kGeoParameterSolveFailure,
          "曲线全域最近点求解失败：圆解析点不可表示",
          "曲线全域最近点求解失败");
    }
    return analytic_result(parameter, nearest);
  }

  struct SearchInterval {
    Scalar begin{};
    Scalar end{};
    long double lower_bound{};
    long double speed_bound{};
    long double acceleration_bound{};
  };
  struct LowerFirst {
    bool operator()(const SearchInterval &lhs,
                    const SearchInterval &rhs) const {
      if (lhs.lower_bound != rhs.lower_bound) {
        return lhs.lower_bound > rhs.lower_bound;
      }
      return lhs.begin > rhs.begin;
    }
  };
  std::priority_queue<SearchInterval, std::vector<SearchInterval>, LowerFirst>
      pending;
  long double best_distance = std::numeric_limits<long double>::infinity();
  Scalar best_parameter = 0.0;
  Point3 best_point{};
  std::uint32_t evaluations = 0;
  std::uint32_t intervals_processed = 0;

  const auto evaluate_candidate = [&](Scalar parameter, Point3 &candidate,
                                      long double &distance) {
    if (evaluations >= options.max_evaluations ||
        !closest_search_point(*state_, curve_id, parameter, candidate)) {
      return false;
    }
    ++evaluations;
    distance = closest_distance(candidate, point);
    if (!std::isfinite(distance)) {
      return false;
    }
    if (distance < best_distance ||
        (distance == best_distance && parameter < best_parameter)) {
      best_distance = distance;
      best_parameter = parameter;
      best_point = candidate;
    }
    return true;
  };
  const auto enqueue = [&](Scalar begin, Scalar end, long double speed_bound,
                           long double acceleration_bound) {
    Point3 midpoint_point{};
    long double midpoint_distance = 0.0L;
    const Scalar midpoint = begin + (end - begin) * 0.5;
    if (!evaluate_candidate(midpoint, midpoint_point, midpoint_distance)) {
      return false;
    }
    const long double radius =
        static_cast<long double>(end - begin) * 0.5L;
    long double lower_bound =
        std::max(0.0L, midpoint_distance - speed_bound * radius);
    Vec3 midpoint_derivative{};
    if (std::isfinite(acceleration_bound) &&
        closest_search_derivative(*state_, curve_id, midpoint,
                                  midpoint_derivative)) {
      const long double dx = static_cast<long double>(midpoint_point.x) - point.x;
      const long double dy = static_cast<long double>(midpoint_point.y) - point.y;
      const long double dz = static_cast<long double>(midpoint_point.z) - point.z;
      const long double slope =
          2.0L * (dx * midpoint_derivative.x + dy * midpoint_derivative.y +
                  dz * midpoint_derivative.z);
      const long double maximum_distance =
          midpoint_distance + speed_bound * radius;
      const long double second_derivative_bound =
          2.0L * (speed_bound * speed_bound +
                  maximum_distance * acceleration_bound);
      const long double squared_lower =
          midpoint_distance * midpoint_distance - std::abs(slope) * radius -
          0.5L * second_derivative_bound * radius * radius;
      lower_bound = std::max(lower_bound, std::sqrt(std::max(0.0L, squared_lower)));
    }
    pending.push(SearchInterval{begin, end, lower_bound, speed_bound,
                                acceleration_bound});
    return true;
  };

  const auto spans = closest_search_spans(curve);
  if (spans.empty()) {
    return detail::failed_result<CurveClosestPointResult>(
        *state_, StatusCode::DegenerateGeometry,
        diag_codes::kGeoParameterSolveFailure,
        "曲线全域最近点求解失败：有效参数域为空",
        "曲线全域最近点求解失败");
  }
  for (const auto &span : spans) {
    const auto speed = closest_search_speed_bound(*state_, curve_id, span.min,
                                                  span.max);
    const auto acceleration = closest_search_acceleration_bound(
        *state_, curve_id, span.min, span.max);
    if (!std::isfinite(speed) || speed < 0.0L ||
        !std::isfinite(acceleration) || acceleration < 0.0L) {
      return detail::failed_result<CurveClosestPointResult>(
          *state_, StatusCode::NumericalInstability,
          diag_codes::kGeoParameterSolveFailure,
          "曲线全域最近点求解失败：无法建立有限速度界",
          "曲线全域最近点求解失败");
    }
    if (!enqueue(span.min, span.max, speed, acceleration)) {
      return detail::failed_result<CurveClosestPointResult>(
          *state_, StatusCode::OperationFailed,
          diag_codes::kGeoParameterSolveFailure,
          "曲线全域最近点求解失败：初始覆盖超过求值预算或点值不可用",
          "曲线全域最近点求解失败");
    }
  }

  const long double distance_tolerance = options.distance_tolerance;
  const long double parameter_tolerance = options.parameter_tolerance;
  long double terminal_lower_bound = std::numeric_limits<long double>::infinity();
  long double maximum_terminal_width = 0.0L;
  auto convergence = CurveClosestPointConvergence::ParameterTolerance;
  while (!pending.empty()) {
    const long double global_lower =
        std::min(terminal_lower_bound, pending.top().lower_bound);
    if (best_distance - global_lower <= distance_tolerance) {
      convergence = CurveClosestPointConvergence::DistanceTolerance;
      break;
    }
    const auto current = pending.top();
    pending.pop();
    ++intervals_processed;
    const long double width =
        static_cast<long double>(current.end) - current.begin;
    if (width <= parameter_tolerance ||
        current.begin == std::nextafter(current.end, current.begin)) {
      terminal_lower_bound =
          std::min(terminal_lower_bound, current.lower_bound);
      maximum_terminal_width = std::max(maximum_terminal_width, width);
      continue;
    }
    if (evaluations + 2 > options.max_evaluations) {
      return detail::failed_result<CurveClosestPointResult>(
          *state_, StatusCode::OperationFailed,
          diag_codes::kGeoParameterSolveFailure,
          "曲线全域最近点求解失败：全域精度证明耗尽求值预算",
          "曲线全域最近点求解失败");
    }
    const Scalar midpoint = current.begin + (current.end - current.begin) * 0.5;
    if (!enqueue(current.begin, midpoint, current.speed_bound,
                 current.acceleration_bound) ||
        !enqueue(midpoint, current.end, current.speed_bound,
                 current.acceleration_bound)) {
      return detail::failed_result<CurveClosestPointResult>(
          *state_, StatusCode::NumericalInstability,
          diag_codes::kGeoParameterSolveFailure,
          "曲线全域最近点求解失败：细分点值不可用",
          "曲线全域最近点求解失败");
    }
  }

  // The distance certificate may converge while the parameter is still less
  // accurate than legacy closest_parameter callers expect (distance grows
  // quadratically around a smooth minimum).  Refine only the already selected
  // global candidate, keep descent-only acceptance, and charge every added
  // point evaluation to the caller's budget.
  const auto search_domain = curve_domain(curve);
  for (std::uint32_t iteration = 0;
       iteration < 32 && evaluations < options.max_evaluations; ++iteration) {
    Vec3 derivative{};
    if (!closest_search_derivative(*state_, curve_id, best_parameter,
                                   derivative)) {
      break;
    }
    const long double vx = derivative.x;
    const long double vy = derivative.y;
    const long double vz = derivative.z;
    const long double denominator = vx * vx + vy * vy + vz * vz;
    if (!(denominator > 0.0L) || !std::isfinite(denominator)) {
      break;
    }
    const long double dx = static_cast<long double>(best_point.x) - point.x;
    const long double dy = static_cast<long double>(best_point.y) - point.y;
    const long double dz = static_cast<long double>(best_point.z) - point.z;
    long double step = -(dx * vx + dy * vy + dz * vz) / denominator;
    if (!std::isfinite(step)) {
      break;
    }
    bool accepted = false;
    for (std::uint32_t attempt = 0;
         attempt < 8 && evaluations < options.max_evaluations; ++attempt) {
      const auto candidate_parameter = std::clamp(
          static_cast<Scalar>(static_cast<long double>(best_parameter) + step),
          search_domain.min, search_domain.max);
      if (candidate_parameter == best_parameter) {
        break;
      }
      Point3 candidate_point{};
      if (!closest_search_point(*state_, curve_id, candidate_parameter,
                                candidate_point)) {
        break;
      }
      ++evaluations;
      const auto candidate_distance = closest_distance(candidate_point, point);
      if (candidate_distance < best_distance) {
        best_parameter = candidate_parameter;
        best_point = candidate_point;
        best_distance = candidate_distance;
        accepted = true;
        break;
      }
      step *= 0.5L;
    }
    if (!accepted ||
        std::abs(step) <= std::numeric_limits<Scalar>::epsilon() *
                              std::max<long double>(1.0L, std::abs(best_parameter))) {
      break;
    }
  }

  if (!std::isfinite(best_distance) ||
      best_distance > std::numeric_limits<Scalar>::max()) {
    return detail::failed_result<CurveClosestPointResult>(
        *state_, StatusCode::NumericalInstability,
        diag_codes::kGeoParameterSolveFailure,
        "曲线全域最近点求解失败：最小距离超出可表示范围",
        "曲线全域最近点求解失败");
  }
  long double global_lower = best_distance;
  if (!pending.empty()) {
    global_lower = std::min(global_lower, pending.top().lower_bound);
  }
  global_lower = std::min(global_lower, terminal_lower_bound);
  if (!std::isfinite(global_lower)) {
    global_lower = best_distance;
  }
  CurveClosestPointResult result;
  result.parameter = curve.kind == detail::CurveKind::Ellipse
                         ? normalize_angle_0_2pi(best_parameter)
                         : best_parameter;
  result.point = best_point;
  result.distance = static_cast<Scalar>(best_distance);
  result.distance_lower_bound = static_cast<Scalar>(
      std::clamp(global_lower, 0.0L, best_distance));
  result.parameter_uncertainty =
      convergence == CurveClosestPointConvergence::ParameterTolerance
          ? static_cast<Scalar>(maximum_terminal_width)
          : 0.0;
  result.evaluations = evaluations;
  result.intervals_processed = intervals_processed;
  result.convergence = convergence;
  return ok_result(result,
                   state_->create_diagnostic("曲线全域最近点已按请求精度收敛"));
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
    // Keep the projection finite when a valid segment's squared length exceeds
    // the range of Scalar. The factory has already rejected short segments.
    const long double dx = static_cast<long double>(b.x) - a.x;
    const long double dy = static_cast<long double>(b.y) - a.y;
    const long double dz = static_cast<long double>(b.z) - a.z;
    const auto denom = dx * dx + dy * dy + dz * dz;
    if (!(denom > 0.0L)) {
      return detail::failed_result<Scalar>(
          *state_, StatusCode::OperationFailed, diag_codes::kGeoParameterSolveFailure,
          "曲线最近参数求解失败：线段退化", "曲线最近参数求解失败");
    }
    const long double qx = static_cast<long double>(point.x) - a.x;
    const long double qy = static_cast<long double>(point.y) - a.y;
    const long double qz = static_cast<long double>(point.z) - a.z;
    const auto tproj = (qx * dx + qy * dy + qz * dz) / denom;
    return ok_result<Scalar>(static_cast<Scalar>(std::clamp(tproj, 0.0L, 1.0L)),
                             state_->create_diagnostic("曲线最近参数已求解"));
  }
  case detail::CurveKind::Circle: {
    const auto rel = detail::subtract(point, curve.origin);
    const auto pu = detail::dot(rel, curve.axis_u);
    const auto pv = detail::dot(rel, curve.axis_v);
    return ok_result<Scalar>(normalize_angle_0_2pi(std::atan2(pv, pu)),
                             state_->create_diagnostic("曲线最近参数已求解"));
  }
  case detail::CurveKind::CompositePolyline: {
    Scalar best_t = 0.0;
    long double best_distance = std::numeric_limits<long double>::max();
    for (std::size_t i = 0; i + 1 < curve.poles.size(); ++i) {
      const auto &a = curve.poles[i];
      const auto &b = curve.poles[i + 1];
      // Finite double coordinates can overflow a double squared distance.
      const long double dx = static_cast<long double>(b.x) - a.x;
      const long double dy = static_cast<long double>(b.y) - a.y;
      const long double dz = static_cast<long double>(b.z) - a.z;
      const long double qx = static_cast<long double>(point.x) - a.x;
      const long double qy = static_cast<long double>(point.y) - a.y;
      const long double qz = static_cast<long double>(point.z) - a.z;
      const auto length_squared = dx * dx + dy * dy + dz * dz;
      const auto local = length_squared > 0.0L
                             ? std::clamp((qx * dx + qy * dy + qz * dz) /
                                              length_squared,
                                          0.0L, 1.0L)
                             : 0.0L;
      const auto rx = qx - local * dx;
      const auto ry = qy - local * dy;
      const auto rz = qz - local * dz;
      const auto distance = rx * rx + ry * ry + rz * rz;
      if (distance < best_distance) {
        best_distance = distance;
        best_t = static_cast<Scalar>(i) + static_cast<Scalar>(local);
      }
    }
    return ok_result<Scalar>(best_t,
                             state_->create_diagnostic("曲线最近参数已求解"));
  }
  default:
    break;
  }

  if (curve.kind == detail::CurveKind::Bezier ||
      curve.kind == detail::CurveKind::BSpline ||
      curve.kind == detail::CurveKind::Nurbs ||
      curve.kind == detail::CurveKind::Ellipse ||
      curve.kind == detail::CurveKind::Parabola ||
      curve.kind == detail::CurveKind::Hyperbola) {
    const auto detailed = closest_point_detailed(curve_id, point);
    if (detailed.status != StatusCode::Ok || !detailed.value.has_value()) {
      return error_result<Scalar>(detailed.status, detailed.diagnostic_id,
                                  detailed.warnings);
    }
    return ok_result(detailed.value->parameter, detailed.diagnostic_id);
  }

  if (curve.kind == detail::CurveKind::CompositeChain) {
    const auto detailed = closest_point_detailed(curve_id, point);
    if (detailed.status != StatusCode::Ok || !detailed.value.has_value()) {
      return error_result<Scalar>(detailed.status, detailed.diagnostic_id,
                                  detailed.warnings);
    }
    return ok_result(detailed.value->parameter, detailed.diagnostic_id);
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

Result<Scalar> CurveService::length(CurveId curve_id) const {
  return length(curve_id, CurveLengthOptions{});
}

Result<Scalar> CurveService::length(CurveId curve_id, Scalar t0, Scalar t1) const {
  return length(curve_id, t0, t1, CurveLengthOptions{});
}

Result<Scalar> CurveService::length(CurveId curve_id, const CurveLengthOptions &options) const {
  const auto range = domain(curve_id);
  if (!range.value) return error_result<Scalar>(range.status, range.diagnostic_id);
  return length(curve_id, range.value->min, range.value->max, options);
}

Result<Scalar> CurveService::length(CurveId curve_id, Scalar t0, Scalar t1,
                                    const CurveLengthOptions &options) const {
  if (!detail::has_curve(*state_, curve_id)) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreInvalidHandle,
        "曲线长度查询失败：目标曲线不存在", "曲线长度查询失败");
  }
  if (!std::isfinite(t0) || !std::isfinite(t1)) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲线长度查询失败：需要有限参数区间", "曲线长度查询失败");
  }
  if (!std::isfinite(options.absolute_tolerance) || options.absolute_tolerance < 0 ||
      !std::isfinite(options.relative_tolerance) || options.relative_tolerance < 0 ||
      (options.absolute_tolerance == 0 && options.relative_tolerance == 0) || options.max_evaluations == 0) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲线长度查询失败：容差或求值预算非法", "曲线长度查询失败");
  }
  const auto integration_failure = [&]() {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::OperationFailed, diag_codes::kGeoLengthIntegrationFailure,
        "曲线长度查询失败：积分预算耗尽、精度停滞或速度不可用", "曲线长度查询失败");
  };
  std::uint32_t evaluations = 0;
  // Iterate the same local [0,1] child intervals as eval; never approximate
  // curved geometry by chords, and do not add jumps between chain children.
  struct Interval { CurveId id; Scalar a; Scalar b; long double tolerance; };
  std::vector<Interval> pending{{curve_id, std::min(t0, t1), std::max(t0, t1), options.absolute_tolerance}};
  long double total = 0.0L;
  while (!pending.empty()) {
    const auto interval = pending.back();
    pending.pop_back();
    const auto it = state_->curves.find(interval.id.value);
    if (it == state_->curves.end()) {
      return detail::invalid_input_result<Scalar>(
          *state_, diag_codes::kCoreInvalidHandle,
          "曲线长度查询失败：子曲线不存在", "曲线长度查询失败");
    }
    const auto &curve = it->second;
    const auto range = curve_domain(curve);
    if (interval.a < range.min || interval.b > range.max) {
      return detail::invalid_input_result<Scalar>(
          *state_, diag_codes::kGeoParameterOutOfDomain,
          "曲线长度查询失败：参数超出定义域", "曲线长度查询失败");
    }
    const long double width = static_cast<long double>(interval.b) - interval.a;
    switch (curve.kind) {
    case detail::CurveKind::Line: {
      const long double speed = std::hypot(static_cast<long double>(curve.direction.x),
                                           curve.direction.y, curve.direction.z);
      if (!(speed > 0.0L) || !std::isfinite(speed)) {
        return detail::failed_result<Scalar>(
            *state_, StatusCode::DegenerateGeometry, diag_codes::kGeoDegenerateGeometry,
            "曲线长度查询失败：直线方向退化", "曲线长度查询失败");
      }
      total += width * speed;
      break;
    }
    case detail::CurveKind::Circle:
      total += width * curve.radius;
      break;
    case detail::CurveKind::LineSegment:
    case detail::CurveKind::CompositePolyline:
      for (std::size_t i = 0; i + 1 < curve.poles.size(); ++i) {
        const long double overlap = std::max(0.0L,
            std::min(static_cast<long double>(interval.b), static_cast<long double>(i + 1)) -
            std::max(static_cast<long double>(interval.a), static_cast<long double>(i)));
        if (overlap == 0.0L) continue;
        const auto &a = curve.poles[i];
        const auto &b = curve.poles[i + 1];
        total += overlap * std::hypot(static_cast<long double>(b.x) - a.x,
                                      static_cast<long double>(b.y) - a.y,
                                      static_cast<long double>(b.z) - a.z);
      }
      break;
    case detail::CurveKind::CompositeChain:
      for (std::size_t i = 0; i < curve.children.size(); ++i) {
        const auto a = std::max(interval.a, static_cast<Scalar>(i));
        const auto b = std::min(interval.b, static_cast<Scalar>(i + 1));
        if (a < b) pending.push_back({curve.children[i], a - static_cast<Scalar>(i), b - static_cast<Scalar>(i),
                                    interval.tolerance * (static_cast<long double>(b) - a) / width});
      }
      break;
    case detail::CurveKind::Ellipse:
    case detail::CurveKind::Parabola:
    case detail::CurveKind::Hyperbola:
    case detail::CurveKind::Bezier:
    case detail::CurveKind::BSpline:
    case detail::CurveKind::Nurbs: {
      const bool bezier = curve.kind == detail::CurveKind::Bezier;
      const bool spline = bezier || curve.kind == detail::CurveKind::BSpline || curve.kind == detail::CurveKind::Nurbs;
      int degree = 0;
      // Work in translated homogeneous coordinates and extended precision. This
      // avoids both absolute knot-denominator cutoffs and rational weight clamps
      // in the general point evaluator; no point-evaluation cache is touched.
      using HPoint = std::array<long double, 4>;
      std::vector<HPoint> poles;
      if (spline) {
        if (curve.poles.empty()) return integration_failure();
        if (bezier) degree = static_cast<int>(curve.poles.size()) - 1;
        else if (!bspline_knots_valid_for_curve(curve, degree)) return integration_failure();
        const auto origin = curve.poles.front();
        for (std::size_t i = 0; i < curve.poles.size(); ++i) {
          const auto &p = curve.poles[i];
          const long double w = curve.kind == detail::CurveKind::Nurbs && i < curve.weights.size()
                                    ? curve.weights[i] : 1.0L;
          if (!finite_point(p) || !(w > 0) || !std::isfinite(w)) return integration_failure();
          poles.push_back({(static_cast<long double>(p.x) - origin.x) * w,
                           (static_cast<long double>(p.y) - origin.y) * w,
                           (static_cast<long double>(p.z) - origin.z) * w, w});
        }
      }
      if (width == 0) break;
      std::vector<long double> cuts{interval.a};
      if (spline && !bezier) {
        for (const auto knot : curve.knots_u) {
          if (knot > cuts.back() && knot < interval.b) cuts.push_back(knot);
        }
      }
      cuts.push_back(interval.b);
      for (std::size_t cut = 0; cut + 1 < cuts.size(); ++cut) {
        const auto left = cuts[cut];
        const auto right = cuts[cut + 1];
        int span = degree;
        if (spline && !bezier) {
          // Select this span once, including its one-sided endpoint limits.
          span = static_cast<int>(std::upper_bound(curve.knots_u.begin(), curve.knots_u.end(),
                                                  (left + right) / 2) - curve.knots_u.begin()) - 1;
        }
        const auto speed = [&](long double t) -> long double {
          if (evaluations == options.max_evaluations) return std::numeric_limits<long double>::quiet_NaN();
          ++evaluations;
          if (spline) {
            std::vector<HPoint> points(static_cast<std::size_t>(degree + 1));
            std::vector<HPoint> derivatives(points.size(), HPoint{});
            for (int j = 0; j <= degree; ++j) points[static_cast<std::size_t>(j)] = poles[static_cast<std::size_t>(span - degree + j)];
            // Differentiate the de Casteljau / de Boor recurrence itself.
            for (int r = 1; r <= degree; ++r) {
              for (int j = degree; j >= r; --j) {
                long double alpha = t;
                long double dalpha = 1;
                if (!bezier) {
                  const auto i = static_cast<std::size_t>(span - degree + j);
                  const long double lo = curve.knots_u[i];
                  const long double hi = curve.knots_u[i + static_cast<std::size_t>(degree - r + 1)];
                  if (!(hi > lo)) return std::numeric_limits<long double>::quiet_NaN();
                  alpha = (t - lo) / (hi - lo);
                  dalpha = 1 / (hi - lo);
                }
                const auto index = static_cast<std::size_t>(j);
                for (std::size_t k = 0; k < 4; ++k) {
                  derivatives[index][k] = (1 - alpha) * derivatives[index - 1][k] + alpha * derivatives[index][k] +
                                          dalpha * (points[index][k] - points[index - 1][k]);
                  points[index][k] = (1 - alpha) * points[index - 1][k] + alpha * points[index][k];
                }
              }
            }
            const auto &p = points.back();
            const auto &d = derivatives.back();
            if (!(p[3] > 0)) return std::numeric_limits<long double>::quiet_NaN();
            return std::hypot((d[0] - (p[0] / p[3]) * d[3]) / p[3],
                              (d[1] - (p[1] / p[3]) * d[3]) / p[3],
                              (d[2] - (p[2] / p[3]) * d[3]) / p[3]);
          }
          long double du{}, dv{};
          if (curve.kind == detail::CurveKind::Ellipse) {
            du = -std::sin(t); dv = std::cos(t);
          } else if (curve.kind == detail::CurveKind::Parabola) {
            du = 1; dv = t / (2 * static_cast<long double>(std::max(curve.param_a, 1e-9)));
          } else {
            du = std::max(curve.param_a, 1e-9) * std::sinh(t);
            dv = std::max(curve.param_b, 1e-9) * std::cosh(t);
          }
          return std::hypot(du * curve.axis_u.x + dv * curve.axis_v.x,
                            du * curve.axis_u.y + dv * curve.axis_v.y,
                            du * curve.axis_u.z + dv * curve.axis_v.z);
        };
        struct Panel {
          long double a, b, fa, fm, fb, estimate, tolerance;
          unsigned depth;
        };
        // Seed each smooth knot span to reduce aliasing on higher-degree curves.
        const auto seeds = std::max(4, degree + 1);
        for (int seed = 0; seed < seeds; ++seed) {
          const auto a = left + (right - left) * seed / seeds;
          const auto b = left + (right - left) * (seed + 1) / seeds;
          const auto m = (a + b) / 2;
          const auto fa = speed(a), fm = speed(m), fb = speed(b);
          std::vector<Panel> panels{{a, b, fa, fm, fb, (b - a) * (fa + 4 * fm + fb) / 6,
                                     interval.tolerance * (b - a) / width, 0}};
          while (!panels.empty()) {
            const auto panel = panels.back();
            panels.pop_back();
            const auto mid = (panel.a + panel.b) / 2;
            const auto lm = (panel.a + mid) / 2, rm = (mid + panel.b) / 2;
            if (!(lm > panel.a && rm < panel.b)) return integration_failure();
            const auto fl = speed(lm), fr = speed(rm);
            const auto sl = (mid - panel.a) * (panel.fa + 4 * fl + panel.fm) / 6;
            const auto sr = (panel.b - mid) * (panel.fm + 4 * fr + panel.fb) / 6;
            const auto sum = sl + sr;
            if (!std::isfinite(sum) || !std::isfinite(panel.estimate)) return integration_failure();
            const auto error = std::abs(sum - panel.estimate) / 15 +
                               8 * std::numeric_limits<Scalar>::epsilon() * std::abs(sum);
            if (error <= panel.tolerance + options.relative_tolerance * std::abs(sum)) {
              // Arc length is non-negative even when a deliberately loose
              // tolerance accepts a panel whose Richardson correction undershoots.
              total += std::max(0.0L, sum + (sum - panel.estimate) / 15);
            } else {
              if (panel.depth >= 64) return integration_failure();
              panels.push_back({mid, panel.b, panel.fm, fr, panel.fb, sr, panel.tolerance / 2, panel.depth + 1});
              panels.push_back({panel.a, mid, panel.fa, fl, panel.fm, sl, panel.tolerance / 2, panel.depth + 1});
            }
          }
        }
      }
      break;
    }
    default:
      return detail::failed_result<Scalar>(
          *state_, StatusCode::NotImplemented, diag_codes::kCoreOperationUnsupported,
          "曲线长度查询失败：该曲线类型尚无长度实现", "曲线长度查询失败");
    }
    if (!std::isfinite(total) || total > std::numeric_limits<Scalar>::max()) {
      return detail::invalid_input_result<Scalar>(
          *state_, diag_codes::kCoreParameterOutOfRange,
          "曲线长度查询失败：结果超出数值范围", "曲线长度查询失败");
    }
  }
  return ok_result(static_cast<Scalar>(total), state_->create_diagnostic("已查询曲线长度"));
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
