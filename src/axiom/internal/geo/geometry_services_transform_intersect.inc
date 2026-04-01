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
          h.surface_v <= surface.trim_v_max + 1e-12 &&
          trimmed_surface_uv_feasible(h.surface_u, h.surface_v, surface)) {
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
