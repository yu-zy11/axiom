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
      const Vec3 vel = curve_velocity_for_closest(r);
      const auto jtj = detail::dot(vel, vel);
      const auto g = detail::dot(vel, err);
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
    const auto result_t = curve.kind == detail::CurveKind::Ellipse
                              ? normalize_angle_0_2pi(t)
                              : std::clamp(t, domain.min, domain.max);
    return ok_result<Scalar>(result_t,
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
      const Vec3 vel = curve_velocity_for_closest(r);
      const auto jtj = detail::dot(vel, vel);
      const auto g = detail::dot(vel, err);
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
