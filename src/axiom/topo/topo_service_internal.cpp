#include "axiom/internal/topo/topo_service_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "axiom/geo/geometry_services.h"
#include "axiom/internal/geo/geo_service_internal.h"
#include "axiom/internal/math/math_internal_utils.h"

namespace axiom {
namespace topo_internal {





bool validate_source_faces_exist(const detail::KernelState &state,
                                 std::span<const FaceId> faces) {
  return std::all_of(faces.begin(), faces.end(), [&state](FaceId face_id) {
    return detail::has_face(state, face_id);
  });
}

bool validate_source_shells_exist(const detail::KernelState &state,
                                  std::span<const ShellId> shells) {
  return std::all_of(shells.begin(), shells.end(), [&state](ShellId shell_id) {
    return detail::has_shell(state, shell_id);
  });
}

bool validate_source_bodies_exist(const detail::KernelState &state,
                                  std::span<const BodyId> bodies) {
  return std::all_of(bodies.begin(), bodies.end(), [&state](BodyId body_id) {
    return detail::has_body(state, body_id);
  });
}

bool extend_bbox(BoundingBox &bbox, const Point3 &point) {
  if (!bbox.is_valid) {
    bbox.min = point;
    bbox.max = point;
    bbox.is_valid = true;
    return true;
  }
  bbox.min.x = std::min(bbox.min.x, point.x);
  bbox.min.y = std::min(bbox.min.y, point.y);
  bbox.min.z = std::min(bbox.min.z, point.z);
  bbox.max.x = std::max(bbox.max.x, point.x);
  bbox.max.y = std::max(bbox.max.y, point.y);
  bbox.max.z = std::max(bbox.max.z, point.z);
  return true;
}

bool append_edge_bbox(const std::shared_ptr<detail::KernelState> &state,
                      EdgeId edge_id,
                      BoundingBox &bbox) {
  if (!state) return false;
  const auto edge_it = state->edges.find(edge_id.value);
  if (edge_it == state->edges.end()) {
    return false;
  }
  const auto v0_it = state->vertices.find(edge_it->second.v0.value);
  const auto v1_it = state->vertices.find(edge_it->second.v1.value);
  if (v0_it == state->vertices.end() || v1_it == state->vertices.end()) {
    return false;
  }
  extend_bbox(bbox, v0_it->second.point);
  extend_bbox(bbox, v1_it->second.point);
  if (edge_it->second.has_parameter_interval) {
    const CurveService curves(state);
    const auto curve_bbox = curves.bbox(
        edge_it->second.curve_id, edge_it->second.start_parameter,
        edge_it->second.end_parameter);
    if (!curve_bbox.value || !curve_bbox.value->is_valid) {
      return false;
    }
    extend_bbox(bbox, curve_bbox.value->min);
    extend_bbox(bbox, curve_bbox.value->max);
  }
  return true;
}

bool append_loop_bbox(const std::shared_ptr<detail::KernelState> &state,
                      LoopId loop_id,
                      BoundingBox &bbox) {
  if (!state) return false;
  const auto loop_it = state->loops.find(loop_id.value);
  if (loop_it == state->loops.end() || loop_it->second.coedges.empty()) {
    return false;
  }
  for (const auto coedge_id : loop_it->second.coedges) {
    const auto coedge_it = state->coedges.find(coedge_id.value);
    if (coedge_it == state->coedges.end() ||
        !append_edge_bbox(state, coedge_it->second.edge_id, bbox)) {
      return false;
    }
  }
  return true;
}

bool append_face_bbox(const std::shared_ptr<detail::KernelState> &state,
                      FaceId face_id,
                      BoundingBox &bbox) {
  if (!state) return false;
  const auto face_it = state->faces.find(face_id.value);
  if (face_it == state->faces.end()) {
    return false;
  }
  if (!append_loop_bbox(state, face_it->second.outer_loop, bbox)) {
    return false;
  }
  for (const auto loop_id : face_it->second.inner_loops) {
    if (!append_loop_bbox(state, loop_id, bbox)) {
      return false;
    }
  }
  return true;
}

BoundingBox compute_body_bbox(const std::shared_ptr<detail::KernelState> &state,
                              std::span<const ShellId> shells) {
  if (!state) return {};
  BoundingBox bbox{};
  for (const auto shell_id : shells) {
    const auto shell_it = state->shells.find(shell_id.value);
    if (shell_it == state->shells.end()) {
      return {};
    }
    for (const auto face_id : shell_it->second.faces) {
      if (!append_face_bbox(state, face_id, bbox)) {
        return {};
      }
    }
  }
  return bbox;
}

std::optional<std::array<VertexId, 2>>
oriented_vertices(const detail::KernelState &state, CoedgeId coedge_id) {
  const auto coedge_it = state.coedges.find(coedge_id.value);
  if (coedge_it == state.coedges.end()) {
    return std::nullopt;
  }
  const auto edge_it = state.edges.find(coedge_it->second.edge_id.value);
  if (edge_it == state.edges.end()) {
    return std::nullopt;
  }
  if (!detail::has_vertex(state, edge_it->second.v0) ||
      !detail::has_vertex(state, edge_it->second.v1) ||
      edge_it->second.v0.value == edge_it->second.v1.value) {
    return std::nullopt;
  }
  if (coedge_it->second.reversed) {
    return std::array<VertexId, 2>{edge_it->second.v1, edge_it->second.v0};
  }
  return std::array<VertexId, 2>{edge_it->second.v0, edge_it->second.v1};
}

// Newell normal (unnormalized) for a closed 3D polygon given as ordered vertex positions.
Vec3 newell_normal_unnormalized(const std::vector<Point3> &poly) {
  Vec3 n{0.0, 0.0, 0.0};
  if (poly.size() < 3) {
    return n;
  }
  for (std::size_t i = 0; i < poly.size(); ++i) {
    const auto &p0 = poly[i];
    const auto &p1 = poly[(i + 1) % poly.size()];
    n.x += (p0.y - p1.y) * (p0.z + p1.z);
    n.y += (p0.z - p1.z) * (p0.x + p1.x);
    n.z += (p0.x - p1.x) * (p0.y + p1.y);
  }
  return n;
}

bool loop_vertex_chain_3d(const detail::KernelState &state, LoopId loop_id,
                           std::vector<Point3> &out, std::string &reason) {
  out.clear();
  const auto lit = state.loops.find(loop_id.value);
  if (lit == state.loops.end() || lit->second.coedges.empty()) {
    reason = "环不存在或为空";
    return false;
  }
  out.reserve(lit->second.coedges.size());
  for (const auto coedge_id : lit->second.coedges) {
    const auto ov = oriented_vertices(state, coedge_id);
    if (!ov.has_value()) {
      reason = "定向边无效";
      return false;
    }
    const auto vit = state.vertices.find((*ov)[0].value);
    if (vit == state.vertices.end()) {
      reason = "顶点不存在";
      return false;
    }
    out.push_back(vit->second.point);
  }
  return true;
}

bool validate_loop_record(const detail::KernelState &state,
                          const detail::LoopRecord &loop, std::string &reason) {
  if (loop.coedges.empty()) {
    reason = "环不包含任何定向边";
    return false;
  }

  {
    std::unordered_set<std::uint64_t> used_coedge;
    used_coedge.reserve(loop.coedges.size());
    for (const auto coedge_id : loop.coedges) {
      if (!used_coedge.insert(coedge_id.value).second) {
        reason = "环包含重复定向边引用";
        return false;
      }
    }
  }

  {
    std::unordered_set<std::uint64_t> used_edges;
    used_edges.reserve(loop.coedges.size());
    for (const auto coedge_id : loop.coedges) {
      const auto coedge_it = state.coedges.find(coedge_id.value);
      if (coedge_it == state.coedges.end()) {
        reason = "环引用了不存在的定向边";
        return false;
      }
      const auto edge_value = coedge_it->second.edge_id.value;
      if (state.edges.find(edge_value) == state.edges.end()) {
        reason = "环引用了不存在的边";
        return false;
      }
      if (!used_edges.insert(edge_value).second) {
        reason = "环包含重复边引用";
        return false;
      }
    }
  }

  std::optional<VertexId> first_start;
  std::optional<VertexId> previous_end;
  std::unordered_set<std::uint64_t> visited_vertices;
  visited_vertices.reserve(loop.coedges.size());
  for (const auto coedge_id : loop.coedges) {
    const auto oriented = oriented_vertices(state, coedge_id);
    if (!oriented.has_value()) {
      reason = "环引用了无效定向边或退化边";
      return false;
    }
    if (!first_start.has_value()) {
      first_start = (*oriented)[0];
    }
    if (!visited_vertices.insert((*oriented)[0].value).second) {
      reason = "环在闭合终点之外重复经过同一顶点";
      return false;
    }
    if (previous_end.has_value() &&
        previous_end->value != (*oriented)[0].value) {
      reason = "环中相邻定向边首尾不连续";
      return false;
    }
    previous_end = (*oriented)[1];
  }

  if (!first_start.has_value() || !previous_end.has_value() ||
      first_start->value != previous_end->value) {
    reason = "环未闭合";
    return false;
  }
  return true;
}

bool validate_loop_id(const detail::KernelState &state, LoopId loop_id,
                      std::string &reason) {
  const auto loop_it = state.loops.find(loop_id.value);
  if (loop_it == state.loops.end()) {
    reason = "引用的环不存在";
    return false;
  }
  return validate_loop_record(state, loop_it->second, reason);
}

bool valid_face_bound_loop_size(const detail::KernelState &state,
                                const detail::LoopRecord &loop) {
  if (loop.coedges.size() >= 3) {
    return true;
  }
  if (loop.coedges.size() != 2) {
    return false;
  }
  const auto c0 = state.coedges.find(loop.coedges[0].value);
  const auto c1 = state.coedges.find(loop.coedges[1].value);
  if (c0 == state.coedges.end() || c1 == state.coedges.end()) {
    return false;
  }
  const auto e0 = state.edges.find(c0->second.edge_id.value);
  const auto e1 = state.edges.find(c1->second.edge_id.value);
  return e0 != state.edges.end() && e1 != state.edges.end() &&
         e0->second.curve_id.value != 0 &&
         e0->second.curve_id.value == e1->second.curve_id.value;
}

std::optional<std::array<std::uint64_t, 3>>
face_cross_loop_shared_vertex(const detail::KernelState &state, LoopId outer_loop,
                              std::span<const LoopId> inner_loops) {
  std::unordered_map<std::uint64_t, LoopId> vertex_loops;
  for (std::size_t i = 0; i <= inner_loops.size(); ++i) {
    const auto loop_id = i == 0 ? outer_loop : inner_loops[i - 1];
    const auto loop_it = state.loops.find(loop_id.value);
    if (loop_it == state.loops.end()) {
      continue;  // The caller validates loop handles first.
    }
    for (const auto coedge_id : loop_it->second.coedges) {
      const auto vertices = oriented_vertices(state, coedge_id);
      if (!vertices.has_value()) {
        continue;  // The caller validates loop records first.
      }
      const auto vertex_value = (*vertices)[0].value;
      const auto [it, inserted] = vertex_loops.emplace(vertex_value, loop_id);
      if (!inserted && it->second.value != loop_id.value) {
        return std::array<std::uint64_t, 3>{it->second.value, loop_id.value,
                                            vertex_value};
      }
    }
  }
  return std::nullopt;
}

std::optional<std::array<std::uint64_t, 4>>
face_cross_loop_coincident_vertices(const detail::KernelState &state,
                                    LoopId outer_loop,
                                    std::span<const LoopId> inner_loops) {
  struct BoundaryVertex {
    LoopId loop;
    VertexId vertex;
    Point3 point;
  };
  std::vector<BoundaryVertex> seen;
  for (std::size_t i = 0; i <= inner_loops.size(); ++i) {
    const auto loop_id = i == 0 ? outer_loop : inner_loops[i - 1];
    const auto loop_it = state.loops.find(loop_id.value);
    if (loop_it == state.loops.end()) {
      continue;
    }
    for (const auto coedge_id : loop_it->second.coedges) {
      const auto oriented = oriented_vertices(state, coedge_id);
      if (!oriented) {
        continue;
      }
      const auto vertex = (*oriented)[0];
      const auto vertex_it = state.vertices.find(vertex.value);
      if (vertex_it == state.vertices.end()) {
        continue;
      }
      const auto &point = vertex_it->second.point;
      if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
          !std::isfinite(point.z)) {
        continue;
      }
      for (const auto &prior : seen) {
        if (prior.loop.value != loop_id.value &&
            prior.vertex.value != vertex.value &&
            prior.point.x == point.x && prior.point.y == point.y &&
            prior.point.z == point.z) {
          return std::array<std::uint64_t, 4>{prior.loop.value, loop_id.value,
                                              prior.vertex.value, vertex.value};
        }
      }
      seen.push_back({loop_id, vertex, point});
    }
  }
  return std::nullopt;
}

FaceBoundaryConflictSearchResult face_cross_loop_boundary_conflict(
    detail::KernelState &state, LoopId outer_loop,
    std::span<const LoopId> inner_loops, Scalar linear_tolerance) {
  using WidePoint = std::array<long double, 3>;
  struct Segment {
    LoopId loop;
    EdgeId edge;
    WidePoint start;
    WidePoint end;
    bool start_is_edge_endpoint {true};
    bool end_is_edge_endpoint {true};
  };
  const auto subtract = [](const WidePoint &a, const WidePoint &b) {
    return WidePoint{a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  };
  const auto add_scaled = [](const WidePoint &a, const WidePoint &b,
                             long double scale) {
    return WidePoint{a[0] + scale * b[0], a[1] + scale * b[1],
                     a[2] + scale * b[2]};
  };
  const auto dot = [](const WidePoint &a, const WidePoint &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };
  const auto clamp01 = [](long double value) {
    return std::max(0.0L, std::min(1.0L, value));
  };
  const auto to_point3 = [](const WidePoint &point) {
    return Point3{static_cast<Scalar>(point[0]),
                  static_cast<Scalar>(point[1]),
                  static_cast<Scalar>(point[2])};
  };

  struct ClosestPair {
    long double first_parameter{0.0L};
    long double second_parameter{0.0L};
    WidePoint first_point{};
    WidePoint second_point{};
    long double squared_distance{0.0L};
  };

  // Closest points of two closed 3D segments.  This handles point-like
  // degeneracy as well as parallel segments without dividing by a tiny
  // determinant; the topology caller can therefore diagnose corrupted or
  // near-degenerate records instead of producing NaNs.
  const auto closest_pair = [&](const Segment &first,
                                const Segment &second) {
    const auto d1 = subtract(first.end, first.start);
    const auto d2 = subtract(second.end, second.start);
    const auto r = subtract(first.start, second.start);
    const long double a = dot(d1, d1);
    const long double e = dot(d2, d2);
    const long double f = dot(d2, r);
    long double s = 0.0L;
    long double t = 0.0L;
    if (a == 0.0L && e == 0.0L) {
      s = 0.0L;
      t = 0.0L;
    } else if (a == 0.0L) {
      s = 0.0L;
      t = clamp01(f / e);
    } else {
      const long double c = dot(d1, r);
      if (e == 0.0L) {
        t = 0.0L;
        s = clamp01(-c / a);
      } else {
        const long double b = dot(d1, d2);
        const long double denominator = a * e - b * b;
        if (denominator > 0.0L) {
          s = clamp01((b * f - c * e) / denominator);
        }
        t = (b * s + f) / e;
        if (t < 0.0L) {
          t = 0.0L;
          s = clamp01(-c / a);
        } else if (t > 1.0L) {
          t = 1.0L;
          s = clamp01((b - c) / a);
        }
      }
    }
    const auto p = add_scaled(first.start, d1, s);
    const auto q = add_scaled(second.start, d2, t);
    const auto delta = subtract(p, q);
    return ClosestPair{s, t, p, q, std::max(0.0L, dot(delta, delta))};
  };

  const auto endpoint_parameter = [](const Segment &segment,
                                     long double value,
                                     long double parameter_epsilon) {
    return (segment.start_is_edge_endpoint && value <= parameter_epsilon) ||
           (segment.end_is_edge_endpoint &&
            value >= 1.0L - parameter_epsilon);
  };

  const auto inspect_pair = [&](const Segment &first,
                                const Segment &second)
      -> std::optional<FaceBoundaryConflict> {
    const auto d1 = subtract(first.end, first.start);
    const auto d2 = subtract(second.end, second.start);
    const auto offset = subtract(second.start, first.start);
    const long double a = dot(d1, d1);
    const long double e = dot(d2, d2);
    const long double parameter_epsilon =
        64.0L * std::numeric_limits<long double>::epsilon();
    const auto cross_is_roundoff_zero = [](const WidePoint &lhs,
                                           const WidePoint &rhs) {
      const std::array<std::array<std::size_t, 4>, 3> terms{{
          {{1, 2, 2, 1}}, {{2, 0, 0, 2}}, {{0, 1, 1, 0}}}};
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto indices = terms[axis];
        const long double first = lhs[indices[0]] * rhs[indices[1]];
        const long double second = lhs[indices[2]] * rhs[indices[3]];
        const long double residual = first - second;
        const long double roundoff =
            64.0L * std::numeric_limits<long double>::epsilon() *
            (std::abs(first) + std::abs(second));
        if (std::abs(residual) > roundoff) return false;
      }
      return true;
    };

    // Exact collinearity is tested independently of the user tolerance so a
    // true overlap remains distinguishable from merely close parallel edges.
    // Each cross-product residual is compared with the products that formed it,
    // so a tiny real separation is not swallowed merely because coordinates on
    // an unrelated axis are large.
    if (a > 0.0L && e > 0.0L) {
      if (cross_is_roundoff_zero(d1, d2) &&
          cross_is_roundoff_zero(offset, d1)) {
        long double second_start_on_first = dot(offset, d1) / a;
        long double second_end_on_first =
            dot(subtract(second.end, first.start), d1) / a;
        if (second_start_on_first > second_end_on_first) {
          std::swap(second_start_on_first, second_end_on_first);
        }
        const long double overlap_start =
            std::max(0.0L, second_start_on_first);
        const long double overlap_end =
            std::min(1.0L, second_end_on_first);
        if (overlap_end + parameter_epsilon >= overlap_start) {
          const bool positive_length =
              overlap_end - overlap_start > parameter_epsilon;
          const long double first_parameter =
              positive_length ? (overlap_start + overlap_end) * 0.5L
                              : clamp01((overlap_start + overlap_end) * 0.5L);
          const auto point = add_scaled(first.start, d1, first_parameter);
          const long double second_parameter =
              clamp01(dot(subtract(point, second.start), d2) / e);
          const bool endpoint =
              endpoint_parameter(first, first_parameter,
                                 parameter_epsilon) ||
              endpoint_parameter(second, second_parameter,
                                 parameter_epsilon);
          return FaceBoundaryConflict{
              positive_length ? FaceBoundaryConflictKind::CollinearOverlap
                              : (endpoint
                                     ? FaceBoundaryConflictKind::EndpointTouch
                                     : FaceBoundaryConflictKind::ProperIntersection),
              first.loop, second.loop, first.edge, second.edge,
              to_point3(point), to_point3(point), 0.0, false, 0.0, 0, 0};
        }
      }
    }

    const auto closest = closest_pair(first, second);
    const long double distance = std::sqrt(closest.squared_distance);
    bool coincident_points = true;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const long double first_step =
          closest.first_parameter * d1[axis];
      const long double second_step =
          closest.second_parameter * d2[axis];
      const long double residual =
          closest.first_point[axis] - closest.second_point[axis];
      const long double roundoff =
          64.0L * std::numeric_limits<long double>::epsilon() *
          (std::abs(first.start[axis]) + std::abs(first_step) +
           std::abs(second.start[axis]) + std::abs(second_step));
      if (std::abs(residual) > roundoff) coincident_points = false;
    }
    if (coincident_points) {
      const bool endpoint = endpoint_parameter(
                                first, closest.first_parameter,
                                parameter_epsilon) ||
                            endpoint_parameter(
                                second, closest.second_parameter,
                                parameter_epsilon);
      return FaceBoundaryConflict{
          endpoint ? FaceBoundaryConflictKind::EndpointTouch
                   : FaceBoundaryConflictKind::ProperIntersection,
          first.loop, second.loop, first.edge, second.edge,
          to_point3(closest.first_point), to_point3(closest.second_point), 0.0,
          false, 0.0, 0, 0};
    }
    if (distance <= static_cast<long double>(linear_tolerance)) {
      return FaceBoundaryConflict{
          FaceBoundaryConflictKind::NearContact, first.loop, second.loop,
          first.edge, second.edge, to_point3(closest.first_point),
          to_point3(closest.second_point), static_cast<Scalar>(distance), false,
          0.0, 0, 0};
    }
    return std::nullopt;
  };

