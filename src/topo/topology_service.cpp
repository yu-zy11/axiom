#include "axiom/topo/topology_service.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "axiom/geo/geometry_services.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"

namespace axiom {

namespace detail {

struct TopologyTransactionState {
  std::unordered_map<std::uint64_t, FaceRecord> original_faces;
  std::unordered_map<std::uint64_t, ShellRecord> original_shells;
  std::unordered_map<std::uint64_t, BodyRecord> original_bodies;

  void snapshot_face(const KernelState &state, FaceId face_id) {
    if (original_faces.find(face_id.value) != original_faces.end()) {
      return;
    }
    const auto it = state.faces.find(face_id.value);
    if (it != state.faces.end()) {
      original_faces.emplace(face_id.value, it->second);
    }
  }

  void snapshot_shell(const KernelState &state, ShellId shell_id) {
    if (original_shells.find(shell_id.value) != original_shells.end()) {
      return;
    }
    const auto it = state.shells.find(shell_id.value);
    if (it != state.shells.end()) {
      original_shells.emplace(shell_id.value, it->second);
    }
  }

  void snapshot_body(const KernelState &state, BodyId body_id) {
    if (original_bodies.find(body_id.value) != original_bodies.end()) {
      return;
    }
    const auto it = state.bodies.find(body_id.value);
    if (it != state.bodies.end()) {
      original_bodies.emplace(body_id.value, it->second);
    }
  }
};

} // namespace detail

namespace {

template <typename Id> bool has_duplicate_ids(std::span<const Id> ids) {
  std::unordered_set<std::uint64_t> seen;
  for (const auto id : ids) {
    if (!seen.insert(id.value).second) {
      return true;
    }
  }
  return false;
}

template <typename Id> void append_unique(std::vector<Id> &ids, Id id) {
  if (std::none_of(ids.begin(), ids.end(),
                   [id](Id current) { return current.value == id.value; })) {
    ids.push_back(id);
  }
}

template <typename Id>
bool contains_id(std::span<const std::uint64_t> values, Id id) {
  return std::find(values.begin(), values.end(), id.value) != values.end();
}

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

} // namespace

TopologyQueryService::TopologyQueryService(
    std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

Result<std::array<VertexId, 2>>
TopologyQueryService::vertices_of_edge(EdgeId edge_id) const {
  const auto it = state_->edges.find(edge_id.value);
  if (it == state_->edges.end()) {
    return detail::invalid_input_result<std::array<VertexId, 2>>(
        *state_, diag_codes::kCoreInvalidHandle, "边查询失败：目标边不存在",
        "边端点查询失败");
  }
  return ok_result(std::array<VertexId, 2>{it->second.v0, it->second.v1},
                   state_->create_diagnostic("已查询边端点"));
}

Result<std::vector<CoedgeId>>
TopologyQueryService::coedges_of_edge(EdgeId edge_id) const {
  if (!detail::has_edge(*state_, edge_id)) {
    return detail::invalid_input_result<std::vector<CoedgeId>>(
        *state_, diag_codes::kCoreInvalidHandle, "边查询失败：目标边不存在",
        "边定向边集合查询失败");
  }

  std::vector<CoedgeId> coedges;
  const auto index_it = state_->edge_to_coedges.find(edge_id.value);
  if (index_it != state_->edge_to_coedges.end()) {
    coedges.reserve(index_it->second.size());
    for (const auto coedge_value : index_it->second) {
      coedges.push_back(CoedgeId{coedge_value});
    }
  }
  return ok_result(coedges, state_->create_diagnostic("已查询边定向边集合"));
}

Result<std::vector<LoopId>>
TopologyQueryService::loops_of_edge(EdgeId edge_id) const {
  if (!detail::has_edge(*state_, edge_id)) {
    return detail::invalid_input_result<std::vector<LoopId>>(
        *state_, diag_codes::kCoreInvalidHandle, "边查询失败：目标边不存在",
        "边所属环集合查询失败");
  }

  std::vector<LoopId> loops;
  const auto coedges_result = coedges_of_edge(edge_id);
  if (coedges_result.status != StatusCode::Ok ||
      !coedges_result.value.has_value()) {
    return detail::failed_result<std::vector<LoopId>>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoCurveTopologyMismatch,
        "边查询失败：无法稳定解析边定向边集合", "边所属环集合查询失败");
  }
  for (const auto coedge_id : *coedges_result.value) {
    const auto loop_it = state_->coedge_to_loop.find(coedge_id.value);
    if (loop_it != state_->coedge_to_loop.end()) {
      append_unique(loops, LoopId{loop_it->second});
    }
  }
  return ok_result(loops, state_->create_diagnostic("已查询边所属环集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::faces_of_edge(EdgeId edge_id) const {
  if (!detail::has_edge(*state_, edge_id)) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kCoreInvalidHandle, "边查询失败：目标边不存在",
        "边所属面集合查询失败");
  }

  const auto loops_result = loops_of_edge(edge_id);
  if (loops_result.status != StatusCode::Ok ||
      !loops_result.value.has_value()) {
    return detail::failed_result<std::vector<FaceId>>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoCurveTopologyMismatch,
        "边查询失败：无法稳定解析边所属环", "边所属面集合查询失败");
  }

  std::vector<FaceId> faces;
  for (const auto loop_id : *loops_result.value) {
    const auto face_it = state_->loop_to_faces.find(loop_id.value);
    if (face_it == state_->loop_to_faces.end()) {
      continue;
    }
    for (const auto face_value : face_it->second) {
      append_unique(faces, FaceId{face_value});
    }
  }
  return ok_result(faces, state_->create_diagnostic("已查询边所属面集合"));
}

Result<std::vector<ShellId>>
TopologyQueryService::shells_of_edge(EdgeId edge_id) const {
  const auto faces_result = faces_of_edge(edge_id);
  if (faces_result.status != StatusCode::Ok ||
      !faces_result.value.has_value()) {
    return detail::failed_result<std::vector<ShellId>>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoCurveTopologyMismatch,
        "边查询失败：无法稳定解析边所属面集合", "边所属壳集合查询失败");
  }

  std::vector<ShellId> shells;
  for (const auto face_id : *faces_result.value) {
    const auto shell_it = state_->face_to_shells.find(face_id.value);
    if (shell_it == state_->face_to_shells.end()) {
      continue;
    }
    for (const auto shell_value : shell_it->second) {
      append_unique(shells, ShellId{shell_value});
    }
  }
  return ok_result(shells, state_->create_diagnostic("已查询边所属壳集合"));
}

Result<std::vector<EdgeId>>
TopologyQueryService::edges_of_loop(LoopId loop_id) const {
  const auto it = state_->loops.find(loop_id.value);
  if (it == state_->loops.end()) {
    return detail::invalid_input_result<std::vector<EdgeId>>(
        *state_, diag_codes::kTopoLoopNotClosed, "环查询失败：目标环不存在",
        "环边集合查询失败");
  }
  std::vector<EdgeId> edges;
  edges.reserve(it->second.coedges.size());
  for (const auto coedge_id : it->second.coedges) {
    const auto coedge_it = state_->coedges.find(coedge_id.value);
    if (coedge_it == state_->coedges.end() ||
        !detail::has_edge(*state_, coedge_it->second.edge_id)) {
      return detail::failed_result<std::vector<EdgeId>>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoLoopNotClosed,
          "环查询失败：环包含无效定向边或边引用", "环边集合查询失败");
    }
    edges.push_back(coedge_it->second.edge_id);
  }
  return ok_result(edges, state_->create_diagnostic("已查询环边集合"));
}

Result<std::vector<LoopId>>
TopologyQueryService::loops_of_face(FaceId face_id) const {
  const auto it = state_->faces.find(face_id.value);
  if (it == state_->faces.end()) {
    return detail::invalid_input_result<std::vector<LoopId>>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "面查询失败：目标面不存在", "面环集合查询失败");
  }
  auto loops = it->second.inner_loops;
  loops.insert(loops.begin(), it->second.outer_loop);
  return ok_result(loops, state_->create_diagnostic("已查询面环集合"));
}

Result<SurfaceId> TopologyQueryService::surface_of_face(FaceId face_id) const {
  const auto it = state_->faces.find(face_id.value);
  if (it == state_->faces.end()) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kTopoCurveTopologyMismatch,
        "面曲面查询失败：目标面不存在", "面曲面映射查询失败");
  }
  return ok_result(it->second.surface_id,
                   state_->create_diagnostic("已查询面曲面映射"));
}

Result<std::vector<ShellId>>
TopologyQueryService::shells_of_face(FaceId face_id) const {
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<std::vector<ShellId>>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "面查询失败：目标面不存在", "面所属壳集合查询失败");
  }

  std::vector<ShellId> shells;
  const auto shell_it = state_->face_to_shells.find(face_id.value);
  if (shell_it != state_->face_to_shells.end()) {
    for (const auto shell_value : shell_it->second) {
      shells.push_back(ShellId{shell_value});
    }
  }
  return ok_result(shells, state_->create_diagnostic("已查询面所属壳集合"));
}

Result<std::vector<BodyId>>
TopologyQueryService::bodies_of_face(FaceId face_id) const {
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<std::vector<BodyId>>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "面查询失败：目标面不存在", "面所属体集合查询失败");
  }

  std::vector<BodyId> bodies;
  const auto shells_result = shells_of_face(face_id);
  if (shells_result.status != StatusCode::Ok ||
      !shells_result.value.has_value()) {
    return detail::failed_result<std::vector<BodyId>>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "面查询失败：无法稳定解析面所属壳集合", "面所属体集合查询失败");
  }
  for (const auto shell_id : *shells_result.value) {
    const auto body_it = state_->shell_to_bodies.find(shell_id.value);
    if (body_it == state_->shell_to_bodies.end()) {
      continue;
    }
    for (const auto body_value : body_it->second) {
      append_unique(bodies, BodyId{body_value});
    }
  }
  return ok_result(bodies, state_->create_diagnostic("已查询面所属体集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::source_faces_of_face(FaceId face_id) const {
  const auto it = state_->faces.find(face_id.value);
  if (it == state_->faces.end()) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "面来源查询失败：目标面不存在", "面来源面集合查询失败");
  }
  return ok_result(it->second.source_faces,
                   state_->create_diagnostic("已查询面来源面集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::faces_of_shell(ShellId shell_id) const {
  const auto it = state_->shells.find(shell_id.value);
  if (it == state_->shells.end()) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kTopoShellNotClosed, "壳查询失败：目标壳不存在",
        "壳面集合查询失败");
  }
  return ok_result(it->second.faces,
                   state_->create_diagnostic("已查询壳面集合"));
}

Result<std::vector<BodyId>>
TopologyQueryService::bodies_of_shell(ShellId shell_id) const {
  if (!detail::has_shell(*state_, shell_id)) {
    return detail::invalid_input_result<std::vector<BodyId>>(
        *state_, diag_codes::kTopoShellNotClosed, "壳查询失败：目标壳不存在",
        "壳所属体集合查询失败");
  }

  std::vector<BodyId> bodies;
  const auto body_it = state_->shell_to_bodies.find(shell_id.value);
  if (body_it != state_->shell_to_bodies.end()) {
    for (const auto body_value : body_it->second) {
      bodies.push_back(BodyId{body_value});
    }
  }
  return ok_result(bodies, state_->create_diagnostic("已查询壳所属体集合"));
}

Result<std::vector<ShellId>>
TopologyQueryService::source_shells_of_shell(ShellId shell_id) const {
  const auto it = state_->shells.find(shell_id.value);
  if (it == state_->shells.end()) {
    return detail::invalid_input_result<std::vector<ShellId>>(
        *state_, diag_codes::kTopoShellNotClosed,
        "壳来源查询失败：目标壳不存在", "壳来源壳集合查询失败");
  }
  return ok_result(it->second.source_shells,
                   state_->create_diagnostic("已查询壳来源壳集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::source_faces_of_shell(ShellId shell_id) const {
  const auto it = state_->shells.find(shell_id.value);
  if (it == state_->shells.end()) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kTopoShellNotClosed,
        "壳来源查询失败：目标壳不存在", "壳来源面集合查询失败");
  }
  return ok_result(it->second.source_faces,
                   state_->create_diagnostic("已查询壳来源面集合"));
}

Result<std::vector<ShellId>>
TopologyQueryService::shells_of_body(BodyId body_id) const {
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<std::vector<ShellId>>(
        *state_, diag_codes::kCoreInvalidHandle, "体查询失败：目标实体不存在",
        "体壳集合查询失败");
  }
  return ok_result(it->second.shells,
                   state_->create_diagnostic("已查询体壳集合"));
}

Result<std::vector<BodyId>>
TopologyQueryService::source_bodies_of_body(BodyId body_id) const {
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<std::vector<BodyId>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体来源查询失败：目标实体不存在", "体来源体集合查询失败");
  }
  return ok_result(it->second.source_bodies,
                   state_->create_diagnostic("已查询体来源体集合"));
}

Result<std::vector<ShellId>>
TopologyQueryService::source_shells_of_body(BodyId body_id) const {
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<std::vector<ShellId>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体来源查询失败：目标实体不存在", "体来源壳集合查询失败");
  }
  return ok_result(it->second.source_shells,
                   state_->create_diagnostic("已查询体来源壳集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::source_faces_of_body(BodyId body_id) const {
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体来源查询失败：目标实体不存在", "体来源面集合查询失败");
  }
  return ok_result(it->second.source_faces,
                   state_->create_diagnostic("已查询体来源面集合"));
}

Result<TopologySummary>
TopologyQueryService::summary_of_shell(ShellId shell_id) const {
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::invalid_input_result<TopologySummary>(
        *state_, diag_codes::kTopoShellNotClosed,
        "壳拓扑摘要查询失败：目标壳不存在", "壳拓扑摘要查询失败");
  }
  TopologySummary summary{};
  summary.shell_count = 1;
  summary.face_count =
      static_cast<std::uint64_t>(shell_it->second.faces.size());
  std::unordered_set<std::uint64_t> loops;
  std::unordered_set<std::uint64_t> edges;
  std::unordered_set<std::uint64_t> vertices;
  for (const auto face_id : shell_it->second.faces) {
    const auto face_it = state_->faces.find(face_id.value);
    if (face_it == state_->faces.end()) {
      continue;
    }
    loops.insert(face_it->second.outer_loop.value);
    for (const auto loop_id : face_it->second.inner_loops)
      loops.insert(loop_id.value);
  }
  for (const auto loop_value : loops) {
    const auto loop_it = state_->loops.find(loop_value);
    if (loop_it == state_->loops.end())
      continue;
    for (const auto coedge_id : loop_it->second.coedges) {
      const auto coedge_it = state_->coedges.find(coedge_id.value);
      if (coedge_it == state_->coedges.end())
        continue;
      const auto edge_id = coedge_it->second.edge_id.value;
      edges.insert(edge_id);
      const auto edge_it = state_->edges.find(edge_id);
      if (edge_it == state_->edges.end())
        continue;
      vertices.insert(edge_it->second.v0.value);
      vertices.insert(edge_it->second.v1.value);
    }
  }
  summary.loop_count = static_cast<std::uint64_t>(loops.size());
  summary.edge_count = static_cast<std::uint64_t>(edges.size());
  summary.vertex_count = static_cast<std::uint64_t>(vertices.size());
  return ok_result(summary, state_->create_diagnostic("已查询壳拓扑摘要"));
}

Result<TopologySummary>
TopologyQueryService::summary_of_body(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::invalid_input_result<TopologySummary>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体拓扑摘要查询失败：目标体不存在", "体拓扑摘要查询失败");
  }
  TopologySummary summary{};
  summary.shell_count =
      static_cast<std::uint64_t>(body_it->second.shells.size());
  for (const auto shell_id : body_it->second.shells) {
    const auto shell_summary = summary_of_shell(shell_id);
    if (shell_summary.status != StatusCode::Ok ||
        !shell_summary.value.has_value()) {
      return error_result<TopologySummary>(shell_summary.status,
                                           shell_summary.diagnostic_id);
    }
    summary.face_count += shell_summary.value->face_count;
    summary.loop_count += shell_summary.value->loop_count;
    summary.edge_count += shell_summary.value->edge_count;
    summary.vertex_count += shell_summary.value->vertex_count;
  }
  return ok_result(summary, state_->create_diagnostic("已查询体拓扑摘要"));
}

Result<std::uint64_t>
TopologyQueryService::edge_count_of_loop(LoopId loop_id) const {
  const auto edges = edges_of_loop(loop_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(edges.value->size()),
      state_->create_diagnostic("已查询环边数量"));
}

Result<std::uint64_t>
TopologyQueryService::loop_count_of_face(FaceId face_id) const {
  const auto loops = loops_of_face(face_id);
  if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
    return error_result<std::uint64_t>(loops.status, loops.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(loops.value->size()),
      state_->create_diagnostic("已查询面环数量"));
}

