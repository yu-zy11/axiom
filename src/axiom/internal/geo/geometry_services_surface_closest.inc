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
    const auto uv_res = closest_uv(surface_id, point);
    if (uv_res.status != StatusCode::Ok || !uv_res.value.has_value()) {
      return error_result<Point3>(uv_res.status, uv_res.diagnostic_id);
    }
    const auto eval_result =
        eval(surface_id, uv_res.value->first, uv_res.value->second, 0);
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
    // 多起点粗采样，减轻样条反求陷入局部极小（工业内核常用策略的极简版）。
    const int grid = 5;
    Scalar best_u = uv.first;
    Scalar best_v = uv.second;
    auto best_cost = std::numeric_limits<Scalar>::max();
    const Scalar span_u = domain.u.max - domain.u.min;
    const Scalar span_v = domain.v.max - domain.v.min;
    const int g1 = std::max(1, grid - 1);
    for (int iu = 0; iu < grid; ++iu) {
      for (int iv = 0; iv < grid; ++iv) {
        const Scalar su =
            domain.u.min + span_u * (static_cast<Scalar>(iu) / static_cast<Scalar>(g1));
        const Scalar sv =
            domain.v.min + span_v * (static_cast<Scalar>(iv) / static_cast<Scalar>(g1));
        const auto evs = eval(surface_id, su, sv, 0);
        if (evs.status == StatusCode::Ok && evs.value.has_value()) {
          const Scalar c = squared_distance(evs.value->point, point);
          if (c < best_cost) {
            best_cost = c;
            best_u = su;
            best_v = sv;
          }
        }
      }
    }
    uv.first = best_u;
    uv.second = best_v;
    // Damped Gauss-Newton (LM-style) refinement using local tangents (du/dv).
    {
      const auto ev0 = eval(surface_id, std::clamp(uv.first, domain.u.min, domain.u.max),
                            std::clamp(uv.second, domain.v.min, domain.v.max), 0);
      if (ev0.status == StatusCode::Ok && ev0.value.has_value()) {
        best_cost = squared_distance(ev0.value->point, point);
      }
    }
    Scalar lambda = 1e-6;
    for (int iter = 0; iter < 22; ++iter) {
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
    Scalar uu = base_uv.value->first;
    Scalar vv = base_uv.value->second;
    apply_trimmed_surface_uv_in_parameter_space(surface, uu, vv);
    return ok_result(std::make_pair(uu, vv),
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