  struct LinearPiece {
    Point3 start;
    Point3 end;
  };
  const auto append_linear_curve_pieces =
      [&](auto &&self, CurveId curve_id, Scalar start_parameter,
          Scalar end_parameter, std::vector<LinearPiece> &pieces,
          std::size_t depth) -> bool {
    if (depth > 16 || !std::isfinite(start_parameter) ||
        !std::isfinite(end_parameter)) {
      return false;
    }
    const auto curve_it = state.curves.find(curve_id.value);
    if (curve_it == state.curves.end()) return false;
    const auto &curve = curve_it->second;
    const auto append_evaluated_piece = [&](Scalar first, Scalar second) {
      Point3 start{};
      Point3 end{};
      if (!geo_internal::evaluate_curve_point_no_cache(
              state, curve_id, first, start) ||
          !geo_internal::evaluate_curve_point_no_cache(
              state, curve_id, second, end)) {
        return false;
      }
      pieces.push_back({start, end});
      return true;
    };

    if (curve.kind == detail::CurveKind::Line ||
        curve.kind == detail::CurveKind::LineSegment) {
      return append_evaluated_piece(start_parameter, end_parameter);
    }

    std::vector<Scalar> parameters{start_parameter};
    const auto append_internal_breaks = [&](std::size_t count) {
      if (start_parameter < end_parameter) {
        for (std::size_t i = 1; i < count; ++i) {
          const auto value = static_cast<Scalar>(i);
          if (value > start_parameter && value < end_parameter)
            parameters.push_back(value);
        }
      } else {
        for (std::size_t i = count; i-- > 1;) {
          const auto value = static_cast<Scalar>(i);
          if (value < start_parameter && value > end_parameter)
            parameters.push_back(value);
        }
      }
      parameters.push_back(end_parameter);
    };

    if (curve.kind == detail::CurveKind::CompositePolyline) {
      if (curve.poles.size() < 2) return false;
      append_internal_breaks(curve.poles.size() - 1);
      for (std::size_t i = 0; i + 1 < parameters.size(); ++i) {
        if (!append_evaluated_piece(parameters[i], parameters[i + 1]))
          return false;
      }
      return true;
    }

    if (curve.kind != detail::CurveKind::CompositeChain ||
        curve.children.empty()) {
      return false;
    }
    append_internal_breaks(curve.children.size());
    for (std::size_t i = 0; i + 1 < parameters.size(); ++i) {
      const auto midpoint =
          (parameters[i] + parameters[i + 1]) * Scalar{0.5};
      const auto child_index = std::min<std::size_t>(
          static_cast<std::size_t>(
              std::max<Scalar>(0.0, std::floor(midpoint))),
          curve.children.size() - 1);
      const auto offset = static_cast<Scalar>(child_index);
      if (!self(self, curve.children[child_index],
                parameters[i] - offset, parameters[i + 1] - offset,
                pieces, depth + 1)) {
        return false;
      }
    }
    return true;
  };

