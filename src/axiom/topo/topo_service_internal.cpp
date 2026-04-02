#include "axiom/internal/topo/topo_service_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

bool append_edge_bbox(const detail::KernelState &state, EdgeId edge_id,
                      BoundingBox &bbox) {
  const auto edge_it = state.edges.find(edge_id.value);
  if (edge_it == state.edges.end()) {
    return false;
  }
  const auto v0_it = state.vertices.find(edge_it->second.v0.value);
  const auto v1_it = state.vertices.find(edge_it->second.v1.value);
  if (v0_it == state.vertices.end() || v1_it == state.vertices.end()) {
    return false;
  }
  extend_bbox(bbox, v0_it->second.point);
  extend_bbox(bbox, v1_it->second.point);
  return true;
}

bool append_loop_bbox(const detail::KernelState &state, LoopId loop_id,
                      BoundingBox &bbox) {
  const auto loop_it = state.loops.find(loop_id.value);
  if (loop_it == state.loops.end() || loop_it->second.coedges.empty()) {
    return false;
  }
  for (const auto coedge_id : loop_it->second.coedges) {
    const auto coedge_it = state.coedges.find(coedge_id.value);
    if (coedge_it == state.coedges.end() ||
        !append_edge_bbox(state, coedge_it->second.edge_id, bbox)) {
      return false;
    }
  }
  return true;
}

bool append_face_bbox(const detail::KernelState &state, FaceId face_id,
                      BoundingBox &bbox) {
  const auto face_it = state.faces.find(face_id.value);
  if (face_it == state.faces.end()) {
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

BoundingBox compute_body_bbox(const detail::KernelState &state,
                              std::span<const ShellId> shells) {
  BoundingBox bbox{};
  for (const auto shell_id : shells) {
    const auto shell_it = state.shells.find(shell_id.value);
    if (shell_it == state.shells.end()) {
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

  if (loop.coedges.size() == 1) {
    const auto oriented = oriented_vertices(state, loop.coedges.front());
    if (!oriented.has_value()) {
      reason = "环引用了无效定向边或退化边";
      return false;
    }
    return true;
  }

  std::optional<VertexId> first_start;
  std::optional<VertexId> previous_end;
  for (const auto coedge_id : loop.coedges) {
    const auto oriented = oriented_vertices(state, coedge_id);
    if (!oriented.has_value()) {
      reason = "环引用了无效定向边或退化边";
      return false;
    }
    if (!first_start.has_value()) {
      first_start = (*oriented)[0];
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