Result<std::uint64_t>
TopologyQueryService::face_count_of_shell(ShellId shell_id) const {
  const auto faces = faces_of_shell(shell_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<std::uint64_t>(faces.status, faces.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(faces.value->size()),
      state_->create_diagnostic("已查询壳面数量"));
}

Result<std::uint64_t>
TopologyQueryService::shell_count_of_body(BodyId body_id) const {
  const auto shells = shells_of_body(body_id);
  if (shells.status != StatusCode::Ok || !shells.value.has_value()) {
    return error_result<std::uint64_t>(shells.status, shells.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(shells.value->size()),
      state_->create_diagnostic("已查询体壳数量"));
}

Result<std::uint64_t>
TopologyQueryService::coedge_count_of_edge(EdgeId edge_id) const {
  const auto coedges = coedges_of_edge(edge_id);
  if (coedges.status != StatusCode::Ok || !coedges.value.has_value()) {
    return error_result<std::uint64_t>(coedges.status, coedges.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(coedges.value->size()),
      state_->create_diagnostic("已查询边定向边数量"));
}

Result<std::uint64_t>
TopologyQueryService::owner_count_of_edge(EdgeId edge_id) const {
  const auto shells = shells_of_edge(edge_id);
  if (shells.status != StatusCode::Ok || !shells.value.has_value()) {
    return error_result<std::uint64_t>(shells.status, shells.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(shells.value->size()),
      state_->create_diagnostic("已查询边壳归属数量"));
}

Result<std::uint64_t>
TopologyQueryService::owner_count_of_face(FaceId face_id) const {
  const auto shells = shells_of_face(face_id);
  if (shells.status != StatusCode::Ok || !shells.value.has_value()) {
    return error_result<std::uint64_t>(shells.status, shells.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(shells.value->size()),
      state_->create_diagnostic("已查询面壳归属数量"));
}

Result<std::uint64_t>
TopologyQueryService::owner_count_of_shell(ShellId shell_id) const {
  const auto bodies = bodies_of_shell(shell_id);
  if (bodies.status != StatusCode::Ok || !bodies.value.has_value()) {
    return error_result<std::uint64_t>(bodies.status, bodies.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(bodies.value->size()),
      state_->create_diagnostic("已查询壳体归属数量"));
}

Result<bool> TopologyQueryService::has_vertex(VertexId vertex_id) const {
  return ok_result(detail::has_vertex(*state_, vertex_id),
                   state_->create_diagnostic("已查询顶点存在性"));
}

Result<bool> TopologyQueryService::has_edge(EdgeId edge_id) const {
  return ok_result(detail::has_edge(*state_, edge_id),
                   state_->create_diagnostic("已查询边存在性"));
}

Result<bool> TopologyQueryService::has_loop(LoopId loop_id) const {
  return ok_result(detail::has_loop(*state_, loop_id),
                   state_->create_diagnostic("已查询环存在性"));
}

Result<bool> TopologyQueryService::has_face(FaceId face_id) const {
  return ok_result(detail::has_face(*state_, face_id),
                   state_->create_diagnostic("已查询面存在性"));
}

Result<bool> TopologyQueryService::has_shell(ShellId shell_id) const {
  return ok_result(detail::has_shell(*state_, shell_id),
                   state_->create_diagnostic("已查询壳存在性"));
}

Result<bool> TopologyQueryService::has_body(BodyId body_id) const {
  return ok_result(detail::has_body(*state_, body_id),
                   state_->create_diagnostic("已查询体存在性"));
}

Result<bool> TopologyQueryService::is_edge_boundary(EdgeId edge_id) const {
  const auto count = owner_count_of_edge(edge_id);
  if (count.status != StatusCode::Ok || !count.value.has_value()) {
    return error_result<bool>(count.status, count.diagnostic_id);
  }
  return ok_result(*count.value == 1,
                   state_->create_diagnostic("已查询边是否边界边"));
}

Result<bool> TopologyQueryService::is_edge_non_manifold(EdgeId edge_id) const {
  const auto count = owner_count_of_edge(edge_id);
  if (count.status != StatusCode::Ok || !count.value.has_value()) {
    return error_result<bool>(count.status, count.diagnostic_id);
  }
  return ok_result(*count.value > 2,
                   state_->create_diagnostic("已查询边是否非流形"));
}

Result<bool> TopologyQueryService::is_face_orphan(FaceId face_id) const {
  const auto owners = owner_count_of_face(face_id);
  if (owners.status != StatusCode::Ok || !owners.value.has_value()) {
    return error_result<bool>(owners.status, owners.diagnostic_id);
  }
  return ok_result(*owners.value == 0,
                   state_->create_diagnostic("已查询面是否孤立"));
}

Result<bool> TopologyQueryService::is_shell_orphan(ShellId shell_id) const {
  const auto owners = owner_count_of_shell(shell_id);
  if (owners.status != StatusCode::Ok || !owners.value.has_value()) {
    return error_result<bool>(owners.status, owners.diagnostic_id);
  }
  return ok_result(*owners.value == 0,
                   state_->create_diagnostic("已查询壳是否孤立"));
}

Result<bool> TopologyQueryService::is_body_derived(BodyId body_id) const {
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体派生查询失败：目标实体不存在", "体派生查询失败");
  }
  const bool derived = !it->second.source_bodies.empty() ||
                       !it->second.source_shells.empty() ||
                       !it->second.source_faces.empty();
  return ok_result(derived, state_->create_diagnostic("已查询体派生属性"));
}

Result<BoundingBox> TopologyQueryService::bbox_of_face(FaceId face_id) const {
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<BoundingBox>(
        *state_, diag_codes::kCoreInvalidHandle,
        "面包围盒查询失败：目标面不存在", "面包围盒查询失败");
  }
  BoundingBox bbox{};
  if (!append_face_bbox(*state_, face_id, bbox) || !bbox.is_valid) {
    return detail::failed_result<BoundingBox>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoFaceOuterLoopInvalid, "面包围盒查询失败：面拓扑不完整",
        "面包围盒查询失败");
  }
  return ok_result(bbox, state_->create_diagnostic("已查询面包围盒"));
}

Result<BoundingBox>
TopologyQueryService::bbox_of_shell(ShellId shell_id) const {
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::invalid_input_result<BoundingBox>(
        *state_, diag_codes::kCoreInvalidHandle,
        "壳包围盒查询失败：目标壳不存在", "壳包围盒查询失败");
  }
  BoundingBox bbox{};
  for (const auto face_id : shell_it->second.faces) {
    if (!append_face_bbox(*state_, face_id, bbox)) {
      return detail::failed_result<BoundingBox>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "壳包围盒查询失败：壳面拓扑不完整", "壳包围盒查询失败");
    }
  }
  if (!bbox.is_valid) {
    return detail::failed_result<BoundingBox>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "壳包围盒查询失败：壳不包含可用拓扑", "壳包围盒查询失败");
  }
  return ok_result(bbox, state_->create_diagnostic("已查询壳包围盒"));
}

Result<BoundingBox>
TopologyQueryService::bbox_of_body_from_topology(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::invalid_input_result<BoundingBox>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体拓扑包围盒查询失败：目标体不存在", "体拓扑包围盒查询失败");
  }
  const auto bbox = compute_body_bbox(*state_, body_it->second.shells);
  if (!bbox.is_valid) {
    return detail::failed_result<BoundingBox>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "体拓扑包围盒查询失败：体拓扑不完整", "体拓扑包围盒查询失败");
  }
  return ok_result(bbox, state_->create_diagnostic("已查询体拓扑包围盒"));
}

Result<std::vector<FaceId>>
TopologyQueryService::faces_of_body(BodyId body_id) const {
  const auto shells = shells_of_body(body_id);
  if (shells.status != StatusCode::Ok || !shells.value.has_value()) {
    return error_result<std::vector<FaceId>>(shells.status,
                                             shells.diagnostic_id);
  }
  std::vector<FaceId> out;
  for (const auto shell_id : *shells.value) {
    const auto faces = faces_of_shell(shell_id);
    if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
      return error_result<std::vector<FaceId>>(faces.status,
                                               faces.diagnostic_id);
    }
    for (const auto face_id : *faces.value)
      append_unique(out, face_id);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询体的全部面"));
}

Result<std::vector<LoopId>>
TopologyQueryService::loops_of_body(BodyId body_id) const {
  const auto faces = faces_of_body(body_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<std::vector<LoopId>>(faces.status, faces.diagnostic_id);
  }
  std::vector<LoopId> out;
  for (const auto face_id : *faces.value) {
    const auto loops = loops_of_face(face_id);
    if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
      return error_result<std::vector<LoopId>>(loops.status,
                                               loops.diagnostic_id);
    }
    for (const auto loop_id : *loops.value)
      append_unique(out, loop_id);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询体的全部环"));
}

Result<std::vector<EdgeId>>
TopologyQueryService::edges_of_body(BodyId body_id) const {
  const auto loops = loops_of_body(body_id);
  if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
    return error_result<std::vector<EdgeId>>(loops.status, loops.diagnostic_id);
  }
  std::vector<EdgeId> out;
  for (const auto loop_id : *loops.value) {
    const auto edges = edges_of_loop(loop_id);
    if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
      return error_result<std::vector<EdgeId>>(edges.status,
                                               edges.diagnostic_id);
    }
    for (const auto edge_id : *edges.value)
      append_unique(out, edge_id);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询体的全部边"));
}

Result<std::vector<VertexId>>
TopologyQueryService::vertices_of_body(BodyId body_id) const {
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::vector<VertexId>>(edges.status,
                                               edges.diagnostic_id);
  }
  std::vector<VertexId> out;
  for (const auto edge_id : *edges.value) {
    const auto vertices = vertices_of_edge(edge_id);
    if (vertices.status != StatusCode::Ok || !vertices.value.has_value()) {
      return error_result<std::vector<VertexId>>(vertices.status,
                                                 vertices.diagnostic_id);
    }
    append_unique(out, vertices.value->at(0));
    append_unique(out, vertices.value->at(1));
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询体的全部顶点"));
}

Result<std::uint64_t>
TopologyQueryService::face_count_of_body(BodyId body_id) const {
  const auto faces = faces_of_body(body_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<std::uint64_t>(faces.status, faces.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(faces.value->size()),
      state_->create_diagnostic("已查询体面数量"));
}

Result<std::uint64_t>
TopologyQueryService::loop_count_of_body(BodyId body_id) const {
  const auto loops = loops_of_body(body_id);
  if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
    return error_result<std::uint64_t>(loops.status, loops.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(loops.value->size()),
      state_->create_diagnostic("已查询体环数量"));
}

Result<std::uint64_t>
TopologyQueryService::edge_count_of_body(BodyId body_id) const {
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(edges.value->size()),
      state_->create_diagnostic("已查询体边数量"));
}

Result<std::uint64_t>
TopologyQueryService::vertex_count_of_body(BodyId body_id) const {
  const auto vertices = vertices_of_body(body_id);
  if (vertices.status != StatusCode::Ok || !vertices.value.has_value()) {
    return error_result<std::uint64_t>(vertices.status, vertices.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(vertices.value->size()),
      state_->create_diagnostic("已查询体顶点数量"));
}

Result<bool> TopologyQueryService::body_has_face(BodyId body_id,
                                                 FaceId face_id) const {
  const auto faces = faces_of_body(body_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<bool>(faces.status, faces.diagnostic_id);
  }
  return ok_result(
      std::any_of(faces.value->begin(), faces.value->end(),
                  [face_id](FaceId id) { return id.value == face_id.value; }),
      state_->create_diagnostic("已查询体是否包含面"));
}

Result<bool> TopologyQueryService::shell_has_face(ShellId shell_id,
                                                  FaceId face_id) const {
  const auto faces = faces_of_shell(shell_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<bool>(faces.status, faces.diagnostic_id);
  }
  return ok_result(
      std::any_of(faces.value->begin(), faces.value->end(),
                  [face_id](FaceId id) { return id.value == face_id.value; }),
      state_->create_diagnostic("已查询壳是否包含面"));
}

Result<bool> TopologyQueryService::face_has_loop(FaceId face_id,
                                                 LoopId loop_id) const {
  const auto loops = loops_of_face(face_id);
  if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
    return error_result<bool>(loops.status, loops.diagnostic_id);
  }
  return ok_result(
      std::any_of(loops.value->begin(), loops.value->end(),
                  [loop_id](LoopId id) { return id.value == loop_id.value; }),
      state_->create_diagnostic("已查询面是否包含环"));
}

Result<bool> TopologyQueryService::loop_has_edge(LoopId loop_id,
                                                 EdgeId edge_id) const {
  const auto edges = edges_of_loop(loop_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<bool>(edges.status, edges.diagnostic_id);
  }
  return ok_result(
      std::any_of(edges.value->begin(), edges.value->end(),
                  [edge_id](EdgeId id) { return id.value == edge_id.value; }),
      state_->create_diagnostic("已查询环是否包含边"));
}

Result<bool> TopologyQueryService::edge_has_vertex(EdgeId edge_id,
                                                   VertexId vertex_id) const {
  const auto vertices = vertices_of_edge(edge_id);
  if (vertices.status != StatusCode::Ok || !vertices.value.has_value()) {
    return error_result<bool>(vertices.status, vertices.diagnostic_id);
  }
  return ok_result(vertices.value->at(0).value == vertex_id.value ||
                       vertices.value->at(1).value == vertex_id.value,
                   state_->create_diagnostic("已查询边是否包含顶点"));
}

Result<std::uint64_t>
TopologyQueryService::shared_face_count_of_body(BodyId body_id) const {
  const auto faces = faces_of_body(body_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<std::uint64_t>(faces.status, faces.diagnostic_id);
  }
  std::uint64_t count = 0;
  for (const auto face_id : *faces.value) {
    const auto owners = owner_count_of_face(face_id);
    if (owners.status != StatusCode::Ok || !owners.value.has_value()) {
      return error_result<std::uint64_t>(owners.status, owners.diagnostic_id);
    }
    if (*owners.value > 1)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计体共享面数量"));
}

Result<std::uint64_t>
TopologyQueryService::shared_edge_count_of_body(BodyId body_id) const {
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  std::uint64_t count = 0;
  for (const auto edge_id : *edges.value) {
    const auto owners = owner_count_of_edge(edge_id);
    if (owners.status != StatusCode::Ok || !owners.value.has_value()) {
      return error_result<std::uint64_t>(owners.status, owners.diagnostic_id);
    }
    if (*owners.value > 1)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计体共享边数量"));
}

Result<std::uint64_t>
TopologyQueryService::boundary_edge_count_of_body(BodyId body_id) const {
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  std::uint64_t count = 0;
  for (const auto edge_id : *edges.value) {
    const auto is_boundary = is_edge_boundary(edge_id);
    if (is_boundary.status != StatusCode::Ok ||
        !is_boundary.value.has_value()) {
      return error_result<std::uint64_t>(is_boundary.status,
                                         is_boundary.diagnostic_id);
    }
    if (*is_boundary.value)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计体边界边数量"));
}

Result<std::uint64_t>
TopologyQueryService::non_manifold_edge_count_of_body(BodyId body_id) const {
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  std::uint64_t count = 0;
  for (const auto edge_id : *edges.value) {
    const auto is_non_manifold = is_edge_non_manifold(edge_id);
    if (is_non_manifold.status != StatusCode::Ok ||
        !is_non_manifold.value.has_value()) {
      return error_result<std::uint64_t>(is_non_manifold.status,
                                         is_non_manifold.diagnostic_id);
    }
    if (*is_non_manifold.value)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计体非流形边数量"));
}

Result<bool>
TopologyQueryService::is_body_topology_empty(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体拓扑空性查询失败：目标体不存在", "体拓扑空性查询失败");
  }
  return ok_result(body_it->second.shells.empty(),
                   state_->create_diagnostic("已查询体拓扑是否为空"));
}

Result<PCurveId> TopologyQueryService::pcurve_of_coedge(CoedgeId coedge_id) const {
  const auto it = state_->coedges.find(coedge_id.value);
  if (it == state_->coedges.end()) {
    return detail::invalid_input_result<PCurveId>(
        *state_, diag_codes::kCoreInvalidHandle,
        "共边参数曲线查询失败：目标定向边不存在", "共边参数曲线查询失败");
  }
  return ok_result(it->second.pcurve_id,
                   state_->create_diagnostic("已查询共边参数曲线"));
}

namespace {

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

void ensure_strict_increasing_range(Scalar &lo, Scalar &hi) {
  if (lo < hi) {
    return;
  }
  const Scalar mid = 0.5 * (lo + hi);
  const Scalar eps = static_cast<Scalar>(1e-9);
  lo = mid - eps;
  hi = mid + eps;
}

} // namespace

Result<SurfaceId>
TopologyQueryService::underlying_surface_for_face_trim(FaceId face_id) const {
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kCoreInvalidHandle,
        "面修剪基曲面查询失败：目标面不存在", "面修剪基曲面查询失败");
  }
  const auto &face = state_->faces.at(face_id.value);
  if (!detail::has_surface(*state_, face.surface_id)) {
    return error_result<SurfaceId>(
        StatusCode::InvalidTopology,
        detail::error_diag(
            *state_, "面修剪基曲面查询失败", diag_codes::kTopoFaceOuterLoopInvalid,
            "面修剪基曲面查询失败：面引用的曲面不存在",
            std::vector<std::uint64_t>{face_id.value, face.surface_id.value}));
  }
  const SurfaceId base =
      underlying_trim_base_surface_id(*state_, face.surface_id);
  return ok_result(base, state_->create_diagnostic("已查询面修剪用底层曲面"));
}

Result<Range2D>
TopologyQueryService::face_outer_loop_uv_bounds(FaceId face_id) const {
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<Range2D>(
        *state_, diag_codes::kCoreInvalidHandle,
        "面外环 UV 包围查询失败：目标面不存在", "面外环 UV 包围查询失败");
  }
  const auto &face = state_->faces.at(face_id.value);
  const auto loop_it = state_->loops.find(face.outer_loop.value);
  if (loop_it == state_->loops.end() || loop_it->second.coedges.empty()) {
    return error_result<Range2D>(
        StatusCode::InvalidTopology,
        detail::error_diag(
            *state_, "面外环 UV 包围查询失败", diag_codes::kTopoFaceOuterLoopInvalid,
            "面外环 UV 包围查询失败：外环不存在或为空",
            std::vector<std::uint64_t>{face_id.value, face.outer_loop.value}));
  }
  bool initialized = false;
  Scalar u_min = 0.0;
  Scalar u_max = 0.0;
  Scalar v_min = 0.0;
  Scalar v_max = 0.0;
  for (const auto coedge_id : loop_it->second.coedges) {
    const auto ce_it = state_->coedges.find(coedge_id.value);
    if (ce_it == state_->coedges.end()) {
      return error_result<Range2D>(
          StatusCode::InvalidTopology,
          detail::error_diag(
              *state_, "面外环 UV 包围查询失败", diag_codes::kTopoFaceOuterLoopInvalid,
              "面外环 UV 包围查询失败：共边记录缺失",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value}));
    }
    const PCurveId pc_id = ce_it->second.pcurve_id;
    if (pc_id.value == 0 || !detail::has_pcurve(*state_, pc_id)) {
      return error_result<Range2D>(
          StatusCode::InvalidTopology,
          detail::error_diag(
              *state_, "面外环 UV 包围查询失败", diag_codes::kTopoRelationInconsistent,
              "面外环 UV 包围查询失败：共边未绑定有效 PCurve",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value,
                                           pc_id.value}));
    }
    const auto &pc = state_->pcurves.at(pc_id.value);
    if (!accumulate_polyline_pcurve_uv_bounds(pc, initialized, u_min, u_max,
                                               v_min, v_max)) {
      return error_result<Range2D>(
          StatusCode::NotImplemented,
          detail::error_diag(
              *state_, "面外环 UV 包围查询失败", diag_codes::kCoreOperationUnsupported,
              "面外环 UV 包围查询失败：暂仅支持折线型 PCurve 且须含有效顶点",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value,
                                           pc_id.value}));
    }
  }
  if (!initialized) {
    return error_result<Range2D>(
        StatusCode::DegenerateGeometry,
        detail::error_diag(
            *state_, "面外环 UV 包围查询失败", diag_codes::kGeoDegenerateGeometry,
            "面外环 UV 包围查询失败：未能从 PCurve 得到有效范围",
            std::vector<std::uint64_t>{face_id.value}));
  }
  ensure_strict_increasing_range(u_min, u_max);
  ensure_strict_increasing_range(v_min, v_max);
  return ok_result(Range2D{Range1D{u_min, u_max}, Range1D{v_min, v_max}},
                   state_->create_diagnostic("已计算面外环 UV 轴对齐包围"));
}