  struct BoundaryEdge {
    LoopId loop;
    EdgeId edge;
    CurveId curve;
    Range1D interval;
    bool has_solver_interval {false};
    bool entirely_linear {false};
    std::vector<Segment> segments;
  };
  std::vector<BoundaryEdge> boundary_edges;

  for (std::size_t i = 0; i <= inner_loops.size(); ++i) {
    const auto loop_id = i == 0 ? outer_loop : inner_loops[i - 1];
    const auto loop_it = state.loops.find(loop_id.value);
    if (loop_it == state.loops.end()) continue;
    for (const auto coedge_id : loop_it->second.coedges) {
      const auto coedge_it = state.coedges.find(coedge_id.value);
      if (coedge_it == state.coedges.end()) continue;
      const auto edge_id = coedge_it->second.edge_id;
      const auto edge_it = state.edges.find(edge_id.value);
      if (edge_it == state.edges.end()) continue;
      const auto curve_it = state.curves.find(edge_it->second.curve_id.value);
      if (curve_it == state.curves.end()) continue;
      const auto v0 = state.vertices.find(edge_it->second.v0.value);
      const auto v1 = state.vertices.find(edge_it->second.v1.value);
      if (v0 == state.vertices.end() || v1 == state.vertices.end()) continue;
      const auto &a = v0->second.point;
      const auto &b = v1->second.point;
      if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.z) ||
          !std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z))
        continue;
      BoundaryEdge boundary{loop_id, edge_id, edge_it->second.curve_id};
      std::vector<LinearPiece> pieces;
      if (curve_it->second.kind == detail::CurveKind::Line ||
          curve_it->second.kind == detail::CurveKind::LineSegment) {
        pieces.push_back({a, b});
        boundary.entirely_linear = true;
      } else if (edge_it->second.has_parameter_interval) {
        boundary.entirely_linear = append_linear_curve_pieces(
            append_linear_curve_pieces, edge_it->second.curve_id,
            edge_it->second.start_parameter, edge_it->second.end_parameter,
            pieces, 0);
      }

      if (edge_it->second.has_parameter_interval) {
        boundary.interval = {
            std::min(edge_it->second.start_parameter,
                     edge_it->second.end_parameter),
            std::max(edge_it->second.start_parameter,
                     edge_it->second.end_parameter)};
        boundary.has_solver_interval = true;
      } else if (curve_it->second.kind == detail::CurveKind::LineSegment) {
        boundary.interval = {0.0, 1.0};
        boundary.has_solver_interval = true;
      } else if (curve_it->second.kind == detail::CurveKind::Line) {
        const auto &origin = curve_it->second.origin;
        const auto &direction = curve_it->second.direction;
        const auto parameter = [&](const Point3 &point) {
          return (point.x - origin.x) * direction.x +
                 (point.y - origin.y) * direction.y +
                 (point.z - origin.z) * direction.z;
        };
        const Scalar first_parameter = parameter(a);
        const Scalar second_parameter = parameter(b);
        boundary.interval = {std::min(first_parameter, second_parameter),
                             std::max(first_parameter, second_parameter)};
        Point3 evaluated_first{};
        Point3 evaluated_second{};
        boundary.has_solver_interval =
            std::isfinite(first_parameter) && std::isfinite(second_parameter) &&
            geo_internal::evaluate_curve_point_no_cache(
                state, boundary.curve, first_parameter, evaluated_first) &&
            geo_internal::evaluate_curve_point_no_cache(
                state, boundary.curve, second_parameter, evaluated_second);
      }

      for (std::size_t piece_index = 0; piece_index < pieces.size();
           ++piece_index) {
        const auto &piece = pieces[piece_index];
        boundary.segments.push_back(Segment{
            loop_id, edge_id,
            {piece.start.x, piece.start.y, piece.start.z},
            {piece.end.x, piece.end.y, piece.end.z},
            piece_index == 0, piece_index + 1 == pieces.size()});
      }
      boundary_edges.push_back(std::move(boundary));
    }
  }

  const auto evaluated_conflict = [&](FaceBoundaryConflictKind kind,
                                      const BoundaryEdge &first,
                                      const BoundaryEdge &second,
                                      Scalar first_parameter,
                                      Scalar second_parameter,
                                      Scalar distance,
                                      Scalar solver_tolerance,
                                      std::uint32_t evaluations,
                                      std::uint32_t rectangles)
      -> std::optional<FaceBoundaryConflict> {
    Point3 first_point{};
    Point3 second_point{};
    if (!geo_internal::evaluate_curve_point_no_cache(
            state, first.curve, first_parameter, first_point) ||
        !geo_internal::evaluate_curve_point_no_cache(
            state, second.curve, second_parameter, second_point)) {
      return std::nullopt;
    }
    return FaceBoundaryConflict{kind, first.loop, second.loop, first.edge,
                                second.edge, first_point, second_point,
                                distance, true, solver_tolerance, evaluations,
                                rectangles};
  };

  const auto result_from_curve_intersection = [&evaluated_conflict](
      const BoundaryEdge &first, const BoundaryEdge &second,
      const CurveCurveIntersectionResult &intersection, bool proximity_pass,
      Scalar solver_tolerance)
      -> std::optional<FaceBoundaryConflict> {
    if (!intersection.overlaps.empty()) {
      const auto &overlap = intersection.overlaps.front();
      const Scalar first_parameter =
          overlap.first_interval.min +
          (overlap.first_interval.max - overlap.first_interval.min) * 0.5;
      const Scalar second_parameter =
          overlap.second_interval.min +
          (overlap.second_interval.max - overlap.second_interval.min) * 0.5;
      return evaluated_conflict(
          FaceBoundaryConflictKind::CurveOverlap, first, second,
          first_parameter, second_parameter, overlap.maximum_separation,
          solver_tolerance, intersection.evaluations,
          intersection.parameter_rectangles_processed);
    }
    if (intersection.points.empty()) return std::nullopt;
    const auto &point = intersection.points.front();
    const auto kind = proximity_pass
                          ? FaceBoundaryConflictKind::NearContact
                          : (point.kind == CurveCurveIntersectionKind::Endpoint
                                 ? FaceBoundaryConflictKind::EndpointTouch
                                 : FaceBoundaryConflictKind::ProperIntersection);
    return evaluated_conflict(kind, first, second, point.first_parameter,
                              point.second_parameter,
                              point.residual_distance, solver_tolerance,
                              intersection.evaluations,
                              intersection.parameter_rectangles_processed);
  };

  for (std::size_t first_index = 0; first_index < boundary_edges.size();
       ++first_index) {
    const auto &first = boundary_edges[first_index];
    for (std::size_t second_index = first_index + 1;
         second_index < boundary_edges.size(); ++second_index) {
      const auto &second = boundary_edges[second_index];
      if (first.loop.value == second.loop.value) continue;

      if (first.entirely_linear && second.entirely_linear) {
        for (const auto &first_segment : first.segments) {
          for (const auto &second_segment : second.segments) {
            if (auto conflict = inspect_pair(first_segment, second_segment)) {
              return {StatusCode::Ok, std::move(conflict), first.edge,
                      second.edge};
            }
          }
        }
        continue;
      }

      if (!first.has_solver_interval || !second.has_solver_interval) {
        return {StatusCode::InvalidTopology, std::nullopt, first.edge,
                second.edge};
      }

      Point3 first_begin{};
      Point3 first_end{};
      Point3 second_begin{};
      Point3 second_end{};
      if (!geo_internal::evaluate_curve_point_no_cache(
              state, first.curve, first.interval.min, first_begin) ||
          !geo_internal::evaluate_curve_point_no_cache(
              state, first.curve, first.interval.max, first_end) ||
          !geo_internal::evaluate_curve_point_no_cache(
              state, second.curve, second.interval.min, second_begin) ||
          !geo_internal::evaluate_curve_point_no_cache(
              state, second.curve, second.interval.max, second_end)) {
        return {StatusCode::NumericalInstability, std::nullopt, first.edge,
                second.edge};
      }
      const auto coordinate_scale = [&]() {
        long double scale = 1.0L;
        for (const auto &point :
             {first_begin, first_end, second_begin, second_end}) {
          scale = std::max(
              scale, std::max({std::abs(static_cast<long double>(point.x)),
                               std::abs(static_cast<long double>(point.y)),
                               std::abs(static_cast<long double>(point.z))}));
        }
        return scale;
      }();
      const Scalar roundoff_floor = static_cast<Scalar>(std::min<long double>(
          linear_tolerance,
          256.0L * std::numeric_limits<Scalar>::epsilon() * coordinate_scale));
      const Scalar exact_tolerance = std::max(
          roundoff_floor,
          std::min(linear_tolerance, linear_tolerance * Scalar{1e-3}));

      CurveCurveIntersectionOptions options;
      options.position_tolerance = exact_tolerance;
      options.parameter_tolerance = 1e-10;
      options.angular_tolerance = 1e-7;
      options.max_evaluations = 200000;
      options.max_subdivisions = 100000;
      options.first_interval = first.interval;
      options.second_interval = second.interval;
      auto intersection = geo_internal::intersect_curve_curve_no_diagnostics(
          state, first.curve, second.curve, options);
      if (intersection.status != StatusCode::Ok || !intersection.value) {
        return {intersection.status, std::nullopt, first.edge, second.edge};
      }
      if (auto conflict = result_from_curve_intersection(
              first, second, *intersection.value, false, exact_tolerance)) {
        return {StatusCode::Ok, std::move(conflict), first.edge, second.edge};
      }

      if (linear_tolerance > exact_tolerance) {
        options.position_tolerance = linear_tolerance;
        intersection = geo_internal::intersect_curve_curve_no_diagnostics(
            state, first.curve, second.curve, options);
        if (intersection.status != StatusCode::Ok || !intersection.value) {
          return {intersection.status, std::nullopt, first.edge, second.edge};
        }
        if (auto conflict = result_from_curve_intersection(
                first, second, *intersection.value, true,
                linear_tolerance)) {
          return {StatusCode::Ok, std::move(conflict), first.edge,
                  second.edge};
        }
      }
    }
  }
  return {};
}

