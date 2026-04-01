#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "axiom/core/types.h"
#include "axiom/internal/core/kernel_state.h"

namespace axiom {
namespace topo_internal {

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
                                 std::span<const FaceId> faces);

bool validate_source_shells_exist(const detail::KernelState &state,
                                  std::span<const ShellId> shells);

bool validate_source_bodies_exist(const detail::KernelState &state,
                                  std::span<const BodyId> bodies);

bool extend_bbox(BoundingBox &bbox, const Point3 &point);

bool append_edge_bbox(const detail::KernelState &state, EdgeId edge_id,
                      BoundingBox &bbox);

bool append_loop_bbox(const detail::KernelState &state, LoopId loop_id,
                      BoundingBox &bbox);

bool append_face_bbox(const detail::KernelState &state, FaceId face_id,
                      BoundingBox &bbox);

BoundingBox compute_body_bbox(const detail::KernelState &state,
                              std::span<const ShellId> shells);

std::optional<std::array<VertexId, 2>>
oriented_vertices(const detail::KernelState &state, CoedgeId coedge_id);

Vec3 newell_normal_unnormalized(const std::vector<Point3> &poly);

bool loop_vertex_chain_3d(const detail::KernelState &state, LoopId loop_id,
                           std::vector<Point3> &out, std::string &reason);

bool validate_loop_record(const detail::KernelState &state,
                          const detail::LoopRecord &loop, std::string &reason);

bool validate_loop_id(const detail::KernelState &state, LoopId loop_id,
                      std::string &reason);

bool face_record_references_loop(const detail::FaceRecord &face,
                                 std::uint64_t loop_value);

void append_unique_raw(std::vector<std::uint64_t> &values,
                       std::uint64_t value);

void rebuild_topology_indices(detail::KernelState &state);

SurfaceId underlying_trim_base_surface_id(const detail::KernelState &state,
                                            SurfaceId start);

bool finite_scalar(Scalar s);

bool accumulate_polyline_pcurve_uv_bounds(const detail::PCurveRecord &pc,
                                            bool &initialized, Scalar &u_min,
                                            Scalar &u_max, Scalar &v_min,
                                            Scalar &v_max);

void append_coedge_polyline_to_uv_path(const detail::PCurveRecord &pc, bool reversed,
                                       std::vector<Point2> &out);

void ensure_strict_increasing_range(Scalar &lo, Scalar &hi);

}  // namespace topo_internal
}  // namespace axiom