TopologyTransaction::TopologyTransaction(
    std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)),
      transaction_state_(std::make_shared<detail::TopologyTransactionState>()) {
}

Result<VertexId> TopologyTransaction::create_vertex(const Point3 &point) {
  if (!active_) {
    return detail::failed_result<VertexId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "创建顶点失败：事务已关闭", "创建顶点失败");
  }
  const auto id = VertexId{state_->allocate_id()};
  state_->vertices.emplace(id.value, detail::VertexRecord{point});
  created_vertices_.push_back(id.value);
  return ok_result(id, state_->create_diagnostic("已创建顶点"));
}

Result<EdgeId> TopologyTransaction::create_edge(CurveId curve_id, VertexId v0,
                                                VertexId v1) {
  if (!active_ || !detail::has_curve(*state_, curve_id) ||
      !detail::has_vertex(*state_, v0) || !detail::has_vertex(*state_, v1) ||
      v0.value == v1.value) {
    return detail::invalid_input_result<EdgeId>(
        *state_, diag_codes::kCoreInvalidHandle,
        "创建边失败：事务已关闭、目标曲线不存在、端点不存在或两个端点重合",
        "创建边失败");
  }
  const auto id = EdgeId{state_->allocate_id()};
  state_->edges.emplace(id.value, detail::EdgeRecord{curve_id, v0, v1});
  created_edges_.push_back(id.value);
  return ok_result(id, state_->create_diagnostic("已创建边"));
}

Result<CoedgeId> TopologyTransaction::create_coedge(EdgeId edge_id,
                                                    bool reversed) {
  if (!active_ || state_->edges.find(edge_id.value) == state_->edges.end()) {
    return detail::invalid_input_result<CoedgeId>(
        *state_, diag_codes::kCoreInvalidHandle,
        "创建定向边失败：事务已关闭或目标边不存在", "创建定向边失败");
  }
  const auto id = CoedgeId{state_->allocate_id()};
  state_->coedges.emplace(id.value, detail::CoedgeRecord{edge_id, reversed, {}});
  created_coedges_.push_back(id.value);
  rebuild_topology_indices(*state_);
  return ok_result(id, state_->create_diagnostic("已创建定向边"));
}

Result<void> TopologyTransaction::set_coedge_pcurve(CoedgeId coedge_id,
                                                    PCurveId pcurve_id) {
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxCommitFailure,
                               "绑定共边参数曲线失败：事务已关闭",
                               "绑定共边参数曲线失败");
  }
  auto it = state_->coedges.find(coedge_id.value);
  if (it == state_->coedges.end()) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "绑定共边参数曲线失败：目标定向边不存在",
                                      "绑定共边参数曲线失败");
  }
  if (pcurve_id.value != 0 && !detail::has_pcurve(*state_, pcurve_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "绑定共边参数曲线失败：目标参数曲线不存在",
                                      "绑定共边参数曲线失败");
  }
  it->second.pcurve_id = pcurve_id;
  rebuild_topology_indices(*state_);
  ++txn_coedge_pcurve_binds_;
  return ok_void(state_->create_diagnostic("已绑定共边参数曲线"));
}

Result<LoopId>
TopologyTransaction::create_loop(std::span<const CoedgeId> coedges) {
  if (!active_ || coedges.empty()) {
    return detail::failed_result<LoopId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "创建环失败：事务已关闭或输入定向边集合为空", "创建环失败");
  }

  for (const auto coedge_id : coedges) {
    if (state_->coedges.find(coedge_id.value) == state_->coedges.end()) {
      return detail::invalid_input_result<LoopId>(
          *state_, diag_codes::kTopoLoopNotClosed, "创建环失败：存在无效定向边",
          "创建环失败");
    }
  }
  for (const auto coedge_id : coedges) {
    if (state_->coedge_to_loop.find(coedge_id.value) != state_->coedge_to_loop.end()) {
      return detail::failed_result<LoopId>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoCoedgeAlreadyOwned,
          "创建环失败：定向边已属于其他环", "创建环失败");
    }
  }
  if (has_duplicate_ids(coedges)) {
    return detail::failed_result<LoopId>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoLoopNotClosed,
        "创建环失败：定向边序列包含重复引用", "创建环失败");
  }
  const auto id = LoopId{state_->allocate_id()};
  detail::LoopRecord loop{
      std::vector<CoedgeId>(coedges.begin(), coedges.end())};
  std::string reason;
  if (!validate_loop_record(*state_, loop, reason)) {
    std::string_view fail_code = diag_codes::kTopoLoopNotClosed;
    if (reason == "环包含重复边引用") {
      fail_code = diag_codes::kTopoLoopDuplicateEdge;
    }
    return detail::failed_result<LoopId>(*state_, StatusCode::InvalidTopology, fail_code,
                                         "创建环失败：" + reason, "创建环失败");
  }
  state_->loops.emplace(id.value, std::move(loop));
  created_loops_.push_back(id.value);
  rebuild_topology_indices(*state_);
  return ok_result(id, state_->create_diagnostic("已创建环"));
}

Result<FaceId>
TopologyTransaction::create_face(SurfaceId surface_id, LoopId outer_loop,
                                 std::span<const LoopId> inner_loops) {
  if (!active_ || !detail::has_surface(*state_, surface_id) ||
      !detail::has_loop(*state_, outer_loop)) {
    return detail::invalid_input_result<FaceId>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "创建面失败：事务已关闭、目标曲面不存在或外环不存在", "创建面失败");
  }

  for (const auto loop_id : inner_loops) {
    if (!detail::has_loop(*state_, loop_id)) {
      return detail::invalid_input_result<FaceId>(
          *state_, diag_codes::kTopoFaceOuterLoopInvalid,
          "创建面失败：存在无效内环", "创建面失败");
    }
  }
  if (has_duplicate_ids(inner_loops)) {
    return detail::failed_result<FaceId>(*state_, StatusCode::InvalidTopology,
                                         diag_codes::kTopoFaceOuterLoopInvalid,
                                         "创建面失败：内环列表包含重复引用",
                                         "创建面失败");
  }
  if (std::any_of(inner_loops.begin(), inner_loops.end(),
                  [outer_loop](LoopId loop_id) {
                    return loop_id.value == outer_loop.value;
                  })) {
    return detail::failed_result<FaceId>(*state_, StatusCode::InvalidTopology,
                                         diag_codes::kTopoFaceOuterLoopInvalid,
                                         "创建面失败：内环不能与外环相同",
                                         "创建面失败");
  }
  std::string reason;
  if (!validate_loop_id(*state_, outer_loop, reason)) {
    return detail::failed_result<FaceId>(*state_, StatusCode::InvalidTopology,
                                         diag_codes::kTopoFaceOuterLoopInvalid,
                                         "创建面失败：外环非法，" + reason,
                                         "创建面失败");
  }
  for (const auto loop_id : inner_loops) {
    if (!validate_loop_id(*state_, loop_id, reason)) {
      return detail::failed_result<FaceId>(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoFaceOuterLoopInvalid,
          "创建面失败：内环非法，" + reason, "创建面失败");
    }
  }
  // 7.2: each LoopId may belong to at most one Face (outer or inner lists).
  const auto outer_used = state_->loop_to_faces.find(outer_loop.value);
  if (outer_used != state_->loop_to_faces.end() &&
      !outer_used->second.empty()) {
    return error_result<FaceId>(
        StatusCode::InvalidTopology,
        detail::error_diag(
            *state_, "创建面失败", diag_codes::kTopoFaceOuterLoopInvalid,
            "创建面失败：外环已被其他面引用",
            std::vector<std::uint64_t>{outer_loop.value,
                                       outer_used->second.front()}));
  }
  for (const auto loop_id : inner_loops) {
    const auto inner_used = state_->loop_to_faces.find(loop_id.value);
    if (inner_used != state_->loop_to_faces.end() &&
        !inner_used->second.empty()) {
      return error_result<FaceId>(
          StatusCode::InvalidTopology,
          detail::error_diag(
              *state_, "创建面失败", diag_codes::kTopoFaceInnerLoopInvalid,
              "创建面失败：内环已被其他面引用",
              std::vector<std::uint64_t>{loop_id.value,
                                         inner_used->second.front()}));
    }
  }
  const auto id = FaceId{state_->allocate_id()};
  detail::FaceRecord face_record;
  face_record.surface_id = surface_id;
  face_record.outer_loop = outer_loop;
  face_record.inner_loops.assign(inner_loops.begin(), inner_loops.end());
  face_record.source_faces.push_back(id);
  state_->faces.emplace(id.value, std::move(face_record));
  created_faces_.push_back(id.value);
  rebuild_topology_indices(*state_);
  return ok_result(id, state_->create_diagnostic("已创建面"));
}

Result<ShellId>
TopologyTransaction::create_shell(std::span<const FaceId> faces) {
  if (!active_ || faces.empty()) {
    return detail::failed_result<ShellId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "创建壳失败：事务已关闭或输入面集合为空", "创建壳失败");
  }

  for (const auto face_id : faces) {
    if (!detail::has_face(*state_, face_id)) {
      return detail::invalid_input_result<ShellId>(
          *state_, diag_codes::kTopoShellNotClosed, "创建壳失败：存在无效面",
          "创建壳失败");
    }
  }
  if (has_duplicate_ids(faces)) {
    return detail::failed_result<ShellId>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoDuplicateFaceInShell,
        "创建壳失败：面列表包含重复引用", "创建壳失败");
  }
  for (const auto face_id : faces) {
    const auto face_it = state_->faces.find(face_id.value);
    std::string reason;
    if (face_it == state_->faces.end() ||
        !validate_loop_id(*state_, face_it->second.outer_loop, reason)) {
      return detail::failed_result<ShellId>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "创建壳失败：包含非法面拓扑", "创建壳失败");
    }
  }
  const auto id = ShellId{state_->allocate_id()};
  detail::ShellRecord shell_record;
  shell_record.faces.assign(faces.begin(), faces.end());
  for (const auto face_id : faces) {
    const auto face_it = state_->faces.find(face_id.value);
    if (face_it == state_->faces.end()) {
      continue;
    }
    if (face_it->second.source_faces.empty()) {
      append_unique(shell_record.source_faces, face_id);
      continue;
    }
    for (const auto source_face_id : face_it->second.source_faces) {
      append_unique(shell_record.source_faces, source_face_id);
    }
    const auto source_shell_it = state_->face_to_shells.find(face_id.value);
    if (source_shell_it != state_->face_to_shells.end()) {
      for (const auto source_shell_value : source_shell_it->second) {
        append_unique(shell_record.source_shells, ShellId{source_shell_value});
      }
    }
  }
  state_->shells.emplace(id.value, std::move(shell_record));
  created_shells_.push_back(id.value);
  rebuild_topology_indices(*state_);
  return ok_result(id, state_->create_diagnostic("已创建壳"));
}

Result<BodyId>
TopologyTransaction::create_body(std::span<const ShellId> shells) {
  if (!active_ || shells.empty()) {
    return detail::failed_result<BodyId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "创建体失败：事务已关闭或输入壳集合为空", "创建体失败");
  }

  for (const auto shell_id : shells) {
    if (!detail::has_shell(*state_, shell_id)) {
      return detail::invalid_input_result<BodyId>(
          *state_, diag_codes::kTopoShellNotClosed, "创建体失败：存在无效壳",
          "创建体失败");
    }
  }
  if (has_duplicate_ids(shells)) {
    return detail::failed_result<BodyId>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "创建体失败：壳列表包含重复引用", "创建体失败");
  }
  const auto id = BodyId{state_->allocate_id()};
  detail::BodyRecord record;
  record.kind = detail::BodyKind::Generic;
  record.rep_kind = RepKind::ExactBRep;
  record.shells.assign(shells.begin(), shells.end());
  record.source_shells.assign(shells.begin(), shells.end());
  for (const auto shell_id : shells) {
    const auto shell_it = state_->shells.find(shell_id.value);
    if (shell_it == state_->shells.end()) {
      continue;
    }
    const auto body_it = state_->shell_to_bodies.find(shell_id.value);
    if (body_it != state_->shell_to_bodies.end()) {
      for (const auto body_value : body_it->second) {
        append_unique(record.source_bodies, BodyId{body_value});
      }
    }
    for (const auto source_shell_id : shell_it->second.source_shells) {
      append_unique(record.source_shells, source_shell_id);
    }
    if (shell_it->second.source_faces.empty()) {
      for (const auto face_id : shell_it->second.faces) {
        append_unique(record.source_faces, face_id);
      }
      continue;
    }
    for (const auto face_id : shell_it->second.source_faces) {
      append_unique(record.source_faces, face_id);
    }
  }
  record.bbox = compute_body_bbox(*state_, shells);
  if (!record.bbox.is_valid) {
    return detail::failed_result<BodyId>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "创建体失败：无法从输入壳推导有效包围盒，拓扑引用可能不完整",
        "创建体失败");
  }
  state_->bodies.emplace(id.value, record);
  created_bodies_.push_back(id.value);
  rebuild_topology_indices(*state_);
  return ok_result(id, state_->create_diagnostic("已创建体"));
}

Result<void> TopologyTransaction::delete_face(FaceId face_id) {
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxCommitFailure,
                               "删除面失败：事务已关闭", "删除面失败");
  }
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "删除面失败：目标面不存在", "删除面失败");
  }

  transaction_state_->snapshot_face(*state_, face_id);

  std::vector<ShellId> affected_shells;
  std::vector<ShellId> shells_to_remove;
  std::vector<BodyId> bodies_to_remove;
  for (auto &[shell_value, shell] : state_->shells) {
    if (std::none_of(shell.faces.begin(), shell.faces.end(),
                     [face_id](FaceId current) {
                       return current.value == face_id.value;
                     })) {
      continue;
    }
    transaction_state_->snapshot_shell(*state_, ShellId{shell_value});
    const auto old_size = shell.faces.size();
    shell.faces.erase(std::remove_if(shell.faces.begin(), shell.faces.end(),
                                     [face_id](FaceId current) {
                                       return current.value == face_id.value;
                                     }),
                      shell.faces.end());
    if (shell.faces.size() != old_size) {
      affected_shells.push_back(ShellId{shell_value});
      if (shell.faces.empty()) {
        shells_to_remove.push_back(ShellId{shell_value});
      }
    }
  }

  for (auto &[body_value, body] : state_->bodies) {
    if (std::none_of(body.shells.begin(), body.shells.end(),
                     [&affected_shells](ShellId shell_id) {
                       return std::any_of(
                           affected_shells.begin(), affected_shells.end(),
                           [shell_id](ShellId removed) {
                             return removed.value == shell_id.value;
                           });
                     })) {
      continue;
    }
    transaction_state_->snapshot_body(*state_, BodyId{body_value});
    const auto old_size = body.shells.size();
    body.shells.erase(
        std::remove_if(body.shells.begin(), body.shells.end(),
                       [&shells_to_remove](ShellId shell_id) {
                         return std::any_of(
                             shells_to_remove.begin(), shells_to_remove.end(),
                             [shell_id](ShellId removed) {
                               return removed.value == shell_id.value;
                             });
                       }),
        body.shells.end());
    if (body.shells.empty() && body.shells.size() != old_size) {
      bodies_to_remove.push_back(BodyId{body_value});
    }
  }

  for (const auto shell_id : shells_to_remove) {
    state_->shells.erase(shell_id.value);
  }
  for (const auto body_id : bodies_to_remove) {
    state_->bodies.erase(body_id.value);
  }
  for (auto &[body_value, body] : state_->bodies) {
    if (std::none_of(body.shells.begin(), body.shells.end(),
                     [&affected_shells](ShellId shell_id) {
                       return std::any_of(
                           affected_shells.begin(), affected_shells.end(),
                           [shell_id](ShellId affected) {
                             return affected.value == shell_id.value;
                           });
                     })) {
      continue;
    }
    const auto bbox = compute_body_bbox(*state_, body.shells);
    if (bbox.is_valid) {
      body.bbox = bbox;
    }
  }

  state_->faces.erase(face_id.value);
  rebuild_topology_indices(*state_);
  ++txn_deleted_faces_;
  return ok_void(state_->create_diagnostic("已删除面"));
}

