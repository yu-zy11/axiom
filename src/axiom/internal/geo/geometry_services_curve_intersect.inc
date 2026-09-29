namespace {

struct CurveIntersectionContext {
  const detail::KernelState &state;
  const CurveCurveIntersectionOptions &options;
  std::uint32_t evaluations{0};
  std::uint32_t rectangles_processed{0};
  bool evaluation_budget_exhausted{false};
  bool numerical_failure{false};
};

struct CurveIntersectionArc {
  CurveId curve{};
  Scalar begin{};
  Scalar end{};
  Point3 begin_point{};
  Point3 middle_point{};
  Point3 end_point{};
  BoundingBox bounds{};
  long double chord_deviation{};
};

struct CurveIntersectionRectangle {
  CurveIntersectionArc first;
  CurveIntersectionArc second;
};

struct LinearCurveSpan {
  Scalar begin{};
  Scalar end{};
  Point3 begin_point{};
  Point3 end_point{};
};

long double intersection_point_distance(const Point3 &lhs,
                                        const Point3 &rhs) {
  return std::hypot(static_cast<long double>(lhs.x) - rhs.x,
                    static_cast<long double>(lhs.y) - rhs.y,
                    static_cast<long double>(lhs.z) - rhs.z);
}

Point3 intersection_midpoint(const Point3 &lhs, const Point3 &rhs) {
  return Point3{lhs.x + (rhs.x - lhs.x) * 0.5,
                lhs.y + (rhs.y - lhs.y) * 0.5,
                lhs.z + (rhs.z - lhs.z) * 0.5};
}

long double intersection_dot(const Vec3 &lhs, const Vec3 &rhs) {
  return static_cast<long double>(lhs.x) * rhs.x +
         static_cast<long double>(lhs.y) * rhs.y +
         static_cast<long double>(lhs.z) * rhs.z;
}

long double intersection_length(const Vec3 &value) {
  return std::hypot(static_cast<long double>(value.x), value.y, value.z);
}

Vec3 intersection_vector(const Point3 &from, const Point3 &to) {
  return Vec3{to.x - from.x, to.y - from.y, to.z - from.z};
}

Point3 intersection_lerp(const Point3 &first, const Point3 &second,
                         long double parameter) {
  return Point3{
      static_cast<Scalar>(static_cast<long double>(first.x) +
                          (static_cast<long double>(second.x) - first.x) * parameter),
      static_cast<Scalar>(static_cast<long double>(first.y) +
                          (static_cast<long double>(second.y) - first.y) * parameter),
      static_cast<Scalar>(static_cast<long double>(first.z) +
                          (static_cast<long double>(second.z) - first.z) * parameter)};
}

Scalar intersection_parameter_lerp(Scalar first, Scalar second,
                                   long double parameter) {
  return static_cast<Scalar>(static_cast<long double>(first) +
                             (static_cast<long double>(second) - first) * parameter);
}

bool intersection_scalar_equal(Scalar lhs, Scalar rhs) {
  return lhs == rhs;
}

bool intersection_point_equal(const Point3 &lhs, const Point3 &rhs) {
  return intersection_scalar_equal(lhs.x, rhs.x) &&
         intersection_scalar_equal(lhs.y, rhs.y) &&
         intersection_scalar_equal(lhs.z, rhs.z);
}

bool intersection_vector_equal(const Vec3 &lhs, const Vec3 &rhs) {
  return intersection_scalar_equal(lhs.x, rhs.x) &&
         intersection_scalar_equal(lhs.y, rhs.y) &&
         intersection_scalar_equal(lhs.z, rhs.z);
}

bool intersection_points_equal(const std::vector<Point3> &lhs,
                               const std::vector<Point3> &rhs) {
  if (lhs.size() != rhs.size()) return false;
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (!intersection_point_equal(lhs[index], rhs[index])) return false;
  }
  return true;
}

bool intersection_scalars_equal(const std::vector<Scalar> &lhs,
                                const std::vector<Scalar> &rhs) {
  return lhs == rhs;
}

bool intersection_curve_point_at_side(const detail::KernelState &state,
                                      CurveId curve_id, Scalar parameter,
                                      bool left_limit, Point3 &point,
                                      std::uint32_t depth = 0) {
  if (depth > 64) return false;
  const auto found = state.curves.find(curve_id.value);
  if (found == state.curves.end()) return false;
  const auto &curve = found->second;
  if (curve.kind != detail::CurveKind::CompositeChain)
    return closest_search_point(state, curve_id, parameter, point, depth);
  if (curve.children.empty()) return false;

  const Scalar count = static_cast<Scalar>(curve.children.size());
  const Scalar clamped = std::clamp(parameter, 0.0, count);
  std::size_t child_index = 0;
  Scalar local_parameter = 0.0;
  if (left_limit && clamped > 0.0) {
    child_index = std::min<std::size_t>(
        static_cast<std::size_t>(std::ceil(clamped) - 1.0),
        curve.children.size() - 1);
    local_parameter = clamped - static_cast<Scalar>(child_index);
  } else {
    const Scalar mapped = std::min(clamped, std::nextafter(count, 0.0));
    child_index = std::min<std::size_t>(
        static_cast<std::size_t>(std::max<Scalar>(0.0, std::floor(mapped))),
        curve.children.size() - 1);
    local_parameter = mapped - static_cast<Scalar>(child_index);
  }
  return intersection_curve_point_at_side(
      state, curve.children[child_index], local_parameter, left_limit, point,
      depth + 1);
}

