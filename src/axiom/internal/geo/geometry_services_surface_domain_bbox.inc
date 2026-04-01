namespace {
constexpr int kDerivedSurfaceBboxSegments = 14;

BoundingBox sample_derived_surface_bbox(
    const std::shared_ptr<detail::KernelState> &state, SurfaceId surface_id,
    const detail::SurfaceRecord &surface) {
  Range2D domain = surface_domain(surface);
  if (surface.kind == detail::SurfaceKind::Offset &&
      detail::has_surface(*state, surface.base_surface_id)) {
    domain = surface_domain(state->surfaces.at(surface.base_surface_id.value));
  }
  const Scalar span_u = domain.u.max - domain.u.min;
  const Scalar span_v = domain.v.max - domain.v.min;
  if (!std::isfinite(span_u) || !std::isfinite(span_v) ||
      !(span_u > 1e-18) || !(span_v > 1e-18)) {
    return make_surface_bbox(surface);
  }
  SurfaceService geo{state};
  bool any = false;
  Point3 lo{};
  Point3 hi{};
  for (int iu = 0; iu <= kDerivedSurfaceBboxSegments; ++iu) {
    const Scalar t_u =
        static_cast<Scalar>(iu) / static_cast<Scalar>(kDerivedSurfaceBboxSegments);
    const Scalar u = domain.u.min + span_u * t_u;
    for (int iv = 0; iv <= kDerivedSurfaceBboxSegments; ++iv) {
      const Scalar t_v =
          static_cast<Scalar>(iv) / static_cast<Scalar>(kDerivedSurfaceBboxSegments);
      const Scalar v = domain.v.min + span_v * t_v;
      const auto er = geo.eval(surface_id, u, v, 0);
      if (er.status != StatusCode::Ok || !er.value.has_value()) {
        continue;
      }
      const auto &p = er.value->point;
      if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
        continue;
      }
      if (!any) {
        lo = hi = p;
        any = true;
      } else {
        lo.x = std::min(lo.x, p.x);
        lo.y = std::min(lo.y, p.y);
        lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x);
        hi.y = std::max(hi.y, p.y);
        hi.z = std::max(hi.z, p.z);
      }
    }
  }
  if (!any) {
    return make_surface_bbox(surface);
  }
  return detail::make_bbox(lo, hi);
}
}  // namespace

Result<Range2D> SurfaceService::domain(SurfaceId surface_id) const {
  if (!detail::has_surface(*state_, surface_id)) {
    return detail::invalid_input_result<Range2D>(
        *state_, diag_codes::kCoreInvalidHandle,
        "曲面定义域查询失败：目标曲面不存在", "曲面定义域查询失败");
  }
  const auto &s = state_->surfaces.at(surface_id.value);
  if (s.kind == detail::SurfaceKind::Offset &&
      detail::has_surface(*state_, s.base_surface_id)) {
    return ok_result(
        surface_domain(state_->surfaces.at(s.base_surface_id.value)),
        state_->create_diagnostic("已返回曲面定义域"));
  }
  return ok_result(surface_domain(s),
                   state_->create_diagnostic("已返回曲面定义域"));
}

Result<BoundingBox> SurfaceService::bbox(SurfaceId surface_id) const {
  const auto it = state_->surfaces.find(surface_id.value);
  if (it == state_->surfaces.end()) {
    return detail::invalid_input_result<BoundingBox>(
        *state_, diag_codes::kCoreInvalidHandle,
        "曲面包围盒查询失败：目标曲面不存在", "曲面包围盒查询失败");
  }
  const auto &rec = it->second;
  switch (rec.kind) {
  case detail::SurfaceKind::Revolved:
  case detail::SurfaceKind::Swept:
  case detail::SurfaceKind::Trimmed:
  case detail::SurfaceKind::Offset:
    return ok_result(sample_derived_surface_bbox(state_, surface_id, rec),
                     state_->create_diagnostic("已返回曲面包围盒"));
  default:
    return ok_result(make_surface_bbox(rec),
                     state_->create_diagnostic("已返回曲面包围盒"));
  }
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