bool face_record_references_loop(const detail::FaceRecord &face,
                                 std::uint64_t loop_value) {
  if (face.outer_loop.value == loop_value) {
    return true;
  }
  return std::any_of(face.inner_loops.begin(), face.inner_loops.end(),
                     [loop_value](LoopId lid) { return lid.value == loop_value; });
}

void append_unique_raw(std::vector<std::uint64_t> &values,
                       std::uint64_t value) {
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

void rebuild_topology_indices(detail::KernelState &state) {
  state.edge_to_coedges.clear();
  state.coedge_to_loop.clear();
  state.loop_to_faces.clear();
  state.face_to_shells.clear();
  state.shell_to_bodies.clear();

  for (const auto &[coedge_value, coedge] : state.coedges) {
    state.edge_to_coedges[coedge.edge_id.value].push_back(coedge_value);
  }

  for (const auto &[loop_value, loop] : state.loops) {
    for (const auto coedge_id : loop.coedges) {
      state.coedge_to_loop[coedge_id.value] = loop_value;
    }
  }

  for (const auto &[face_value, face] : state.faces) {
    append_unique_raw(state.loop_to_faces[face.outer_loop.value], face_value);
    for (const auto loop_id : face.inner_loops) {
      append_unique_raw(state.loop_to_faces[loop_id.value], face_value);
    }
  }

  for (const auto &[shell_value, shell] : state.shells) {
    for (const auto face_id : shell.faces) {
      append_unique_raw(state.face_to_shells[face_id.value], shell_value);
    }
  }

  for (const auto &[body_value, body] : state.bodies) {
    for (const auto shell_id : body.shells) {
      append_unique_raw(state.shell_to_bodies[shell_id.value], body_value);
    }
  }
}



SurfaceId underlying_trim_base_surface_id(const detail::KernelState &state,
                                            SurfaceId start) {
  std::unordered_set<std::uint64_t> visited;
  SurfaceId cur = start;
  for (int guard = 0; guard < 64; ++guard) {
    if (!detail::has_surface(state, cur)) {
      return cur;
    }
    if (!visited.insert(cur.value).second) {
      return cur;
    }
    const auto &rec = state.surfaces.at(cur.value);
    if (rec.kind != detail::SurfaceKind::Trimmed) {
      return cur;
    }
    if (rec.base_surface_id.value == 0 ||
        !detail::has_surface(state, rec.base_surface_id)) {
      return cur;
    }
    cur = rec.base_surface_id;
  }
  return start;
}

bool finite_scalar(Scalar s) { return std::isfinite(s); }

bool accumulate_polyline_pcurve_uv_bounds(const detail::PCurveRecord &pc,
                                            bool &initialized, Scalar &u_min,
                                            Scalar &u_max, Scalar &v_min,
                                            Scalar &v_max) {
  if (pc.kind != detail::PCurveKind::Polyline || pc.poles.empty()) {
    return false;
  }
  for (const auto &p : pc.poles) {
    if (!finite_scalar(p.x) || !finite_scalar(p.y)) {
      return false;
    }
    if (!initialized) {
      u_min = u_max = p.x;
      v_min = v_max = p.y;
      initialized = true;
    } else {
      u_min = std::min(u_min, p.x);
      u_max = std::max(u_max, p.x);
      v_min = std::min(v_min, p.y);
      v_max = std::max(v_max, p.y);
    }
  }
  return initialized;
}

void append_coedge_polyline_to_uv_path(const detail::PCurveRecord &pc, bool reversed,
                                       std::vector<Point2> &out) {
  if (pc.kind != detail::PCurveKind::Polyline || pc.poles.empty()) {
    return;
  }
  auto push_pt = [&](Point2 pt) {
    if (!out.empty()) {
      const auto &prev = out.back();
      if (std::hypot(pt.x - prev.x, pt.y - prev.y) < 1e-12) {
        return;
      }
    }
    out.push_back(pt);
  };
  if (!reversed) {
    for (const auto &pt : pc.poles) {
      push_pt(pt);
    }
  } else {
    for (std::size_t ii = pc.poles.size(); ii-- > 0;) {
      push_pt(pc.poles[ii]);
    }
  }
}

void ensure_strict_increasing_range(Scalar &lo, Scalar &hi) {
  if (lo < hi) {
    return;
  }
  const Scalar mid = 0.5 * (lo + hi);
  const Scalar eps = static_cast<Scalar>(1e-9);
  lo = mid - eps;
  hi = mid + eps;
}

}  // namespace topo_internal
}  // namespace axiom