Result<void> TopologyTransaction::delete_shell(ShellId shell_id) {
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxCommitFailure,
                               "删除壳失败：事务已关闭", "删除壳失败");
  }
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "删除壳失败：目标壳不存在", "删除壳失败");
  }

  transaction_state_->snapshot_shell(*state_, shell_id);

  // Remove shell ownership from bodies; delete bodies that become empty.
  std::vector<BodyId> bodies_to_remove;
  for (auto &[body_value, body] : state_->bodies) {
    if (std::none_of(body.shells.begin(), body.shells.end(),
                     [shell_id](ShellId current) {
                       return current.value == shell_id.value;
                     })) {
      continue;
    }
    transaction_state_->snapshot_body(*state_, BodyId{body_value});
    const auto old_size = body.shells.size();
    body.shells.erase(std::remove_if(body.shells.begin(), body.shells.end(),
                                     [shell_id](ShellId current) {
                                       return current.value == shell_id.value;
                                     }),
                      body.shells.end());
    if (body.shells.empty() && body.shells.size() != old_size) {
      bodies_to_remove.push_back(BodyId{body_value});
    } else {
      const auto bbox = compute_body_bbox(*state_, body.shells);
      if (bbox.is_valid) {
        body.bbox = bbox;
      }
    }
  }

  for (const auto body_id : bodies_to_remove) {
    state_->bodies.erase(body_id.value);
  }
  state_->shells.erase(shell_id.value);
  rebuild_topology_indices(*state_);
  ++txn_deleted_shells_;
  return ok_void(state_->create_diagnostic("已删除壳"));
}

Result<void> TopologyTransaction::delete_body(BodyId body_id) {
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxCommitFailure,
                               "删除体失败：事务已关闭", "删除体失败");
  }
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle,
                                      "删除体失败：目标体不存在", "删除体失败");
  }

  transaction_state_->snapshot_body(*state_, body_id);

  // Note: we do NOT delete referenced shells/faces automatically here, because
  // shells can be shared by multiple bodies (e.g. boolean provenance tests).
  state_->bodies.erase(body_id.value);
  rebuild_topology_indices(*state_);
  ++txn_deleted_bodies_;
  return ok_void(state_->create_diagnostic("已删除体"));
}

Result<void> TopologyTransaction::replace_surface(FaceId face_id,
                                                  SurfaceId replacement) {
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxCommitFailure,
                               "替换面失败：事务已关闭", "替换面失败");
  }
  const auto it = state_->faces.find(face_id.value);
  if (it == state_->faces.end() || !detail::has_surface(*state_, replacement)) {
    return detail::invalid_input_void(
        *state_, diag_codes::kModReplaceFaceIncompatible,
        "替换面失败：目标面不存在或替换曲面不存在", "替换面失败");
  }
  transaction_state_->snapshot_face(*state_, face_id);
  it->second.surface_id = replacement;
  rebuild_topology_indices(*state_);
  ++txn_replaced_surfaces_;
  return ok_void(state_->create_diagnostic("已替换面曲面"));
}

Result<VersionId> TopologyTransaction::commit() {
  if (!active_) {
    return detail::failed_result<VersionId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "事务提交失败：事务已关闭", "事务提交失败");
  }
  active_ = false;
  return ok_result<VersionId>(state_->next_version++,
                              state_->create_diagnostic("事务已提交"));
}

Result<void> TopologyTransaction::rollback() {
  if (!active_) {
    return detail::failed_void(*state_, StatusCode::OperationFailed,
                               diag_codes::kTxRollbackFailure,
                               "事务回滚失败：事务已关闭", "事务回滚失败");
  }
  for (const auto id : created_bodies_) {
    state_->bodies.erase(id);
  }
  for (const auto id : created_shells_) {
    state_->shells.erase(id);
  }
  for (const auto id : created_faces_) {
    state_->faces.erase(id);
  }
  for (const auto &[id, record] : transaction_state_->original_faces) {
    state_->faces[id] = record;
  }
  for (const auto &[id, record] : transaction_state_->original_shells) {
    state_->shells[id] = record;
  }
  for (const auto &[id, record] : transaction_state_->original_bodies) {
    state_->bodies[id] = record;
  }

  // 恢复面/壳/体之后再删除本事务创建的环/共边/边/顶点，且仅删除已不再被当前状态引用的项，
  // 否则会出现“面仍引用环 id，但环记录已被擦除”的断裂（例如同事务内 delete_face 后 rollback）。
  auto loop_referenced = [this](std::uint64_t loop_id) {
    for (const auto &[fid, face] : state_->faces) {
      (void)fid;
      if (face.outer_loop.value == loop_id) {
        return true;
      }
      if (std::any_of(face.inner_loops.begin(), face.inner_loops.end(),
                      [loop_id](LoopId inner) { return inner.value == loop_id; })) {
        return true;
      }
    }
    return false;
  };
  for (const auto id : created_loops_) {
    if (!loop_referenced(id)) {
      state_->loops.erase(id);
    }
  }
  auto coedge_referenced = [this](std::uint64_t coedge_id) {
    for (const auto &[lid, loop] : state_->loops) {
      (void)lid;
      if (std::any_of(loop.coedges.begin(), loop.coedges.end(),
                      [coedge_id](CoedgeId c) { return c.value == coedge_id; })) {
        return true;
      }
    }
    return false;
  };
  for (const auto id : created_coedges_) {
    if (!coedge_referenced(id)) {
      state_->coedges.erase(id);
    }
  }
  auto edge_referenced = [this](std::uint64_t edge_id) {
    for (const auto &[cid, co] : state_->coedges) {
      (void)cid;
      if (co.edge_id.value == edge_id) {
        return true;
      }
    }
    return false;
  };
  for (const auto id : created_edges_) {
    if (!edge_referenced(id)) {
      state_->edges.erase(id);
    }
  }
  auto vertex_referenced = [this](std::uint64_t vertex_id) {
    for (const auto &[eid, edge] : state_->edges) {
      (void)eid;
      if (edge.v0.value == vertex_id || edge.v1.value == vertex_id) {
        return true;
      }
    }
    return false;
  };
  for (const auto id : created_vertices_) {
    if (!vertex_referenced(id)) {
      state_->vertices.erase(id);
    }
  }
  rebuild_topology_indices(*state_);
  txn_deleted_faces_ = 0;
  txn_deleted_shells_ = 0;
  txn_deleted_bodies_ = 0;
  txn_replaced_surfaces_ = 0;
  txn_coedge_pcurve_binds_ = 0;
  active_ = false;
  return ok_void(state_->create_diagnostic("事务已回滚"));
}

Result<bool> TopologyTransaction::is_active() const {
  return ok_result(active_, state_->create_diagnostic("已查询事务活动状态"));
}

Result<std::uint64_t> TopologyTransaction::created_vertex_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_vertices_.size()),
      state_->create_diagnostic("已查询事务创建顶点数量"));
}

Result<std::uint64_t> TopologyTransaction::created_edge_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_edges_.size()),
      state_->create_diagnostic("已查询事务创建边数量"));
}

Result<std::uint64_t> TopologyTransaction::created_coedge_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_coedges_.size()),
      state_->create_diagnostic("已查询事务创建定向边数量"));
}

Result<std::uint64_t> TopologyTransaction::created_loop_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_loops_.size()),
      state_->create_diagnostic("已查询事务创建环数量"));
}

Result<std::uint64_t> TopologyTransaction::created_face_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_faces_.size()),
      state_->create_diagnostic("已查询事务创建面数量"));
}

Result<std::uint64_t> TopologyTransaction::created_shell_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_shells_.size()),
      state_->create_diagnostic("已查询事务创建壳数量"));
}

Result<std::uint64_t> TopologyTransaction::created_body_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(created_bodies_.size()),
      state_->create_diagnostic("已查询事务创建体数量"));
}

Result<std::uint64_t> TopologyTransaction::created_entity_count_total() const {
  const auto total = created_vertices_.size() + created_edges_.size() +
                     created_coedges_.size() + created_loops_.size() +
                     created_faces_.size() + created_shells_.size() +
                     created_bodies_.size();
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(total),
      state_->create_diagnostic("已查询事务创建实体总数"));
}

Result<std::uint64_t> TopologyTransaction::touched_face_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(transaction_state_->original_faces.size()),
      state_->create_diagnostic("已查询事务触达面数量"));
}

Result<std::uint64_t> TopologyTransaction::touched_shell_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(transaction_state_->original_shells.size()),
      state_->create_diagnostic("已查询事务触达壳数量"));
}

Result<std::uint64_t> TopologyTransaction::touched_body_count() const {
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(transaction_state_->original_bodies.size()),
      state_->create_diagnostic("已查询事务触达体数量"));
}

Result<std::uint64_t> TopologyTransaction::deleted_face_count() const {
  return ok_result<std::uint64_t>(txn_deleted_faces_,
                                  state_->create_diagnostic("已查询事务删除面数量"));
}

Result<std::uint64_t> TopologyTransaction::deleted_shell_count() const {
  return ok_result<std::uint64_t>(txn_deleted_shells_,
                                  state_->create_diagnostic("已查询事务删除壳数量"));
}

Result<std::uint64_t> TopologyTransaction::deleted_body_count() const {
  return ok_result<std::uint64_t>(txn_deleted_bodies_,
                                  state_->create_diagnostic("已查询事务删除体数量"));
}

Result<std::uint64_t> TopologyTransaction::replaced_surface_count() const {
  return ok_result<std::uint64_t>(
      txn_replaced_surfaces_,
      state_->create_diagnostic("已查询事务替换曲面次数"));
}

Result<std::uint64_t> TopologyTransaction::coedge_pcurve_bind_count() const {
  return ok_result<std::uint64_t>(
      txn_coedge_pcurve_binds_,
      state_->create_diagnostic("已查询事务共边 PCurve 绑定次数"));
}

Result<bool> TopologyTransaction::has_created_vertex(VertexId vertex_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_vertices_), vertex_id),
      state_->create_diagnostic("已查询事务是否创建顶点"));
}

Result<bool> TopologyTransaction::has_created_edge(EdgeId edge_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_edges_), edge_id),
      state_->create_diagnostic("已查询事务是否创建边"));
}

Result<bool> TopologyTransaction::has_created_coedge(CoedgeId coedge_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_coedges_), coedge_id),
      state_->create_diagnostic("已查询事务是否创建定向边"));
}

Result<bool> TopologyTransaction::has_created_loop(LoopId loop_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_loops_), loop_id),
      state_->create_diagnostic("已查询事务是否创建环"));
}

Result<bool> TopologyTransaction::has_created_face(FaceId face_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_faces_), face_id),
      state_->create_diagnostic("已查询事务是否创建面"));
}

Result<bool> TopologyTransaction::has_created_shell(ShellId shell_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_shells_), shell_id),
      state_->create_diagnostic("已查询事务是否创建壳"));
}

Result<bool> TopologyTransaction::has_created_body(BodyId body_id) const {
  return ok_result(
      contains_id(std::span<const std::uint64_t>(created_bodies_), body_id),
      state_->create_diagnostic("已查询事务是否创建体"));
}

Result<bool> TopologyTransaction::can_commit() const {
  return ok_result(active_, state_->create_diagnostic("已查询事务是否可提交"));
}

Result<VersionId> TopologyTransaction::preview_commit_version() const {
  if (!active_) {
    return detail::failed_result<VersionId>(
        *state_, StatusCode::OperationFailed, diag_codes::kTxCommitFailure,
        "事务预览失败：事务已关闭", "事务预览失败");
  }
  return ok_result<VersionId>(state_->next_version,
                              state_->create_diagnostic("已预览事务提交版本"));
}

Result<bool> TopologyTransaction::has_snapshot_face(FaceId face_id) const {
  return ok_result(transaction_state_->original_faces.find(face_id.value) !=
                       transaction_state_->original_faces.end(),
                   state_->create_diagnostic("已查询事务是否快照面"));
}

Result<bool> TopologyTransaction::has_snapshot_shell(ShellId shell_id) const {
  return ok_result(transaction_state_->original_shells.find(shell_id.value) !=
                       transaction_state_->original_shells.end(),
                   state_->create_diagnostic("已查询事务是否快照壳"));
}

Result<bool> TopologyTransaction::has_snapshot_body(BodyId body_id) const {
  return ok_result(transaction_state_->original_bodies.find(body_id.value) !=
                       transaction_state_->original_bodies.end(),
                   state_->create_diagnostic("已查询事务是否快照体"));
}

Result<void> TopologyTransaction::clear_tracking_records() {
  created_vertices_.clear();
  created_edges_.clear();
  created_coedges_.clear();
  created_loops_.clear();
  created_faces_.clear();
  created_shells_.clear();
  created_bodies_.clear();
  txn_deleted_faces_ = 0;
  txn_deleted_shells_ = 0;
  txn_deleted_bodies_ = 0;
  txn_replaced_surfaces_ = 0;
  txn_coedge_pcurve_binds_ = 0;
  transaction_state_->original_faces.clear();
  transaction_state_->original_shells.clear();
  transaction_state_->original_bodies.clear();
  return ok_void(state_->create_diagnostic("已清空事务跟踪记录"));
}

Result<std::vector<VertexId>> TopologyTransaction::created_vertices() const {
  std::vector<VertexId> out;
  out.reserve(created_vertices_.size());
  for (const auto id : created_vertices_) {
    out.push_back(VertexId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建顶点列表"));
}

Result<std::vector<EdgeId>> TopologyTransaction::created_edges() const {
  std::vector<EdgeId> out;
  out.reserve(created_edges_.size());
  for (const auto id : created_edges_) {
    out.push_back(EdgeId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建边列表"));
}

Result<std::vector<CoedgeId>> TopologyTransaction::created_coedges() const {
  std::vector<CoedgeId> out;
  out.reserve(created_coedges_.size());
  for (const auto id : created_coedges_) {
    out.push_back(CoedgeId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建定向边列表"));
}

Result<std::vector<LoopId>> TopologyTransaction::created_loops() const {
  std::vector<LoopId> out;
  out.reserve(created_loops_.size());
  for (const auto id : created_loops_) {
    out.push_back(LoopId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建环列表"));
}

Result<std::vector<FaceId>> TopologyTransaction::created_faces() const {
  std::vector<FaceId> out;
  out.reserve(created_faces_.size());
  for (const auto id : created_faces_) {
    out.push_back(FaceId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建面列表"));
}

Result<std::vector<ShellId>> TopologyTransaction::created_shells() const {
  std::vector<ShellId> out;
  out.reserve(created_shells_.size());
  for (const auto id : created_shells_) {
    out.push_back(ShellId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建壳列表"));
}

Result<std::vector<BodyId>> TopologyTransaction::created_bodies() const {
  std::vector<BodyId> out;
  out.reserve(created_bodies_.size());
  for (const auto id : created_bodies_) {
    out.push_back(BodyId{id});
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询事务创建体列表"));
}

TopologyValidationService::TopologyValidationService(
    std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

Result<void>
TopologyValidationService::validate_vertex(VertexId vertex_id) const {
  const auto vertex_it = state_->vertices.find(vertex_id.value);
  if (vertex_it == state_->vertices.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kCoreInvalidHandle,
                               "顶点验证失败：目标顶点不存在", "顶点验证失败");
  }
  const auto &p = vertex_it->second.point;
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kCoreParameterOutOfRange,
                               "顶点验证失败：顶点坐标非法", "顶点验证失败");
  }
  const bool used_by_edge = std::any_of(
      state_->edges.begin(), state_->edges.end(),
      [vid = vertex_id.value](const auto &entry) {
        const auto &e = entry.second;
        return e.v0.value == vid || e.v1.value == vid;
      });
  if (!used_by_edge) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoDanglingVertex,
                               "顶点验证失败：悬挂顶点（未作为任何边的端点）",
                               "顶点验证失败", {vertex_id.value});
  }
  return ok_void(state_->create_diagnostic("顶点验证通过"));
}

Result<void> TopologyValidationService::validate_edge(EdgeId edge_id) const {
  const auto edge_it = state_->edges.find(edge_id.value);
  if (edge_it == state_->edges.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoCurveTopologyMismatch,
                               "边验证失败：目标边不存在", "边验证失败");
  }
  {
    const auto owners_it = state_->edge_to_coedges.find(edge_id.value);
    if (owners_it == state_->edge_to_coedges.end() || owners_it->second.empty()) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoDanglingEdge,
                                 "边验证失败：检测到悬挂边（未被任何定向边使用）",
                                 "边验证失败");
    }
  }
  if (!detail::has_curve(*state_, edge_it->second.curve_id)) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoCurveTopologyMismatch,
                               "边验证失败：边引用的曲线不存在", "边验证失败");
  }
  if (!detail::has_vertex(*state_, edge_it->second.v0) ||
      !detail::has_vertex(*state_, edge_it->second.v1)) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoCurveTopologyMismatch,
                               "边验证失败：边引用的端点不存在", "边验证失败");
  }
  if (edge_it->second.v0.value == edge_it->second.v1.value) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoCurveTopologyMismatch,
                               "边验证失败：边的两个端点不能相同",
                               "边验证失败");
  }
  return ok_void(state_->create_diagnostic("边验证通过"));
}