bool same_curve_parameterization(const detail::KernelState &state,
                                 CurveId first, CurveId second,
                                 std::uint32_t depth = 0) {
  if (first.value == second.value) return true;
  if (depth > 64) return false;
  const auto first_it = state.curves.find(first.value);
  const auto second_it = state.curves.find(second.value);
  if (first_it == state.curves.end() || second_it == state.curves.end())
    return false;
  const auto &lhs = first_it->second;
  const auto &rhs = second_it->second;
  if (lhs.kind != rhs.kind ||
      !intersection_point_equal(lhs.origin, rhs.origin) ||
      !intersection_vector_equal(lhs.direction, rhs.direction) ||
      lhs.radius != rhs.radius || lhs.param_a != rhs.param_a ||
      lhs.param_b != rhs.param_b ||
      !intersection_vector_equal(lhs.normal, rhs.normal) ||
      !intersection_vector_equal(lhs.axis_u, rhs.axis_u) ||
      !intersection_vector_equal(lhs.axis_v, rhs.axis_v) ||
      !intersection_points_equal(lhs.poles, rhs.poles) ||
      !intersection_scalars_equal(lhs.weights, rhs.weights) ||
      !intersection_scalars_equal(lhs.knots_u, rhs.knots_u) ||
      lhs.children.size() != rhs.children.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.children.size(); ++index) {
    if (!same_curve_parameterization(state, lhs.children[index],
                                     rhs.children[index], depth + 1)) {
      return false;
    }
  }
  return true;
}

bool intersection_evaluate(CurveIntersectionContext &context, CurveId curve,
                           Scalar parameter, Point3 &point,
                           bool left_limit = false) {
  if (context.evaluations >= context.options.max_evaluations) {
    context.evaluation_budget_exhausted = true;
    return false;
  }
  ++context.evaluations;
  if (!intersection_curve_point_at_side(context.state, curve, parameter,
                                        left_limit, point)) {
    context.numerical_failure = true;
    return false;
  }
  return true;
}

BoundingBox intersection_bounds_from_samples(
    const Point3 &first, const Point3 &middle, const Point3 &last,
    long double padding) {
  const long double max_scalar = std::numeric_limits<Scalar>::max();
  const auto coordinate_min = [&](Scalar a, Scalar b, Scalar c) {
    return std::max(-max_scalar,
                    std::min({static_cast<long double>(a),
                              static_cast<long double>(b),
                              static_cast<long double>(c)}) - padding);
  };
  const auto coordinate_max = [&](Scalar a, Scalar b, Scalar c) {
    return std::min(max_scalar,
                    std::max({static_cast<long double>(a),
                              static_cast<long double>(b),
                              static_cast<long double>(c)}) + padding);
  };
  const auto min_x = coordinate_min(first.x, middle.x, last.x);
  const auto min_y = coordinate_min(first.y, middle.y, last.y);
  const auto min_z = coordinate_min(first.z, middle.z, last.z);
  const auto max_x = coordinate_max(first.x, middle.x, last.x);
  const auto max_y = coordinate_max(first.y, middle.y, last.y);
  const auto max_z = coordinate_max(first.z, middle.z, last.z);
  if (!std::isfinite(min_x) || !std::isfinite(min_y) ||
      !std::isfinite(min_z) || !std::isfinite(max_x) ||
      !std::isfinite(max_y) || !std::isfinite(max_z)) {
    return {};
  }
  return detail::make_bbox(
      Point3{static_cast<Scalar>(min_x), static_cast<Scalar>(min_y),
             static_cast<Scalar>(min_z)},
      Point3{static_cast<Scalar>(max_x), static_cast<Scalar>(max_y),
             static_cast<Scalar>(max_z)});
}

bool finish_intersection_arc(CurveIntersectionContext &context,
                             CurveIntersectionArc &arc) {
  const long double width = static_cast<long double>(arc.end) - arc.begin;
  if (!(width >= 0.0L) || !std::isfinite(width)) {
    context.numerical_failure = true;
    return false;
  }
  const long double acceleration = closest_search_acceleration_bound(
      context.state, arc.curve, arc.begin, arc.end);
  long double padding = std::numeric_limits<long double>::infinity();
  Point3 bounds_begin = arc.begin_point;
  Point3 bounds_end = arc.end_point;
  if (std::isfinite(acceleration) && acceleration >= 0.0L) {
    // For a twice differentiable curve, every coordinate differs from its
    // endpoint chord by at most max|C''| * width^2 / 8.
    padding = acceleration * width * width / 8.0L;
  } else {
    const long double speed = closest_search_speed_bound(
        context.state, arc.curve, arc.begin, arc.end);
    if (std::isfinite(speed) && speed >= 0.0L) {
      // Lipschitz fallback around the sampled midpoint.
      padding = speed * width * 0.5L;
      bounds_begin = arc.middle_point;
      bounds_end = arc.middle_point;
    }
  }
  if (!std::isfinite(padding) || padding < 0.0L) {
    context.numerical_failure = true;
    return false;
  }
  arc.chord_deviation = padding;
  padding += 16.0L * std::numeric_limits<Scalar>::epsilon() *
             std::max({1.0L,
                       std::abs(static_cast<long double>(arc.middle_point.x)),
                       std::abs(static_cast<long double>(arc.middle_point.y)),
                       std::abs(static_cast<long double>(arc.middle_point.z))});
  arc.bounds = intersection_bounds_from_samples(
      bounds_begin, arc.middle_point, bounds_end, padding);
  if (!arc.bounds.is_valid) {
    context.numerical_failure = true;
    return false;
  }
  const long double dx = static_cast<long double>(arc.bounds.max.x) -
                         arc.bounds.min.x;
  const long double dy = static_cast<long double>(arc.bounds.max.y) -
                         arc.bounds.min.y;
  const long double dz = static_cast<long double>(arc.bounds.max.z) -
                         arc.bounds.min.z;
  return std::isfinite(std::hypot(dx, dy, dz));
}

