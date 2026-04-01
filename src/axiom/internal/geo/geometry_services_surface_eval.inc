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
    const auto cu = std::clamp(u, domain.u.min, domain.u.max);
    const auto cv = std::clamp(v, domain.v.min, domain.v.max);
    if (bezier_tensor_surface_eval_with_partials(surface, cu, cv, result.point, result.du,
                                                 result.dv)) {
      result.normal = normalize_or_default(detail::cross(result.du, result.dv),
                                           Vec3{0.0, 0.0, 1.0});
      tensor_spline_surface_principal_curvature_improved(surface, cu, cv, domain, result.k1,
                                                         result.k2);
    } else {
      const auto eval_fn = [&surface](Scalar uu, Scalar vv) {
        return evaluate_surface_point(surface, uu, vv);
      };
      result.point = eval_fn(cu, cv);
      result.du = surface_partial_u_from_eval(eval_fn, cu, cv, domain);
      result.dv = surface_partial_v_from_eval(eval_fn, cu, cv, domain);
      spline_surface_principal_curvature_fd(surface, cu, cv, domain, result.k1,
                                            result.k2);
      result.normal = normalize_or_default(detail::cross(result.du, result.dv),
                                           Vec3{0.0, 0.0, 1.0});
    }
    break;
  }
  case detail::SurfaceKind::BSpline:
  case detail::SurfaceKind::Nurbs: {
    const auto domain = surface_domain(surface);
    const auto cu = std::clamp(u, domain.u.min, domain.u.max);
    const auto cv = std::clamp(v, domain.v.min, domain.v.max);
    if (nurbs_tensor_surface_eval_with_partials(surface, cu, cv, result.point, result.du,
                                                 result.dv)) {
      result.normal = normalize_or_default(detail::cross(result.du, result.dv),
                                           Vec3{0.0, 0.0, 1.0});
      parametric_surface_principal_curvature_fd(
          [&surface](Scalar uu, Scalar vv) {
            return evaluate_surface_point(surface, uu, vv);
          },
          cu, cv, domain, result.k1, result.k2);
    } else {
      Point3 p00{};
      Point3 p01{};
      Point3 p10{};
      Point3 p11{};
      Scalar lu = 0.0;
      Scalar lv = 0.0;
      bool rational = false;
      Scalar w00 = 1.0;
      Scalar w01 = 1.0;
      Scalar w10 = 1.0;
      Scalar w11 = 1.0;
      if (locate_surface_bilinear_cell(surface, cu, cv, p00, p01, p10, p11, lu, lv,
                                       rational, w00, w01, w10, w11)) {
        if (rational) {
          rational_bilinear_patch_partials(p00, p01, p10, p11, w00, w01, w10, w11, lu,
                                           lv, result.point, result.du, result.dv);
          spline_surface_principal_curvature_fd(surface, cu, cv, domain, result.k1,
                                                result.k2);
        } else {
          bilinear_patch_partials_non_rational(p00, p01, p10, p11, lu, lv, result.point,
                                               result.du, result.dv);
          bilinear_patch_principal_curvatures_non_rational(p00, p01, p10, p11, lu, lv,
                                                             result.k1, result.k2);
        }
      } else {
        const auto eval_fn = [&surface](Scalar uu, Scalar vv) {
          return evaluate_surface_point(surface, uu, vv);
        };
        result.point = eval_fn(cu, cv);
        result.du = surface_partial_u_from_eval(eval_fn, cu, cv, domain);
        result.dv = surface_partial_v_from_eval(eval_fn, cu, cv, domain);
        spline_surface_principal_curvature_fd(surface, cu, cv, domain, result.k1,
                                              result.k2);
      }
      result.normal = normalize_or_default(detail::cross(result.du, result.dv),
                                           Vec3{0.0, 0.0, 1.0});
    }
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
    result.du = surface_partial_u_from_eval(eval_pt, cu, cv, dom);
    result.dv = surface_partial_v_from_eval(eval_pt, cu, cv, dom);
    result.normal = normalize_or_default(detail::cross(result.du, result.dv),
                                         Vec3{0.0, 0.0, 1.0});
    parametric_surface_principal_curvature_fd(eval_pt, cu, cv, dom, result.k1,
                                              result.k2);
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
    const auto eval_pt = [&](Scalar uu, Scalar vv) -> Point3 {
      const auto cuu = std::clamp(uu, dom.u.min, dom.u.max);
      const auto cvv = std::clamp(vv, dom.v.min, dom.v.max);
      const auto tvv = prof_dom.min + cvv * (prof_dom.max - prof_dom.min);
      const auto ev = curves.eval(surface.profile_curve_id, tvv, 0);
      if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
        return surface.origin;
      }
      return detail::add_point_vec(ev.value->point, detail::scale(dir, cuu * L));
    };
    result.point = eval_pt(cu, cv);
    result.du = surface_partial_u_from_eval(eval_pt, cu, cv, dom);
    result.dv = surface_partial_v_from_eval(eval_pt, cu, cv, dom);
    result.normal = normalize_or_default(detail::cross(result.du, result.dv),
                                         Vec3{0.0, 0.0, 1.0});
    parametric_surface_principal_curvature_fd(eval_pt, cu, cv, dom, result.k1,
                                              result.k2);
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
    Scalar cu = std::clamp(u, dom.u.min, dom.u.max);
    Scalar cv = std::clamp(v, dom.v.min, dom.v.max);
    apply_trimmed_surface_uv_in_parameter_space(surface, cu, cv);
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
    SurfaceService base_svc{state_};
    const auto base_dom = base_svc.domain(surface.base_surface_id);
    Range2D dom = base_dom.value.has_value() ? *base_dom.value
                                             : Range2D{Range1D{0.0, 1.0}, Range1D{0.0, 1.0}};
    const auto cu = std::clamp(u, dom.u.min, dom.u.max);
    const auto cv = std::clamp(v, dom.v.min, dom.v.max);
    const SurfaceId base_id = surface.base_surface_id;
    const Scalar off_d = surface.offset_distance;
    const auto eval_offset_pt = [&](Scalar uu, Scalar vv) -> Point3 {
      const auto e = base_svc.eval(base_id, uu, vv, 1);
      if (e.status != StatusCode::Ok || !e.value.has_value()) {
        return surface.origin;
      }
      const auto &b = *e.value;
      return detail::add_point_vec(b.point, detail::scale(b.normal, off_d));
    };
    result.point = eval_offset_pt(cu, cv);
    result.du = surface_partial_u_from_eval(eval_offset_pt, cu, cv, dom);
    result.dv = surface_partial_v_from_eval(eval_offset_pt, cu, cv, dom);
    result.normal = normalize_or_default(detail::cross(result.du, result.dv),
                                         Vec3{0.0, 0.0, 1.0});
    parametric_surface_principal_curvature_fd(eval_offset_pt, cu, cv, dom,
                                                result.k1, result.k2);
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