Result<void>
TopologyValidationService::validate_coedge(CoedgeId coedge_id) const {
  const auto coedge_it = state_->coedges.find(coedge_id.value);
  if (coedge_it == state_->coedges.end()) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kCoreInvalidHandle,
        "定向边验证失败：目标定向边不存在", "定向边验证失败");
  }
  const auto edge_valid = validate_edge(coedge_it->second.edge_id);
  if (edge_valid.status != StatusCode::Ok) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoCurveTopologyMismatch,
                               "定向边验证失败：定向边引用的边非法",
                               "定向边验证失败");
  }
  if (coedge_it->second.pcurve_id.value != 0 &&
      !detail::has_pcurve(*state_, coedge_it->second.pcurve_id)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoCurveTopologyMismatch,
        "定向边验证失败：定向边引用的参数曲线不存在", "定向边验证失败");
  }
  return ok_void(state_->create_diagnostic("定向边验证通过"));
}

Result<void> TopologyValidationService::validate_loop(LoopId loop_id) const {
  std::string reason;
  if (!validate_loop_id(*state_, loop_id, reason)) {
    std::string_view code = diag_codes::kTopoLoopNotClosed;
    if (reason == "环包含重复边引用") {
      code = diag_codes::kTopoLoopDuplicateEdge;
    }
    return detail::failed_void(*state_, StatusCode::InvalidTopology, code,
                               "环验证失败：" + reason, "环验证失败");
  }
  const auto loop_it = state_->loops.find(loop_id.value);
  if (loop_it == state_->loops.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoLoopNotClosed,
                               "环验证失败：目标环不存在", "环验证失败");
  }
  for (const auto coedge_id : loop_it->second.coedges) {
    const auto coedge_valid = validate_coedge(coedge_id);
    if (coedge_valid.status != StatusCode::Ok) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoLoopNotClosed,
                                 "环验证失败：环包含非法定向边", "环验证失败");
    }
  }
  return ok_void(state_->create_diagnostic("环验证通过"));
}

Result<void>
TopologyValidationService::validate_loop_pcurve_closedness(LoopId loop_id) const {
  const auto loop_it = state_->loops.find(loop_id.value);
  if (loop_it == state_->loops.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoLoopNotClosed,
                               "参数环验证失败：目标环不存在", "参数环验证失败");
  }
  if (loop_it->second.coedges.size() < 2) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoLoopNotClosed,
                               "参数环验证失败：环共边数量不足", "参数环验证失败");
  }

  const auto tol = std::max<Scalar>(state_->config.tolerance.linear, 1e-12);
  const auto dist2 = [](const Point2 &a, const Point2 &b) -> Scalar {
    const auto dx = a.x - b.x;
    const auto dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
  };

  // Build ordered UV endpoints per coedge.
  std::vector<std::pair<Point2, Point2>> segs;
  std::vector<CoedgeId> coedge_ids;
  segs.reserve(loop_it->second.coedges.size());
  coedge_ids.reserve(loop_it->second.coedges.size());
  for (const auto coedge_id : loop_it->second.coedges) {
    const auto coedge_it = state_->coedges.find(coedge_id.value);
    if (coedge_it == state_->coedges.end()) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoLoopNotClosed,
                                 "参数环验证失败：环引用的定向边不存在",
                                 "参数环验证失败");
    }
    const auto pc = coedge_it->second.pcurve_id;
    if (pc.value == 0) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoCurveTopologyMismatch,
                                 "参数环验证失败：定向边未绑定参数曲线",
                                 "参数环验证失败",
                                 {loop_id.value, coedge_id.value});
    }
    if (!detail::has_pcurve(*state_, pc)) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoCurveTopologyMismatch,
                                 "参数环验证失败：定向边绑定的参数曲线不存在",
                                 "参数环验证失败",
                                 {loop_id.value, coedge_id.value, pc.value});
    }
    const auto &rec = state_->pcurves.at(pc.value);
    if (rec.poles.size() < 2) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoCurveTopologyMismatch,
                                 "参数环验证失败：参数曲线点数不足",
                                 "参数环验证失败",
                                 {loop_id.value, coedge_id.value, pc.value});
    }
    const auto seg_tol = std::max<Scalar>(tol * static_cast<Scalar>(1e-9),
                                         static_cast<Scalar>(1e-18));
    for (std::size_t pi = 1; pi < rec.poles.size(); ++pi) {
      const auto dx = rec.poles[pi].x - rec.poles[pi - 1].x;
      const auto dy = rec.poles[pi].y - rec.poles[pi - 1].y;
      const auto seg_len = std::sqrt(dx * dx + dy * dy);
      if (!std::isfinite(seg_len) || seg_len <= seg_tol) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoCurveTopologyMismatch,
            "参数环验证失败：参数曲线折线存在退化段（相邻控制点过近）",
            "参数环验证失败",
            {loop_id.value, coedge_id.value, pc.value});
      }
    }
    Point2 a = rec.poles.front();
    Point2 b = rec.poles.back();
    if (coedge_it->second.reversed) {
      std::swap(a, b);
    }
    segs.push_back({a, b});
    coedge_ids.push_back(coedge_id);
  }

  for (std::size_t i = 0; i < segs.size(); ++i) {
    const auto &cur = segs[i];
    const auto &next = segs[(i + 1) % segs.size()];
    const auto d = dist2(cur.second, next.first);
    if (d > tol) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoLoopNotClosed,
                                 "参数环验证失败：参数环不连续或未闭合（loop=" +
                                     std::to_string(loop_id.value) +
                                     ", break_at_coedge=" +
                                     std::to_string(coedge_ids[i].value) +
                                     " -> next_coedge=" +
                                     std::to_string(
                                         coedge_ids[(i + 1) % coedge_ids.size()]
                                             .value) +
                                     ", gap=" + std::to_string(d) + ")",
                                 "参数环验证失败",
                                 {loop_id.value, coedge_ids[i].value,
                                  coedge_ids[(i + 1) % coedge_ids.size()].value});
    }
  }
  return ok_void(state_->create_diagnostic("参数环验证通过"));
}

Result<void>
TopologyValidationService::validate_face_trim_consistency(FaceId face_id) const {
  const auto face_it = state_->faces.find(face_id.value);
  if (face_it == state_->faces.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面修剪验证失败：目标面不存在", "面修剪验证失败");
  }
  const auto inspect_loop = [this](LoopId loop_id, std::size_t &with_pc,
                                   std::size_t &without_pc,
                                   std::string &reason) -> bool {
    const auto loop_it = state_->loops.find(loop_id.value);
    if (loop_it == state_->loops.end()) {
      reason = "面引用了不存在的环";
      return false;
    }
    for (const auto coedge_id : loop_it->second.coedges) {
      const auto coedge_it = state_->coedges.find(coedge_id.value);
      if (coedge_it == state_->coedges.end()) {
        reason = "环引用了不存在的定向边";
        return false;
      }
      if (coedge_it->second.pcurve_id.value == 0) {
        ++without_pc;
      } else {
        ++with_pc;
      }
    }
    return true;
  };

  std::size_t with_pc = 0;
  std::size_t without_pc = 0;
  std::string reason;
  if (!inspect_loop(face_it->second.outer_loop, with_pc, without_pc, reason)) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面修剪验证失败：" + reason, "面修剪验证失败");
  }
  for (const auto loop_id : face_it->second.inner_loops) {
    if (!inspect_loop(loop_id, with_pc, without_pc, reason)) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoFaceOuterLoopInvalid,
                                 "面修剪验证失败：" + reason, "面修剪验证失败");
    }
  }

  // If no pcurves are provided at all, treat trim as "not available" and pass
  // (backward compatible with current Stage 2 state).
  if (with_pc == 0) {
    return ok_void(state_->create_diagnostic("面修剪验证通过（未提供参数曲线）"));
  }

  // If partially provided, it's inconsistent (cannot form a valid trim ring).
  if (without_pc != 0) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoCurveTopologyMismatch,
                               "面修剪验证失败：参数曲线绑定不完整",
                               "面修剪验证失败",
                               {face_id.value});
  }

  // All coedges have pcurves: enforce UV closedness for each loop.
  auto outer = validate_loop_pcurve_closedness(face_it->second.outer_loop);
  if (outer.status != StatusCode::Ok) {
    // Preserve detailed loop/coedge break information for engineering debugging.
    return outer;
  }
  for (const auto loop_id : face_it->second.inner_loops) {
    auto inner = validate_loop_pcurve_closedness(loop_id);
    if (inner.status != StatusCode::Ok) {
      return inner;
    }
  }

  if (!face_it->second.inner_loops.empty()) {
    const auto tol_ori = std::max<Scalar>(state_->config.tolerance.linear, 1e-12);
    const auto compute_loop_signed_uv_area =
        [&](LoopId loop_id, Scalar &out_area, std::string &out_reason) -> bool {
      const auto lit = state_->loops.find(loop_id.value);
      if (lit == state_->loops.end()) {
        out_reason = "环不存在";
        return false;
      }
      std::vector<Point2> ring;
      ring.reserve(lit->second.coedges.size() + 1);
      for (const auto coedge_id : lit->second.coedges) {
        const auto ce_it = state_->coedges.find(coedge_id.value);
        if (ce_it == state_->coedges.end()) {
          out_reason = "定向边不存在";
          return false;
        }
        const auto pc_id = ce_it->second.pcurve_id;
        if (pc_id.value == 0 || !detail::has_pcurve(*state_, pc_id)) {
          out_reason = "缺少参数曲线";
          return false;
        }
        const auto &rec = state_->pcurves.at(pc_id.value);
        if (rec.poles.size() < 2) {
          out_reason = "参数曲线退化";
          return false;
        }
        Point2 a = rec.poles.front();
        Point2 b = rec.poles.back();
        if (ce_it->second.reversed) {
          std::swap(a, b);
        }
        if (ring.empty()) {
          ring.push_back(a);
          ring.push_back(b);
        } else {
          const auto dx = ring.back().x - a.x;
          const auto dy = ring.back().y - a.y;
          if (std::sqrt(dx * dx + dy * dy) > tol_ori) {
            out_reason = "环参数边界不连续";
            return false;
          }
          ring.push_back(b);
        }
      }
      if (ring.size() < 4) {
        out_reason = "环边数不足";
        return false;
      }
      const auto dx0 = ring.front().x - ring.back().x;
      const auto dy0 = ring.front().y - ring.back().y;
      if (std::sqrt(dx0 * dx0 + dy0 * dy0) > tol_ori) {
        out_reason = "环未闭合";
        return false;
      }
      Scalar sum2 = 0.0;
      for (std::size_t i = 0; i + 1 < ring.size(); ++i) {
        sum2 += ring[i].x * ring[i + 1].y - ring[i + 1].x * ring[i].y;
      }
      out_area = sum2 * 0.5;
      if (std::abs(out_area) <= tol_ori * tol_ori * static_cast<Scalar>(10)) {
        out_reason = "环有符号面积过小";
        return false;
      }
      return true;
    };

    Scalar outer_area = 0.0;
    std::string area_reason;
    if (!compute_loop_signed_uv_area(face_it->second.outer_loop, outer_area,
                                     area_reason)) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoFaceOuterLoopInvalid,
                                 "面修剪验证失败：外环方向不可判定（" + area_reason + "）",
                                 "面修剪验证失败",
                                 {face_id.value, face_it->second.outer_loop.value});
    }
    for (const auto inner_loop_id : face_it->second.inner_loops) {
      Scalar inner_area = 0.0;
      if (!compute_loop_signed_uv_area(inner_loop_id, inner_area, area_reason)) {
        return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                   diag_codes::kTopoFaceInnerLoopInvalid,
                                   "面修剪验证失败：内环方向不可判定（" + area_reason + "）",
                                   "面修剪验证失败",
                                   {face_id.value, inner_loop_id.value});
      }
      if ((outer_area > 0.0 && inner_area > 0.0) ||
          (outer_area < 0.0 && inner_area < 0.0)) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoLoopOrientationMismatch,
            "面修剪验证失败：内外环方向不符合规则（应相反）"
            " (face=" +
                std::to_string(face_id.value) + ", outer_loop=" +
                std::to_string(face_it->second.outer_loop.value) +
                ", inner_loop=" + std::to_string(inner_loop_id.value) + ")",
            "面修剪验证失败",
            {face_id.value, face_it->second.outer_loop.value, inner_loop_id.value});
      }
    }
  }

  // Lightweight PCurve <-> 3D edge endpoint consistency check (best-effort):
  // Only enforce when surface->closest_uv/surface->eval and curve->closest_point are available.
  SurfaceService surface_service{state_};
  PCurveService pcurve_service{state_};
  CurveService curve_service{state_};
  const auto tol = std::max<Scalar>(state_->config.tolerance.linear, 1e-9);
  const auto tol3 = tol;
  const auto dist2 = [](const Point2 &a, const Point2 &b) -> Scalar {
    const auto dx = a.x - b.x;
    const auto dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
  };
  const auto dist3 = [](const Point3 &a, const Point3 &b) -> Scalar {
    const auto dx = a.x - b.x;
    const auto dy = a.y - b.y;
    const auto dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  };
  auto check_loop = [&](LoopId loop_id) -> Result<void> {
    const auto loop_it = state_->loops.find(loop_id.value);
    if (loop_it == state_->loops.end()) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoFaceOuterLoopInvalid,
                                 "面修剪验证失败：面引用了不存在的环",
                                 "面修剪验证失败");
    }
    std::size_t checked = 0;
    for (const auto coedge_id : loop_it->second.coedges) {
      const auto coedge_it = state_->coedges.find(coedge_id.value);
      if (coedge_it == state_->coedges.end()) {
        continue;
      }
      const auto pc_id = coedge_it->second.pcurve_id;
      if (pc_id.value == 0 || !detail::has_pcurve(*state_, pc_id)) {
        continue;
      }
      const auto edge_it = state_->edges.find(coedge_it->second.edge_id.value);
      if (edge_it == state_->edges.end()) {
        continue;
      }
      const auto curve_id = edge_it->second.curve_id;
      if (curve_id.value == 0 || !detail::has_curve(*state_, curve_id)) {
        continue;
      }
      const auto v0_it = state_->vertices.find(edge_it->second.v0.value);
      const auto v1_it = state_->vertices.find(edge_it->second.v1.value);
      if (v0_it == state_->vertices.end() || v1_it == state_->vertices.end()) {
        continue;
      }
      const auto start3 =
          coedge_it->second.reversed ? v1_it->second.point : v0_it->second.point;
      const auto end3 =
          coedge_it->second.reversed ? v0_it->second.point : v1_it->second.point;

      const auto uv_start = surface_service.closest_uv(face_it->second.surface_id, start3);
      const auto uv_end = surface_service.closest_uv(face_it->second.surface_id, end3);
      if (uv_start.status != StatusCode::Ok || uv_end.status != StatusCode::Ok ||
          !uv_start.value.has_value() || !uv_end.value.has_value()) {
        continue; // unsupported or not solvable -> skip
      }
      ++checked;

      const auto &pc = state_->pcurves.at(pc_id.value);
      if (pc.poles.size() < 2) {
        continue;
      }
      const Point2 pc_start = pc.poles.front();
      const Point2 pc_end = pc.poles.back();
      const Point2 s2{uv_start.value->first, uv_start.value->second};
      const Point2 e2{uv_end.value->first, uv_end.value->second};
      const auto ds = dist2(pc_start, s2);
      const auto de = dist2(pc_end, e2);
      if (ds > tol || de > tol) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoCurveTopologyMismatch,
            "面修剪验证失败：参数曲线端点与 3D 边端点的 UV 映射不一致"
            " (face=" +
                std::to_string(face_id.value) + ", loop=" +
                std::to_string(loop_id.value) + ", coedge=" +
                std::to_string(coedge_id.value) + ", edge=" +
                std::to_string(coedge_it->second.edge_id.value) +
                ", pcurve=" + std::to_string(pc_id.value) +
                ", ds=" + std::to_string(ds) + ", de=" + std::to_string(de) +
                ")",
            "面修剪验证失败");
      }

      // Also enforce that pcurve endpoints map back to the 3D edge endpoints on the surface.
      // (This catches cases where vertices are consistent in UV but the underlying 3D curve deviates from trimming.)
      {
        auto eval_surf_at = [&](const Point2 &uv) -> std::optional<Point3> {
          const auto ev = surface_service.eval(face_it->second.surface_id, uv.x, uv.y, 0);
          if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
            return std::nullopt;
          }
          return ev.value->point;
        };
        const auto ps = eval_surf_at(pc_start);
        const auto pe = eval_surf_at(pc_end);
        if (ps.has_value() && dist3(*ps, start3) > tol3) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoCurveTopologyMismatch,
              "面修剪验证失败：参数曲线起点映射的 3D 点与边起点不一致"
              " (face=" +
                  std::to_string(face_id.value) + ", loop=" +
                  std::to_string(loop_id.value) + ", coedge=" +
                  std::to_string(coedge_id.value) + ", edge=" +
                  std::to_string(coedge_it->second.edge_id.value) +
                  ", pcurve=" + std::to_string(pc_id.value) + ")",
              "面修剪验证失败");
        }
        if (pe.has_value() && dist3(*pe, end3) > tol3) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoCurveTopologyMismatch,
              "面修剪验证失败：参数曲线终点映射的 3D 点与边终点不一致"
              " (face=" +
                  std::to_string(face_id.value) + ", loop=" +
                  std::to_string(loop_id.value) + ", coedge=" +
                  std::to_string(coedge_id.value) + ", edge=" +
                  std::to_string(coedge_it->second.edge_id.value) +
                  ", pcurve=" + std::to_string(pc_id.value) + ")",
              "面修剪验证失败");
        }
      }

      // Segment sampling consistency (best-effort):
      // Compare a few samples along the PCurve mapped to surface 3D points against the 3D edge curve.
      const auto domain = pcurve_service.domain(pc_id);
      if (domain.status != StatusCode::Ok || !domain.value.has_value() ||
          !(domain.value->max >= domain.value->min) ||
          !std::isfinite(domain.value->min) || !std::isfinite(domain.value->max)) {
        continue;
      }
      const auto tmin = domain.value->min;
      const auto tmax = domain.value->max;
      // Dense interior sampling so piecewise-linear UV cannot hide large gaps.
      const std::array<Scalar, 7> alphas{{1.0 / 8.0, 2.0 / 8.0, 3.0 / 8.0, 4.0 / 8.0,
                                          5.0 / 8.0, 6.0 / 8.0, 7.0 / 8.0}};
      for (const auto alpha : alphas) {
        const auto t_alpha =
            coedge_it->second.reversed ? (tmin + (tmax - tmin) * (1.0 - alpha))
                                       : (tmin + (tmax - tmin) * alpha);
        const auto pc_eval = pcurve_service.eval(pc_id, t_alpha, 0);
        if (pc_eval.status != StatusCode::Ok || !pc_eval.value.has_value()) {
          continue;
        }
        const Point2 pc2{pc_eval.value->point.x, pc_eval.value->point.y};
        const auto surf_ev = surface_service.eval(face_it->second.surface_id, pc2.x, pc2.y, 0);
        if (surf_ev.status != StatusCode::Ok || !surf_ev.value.has_value()) {
          continue;
        }
        const auto target3 = surf_ev.value->point;
        const auto cp = curve_service.closest_point(curve_id, target3);
        if (cp.status != StatusCode::Ok || !cp.value.has_value()) {
          continue;
        }
        const auto d3 = dist3(*cp.value, target3);
        if (d3 > tol3) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoCurveTopologyMismatch,
              "面修剪验证失败：参数曲线映射到曲面后的 3D 位置与边 3D 曲线不一致"
              " (face=" +
                  std::to_string(face_id.value) + ", loop=" +
                  std::to_string(loop_id.value) + ", coedge=" +
                  std::to_string(coedge_id.value) + ", edge=" +
                  std::to_string(coedge_it->second.edge_id.value) +
                  ", pcurve=" + std::to_string(pc_id.value) +
                  ", alpha=" + std::to_string(alpha) +
                  ", d3=" + std::to_string(d3) + ")",
              "面修剪验证失败",
              {face_id.value, loop_id.value, coedge_id.value,
               coedge_it->second.edge_id.value, pc_id.value});
        }
      }
    }
    // If nothing was checkable, do not fail (keeps Stage 2 compatible).
    (void)checked;
    return ok_void(state_->create_diagnostic("面修剪端点一致性校验通过"));
  };

  auto outer_check = check_loop(face_it->second.outer_loop);
  if (outer_check.status != StatusCode::Ok) {
    return outer_check;
  }
  for (const auto loop_id : face_it->second.inner_loops) {
    auto inner_check = check_loop(loop_id);
    if (inner_check.status != StatusCode::Ok) {
      return inner_check;
    }
  }
  return ok_void(state_->create_diagnostic("面修剪验证通过"));
}