bool make_intersection_arc(CurveIntersectionContext &context, CurveId curve,
                           Scalar begin, Scalar end,
                           CurveIntersectionArc &arc) {
  if (end < begin) std::swap(begin, end);
  const Scalar middle = begin + (end - begin) * 0.5;
  arc.curve = curve;
  arc.begin = begin;
  arc.end = end;
  const bool has_width = end > begin;
  if (!intersection_evaluate(context, curve, begin, arc.begin_point) ||
      !intersection_evaluate(context, curve, middle, arc.middle_point) ||
      !intersection_evaluate(context, curve, end, arc.end_point, has_width)) {
    return false;
  }
  return finish_intersection_arc(context, arc);
}

bool split_intersection_arc(CurveIntersectionContext &context,
                            const CurveIntersectionArc &parent,
                            CurveIntersectionArc &left,
                            CurveIntersectionArc &right) {
  const Scalar middle = parent.begin + (parent.end - parent.begin) * 0.5;
  const Scalar left_middle = parent.begin + (middle - parent.begin) * 0.5;
  const Scalar right_middle = middle + (parent.end - middle) * 0.5;
  Point3 left_middle_point{};
  Point3 right_middle_point{};
  if (!(left_middle > parent.begin) || !(right_middle < parent.end) ||
      !intersection_evaluate(context, parent.curve, left_middle,
                             left_middle_point) ||
      !intersection_evaluate(context, parent.curve, right_middle,
                             right_middle_point)) {
    if (!context.evaluation_budget_exhausted) context.numerical_failure = true;
    return false;
  }
  left.curve = parent.curve;
  left.begin = parent.begin;
  left.end = middle;
  left.begin_point = parent.begin_point;
  left.middle_point = left_middle_point;
  left.end_point = parent.middle_point;
  right.curve = parent.curve;
  right.begin = middle;
  right.end = parent.end;
  right.begin_point = parent.middle_point;
  right.middle_point = right_middle_point;
  right.end_point = parent.end_point;
  return finish_intersection_arc(context, left) &&
         finish_intersection_arc(context, right);
}

long double intersection_box_distance(const BoundingBox &lhs,
                                      const BoundingBox &rhs) {
  const auto gap = [](Scalar first_min, Scalar first_max, Scalar second_min,
                      Scalar second_max) -> long double {
    if (first_max < second_min)
      return static_cast<long double>(second_min) - first_max;
    if (second_max < first_min)
      return static_cast<long double>(first_min) - second_max;
    return 0.0L;
  };
  return std::hypot(gap(lhs.min.x, lhs.max.x, rhs.min.x, rhs.max.x),
                    gap(lhs.min.y, lhs.max.y, rhs.min.y, rhs.max.y),
                    gap(lhs.min.z, lhs.max.z, rhs.min.z, rhs.max.z));
}

bool intersection_interval_endpoint(Scalar parameter, const Range1D &range,
                                    Scalar parameter_tolerance) {
  return std::abs(parameter - range.min) <= parameter_tolerance ||
         std::abs(parameter - range.max) <= parameter_tolerance;
}

CurveCurveIntersectionKind classify_curve_intersection(
    const detail::KernelState &state, CurveId first_curve, Scalar first_parameter,
    CurveId second_curve, Scalar second_parameter, const Range1D &first_range,
    const Range1D &second_range,
    const CurveCurveIntersectionOptions &options) {
  if (intersection_interval_endpoint(first_parameter, first_range,
                                     options.parameter_tolerance) ||
      intersection_interval_endpoint(second_parameter, second_range,
                                     options.parameter_tolerance)) {
    return CurveCurveIntersectionKind::Endpoint;
  }
  Vec3 first_derivative{};
  Vec3 second_derivative{};
  if (!closest_search_derivative(state, first_curve, first_parameter,
                                 first_derivative) ||
      !closest_search_derivative(state, second_curve, second_parameter,
                                 second_derivative)) {
    return CurveCurveIntersectionKind::Tangent;
  }
  const long double first_length = intersection_length(first_derivative);
  const long double second_length = intersection_length(second_derivative);
  if (!(first_length > 0.0L) || !(second_length > 0.0L))
    return CurveCurveIntersectionKind::Tangent;
  const long double sine = intersection_length(
      detail::cross(first_derivative, second_derivative)) /
      (first_length * second_length);
  return sine <= options.angular_tolerance
             ? CurveCurveIntersectionKind::Tangent
             : CurveCurveIntersectionKind::Transverse;
}

void add_curve_intersection_point(
    CurveCurveIntersectionResult &result,
    CurveCurveIntersectionPoint candidate,
    const CurveCurveIntersectionOptions &options) {
  for (auto &existing : result.points) {
    const bool same_parameters =
        std::abs(existing.first_parameter - candidate.first_parameter) <=
            options.parameter_tolerance * 4.0 &&
        std::abs(existing.second_parameter - candidate.second_parameter) <=
            options.parameter_tolerance * 4.0;
    const bool same_closed_seam =
        existing.kind == CurveCurveIntersectionKind::Endpoint &&
        candidate.kind == CurveCurveIntersectionKind::Endpoint &&
        intersection_point_distance(existing.point, candidate.point) <=
            options.position_tolerance;
    if (same_parameters || same_closed_seam) {
      if (candidate.residual_distance < existing.residual_distance)
        existing = candidate;
      if (existing.kind != CurveCurveIntersectionKind::Endpoint &&
          candidate.kind == CurveCurveIntersectionKind::Endpoint)
        existing.kind = CurveCurveIntersectionKind::Endpoint;
      return;
    }
  }
  result.points.push_back(candidate);
}

