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
  for (const auto &p : desc.poles) {
    if (!finite_point(p)) {
      return detail::invalid_input_result<SurfaceId>(
          *state_, diag_codes::kGeoSurfaceCreationInvalid,
          "BSpline 曲面创建失败：输入包含非法数值", "BSpline 曲面创建失败");
    }
  }
  const auto [rows, cols] = infer_surface_grid_dims(desc.poles.size());
  if (rows < 2 || cols < 2) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "BSpline 曲面创建失败：控制点需构成至少 2x2 网格", "BSpline 曲面创建失败");
  }
  std::vector<Scalar> knots_u;
  std::vector<Scalar> knots_v;
  int pu = 0;
  int pv = 0;
  if (!resolve_bspline_surface_axis(static_cast<int>(rows), desc.degree_u, desc.knots_u,
                                    knots_u, pu)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "BSpline 曲面创建失败：u 向阶数或结点向量无效", "BSpline 曲面创建失败");
  }
  if (!resolve_bspline_surface_axis(static_cast<int>(cols), desc.degree_v, desc.knots_v,
                                    knots_v, pv)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "BSpline 曲面创建失败：v 向阶数或结点向量无效", "BSpline 曲面创建失败");
  }
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(
      id.value,
      make_surface_record(detail::SurfaceKind::BSpline, desc.poles.front(),
                          {0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}, 0.0, 0.0, 0.0, desc.poles, {},
                          std::move(knots_u), std::move(knots_v), SurfaceId{}, CurveId{}, 0.0,
                          0.0, 0.0, 1.0, 0.0, 1.0, {}, 0.0, pu, pv));
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
  if (rows < 2 || cols < 2) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "NURBS 曲面创建失败：控制点需构成至少 2x2 网格", "NURBS 曲面创建失败");
  }
  std::vector<Scalar> knots_u;
  std::vector<Scalar> knots_v;
  int pu = 0;
  int pv = 0;
  if (!resolve_bspline_surface_axis(static_cast<int>(rows), desc.degree_u, desc.knots_u,
                                    knots_u, pu)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "NURBS 曲面创建失败：u 向阶数或结点向量无效", "NURBS 曲面创建失败");
  }
  if (!resolve_bspline_surface_axis(static_cast<int>(cols), desc.degree_v, desc.knots_v,
                                    knots_v, pv)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kGeoSurfaceCreationInvalid,
        "NURBS 曲面创建失败：v 向阶数或结点向量无效", "NURBS 曲面创建失败");
  }
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(
      id.value,
      make_surface_record(detail::SurfaceKind::Nurbs, desc.poles.front(),
                          {0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}, 0.0, 0.0, 0.0, desc.poles,
                          std::move(normalized_weights), std::move(knots_u),
                          std::move(knots_v), SurfaceId{}, CurveId{}, 0.0, 0.0, 0.0, 1.0,
                          0.0, 1.0, {}, 0.0, pu, pv));
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
  return make_trimmed_polygon(base_surface, u_min, u_max, v_min, v_max, {});
}

Result<SurfaceId> SurfaceFactory::make_trimmed_polygon(SurfaceId base_surface, Scalar u_min,
                                                       Scalar u_max, Scalar v_min,
                                                       Scalar v_max,
                                                       std::span<const Point2> uv_boundary_loop) {
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
  std::vector<Point2> loop(uv_boundary_loop.begin(), uv_boundary_loop.end());
  if (!loop.empty()) {
    if (loop.size() < 3) {
      return detail::invalid_input_result<SurfaceId>(
          *state_, diag_codes::kGeoSurfaceCreationInvalid,
          "修剪面创建失败：UV 边界折线至少需要 3 个顶点", "修剪面创建失败");
    }
    for (const auto &q : loop) {
      if (!finite_scalar(q.x) || !finite_scalar(q.y)) {
        return detail::invalid_input_result<SurfaceId>(
            *state_, diag_codes::kGeoSurfaceCreationInvalid,
            "修剪面创建失败：UV 边界包含非法数值", "修剪面创建失败");
      }
    }
  }
  const auto &base = state_->surfaces.at(base_surface.value);
  const auto id = SurfaceId{state_->allocate_id()};
  state_->surfaces.emplace(
      id.value,
      make_surface_record(detail::SurfaceKind::Trimmed, base.origin, base.axis,
                          base.normal, base.radius_a, base.radius_b,
                          base.semi_angle, {}, {}, {}, {}, base_surface, {},
                          0.0, 0.0, u_min, u_max, v_min, v_max, std::move(loop)));
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
                          0.0, 0.0, 0.0, 1.0, 0.0, 1.0, {}, offset_distance));
  return ok_result(id, state_->create_diagnostic("已创建偏置面"));
}
