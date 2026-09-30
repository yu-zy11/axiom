namespace {

struct SurfaceSearchBounds {
  long double du{};
  long double dv{};
  long double fixed_radius{};
};

bool surface_search_frame(const detail::KernelState &state, SurfaceId surface_id,
                          Scalar u, Scalar v, Point3 &point, Vec3 &du, Vec3 &dv,
                          std::uint32_t depth = 0) {
  if (depth > 32) {
    return false;
  }
  const auto found = state.surfaces.find(surface_id.value);
  if (found == state.surfaces.end()) {
    return false;
  }
  const auto &surface = found->second;
  const auto frame = make_frame_from_axis(surface.normal);
  switch (surface.kind) {
  case detail::SurfaceKind::Plane:
    point = point_from_local(surface.origin, frame, u, v, 0.0);
    du = frame.u;
    dv = frame.v;
    return finite_point(point);
  case detail::SurfaceKind::Sphere:
    point = Point3{surface.origin.x + surface.radius_a * std::cos(u) * std::sin(v),
                   surface.origin.y + surface.radius_a * std::sin(u) * std::sin(v),
                   surface.origin.z + surface.radius_a * std::cos(v)};
    du = Vec3{-surface.radius_a * std::sin(u) * std::sin(v),
              surface.radius_a * std::cos(u) * std::sin(v), 0.0};
    dv = Vec3{surface.radius_a * std::cos(u) * std::cos(v),
              surface.radius_a * std::sin(u) * std::cos(v),
              -surface.radius_a * std::sin(v)};
    return finite_point(point) && finite_vec(du) && finite_vec(dv);
  case detail::SurfaceKind::Cylinder:
    point = point_from_local(surface.origin, frame, surface.radius_a * std::cos(u),
                             surface.radius_a * std::sin(u), v);
    du = vec_from_local(frame, -surface.radius_a * std::sin(u),
                        surface.radius_a * std::cos(u), 0.0);
    dv = frame.w;
    return finite_point(point);
  case detail::SurfaceKind::Cone: {
    const Scalar slope = std::tan(surface.semi_angle);
    const Scalar radial = slope * v;
    point = point_from_local(surface.origin, frame, radial * std::cos(u),
                             radial * std::sin(u), v);
    du = vec_from_local(frame, -radial * std::sin(u), radial * std::cos(u), 0.0);
    dv = vec_from_local(frame, slope * std::cos(u), slope * std::sin(u), 1.0);
    return finite_point(point) && finite_vec(du) && finite_vec(dv);
  }
  case detail::SurfaceKind::Torus: {
    const Scalar ring = surface.radius_a + surface.radius_b * std::cos(v);
    point = point_from_local(surface.origin, frame, ring * std::cos(u),
                             ring * std::sin(u), surface.radius_b * std::sin(v));
    du = vec_from_local(frame, -ring * std::sin(u), ring * std::cos(u), 0.0);
    dv = vec_from_local(frame, -surface.radius_b * std::sin(v) * std::cos(u),
                        -surface.radius_b * std::sin(v) * std::sin(u),
                        surface.radius_b * std::cos(v));
    return finite_point(point) && finite_vec(du) && finite_vec(dv);
  }
  case detail::SurfaceKind::Bezier:
    return bezier_tensor_surface_eval_with_partials(surface, u, v, point, du, dv) &&
           finite_point(point) && finite_vec(du) && finite_vec(dv);
  case detail::SurfaceKind::BSpline:
  case detail::SurfaceKind::Nurbs:
    return nurbs_tensor_surface_eval_with_partials(surface, u, v, point, du, dv) &&
           finite_point(point) && finite_vec(du) && finite_vec(dv);
  case detail::SurfaceKind::Swept: {
    const auto curve = state.curves.find(surface.profile_curve_id.value);
    if (curve == state.curves.end()) {
      return false;
    }
    const auto curve_dom = curve_domain(curve->second);
    const Scalar t = curve_dom.min + v * (curve_dom.max - curve_dom.min);
    Point3 profile{};
    Vec3 profile_d{};
    if (!closest_search_point(state, surface.profile_curve_id, t, profile) ||
        !closest_search_derivative(state, surface.profile_curve_id, t, profile_d)) {
      return false;
    }
    const auto direction = normalize_or_default(surface.axis, Vec3{0.0, 0.0, 1.0});
    point = detail::add_point_vec(profile,
                                  detail::scale(direction, u * surface.sweep_length));
    du = detail::scale(direction, surface.sweep_length);
    dv = detail::scale(profile_d, curve_dom.max - curve_dom.min);
    return finite_point(point) && finite_vec(du) && finite_vec(dv);
  }
  case detail::SurfaceKind::Revolved: {
    const auto curve = state.curves.find(surface.profile_curve_id.value);
    if (curve == state.curves.end()) {
      return false;
    }
    const auto curve_dom = curve_domain(curve->second);
    const Scalar t = curve_dom.min + v * (curve_dom.max - curve_dom.min);
    Point3 profile{};
    Vec3 profile_d{};
    if (!closest_search_point(state, surface.profile_curve_id, t, profile) ||
        !closest_search_derivative(state, surface.profile_curve_id, t, profile_d)) {
      return false;
    }
    const auto axis = normalize_or_default(surface.axis, Vec3{0.0, 0.0, 1.0});
    const auto relative = detail::subtract(profile, surface.origin);
    const auto rotated = rotate_vec_around_axis(axis, relative, u);
    point = detail::add_point_vec(surface.origin, rotated);
    du = detail::cross(axis, rotated);
    dv = detail::scale(rotate_vec_around_axis(axis, profile_d, u),
                       curve_dom.max - curve_dom.min);
    return finite_point(point) && finite_vec(du) && finite_vec(dv);
  }
  case detail::SurfaceKind::Trimmed:
    return surface_search_frame(state, surface.base_surface_id, u, v, point, du, dv,
                                depth + 1);
  case detail::SurfaceKind::Offset: {
    Point3 base_point{};
    Vec3 base_du{};
    Vec3 base_dv{};
    if (!surface_search_frame(state, surface.base_surface_id, u, v, base_point,
                              base_du, base_dv, depth + 1)) {
      return false;
    }
    const auto base_record = state.surfaces.find(surface.base_surface_id.value);
    const auto cross = detail::cross(base_du, base_dv);
    Vec3 normal{};
    if (base_record != state.surfaces.end() &&
        base_record->second.kind == detail::SurfaceKind::Sphere) {
      normal = normalize_or_default(detail::subtract(base_point, base_record->second.origin),
                                    Vec3{0.0, 0.0, 1.0});
    } else {
      if (!(detail::norm(cross) > static_cast<Scalar>(1e-20))) {
        return false;
      }
      normal = detail::normalize(cross);
    }
    point = detail::add_point_vec(base_point,
                                  detail::scale(normal, surface.offset_distance));
    // Parallel regular surfaces share the same normal. These tangents are sufficient for
    // nested offsets; the global bound accounts separately for normal variation.
    du = base_du;
    dv = base_dv;
    return finite_point(point);
  }
  default:
    return false;
  }
}

bool tensor_surface_speed_bounds(const detail::SurfaceRecord &surface,
                                 long double &bound_u, long double &bound_v,
                                 const Range2D *patch = nullptr) {
  const auto [rows_size, cols_size] = infer_surface_grid_dims(surface.poles.size());
  if (rows_size < 1 || cols_size < 1) {
    return false;
  }
  int rows = static_cast<int>(rows_size);
  int cols = static_cast<int>(cols_size);
  int degree_u = rows - 1;
  int degree_v = cols - 1;
  std::vector<Scalar> knots_u;
  std::vector<Scalar> knots_v;
  if (surface.kind == detail::SurfaceKind::Bezier) {
    knots_u = bspline_open_uniform_knots(rows_size, degree_u);
    knots_v = bspline_open_uniform_knots(cols_size, degree_v);
  } else if (!nurbs_tensor_surface_prepare(surface, rows, cols, degree_u, degree_v,
                                            knots_u, knots_v)) {
    return false;
  }

  const bool rational = surface.kind == detail::SurfaceKind::Nurbs &&
                        surface.weights.size() == surface.poles.size();
  const Point3 reference = surface.poles.front();
  long double minimum_weight = std::numeric_limits<long double>::infinity();
  long double maximum_homogeneous_point = 0.0L;
  std::vector<std::array<long double, 4>> homogeneous(surface.poles.size());
  for (std::size_t i = 0; i < surface.poles.size(); ++i) {
    const long double weight = rational ? surface.weights[i] : 1.0L;
    const long double x = (static_cast<long double>(surface.poles[i].x) - reference.x) * weight;
    const long double y = (static_cast<long double>(surface.poles[i].y) - reference.y) * weight;
    const long double z = (static_cast<long double>(surface.poles[i].z) - reference.z) * weight;
    homogeneous[i] = {x, y, z, weight};
    minimum_weight = std::min(minimum_weight, weight);
    maximum_homogeneous_point =
        std::max(maximum_homogeneous_point, std::hypot(x, y, z));
  }
  if (!(minimum_weight > 0.0L)) {
    return false;
  }

  long double xyz_u = 0.0L;
  long double xyz_v = 0.0L;
  long double weight_u = 0.0L;
  long double weight_v = 0.0L;
  const auto index = [cols](int i, int j) {
    return static_cast<std::size_t>(i * cols + j);
  };
  int derivative_u_begin = 0;
  int derivative_u_end = rows - 2;
  int support_u_begin = 0;
  int support_u_end = rows - 1;
  int derivative_v_begin = 0;
  int derivative_v_end = cols - 2;
  int support_v_begin = 0;
  int support_v_end = cols - 1;
  if (patch != nullptr && surface.kind != detail::SurfaceKind::Bezier) {
    const Scalar middle_u = patch->u.min + (patch->u.max - patch->u.min) * 0.5;
    const Scalar middle_v = patch->v.min + (patch->v.max - patch->v.min) * 0.5;
    const int span_u = bspline_find_span(rows_size, degree_u, middle_u, knots_u);
    const int span_v = bspline_find_span(cols_size, degree_v, middle_v, knots_v);
    derivative_u_begin = std::max(0, span_u - degree_u);
    derivative_u_end = std::min(rows - 2, span_u - 1);
    support_u_begin = std::max(0, span_u - degree_u);
    support_u_end = std::min(rows - 1, span_u);
    derivative_v_begin = std::max(0, span_v - degree_v);
    derivative_v_end = std::min(cols - 2, span_v - 1);
    support_v_begin = std::max(0, span_v - degree_v);
    support_v_end = std::min(cols - 1, span_v);
  }
  for (int i = derivative_u_begin; i <= derivative_u_end; ++i) {
    const long double denominator =
        static_cast<long double>(knots_u[static_cast<std::size_t>(i + degree_u + 1)]) -
        knots_u[static_cast<std::size_t>(i + 1)];
    if (!(denominator > 0.0L)) {
      continue;
    }
    const long double factor = static_cast<long double>(degree_u) / denominator;
    for (int j = support_v_begin; j <= support_v_end; ++j) {
      const auto &a = homogeneous[index(i, j)];
      const auto &b = homogeneous[index(i + 1, j)];
      xyz_u = std::max(xyz_u, factor * std::hypot(b[0] - a[0], b[1] - a[1],
                                                   b[2] - a[2]));
      weight_u = std::max(weight_u, factor * std::abs(b[3] - a[3]));
    }
  }
  for (int j = derivative_v_begin; j <= derivative_v_end; ++j) {
    const long double denominator =
        static_cast<long double>(knots_v[static_cast<std::size_t>(j + degree_v + 1)]) -
        knots_v[static_cast<std::size_t>(j + 1)];
    if (!(denominator > 0.0L)) {
      continue;
    }
    const long double factor = static_cast<long double>(degree_v) / denominator;
    for (int i = support_u_begin; i <= support_u_end; ++i) {
      const auto &a = homogeneous[index(i, j)];
      const auto &b = homogeneous[index(i, j + 1)];
      xyz_v = std::max(xyz_v, factor * std::hypot(b[0] - a[0], b[1] - a[1],
                                                   b[2] - a[2]));
      weight_v = std::max(weight_v, factor * std::abs(b[3] - a[3]));
    }
  }
  const long double inverse_weight = 1.0L / minimum_weight;
  bound_u = xyz_u * inverse_weight +
            maximum_homogeneous_point * weight_u * inverse_weight * inverse_weight;
  bound_v = xyz_v * inverse_weight +
            maximum_homogeneous_point * weight_v * inverse_weight * inverse_weight;
  return std::isfinite(bound_u) && std::isfinite(bound_v);
}

bool surface_search_bounds(const detail::KernelState &state, SurfaceId surface_id,
                           const Range2D &domain, SurfaceSearchBounds &bounds,
                           std::uint32_t depth = 0) {
  if (depth > 32) {
    return false;
  }
  const auto found = state.surfaces.find(surface_id.value);
  if (found == state.surfaces.end()) {
    return false;
  }
  const auto &surface = found->second;
  switch (surface.kind) {
  case detail::SurfaceKind::Plane:
    bounds.du = bounds.dv = 1.0L;
    return true;
  case detail::SurfaceKind::Sphere:
    bounds.du = bounds.dv = std::abs(static_cast<long double>(surface.radius_a));
    return true;
  case detail::SurfaceKind::Cylinder:
    bounds.du = std::abs(static_cast<long double>(surface.radius_a));
    bounds.dv = 1.0L;
    return true;
  case detail::SurfaceKind::Cone: {
    const long double slope = std::abs(std::tan(static_cast<long double>(surface.semi_angle)));
    bounds.du = slope * std::max(std::abs(static_cast<long double>(domain.v.min)),
                                 std::abs(static_cast<long double>(domain.v.max)));
    bounds.dv = std::hypot(1.0L, slope);
    return std::isfinite(bounds.du) && std::isfinite(bounds.dv);
  }
  case detail::SurfaceKind::Torus:
    bounds.du = std::abs(static_cast<long double>(surface.radius_a)) +
                std::abs(static_cast<long double>(surface.radius_b));
    bounds.dv = std::abs(static_cast<long double>(surface.radius_b));
    return true;
  case detail::SurfaceKind::Bezier:
  case detail::SurfaceKind::BSpline:
  case detail::SurfaceKind::Nurbs:
    return tensor_surface_speed_bounds(surface, bounds.du, bounds.dv);
  case detail::SurfaceKind::Swept:
  case detail::SurfaceKind::Revolved: {
    const auto curve = state.curves.find(surface.profile_curve_id.value);
    if (curve == state.curves.end()) {
      return false;
    }
    const auto curve_dom = curve_domain(curve->second);
    const long double speed = closest_search_speed_bound(
        state, surface.profile_curve_id, curve_dom.min, curve_dom.max);
    const long double curve_span = static_cast<long double>(curve_dom.max) - curve_dom.min;
    if (!std::isfinite(speed) || !std::isfinite(curve_span)) {
      return false;
    }
    bounds.dv = speed * curve_span;
    if (surface.kind == detail::SurfaceKind::Swept) {
      bounds.du = std::abs(static_cast<long double>(surface.sweep_length));
      return true;
    }
    Point3 midpoint{};
    const Scalar middle_t = curve_dom.min + (curve_dom.max - curve_dom.min) * 0.5;
    if (!closest_search_point(state, surface.profile_curve_id, middle_t, midpoint)) {
      return false;
    }
    const long double center_radius = closest_distance(midpoint, surface.origin);
    bounds.du = center_radius + speed * curve_span * 0.5L;
    return std::isfinite(bounds.du);
  }
  case detail::SurfaceKind::Trimmed:
    return surface_search_bounds(state, surface.base_surface_id, domain, bounds, depth + 1);
  case detail::SurfaceKind::Offset:
    if (!surface_search_bounds(state, surface.base_surface_id, domain, bounds, depth + 1)) {
      return false;
    }
    if (const auto base = state.surfaces.find(surface.base_surface_id.value);
        base != state.surfaces.end()) {
      const auto &base_surface = base->second;
      if (base_surface.kind == detail::SurfaceKind::Plane) {
        return true;
      }
      if (base_surface.kind == detail::SurfaceKind::Sphere) {
        bounds.du = bounds.dv = std::abs(static_cast<long double>(base_surface.radius_a) +
                                         surface.offset_distance);
        return true;
      }
      if (base_surface.kind == detail::SurfaceKind::Cylinder) {
        bounds.du = std::abs(static_cast<long double>(base_surface.radius_a) +
                             surface.offset_distance);
        bounds.dv = 1.0L;
        return true;
      }
      if (base_surface.kind == detail::SurfaceKind::Torus) {
        const long double effective_minor =
            std::abs(static_cast<long double>(base_surface.radius_b) +
                     surface.offset_distance);
        bounds.du = std::abs(static_cast<long double>(base_surface.radius_a)) +
                    effective_minor;
        bounds.dv = effective_minor;
        return true;
      }
    }
    // Without assuming a global curvature bound, two unit normals can differ by at most two.
    // This fixed cell radius is conservative; parameter-tolerance termination remains available.
    bounds.fixed_radius += 2.0L * std::abs(static_cast<long double>(surface.offset_distance));
    return std::isfinite(bounds.fixed_radius);
  default:
    return false;
  }
}

void tighten_tensor_patch_bounds(const detail::KernelState &state,
                                 SurfaceId surface_id, const Range2D &patch,
                                 SurfaceSearchBounds &bounds,
                                 std::uint32_t depth = 0) {
  if (depth > 32) {
    return;
  }
  const auto found = state.surfaces.find(surface_id.value);
  if (found == state.surfaces.end()) {
    return;
  }
  const auto &surface = found->second;
  if (surface.kind == detail::SurfaceKind::Bezier ||
      surface.kind == detail::SurfaceKind::BSpline ||
      surface.kind == detail::SurfaceKind::Nurbs) {
    long double patch_du = bounds.du;
    long double patch_dv = bounds.dv;
    if (tensor_surface_speed_bounds(surface, patch_du, patch_dv, &patch)) {
      bounds.du = patch_du;
      bounds.dv = patch_dv;
    }
    return;
  }
  if (surface.kind == detail::SurfaceKind::Trimmed ||
      surface.kind == detail::SurfaceKind::Offset) {
    tighten_tensor_patch_bounds(state, surface.base_surface_id, patch, bounds,
                                depth + 1);
  }
}

struct TensorPatchHull {
  std::array<long double, 3> minimum{
      std::numeric_limits<long double>::infinity(),
      std::numeric_limits<long double>::infinity(),
      std::numeric_limits<long double>::infinity()};
  std::array<long double, 3> maximum{
      -std::numeric_limits<long double>::infinity(),
      -std::numeric_limits<long double>::infinity(),
      -std::numeric_limits<long double>::infinity()};
  long double offset_padding{};
};

int exact_knot_multiplicity(const std::vector<Scalar> &knots, Scalar knot) {
  return static_cast<int>(
      std::count(knots.begin(), knots.end(), knot));
}

// Inserts one knot through every row/column of a homogeneous tensor net.  The
// operation is affine in R4, so positive rational weights stay positive and the
// Euclidean control points of an extracted patch retain the convex-hull property.
bool insert_tensor_surface_knot(std::vector<Hom4> &net, int &nu, int &nv,
                                std::vector<Scalar> &knots, int degree,
                                Scalar knot, bool along_u) {
  const int axis_count = along_u ? nu : nv;
  const int strip_count = along_u ? nv : nu;
  if (axis_count < 1 || degree < 0 || degree >= axis_count ||
      static_cast<int>(knots.size()) != axis_count + degree + 1 ||
      static_cast<int>(net.size()) != nu * nv) {
    return false;
  }
  const int multiplicity = exact_knot_multiplicity(knots, knot);
  if (multiplicity >= degree + 1) {
    return true;
  }
  const int span = bspline_find_span(static_cast<std::size_t>(axis_count),
                                     degree, knot, knots);
  if (span < degree || span >= axis_count) {
    return false;
  }

  std::vector<Scalar> inserted_knots;
  inserted_knots.reserve(knots.size() + 1);
  inserted_knots.insert(inserted_knots.end(), knots.begin(),
                        knots.begin() + span + 1);
  inserted_knots.push_back(knot);
  inserted_knots.insert(inserted_knots.end(), knots.begin() + span + 1,
                        knots.end());

  const int new_nu = along_u ? nu + 1 : nu;
  const int new_nv = along_u ? nv : nv + 1;
  std::vector<Hom4> inserted_net(
      static_cast<std::size_t>(new_nu * new_nv));
  const auto old_at = [&](int axis, int strip) -> const Hom4 & {
    const int i = along_u ? axis : strip;
    const int j = along_u ? strip : axis;
    return net[static_cast<std::size_t>(i * nv + j)];
  };
  const auto assign = [&](int axis, int strip, const Hom4 &value) {
    const int i = along_u ? axis : strip;
    const int j = along_u ? strip : axis;
    inserted_net[static_cast<std::size_t>(i * new_nv + j)] = value;
  };
  for (int strip = 0; strip < strip_count; ++strip) {
    for (int i = 0; i <= span - degree; ++i) {
      assign(i, strip, old_at(i, strip));
    }
    for (int i = span - multiplicity; i < axis_count; ++i) {
      assign(i + 1, strip, old_at(i, strip));
    }
    for (int i = span - degree + 1; i <= span - multiplicity; ++i) {
      const Scalar denominator =
          knots[static_cast<std::size_t>(i + degree)] -
          knots[static_cast<std::size_t>(i)];
      if (!(denominator > 0.0) || !finite_scalar(denominator)) {
        return false;
      }
      const Scalar alpha =
          (knot - knots[static_cast<std::size_t>(i)]) / denominator;
      if (!finite_scalar(alpha) || alpha < 0.0 || alpha > 1.0) {
        return false;
      }
      assign(i, strip,
             lerp_hom4(old_at(i - 1, strip), old_at(i, strip), alpha));
    }
  }
  net = std::move(inserted_net);
  knots = std::move(inserted_knots);
  nu = new_nu;
  nv = new_nv;
  return true;
}

bool insert_tensor_surface_knot_to_full_multiplicity(
    std::vector<Hom4> &net, int &nu, int &nv, std::vector<Scalar> &knots,
    int degree, Scalar knot, bool along_u) {
  while (exact_knot_multiplicity(knots, knot) < degree + 1) {
    if (!insert_tensor_surface_knot(net, nu, nv, knots, degree, knot,
                                    along_u)) {
      return false;
    }
  }
  return true;
}

int restricted_patch_control_begin(const std::vector<Scalar> &knots,
                                   Scalar lower, int degree) {
  int last = -1;
  for (std::size_t i = 0; i < knots.size(); ++i) {
    if (knots[i] == lower) {
      last = static_cast<int>(i);
    }
  }
  return last - degree;
}

bool tensor_surface_patch_hull(const detail::SurfaceRecord &surface,
                               const Range2D &patch, TensorPatchHull &hull) {
  if (surface.kind != detail::SurfaceKind::Bezier &&
      surface.kind != detail::SurfaceKind::BSpline &&
      surface.kind != detail::SurfaceKind::Nurbs) {
    return false;
  }
  const auto [rows_size, cols_size] =
      infer_surface_grid_dims(surface.poles.size());
  if (rows_size < 1 || cols_size < 1) {
    return false;
  }
  int nu = static_cast<int>(rows_size);
  int nv = static_cast<int>(cols_size);
  int degree_u = nu - 1;
  int degree_v = nv - 1;
  std::vector<Scalar> knots_u;
  std::vector<Scalar> knots_v;
  if (surface.kind == detail::SurfaceKind::Bezier) {
    knots_u = bspline_open_uniform_knots(rows_size, degree_u);
    knots_v = bspline_open_uniform_knots(cols_size, degree_v);
  } else if (!nurbs_tensor_surface_prepare(surface, nu, nv, degree_u,
                                            degree_v, knots_u, knots_v)) {
    return false;
  }
  if (!(patch.u.max > patch.u.min) || !(patch.v.max > patch.v.min) ||
      patch.u.min < knots_u[static_cast<std::size_t>(degree_u)] ||
      patch.u.max > knots_u[static_cast<std::size_t>(nu)] ||
      patch.v.min < knots_v[static_cast<std::size_t>(degree_v)] ||
      patch.v.max > knots_v[static_cast<std::size_t>(nv)]) {
    return false;
  }

  std::vector<Hom4> net = nurbs_surface_homogeneous_net(surface, nu, nv);
  if (!insert_tensor_surface_knot_to_full_multiplicity(
          net, nu, nv, knots_u, degree_u, patch.u.min, true) ||
      !insert_tensor_surface_knot_to_full_multiplicity(
          net, nu, nv, knots_u, degree_u, patch.u.max, true) ||
      !insert_tensor_surface_knot_to_full_multiplicity(
          net, nu, nv, knots_v, degree_v, patch.v.min, false) ||
      !insert_tensor_surface_knot_to_full_multiplicity(
          net, nu, nv, knots_v, degree_v, patch.v.max, false)) {
    return false;
  }
  const int begin_u =
      restricted_patch_control_begin(knots_u, patch.u.min, degree_u);
  const int begin_v =
      restricted_patch_control_begin(knots_v, patch.v.min, degree_v);
  if (begin_u < 0 || begin_v < 0 || begin_u + degree_u >= nu ||
      begin_v + degree_v >= nv) {
    return false;
  }

  for (int i = begin_u; i <= begin_u + degree_u; ++i) {
    for (int j = begin_v; j <= begin_v + degree_v; ++j) {
      const Hom4 &control =
          net[static_cast<std::size_t>(i * nv + j)];
      // The evaluator protects division below 1e-30 by clamping its
      // denominator.  Only issue a convex-hull certificate when the extracted
      // positive net proves that clamp cannot be reached anywhere in the patch.
      if (!(control.w > static_cast<Scalar>(1e-30)) || !finite_scalar(control.x) ||
          !finite_scalar(control.y) || !finite_scalar(control.z) ||
          !finite_scalar(control.w)) {
        return false;
      }
      const std::array<long double, 3> point{
          static_cast<long double>(control.x) / control.w,
          static_cast<long double>(control.y) / control.w,
          static_cast<long double>(control.z) / control.w};
      for (std::size_t axis = 0; axis < point.size(); ++axis) {
        if (!std::isfinite(point[axis])) {
          return false;
        }
        hull.minimum[axis] = std::min(hull.minimum[axis], point[axis]);
        hull.maximum[axis] = std::max(hull.maximum[axis], point[axis]);
      }
    }
  }
  return true;
}

bool tensor_surface_patch_hull(const detail::KernelState &state,
                               SurfaceId surface_id, const Range2D &patch,
                               TensorPatchHull &hull,
                               std::uint32_t depth = 0) {
  if (depth > 32) {
    return false;
  }
  const auto found = state.surfaces.find(surface_id.value);
  if (found == state.surfaces.end()) {
    return false;
  }
  const auto &surface = found->second;
  if (surface.kind == detail::SurfaceKind::Trimmed) {
    return tensor_surface_patch_hull(state, surface.base_surface_id, patch,
                                     hull, depth + 1);
  }
  if (surface.kind == detail::SurfaceKind::Offset) {
    hull.offset_padding +=
        std::abs(static_cast<long double>(surface.offset_distance));
    if (!std::isfinite(hull.offset_padding)) {
      return false;
    }
    return tensor_surface_patch_hull(state, surface.base_surface_id, patch,
                                     hull, depth + 1);
  }
  return tensor_surface_patch_hull(surface, patch, hull);
}

long double point_to_tensor_patch_hull_distance(const Point3 &query,
                                                const TensorPatchHull &hull) {
  const std::array<long double, 3> coordinates{
      static_cast<long double>(query.x), static_cast<long double>(query.y),
      static_cast<long double>(query.z)};
  long double squared = 0.0L;
  for (std::size_t axis = 0; axis < coordinates.size(); ++axis) {
    const long double lower = hull.minimum[axis] - hull.offset_padding;
    const long double upper = hull.maximum[axis] + hull.offset_padding;
    long double separation = 0.0L;
    if (coordinates[axis] < lower) {
      separation = lower - coordinates[axis];
    } else if (coordinates[axis] > upper) {
      separation = coordinates[axis] - upper;
    }
    squared += separation * separation;
  }
  return std::sqrt(squared);
}

Range2D surface_search_domain(const detail::KernelState &state,
                             const detail::SurfaceRecord &surface,
                             std::uint32_t depth = 0) {
  if (depth <= 32 && surface.kind == detail::SurfaceKind::Offset) {
    const auto base = state.surfaces.find(surface.base_surface_id.value);
    if (base != state.surfaces.end()) {
      return surface_search_domain(state, base->second, depth + 1);
    }
  }
  return surface_domain(surface);
}

const detail::SurfaceRecord *surface_search_planar_root(
    const detail::KernelState &state, SurfaceId surface_id,
    std::uint32_t depth = 0) {
  if (depth > 32) {
    return nullptr;
  }
  const auto found = state.surfaces.find(surface_id.value);
  if (found == state.surfaces.end()) {
    return nullptr;
  }
  const auto &surface = found->second;
  if (surface.kind == detail::SurfaceKind::Plane) {
    return &surface;
  }
  if (surface.kind == detail::SurfaceKind::Trimmed ||
      surface.kind == detail::SurfaceKind::Offset) {
    return surface_search_planar_root(state, surface.base_surface_id, depth + 1);
  }
  return nullptr;
}

void surface_search_project_uv(const detail::KernelState &state, SurfaceId surface_id,
                               const Range2D &domain, Scalar &u, Scalar &v,
                               std::uint32_t depth = 0) {
  u = std::clamp(u, domain.u.min, domain.u.max);
  v = std::clamp(v, domain.v.min, domain.v.max);
  if (depth > 32) {
    return;
  }
  const auto found = state.surfaces.find(surface_id.value);
  if (found == state.surfaces.end()) {
    return;
  }
  const auto &surface = found->second;
  if (surface.kind == detail::SurfaceKind::Trimmed) {
    apply_trimmed_surface_uv_in_parameter_space(surface, u, v);
    surface_search_project_uv(state, surface.base_surface_id, domain, u, v, depth + 1);
  } else if (surface.kind == detail::SurfaceKind::Offset) {
    surface_search_project_uv(state, surface.base_surface_id, domain, u, v, depth + 1);
  }
}

void append_surface_search_cuts(const detail::KernelState &state, SurfaceId surface_id,
                                const Range2D &domain, std::vector<Scalar> &u_cuts,
                                std::vector<Scalar> &v_cuts, std::uint32_t depth = 0) {
  if (depth > 32) {
    return;
  }
  const auto found = state.surfaces.find(surface_id.value);
  if (found == state.surfaces.end()) {
    return;
  }
  const auto &surface = found->second;
  if (surface.kind == detail::SurfaceKind::BSpline ||
      surface.kind == detail::SurfaceKind::Nurbs) {
    for (const Scalar knot : surface.knots_u) {
      if (knot > domain.u.min && knot < domain.u.max) {
        u_cuts.push_back(knot);
      }
    }
    for (const Scalar knot : surface.knots_v) {
      if (knot > domain.v.min && knot < domain.v.max) {
        v_cuts.push_back(knot);
      }
    }
  } else if (surface.kind == detail::SurfaceKind::Trimmed ||
             surface.kind == detail::SurfaceKind::Offset) {
    append_surface_search_cuts(state, surface.base_surface_id, domain, u_cuts, v_cuts,
                               depth + 1);
  }
}

bool uv_point_in_patch(const Point2 &point, Scalar u0, Scalar u1, Scalar v0,
                       Scalar v1) {
  return point.x >= u0 && point.x <= u1 && point.y >= v0 && point.y <= v1;
}

bool uv_loop_boundary_may_cross_patch(std::span<const Point2> loop, Scalar u0,
                                      Scalar u1, Scalar v0, Scalar v1) {
  for (std::size_t i = 0; i < loop.size(); ++i) {
    const auto &a = loop[i];
    const auto &b = loop[(i + 1) % loop.size()];
    if (std::max(a.x, b.x) >= u0 && std::min(a.x, b.x) <= u1 &&
        std::max(a.y, b.y) >= v0 && std::min(a.y, b.y) <= v1) {
      // Bounding-box overlap is deliberately conservative: false positives cost work,
      // while a false negative could discard the true closest trim-boundary point.
      return true;
    }
  }
  return false;
}

bool surface_search_patch_may_be_feasible(const detail::KernelState &state,
                                          SurfaceId surface_id, Scalar u0,
                                          Scalar u1, Scalar v0, Scalar v1,
                                          std::uint32_t depth = 0) {
  if (depth > 32) {
    return true;
  }
  const auto found = state.surfaces.find(surface_id.value);
  if (found == state.surfaces.end()) {
    return false;
  }
  const auto &surface = found->second;
  if (surface.kind == detail::SurfaceKind::Offset) {
    return surface_search_patch_may_be_feasible(
        state, surface.base_surface_id, u0, u1, v0, v1, depth + 1);
  }
  if (surface.kind != detail::SurfaceKind::Trimmed) {
    return true;
  }
  if (u1 < surface.trim_u_min || u0 > surface.trim_u_max ||
      v1 < surface.trim_v_min || v0 > surface.trim_v_max) {
    return false;
  }
  if (surface.trim_uv_loop.size() >= 3) {
    const std::array<Point2, 4> corners{{{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}};
    bool outer_overlap = false;
    for (const auto &corner : corners) {
      outer_overlap = outer_overlap ||
                      point_in_uv_polygon_raycast(corner, surface.trim_uv_loop);
    }
    for (const auto &vertex : surface.trim_uv_loop) {
      outer_overlap = outer_overlap || uv_point_in_patch(vertex, u0, u1, v0, v1);
    }
    outer_overlap = outer_overlap || uv_loop_boundary_may_cross_patch(
                                         surface.trim_uv_loop, u0, u1, v0, v1);
    if (!outer_overlap) {
      return false;
    }
    for (const auto &hole : surface.trim_uv_holes) {
      if (hole.size() < 3) {
        continue;
      }
      bool all_corners_in_hole = true;
      for (const auto &corner : corners) {
        all_corners_in_hole = all_corners_in_hole &&
                              point_in_uv_polygon_raycast(corner, hole);
      }
      bool contains_hole_vertex = false;
      for (const auto &vertex : hole) {
        contains_hole_vertex = contains_hole_vertex ||
                               uv_point_in_patch(vertex, u0, u1, v0, v1);
      }
      if (all_corners_in_hole && !contains_hole_vertex &&
          !uv_loop_boundary_may_cross_patch(hole, u0, u1, v0, v1)) {
        return false;
      }
    }
  }
  return surface_search_patch_may_be_feasible(
      state, surface.base_surface_id, u0, u1, v0, v1, depth + 1);
}

enum class AnalyticSurfaceClosestStatus : std::uint8_t {
  NotApplicable,
  Success,
  DegenerateOffset,
  NumericalFailure
};

struct AnalyticSurfaceClosestAttempt {
  AnalyticSurfaceClosestStatus status{AnalyticSurfaceClosestStatus::NotApplicable};
  SurfaceClosestPointResult result{};
};

struct AnalyticSurfaceStack {
  const detail::SurfaceRecord *root{};
  long double offset{};
  // Traversal starts at the queried wrapper.  Reverse iteration therefore
  // validates every materialized intermediate parallel surface from root out.
  std::vector<long double> offset_layers;
};

AnalyticSurfaceClosestStatus unwrap_analytic_surface_stack(
    const detail::KernelState &state, SurfaceId surface_id,
    AnalyticSurfaceStack &stack) {
  SurfaceId current = surface_id;
  for (std::uint32_t depth = 0; depth <= 32; ++depth) {
    const auto found = state.surfaces.find(current.value);
    if (found == state.surfaces.end()) {
      return AnalyticSurfaceClosestStatus::NumericalFailure;
    }
    const auto &surface = found->second;
    if (surface.kind == detail::SurfaceKind::Offset) {
      const long double layer = surface.offset_distance;
      stack.offset_layers.push_back(layer);
      stack.offset += layer;
      if (!std::isfinite(stack.offset)) {
        return AnalyticSurfaceClosestStatus::NumericalFailure;
      }
      current = surface.base_surface_id;
      continue;
    }
    switch (surface.kind) {
    case detail::SurfaceKind::Plane:
    case detail::SurfaceKind::Sphere:
    case detail::SurfaceKind::Cylinder:
    case detail::SurfaceKind::Cone:
    case detail::SurfaceKind::Torus:
      stack.root = &surface;
      return AnalyticSurfaceClosestStatus::Success;
    default:
      return AnalyticSurfaceClosestStatus::NotApplicable;
    }
  }
  return AnalyticSurfaceClosestStatus::NumericalFailure;
}

bool finite_long_double_point(long double x, long double y, long double z) {
  const long double limit = std::numeric_limits<Scalar>::max();
  return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
         std::abs(x) <= limit && std::abs(y) <= limit && std::abs(z) <= limit;
}

bool analytic_point_from_local(const Point3 &origin, const OrthoFrame &frame,
                               long double x, long double y, long double z,
                               Point3 &point) {
  const long double px = static_cast<long double>(origin.x) + frame.u.x * x +
                         frame.v.x * y + frame.w.x * z;
  const long double py = static_cast<long double>(origin.y) + frame.u.y * x +
                         frame.v.y * y + frame.w.y * z;
  const long double pz = static_cast<long double>(origin.z) + frame.u.z * x +
                         frame.v.z * y + frame.w.z * z;
  if (!finite_long_double_point(px, py, pz)) {
    return false;
  }
  point = Point3{static_cast<Scalar>(px), static_cast<Scalar>(py),
                 static_cast<Scalar>(pz)};
  return finite_point(point);
}

bool analytic_query_local(const Point3 &query, const Point3 &origin,
                          const OrthoFrame &frame, long double &x,
                          long double &y, long double &z) {
  const long double dx = static_cast<long double>(query.x) - origin.x;
  const long double dy = static_cast<long double>(query.y) - origin.y;
  const long double dz = static_cast<long double>(query.z) - origin.z;
  x = dx * frame.u.x + dy * frame.u.y + dz * frame.u.z;
  y = dx * frame.v.x + dy * frame.v.y + dz * frame.v.z;
  z = dx * frame.w.x + dy * frame.w.y + dz * frame.w.z;
  return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

Scalar canonical_surface_angle(long double y, long double x) {
  const long double two_pi = 2.0L * std::acos(-1.0L);
  long double angle = std::atan2(y, x);
  if (angle < 0.0L) {
    angle += two_pi;
  }
  if (angle >= two_pi) {
    angle = 0.0L;
  }
  return static_cast<Scalar>(angle);
}

bool finalize_analytic_surface_closest(const Point3 &query, Scalar u, Scalar v,
                                       const Point3 &point,
                                       const Range2D &effective_domain,
                                       bool finiteized,
                                       AnalyticSurfaceClosestAttempt &attempt) {
  const long double distance = closest_distance(point, query);
  if (!finite_scalar(u) || !finite_scalar(v) || !finite_point(point) ||
      !std::isfinite(distance) ||
      distance > std::numeric_limits<Scalar>::max()) {
    attempt.status = AnalyticSurfaceClosestStatus::NumericalFailure;
    return false;
  }
  auto &result = attempt.result;
  result.u = u;
  result.v = v;
  result.point = point;
  result.distance = static_cast<Scalar>(distance);
  result.distance_lower_bound = result.distance;
  result.evaluations = 0;
  result.patches_processed = 0;
  result.convergence = SurfaceClosestPointConvergence::Analytic;
  result.effective_domain = effective_domain;
  result.domain_was_finiteized = finiteized;
  attempt.status = AnalyticSurfaceClosestStatus::Success;
  return true;
}

AnalyticSurfaceClosestAttempt try_analytic_surface_closest(
    const detail::KernelState &state, SurfaceId surface_id,
    const Point3 &query) {
  AnalyticSurfaceClosestAttempt attempt;
  AnalyticSurfaceStack stack;
  attempt.status =
      unwrap_analytic_surface_stack(state, surface_id, stack);
  if (attempt.status != AnalyticSurfaceClosestStatus::Success ||
      stack.root == nullptr) {
    return attempt;
  }

  const auto &surface = *stack.root;
  const auto frame = make_frame_from_axis(surface.normal);
  long double qx = 0.0L;
  long double qy = 0.0L;
  long double qz = 0.0L;
  if (!analytic_query_local(query, surface.origin, frame, qx, qy, qz)) {
    attempt.status = AnalyticSurfaceClosestStatus::NumericalFailure;
    return attempt;
  }
  const long double pi = std::acos(-1.0L);
  const long double two_pi = 2.0L * pi;
  const Range1D periodic{0.0, static_cast<Scalar>(two_pi)};

  switch (surface.kind) {
  case detail::SurfaceKind::Plane: {
    const long double shifted_z = stack.offset;
    Point3 point{};
    if (!analytic_point_from_local(surface.origin, frame, qx, qy, shifted_z,
                                   point)) {
      attempt.status = AnalyticSurfaceClosestStatus::NumericalFailure;
      return attempt;
    }
    const Scalar u = static_cast<Scalar>(qx);
    const Scalar v = static_cast<Scalar>(qy);
    const Range2D witness{{u, u}, {v, v}};
    finalize_analytic_surface_closest(query, u, v, point, witness, true,
                                      attempt);
    return attempt;
  }
  case detail::SurfaceKind::Sphere: {
    const long double radius =
        static_cast<long double>(surface.radius_a) + stack.offset;
    long double cumulative_offset = 0.0L;
    for (auto layer = stack.offset_layers.rbegin();
         layer != stack.offset_layers.rend(); ++layer) {
      cumulative_offset += *layer;
      if (!(static_cast<long double>(surface.radius_a) + cumulative_offset >
            static_cast<long double>(kEpsilon))) {
        attempt.status = AnalyticSurfaceClosestStatus::DegenerateOffset;
        return attempt;
      }
    }
    const long double query_radius = std::hypot(qx, qy, qz);
    long double dx = 0.0L;
    long double dy = 0.0L;
    long double dz = 1.0L;
    if (query_radius > 0.0L) {
      dx = qx / query_radius;
      dy = qy / query_radius;
      dz = qz / query_radius;
    }
    Point3 point{};
    // Sphere evaluation is intentionally expressed in the world XYZ frame.
    const long double px = static_cast<long double>(surface.origin.x) + radius * dx;
    const long double py = static_cast<long double>(surface.origin.y) + radius * dy;
    const long double pz = static_cast<long double>(surface.origin.z) + radius * dz;
    if (!finite_long_double_point(px, py, pz)) {
      attempt.status = AnalyticSurfaceClosestStatus::NumericalFailure;
      return attempt;
    }
    point = {static_cast<Scalar>(px), static_cast<Scalar>(py),
             static_cast<Scalar>(pz)};
    const Scalar u = query_radius > 0.0L ? canonical_surface_angle(dy, dx) : 0.0;
    const Scalar v = query_radius > 0.0L
                         ? static_cast<Scalar>(std::acos(std::clamp(dz, -1.0L, 1.0L)))
                         : 0.0;
    const Range2D domain{periodic, {0.0, static_cast<Scalar>(pi)}};
    finalize_analytic_surface_closest(query, u, v, point, domain, false,
                                      attempt);
    return attempt;
  }
  case detail::SurfaceKind::Cylinder: {
    const long double radius =
        static_cast<long double>(surface.radius_a) + stack.offset;
    long double cumulative_offset = 0.0L;
    for (auto layer = stack.offset_layers.rbegin();
         layer != stack.offset_layers.rend(); ++layer) {
      cumulative_offset += *layer;
      if (!(static_cast<long double>(surface.radius_a) + cumulative_offset >
            static_cast<long double>(kEpsilon))) {
        attempt.status = AnalyticSurfaceClosestStatus::DegenerateOffset;
        return attempt;
      }
    }
    const long double planar = std::hypot(qx, qy);
    const long double cx = planar > 0.0L ? qx / planar : 1.0L;
    const long double cy = planar > 0.0L ? qy / planar : 0.0L;
    Point3 point{};
    if (!analytic_point_from_local(surface.origin, frame, radius * cx,
                                   radius * cy, qz, point)) {
      attempt.status = AnalyticSurfaceClosestStatus::NumericalFailure;
      return attempt;
    }
    const Scalar u = planar > 0.0L ? canonical_surface_angle(qy, qx) : 0.0;
    const Scalar v = static_cast<Scalar>(qz);
    const Range2D witness{periodic, {v, v}};
    finalize_analytic_surface_closest(query, u, v, point, witness, true,
                                      attempt);
    return attempt;
  }
  case detail::SurfaceKind::Cone: {
    const long double angle = surface.semi_angle;
    const long double slope = std::tan(angle);
    const long double sine = std::sin(angle);
    const long double cosine = std::cos(angle);
    if (!std::isfinite(slope) || !(slope > 0.0L) || !(cosine > 0.0L)) {
      attempt.status = AnalyticSurfaceClosestStatus::NumericalFailure;
      return attempt;
    }
    // A negative parallel offset crosses the axis before the original apex and
    // is self-intersecting/normal-singular over the declared v >= 0 domain.
    long double cumulative_offset = 0.0L;
    for (auto layer = stack.offset_layers.rbegin();
         layer != stack.offset_layers.rend(); ++layer) {
      cumulative_offset += *layer;
      if (cumulative_offset < 0.0L) {
        attempt.status = AnalyticSurfaceClosestStatus::DegenerateOffset;
        return attempt;
      }
    }
    const long double planar = std::hypot(qx, qy);
    const long double boundary_radius = stack.offset * cosine;
    const long double boundary_z = -stack.offset * sine;
    const long double numerator =
        slope * (planar - boundary_radius) + (qz - boundary_z);
    const long double axial = std::max(0.0L, numerator / (1.0L + slope * slope));
    const long double radial = boundary_radius + slope * axial;
    const long double z = boundary_z + axial;
    const long double cx = planar > 0.0L ? qx / planar : 1.0L;
    const long double cy = planar > 0.0L ? qy / planar : 0.0L;
    Point3 point{};
    if (!analytic_point_from_local(surface.origin, frame, radial * cx,
                                   radial * cy, z, point)) {
      attempt.status = AnalyticSurfaceClosestStatus::NumericalFailure;
      return attempt;
    }
    const Scalar u = planar > 0.0L ? canonical_surface_angle(qy, qx) : 0.0;
    const Scalar v = static_cast<Scalar>(axial);
    const Range2D witness{periodic, {0.0, v}};
    finalize_analytic_surface_closest(query, u, v, point, witness, true,
                                      attempt);
    return attempt;
  }
  case detail::SurfaceKind::Torus: {
    const long double major_radius = surface.radius_a;
    const long double minor_radius =
        static_cast<long double>(surface.radius_b) + stack.offset;
    // The closed-form meridian-circle certificate below is for a regular ring
    // torus. Spindle/horn tori remain on the bounded generic solver.
    const long double regularity_epsilon = static_cast<long double>(kEpsilon);
    const bool root_is_regular =
        static_cast<long double>(surface.radius_b) > regularity_epsilon &&
        major_radius > static_cast<long double>(surface.radius_b) +
                           regularity_epsilon;
    if (!root_is_regular) {
      attempt.status = AnalyticSurfaceClosestStatus::NotApplicable;
      return attempt;
    }
    long double cumulative_offset = 0.0L;
    for (auto layer = stack.offset_layers.rbegin();
         layer != stack.offset_layers.rend(); ++layer) {
      cumulative_offset += *layer;
      const long double intermediate_minor =
          static_cast<long double>(surface.radius_b) + cumulative_offset;
      if (!(intermediate_minor > regularity_epsilon) ||
          !(major_radius > intermediate_minor + regularity_epsilon)) {
        attempt.status = AnalyticSurfaceClosestStatus::DegenerateOffset;
        return attempt;
      }
    }
    const long double planar = std::hypot(qx, qy);
    const long double meridian_x = planar - major_radius;
    const long double meridian_length = std::hypot(meridian_x, qz);
    const long double mx = meridian_length > 0.0L
                               ? meridian_x / meridian_length
                               : 1.0L;
    const long double mz = meridian_length > 0.0L ? qz / meridian_length : 0.0L;
    const long double radial = major_radius + minor_radius * mx;
    const long double z = minor_radius * mz;
    const long double cx = planar > 0.0L ? qx / planar : 1.0L;
    const long double cy = planar > 0.0L ? qy / planar : 0.0L;
    Point3 point{};
    if (!analytic_point_from_local(surface.origin, frame, radial * cx,
                                   radial * cy, z, point)) {
      attempt.status = AnalyticSurfaceClosestStatus::NumericalFailure;
      return attempt;
    }
    const Scalar u = planar > 0.0L ? canonical_surface_angle(qy, qx) : 0.0;
    const Scalar v = meridian_length > 0.0L
                         ? canonical_surface_angle(qz, meridian_x)
                         : 0.0;
    const Range2D domain{periodic, periodic};
    finalize_analytic_surface_closest(query, u, v, point, domain, false,
                                      attempt);
    return attempt;
  }
  default:
    attempt.status = AnalyticSurfaceClosestStatus::NotApplicable;
    return attempt;
  }
}

} // namespace

Result<SurfaceClosestPointResult> SurfaceService::closest_point_detailed(
    SurfaceId surface_id, const Point3 &query,
    const SurfaceClosestPointOptions &options) const {
  if (!finite_point(query) || !finite_scalar(options.distance_tolerance) ||
      !finite_scalar(options.parameter_tolerance) ||
      options.distance_tolerance < 0.0 || options.parameter_tolerance <= 0.0 ||
      options.max_evaluations < 5) {
    return detail::invalid_input_result<SurfaceClosestPointResult>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲面全域最近点求解失败：输入点或精度/预算选项非法",
        "曲面全域最近点求解失败");
  }
  const auto found = state_->surfaces.find(surface_id.value);
  if (found == state_->surfaces.end()) {
    return detail::failed_result<SurfaceClosestPointResult>(
        *state_, StatusCode::InvalidInput, diag_codes::kGeoParameterSolveFailure,
        "曲面全域最近点求解失败：目标曲面不存在", "曲面全域最近点求解失败");
  }
  const auto analytic =
      try_analytic_surface_closest(*state_, surface_id, query);
  if (analytic.status == AnalyticSurfaceClosestStatus::Success) {
    return ok_result(analytic.result,
                     state_->create_diagnostic(
                         analytic.result.domain_was_finiteized
                             ? "曲面全域最近点已解析求解并自动有限化"
                             : "曲面全域最近点已解析求解"));
  }
  if (analytic.status == AnalyticSurfaceClosestStatus::DegenerateOffset) {
    return detail::failed_result<SurfaceClosestPointResult>(
        *state_, StatusCode::DegenerateGeometry,
        diag_codes::kGeoOffsetSelfIntersection,
        "曲面全域最近点求解失败：解析偏置链半径坍缩或在完整参数域内自交",
        "曲面全域最近点求解失败");
  }
  if (analytic.status == AnalyticSurfaceClosestStatus::NumericalFailure) {
    return detail::failed_result<SurfaceClosestPointResult>(
        *state_, StatusCode::NumericalInstability,
        diag_codes::kGeoParameterSolveFailure,
        "曲面全域最近点求解失败：解析有限化参数或结果超出可表示范围",
        "曲面全域最近点求解失败");
  }
  const Range2D domain = surface_search_domain(*state_, found->second);
  if (!finite_scalar(domain.u.min) || !finite_scalar(domain.u.max) ||
      !finite_scalar(domain.v.min) || !finite_scalar(domain.v.max) ||
      !(domain.u.max > domain.u.min) || !(domain.v.max > domain.v.min)) {
    return detail::failed_result<SurfaceClosestPointResult>(
        *state_, StatusCode::InvalidInput, diag_codes::kGeoParameterSolveFailure,
        "曲面全域最近点求解失败：参数域无界或为空，请先建立有限修剪域",
        "曲面全域最近点求解失败");
  }

  // A finite trim/offset stack over a plane is an isometric UV mapping.  Its
  // projected UV is therefore the exact global closest point, including outer
  // polygon and hole boundaries, and needs no branch-and-bound budget.
  if (const auto *plane = surface_search_planar_root(*state_, surface_id);
      plane != nullptr) {
    const auto frame = make_frame_from_axis(plane->normal);
    const auto local = to_local(detail::subtract(query, plane->origin), frame);
    Scalar u = local[0];
    Scalar v = local[1];
    surface_search_project_uv(*state_, surface_id, domain, u, v);
    Point3 point{};
    Vec3 du{};
    Vec3 dv{};
    if (!surface_search_frame(*state_, surface_id, u, v, point, du, dv)) {
      return detail::failed_result<SurfaceClosestPointResult>(
          *state_, StatusCode::OperationFailed,
          diag_codes::kGeoParameterSolveFailure,
          "曲面全域最近点求解失败：平面投影点不可用",
          "曲面全域最近点求解失败");
    }
    const Scalar distance = static_cast<Scalar>(closest_distance(point, query));
    SurfaceClosestPointResult result;
    result.u = u;
    result.v = v;
    result.point = point;
    result.distance = distance;
    result.distance_lower_bound = distance;
    result.evaluations = 1;
    result.convergence = SurfaceClosestPointConvergence::DistanceTolerance;
    result.effective_domain = domain;
    return ok_result(result,
                     state_->create_diagnostic("曲面全域最近点已解析求解"));
  }

  // A planar tensor surface can also be certified by its supporting plane.
  // Recover the UV with bounded, cache-free local solves; once the candidate
  // reaches the plane projection, the plane distance is a global lower bound.
  // Keep very small budgets on the generic path so their exhaustion contract
  // remains observable and deterministic.
  std::uint32_t preliminary_evaluations = 0;
  std::vector<Point3> planar_control_points;
  if (found->second.kind == detail::SurfaceKind::Bezier ||
      found->second.kind == detail::SurfaceKind::BSpline ||
      found->second.kind == detail::SurfaceKind::Nurbs) {
    planar_control_points = found->second.poles;
  } else if (found->second.kind == detail::SurfaceKind::Swept) {
    const auto profile = state_->curves.find(found->second.profile_curve_id.value);
    if (profile != state_->curves.end() &&
        profile->second.kind == detail::CurveKind::LineSegment &&
        profile->second.poles.size() == 2) {
      const Vec3 sweep = detail::scale(
          normalize_or_default(found->second.axis, Vec3{0.0, 0.0, 1.0}),
          found->second.sweep_length);
      planar_control_points = {
          profile->second.poles[0], profile->second.poles[1],
          detail::add_point_vec(profile->second.poles[0], sweep),
          detail::add_point_vec(profile->second.poles[1], sweep)};
    }
  }
  if (!planar_control_points.empty() && options.max_evaluations >= 64) {
    const Point3 origin = planar_control_points.front();
    long double pole_scale = 1.0L;
    for (const auto &pole : planar_control_points) {
      pole_scale = std::max(pole_scale, closest_distance(pole, origin));
    }
    Vec3 plane_normal{};
    bool have_plane = false;
    for (std::size_t i = 1; i < planar_control_points.size() && !have_plane; ++i) {
      const Vec3 a = detail::subtract(planar_control_points[i], origin);
      for (std::size_t j = i + 1; j < planar_control_points.size(); ++j) {
        const Vec3 b = detail::subtract(planar_control_points[j], origin);
        const Vec3 cross = detail::cross(a, b);
        if (detail::norm(cross) > static_cast<Scalar>(pole_scale * 1e-12L)) {
          plane_normal = detail::normalize(cross);
          have_plane = true;
          break;
        }
      }
    }
    bool coplanar = have_plane;
    if (coplanar) {
      for (const auto &pole : planar_control_points) {
        if (std::abs(static_cast<long double>(detail::dot(
                plane_normal, detail::subtract(pole, origin)))) >
            pole_scale * 1e-10L) {
          coplanar = false;
          break;
        }
      }
    }
    const bool constant = std::all_of(
        planar_control_points.begin(), planar_control_points.end(), [&](const Point3 &pole) {
          return closest_distance(pole, origin) <= pole_scale * 1e-14L;
        });
    if (constant) {
      SurfaceClosestPointResult result;
      result.u = domain.u.min;
      result.v = domain.v.min;
      result.point = origin;
      result.distance = static_cast<Scalar>(closest_distance(origin, query));
      result.distance_lower_bound = result.distance;
      result.evaluations = 1;
      result.convergence = SurfaceClosestPointConvergence::DistanceTolerance;
      result.effective_domain = domain;
      return ok_result(result,
                       state_->create_diagnostic("曲面全域最近点已解析求解"));
    }
    if (coplanar) {
      std::uint32_t planar_evaluations = 0;
      Scalar candidate_u = domain.u.min;
      Scalar candidate_v = domain.v.min;
      Point3 candidate_point{};
      long double candidate_distance = std::numeric_limits<long double>::infinity();
      const auto consider = [&](Scalar u, Scalar v) {
        if (planar_evaluations >= options.max_evaluations) {
          return;
        }
        Point3 point{};
        Vec3 du{};
        Vec3 dv{};
        if (!surface_search_frame(*state_, surface_id, u, v, point, du, dv)) {
          return;
        }
        ++planar_evaluations;
        const long double distance = closest_distance(point, query);
        if (distance < candidate_distance) {
          candidate_distance = distance;
          candidate_u = u;
          candidate_v = v;
          candidate_point = point;
        }
      };
      constexpr int grid = 7;
      for (int iu = 0; iu < grid; ++iu) {
        const Scalar u = domain.u.min + (domain.u.max - domain.u.min) *
                                          static_cast<Scalar>(iu) / (grid - 1);
        for (int iv = 0; iv < grid; ++iv) {
          const Scalar v = domain.v.min + (domain.v.max - domain.v.min) *
                                            static_cast<Scalar>(iv) / (grid - 1);
          consider(u, v);
        }
      }
      std::vector<Scalar> u_cuts{domain.u.min, domain.u.max};
      std::vector<Scalar> v_cuts{domain.v.min, domain.v.max};
      append_surface_search_cuts(*state_, surface_id, domain, u_cuts, v_cuts);
      std::sort(u_cuts.begin(), u_cuts.end());
      u_cuts.erase(std::unique(u_cuts.begin(), u_cuts.end()), u_cuts.end());
      std::sort(v_cuts.begin(), v_cuts.end());
      v_cuts.erase(std::unique(v_cuts.begin(), v_cuts.end()), v_cuts.end());
      for (std::size_t iu = 0; iu + 1 < u_cuts.size(); ++iu) {
        for (std::size_t iv = 0; iv + 1 < v_cuts.size(); ++iv) {
          if (u_cuts[iu + 1] > u_cuts[iu] && v_cuts[iv + 1] > v_cuts[iv]) {
            consider(u_cuts[iu] + (u_cuts[iu + 1] - u_cuts[iu]) * 0.5,
                     v_cuts[iv] + (v_cuts[iv + 1] - v_cuts[iv]) * 0.5);
          }
        }
      }
      Scalar lambda = 1e-6;
      for (int iteration = 0;
           iteration < 22 && planar_evaluations + 1 < options.max_evaluations;
           ++iteration) {
        Point3 point{};
        Vec3 du{};
        Vec3 dv{};
        if (!surface_search_frame(*state_, surface_id, candidate_u, candidate_v,
                                  point, du, dv)) {
          break;
        }
        ++planar_evaluations;
        const Vec3 error = detail::subtract(point, query);
        const Scalar j00 = detail::dot(du, du);
        const Scalar j01 = detail::dot(du, dv);
        const Scalar j11 = detail::dot(dv, dv);
        const Scalar g0 = detail::dot(du, error);
        const Scalar g1 = detail::dot(dv, error);
        Scalar u_step = 0.0;
        Scalar v_step = 0.0;
        const Scalar damping = lambda * std::max<Scalar>(1.0, j00 + j11);
        if (!solve_2x2(j00 + damping, j01, j01, j11 + damping, -g0, -g1,
                       u_step, v_step)) {
          break;
        }
        const Scalar next_u = std::clamp(candidate_u + u_step, domain.u.min,
                                         domain.u.max);
        const Scalar next_v = std::clamp(candidate_v + v_step, domain.v.min,
                                         domain.v.max);
        Point3 next_point{};
        Vec3 next_du{};
        Vec3 next_dv{};
        if (!surface_search_frame(*state_, surface_id, next_u, next_v, next_point,
                                  next_du, next_dv)) {
          break;
        }
        ++planar_evaluations;
        const long double next_distance = closest_distance(next_point, query);
        if (next_distance > candidate_distance) {
          lambda *= 10.0;
          continue;
        }
        candidate_u = next_u;
        candidate_v = next_v;
        candidate_point = next_point;
        candidate_distance = next_distance;
        lambda = std::max<Scalar>(1e-12, lambda * 0.3);
        if (std::abs(u_step) + std::abs(v_step) < 1e-12) {
          break;
        }
      }
      const long double plane_distance = std::abs(static_cast<long double>(
          detail::dot(plane_normal, detail::subtract(query, origin))));
      const long double certificate_tolerance = std::max(
          static_cast<long double>(options.distance_tolerance),
          1e-10L * std::max(1.0L, candidate_distance));
      if (std::isfinite(candidate_distance) &&
          candidate_distance - plane_distance <= certificate_tolerance) {
        SurfaceClosestPointResult result;
        result.u = candidate_u;
        result.v = candidate_v;
        result.point = candidate_point;
        result.distance = static_cast<Scalar>(candidate_distance);
        result.distance_lower_bound = static_cast<Scalar>(plane_distance);
        result.evaluations = planar_evaluations;
        result.patches_processed = 1;
        result.convergence = SurfaceClosestPointConvergence::DistanceTolerance;
        result.effective_domain = domain;
        return ok_result(result,
                         state_->create_diagnostic("曲面全域最近点已由平面界认证"));
      }
      preliminary_evaluations = planar_evaluations;
    }
  }
  SurfaceSearchBounds bounds{};
  if (!surface_search_bounds(*state_, surface_id, domain, bounds) ||
      !std::isfinite(bounds.du) || !std::isfinite(bounds.dv) ||
      !std::isfinite(bounds.fixed_radius) || bounds.du < 0.0L || bounds.dv < 0.0L) {
    return detail::failed_result<SurfaceClosestPointResult>(
        *state_, StatusCode::NumericalInstability, diag_codes::kGeoParameterSolveFailure,
        "曲面全域最近点求解失败：无法建立有限的全域变化界",
        "曲面全域最近点求解失败");
  }

  struct Patch {
    Scalar u0{};
    Scalar u1{};
    Scalar v0{};
    Scalar v1{};
    long double lower{};
  };
  struct LowerFirst {
    bool operator()(const Patch &lhs, const Patch &rhs) const {
      if (lhs.lower != rhs.lower) {
        return lhs.lower > rhs.lower;
      }
      if (lhs.u0 != rhs.u0) {
        return lhs.u0 > rhs.u0;
      }
      return lhs.v0 > rhs.v0;
    }
  };
  std::priority_queue<Patch, std::vector<Patch>, LowerFirst> pending;
  std::uint32_t evaluations = preliminary_evaluations;
  std::uint32_t patches_processed = 0;
  std::uint32_t control_net_bound_patches = 0;
  std::uint32_t pruned_patches = 0;
  long double best_distance = std::numeric_limits<long double>::infinity();
  Scalar best_u = domain.u.min;
  Scalar best_v = domain.v.min;
  Point3 best_point{};

  const auto evaluate = [&](Scalar u, Scalar v, bool project_to_trim,
                            Point3 &point, long double &distance) {
    if (evaluations >= options.max_evaluations) {
      return false;
    }
    if (project_to_trim) {
      surface_search_project_uv(*state_, surface_id, domain, u, v);
    }
    Vec3 du{};
    Vec3 dv{};
    if (!surface_search_frame(*state_, surface_id, u, v, point, du, dv)) {
      return false;
    }
    ++evaluations;
    distance = closest_distance(point, query);
    if (!std::isfinite(distance)) {
      return false;
    }
    if (project_to_trim &&
        (distance < best_distance ||
         (distance == best_distance &&
          (u < best_u || (u == best_u && v < best_v))))) {
      best_distance = distance;
      best_u = u;
      best_v = v;
      best_point = point;
    }
    return true;
  };

  const auto enqueue = [&](Scalar u0, Scalar u1, Scalar v0, Scalar v1) {
    if (!surface_search_patch_may_be_feasible(*state_, surface_id, u0, u1, v0,
                                              v1)) {
      return true;
    }
    const Scalar uc = u0 + (u1 - u0) * 0.5;
    const Scalar vc = v0 + (v1 - v0) * 0.5;
    Point3 center{};
    long double center_distance = 0.0L;
    if (!evaluate(uc, vc, false, center, center_distance)) {
      return false;
    }
    Scalar candidate_u = uc;
    Scalar candidate_v = vc;
    surface_search_project_uv(*state_, surface_id, domain, candidate_u, candidate_v);
    if (candidate_u == uc && candidate_v == vc) {
      if (center_distance < best_distance ||
          (center_distance == best_distance &&
           (uc < best_u || (uc == best_u && vc < best_v)))) {
        best_distance = center_distance;
        best_u = uc;
        best_v = vc;
        best_point = center;
      }
    } else {
      Point3 candidate{};
      long double candidate_distance = 0.0L;
      if (!evaluate(candidate_u, candidate_v, true, candidate, candidate_distance)) {
        return false;
      }
    }
    SurfaceSearchBounds patch_bounds = bounds;
    const Range2D patch_domain{{u0, u1}, {v0, v1}};
    tighten_tensor_patch_bounds(*state_, surface_id, patch_domain, patch_bounds);
    const long double radius =
        patch_bounds.du * (static_cast<long double>(u1) - u0) * 0.5L +
        patch_bounds.dv * (static_cast<long double>(v1) - v0) * 0.5L +
        patch_bounds.fixed_radius;
    long double lower = std::max(0.0L, center_distance - radius);
    TensorPatchHull hull;
    const bool has_control_net_bound = tensor_surface_patch_hull(
        *state_, surface_id, patch_domain, hull);
    if (has_control_net_bound) {
      const long double hull_lower =
          point_to_tensor_patch_hull_distance(query, hull);
      if (!std::isfinite(hull_lower)) {
        return false;
      }
      lower = std::max(lower, hull_lower);
      ++control_net_bound_patches;
    }
    pending.push(Patch{u0, u1, v0, v1, lower});
    return true;
  };

  std::vector<Scalar> u_cuts{domain.u.min, domain.u.max};
  std::vector<Scalar> v_cuts{domain.v.min, domain.v.max};
  append_surface_search_cuts(*state_, surface_id, domain, u_cuts, v_cuts);
  const auto normalize_cuts = [](std::vector<Scalar> &cuts) {
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
  };
  normalize_cuts(u_cuts);
  normalize_cuts(v_cuts);
  for (std::size_t iu = 0; iu + 1 < u_cuts.size(); ++iu) {
    if (!(u_cuts[iu + 1] > u_cuts[iu])) {
      continue;
    }
    for (std::size_t iv = 0; iv + 1 < v_cuts.size(); ++iv) {
      if (v_cuts[iv + 1] > v_cuts[iv] &&
          !enqueue(u_cuts[iu], u_cuts[iu + 1], v_cuts[iv], v_cuts[iv + 1])) {
        return detail::failed_result<SurfaceClosestPointResult>(
            *state_, StatusCode::OperationFailed, diag_codes::kGeoParameterSolveFailure,
            "曲面全域最近点求解失败：初始结点片覆盖超过预算或点值不可用",
            "曲面全域最近点求解失败");
      }
    }
  }
  if (pending.empty() || !std::isfinite(best_distance)) {
    return detail::failed_result<SurfaceClosestPointResult>(
        *state_, StatusCode::DegenerateGeometry, diag_codes::kGeoParameterSolveFailure,
        "曲面全域最近点求解失败：有效参数片为空", "曲面全域最近点求解失败");
  }

  long double terminal_lower = std::numeric_limits<long double>::infinity();
  long double max_terminal_u = 0.0L;
  long double max_terminal_v = 0.0L;
  auto convergence = SurfaceClosestPointConvergence::ParameterTolerance;
  while (!pending.empty()) {
    const long double global_lower = std::min(terminal_lower, pending.top().lower);
    if (best_distance - global_lower <=
        static_cast<long double>(options.distance_tolerance)) {
      convergence = SurfaceClosestPointConvergence::DistanceTolerance;
      break;
    }
    const Patch current = pending.top();
    pending.pop();
    ++patches_processed;
    if (current.lower >= best_distance) {
      ++pruned_patches;
      continue;
    }
    const long double width_u = static_cast<long double>(current.u1) - current.u0;
    const long double width_v = static_cast<long double>(current.v1) - current.v0;
    if (width_u <= options.parameter_tolerance &&
        width_v <= options.parameter_tolerance) {
      terminal_lower = std::min(terminal_lower, current.lower);
      max_terminal_u = std::max(max_terminal_u, width_u);
      max_terminal_v = std::max(max_terminal_v, width_v);
      continue;
    }
    bool ok = true;
    if (width_u / options.parameter_tolerance >=
        width_v / options.parameter_tolerance) {
      const Scalar middle = current.u0 + (current.u1 - current.u0) * 0.5;
      ok = enqueue(current.u0, middle, current.v0, current.v1) &&
           enqueue(middle, current.u1, current.v0, current.v1);
    } else {
      const Scalar middle = current.v0 + (current.v1 - current.v0) * 0.5;
      ok = enqueue(current.u0, current.u1, current.v0, middle) &&
           enqueue(current.u0, current.u1, middle, current.v1);
    }
    if (!ok) {
      return detail::failed_result<SurfaceClosestPointResult>(
          *state_, StatusCode::OperationFailed, diag_codes::kGeoParameterSolveFailure,
          "曲面全域最近点求解失败：点值求值预算耗尽或细分点不可用",
          "曲面全域最近点求解失败");
    }
  }

  // Branch-and-bound provides the global certificate, while its terminal cell
  // centre is only parameter-tolerance accurate.  Finish from the winning cell
  // with a small damped Gauss-Newton solve so narrow/high-speed knot spans keep
  // the accuracy historically provided by closest_uv/closest_point.  Every
  // trial remains projected into the finite/trimmed domain and consumes the
  // same explicit evaluation budget.
  Scalar refine_u = best_u;
  Scalar refine_v = best_v;
  Scalar lambda = 1e-6;
  for (int iteration = 0; iteration < 22 && evaluations < options.max_evaluations;
       ++iteration) {
    Point3 current_point{};
    Vec3 current_du{};
    Vec3 current_dv{};
    if (!surface_search_frame(*state_, surface_id, refine_u, refine_v,
                              current_point, current_du, current_dv)) {
      break;
    }
    ++evaluations;
    const Vec3 error = detail::subtract(current_point, query);
    const Scalar j00 = detail::dot(current_du, current_du);
    const Scalar j01 = detail::dot(current_du, current_dv);
    const Scalar j11 = detail::dot(current_dv, current_dv);
    const Scalar g0 = detail::dot(current_du, error);
    const Scalar g1 = detail::dot(current_dv, error);
    bool accepted = false;
    for (int attempt = 0; attempt < 6 && evaluations < options.max_evaluations;
         ++attempt) {
      Scalar u_step = 0.0;
      Scalar v_step = 0.0;
      const Scalar scale = std::max<Scalar>(1.0, j00 + j11);
      const Scalar damping = lambda * scale;
      if (!solve_2x2(j00 + damping, j01, j01, j11 + damping, -g0, -g1,
                     u_step, v_step)) {
        lambda *= 10.0;
        continue;
      }
      Scalar candidate_u = refine_u + u_step;
      Scalar candidate_v = refine_v + v_step;
      surface_search_project_uv(*state_, surface_id, domain, candidate_u,
                                candidate_v);
      Point3 candidate_point{};
      Vec3 candidate_du{};
      Vec3 candidate_dv{};
      if (!surface_search_frame(*state_, surface_id, candidate_u, candidate_v,
                                candidate_point, candidate_du, candidate_dv)) {
        lambda *= 10.0;
        continue;
      }
      ++evaluations;
      const long double candidate_distance =
          closest_distance(candidate_point, query);
      if (std::isfinite(candidate_distance) &&
          candidate_distance <= best_distance) {
        best_distance = candidate_distance;
        best_u = refine_u = candidate_u;
        best_v = refine_v = candidate_v;
        best_point = candidate_point;
        lambda = std::max<Scalar>(1e-12, lambda * 0.3);
        accepted = true;
        if (std::abs(u_step) + std::abs(v_step) < 1e-12) {
          iteration = 22;
        }
        break;
      }
      lambda *= 10.0;
    }
    if (!accepted) {
      break;
    }
  }

  long double global_lower = terminal_lower;
  if (!pending.empty()) {
    global_lower = std::min(global_lower, pending.top().lower);
  }
  if (!std::isfinite(global_lower)) {
    global_lower = best_distance;
    convergence = SurfaceClosestPointConvergence::DistanceTolerance;
  }
  SurfaceClosestPointResult result;
  result.u = best_u;
  result.v = best_v;
  result.point = best_point;
  result.distance = static_cast<Scalar>(best_distance);
  result.distance_lower_bound =
      static_cast<Scalar>(std::clamp(global_lower, 0.0L, best_distance));
  result.evaluations = evaluations;
  result.patches_processed = patches_processed;
  result.control_net_bound_patches = control_net_bound_patches;
  result.pruned_patches = pruned_patches;
  result.convergence = convergence;
  result.effective_domain = domain;
  if (convergence == SurfaceClosestPointConvergence::ParameterTolerance) {
    result.u_uncertainty = static_cast<Scalar>(max_terminal_u);
    result.v_uncertainty = static_cast<Scalar>(max_terminal_v);
  }
  return ok_result(result,
                   state_->create_diagnostic("曲面全域最近点已按请求精度收敛"));
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
      surface.kind == detail::SurfaceKind::Nurbs ||
      surface.kind == detail::SurfaceKind::Revolved ||
      surface.kind == detail::SurfaceKind::Swept ||
      surface.kind == detail::SurfaceKind::Trimmed ||
      surface.kind == detail::SurfaceKind::Offset) {
    const auto detailed = closest_point_detailed(surface_id, point);
    if (detailed.status != StatusCode::Ok || !detailed.value.has_value()) {
      return error_result<Point3>(detailed.status, detailed.diagnostic_id);
    }
    return ok_result(detailed.value->point,
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
  case detail::SurfaceKind::Trimmed:
  case detail::SurfaceKind::Revolved:
  case detail::SurfaceKind::Swept:
  case detail::SurfaceKind::Offset: {
    const auto detailed = closest_point_detailed(surface_id, point);
    if (detailed.status != StatusCode::Ok || !detailed.value.has_value()) {
      return error_result<std::pair<Scalar, Scalar>>(detailed.status,
                                                     detailed.diagnostic_id);
    }
    return ok_result(std::make_pair(detailed.value->u, detailed.value->v),
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