void add_curve_overlap(CurveCurveIntersectionResult &result,
                       CurveCurveOverlap candidate,
                       const CurveCurveIntersectionOptions &options) {
  if (candidate.first_interval.max < candidate.first_interval.min)
    std::swap(candidate.first_interval.min, candidate.first_interval.max);
  if (candidate.second_interval.max < candidate.second_interval.min)
    std::swap(candidate.second_interval.min, candidate.second_interval.max);
  for (auto &existing : result.overlaps) {
    if (existing.same_direction != candidate.same_direction) continue;
    const bool adjacent_first =
        candidate.first_interval.min <=
            existing.first_interval.max + options.parameter_tolerance &&
        existing.first_interval.min <=
            candidate.first_interval.max + options.parameter_tolerance;
    const bool adjacent_second =
        candidate.second_interval.min <=
            existing.second_interval.max + options.parameter_tolerance &&
        existing.second_interval.min <=
            candidate.second_interval.max + options.parameter_tolerance;
    if (adjacent_first && adjacent_second) {
      existing.first_interval.min =
          std::min(existing.first_interval.min, candidate.first_interval.min);
      existing.first_interval.max =
          std::max(existing.first_interval.max, candidate.first_interval.max);
      existing.second_interval.min =
          std::min(existing.second_interval.min, candidate.second_interval.min);
      existing.second_interval.max =
          std::max(existing.second_interval.max, candidate.second_interval.max);
      existing.maximum_separation =
          std::max(existing.maximum_separation, candidate.maximum_separation);
      return;
    }
  }
  result.overlaps.push_back(candidate);
}

std::vector<Range1D> restricted_curve_intersection_spans(
    const detail::CurveRecord &curve, const Range1D &requested) {
  std::vector<Range1D> source = closest_search_spans(curve);
  if (curve.kind == detail::CurveKind::CompositePolyline) {
    source.clear();
    const auto first = static_cast<long long>(std::floor(requested.min));
    const auto last = static_cast<long long>(std::ceil(requested.max));
    for (auto segment = first; segment < last; ++segment) {
      source.push_back(Range1D{static_cast<Scalar>(segment),
                               static_cast<Scalar>(segment + 1)});
    }
  }
  if (curve.kind == detail::CurveKind::Circle ||
      curve.kind == detail::CurveKind::Bezier) {
    source.clear();
    constexpr std::uint32_t initial_pieces = 8;
    for (std::uint32_t index = 0; index < initial_pieces; ++index) {
      const Scalar alpha0 = static_cast<Scalar>(index) / initial_pieces;
      const Scalar alpha1 = static_cast<Scalar>(index + 1) / initial_pieces;
      source.push_back(
          Range1D{requested.min + (requested.max - requested.min) * alpha0,
                  requested.min + (requested.max - requested.min) * alpha1});
    }
  }
  std::vector<Range1D> result;
  for (const auto &span : source) {
    const Scalar begin = std::max(requested.min, span.min);
    const Scalar end = std::min(requested.max, span.max);
    if (end > begin) result.push_back({begin, end});
  }
  if (result.empty() && requested.max > requested.min)
    result.push_back(requested);
  return result;
}

bool collect_linear_curve_spans(const detail::KernelState &state,
                                CurveId curve_id, const Range1D &requested,
                                std::vector<LinearCurveSpan> &result) {
  const auto curve_it = state.curves.find(curve_id.value);
  if (curve_it == state.curves.end()) return false;
  const auto spans =
      restricted_curve_intersection_spans(curve_it->second, requested);
  result.clear();
  result.reserve(spans.size());
  for (const auto &span : spans) {
    const long double acceleration = closest_search_acceleration_bound(
        state, curve_id, span.min, span.max);
    if (acceleration != 0.0L) {
      result.clear();
      return false;
    }
    Point3 first{};
    Point3 second{};
    if (!intersection_curve_point_at_side(state, curve_id, span.min, false,
                                          first) ||
        !intersection_curve_point_at_side(state, curve_id, span.max, true,
                                          second)) {
      result.clear();
      return false;
    }
    result.push_back({span.min, span.max, first, second});
  }
  if (!result.empty()) {
    const Point3 constant_point = result.front().begin_point;
    const bool constant = std::all_of(
        result.begin(), result.end(), [&](const LinearCurveSpan &span) {
          return intersection_point_equal(span.begin_point, constant_point) &&
                 intersection_point_equal(span.end_point, constant_point);
        });
    if (constant) {
      result.assign(1, LinearCurveSpan{requested.min, requested.max,
                                       constant_point, constant_point});
    }
  }
  return !result.empty();
}

long double closest_fraction_on_linear_span(const Point3 &point,
                                            const Point3 &begin,
                                            const Point3 &end) {
  const auto direction = intersection_vector(begin, end);
  const long double denominator = intersection_dot(direction, direction);
  if (!(denominator > 0.0L)) return 0.0L;
  return std::clamp(intersection_dot(intersection_vector(begin, point), direction) /
                        denominator,
                    0.0L, 1.0L);
}