Result<void>
TopologyValidationService::validate_shell_trim_consistency(
    ShellId shell_id) const {
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "壳面修剪验证失败：目标壳不存在",
                               "壳面修剪验证失败");
  }
  for (const auto face_id : shell_it->second.faces) {
    auto r = validate_face_trim_consistency(face_id);
    if (r.status != StatusCode::Ok) {
      return r;
    }
  }
  return ok_void(state_->create_diagnostic("壳面修剪验证通过"));
}

Result<void>
TopologyValidationService::validate_body_trim_consistency(
    BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "体面修剪验证失败：目标体不存在",
                               "体面修剪验证失败");
  }
  for (const auto shell_id : body_it->second.shells) {
    auto r = validate_shell_trim_consistency(shell_id);
    if (r.status != StatusCode::Ok) {
      return r;
    }
  }
  return ok_void(state_->create_diagnostic("体面修剪验证通过"));
}

Result<void>
TopologyValidationService::validate_face_sources(FaceId face_id) const {
  const auto face_it = state_->faces.find(face_id.value);
  if (face_it == state_->faces.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面来源验证失败：目标面不存在",
                               "面来源验证失败");
  }
  if (has_duplicate_ids(
          std::span<const FaceId>(face_it->second.source_faces)) ||
      !validate_source_faces_exist(*state_, face_it->second.source_faces)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoSourceRefInvalid,
        "面来源验证失败：来源面引用非法", "面来源验证失败");
  }
  return ok_void(state_->create_diagnostic("面来源验证通过"));
}

Result<void> TopologyValidationService::validate_face(FaceId face_id) const {
  const auto face_it = state_->faces.find(face_id.value);
  if (face_it == state_->faces.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面验证失败：目标面不存在", "面验证失败");
  }
  if (!detail::has_surface(*state_, face_it->second.surface_id)) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面验证失败：面引用的曲面不存在", "面验证失败");
  }

  auto outer_loop_valid = validate_loop(face_it->second.outer_loop);
  if (outer_loop_valid.status != StatusCode::Ok) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面验证失败：外环非法", "面验证失败");
  }
  if (has_duplicate_ids(std::span<const LoopId>(face_it->second.inner_loops))) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoFaceOuterLoopInvalid,
                               "面验证失败：内环列表包含重复引用",
                               "面验证失败");
  }

  // Stronger consistency: within one Face, the same underlying Edge must not be referenced
  // by more than one loop (outer/inner). This avoids self-gluing trims and ambiguous regions.
  {
    std::unordered_set<std::uint64_t> used_edges;
    auto consume_loop_edges = [&](LoopId loop_id) -> Result<void> {
      const auto loop_it = state_->loops.find(loop_id.value);
      if (loop_it == state_->loops.end()) {
        return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                   diag_codes::kTopoFaceOuterLoopInvalid,
                                   "面验证失败：面引用了不存在的环", "面验证失败",
                                   {face_id.value, loop_id.value});
      }
      for (const auto coedge_id : loop_it->second.coedges) {
        const auto coedge_it = state_->coedges.find(coedge_id.value);
        if (coedge_it == state_->coedges.end()) {
          return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                     diag_codes::kTopoFaceOuterLoopInvalid,
                                     "面验证失败：环引用了不存在的定向边",
                                     "面验证失败",
                                     {face_id.value, loop_id.value, coedge_id.value});
        }
        const auto edge_value = coedge_it->second.edge_id.value;
        if (!used_edges.insert(edge_value).second) {
          // Emit kTopoLoopDuplicateEdge so validation callers can treat it as a stable, specific defect.
          // This is used by regression tests to ensure cross-loop duplicate edges are surfaced.
          return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                     diag_codes::kTopoLoopDuplicateEdge,
                                     "面验证失败：面内存在跨环复用的边（edge=" +
                                         std::to_string(edge_value) + ")",
                                     "面验证失败",
                                     {face_id.value, loop_id.value, edge_value});
        }
      }
      return ok_void(state_->create_diagnostic("面边界边集一致性校验通过"));
    };
    auto outer_edges = consume_loop_edges(face_it->second.outer_loop);
    if (outer_edges.status != StatusCode::Ok) {
      return outer_edges;
    }
    for (const auto loop_id : face_it->second.inner_loops) {
      auto inner_edges = consume_loop_edges(loop_id);
      if (inner_edges.status != StatusCode::Ok) {
        return inner_edges;
      }
    }
  }

  // Planar faces without PCurve trim: (1) outer 3D winding must align with Surface normal
  // (Newell vs plane normal). (2) With holes, outer/inner signed areas in plane basis must
  // oppose (standard hole orientation).
  {
    bool any_pcurve = false;
    auto scan_loop_for_pcurve = [&](LoopId loop_id) {
      const auto loop_it = state_->loops.find(loop_id.value);
      if (loop_it == state_->loops.end()) return;
      for (const auto coedge_id : loop_it->second.coedges) {
        const auto coedge_it = state_->coedges.find(coedge_id.value);
        if (coedge_it != state_->coedges.end() && coedge_it->second.pcurve_id.value != 0) {
          any_pcurve = true;
          return;
        }
      }
    };
    scan_loop_for_pcurve(face_it->second.outer_loop);
    for (const auto loop_id : face_it->second.inner_loops) {
      scan_loop_for_pcurve(loop_id);
    }

    const auto surf_it = state_->surfaces.find(face_it->second.surface_id.value);
    if (!any_pcurve && surf_it != state_->surfaces.end() &&
        surf_it->second.kind == detail::SurfaceKind::Plane) {
      const auto n = detail::normalize(surf_it->second.normal);
      const auto tol = std::max<Scalar>(state_->config.tolerance.linear, 1e-12);

      std::vector<Point3> outer_pts;
      std::string chain_reason;
      if (!loop_vertex_chain_3d(*state_, face_it->second.outer_loop, outer_pts,
                                 chain_reason)) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoFaceOuterLoopInvalid,
            "面验证失败：外环无法提取 3D 边界（" + chain_reason + "）",
            "面验证失败",
            {face_id.value, face_it->second.outer_loop.value});
      }
      if (outer_pts.size() < 3) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoFaceOuterLoopInvalid,
            "面验证失败：外环顶点数不足", "面验证失败",
            {face_id.value, face_it->second.outer_loop.value});
      }
      const auto newell_o = newell_normal_unnormalized(outer_pts);
      const auto newell_thresh = tol * tol * static_cast<Scalar>(100);
      // Only enforce winding-vs-normal when Newell vector is stable.
      // Degenerate/intermediate planar loops skip this gate to avoid false negatives.
      if (detail::norm(newell_o) > newell_thresh) {
        const auto nn = detail::normalize(newell_o);
        const auto dot_align = detail::dot(nn, n);
        const auto dot_tol = std::max<Scalar>(static_cast<Scalar>(1e-8),
                                              tol * static_cast<Scalar>(1e-6));
        if (dot_align <= dot_tol) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoLoopOrientationMismatch,
              "面验证失败：平面外环绕序与曲面法向不一致（右手系）"
              " (face=" +
                  std::to_string(face_id.value) + ", outer_loop=" +
                  std::to_string(face_it->second.outer_loop.value) + ")",
              "面验证失败",
              {face_id.value, face_it->second.outer_loop.value});
        }
      }

      if (!face_it->second.inner_loops.empty()) {
        Vec3 ref = (std::abs(n.z) < 0.9) ? Vec3 {0.0, 0.0, 1.0} : Vec3 {1.0, 0.0, 0.0};
        auto u = detail::cross(ref, n);
        if (detail::norm(u) <= 1e-14) {
          ref = Vec3 {0.0, 1.0, 0.0};
          u = detail::cross(ref, n);
        }
        u = detail::normalize(u);
        const auto v = detail::cross(n, u);
        const auto o = surf_it->second.origin;

        auto signed_area_loop =
            [&](LoopId loop_id, Scalar &out_area,
                std::string &out_reason) -> bool {
          const auto loop_it = state_->loops.find(loop_id.value);
          if (loop_it == state_->loops.end() ||
              loop_it->second.coedges.size() < 3) {
            out_reason = "环不存在或边数不足";
            return false;
          }
          std::vector<Point2> ring;
          ring.reserve(loop_it->second.coedges.size() + 1);
          for (const auto coedge_id : loop_it->second.coedges) {
            const auto ov = oriented_vertices(*state_, coedge_id);
            if (!ov.has_value()) {
              out_reason = "定向边无效";
              return false;
            }
            const auto vit = state_->vertices.find((*ov)[0].value);
            if (vit == state_->vertices.end()) {
              out_reason = "顶点不存在";
              return false;
            }
            const auto p = vit->second.point;
            const Vec3 d = detail::subtract(p, o);
            const Scalar uu = detail::dot(d, u);
            const Scalar vv = detail::dot(d, v);
            ring.push_back(Point2 {uu, vv});
          }
          ring.push_back(ring.front());
          Scalar sum2 = 0.0;
          for (std::size_t i = 0; i + 1 < ring.size(); ++i) {
            sum2 += ring[i].x * ring[i + 1].y - ring[i + 1].x * ring[i].y;
          }
          out_area = sum2 * 0.5;
          if (std::abs(out_area) <= tol * tol * static_cast<Scalar>(10)) {
            out_reason = "环面积过小";
            return false;
          }
          return true;
        };

        Scalar outer_area = 0.0;
        std::string area_reason;
        if (!signed_area_loop(face_it->second.outer_loop, outer_area,
                              area_reason)) {
          return detail::failed_void(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoFaceOuterLoopInvalid,
              "面验证失败：外环方向不可判定（" + area_reason + "）",
              "面验证失败",
              {face_id.value, face_it->second.outer_loop.value});
        }
        for (const auto inner_loop_id : face_it->second.inner_loops) {
          Scalar inner_area = 0.0;
          if (!signed_area_loop(inner_loop_id, inner_area, area_reason)) {
            return detail::failed_void(
                *state_, StatusCode::InvalidTopology,
                diag_codes::kTopoFaceInnerLoopInvalid,
                "面验证失败：内环方向不可判定（" + area_reason + "）",
                "面验证失败",
                {face_id.value, inner_loop_id.value});
          }
          if ((outer_area > 0.0 && inner_area > 0.0) ||
              (outer_area < 0.0 && inner_area < 0.0)) {
            return detail::failed_void(
                *state_, StatusCode::InvalidTopology,
                diag_codes::kTopoLoopOrientationMismatch,
                "面验证失败：内外环方向不符合孔洞规则（应相反）"
                " (face=" +
                    std::to_string(face_id.value) + ", outer_loop=" +
                    std::to_string(face_it->second.outer_loop.value) +
                    ", inner_loop=" + std::to_string(inner_loop_id.value) + ")",
                "面验证失败",
                {face_id.value, face_it->second.outer_loop.value,
                 inner_loop_id.value});
          }
        }
      }
    }
  }

  for (const auto loop_id : face_it->second.inner_loops) {
    if (loop_id.value == face_it->second.outer_loop.value) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoFaceOuterLoopInvalid,
                                 "面验证失败：内环不能与外环相同",
                                 "面验证失败");
    }
    auto inner_loop_valid = validate_loop(loop_id);
    if (inner_loop_valid.status != StatusCode::Ok) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoFaceOuterLoopInvalid,
                                 "面验证失败：内环非法", "面验证失败");
    }
  }
  auto source_valid = validate_face_sources(face_id);
  if (source_valid.status != StatusCode::Ok) {
    return source_valid;
  }
  return ok_void(state_->create_diagnostic("面验证通过"));
}

Result<void>
TopologyValidationService::validate_shell_sources(ShellId shell_id) const {
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "壳来源验证失败：目标壳不存在", "壳来源验证失败");
  }
  if (has_duplicate_ids(
          std::span<const ShellId>(shell_it->second.source_shells)) ||
      !validate_source_shells_exist(*state_, shell_it->second.source_shells)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoSourceRefInvalid,
        "壳来源验证失败：来源壳引用非法", "壳来源验证失败");
  }
  if (has_duplicate_ids(
          std::span<const FaceId>(shell_it->second.source_faces)) ||
      !validate_source_faces_exist(*state_, shell_it->second.source_faces)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoSourceRefInvalid,
        "壳来源验证失败：来源面引用非法", "壳来源验证失败");
  }
  return ok_void(state_->create_diagnostic("壳来源验证通过"));
}