void intersect_linear_span_pair(
    const detail::KernelState &state, CurveId first_curve,
    const LinearCurveSpan &first, CurveId second_curve,
    const LinearCurveSpan &second, const Range1D &first_range,
    const Range1D &second_range, const CurveCurveIntersectionOptions &options,
    CurveCurveIntersectionResult &result) {
  const Vec3 first_direction =
      intersection_vector(first.begin_point, first.end_point);
  const Vec3 second_direction =
      intersection_vector(second.begin_point, second.end_point);
  const long double first_length = intersection_length(first_direction);
  const long double second_length = intersection_length(second_direction);
  const long double tolerance = options.position_tolerance;

  const auto add_point = [&](long double first_fraction,
                             long double second_fraction) {
    const Point3 first_point = intersection_lerp(
        first.begin_point, first.end_point, first_fraction);
    const Point3 second_point = intersection_lerp(
        second.begin_point, second.end_point, second_fraction);
    const long double residual =
        intersection_point_distance(first_point, second_point);
    if (residual > tolerance) return;
    CurveCurveIntersectionPoint point;
    point.point = intersection_midpoint(first_point, second_point);
    point.first_parameter = intersection_parameter_lerp(
        first.begin, first.end, first_fraction);
    point.second_parameter = intersection_parameter_lerp(
        second.begin, second.end, second_fraction);
    point.residual_distance = static_cast<Scalar>(residual);
    point.kind = classify_curve_intersection(
        state, first_curve, point.first_parameter, second_curve,
        point.second_parameter, first_range, second_range, options);
    add_curve_intersection_point(result, point, options);
  };

  if (!(first_length > tolerance) && !(second_length > tolerance)) {
    if (intersection_point_distance(first.begin_point, second.begin_point) <=
        tolerance)
      add_point(0.0L, 0.0L);
    return;
  }
  if (!(first_length > tolerance)) {
    const auto second_fraction = closest_fraction_on_linear_span(
        first.begin_point, second.begin_point, second.end_point);
    add_point(0.0L, second_fraction);
    return;
  }
  if (!(second_length > tolerance)) {
    const auto first_fraction = closest_fraction_on_linear_span(
        second.begin_point, first.begin_point, first.end_point);
    add_point(first_fraction, 0.0L);
    return;
  }

  const Vec3 offset =
      intersection_vector(second.begin_point, first.begin_point);
  const long double a = intersection_dot(first_direction, first_direction);
  const long double b = intersection_dot(first_direction, second_direction);
  const long double c = intersection_dot(second_direction, second_direction);
  const long double d = intersection_dot(first_direction, offset);
  const long double e = intersection_dot(second_direction, offset);
  const long double determinant = a * c - b * b;
  const long double parallel_scale = a * c;
  const bool parallel =
      determinant <= options.angular_tolerance * options.angular_tolerance *
                         parallel_scale;
  if (!parallel) {
    const long double first_fraction = (b * e - c * d) / determinant;
    const long double second_fraction = (a * e - b * d) / determinant;
    const long double fraction_slack =
        tolerance / std::max(1.0L, std::min(first_length, second_length));
    if (first_fraction >= -fraction_slack &&
        first_fraction <= 1.0L + fraction_slack &&
        second_fraction >= -fraction_slack &&
        second_fraction <= 1.0L + fraction_slack) {
      add_point(std::clamp(first_fraction, 0.0L, 1.0L),
                std::clamp(second_fraction, 0.0L, 1.0L));
    }
    return;
  }

  const long double second_begin_on_first =
      intersection_dot(intersection_vector(first.begin_point,
                                           second.begin_point),
                       first_direction) /
      a;
  const Point3 projected_second_begin = intersection_lerp(
      first.begin_point, first.end_point, second_begin_on_first);
  if (intersection_point_distance(projected_second_begin,
                                  second.begin_point) > tolerance) {
    return;
  }
  const long double second_end_on_first =
      intersection_dot(intersection_vector(first.begin_point, second.end_point),
                       first_direction) /
      a;
  const long double overlap_begin =
      std::max(0.0L, std::min(second_begin_on_first, second_end_on_first));
  const long double overlap_end =
      std::min(1.0L, std::max(second_begin_on_first, second_end_on_first));
  const long double fraction_slack = tolerance / first_length;
  if (overlap_end < overlap_begin - fraction_slack) return;
  if ((overlap_end - overlap_begin) * first_length <= tolerance) {
    const long double first_fraction =
        std::clamp((overlap_begin + overlap_end) * 0.5L, 0.0L, 1.0L);
    const Point3 point = intersection_lerp(first.begin_point, first.end_point,
                                           first_fraction);
    const long double second_fraction = closest_fraction_on_linear_span(
        point, second.begin_point, second.end_point);
    add_point(first_fraction, second_fraction);
    return;
  }

  const Point3 overlap_first_point = intersection_lerp(
      first.begin_point, first.end_point, overlap_begin);
  const Point3 overlap_last_point = intersection_lerp(
      first.begin_point, first.end_point, overlap_end);
  const long double second_fraction0 = closest_fraction_on_linear_span(
      overlap_first_point, second.begin_point, second.end_point);
  const long double second_fraction1 = closest_fraction_on_linear_span(
      overlap_last_point, second.begin_point, second.end_point);
  CurveCurveOverlap overlap;
  overlap.first_interval =
      {intersection_parameter_lerp(first.begin, first.end, overlap_begin),
       intersection_parameter_lerp(first.begin, first.end, overlap_end)};
  overlap.second_interval =
      {intersection_parameter_lerp(second.begin, second.end,
                                   std::min(second_fraction0, second_fraction1)),
       intersection_parameter_lerp(second.begin, second.end,
                                   std::max(second_fraction0, second_fraction1))};
  overlap.same_direction = b >= 0.0L;
  overlap.maximum_separation = static_cast<Scalar>(std::max(
      intersection_point_distance(
          overlap_first_point,
          intersection_lerp(second.begin_point, second.end_point,
                            second_fraction0)),
      intersection_point_distance(
          overlap_last_point,
          intersection_lerp(second.begin_point, second.end_point,
                            second_fraction1))));
  add_curve_overlap(result, overlap, options);
}

bool refine_curve_intersection(
    CurveIntersectionContext &context,
    const CurveIntersectionRectangle &rectangle, const Range1D &first_range,
    const Range1D &second_range, CurveCurveIntersectionPoint &result) {
  Scalar first_parameter =
      rectangle.first.begin +
      (rectangle.first.end - rectangle.first.begin) * 0.5;
  Scalar second_parameter =
      rectangle.second.begin +
      (rectangle.second.end - rectangle.second.begin) * 0.5;
  Point3 first_point{};
  Point3 second_point{};
  if (!intersection_evaluate(context, rectangle.first.curve, first_parameter,
                             first_point) ||
      !intersection_evaluate(context, rectangle.second.curve, second_parameter,
                             second_point)) {
    return false;
  }
  long double best_distance =
      intersection_point_distance(first_point, second_point);

  const auto consider = [&](Scalar candidate_first, Scalar candidate_second,
                            Scalar &accepted_first, Scalar &accepted_second,
                            Point3 &accepted_first_point,
                            Point3 &accepted_second_point,
                            long double &accepted_distance) {
    Point3 candidate_first_point{};
    Point3 candidate_second_point{};
    if (!intersection_evaluate(context, rectangle.first.curve,
                               candidate_first, candidate_first_point) ||
        !intersection_evaluate(context, rectangle.second.curve,
                               candidate_second, candidate_second_point)) {
      return false;
    }
    const long double candidate_distance = intersection_point_distance(
        candidate_first_point, candidate_second_point);
    if (candidate_distance < accepted_distance) {
      accepted_distance = candidate_distance;
      accepted_first = candidate_first;
      accepted_second = candidate_second;
      accepted_first_point = candidate_first_point;
      accepted_second_point = candidate_second_point;
    }
    return true;
  };

  // Corners and edge midpoints make endpoint intersections deterministic and
  // provide a stable seed when the normal equations are singular at tangency.
  const std::array<Scalar, 3> first_seeds{
      rectangle.first.begin, first_parameter, rectangle.first.end};
  const std::array<Scalar, 3> second_seeds{
      rectangle.second.begin, second_parameter, rectangle.second.end};
  for (const Scalar first_seed : first_seeds) {
    for (const Scalar second_seed : second_seeds) {
      if (first_seed == first_parameter && second_seed == second_parameter)
        continue;
      if (!consider(first_seed, second_seed, first_parameter, second_parameter,
                    first_point, second_point, best_distance))
        return false;
    }
  }

  for (std::uint32_t iteration = 0; iteration < 32; ++iteration) {
    if (best_distance <= context.options.position_tolerance * 0.125L) break;
    Vec3 first_derivative{};
    Vec3 second_derivative{};
    if (!closest_search_derivative(context.state, rectangle.first.curve,
                                   first_parameter, first_derivative) ||
        !closest_search_derivative(context.state, rectangle.second.curve,
                                   second_parameter, second_derivative)) {
      context.numerical_failure = true;
      return false;
    }
    const Vec3 residual = intersection_vector(second_point, first_point);
    const long double a = intersection_dot(first_derivative, first_derivative);
    const long double cross =
        intersection_dot(first_derivative, second_derivative);
    const long double c = intersection_dot(second_derivative, second_derivative);
    const long double rhs_first = -intersection_dot(first_derivative, residual);
    const long double rhs_second = intersection_dot(second_derivative, residual);
    const long double determinant = a * c - cross * cross;
    long double first_step = 0.0L;
    long double second_step = 0.0L;
    if (determinant > 64.0L * std::numeric_limits<long double>::epsilon() *
                          std::max(1.0L, a * c)) {
      first_step = (c * rhs_first + cross * rhs_second) / determinant;
      second_step = (cross * rhs_first + a * rhs_second) / determinant;
    } else {
      // Alternating orthogonal projections remain defined for tangent and
      // nearly parallel pairs, where the two-variable normal matrix is rank 1.
      if (a > 0.0L) first_step = rhs_first / a;
      if (c > 0.0L) second_step = rhs_second / c;
    }
    if (!std::isfinite(first_step) || !std::isfinite(second_step)) {
      context.numerical_failure = true;
      return false;
    }
    bool improved = false;
    for (std::uint32_t damping = 0; damping < 12; ++damping) {
      const long double scale = std::ldexp(1.0L, -static_cast<int>(damping));
      const Scalar candidate_first = std::clamp(
          static_cast<Scalar>(first_parameter + first_step * scale),
          rectangle.first.begin, rectangle.first.end);
      const Scalar candidate_second = std::clamp(
          static_cast<Scalar>(second_parameter + second_step * scale),
          rectangle.second.begin, rectangle.second.end);
      const long double previous = best_distance;
      if (!consider(candidate_first, candidate_second, first_parameter,
                    second_parameter, first_point, second_point,
                    best_distance))
        return false;
      if (best_distance < previous) {
        improved = true;
        break;
      }
    }
    if (!improved) break;
  }
  if (best_distance > context.options.position_tolerance) return false;
  result.point = intersection_midpoint(first_point, second_point);
  result.first_parameter = first_parameter;
  result.second_parameter = second_parameter;
  result.residual_distance = static_cast<Scalar>(best_distance);
  result.kind = classify_curve_intersection(
      context.state, rectangle.first.curve, first_parameter,
      rectangle.second.curve, second_parameter, first_range, second_range,
      context.options);
  return true;
}

bool curve_intersection_interval_valid(const Range1D &range) {
  return std::isfinite(range.min) && std::isfinite(range.max) &&
         range.min <= range.max;
}