Result<void> TopologyValidationService::validate_shell(ShellId shell_id) const {
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "壳验证失败：目标壳不存在", "壳验证失败");
  }
  if (shell_it->second.faces.empty()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "壳验证失败：壳不包含任何面", "壳验证失败");
  }
  if (has_duplicate_ids(std::span<const FaceId>(shell_it->second.faces))) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoDuplicateFaceInShell,
                               "壳验证失败：壳包含重复面引用", "壳验证失败",
                               {shell_id.value});
  }
  // Shell ↔ face_to_shells: every face listed by the shell must reverse-index this shell.
  for (const auto face_id : shell_it->second.faces) {
    if (!detail::has_face(*state_, face_id)) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "壳验证失败：壳引用了不存在的面", "壳验证失败",
          {shell_id.value, face_id.value});
    }
    const auto fts_it = state_->face_to_shells.find(face_id.value);
    if (fts_it == state_->face_to_shells.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "壳验证失败：面在 face_to_shells 中缺少反向索引条目", "壳验证失败",
          {shell_id.value, face_id.value});
    }
    const bool lists_this_shell =
        std::any_of(fts_it->second.begin(), fts_it->second.end(),
                    [sv = shell_id.value](std::uint64_t x) { return x == sv; });
    if (!lists_this_shell) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "壳验证失败：face_to_shells 未列出本壳（索引与壳成员不一致）",
          "壳验证失败", {shell_id.value, face_id.value});
    }
  }
  for (const auto face_id : shell_it->second.faces) {
    const auto result = validate_face(face_id);
    if (result.status != StatusCode::Ok) {
      // Preserve the underlying diagnostic details from face validation,
      // instead of masking them as a generic shell error.
      return result;
    }
  }

  auto source_valid = validate_shell_sources(shell_id);
  if (source_valid.status != StatusCode::Ok) {
    return source_valid;
  }
  // 注意：TopoCore 的“结构校验”不强制壳必须闭合（闭合/非流形属于更强约束，
  // 在 Heal/Validation 的 Strict 模式中作为硬门禁）。这里给出可诊断的告警，避免静默通过。
  std::unordered_map<std::uint64_t, std::uint64_t> edge_use_count;
  std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> edge_to_faces;
  for (const auto face_id : shell_it->second.faces) {
    const auto face_it = state_->faces.find(face_id.value);
    if (face_it == state_->faces.end()) {
      continue;
    }
    std::vector<LoopId> loops;
    loops.push_back(face_it->second.outer_loop);
    loops.insert(loops.end(), face_it->second.inner_loops.begin(),
                 face_it->second.inner_loops.end());
    for (const auto loop_id : loops) {
      const auto loop_it = state_->loops.find(loop_id.value);
      if (loop_it == state_->loops.end()) {
        continue;
      }
      for (const auto coedge_id : loop_it->second.coedges) {
        const auto coedge_it = state_->coedges.find(coedge_id.value);
        if (coedge_it == state_->coedges.end()) {
          continue;
        }
        ++edge_use_count[coedge_it->second.edge_id.value];
        edge_to_faces[coedge_it->second.edge_id.value].push_back(face_id.value);
      }
    }
  }

  const auto diag = state_->create_diagnostic("壳验证通过");
  Result<void> out = ok_void(diag);

  // Duplicate face rule (stronger relation consistency, Stage 2):
  // Report (as warning) when two different FaceIds in the same shell share the same surface and the same boundary loops.
  // NOTE: Keep as warning to avoid masking Strict closedness diagnostics (e.g., non-manifold edges).
  {
    std::unordered_map<std::string, FaceId> seen;
    seen.reserve(shell_it->second.faces.size());
    for (const auto face_id : shell_it->second.faces) {
      const auto face_it = state_->faces.find(face_id.value);
      if (face_it == state_->faces.end()) {
        continue;
      }
      std::vector<std::uint64_t> inner;
      inner.reserve(face_it->second.inner_loops.size());
      for (const auto loop_id : face_it->second.inner_loops) {
        inner.push_back(loop_id.value);
      }
      std::sort(inner.begin(), inner.end());
      std::string sig;
      sig.reserve(64 + inner.size() * 12);
      sig += std::to_string(face_it->second.surface_id.value);
      sig += "|o=";
      sig += std::to_string(face_it->second.outer_loop.value);
      sig += "|i=";
      for (const auto v : inner) {
        sig += std::to_string(v);
        sig += ",";
      }
      const auto it = seen.find(sig);
      if (it != seen.end() && it->second.value != face_id.value) {
        const auto msg =
            "壳可能包含重复面：face_a=" + std::to_string(it->second.value) +
            ", face_b=" + std::to_string(face_id.value) +
            ", outer_loop=" + std::to_string(face_it->second.outer_loop.value);
        out.warnings.push_back(detail::make_warning(diag_codes::kTopoDuplicateFaceInShell, msg));
        auto issue = detail::make_warning_issue(diag_codes::kTopoDuplicateFaceInShell, msg);
        issue.related_entities = {shell_id.value, it->second.value, face_id.value,
                                  face_it->second.outer_loop.value};
        state_->append_diagnostic_issue(diag, std::move(issue));
      } else {
        seen.emplace(std::move(sig), face_id);
      }
    }
  }

  // Connectivity rule (industrial-kernel inspired, best-effort):
  // If the shell's faces form multiple disconnected components (by shared edges), report as warning.
  {
    std::unordered_map<std::uint64_t, std::size_t> face_index;
    face_index.reserve(shell_it->second.faces.size());
    for (std::size_t i = 0; i < shell_it->second.faces.size(); ++i) {
      face_index.emplace(shell_it->second.faces[i].value, i);
    }
    const auto n = shell_it->second.faces.size();
    std::vector<std::vector<std::size_t>> adj(n);
    for (auto& v : adj) v.reserve(4);

    for (auto& [edge_value, faces] : edge_to_faces) {
      if (faces.size() < 2) continue;
      // Dedup faces per edge (in case a face references same edge multiple times in degenerate data).
      std::sort(faces.begin(), faces.end());
      faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
      for (std::size_t i = 0; i + 1 < faces.size(); ++i) {
        for (std::size_t j = i + 1; j < faces.size(); ++j) {
          const auto it_i = face_index.find(faces[i]);
          const auto it_j = face_index.find(faces[j]);
          if (it_i == face_index.end() || it_j == face_index.end()) continue;
          adj[it_i->second].push_back(it_j->second);
          adj[it_j->second].push_back(it_i->second);
        }
      }
    }

    std::vector<char> visited(n, 0);
    std::size_t components = 0;
    for (std::size_t i = 0; i < n; ++i) {
      if (visited[i]) continue;
      ++components;
      std::vector<std::size_t> stack;
      stack.push_back(i);
      visited[i] = 1;
      while (!stack.empty()) {
        const auto u = stack.back();
        stack.pop_back();
        for (const auto v : adj[u]) {
          if (!visited[v]) {
            visited[v] = 1;
            stack.push_back(v);
          }
        }
      }
    }
    if (components > 1) {
      const auto msg = "壳可能不连通：components=" + std::to_string(components) +
                       ", face_count=" + std::to_string(n);
      out.warnings.push_back(detail::make_warning(diag_codes::kTopoShellDisconnected, msg));
      auto issue = detail::make_warning_issue(diag_codes::kTopoShellDisconnected, msg);
      issue.related_entities = {shell_id.value};
      state_->append_diagnostic_issue(diag, std::move(issue));
    }
  }

  for (const auto& [edge_value, use_count] : edge_use_count) {
    if (use_count < 2) {
      const auto msg = "壳可能未闭合：边 " + std::to_string(edge_value) +
                       " 使用次数为 " + std::to_string(use_count);
      out.warnings.push_back(detail::make_warning(diag_codes::kTopoOpenBoundary, msg));
      state_->append_diagnostic_issue(
          diag, detail::make_warning_issue(diag_codes::kTopoOpenBoundary, msg));
    } else if (use_count > 2) {
      const auto msg = "壳可能存在非流形边：边 " + std::to_string(edge_value) +
                       " 使用次数为 " + std::to_string(use_count);
      out.warnings.push_back(detail::make_warning(diag_codes::kTopoNonManifoldEdge, msg));
      state_->append_diagnostic_issue(
          diag, detail::make_warning_issue(diag_codes::kTopoNonManifoldEdge, msg));
    }
  }
  return out;
}

Result<void>
TopologyValidationService::validate_shell_closedness(ShellId shell_id) const {
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "壳闭合性验证失败：目标壳不存在",
                               "壳闭合性验证失败");
  }
  if (shell_it->second.faces.empty()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "壳闭合性验证失败：壳不包含任何面",
                               "壳闭合性验证失败");
  }

  // 复用与 validate_shell 相同的计数逻辑，但在此处作为硬门禁返回失败。
  std::unordered_map<std::uint64_t, std::uint64_t> edge_use_count;
  std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> edge_to_faces;
  for (const auto face_id : shell_it->second.faces) {
    const auto face_it = state_->faces.find(face_id.value);
    if (face_it == state_->faces.end()) {
      continue;
    }
    std::vector<LoopId> loops;
    loops.push_back(face_it->second.outer_loop);
    loops.insert(loops.end(), face_it->second.inner_loops.begin(),
                 face_it->second.inner_loops.end());
    for (const auto loop_id : loops) {
      const auto loop_it = state_->loops.find(loop_id.value);
      if (loop_it == state_->loops.end()) {
        continue;
      }
      for (const auto coedge_id : loop_it->second.coedges) {
        const auto coedge_it = state_->coedges.find(coedge_id.value);
        if (coedge_it == state_->coedges.end()) {
          continue;
        }
        ++edge_use_count[coedge_it->second.edge_id.value];
        edge_to_faces[coedge_it->second.edge_id.value].push_back(face_id.value);
      }
    }
  }

  const auto append_face_outer_loops = [&](std::uint64_t ev, std::vector<std::uint64_t>& rel) {
    const auto itf = edge_to_faces.find(ev);
    if (itf == edge_to_faces.end()) {
      return;
    }
    std::vector<std::uint64_t> vf = itf->second;
    std::sort(vf.begin(), vf.end());
    vf.erase(std::unique(vf.begin(), vf.end()), vf.end());
    const std::size_t lim = std::min<std::size_t>(vf.size(), 3);
    for (std::size_t i = 0; i < lim; ++i) {
      rel.push_back(vf[i]);
      const auto fi = state_->faces.find(vf[i]);
      if (fi != state_->faces.end()) {
        rel.push_back(fi->second.outer_loop.value);
      }
    }
  };

  for (const auto& [edge_value, use_count] : edge_use_count) {
    auto fit = edge_to_faces.find(edge_value);
    if (fit != edge_to_faces.end()) {
      auto& faces = fit->second;
      std::sort(faces.begin(), faces.end());
      faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
      if (faces.size() < 2) {
        const auto msg = "壳未闭合：边 " + std::to_string(edge_value) +
                         " 未被两侧不同拓扑面使用（unique_faces=" +
                         std::to_string(faces.size()) + ", use_count=" +
                         std::to_string(use_count) + ")";
        std::vector<std::uint64_t> related {shell_id.value, edge_value};
        append_face_outer_loops(edge_value, related);
        return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                   diag_codes::kTopoOpenBoundary, msg,
                                   "壳未闭合",
                                   std::move(related));
      }
      if (faces.size() > 2) {
        const auto msg = "壳非流形：边 " + std::to_string(edge_value) +
                         " 被超过两个拓扑面共享（unique_faces=" +
                         std::to_string(faces.size()) + ", use_count=" +
                         std::to_string(use_count) + ")";
        std::vector<std::uint64_t> related {shell_id.value, edge_value};
        append_face_outer_loops(edge_value, related);
        return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                   diag_codes::kTopoNonManifoldEdge, msg,
                                   "壳非流形",
                                   std::move(related));
      }
    }
    if (use_count < 2) {
      const auto msg = "壳未闭合：边 " + std::to_string(edge_value) +
                       " 使用次数为 " + std::to_string(use_count);
      std::vector<std::uint64_t> related {shell_id.value, edge_value};
      append_face_outer_loops(edge_value, related);
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoOpenBoundary, msg,
                                 "壳未闭合",
                                 std::move(related));
    }
    if (use_count > 2) {
      const auto msg = "壳非流形：边 " + std::to_string(edge_value) +
                       " 使用次数为 " + std::to_string(use_count);
      std::vector<std::uint64_t> related {shell_id.value, edge_value};
      append_face_outer_loops(edge_value, related);
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoNonManifoldEdge, msg,
                                 "壳非流形",
                                 std::move(related));
    }
  }
  return ok_void(state_->create_diagnostic("壳闭合性验证通过"));
}

Result<void>
TopologyValidationService::validate_body_sources(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kCoreInvalidHandle,
        "体来源验证失败：目标体不存在", "体来源验证失败");
  }
  if (has_duplicate_ids(
          std::span<const BodyId>(body_it->second.source_bodies)) ||
      !validate_source_bodies_exist(*state_, body_it->second.source_bodies)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoSourceRefInvalid,
        "体来源验证失败：来源体引用非法", "体来源验证失败");
  }
  if (has_duplicate_ids(
          std::span<const ShellId>(body_it->second.source_shells)) ||
      !validate_source_shells_exist(*state_, body_it->second.source_shells)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoSourceRefInvalid,
        "体来源验证失败：来源壳引用非法", "体来源验证失败");
  }
  if (has_duplicate_ids(
          std::span<const FaceId>(body_it->second.source_faces)) ||
      !validate_source_faces_exist(*state_, body_it->second.source_faces)) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoSourceRefInvalid,
        "体来源验证失败：来源面引用非法", "体来源验证失败");
  }
  return ok_void(state_->create_diagnostic("体来源验证通过"));
}

Result<void>
TopologyValidationService::validate_body_bbox(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kCoreInvalidHandle,
        "体包围盒验证失败：目标体不存在", "体包围盒验证失败");
  }
  const auto bbox = compute_body_bbox(*state_, body_it->second.shells);
  if (!bbox.is_valid) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "体包围盒验证失败：无法从壳推导有效包围盒", "体包围盒验证失败");
  }
  return ok_void(state_->create_diagnostic("体包围盒验证通过"));
}

Result<void> TopologyValidationService::validate_body(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "体验证失败：目标实体不存在", "体验证失败");
  }
  if (body_it->second.rep_kind == RepKind::ExactBRep &&
      body_it->second.shells.empty()) {
    return detail::failed_void(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "体验证失败：精确 B-Rep 体不包含任何壳", "体验证失败");
  }
  if (has_duplicate_ids(std::span<const ShellId>(body_it->second.shells))) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "体验证失败：体包含重复壳引用", "体验证失败");
  }
  // Body ↔ shell_to_bodies: each shell owned by the body must reverse-index this body.
  for (const auto shell_id : body_it->second.shells) {
    if (!detail::has_shell(*state_, shell_id)) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "体验证失败：体引用了不存在的壳", "体验证失败",
          {body_id.value, shell_id.value});
    }
    const auto stb_it = state_->shell_to_bodies.find(shell_id.value);
    if (stb_it == state_->shell_to_bodies.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "体验证失败：壳在 shell_to_bodies 中缺少反向索引条目", "体验证失败",
          {body_id.value, shell_id.value});
    }
    const bool lists_this_body =
        std::any_of(stb_it->second.begin(), stb_it->second.end(),
                    [bv = body_id.value](std::uint64_t x) { return x == bv; });
    if (!lists_this_body) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "体验证失败：shell_to_bodies 未列出本体（索引与体成员不一致）",
          "体验证失败", {body_id.value, shell_id.value});
    }
  }
  for (const auto shell_id : body_it->second.shells) {
    const auto result = validate_shell(shell_id);
    if (result.status != StatusCode::Ok) {
      // Preserve the underlying diagnostic details from shell/face validation.
      return result;
    }
  }
  auto source_valid = validate_body_sources(body_id);
  if (source_valid.status != StatusCode::Ok) {
    return source_valid;
  }
  auto bbox_valid = validate_body_bbox(body_id);
  if (bbox_valid.status != StatusCode::Ok) {
    return bbox_valid;
  }
  const auto idx_valid = validate_body_topology_indices(body_id);
  if (idx_valid.status != StatusCode::Ok) {
    return idx_valid;
  }
  return ok_void(state_->create_diagnostic("体验证通过"));
}