Result<CurveCurveIntersectionResult> curve_intersection_failure(
    detail::KernelState &state, StatusCode status, std::string message) {
  return detail::failed_result<CurveCurveIntersectionResult>(
      state, status, diag_codes::kGeoIntersectionFailure, std::move(message),
      "曲线-曲线求交失败");
}

} // namespace

Result<CurveCurveIntersectionResult>
GeometryIntersectionService::intersect_curve_curve(
    CurveId first_curve, CurveId second_curve,
    const CurveCurveIntersectionOptions &options) const {
  const auto first_it = state_->curves.find(first_curve.value);
  const auto second_it = state_->curves.find(second_curve.value);
  if (first_it == state_->curves.end() || second_it == state_->curves.end()) {
    return detail::invalid_input_result<CurveCurveIntersectionResult>(
        *state_, diag_codes::kCoreInvalidHandle,
        "曲线-曲线求交失败：目标曲线不存在", "曲线-曲线求交失败");
  }
  if (!std::isfinite(options.position_tolerance) ||
      !(options.position_tolerance > 0.0) ||
      !std::isfinite(options.parameter_tolerance) ||
      !(options.parameter_tolerance > 0.0) ||
      !std::isfinite(options.angular_tolerance) ||
      options.angular_tolerance < 0.0 || options.angular_tolerance > 1.0 ||
      options.max_evaluations < 6 || options.max_subdivisions == 0) {
    return detail::invalid_input_result<CurveCurveIntersectionResult>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲线-曲线求交失败：容差或求值/细分预算非法",
        "曲线-曲线求交失败");
  }

  Range1D first_range =
      options.first_interval.value_or(curve_domain(first_it->second));
  Range1D second_range =
      options.second_interval.value_or(curve_domain(second_it->second));
  if (first_range.max < first_range.min)
    std::swap(first_range.min, first_range.max);
  if (second_range.max < second_range.min)
    std::swap(second_range.min, second_range.max);
  if (!curve_intersection_interval_valid(first_range) ||
      !curve_intersection_interval_valid(second_range)) {
    return detail::invalid_input_result<CurveCurveIntersectionResult>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "曲线-曲线求交失败：无限曲线需要显式有限参数区间",
        "曲线-曲线求交失败");
  }
  const auto first_domain = curve_domain(first_it->second);
  const auto second_domain = curve_domain(second_it->second);
  if (first_range.min < first_domain.min || first_range.max > first_domain.max ||
      second_range.min < second_domain.min ||
      second_range.max > second_domain.max) {
    return detail::invalid_input_result<CurveCurveIntersectionResult>(
        *state_, diag_codes::kGeoParameterOutOfDomain,
        "曲线-曲线求交失败：请求参数区间超出曲线定义域",
        "曲线-曲线求交失败");
  }

  CurveCurveIntersectionResult output;
  CurveIntersectionContext context{*state_, options};

  const bool same_parameterization =
      same_curve_parameterization(*state_, first_curve, second_curve);
  const Scalar common_begin = std::max(first_range.min, second_range.min);
  const Scalar common_end = std::min(first_range.max, second_range.max);
  const bool has_parameter_overlap =
      same_parameterization && common_end > common_begin;
  if (has_parameter_overlap) {
    add_curve_overlap(output,
                      CurveCurveOverlap{{common_begin, common_end},
                                        {common_begin, common_end}, true, 0.0},
                      options);
  }

  std::vector<LinearCurveSpan> first_linear;
  std::vector<LinearCurveSpan> second_linear;
  const bool first_entirely_linear = collect_linear_curve_spans(
      *state_, first_curve, first_range, first_linear);
  const bool second_entirely_linear = collect_linear_curve_spans(
      *state_, second_curve, second_range, second_linear);
  if (first_entirely_linear && second_entirely_linear) {
    for (const auto &first_span : first_linear) {
      for (const auto &second_span : second_linear) {
        intersect_linear_span_pair(
            *state_, first_curve, first_span, second_curve, second_span,
            first_range, second_range, options, output);
        ++output.parameter_rectangles_processed;
      }
    }
    std::sort(output.points.begin(), output.points.end(),
              [](const auto &lhs, const auto &rhs) {
                if (lhs.first_parameter != rhs.first_parameter)
                  return lhs.first_parameter < rhs.first_parameter;
                return lhs.second_parameter < rhs.second_parameter;
              });
    std::sort(output.overlaps.begin(), output.overlaps.end(),
              [](const auto &lhs, const auto &rhs) {
                if (lhs.first_interval.min != rhs.first_interval.min)
                  return lhs.first_interval.min < rhs.first_interval.min;
                return lhs.second_interval.min < rhs.second_interval.min;
              });
    return ok_result(std::move(output),
                     state_->create_diagnostic("曲线-曲线解析分段直线求交完成"));
  }

  // Zero-width intervals are legitimate point-vs-curve queries. Give them a
  // finite numerical width only on the non-degenerate side; the fixed side is
  // represented by a zero-radius arc and is never subdivided.
  auto first_spans = first_range.min == first_range.max
                         ? std::vector<Range1D>{first_range}
                         : restricted_curve_intersection_spans(
                               first_it->second, first_range);
  auto second_spans = second_range.min == second_range.max
                          ? std::vector<Range1D>{second_range}
                          : restricted_curve_intersection_spans(
                                second_it->second, second_range);
  if (has_parameter_overlap) {
    const auto split_at_overlap_boundaries =
        [&](std::vector<Range1D> &spans) {
          std::vector<Range1D> split;
          for (const auto &span : spans) {
            std::vector<Scalar> cuts{span.min};
            if (common_begin > span.min && common_begin < span.max)
              cuts.push_back(common_begin);
            if (common_end > span.min && common_end < span.max)
              cuts.push_back(common_end);
            cuts.push_back(span.max);
            for (std::size_t index = 0; index + 1 < cuts.size(); ++index) {
              if (cuts[index + 1] > cuts[index])
                split.push_back({cuts[index], cuts[index + 1]});
            }
          }
          spans = std::move(split);
        };
    split_at_overlap_boundaries(first_spans);
    split_at_overlap_boundaries(second_spans);
  }
  std::vector<CurveIntersectionRectangle> pending;
  for (const auto &first_span : first_spans) {
    CurveIntersectionArc first_arc;
    if (!make_intersection_arc(context, first_curve, first_span.min,
                               first_span.max, first_arc))
      break;
    for (const auto &second_span : second_spans) {
      // A proven same-parameter overlap is already represented as a continuous
      // result; do not recursively turn it into thousands of point hits.
      if (has_parameter_overlap && first_span.min >= common_begin &&
          first_span.max <= common_end && second_span.min >= common_begin &&
          second_span.max <= common_end) {
        continue;
      }
      CurveIntersectionArc second_arc;
      if (!make_intersection_arc(context, second_curve, second_span.min,
                                 second_span.max, second_arc))
        break;
      if (intersection_box_distance(first_arc.bounds, second_arc.bounds) <=
          options.position_tolerance) {
        pending.push_back({first_arc, second_arc});
      }
    }
    if (context.evaluation_budget_exhausted || context.numerical_failure) break;
  }

  while (!pending.empty() && !context.evaluation_budget_exhausted &&
         !context.numerical_failure) {
    if (context.rectangles_processed >= options.max_subdivisions) {
      break;
    }
    auto rectangle = std::move(pending.back());
    pending.pop_back();
    ++context.rectangles_processed;
    if (intersection_box_distance(rectangle.first.bounds,
                                  rectangle.second.bounds) >
        options.position_tolerance) {
      continue;
    }
    const Scalar first_width = rectangle.first.end - rectangle.first.begin;
    const Scalar second_width = rectangle.second.end - rectangle.second.begin;
    const bool split_first =
        first_width > options.parameter_tolerance &&
        rectangle.first.chord_deviation >
            options.position_tolerance * 0.125L;
    const bool split_second =
        second_width > options.parameter_tolerance &&
        rectangle.second.chord_deviation >
            options.position_tolerance * 0.125L;
    if (!split_first && !split_second) {
      CurveCurveIntersectionPoint point;
      if (refine_curve_intersection(context, rectangle, first_range,
                                    second_range, point)) {
        add_curve_intersection_point(output, point, options);
      }
      continue;
    }

    const long double first_score = split_first
        ? std::max(static_cast<long double>(first_width) /
                       options.parameter_tolerance,
                   rectangle.first.chord_deviation /
                       options.position_tolerance)
        : -1.0L;
    const long double second_score = split_second
        ? std::max(static_cast<long double>(second_width) /
                       options.parameter_tolerance,
                   rectangle.second.chord_deviation /
                       options.position_tolerance)
        : -1.0L;
    if (first_score >= second_score) {
      CurveIntersectionArc left;
      CurveIntersectionArc right;
      if (!split_intersection_arc(context, rectangle.first, left, right))
        break;
      if (intersection_box_distance(left.bounds, rectangle.second.bounds) <=
          options.position_tolerance)
        pending.push_back({left, rectangle.second});
      if (intersection_box_distance(right.bounds, rectangle.second.bounds) <=
          options.position_tolerance)
        pending.push_back({right, rectangle.second});
    } else {
      CurveIntersectionArc left;
      CurveIntersectionArc right;
      if (!split_intersection_arc(context, rectangle.second, left, right))
        break;
      if (intersection_box_distance(rectangle.first.bounds, left.bounds) <=
          options.position_tolerance)
        pending.push_back({rectangle.first, left});
      if (intersection_box_distance(rectangle.first.bounds, right.bounds) <=
          options.position_tolerance)
        pending.push_back({rectangle.first, right});
    }
  }

  if (context.evaluation_budget_exhausted) {
    return curve_intersection_failure(
        *state_, StatusCode::OperationFailed,
        "曲线-曲线求交失败：点值求值预算耗尽，未返回部分交集");
  }
  if (context.numerical_failure) {
    return curve_intersection_failure(
        *state_, StatusCode::NumericalInstability,
        "曲线-曲线求交失败：无法建立有限保守界或参数精化停滞");
  }
  if (!pending.empty()) {
    return curve_intersection_failure(
        *state_, StatusCode::OperationFailed,
        "曲线-曲线求交失败：候选参数矩形预算耗尽，未返回部分交集");
  }

  output.evaluations = context.evaluations;
  output.parameter_rectangles_processed = context.rectangles_processed;
  std::sort(output.points.begin(), output.points.end(),
            [](const auto &lhs, const auto &rhs) {
              if (lhs.first_parameter != rhs.first_parameter)
                return lhs.first_parameter < rhs.first_parameter;
              return lhs.second_parameter < rhs.second_parameter;
            });
  std::sort(output.overlaps.begin(), output.overlaps.end(),
            [](const auto &lhs, const auto &rhs) {
              if (lhs.first_interval.min != rhs.first_interval.min)
                return lhs.first_interval.min < rhs.first_interval.min;
              return lhs.second_interval.min < rhs.second_interval.min;
            });
  return ok_result(std::move(output),
                   state_->create_diagnostic("曲线-曲线全域有界求交完成"));
}