Result<void>
TopologyValidationService::validate_body_topology_indices(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "体索引验证失败：目标实体不存在", "体索引验证失败");
  }
  std::unordered_set<std::uint64_t> face_ids;
  for (const auto shell_id : body_it->second.shells) {
    const auto shell_it = state_->shells.find(shell_id.value);
    if (shell_it == state_->shells.end()) {
      continue;
    }
    for (const auto face_id : shell_it->second.faces) {
      face_ids.insert(face_id.value);
    }
  }
  if (face_ids.empty()) {
    return ok_void(state_->create_diagnostic("体索引验证跳过（无拓扑面）"));
  }
  std::unordered_set<std::uint64_t> loop_ids;
  for (const auto face_value : face_ids) {
    const auto fit = state_->faces.find(face_value);
    if (fit == state_->faces.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "体索引验证失败：壳引用了不存在的面", "体索引验证失败",
          {body_id.value, face_value});
    }
    loop_ids.insert(fit->second.outer_loop.value);
    for (const auto inner : fit->second.inner_loops) {
      loop_ids.insert(inner.value);
    }
  }
  std::unordered_set<std::uint64_t> coedge_ids;
  std::unordered_set<std::uint64_t> edge_ids;
  for (const auto loop_value : loop_ids) {
    const auto lit = state_->loops.find(loop_value);
    if (lit == state_->loops.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "体索引验证失败：面引用了不存在的环", "体索引验证失败",
          {body_id.value, loop_value});
    }
    for (const auto coedge_id : lit->second.coedges) {
      coedge_ids.insert(coedge_id.value);
      const auto cit = state_->coedges.find(coedge_id.value);
      if (cit == state_->coedges.end()) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
            "体索引验证失败：环引用了不存在的定向边", "体索引验证失败",
            {loop_value, coedge_id.value});
      }
      const auto cl = state_->coedge_to_loop.find(coedge_id.value);
      if (cl == state_->coedge_to_loop.end() || cl->second != loop_value) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "体索引验证失败：coedge_to_loop 与环成员不一致", "体索引验证失败",
            {coedge_id.value, loop_value});
      }
      edge_ids.insert(cit->second.edge_id.value);
    }
  }
  for (const auto loop_value : loop_ids) {
    std::uint64_t owner_face = 0;
    unsigned owner_count = 0;
    for (const auto face_value : face_ids) {
      const auto& fr = state_->faces.at(face_value);
      if (face_record_references_loop(fr, loop_value)) {
        ++owner_count;
        owner_face = face_value;
      }
    }
    if (owner_count != 1U) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          owner_count == 0 ? diag_codes::kTopoOrphanLoop
                           : diag_codes::kTopoInvariantBroken,
          "体索引验证失败：环在体的面集合中缺少唯一所属面", "体索引验证失败",
          {body_id.value, loop_value});
    }
    const auto ltf = state_->loop_to_faces.find(loop_value);
    if (ltf == state_->loop_to_faces.end() || ltf->second.empty()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoOrphanLoop,
          "体索引验证失败：loop_to_faces 缺少环条目", "体索引验证失败",
          {loop_value});
    }
    std::unordered_set<std::uint64_t> listed;
    for (const auto fv : ltf->second) {
      if (!listed.insert(fv).second) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "体索引验证失败：loop_to_faces 存在重复面", "体索引验证失败",
            {loop_value, fv});
      }
    }
    if (listed.size() != 1U || listed.count(owner_face) == 0U) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "体索引验证失败：loop_to_faces 与体的所属面不一致", "体索引验证失败",
          {loop_value, owner_face});
    }
  }
  for (const auto edge_value : edge_ids) {
    if (state_->edges.find(edge_value) == state_->edges.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoCurveTopologyMismatch,
          "体索引验证失败：定向边引用了不存在的边", "体索引验证失败",
          {edge_value});
    }
    const auto ec_it = state_->edge_to_coedges.find(edge_value);
    if (ec_it == state_->edge_to_coedges.end() || ec_it->second.empty()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoDanglingEdge,
          "体索引验证失败：边缺少 edge_to_coedges 反向索引", "体索引验证失败",
          {edge_value, body_id.value});
    }
    std::unordered_set<std::uint64_t> rev_seen;
    rev_seen.reserve(ec_it->second.size());
    for (const auto co_value : ec_it->second) {
      if (!rev_seen.insert(co_value).second) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "体索引验证失败：edge_to_coedges 存在重复定向边", "体索引验证失败",
            {edge_value, co_value});
      }
    }
    for (const auto co_value : coedge_ids) {
      const auto cit = state_->coedges.find(co_value);
      if (cit == state_->coedges.end() ||
          cit->second.edge_id.value != edge_value) {
        continue;
      }
      if (rev_seen.count(co_value) == 0U) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "体索引验证失败：体的共边未列入 edge_to_coedges", "体索引验证失败",
            {edge_value, co_value});
      }
    }
  }
  // Shell ↔ face_to_shells: every (body shell, face) membership must appear in reverse index.
  for (const auto face_value : face_ids) {
    for (const auto shell_id : body_it->second.shells) {
      const auto shell_it = state_->shells.find(shell_id.value);
      if (shell_it == state_->shells.end()) {
        continue;
      }
      const bool shell_has_face = std::any_of(
          shell_it->second.faces.begin(), shell_it->second.faces.end(),
          [face_value](FaceId fid) { return fid.value == face_value; });
      if (!shell_has_face) {
        continue;
      }
      const auto fts_it = state_->face_to_shells.find(face_value);
      if (fts_it == state_->face_to_shells.end()) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "体索引验证失败：face_to_shells 缺少面条目（壳已引用该面）",
            "体索引验证失败", {body_id.value, face_value, shell_id.value});
      }
      const bool lists_shell = std::any_of(
          fts_it->second.begin(), fts_it->second.end(),
          [sv = shell_id.value](std::uint64_t x) { return x == sv; });
      if (!lists_shell) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "体索引验证失败：face_to_shells 未列出体的壳", "体索引验证失败",
            {body_id.value, face_value, shell_id.value});
      }
    }
  }
  return ok_void(state_->create_diagnostic("体拓扑索引验证通过"));
}

Result<void>
TopologyValidationService::validate_body_closedness(BodyId body_id) const {
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                               diag_codes::kTopoShellNotClosed,
                               "体闭合性验证失败：目标实体不存在",
                               "体闭合性验证失败");
  }
  for (const auto shell_id : body_it->second.shells) {
    const auto r = validate_shell_closedness(shell_id);
    if (r.status != StatusCode::Ok) {
      return r;
    }
  }
  return ok_void(state_->create_diagnostic("体闭合性验证通过"));
}

Result<void> TopologyValidationService::validate_indices_consistency() const {
  for (const auto &[edge_value, coedge_values] : state_->edge_to_coedges) {
    if (state_->edges.find(edge_value) == state_->edges.end()) {
      return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                 diag_codes::kTopoCurveTopologyMismatch,
                                 "索引验证失败：edge_to_coedges 包含失效边",
                                 "拓扑索引验证失败");
    }
    for (const auto coedge_value : coedge_values) {
      const auto coedge_it = state_->coedges.find(coedge_value);
      if (coedge_it == state_->coedges.end() ||
          coedge_it->second.edge_id.value != edge_value) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoCurveTopologyMismatch,
            "索引验证失败：edge_to_coedges 与 coedges 不一致",
            "拓扑索引验证失败");
      }
    }
  }
  for (const auto &[face_value, shell_values] : state_->face_to_shells) {
    if (state_->faces.find(face_value) == state_->faces.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "索引验证失败：face_to_shells 包含失效面", "拓扑索引验证失败");
    }
    std::unordered_set<std::uint64_t> seen_shell;
    seen_shell.reserve(shell_values.size());
    for (const auto shell_value : shell_values) {
      if (!seen_shell.insert(shell_value).second) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "索引验证失败：face_to_shells 存在重复壳条目", "拓扑索引验证失败",
            {face_value, shell_value});
      }
      const auto shell_it = state_->shells.find(shell_value);
      if (shell_it == state_->shells.end() ||
          std::none_of(
              shell_it->second.faces.begin(), shell_it->second.faces.end(),
              [face_value](FaceId id) { return id.value == face_value; })) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoShellNotClosed,
            "索引验证失败：face_to_shells 与 shells 不一致",
            "拓扑索引验证失败");
      }
    }
  }
  for (const auto &[shell_value, body_values] : state_->shell_to_bodies) {
    if (state_->shells.find(shell_value) == state_->shells.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "索引验证失败：shell_to_bodies 包含失效壳", "拓扑索引验证失败");
    }
    std::unordered_set<std::uint64_t> seen_body;
    seen_body.reserve(body_values.size());
    for (const auto body_value : body_values) {
      if (!seen_body.insert(body_value).second) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "索引验证失败：shell_to_bodies 存在重复体条目", "拓扑索引验证失败",
            {shell_value, body_value});
      }
      const auto body_it = state_->bodies.find(body_value);
      if (body_it == state_->bodies.end() ||
          std::none_of(
              body_it->second.shells.begin(), body_it->second.shells.end(),
              [shell_value](ShellId id) { return id.value == shell_value; })) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoShellNotClosed,
            "索引验证失败：shell_to_bodies 与 bodies 不一致",
            "拓扑索引验证失败");
      }
    }
  }

  // Forward containment: duplicate FaceId in shell.faces / ShellId in body.shells
  // (index corruption or partial writes; mirrors validate_shell / validate_body).
  for (const auto &[shell_value, shell] : state_->shells) {
    if (has_duplicate_ids(std::span<const FaceId>(shell.faces))) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoDuplicateFaceInShell,
          "索引验证失败：壳的面列表存在重复条目", "拓扑索引验证失败",
          {shell_value});
    }
  }
  for (const auto &[body_value, body] : state_->bodies) {
    if (has_duplicate_ids(std::span<const ShellId>(body.shells))) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "索引验证失败：体的壳列表存在重复条目", "拓扑索引验证失败",
          {body_value});
    }
  }

  for (const auto &[coedge_value, loop_value] : state_->coedge_to_loop) {
    if (state_->coedges.find(coedge_value) == state_->coedges.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "索引验证失败：coedge_to_loop 包含失效定向边", "拓扑索引验证失败",
          {coedge_value, loop_value});
    }
    const auto lit = state_->loops.find(loop_value);
    if (lit == state_->loops.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "索引验证失败：coedge_to_loop 指向不存在的环", "拓扑索引验证失败",
          {coedge_value, loop_value});
    }
    const bool listed = std::any_of(
        lit->second.coedges.begin(), lit->second.coedges.end(),
        [coedge_value](CoedgeId cid) { return cid.value == coedge_value; });
    if (!listed) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "索引验证失败：定向边索引与环成员列表不一致", "拓扑索引验证失败",
          {coedge_value, loop_value});
    }
  }

  for (const auto &[coedge_value, coedge] : state_->coedges) {
    (void)coedge;
    if (state_->coedge_to_loop.find(coedge_value) ==
        state_->coedge_to_loop.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoDanglingCoedge,
          "索引验证失败：定向边未被任何环引用", "拓扑索引验证失败",
          {coedge_value});
    }
  }

  // Reverse index: every Edge record must be referenced by at least one Coedge
  // (edge_to_coedges rebuilt from coedges). Catches committed "edge-only" writes
  // and index corruption after partial updates.
  for (const auto &[edge_value, edge] : state_->edges) {
    (void)edge;
    const auto owners_it = state_->edge_to_coedges.find(edge_value);
    if (owners_it == state_->edge_to_coedges.end() ||
        owners_it->second.empty()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoDanglingEdge,
          "索引验证失败：边在 edge_to_coedges 中无定向边引用（悬挂边）",
          "拓扑索引验证失败", {edge_value});
    }
    std::unordered_set<std::uint64_t> seen_coedge;
    seen_coedge.reserve(owners_it->second.size());
    for (const auto coedge_value : owners_it->second) {
      if (!seen_coedge.insert(coedge_value).second) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "索引验证失败：edge_to_coedges 存在重复定向边条目",
            "拓扑索引验证失败", {edge_value, coedge_value});
      }
    }
  }

  for (const auto &[loop_value, loop] : state_->loops) {
    (void)loop;
    const auto ltf = state_->loop_to_faces.find(loop_value);
    if (ltf == state_->loop_to_faces.end() || ltf->second.empty()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoOrphanLoop,
          "索引验证失败：环未被任何面引用", "拓扑索引验证失败",
          {loop_value});
    }
  }

  for (const auto &[loop_value, face_values] : state_->loop_to_faces) {
    if (state_->loops.find(loop_value) == state_->loops.end()) {
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "索引验证失败：loop_to_faces 包含失效环", "拓扑索引验证失败",
          {loop_value});
    }
    std::unordered_set<std::uint64_t> loop_face_owners;
    loop_face_owners.reserve(face_values.size());
    for (const auto face_value : face_values) {
      if (!loop_face_owners.insert(face_value).second) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "索引验证失败：loop_to_faces 存在重复面条目", "拓扑索引验证失败",
            {loop_value, face_value});
      }
    }
    if (loop_face_owners.size() > 1U) {
      auto it = loop_face_owners.begin();
      const std::uint64_t fa = *it++;
      const std::uint64_t fb = *it;
      return detail::failed_void(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoRelationInconsistent,
          "索引验证失败：同一环被多个面引用（loop_to_faces）", "拓扑索引验证失败",
          {loop_value, fa, fb});
    }
    for (const auto face_value : face_values) {
      const auto fit = state_->faces.find(face_value);
      if (fit == state_->faces.end()) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
            "索引验证失败：loop_to_faces 引用不存在的面", "拓扑索引验证失败",
            {loop_value, face_value});
      }
      if (!face_record_references_loop(fit->second, loop_value)) {
        return detail::failed_void(
            *state_, StatusCode::InvalidTopology,
            diag_codes::kTopoRelationInconsistent,
            "索引验证失败：loop_to_faces 与面记录不一致", "拓扑索引验证失败",
            {loop_value, face_value});
      }
    }
  }

  return ok_void(state_->create_diagnostic("拓扑索引验证通过"));
}

Result<void> TopologyValidationService::validate_face_many(
    std::span<const FaceId> face_ids) const {
  if (face_ids.empty()) {
    return detail::invalid_input_void(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "面批量验证失败：输入为空", "面批量验证失败");
  }
  for (const auto face_id : face_ids) {
    const auto r = validate_face(face_id);
    if (r.status != StatusCode::Ok)
      return r;
  }
  return ok_void(state_->create_diagnostic("面批量验证通过"));
}

Result<void> TopologyValidationService::validate_shell_many(
    std::span<const ShellId> shell_ids) const {
  if (shell_ids.empty()) {
    return detail::invalid_input_void(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "壳批量验证失败：输入为空", "壳批量验证失败");
  }
  for (const auto shell_id : shell_ids) {
    const auto r = validate_shell(shell_id);
    if (r.status != StatusCode::Ok)
      return r;
  }
  return ok_void(state_->create_diagnostic("壳批量验证通过"));
}

Result<void> TopologyValidationService::validate_body_many(
    std::span<const BodyId> body_ids) const {
  if (body_ids.empty()) {
    return detail::invalid_input_void(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "体批量验证失败：输入为空", "体批量验证失败");
  }
  for (const auto body_id : body_ids) {
    const auto r = validate_body(body_id);
    if (r.status != StatusCode::Ok)
      return r;
  }
  return ok_void(state_->create_diagnostic("体批量验证通过"));
}

Result<bool> TopologyValidationService::is_face_valid(FaceId face_id) const {
  const auto r = validate_face(face_id);
  if (r.status == StatusCode::InvalidInput)
    return error_result<bool>(r.status, r.diagnostic_id);
  return ok_result(r.status == StatusCode::Ok,
                   state_->create_diagnostic("已查询面有效性"));
}

Result<bool> TopologyValidationService::is_shell_valid(ShellId shell_id) const {
  const auto r = validate_shell(shell_id);
  if (r.status == StatusCode::InvalidInput)
    return error_result<bool>(r.status, r.diagnostic_id);
  return ok_result(r.status == StatusCode::Ok,
                   state_->create_diagnostic("已查询壳有效性"));
}

Result<bool> TopologyValidationService::is_body_valid(BodyId body_id) const {
  const auto r = validate_body(body_id);
  if (r.status == StatusCode::InvalidInput)
    return error_result<bool>(r.status, r.diagnostic_id);
  return ok_result(r.status == StatusCode::Ok,
                   state_->create_diagnostic("已查询体有效性"));
}

Result<std::uint64_t> TopologyValidationService::count_invalid_faces(
    std::span<const FaceId> face_ids) const {
  std::uint64_t count = 0;
  for (const auto face_id : face_ids) {
    if (validate_face(face_id).status != StatusCode::Ok)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计无效面数量"));
}

Result<std::uint64_t> TopologyValidationService::count_invalid_shells(
    std::span<const ShellId> shell_ids) const {
  std::uint64_t count = 0;
  for (const auto shell_id : shell_ids) {
    if (validate_shell(shell_id).status != StatusCode::Ok)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计无效壳数量"));
}

Result<std::uint64_t> TopologyValidationService::count_invalid_bodies(
    std::span<const BodyId> body_ids) const {
  std::uint64_t count = 0;
  for (const auto body_id : body_ids) {
    if (validate_body(body_id).status != StatusCode::Ok)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计无效体数量"));
}

Result<FaceId> TopologyValidationService::first_invalid_face(
    std::span<const FaceId> face_ids) const {
  for (const auto face_id : face_ids) {
    if (validate_face(face_id).status != StatusCode::Ok) {
      return ok_result(face_id, state_->create_diagnostic("已找到首个无效面"));
    }
  }
  return detail::failed_result<FaceId>(*state_, StatusCode::OperationFailed,
                                       diag_codes::kCoreParameterOutOfRange,
                                       "无效面查询失败：输入中不存在无效面",
                                       "无效面查询失败");
}

Result<ShellId> TopologyValidationService::first_invalid_shell(
    std::span<const ShellId> shell_ids) const {
  for (const auto shell_id : shell_ids) {
    if (validate_shell(shell_id).status != StatusCode::Ok) {
      return ok_result(shell_id, state_->create_diagnostic("已找到首个无效壳"));
    }
  }
  return detail::failed_result<ShellId>(*state_, StatusCode::OperationFailed,
                                        diag_codes::kCoreParameterOutOfRange,
                                        "无效壳查询失败：输入中不存在无效壳",
                                        "无效壳查询失败");
}

Result<BodyId> TopologyValidationService::first_invalid_body(
    std::span<const BodyId> body_ids) const {
  for (const auto body_id : body_ids) {
    if (validate_body(body_id).status != StatusCode::Ok) {
      return ok_result(body_id, state_->create_diagnostic("已找到首个无效体"));
    }
  }
  return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed,
                                       diag_codes::kCoreParameterOutOfRange,
                                       "无效体查询失败：输入中不存在无效体",
                                       "无效体查询失败");
}

TopologyService::TopologyService(std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)), query_service_(state_),
      validation_service_(state_) {}

TopologyTransaction TopologyService::begin_transaction() {
  return TopologyTransaction{state_};
}

TopologyQueryService &TopologyService::query() { return query_service_; }

TopologyValidationService &TopologyService::validate() {
  return validation_service_;
}

} // namespace axiom
