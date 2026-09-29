#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <limits>
#include <span>
#include <utility>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include "axiom/internal/core/kernel_state.h"

namespace axiom::detail {

template <typename RawId>
inline void append_unique_raw_id(std::vector<RawId>& values, RawId value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

inline bool extend_materialization_bbox(BoundingBox& bbox, const Point3& point) {
    if (!bbox.is_valid) {
        bbox = BoundingBox {point, point, true};
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

inline bool append_edge_bbox(KernelState& state, EdgeId edge_id, BoundingBox& bbox) {
    const auto edge_it = state.edges.find(edge_id.value);
    if (edge_it == state.edges.end()) {
        return false;
    }
    const auto v0_it = state.vertices.find(edge_it->second.v0.value);
    const auto v1_it = state.vertices.find(edge_it->second.v1.value);
    if (v0_it == state.vertices.end() || v1_it == state.vertices.end()) {
        return false;
    }
    extend_materialization_bbox(bbox, v0_it->second.point);
    extend_materialization_bbox(bbox, v1_it->second.point);
    return true;
}

inline bool append_loop_bbox(KernelState& state, LoopId loop_id, BoundingBox& bbox) {
    const auto loop_it = state.loops.find(loop_id.value);
    if (loop_it == state.loops.end()) {
        return false;
    }
    for (const auto coedge_id : loop_it->second.coedges) {
        const auto coedge_it = state.coedges.find(coedge_id.value);
        if (coedge_it == state.coedges.end() || !append_edge_bbox(state, coedge_it->second.edge_id, bbox)) {
            return false;
        }
    }
    return true;
}

inline bool append_face_bbox(KernelState& state, FaceId face_id, BoundingBox& bbox) {
    const auto face_it = state.faces.find(face_id.value);
    if (face_it == state.faces.end()) {
        return false;
    }
    if (!append_loop_bbox(state, face_it->second.outer_loop, bbox)) {
        return false;
    }
    for (const auto inner_loop : face_it->second.inner_loops) {
        if (!append_loop_bbox(state, inner_loop, bbox)) {
            return false;
        }
    }
    return true;
}

inline BoundingBox compute_faces_bbox(KernelState& state, std::span<const FaceId> faces) {
    BoundingBox bbox {};
    for (const auto face_id : faces) {
        if (!append_face_bbox(state, face_id, bbox)) {
            return {};
        }
    }
    return bbox;
}

template <typename Id>
inline bool has_duplicate_materialization_ids(std::span<const Id> ids) {
    std::vector<std::uint64_t> values;
    values.reserve(ids.size());
    for (const auto id : ids) {
        values.push_back(id.value);
    }
    std::sort(values.begin(), values.end());
    return std::adjacent_find(values.begin(), values.end()) != values.end();
}

inline void rebuild_topology_links(KernelState& state) {
    state.edge_to_coedges.clear();
    state.coedge_to_loop.clear();
    state.loop_to_faces.clear();
    state.face_to_shells.clear();
    state.shell_to_bodies.clear();

    for (const auto& [coedge_value, coedge] : state.coedges) {
        state.edge_to_coedges[coedge.edge_id.value].push_back(coedge_value);
    }

    for (const auto& [loop_value, loop] : state.loops) {
        for (const auto coedge_id : loop.coedges) {
            state.coedge_to_loop[coedge_id.value] = loop_value;
        }
    }

    for (const auto& [face_value, face] : state.faces) {
        append_unique_raw_id(state.loop_to_faces[face.outer_loop.value], face_value);
        for (const auto loop_id : face.inner_loops) {
            append_unique_raw_id(state.loop_to_faces[loop_id.value], face_value);
        }
    }

    for (const auto& [shell_value, shell] : state.shells) {
        for (const auto face_id : shell.faces) {
            append_unique_raw_id(state.face_to_shells[face_id.value], shell_value);
        }
    }

    for (const auto& [body_value, body] : state.bodies) {
        for (const auto shell_id : body.shells) {
            append_unique_raw_id(state.shell_to_bodies[shell_id.value], body_value);
        }
    }
}

inline std::array<int, 3> choose_materialization_axes(const BoundingBox& bbox) {
    const std::array<Scalar, 3> extents {
        bbox.max.x - bbox.min.x,
        bbox.max.y - bbox.min.y,
        bbox.max.z - bbox.min.z,
    };
    std::array<int, 3> axes {0, 1, 2};
    std::sort(axes.begin(), axes.end(), [&extents](int lhs, int rhs) {
        return extents[lhs] > extents[rhs];
    });
    return axes;
}

inline Point3 materialization_corner(const BoundingBox& bbox, int axis_u, int axis_v, bool high_u, bool high_v) {
    std::array<Scalar, 3> coords {bbox.min.x, bbox.min.y, bbox.min.z};
    const std::array<Scalar, 3> mins {bbox.min.x, bbox.min.y, bbox.min.z};
    const std::array<Scalar, 3> maxs {bbox.max.x, bbox.max.y, bbox.max.z};
    coords[axis_u] = high_u ? maxs[axis_u] : mins[axis_u];
    coords[axis_v] = high_v ? maxs[axis_v] : mins[axis_v];
    return Point3 {coords[0], coords[1], coords[2]};
}

inline Vec3 axis_normal(int axis, bool positive) {
    switch (axis) {
        case 0:
            return Vec3 {positive ? 1.0 : -1.0, 0.0, 0.0};
        case 1:
            return Vec3 {0.0, positive ? 1.0 : -1.0, 0.0};
        default:
            return Vec3 {0.0, 0.0, positive ? 1.0 : -1.0};
    }
}

inline CurveId create_materialized_line(KernelState& state, const Point3& start, const Point3& end) {
    CurveRecord curve;
    curve.kind = CurveKind::Line;
    curve.origin = start;
    curve.direction = normalize(subtract(end, start));
    const auto curve_id = CurveId {state.allocate_id()};
    state.curves.emplace(curve_id.value, std::move(curve));
    return curve_id;
}

inline bool validate_closed_shell_edge_usage_for_materialization(const KernelState& state, ShellId shell_id) {
    const auto shell_it = state.shells.find(shell_id.value);
    if (shell_it == state.shells.end() || shell_it->second.faces.empty()) {
        return false;
    }

    std::unordered_map<std::uint64_t, std::size_t> edge_use_count;
    for (const auto face_id : shell_it->second.faces) {
        const auto face_it = state.faces.find(face_id.value);
        if (face_it == state.faces.end()) {
            return false;
        }
        std::vector<LoopId> loops;
        loops.push_back(face_it->second.outer_loop);
        loops.insert(loops.end(), face_it->second.inner_loops.begin(), face_it->second.inner_loops.end());
        for (const auto loop_id : loops) {
            const auto loop_it = state.loops.find(loop_id.value);
            if (loop_it == state.loops.end()) {
                return false;
            }
            for (const auto coedge_id : loop_it->second.coedges) {
                const auto coedge_it = state.coedges.find(coedge_id.value);
                if (coedge_it == state.coedges.end()) {
                    return false;
                }
                ++edge_use_count[coedge_it->second.edge_id.value];
            }
        }
    }

    if (edge_use_count.empty()) {
        return false;
    }
    return std::all_of(edge_use_count.begin(), edge_use_count.end(),
                       [](const auto& entry) { return entry.second == 2; });
}

inline std::optional<std::array<VertexId, 2>> oriented_vertices_for_coedge(const KernelState& state, CoedgeId coedge_id) {
    const auto coedge_it = state.coedges.find(coedge_id.value);
    if (coedge_it == state.coedges.end()) {
        return std::nullopt;
    }
    const auto edge_it = state.edges.find(coedge_it->second.edge_id.value);
    if (edge_it == state.edges.end()) {
        return std::nullopt;
    }
    if (!has_vertex(state, edge_it->second.v0) || !has_vertex(state, edge_it->second.v1) ||
        edge_it->second.v0.value == edge_it->second.v1.value) {
        return std::nullopt;
    }
    if (coedge_it->second.reversed) {
        return std::array<VertexId, 2> {edge_it->second.v1, edge_it->second.v0};
    }
    return std::array<VertexId, 2> {edge_it->second.v0, edge_it->second.v1};
}

inline bool validate_loop_connectivity_for_materialization(const KernelState& state, LoopId loop_id) {
    const auto loop_it = state.loops.find(loop_id.value);
    if (loop_it == state.loops.end() || loop_it->second.coedges.empty()) {
        return false;
    }
    std::optional<VertexId> first_start;
    std::optional<VertexId> previous_end;
    for (const auto coedge_id : loop_it->second.coedges) {
        const auto oriented = oriented_vertices_for_coedge(state, coedge_id);
        if (!oriented.has_value()) {
            return false;
        }
        if (!first_start.has_value()) {
            first_start = (*oriented)[0];
        }
        if (previous_end.has_value() && previous_end->value != (*oriented)[0].value) {
            return false;
        }
        previous_end = (*oriented)[1];
    }
    return first_start.has_value() && previous_end.has_value() &&
           first_start->value == previous_end->value;
}

inline bool validate_shell_loop_consistency_for_materialization(const KernelState& state, ShellId shell_id) {
    const auto shell_it = state.shells.find(shell_id.value);
    if (shell_it == state.shells.end() || shell_it->second.faces.empty()) {
        return false;
    }
    for (const auto face_id : shell_it->second.faces) {
        const auto face_it = state.faces.find(face_id.value);
        if (face_it == state.faces.end()) {
            return false;
        }
        if (!validate_loop_connectivity_for_materialization(state, face_it->second.outer_loop)) {
            return false;
        }
        for (const auto inner_loop : face_it->second.inner_loops) {
            if (!validate_loop_connectivity_for_materialization(state, inner_loop)) {
                return false;
            }
        }
    }
    return true;
}

inline std::vector<FaceId> faces_sharing_edge_in_shell(const KernelState& state,
                                                       ShellId shell_id,
                                                       std::uint64_t edge_value) {
    std::vector<FaceId> faces;
    const auto shell_it = state.shells.find(shell_id.value);
    if (shell_it == state.shells.end()) {
        return faces;
    }
    const auto coedge_it = state.edge_to_coedges.find(edge_value);
    if (coedge_it == state.edge_to_coedges.end()) {
        return faces;
    }
    for (const auto coedge_value : coedge_it->second) {
        const auto loop_it = state.coedge_to_loop.find(coedge_value);
        if (loop_it == state.coedge_to_loop.end()) {
            continue;
        }
        const auto face_it = state.loop_to_faces.find(loop_it->second);
        if (face_it == state.loop_to_faces.end()) {
            continue;
        }
        for (const auto candidate_face_value : face_it->second) {
            const auto in_shell = std::any_of(shell_it->second.faces.begin(), shell_it->second.faces.end(),
                                              [candidate_face_value](FaceId shell_face) {
                                                  return shell_face.value == candidate_face_value;
                                              });
            if (!in_shell) {
                continue;
            }
            append_unique_raw_id(faces, FaceId {candidate_face_value});
        }
    }
    return faces;
}

inline bool count_face_edges_for_region(const KernelState& state,
                                        std::span<const FaceId> selected_faces,
                                        std::unordered_map<std::uint64_t, std::size_t>& edge_use_count) {
    edge_use_count.clear();
    for (const auto face_id : selected_faces) {
        const auto face_it = state.faces.find(face_id.value);
        if (face_it == state.faces.end()) {
            return false;
        }
        std::vector<LoopId> loops;
        loops.push_back(face_it->second.outer_loop);
        loops.insert(loops.end(), face_it->second.inner_loops.begin(), face_it->second.inner_loops.end());
        for (const auto loop_id : loops) {
            const auto loop_it = state.loops.find(loop_id.value);
            if (loop_it == state.loops.end()) {
                return false;
            }
            for (const auto coedge_id : loop_it->second.coedges) {
                const auto coedge_it = state.coedges.find(coedge_id.value);
                if (coedge_it == state.coedges.end()) {
                    return false;
                }
                ++edge_use_count[coedge_it->second.edge_id.value];
            }
        }
    }
    return true;
}

inline std::vector<FaceId> build_closed_face_region_from_source_faces(const KernelState& state,
                                                                      ShellId source_shell,
                                                                      std::span<const FaceId> source_faces) {
    std::vector<FaceId> selected;
    const auto shell_it = state.shells.find(source_shell.value);
    if (shell_it == state.shells.end()) {
        return selected;
    }

    for (const auto face_id : source_faces) {
        const auto in_shell = std::any_of(shell_it->second.faces.begin(), shell_it->second.faces.end(),
                                          [face_id](FaceId shell_face) { return shell_face.value == face_id.value; });
        if (in_shell) {
            append_unique_raw_id(selected, face_id);
        }
    }
    if (selected.empty()) {
        return selected;
    }

    std::unordered_map<std::uint64_t, std::size_t> edge_use_count;
    bool grown = true;
    while (grown) {
        grown = false;
        if (!count_face_edges_for_region(state, selected, edge_use_count)) {
            return {};
        }

        std::vector<std::uint64_t> boundary_edges;
        for (const auto& [edge_value, use_count] : edge_use_count) {
            if (use_count == 1) {
                boundary_edges.push_back(edge_value);
            }
        }
        if (boundary_edges.empty()) {
            break;
        }

        for (const auto edge_value : boundary_edges) {
            const auto adjacent_faces = faces_sharing_edge_in_shell(state, source_shell, edge_value);
            for (const auto face_id : adjacent_faces) {
                const auto already = std::any_of(selected.begin(), selected.end(),
                                                 [face_id](FaceId current) { return current.value == face_id.value; });
                if (already) {
                    continue;
                }
                selected.push_back(face_id);
                grown = true;
            }
        }
    }

    if (!count_face_edges_for_region(state, selected, edge_use_count)) {
        return {};
    }
    for (const auto& [_, use_count] : edge_use_count) {
        if (use_count != 2) {
            return {};
        }
    }
    return selected;
}

inline void inherit_source_topology_from_owned_shells(const KernelState& state, BodyRecord& record) {
    for (const auto shell_id : record.shells) {
        append_unique_raw_id(record.source_shells, shell_id);
        const auto shell_it = state.shells.find(shell_id.value);
        if (shell_it == state.shells.end()) {
            continue;
        }
        for (const auto face_id : shell_it->second.faces) {
            append_unique_raw_id(record.source_faces, face_id);
        }
    }
}

inline void infer_source_shells_from_source_faces(const KernelState& state, BodyRecord& record) {
    if (record.source_faces.empty()) {
        return;
    }
    auto shell_is_owned_by_source_bodies = [&state, &record](std::uint64_t shell_value) {
        if (record.source_bodies.empty()) {
            return true;
        }
        const auto owners_it = state.shell_to_bodies.find(shell_value);
        if (owners_it == state.shell_to_bodies.end()) {
            return false;
        }
        return std::any_of(owners_it->second.begin(), owners_it->second.end(),
                           [&record](std::uint64_t body_value) {
                               return std::any_of(record.source_bodies.begin(), record.source_bodies.end(),
                                                  [body_value](BodyId source_body) {
                                                      return source_body.value == body_value;
                                                  });
                           });
    };

    std::vector<ShellId> preferred_shells;
    std::vector<ShellId> fallback_shells;
    for (const auto face_id : record.source_faces) {
        const auto it = state.face_to_shells.find(face_id.value);
        if (it == state.face_to_shells.end()) {
            continue;
        }
        for (const auto shell_value : it->second) {
            if (shell_is_owned_by_source_bodies(shell_value)) {
                append_unique_raw_id(preferred_shells, ShellId {shell_value});
            } else {
                append_unique_raw_id(fallback_shells, ShellId {shell_value});
            }
        }
    }

    if (!preferred_shells.empty()) {
        for (const auto shell_id : preferred_shells) {
            append_unique_raw_id(record.source_shells, shell_id);
        }
        return;
    }
    for (const auto shell_id : fallback_shells) {
        append_unique_raw_id(record.source_shells, shell_id);
    }
}

inline void sanitize_source_references(const KernelState& state, BodyRecord& record) {
    std::vector<BodyId> valid_bodies;
    for (const auto body_id : record.source_bodies) {
        if (has_body(state, body_id)) {
            append_unique_raw_id(valid_bodies, body_id);
        }
    }
    record.source_bodies = std::move(valid_bodies);

    std::vector<ShellId> valid_shells;
    for (const auto shell_id : record.source_shells) {
        if (has_shell(state, shell_id)) {
            append_unique_raw_id(valid_shells, shell_id);
        }
    }
    record.source_shells = std::move(valid_shells);

    std::vector<FaceId> valid_faces;
    for (const auto face_id : record.source_faces) {
        if (has_face(state, face_id)) {
            append_unique_raw_id(valid_faces, face_id);
        }
    }
    if (!record.source_shells.empty()) {
        std::vector<FaceId> shell_consistent_faces;
        for (const auto face_id : valid_faces) {
            const auto shell_it = state.face_to_shells.find(face_id.value);
            if (shell_it == state.face_to_shells.end()) {
                continue;
            }
            const bool belongs_to_source_shell =
                std::any_of(shell_it->second.begin(), shell_it->second.end(), [&record](std::uint64_t shell_value) {
                    return std::any_of(record.source_shells.begin(), record.source_shells.end(),
                                       [shell_value](ShellId source_shell) { return source_shell.value == shell_value; });
                });
            if (belongs_to_source_shell) {
                append_unique_raw_id(shell_consistent_faces, face_id);
            }
        }
        valid_faces = std::move(shell_consistent_faces);
    }
    record.source_faces = std::move(valid_faces);
}

inline std::vector<FaceId> sanitize_face_source_refs(const KernelState& state, std::span<const FaceId> source_faces) {
    std::vector<FaceId> sanitized;
    for (const auto face_id : source_faces) {
        if (has_face(state, face_id)) {
            append_unique_raw_id(sanitized, face_id);
        }
    }
    return sanitized;
}

inline VertexId clone_materialized_vertex(KernelState& state,
                                          VertexId source_vertex,
                                          std::unordered_map<std::uint64_t, VertexId>& vertex_map) {
    const auto existing = vertex_map.find(source_vertex.value);
    if (existing != vertex_map.end()) {
        return existing->second;
    }
    const auto source_it = state.vertices.find(source_vertex.value);
    const auto cloned = VertexId {state.allocate_id()};
    state.vertices.emplace(cloned.value, VertexRecord {source_it->second.point});
    vertex_map.emplace(source_vertex.value, cloned);
    return cloned;
}

inline EdgeId clone_materialized_edge(KernelState& state,
                                      EdgeId source_edge,
                                      std::unordered_map<std::uint64_t, VertexId>& vertex_map,
                                      std::unordered_map<std::uint64_t, EdgeId>& edge_map) {
    const auto existing = edge_map.find(source_edge.value);
    if (existing != edge_map.end()) {
        return existing->second;
    }
    const auto source_it = state.edges.find(source_edge.value);
    const auto v0 = clone_materialized_vertex(state, source_it->second.v0, vertex_map);
    const auto v1 = clone_materialized_vertex(state, source_it->second.v1, vertex_map);
    const auto cloned = EdgeId {state.allocate_id()};
    state.edges.emplace(cloned.value, EdgeRecord {source_it->second.curve_id, v0, v1});
    edge_map.emplace(source_edge.value, cloned);
    return cloned;
}

inline LoopId clone_materialized_loop(KernelState& state,
                                      LoopId source_loop,
                                      std::unordered_map<std::uint64_t, VertexId>& vertex_map,
                                      std::unordered_map<std::uint64_t, EdgeId>& edge_map) {
    const auto source_it = state.loops.find(source_loop.value);
    std::vector<CoedgeId> coedges;
    coedges.reserve(source_it->second.coedges.size());
    for (const auto source_coedge_id : source_it->second.coedges) {
        const auto source_coedge_it = state.coedges.find(source_coedge_id.value);
        const auto cloned_edge =
            clone_materialized_edge(state, source_coedge_it->second.edge_id, vertex_map, edge_map);
        const auto cloned_coedge = CoedgeId {state.allocate_id()};
        state.coedges.emplace(cloned_coedge.value, CoedgeRecord {cloned_edge, source_coedge_it->second.reversed});
        coedges.push_back(cloned_coedge);
    }
    const auto cloned_loop = LoopId {state.allocate_id()};
    state.loops.emplace(cloned_loop.value, LoopRecord {std::move(coedges)});
    return cloned_loop;
}

inline ShellId clone_materialized_shell(KernelState& state, ShellId source_shell) {
    const auto shell_it = state.shells.find(source_shell.value);
    std::unordered_map<std::uint64_t, VertexId> vertex_map;
    std::unordered_map<std::uint64_t, EdgeId> edge_map;
    std::vector<FaceId> cloned_faces;
    cloned_faces.reserve(shell_it->second.faces.size());

    for (const auto source_face_id : shell_it->second.faces) {
        const auto source_face_it = state.faces.find(source_face_id.value);
        const auto cloned_outer = clone_materialized_loop(state, source_face_it->second.outer_loop, vertex_map, edge_map);

        std::vector<LoopId> cloned_inner_loops;
        cloned_inner_loops.reserve(source_face_it->second.inner_loops.size());
        for (const auto inner_loop_id : source_face_it->second.inner_loops) {
            cloned_inner_loops.push_back(clone_materialized_loop(state, inner_loop_id, vertex_map, edge_map));
        }

        const auto cloned_face = FaceId {state.allocate_id()};
        FaceRecord face;
        face.surface_id = source_face_it->second.surface_id;
        face.outer_loop = cloned_outer;
        face.inner_loops = std::move(cloned_inner_loops);
        if (source_face_it->second.source_faces.empty()) {
            face.source_faces = sanitize_face_source_refs(state, std::span<const FaceId>(&source_face_id, 1));
        } else {
            face.source_faces = sanitize_face_source_refs(state, std::span<const FaceId>(source_face_it->second.source_faces));
            if (face.source_faces.empty()) {
                face.source_faces = sanitize_face_source_refs(state, std::span<const FaceId>(&source_face_id, 1));
            }
        }
        state.faces.emplace(cloned_face.value, std::move(face));
        cloned_faces.push_back(cloned_face);
    }

    const auto cloned_shell = ShellId {state.allocate_id()};
    ShellRecord shell;
    shell.faces = std::move(cloned_faces);
    shell.source_shells.push_back(source_shell);
    if (shell_it->second.source_faces.empty()) {
        shell.source_faces = sanitize_face_source_refs(state, std::span<const FaceId>(shell_it->second.faces));
    } else {
        shell.source_faces = sanitize_face_source_refs(state, std::span<const FaceId>(shell_it->second.source_faces));
    }
    if (shell.source_faces.empty()) {
        shell.source_faces = sanitize_face_source_refs(state, std::span<const FaceId>(shell_it->second.faces));
    }
    state.shells.emplace(cloned_shell.value, std::move(shell));
    return cloned_shell;
}

inline ShellId clone_materialized_shell_with_faces(KernelState& state,
                                                   ShellId source_shell,
                                                   std::span<const FaceId> source_faces) {
    const auto shell_it = state.shells.find(source_shell.value);
    std::unordered_map<std::uint64_t, VertexId> vertex_map;
    std::unordered_map<std::uint64_t, EdgeId> edge_map;
    std::vector<FaceId> cloned_faces;
    cloned_faces.reserve(source_faces.size());

    for (const auto source_face_id : source_faces) {
        const auto in_shell = std::any_of(shell_it->second.faces.begin(), shell_it->second.faces.end(),
                                          [source_face_id](FaceId shell_face) {
                                              return shell_face.value == source_face_id.value;
                                          });
        if (!in_shell) {
            continue;
        }
        const auto source_face_it = state.faces.find(source_face_id.value);
        const auto cloned_outer = clone_materialized_loop(state, source_face_it->second.outer_loop, vertex_map, edge_map);

        std::vector<LoopId> cloned_inner_loops;
        cloned_inner_loops.reserve(source_face_it->second.inner_loops.size());
        for (const auto inner_loop_id : source_face_it->second.inner_loops) {
            cloned_inner_loops.push_back(clone_materialized_loop(state, inner_loop_id, vertex_map, edge_map));
        }

        const auto cloned_face = FaceId {state.allocate_id()};
        FaceRecord face;
        face.surface_id = source_face_it->second.surface_id;
        face.outer_loop = cloned_outer;
        face.inner_loops = std::move(cloned_inner_loops);
        if (source_face_it->second.source_faces.empty()) {
            face.source_faces = sanitize_face_source_refs(state, std::span<const FaceId>(&source_face_id, 1));
        } else {
            face.source_faces = sanitize_face_source_refs(state, std::span<const FaceId>(source_face_it->second.source_faces));
            if (face.source_faces.empty()) {
                face.source_faces = sanitize_face_source_refs(state, std::span<const FaceId>(&source_face_id, 1));
            }
        }
        state.faces.emplace(cloned_face.value, std::move(face));
        cloned_faces.push_back(cloned_face);
    }

    const auto cloned_shell = ShellId {state.allocate_id()};
    ShellRecord shell;
    shell.faces = std::move(cloned_faces);
    shell.source_shells.push_back(source_shell);
    if (shell_it->second.source_faces.empty()) {
        shell.source_faces = sanitize_face_source_refs(state, source_faces);
    } else {
        shell.source_faces = sanitize_face_source_refs(state, std::span<const FaceId>(shell_it->second.source_faces));
    }
    if (shell.source_faces.empty()) {
        shell.source_faces = sanitize_face_source_refs(state, source_faces);
    }
    state.shells.emplace(cloned_shell.value, std::move(shell));
    return cloned_shell;
}

inline bool materialize_body_from_source_shells(KernelState& state, BodyRecord& record) {
    if (record.source_shells.empty() || has_duplicate_materialization_ids(std::span<const ShellId>(record.source_shells))) {
        return false;
    }
    std::vector<ShellId> valid_shells;
    for (const auto shell_id : record.source_shells) {
        if (!has_shell(state, shell_id)) {
            continue;
        }
        if (!validate_closed_shell_edge_usage_for_materialization(state, shell_id)) {
            continue;
        }
        if (!validate_shell_loop_consistency_for_materialization(state, shell_id)) {
            continue;
        }
        append_unique_raw_id(valid_shells, shell_id);
    }
    if (valid_shells.empty()) {
        return false;
    }
    std::vector<ShellId> cloned_shells;
    for (const auto shell_id : valid_shells) {
        const auto cloned_shell = clone_materialized_shell(state, shell_id);
        if (!validate_shell_loop_consistency_for_materialization(state, cloned_shell)) {
            continue;
        }
        if (!validate_closed_shell_edge_usage_for_materialization(state, cloned_shell)) {
            continue;
        }
        cloned_shells.push_back(cloned_shell);
    }
    if (cloned_shells.empty()) {
        return false;
    }
    record.shells.insert(record.shells.end(), cloned_shells.begin(), cloned_shells.end());
    return true;
}

inline std::vector<ShellId> rank_source_shell_candidates(const KernelState& state,
                                                         const BodyRecord& record,
                                                         std::span<const FaceId> source_faces) {
    struct CandidateScore {
        ShellId shell_id {};
        int body_affinity {0};
        int overlap_faces {0};
        int shell_face_count {0};
    };

    std::vector<CandidateScore> scored;
    scored.reserve(record.source_shells.size());

    for (const auto shell_id : record.source_shells) {
        const auto shell_it = state.shells.find(shell_id.value);
        if (shell_it == state.shells.end()) {
            continue;
        }
        CandidateScore s;
        s.shell_id = shell_id;
        s.shell_face_count = static_cast<int>(shell_it->second.faces.size());

        if (!record.source_bodies.empty()) {
            const auto owners_it = state.shell_to_bodies.find(shell_id.value);
            if (owners_it != state.shell_to_bodies.end()) {
                for (const auto owner_body_value : owners_it->second) {
                    const auto owner_is_source = std::any_of(
                        record.source_bodies.begin(), record.source_bodies.end(),
                        [owner_body_value](BodyId source_body) { return source_body.value == owner_body_value; });
                    if (owner_is_source) {
                        ++s.body_affinity;
                    }
                }
            }
        }

        for (const auto face_id : source_faces) {
            const auto in_shell = std::any_of(shell_it->second.faces.begin(), shell_it->second.faces.end(),
                                              [face_id](FaceId shell_face) { return shell_face.value == face_id.value; });
            if (in_shell) {
                ++s.overlap_faces;
            }
        }

        scored.push_back(s);
    }

    std::sort(scored.begin(), scored.end(), [](const CandidateScore& lhs, const CandidateScore& rhs) {
        if (lhs.body_affinity != rhs.body_affinity) {
            return lhs.body_affinity > rhs.body_affinity;
        }
        if (lhs.overlap_faces != rhs.overlap_faces) {
            return lhs.overlap_faces > rhs.overlap_faces;
        }
        if (lhs.shell_face_count != rhs.shell_face_count) {
            return lhs.shell_face_count < rhs.shell_face_count;
        }
        return lhs.shell_id.value < rhs.shell_id.value;
    });

    std::vector<ShellId> ordered;
    ordered.reserve(scored.size());
    for (const auto& item : scored) {
        if (item.overlap_faces > 0) {
            ordered.push_back(item.shell_id);
        }
    }
    return ordered;
}

inline bool materialize_body_from_source_faces(KernelState& state, BodyRecord& record) {
    if (record.source_faces.empty()) {
        return false;
    }
    if (has_duplicate_materialization_ids(std::span<const FaceId>(record.source_faces))) {
        return false;
    }
    std::vector<FaceId> valid_source_faces;
    for (const auto face_id : record.source_faces) {
        if (has_face(state, face_id)) {
            append_unique_raw_id(valid_source_faces, face_id);
        }
    }
    if (valid_source_faces.empty()) {
        return false;
    }
    if (record.source_shells.empty() || has_duplicate_materialization_ids(std::span<const ShellId>(record.source_shells))) {
        return false;
    }

    const auto shell_candidates =
        rank_source_shell_candidates(state, record, std::span<const FaceId>(valid_source_faces));
    for (const auto source_shell : shell_candidates) {
        if (!has_shell(state, source_shell)) {
            continue;
        }
        const auto selected_faces =
            build_closed_face_region_from_source_faces(state, source_shell, std::span<const FaceId>(valid_source_faces));
        if (selected_faces.empty()) {
            continue;
        }
        const auto cloned_shell = clone_materialized_shell_with_faces(state, source_shell, selected_faces);
        if (!validate_shell_loop_consistency_for_materialization(state, cloned_shell)) {
            continue;
        }
        if (!validate_closed_shell_edge_usage_for_materialization(state, cloned_shell)) {
            continue;
        }
        record.shells.push_back(cloned_shell);
        return true;
    }
    return false;
}

inline FaceId create_materialized_face(KernelState& state,
                                       const std::array<EdgeId, 12>& edges,
                                       std::array<VertexId, 8> vertices,
                                       const std::array<int, 4>& corner_indices,
                                       const std::array<std::pair<int, bool>, 4>& edge_refs,
                                       int normal_axis,
                                       bool positive_normal,
                                       std::span<const FaceId> source_faces) {
    SurfaceRecord surface;
    surface.kind = SurfaceKind::Plane;
    surface.origin = state.vertices.at(vertices[corner_indices[0]].value).point;
    surface.normal = axis_normal(normal_axis, positive_normal);
    const auto surface_id = SurfaceId {state.allocate_id()};
    state.surfaces.emplace(surface_id.value, std::move(surface));

    std::vector<CoedgeId> coedges;
    coedges.reserve(edge_refs.size());
    for (const auto& [edge_index, reversed] : edge_refs) {
        const auto coedge_id = CoedgeId {state.allocate_id()};
        state.coedges.emplace(coedge_id.value, CoedgeRecord {edges[edge_index], reversed});
        coedges.push_back(coedge_id);
    }

    const auto loop_id = LoopId {state.allocate_id()};
    state.loops.emplace(loop_id.value, LoopRecord {std::move(coedges)});

    const auto face_id = FaceId {state.allocate_id()};
    FaceRecord face;
    face.surface_id = surface_id;
    face.outer_loop = loop_id;
    if (source_faces.empty()) {
        face.source_faces.push_back(face_id);
    } else {
        face.source_faces.assign(source_faces.begin(), source_faces.end());
    }
    state.faces.emplace(face_id.value, std::move(face));
    return face_id;
}

inline Vec3 safe_unit_normal(const Vec3& normal) {
    const auto n = norm(normal);
    if (!(n > 0.0)) {
        return Vec3 {0.0, 0.0, 1.0};
    }
    return scale(normal, 1.0 / n);
}

inline FaceId create_materialized_polygon_face(KernelState& state,
                                               const std::vector<EdgeId>& edges,
                                               std::span<const std::pair<int, bool>> edge_refs,
                                               const Vec3& normal,
                                               std::span<const FaceId> source_faces) {
    if (edge_refs.empty() || edges.empty()) {
        return {};
    }
    SurfaceRecord surface;
    surface.kind = SurfaceKind::Plane;
    const auto first_edge = edges[static_cast<std::size_t>(edge_refs[0].first)];
    const auto first_edge_it = state.edges.find(first_edge.value);
    if (first_edge_it != state.edges.end()) {
        const auto v0_it = state.vertices.find(first_edge_it->second.v0.value);
        if (v0_it != state.vertices.end()) {
            surface.origin = v0_it->second.point;
        }
    }
    surface.normal = safe_unit_normal(normal);
    const auto surface_id = SurfaceId {state.allocate_id()};
    state.surfaces.emplace(surface_id.value, std::move(surface));

    std::vector<CoedgeId> coedges;
    coedges.reserve(edge_refs.size());
    for (const auto& [edge_index, reversed] : edge_refs) {
        const auto idx = static_cast<std::size_t>(edge_index);
        if (idx >= edges.size()) {
            continue;
        }
        const auto coedge_id = CoedgeId {state.allocate_id()};
        state.coedges.emplace(coedge_id.value, CoedgeRecord {edges[idx], reversed});
        coedges.push_back(coedge_id);
    }

    const auto loop_id = LoopId {state.allocate_id()};
    state.loops.emplace(loop_id.value, LoopRecord {std::move(coedges)});

    const auto face_id = FaceId {state.allocate_id()};
    FaceRecord face;
    face.surface_id = surface_id;
    face.outer_loop = loop_id;
    if (source_faces.empty()) {
        face.source_faces.push_back(face_id);
    } else {
        face.source_faces.assign(source_faces.begin(), source_faces.end());
    }
    state.faces.emplace(face_id.value, std::move(face));
    return face_id;
}

inline void orthonormal_frame_from_axis(const Vec3& axis_unit, Vec3& u, Vec3& v) {
    const auto a = axis_unit;
    Vec3 ref = (std::abs(a.z) < 0.9) ? Vec3 {0.0, 0.0, 1.0} : Vec3 {1.0, 0.0, 0.0};
    u = cross(ref, a);
    if (norm(u) <= 1e-14) {
        ref = Vec3 {0.0, 1.0, 0.0};
        u = cross(ref, a);
    }
    u = normalize(u);
    v = cross(a, u);
}

inline void materialize_body_wedge_shell(KernelState& state, BodyRecord& record) {
    if (record.kind != BodyKind::Wedge || record.rep_kind != RepKind::ExactBRep || !record.bbox.is_valid ||
        !record.shells.empty()) {
        return;
    }
    const auto o = record.origin;
    const auto dx = record.a;
    const auto dy = record.b;
    const auto dz = record.c;
    if (!(dx > 0.0 && dy > 0.0 && dz > 0.0)) {
        return;
    }

    const std::array<Point3, 6> corners {
        Point3 {o.x, o.y, o.z},
        Point3 {o.x + dx, o.y, o.z},
        Point3 {o.x, o.y + dy, o.z},
        Point3 {o.x, o.y, o.z + dz},
        Point3 {o.x + dx, o.y, o.z + dz},
        Point3 {o.x, o.y + dy, o.z + dz},
    };

    std::array<VertexId, 6> vertices {};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        vertices[i] = VertexId {state.allocate_id()};
        state.vertices.emplace(vertices[i].value, VertexRecord {corners[i]});
    }

    const std::array<std::pair<int, int>, 9> edge_vertices {{
        {0, 1}, {1, 2}, {2, 0},
        {3, 4}, {4, 5}, {5, 3},
        {0, 3}, {1, 4}, {2, 5},
    }};
    std::vector<EdgeId> edges;
    edges.reserve(edge_vertices.size());
    for (std::size_t i = 0; i < edge_vertices.size(); ++i) {
        const auto s = edge_vertices[i].first;
        const auto t = edge_vertices[i].second;
        const auto curve_id = create_materialized_line(state, corners[s], corners[t]);
        const auto eid = EdgeId {state.allocate_id()};
        state.edges.emplace(eid.value, EdgeRecord {curve_id, vertices[s], vertices[t]});
        edges.push_back(eid);
    }

    const auto source_faces = std::span<const FaceId>(record.source_faces);
    std::vector<FaceId> faces;
    faces.reserve(5);

    {
        const std::array<std::pair<int, bool>, 3> refs {{{2, true}, {1, true}, {0, true}}};
        faces.push_back(create_materialized_polygon_face(state, edges, std::span<const std::pair<int, bool>>(refs),
                                                         Vec3 {0.0, 0.0, -1.0}, source_faces));
    }
    {
        const std::array<std::pair<int, bool>, 3> refs {{{3, false}, {4, false}, {5, false}}};
        faces.push_back(create_materialized_polygon_face(state, edges, std::span<const std::pair<int, bool>>(refs),
                                                         Vec3 {0.0, 0.0, 1.0}, source_faces));
    }
    {
        const std::array<std::pair<int, bool>, 4> refs {{{0, false}, {7, false}, {3, true}, {6, true}}};
        faces.push_back(create_materialized_polygon_face(state, edges, std::span<const std::pair<int, bool>>(refs),
                                                         Vec3 {0.0, -1.0, 0.0}, source_faces));
    }
    {
        const std::array<std::pair<int, bool>, 4> refs {{{6, false}, {5, true}, {8, true}, {2, false}}};
        faces.push_back(create_materialized_polygon_face(state, edges, std::span<const std::pair<int, bool>>(refs),
                                                         Vec3 {-1.0, 0.0, 0.0}, source_faces));
    }
    {
        const std::array<std::pair<int, bool>, 4> refs {{{1, false}, {8, false}, {4, true}, {7, true}}};
        faces.push_back(create_materialized_polygon_face(state, edges, std::span<const std::pair<int, bool>>(refs),
                                                         Vec3 {1.0, 1.0, 0.0}, source_faces));
    }

    const auto shell_id = ShellId {state.allocate_id()};
    ShellRecord shell;
    shell.faces = std::move(faces);
    shell.source_shells = record.source_shells;
    if (record.source_faces.empty()) {
        shell.source_faces = shell.faces;
    } else {
        shell.source_faces = record.source_faces;
    }
    state.shells.emplace(shell_id.value, std::move(shell));
    record.shells.push_back(shell_id);
}

inline void materialize_body_prism_cylinder_shell(KernelState& state, BodyRecord& record) {
    if (record.kind != BodyKind::Cylinder || record.rep_kind != RepKind::ExactBRep || !record.bbox.is_valid ||
        !record.shells.empty()) {
        return;
    }
    const auto r = record.a;
    const auto h = record.b;
    if (!(r > 0.0 && h > 0.0)) {
        return;
    }
    constexpr int N = 8;
    Vec3 u {};
    Vec3 v {};
    orthonormal_frame_from_axis(record.axis, u, v);
    const auto C = record.origin;
    const auto C0 = add_point_vec(C, scale(record.axis, -h * 0.5));
    const auto C1 = add_point_vec(C, scale(record.axis, h * 0.5));

    std::array<Point3, static_cast<std::size_t>(2 * N)> corners {};
      for (int i = 0; i < N; ++i) {
        const Scalar th = 2.0 * 3.14159265358979323846 * static_cast<Scalar>(i) / static_cast<Scalar>(N);
        const Scalar cs = std::cos(th);
        const Scalar sn = std::sin(th);
        const Vec3 rad {u.x * r * cs + v.x * r * sn, u.y * r * cs + v.y * r * sn, u.z * r * cs + v.z * r * sn};
        corners[static_cast<std::size_t>(i)] = add_point_vec(C0, rad);
        corners[static_cast<std::size_t>(N + i)] = add_point_vec(C1, rad);
    }

    std::array<VertexId, static_cast<std::size_t>(2 * N)> vertices {};
    for (int i = 0; i < 2 * N; ++i) {
        vertices[static_cast<std::size_t>(i)] = VertexId {state.allocate_id()};
        state.vertices.emplace(vertices[static_cast<std::size_t>(i)].value, VertexRecord {corners[static_cast<std::size_t>(i)]});
    }

    std::vector<EdgeId> edges;
    edges.reserve(static_cast<std::size_t>(3 * N));
    for (int i = 0; i < N; ++i) {
        const int j = (i + 1) % N;
        const auto curve_id =
            create_materialized_line(state, corners[static_cast<std::size_t>(i)], corners[static_cast<std::size_t>(j)]);
        const auto eid = EdgeId {state.allocate_id()};
        state.edges.emplace(eid.value,
                            EdgeRecord {curve_id, vertices[static_cast<std::size_t>(i)],
                                        vertices[static_cast<std::size_t>(j)]});
        edges.push_back(eid);
    }
    for (int i = 0; i < N; ++i) {
        const int j = (i + 1) % N;
        const auto curve_id = create_materialized_line(
            state, corners[static_cast<std::size_t>(N + i)], corners[static_cast<std::size_t>(N + j)]);
        const auto eid = EdgeId {state.allocate_id()};
        state.edges.emplace(eid.value,
                            EdgeRecord {curve_id, vertices[static_cast<std::size_t>(N + i)],
                                        vertices[static_cast<std::size_t>(N + j)]});
        edges.push_back(eid);
    }
    for (int i = 0; i < N; ++i) {
        const auto curve_id =
            create_materialized_line(state, corners[static_cast<std::size_t>(i)], corners[static_cast<std::size_t>(N + i)]);
        const auto eid = EdgeId {state.allocate_id()};
        state.edges.emplace(eid.value,
                            EdgeRecord {curve_id, vertices[static_cast<std::size_t>(i)],
                                        vertices[static_cast<std::size_t>(N + i)]});
        edges.push_back(eid);
    }

    const auto source_faces = std::span<const FaceId>(record.source_faces);
    std::vector<FaceId> out_faces;
    out_faces.reserve(static_cast<std::size_t>(N + 2));

    for (int i = 0; i < N; ++i) {
        const int j = (i + 1) % N;
        const Scalar th = 2.0 * 3.14159265358979323846 * (static_cast<Scalar>(i) + 0.5) / static_cast<Scalar>(N);
        const Scalar cs = std::cos(th);
        const Scalar sn = std::sin(th);
        const Vec3 nout {u.x * cs + v.x * sn, u.y * cs + v.y * sn, u.z * cs + v.z * sn};
        const std::array<std::pair<int, bool>, 4> refs {{
            {i, false},
            {2 * N + j, false},
            {N + i, true},
            {2 * N + i, true},
        }};
        out_faces.push_back(create_materialized_polygon_face(state, edges, std::span<const std::pair<int, bool>>(refs), nout,
                                                             source_faces));
    }

    {
        std::vector<std::pair<int, bool>> bottom_refs;
        bottom_refs.reserve(static_cast<std::size_t>(N));
        for (int k = N - 1; k >= 0; --k) {
            bottom_refs.push_back({k, true});
        }
        out_faces.push_back(create_materialized_polygon_face(
            state, edges, std::span<const std::pair<int, bool>>(bottom_refs.data(), bottom_refs.size()),
            scale(record.axis, -1.0), source_faces));
    }
    {
        std::vector<std::pair<int, bool>> top_refs;
        top_refs.reserve(static_cast<std::size_t>(N));
        for (int k = 0; k < N; ++k) {
            top_refs.push_back({N + k, false});
        }
        out_faces.push_back(create_materialized_polygon_face(
            state, edges, std::span<const std::pair<int, bool>>(top_refs.data(), top_refs.size()), record.axis, source_faces));
    }

    const auto shell_id = ShellId {state.allocate_id()};
    ShellRecord shell;
    shell.faces = std::move(out_faces);
    shell.source_shells = record.source_shells;
    if (record.source_faces.empty()) {
        shell.source_faces = shell.faces;
    } else {
        shell.source_faces = record.source_faces;
    }
    state.shells.emplace(shell_id.value, std::move(shell));
    record.shells.push_back(shell_id);
}

inline void materialize_body_bbox_shell(KernelState& state, BodyRecord& record) {
    if (record.rep_kind != RepKind::ExactBRep || !record.bbox.is_valid || !record.shells.empty()) {
        return;
    }

    // Ensure the placeholder closed shell is non-degenerate even when the source bbox is planar/linear.
    // This keeps Stage-2 materialized results passable under strict topology validation.
    {
        const auto eps = std::max<Scalar>(state.config.tolerance.linear, 1e-6);
        const auto ex = record.bbox.max.x - record.bbox.min.x;
        const auto ey = record.bbox.max.y - record.bbox.min.y;
        const auto ez = record.bbox.max.z - record.bbox.min.z;
        if (!(ex > eps)) { record.bbox.min.x -= eps * 0.5; record.bbox.max.x += eps * 0.5; }
        if (!(ey > eps)) { record.bbox.min.y -= eps * 0.5; record.bbox.max.y += eps * 0.5; }
        if (!(ez > eps)) { record.bbox.min.z -= eps * 0.5; record.bbox.max.z += eps * 0.5; }
    }

    const std::array<Point3, 8> corners {
        Point3 {record.bbox.min.x, record.bbox.min.y, record.bbox.min.z},
        Point3 {record.bbox.max.x, record.bbox.min.y, record.bbox.min.z},
        Point3 {record.bbox.max.x, record.bbox.max.y, record.bbox.min.z},
        Point3 {record.bbox.min.x, record.bbox.max.y, record.bbox.min.z},
        Point3 {record.bbox.min.x, record.bbox.min.y, record.bbox.max.z},
        Point3 {record.bbox.max.x, record.bbox.min.y, record.bbox.max.z},
        Point3 {record.bbox.max.x, record.bbox.max.y, record.bbox.max.z},
        Point3 {record.bbox.min.x, record.bbox.max.y, record.bbox.max.z},
    };

    std::array<VertexId, 8> vertices {};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        vertices[i] = VertexId {state.allocate_id()};
        state.vertices.emplace(vertices[i].value, VertexRecord {corners[i]});
    }

    const std::array<std::pair<int, int>, 12> edge_vertices {{
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    }};
    std::array<EdgeId, 12> edges {};
    for (std::size_t i = 0; i < edge_vertices.size(); ++i) {
        const auto start = vertices[edge_vertices[i].first];
        const auto end = vertices[edge_vertices[i].second];
        const auto curve_id = create_materialized_line(state, corners[edge_vertices[i].first], corners[edge_vertices[i].second]);
        edges[i] = EdgeId {state.allocate_id()};
        state.edges.emplace(edges[i].value, EdgeRecord {curve_id, start, end});
    }

    std::vector<FaceId> faces;
    faces.reserve(6);
    const auto source_faces = std::span<const FaceId>(record.source_faces);

    faces.push_back(create_materialized_face(state, edges, vertices, {0, 3, 2, 1},
                                             {{{3, true}, {2, true}, {1, true}, {0, true}}},
                                             2, false, source_faces));
    faces.push_back(create_materialized_face(state, edges, vertices, {4, 5, 6, 7},
                                             {{{4, false}, {5, false}, {6, false}, {7, false}}},
                                             2, true, source_faces));
    faces.push_back(create_materialized_face(state, edges, vertices, {0, 1, 5, 4},
                                             {{{0, false}, {9, false}, {4, true}, {8, true}}},
                                             1, false, source_faces));
    faces.push_back(create_materialized_face(state, edges, vertices, {3, 7, 6, 2},
                                             {{{11, false}, {6, true}, {10, true}, {2, false}}},
                                             1, true, source_faces));
    faces.push_back(create_materialized_face(state, edges, vertices, {0, 4, 7, 3},
                                             {{{8, false}, {7, true}, {11, true}, {3, false}}},
                                             0, false, source_faces));
    faces.push_back(create_materialized_face(state, edges, vertices, {1, 2, 6, 5},
                                             {{{1, false}, {10, false}, {5, true}, {9, true}}},
                                             0, true, source_faces));

    const auto shell_id = ShellId {state.allocate_id()};
    ShellRecord shell;
    shell.faces = std::move(faces);
    shell.source_shells = record.source_shells;
    if (record.source_faces.empty()) {
        shell.source_faces = shell.faces;
    } else {
        shell.source_faces = record.source_faces;
    }
    state.shells.emplace(shell_id.value, std::move(shell));

    record.shells.push_back(shell_id);
}

inline Vec3 newell_normal_unnormalized_poly(std::span<const Point3> poly) {
    Vec3 n {0.0, 0.0, 0.0};
    if (poly.size() < 3) {
        return n;
    }
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const auto& p0 = poly[i];
        const auto& p1 = poly[(i + 1) % poly.size()];
        n.x += (p0.y - p1.y) * (p0.z + p1.z);
        n.y += (p0.z - p1.z) * (p0.x + p1.x);
        n.z += (p0.x - p1.x) * (p0.y + p1.y);
    }
    return n;
}

inline Point3 rodrigues_rotate_point_revolve(const Point3& p, const Point3& O, const Vec3& u, Scalar cos_t,
                                             Scalar sin_t) {
    const Vec3 v {p.x - O.x, p.y - O.y, p.z - O.z};
    const auto uxv = cross(u, v);
    const auto udotv = dot(u, v);
    const auto omc = 1.0 - cos_t;
    const Vec3 vr {v.x * cos_t + uxv.x * sin_t + u.x * udotv * omc,
                   v.y * cos_t + uxv.y * sin_t + u.y * udotv * omc,
                   v.z * cos_t + uxv.z * sin_t + u.z * udotv * omc};
    return add_point_vec(O, vr);
}

/// David Eberly, Polyhedral Mass Properties (triangular faces), ρ=1。输出关于质心的惯性张量（世界系，对称）。
inline void polyhedral_mass_properties_from_triangles(const std::vector<Point3>& p,
                                                      const std::vector<std::array<int, 3>>& index,
                                                      Scalar& out_volume, Point3& out_cm,
                                                      std::array<Scalar, 9>& out_inertia, Scalar& out_area) {
    out_volume = 0.0;
    out_cm = Point3 {0.0, 0.0, 0.0};
    out_area = 0.0;
    out_inertia = {};
    if (p.empty() || index.empty()) {
        return;
    }
    constexpr Scalar mult[10] {1.0 / 6.0,  1.0 / 24.0, 1.0 / 24.0, 1.0 / 24.0, 1.0 / 60.0,
                                1.0 / 60.0, 1.0 / 60.0, 1.0 / 120.0, 1.0 / 120.0, 1.0 / 120.0};
    Scalar intg[10] {};
    for (const auto& tri : index) {
        const int i0 = tri[0];
        const int i1 = tri[1];
        const int i2 = tri[2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || static_cast<std::size_t>(i0) >= p.size() ||
            static_cast<std::size_t>(i1) >= p.size() || static_cast<std::size_t>(i2) >= p.size()) {
            continue;
        }
        const Scalar x0 = p[static_cast<std::size_t>(i0)].x;
        const Scalar y0 = p[static_cast<std::size_t>(i0)].y;
        const Scalar z0 = p[static_cast<std::size_t>(i0)].z;
        const Scalar x1 = p[static_cast<std::size_t>(i1)].x;
        const Scalar y1 = p[static_cast<std::size_t>(i1)].y;
        const Scalar z1 = p[static_cast<std::size_t>(i1)].z;
        const Scalar x2 = p[static_cast<std::size_t>(i2)].x;
        const Scalar y2 = p[static_cast<std::size_t>(i2)].y;
        const Scalar z2 = p[static_cast<std::size_t>(i2)].z;
        const Scalar a1 = x1 - x0;
        const Scalar b1 = y1 - y0;
        const Scalar c1 = z1 - z0;
        const Scalar a2 = x2 - x0;
        const Scalar b2 = y2 - y0;
        const Scalar c2 = z2 - z0;
        const Scalar d0 = b1 * c2 - b2 * c1;
        const Scalar d1 = a2 * c1 - a1 * c2;
        const Scalar d2 = a1 * b2 - a2 * b1;
        const auto e1 = subtract(p[static_cast<std::size_t>(i1)], p[static_cast<std::size_t>(i0)]);
        const auto e2 = subtract(p[static_cast<std::size_t>(i2)], p[static_cast<std::size_t>(i0)]);
        out_area += 0.5 * norm(cross(e1, e2));

        auto subexpr = [](Scalar w0, Scalar w1, Scalar w2, Scalar& f1, Scalar& f2, Scalar& f3, Scalar& g0,
                          Scalar& g1, Scalar& g2) {
            const Scalar temp0 = w0 + w1;
            f1 = temp0 + w2;
            const Scalar temp1 = w0 * w0;
            const Scalar temp2 = temp1 + w1 * temp0;
            f2 = temp2 + w2 * f1;
            f3 = w0 * temp1 + w1 * temp2 + w2 * f2;
            g0 = f2 + w0 * (f1 + w0);
            g1 = f2 + w1 * (f1 + w1);
            g2 = f2 + w2 * (f1 + w2);
        };
        Scalar f1x, f2x, f3x, g0x, g1x, g2x;
        Scalar f1y, f2y, f3y, g0y, g1y, g2y;
        Scalar f1z, f2z, f3z, g0z, g1z, g2z;
        subexpr(x0, x1, x2, f1x, f2x, f3x, g0x, g1x, g2x);
        subexpr(y0, y1, y2, f1y, f2y, f3y, g0y, g1y, g2y);
        subexpr(z0, z1, z2, f1z, f2z, f3z, g0z, g1z, g2z);
        intg[0] += d0 * f1x;
        intg[1] += d0 * f2x;
        intg[2] += d1 * f2y;
        intg[3] += d2 * f2z;
        intg[4] += d0 * f3x;
        intg[5] += d1 * f3y;
        intg[6] += d2 * f3z;
        intg[7] += d0 * (y0 * g0x + y1 * g1x + y2 * g2x);
        intg[8] += d1 * (z0 * g0y + z1 * g1y + z2 * g2y);
        intg[9] += d2 * (x0 * g0z + x1 * g1z + x2 * g2z);
    }
    for (int i = 0; i < 10; ++i) {
        intg[i] *= mult[i];
    }
    const Scalar mass = intg[0];
    if (!(mass > 1e-30)) {
        return;
    }
    out_volume = mass;
    out_cm = Point3 {intg[1] / mass, intg[2] / mass, intg[3] / mass};
    const Scalar cmx = out_cm.x;
    const Scalar cmy = out_cm.y;
    const Scalar cmz = out_cm.z;
    const Scalar ixx = intg[5] + intg[6] - mass * (cmy * cmy + cmz * cmz);
    const Scalar iyy = intg[4] + intg[6] - mass * (cmz * cmz + cmx * cmx);
    const Scalar izz = intg[4] + intg[5] - mass * (cmx * cmx + cmy * cmy);
    const Scalar ixy = -(intg[7] - mass * cmx * cmy);
    const Scalar iyz = -(intg[8] - mass * cmy * cmz);
    const Scalar ixz = -(intg[9] - mass * cmx * cmz);
    out_inertia = {ixx, ixy, ixz, ixy, iyy, iyz, ixz, iyz, izz};
}

// Validate a simple polygon and clip ears in a local plane frame. The output
// retains input winding and vertex indices; no kernel objects are allocated.
inline bool triangulate_extrude_profile(std::span<const Point3> poly, const Vec3& normal,
                                        std::vector<std::array<int, 3>>& triangles) {
    triangles.clear();
    if (poly.size() < 3 || poly.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() / 4)) {
        return false;
    }
    Vec3 u {}, v {};
    orthonormal_frame_from_axis(normal, u, v);
    std::vector<std::array<Scalar, 2>> points;
    Scalar extent = 0.0;
    for (const auto& p : poly) {
        const auto offset = subtract(p, poly.front());
        const Scalar x = dot(offset, u), y = dot(offset, v);
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        points.push_back({x, y});
        extent = std::max({extent, std::abs(x), std::abs(y)});
    }
    const Scalar length_tol = std::max(Scalar(1e-12), 64 * std::numeric_limits<Scalar>::epsilon() * extent);
    const Scalar area_tol = std::max(Scalar(1e-14), length_tol * extent);
    const auto turn = [&](int a, int b, int c) {
        const auto& p = points[static_cast<std::size_t>(a)];
        const auto& q = points[static_cast<std::size_t>(b)];
        const auto& r = points[static_cast<std::size_t>(c)];
        return (static_cast<long double>(q[0]) - p[0]) * (static_cast<long double>(r[1]) - p[1]) -
               (static_cast<long double>(q[1]) - p[1]) * (static_cast<long double>(r[0]) - p[0]);
    };
    const int n = static_cast<int>(poly.size());
    for (int i = 0; i < n; ++i) {
        if (std::abs(turn(i, (i + 1) % n, (i + 2) % n)) <= area_tol) return false;
        for (int j = i + 1; j < n; ++j) {
            if (std::hypot(points[i][0] - points[j][0], points[i][1] - points[j][1]) <= length_tol) {
                return false;
            }
        }
    }
    const auto on_segment = [&](int a, int b, int p) {
        return std::abs(turn(a, b, p)) <= area_tol &&
            points[p][0] >= std::min(points[a][0], points[b][0]) - length_tol &&
            points[p][0] <= std::max(points[a][0], points[b][0]) + length_tol &&
            points[p][1] >= std::min(points[a][1], points[b][1]) - length_tol &&
            points[p][1] <= std::max(points[a][1], points[b][1]) + length_tol;
    };
    for (int a = 0; a < n; ++a) {
        const int b = (a + 1) % n;
        for (int c = a + 1; c < n; ++c) {
            const int d = (c + 1) % n;
            if (b == c || d == a) continue;
            const auto ab_c = turn(a, b, c), ab_d = turn(a, b, d);
            const auto cd_a = turn(c, d, a), cd_b = turn(c, d, b);
            const bool crosses_ab = (ab_c > area_tol && ab_d < -area_tol) ||
                                    (ab_c < -area_tol && ab_d > area_tol);
            const bool crosses_cd = (cd_a > area_tol && cd_b < -area_tol) ||
                                    (cd_a < -area_tol && cd_b > area_tol);
            if ((crosses_ab && crosses_cd) || on_segment(a, b, c) || on_segment(a, b, d) ||
                on_segment(c, d, a) || on_segment(c, d, b)) return false;
        }
    }
    std::vector<int> ring;
    for (int i = 0; i < n; ++i) ring.push_back(i);
    while (ring.size() > 3) {
        bool clipped = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const int a = ring[(i + ring.size() - 1) % ring.size()];
            const int b = ring[i], c = ring[(i + 1) % ring.size()];
            if (turn(a, b, c) <= area_tol) continue;
            bool contains_vertex = false;
            for (const int p : ring) {
                if (p == a || p == b || p == c) continue;
                // Boundary points also block an ear, so a diagonal cannot skip a vertex.
                if (turn(a, b, p) >= -area_tol && turn(b, c, p) >= -area_tol &&
                    turn(c, a, p) >= -area_tol) {
                    contains_vertex = true;
                    break;
                }
            }
            if (contains_vertex) continue;
            triangles.push_back({a, b, c});
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) return false;
    }
    if (turn(ring[0], ring[1], ring[2]) <= area_tol) return false;
    triangles.push_back({ring[0], ring[1], ring[2]});
    return true;
}

// Triangulate a polygonal region without inserting geometric vertices. Boundary
// constraints are extended to a maximal noncrossing planar graph, then its bounded
// material faces are walked. This avoids duplicated bridge vertices at hole seams.
// All validation and triangulation finish before allocating kernel objects.
inline bool triangulate_extrude_region(const std::vector<Point3>& outer,
                                       const std::vector<std::vector<Point3>>& holes,
                                       const Vec3& normal, Scalar plane_tol,
                                       std::vector<Point3>& points,
                                       std::vector<std::pair<int, int>>& boundary,
                                       std::vector<std::array<int, 3>>& triangles) {
    points.clear();
    boundary.clear();
    triangles.clear();
    std::vector<std::vector<int>> rings;
    for (std::size_t r = 0; r <= holes.size(); ++r) {
        auto polygon = r == 0 ? outer : holes[r - 1];
        if (polygon.size() < 3 || polygon.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() / 4) - points.size()) {
            return false;
        }
        for (const auto& p : polygon) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
                std::abs(dot(subtract(p, outer.front()), normal)) > plane_tol) return false;
        }
        const auto raw = newell_normal_unnormalized_poly(polygon);
        const auto length = norm(raw);
        std::vector<std::array<int, 3>> unused;
        if (!std::isfinite(length) || length <= 1e-14 ||
            !triangulate_extrude_profile(polygon, scale(raw, 1.0 / length), unused)) return false;
        // Outer CCW, holes CW in the common frame: material lies to the left.
        if ((dot(raw, normal) > 0) != (r == 0)) std::reverse(polygon.begin(), polygon.end());
        std::vector<int> ring;
        for (const auto& p : polygon) {
            ring.push_back(static_cast<int>(points.size()));
            points.push_back(p);
        }
        for (std::size_t i = 0; i < ring.size(); ++i) boundary.emplace_back(ring[i], ring[(i + 1) % ring.size()]);
        rings.push_back(std::move(ring));
    }
    Vec3 u {}, v {};
    orthonormal_frame_from_axis(normal, u, v);
    std::vector<std::array<Scalar, 2>> xy;
    Scalar extent = 0;
    for (const auto& p : points) {
        const auto d = subtract(p, outer.front());
        const Scalar x = dot(d, u), y = dot(d, v);
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        xy.push_back({x, y});
        extent = std::max({extent, std::abs(x), std::abs(y)});
    }
    const Scalar length_tol = std::max(Scalar(1e-12), 64 * std::numeric_limits<Scalar>::epsilon() * extent);
    const Scalar area_tol = std::max(Scalar(1e-14), length_tol * extent);
    const auto turn = [&](int a, int b, int c) {
        return (static_cast<long double>(xy[b][0]) - xy[a][0]) * (static_cast<long double>(xy[c][1]) - xy[a][1]) -
               (static_cast<long double>(xy[b][1]) - xy[a][1]) * (static_cast<long double>(xy[c][0]) - xy[a][0]);
    };
    const auto on_segment = [&](int a, int b, int p) {
        return std::abs(turn(a, b, p)) <= area_tol &&
            xy[p][0] >= std::min(xy[a][0], xy[b][0]) - length_tol &&
            xy[p][0] <= std::max(xy[a][0], xy[b][0]) + length_tol &&
            xy[p][1] >= std::min(xy[a][1], xy[b][1]) - length_tol &&
            xy[p][1] <= std::max(xy[a][1], xy[b][1]) + length_tol;
    };
    const auto conflict = [&](int a, int b, int c, int d) {
        if ((c != a && c != b && on_segment(a, b, c)) ||
            (d != a && d != b && on_segment(a, b, d)) ||
            (a != c && a != d && on_segment(c, d, a)) ||
            (b != c && b != d && on_segment(c, d, b))) return true;
        const auto t1 = turn(a, b, c), t2 = turn(a, b, d);
        const auto t3 = turn(c, d, a), t4 = turn(c, d, b);
        return ((t1 > area_tol && t2 < -area_tol) || (t1 < -area_tol && t2 > area_tol)) &&
               ((t3 > area_tol && t4 < -area_tol) || (t3 < -area_tol && t4 > area_tol));
    };
    for (std::size_t i = 0; i < xy.size(); ++i) {
        for (std::size_t j = i + 1; j < xy.size(); ++j) {
            if (std::hypot(xy[i][0] - xy[j][0], xy[i][1] - xy[j][1]) <= length_tol) return false;
        }
    }
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        for (std::size_t j = i + 1; j < boundary.size(); ++j) {
            if (conflict(boundary[i].first, boundary[i].second, boundary[j].first, boundary[j].second)) return false;
        }
    }
    const auto inside_ring = [&](const std::array<Scalar, 2>& p, const std::vector<int>& ring) {
        bool inside = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const auto& a = xy[ring[i]];
            const auto& b = xy[ring[(i + 1) % ring.size()]];
            if ((a[1] > p[1]) != (b[1] > p[1]) &&
                p[0] < (static_cast<long double>(b[0]) - a[0]) * (p[1] - a[1]) / (b[1] - a[1]) + a[0]) {
                inside = !inside;
            }
        }
        return inside;
    };
    for (std::size_t i = 1; i < rings.size(); ++i) {
        if (!inside_ring(xy[rings[i].front()], rings.front())) return false;
        for (std::size_t j = 1; j < rings.size(); ++j) {
            if (i != j && inside_ring(xy[rings[i].front()], rings[j])) return false;
        }
    }
    const auto inside_region = [&](const std::array<Scalar, 2>& p) {
        if (!inside_ring(p, rings.front())) return false;
        for (std::size_t i = 1; i < rings.size(); ++i) if (inside_ring(p, rings[i])) return false;
        return true;
    };
    auto edges = boundary;
    const int n = static_cast<int>(points.size());
    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) {
            bool blocked = false;
            for (const auto& [c, d] : edges) {
                if ((a == c && b == d) || (a == d && b == c) || conflict(a, b, c, d)) {
                    blocked = true;
                    break;
                }
            }
            if (!blocked && inside_region({(xy[a][0] + xy[b][0]) / 2, (xy[a][1] + xy[b][1]) / 2})) {
                edges.emplace_back(a, b);
            }
        }
    }
    std::vector<std::vector<int>> neighbors(points.size());
    for (const auto& [a, b] : edges) {
        neighbors[a].push_back(b);
        neighbors[b].push_back(a);
    }
    for (int a = 0; a < n; ++a) {
        std::sort(neighbors[a].begin(), neighbors[a].end(), [&](int b, int c) {
            return std::atan2(xy[b][1] - xy[a][1], xy[b][0] - xy[a][0]) <
                   std::atan2(xy[c][1] - xy[a][1], xy[c][0] - xy[a][0]);
        });
    }
    std::map<std::pair<int, int>, bool> visited;
    long double cap_area = 0;
    for (const auto& edge : edges) {
        for (const bool reverse : {false, true}) {
            const auto start = reverse ? std::make_pair(edge.second, edge.first) : edge;
            if (visited[start]) continue;
            auto current = start;
            std::vector<int> face;
            do {
                if (visited[current] || face.size() > 2 * edges.size()) return false;
                visited[current] = true;
                face.push_back(current.first);
                const auto& next = neighbors[current.second];
                const auto it = std::find(next.begin(), next.end(), current.first);
                const auto offset = static_cast<std::size_t>(it - next.begin());
                current = {current.second, next[(offset + next.size() - 1) % next.size()]};
            } while (current != start);
            if (face.size() != 3 || turn(face[0], face[1], face[2]) <= area_tol) continue;
            const int a = face[0], b = face[1], c = face[2];
            if (!inside_region({(xy[a][0] + xy[b][0] + xy[c][0]) / 3,
                                (xy[a][1] + xy[b][1] + xy[c][1]) / 3})) continue;
            triangles.push_back({a, b, c});
            cap_area += turn(a, b, c) / 2;
        }
    }
    long double expected_area = 0;
    for (const auto& [a, b] : boundary) {
        expected_area += (static_cast<long double>(xy[a][0]) * xy[b][1] -
                          static_cast<long double>(xy[a][1]) * xy[b][0]) / 2;
    }
    return triangles.size() == points.size() + 2 * holes.size() - 2 && expected_area > area_tol &&
           std::abs(cap_area - expected_area) <= area_tol * points.size();
}

/// 简单平面多边形（可凹、可带孔）直线/至平面拉伸、等比变截面拉伸或折线平移扫掠：三角端盖 + 平面侧壁。
/// 所有可失败检查在对象分配前完成，禁止失败时留下部分拓扑。
inline bool try_materialize_sweep_extrude_prism_body(KernelState& state, BodyRecord& record) {
    if (record.kind != BodyKind::Sweep || record.rep_kind != RepKind::ExactBRep || !record.bbox.is_valid ||
        !record.shells.empty()) {
        return false;
    }
    if (record.sweep_station_offsets.empty() &&
        (record.label.size() < 8 || record.label.compare(0, 8, "extrude:") != 0)) {
        return false;
    }
    const auto& poly_in = record.extrude_profile_xyz;
    if (poly_in.size() < 3 || poly_in.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() / 4)) {
        return false;
    }
    const Scalar h = record.b;
    if (!std::isfinite(h) || !(h > 0.0)) {
        return false;
    }
    const Vec3 D = normalize(record.axis);
    if (norm(D) <= 1e-14) {
        return false;
    }
    const auto n_raw = newell_normal_unnormalized_poly(std::span<const Point3>(poly_in.data(), poly_in.size()));
    const auto n_len = norm(n_raw);
    if (!std::isfinite(n_len) || n_len <= 1e-14) {
        return false;
    }
    const auto n_unit = scale(n_raw, 1.0 / n_len);
    const Scalar plane_tol = std::max(Scalar(1e-7), state.config.tolerance.linear * Scalar(100.0));
    const Point3& p0r = poly_in[0];
    for (const auto& pt : poly_in) {
        if (!std::isfinite(pt.x) || !std::isfinite(pt.y) || !std::isfinite(pt.z)) return false;
        const Scalar dd = std::abs(dot(n_unit, subtract(pt, p0r)));
        if (!std::isfinite(dd) || dd > plane_tol) {
            return false;
        }
    }
    const Scalar align = std::abs(dot(D, n_unit));
    if (align < Scalar(1e-6)) {
        return false;
    }

    std::vector<Point3> base;
    std::vector<std::pair<int, int>> boundary;
    std::vector<std::array<int, 3>> caps;
    if (record.extrude_holes_xyz.empty()) {
        base = poly_in;
        if (!triangulate_extrude_profile(base, n_unit, caps)) return false;
        for (int i = 0; i < static_cast<int>(base.size()); ++i) {
            boundary.emplace_back(i, (i + 1) % static_cast<int>(base.size()));
        }
    } else if (!triangulate_extrude_region(poly_in, record.extrude_holes_xyz, n_unit, plane_tol,
                                          base, boundary, caps)) {
        return false;
    }
    // Adjacent stations share one ring. Strict monotonicity separates segment
    // interiors into disjoint slabs, so no self-intersection SAT is needed here.
    auto offsets = record.sweep_station_offsets;
    if (offsets.empty()) offsets = {{0, 0, 0}, scale(D, h)};
    if (offsets.size() < 2 || norm(offsets.front()) != 0.0 ||
        base.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) / offsets.size()) return false;
    const Scalar sign = dot(n_unit, D) < 0.0 ? -1.0 : 1.0;
    for (std::size_t k = 1; k < offsets.size(); ++k) {
        const Vec3 step {offsets[k].x - offsets[k-1].x, offsets[k].y - offsets[k-1].y,
                         offsets[k].z - offsets[k-1].z};
        const Scalar length = std::hypot(step.x, step.y, step.z);
        const Scalar advance = sign * dot(n_unit, step);
        if (!std::isfinite(length) || !std::isfinite(advance) || length <= 0.0 ||
            (!record.sweep_station_offsets.empty() && length <= 1e-14) ||
            advance <= 0.0 || advance / length < 1e-6) return false;
    }
    const int n = static_cast<int>(base.size());
    const int last = static_cast<int>((offsets.size() - 1) * base.size());
    const Scalar end_scale = record.extrude_end_scale;
    const bool apex = end_scale == 0.0;
    if (!std::isfinite(end_scale) || end_scale < 0.0 ||
        (apex && !record.extrude_holes_xyz.empty()) ||
        (end_scale != 1.0 && !record.sweep_station_offsets.empty())) return false;
    const bool to_plane = record.extrude_end_plane.has_value();
    Vec3 end_normal = n_unit;
    Scalar plane_direction = 1.0;
    if (to_plane) {
        if (end_scale != 1.0 || !record.sweep_station_offsets.empty()) return false;
        end_normal = record.extrude_end_plane->normal;
        plane_direction = dot(end_normal, D);
        if (!std::isfinite(plane_direction) || std::abs(plane_direction) < 1e-6) return false;
        if (plane_direction * sign < 0.0) end_normal = scale(end_normal, -1.0);
    }
    const auto end_point = [&](const Point3& p) {
        if (to_plane) {
            const auto& plane = *record.extrude_end_plane;
            return add_point_vec(p, scale(D, dot(plane.normal, subtract(plane.origin, p)) / plane_direction));
        }
        return add_point_vec(add_point_vec(record.extrude_scale_center,
            scale(subtract(p, record.extrude_scale_center), end_scale)), offsets.back());
    };
    std::vector<Point3> pos;
    pos.reserve(base.size() * offsets.size());
    for (std::size_t k = 0; k < offsets.size(); ++k) {
        if (apex && k != 0) {
            // A cone over a simple polygon has one shared apex. Collapsing an
            // entire end ring would create zero-length edges and nonmanifold uses.
            pos.push_back(add_point_vec(record.extrude_scale_center, offsets[k]));
            continue;
        }
        for (const auto& p : base) {
            if (to_plane && k != 0) {
                const auto& plane = *record.extrude_end_plane;
                const Scalar travel = dot(plane.normal, subtract(plane.origin, p)) / plane_direction;
                // An affine height attains its extrema on boundary vertices, also
                // for concave regions with holes. Strict separation prevents folds.
                if (!std::isfinite(travel) || travel * align <= plane_tol) return false;
                const auto top = end_point(p);
                const Scalar residual = dot(plane.normal, subtract(top, plane.origin));
                if (!std::isfinite(residual) || std::abs(residual) > plane_tol) return false;
                pos.push_back(top);
                continue;
            }
            const auto section_point = k == 0 || end_scale == 1.0 ? p :
                add_point_vec(record.extrude_scale_center, scale(subtract(p, record.extrude_scale_center), end_scale));
            pos.push_back(add_point_vec(section_point, offsets[k]));
        }
    }

    std::vector<std::array<int, 3>> tris;
    tris.reserve(2 * (caps.size() + boundary.size() * (offsets.size() - 1)));
    for (const auto& cap : caps) {
        tris.push_back({cap[0], cap[2], cap[1]});
        if (!apex) tris.push_back({last + cap[0], last + cap[1], last + cap[2]});
    }
    for (std::size_t k = 0; k + 1 < offsets.size(); ++k) {
        const int lo = static_cast<int>(k * base.size()), hi = lo + n;
        for (const auto& [i, j] : boundary) {
            if (apex) {
                tris.push_back({i, j, n});
            } else {
                tris.push_back({lo + i, lo + j, hi + j});
                tris.push_back({lo + i, hi + j, hi + i});
            }
        }
    }
    if (sign < 0.0) {
        for (auto& t : tris) std::swap(t[1], t[2]);
    }

    for (const auto& p : pos) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    }
    if (!record.sweep_station_offsets.empty() || end_scale != 1.0 || to_plane) {
        // Check the actual rounded coordinates too: sections (including a final
        // apex) must retain their order, even for planarity within tolerance.
        Scalar previous_max = -std::numeric_limits<Scalar>::infinity();
        for (std::size_t k = 0; k < offsets.size(); ++k) {
            Scalar low = std::numeric_limits<Scalar>::infinity(), high = -low;
            const std::size_t station_size = apex && k != 0 ? 1 : base.size();
            for (std::size_t i = 0; i < station_size; ++i) {
                const Scalar level = sign * dot(n_unit, subtract(pos[k * base.size() + i], poly_in.front()));
                if (!std::isfinite(level)) return false;
                low = std::min(low, level);
                high = std::max(high, level);
            }
            if (low <= previous_max) return false;
            previous_max = high;
        }
    }
    if ((end_scale != 1.0 && !apex) || to_plane) {
        // In exact arithmetic a positive homothety preserves the entire planar
        // region. Recheck rounded end coordinates to reject collapsed gaps/edges
        // at extreme scales or world offsets, before allocating model objects.
        const auto end_ring = [&](const std::vector<Point3>& ring) {
            std::vector<Point3> result;
            for (const auto& p : ring) result.push_back(end_point(p));
            return result;
        };
        auto outer = end_ring(poly_in);
        std::vector<std::array<int, 3>> end_caps;
        if (record.extrude_holes_xyz.empty()) {
            if (!triangulate_extrude_profile(outer, end_normal, end_caps)) return false;
        } else {
            std::vector<std::vector<Point3>> holes;
            for (const auto& ring : record.extrude_holes_xyz) holes.push_back(end_ring(ring));
            std::vector<Point3> end_base;
            std::vector<std::pair<int, int>> end_boundary;
            if (!triangulate_extrude_region(outer, holes, end_normal, plane_tol, end_base, end_boundary, end_caps)) return false;
        }
        // The start triangulation is reused at the end: every cap triangle must
        // retain its orientation, even if rounding changes an admissible diagonal.
        for (const auto& cap : caps) {
            if (dot(end_normal, cross(subtract(pos[last + cap[1]], pos[last + cap[0]]),
                                  subtract(pos[last + cap[2]], pos[last + cap[0]]))) <= 1e-14) return false;
        }
    }
    for (const auto& t : tris) {
        const auto area = norm(cross(subtract(pos[t[1]], pos[t[0]]), subtract(pos[t[2]], pos[t[0]])));
        if (!std::isfinite(area) || area <= 1e-14) return false;
    }

    Scalar vol_chk = 0.0;
    Point3 cm_tmp {};
    std::array<Scalar, 9> in_tmp {};
    Scalar area_tmp = 0.0;
    // Integrate near the profile origin to avoid cancellation for translated profiles.
    std::vector<Point3> local_pos;
    local_pos.reserve(pos.size());
    for (const auto& p : pos) {
        const auto offset = subtract(p, poly_in.front());
        local_pos.push_back({offset.x, offset.y, offset.z});
    }
    polyhedral_mass_properties_from_triangles(local_pos, tris, vol_chk, cm_tmp, in_tmp, area_tmp);
    cm_tmp = {cm_tmp.x + poly_in.front().x, cm_tmp.y + poly_in.front().y, cm_tmp.z + poly_in.front().z};
    if (!(vol_chk > 1e-18) || !std::isfinite(vol_chk) || !std::isfinite(area_tmp) ||
        !std::isfinite(cm_tmp.x) || !std::isfinite(cm_tmp.y) || !std::isfinite(cm_tmp.z) ||
        !std::all_of(in_tmp.begin(), in_tmp.end(), [](Scalar x) { return std::isfinite(x); })) {
        return false;
    }

    // The bounds include every bend, not just the two end sections.
    record.bbox = make_bbox(pos.front(), pos.front());
    for (const auto& p : pos) {
        record.bbox.min.x = std::min(record.bbox.min.x, p.x);
        record.bbox.min.y = std::min(record.bbox.min.y, p.y);
        record.bbox.min.z = std::min(record.bbox.min.z, p.z);
        record.bbox.max.x = std::max(record.bbox.max.x, p.x);
        record.bbox.max.y = std::max(record.bbox.max.y, p.y);
        record.bbox.max.z = std::max(record.bbox.max.z, p.z);
    }
    std::vector<VertexId> vid(pos.size());
    for (std::size_t i = 0; i < pos.size(); ++i) {
        vid[static_cast<std::size_t>(i)] = VertexId {state.allocate_id()};
        state.vertices.emplace(vid[static_cast<std::size_t>(i)].value,
                               VertexRecord {pos[static_cast<std::size_t>(i)]});
    }

    std::vector<EdgeId> edges;
    std::map<std::pair<int, int>, int> edge_to_index;
    auto edge_index_for_pair = [&](int a, int b) -> int {
        const int lo = std::min(a, b);
        const int hi = std::max(a, b);
        const auto key = std::make_pair(lo, hi);
        const auto it = edge_to_index.find(key);
        if (it != edge_to_index.end()) {
            return it->second;
        }
        const auto curve_id =
            create_materialized_line(state, pos[static_cast<std::size_t>(lo)], pos[static_cast<std::size_t>(hi)]);
        const auto eid = EdgeId {state.allocate_id()};
        state.edges.emplace(eid.value,
                            EdgeRecord {curve_id, vid[static_cast<std::size_t>(lo)], vid[static_cast<std::size_t>(hi)]});
        const int idx = static_cast<int>(edges.size());
        edges.push_back(eid);
        edge_to_index.emplace(key, idx);
        return idx;
    };
    auto coedge_ref = [&](int a, int b) -> std::pair<int, bool> {
        const int lo = std::min(a, b);
        const int hi = std::max(a, b);
        const int ei = edge_index_for_pair(lo, hi);
        const bool rev = (a == hi);
        return {ei, rev};
    };
    auto tri_normal = [&](int a, int b, int c) -> Vec3 {
        const auto pa = pos[static_cast<std::size_t>(a)];
        const auto pb = pos[static_cast<std::size_t>(b)];
        const auto pc = pos[static_cast<std::size_t>(c)];
        const auto e1 = subtract(pb, pa);
        const auto e2 = subtract(pc, pa);
        return safe_unit_normal(cross(e1, e2));
    };

    const auto source_faces = std::span<const FaceId>(record.source_faces);
    std::vector<FaceId> faces;
    faces.reserve(tris.size());

    for (const auto& t : tris) {
        const auto [e0, r0] = coedge_ref(t[0], t[1]);
        const auto [e1, r1] = coedge_ref(t[1], t[2]);
        const auto [e2, r2] = coedge_ref(t[2], t[0]);
        const std::array<std::pair<int, bool>, 3> refs {{{e0, r0}, {e1, r1}, {e2, r2}}};
        faces.push_back(create_materialized_polygon_face(state, edges, std::span<const std::pair<int, bool>>(refs),
                                                         tri_normal(t[0], t[1], t[2]), source_faces));
    }

    const auto shell_id = ShellId {state.allocate_id()};
    ShellRecord shell;
    shell.faces = std::move(faces);
    shell.source_shells = record.source_shells;
    if (record.source_faces.empty()) {
        shell.source_faces = shell.faces;
    } else {
        shell.source_faces = record.source_faces;
    }
    state.shells.emplace(shell_id.value, std::move(shell));
    record.shells.push_back(shell_id);

    record.sweep_polyhedral_mass_valid = true;
    record.sweep_polyhedral_volume = vol_chk;
    record.sweep_cached_surface_area = area_tmp;
    record.sweep_polyhedral_centroid = cm_tmp;
    record.sweep_inertia_about_centroid = in_tmp;
    record.a = vol_chk;
    record.extrude_poly_cap_area = vol_chk / (align * h);
    if (end_scale != 1.0 || to_plane) {
        record.extrude_poly_cap_area = 0.0;
        for (const auto& cap : caps) record.extrude_poly_cap_area += 0.5 * norm(
            cross(subtract(pos[cap[1]], pos[cap[0]]), subtract(pos[cap[2]], pos[cap[0]])));
    }
    record.extrude_lateral_area = area_tmp - (1 + end_scale * end_scale) * record.extrude_poly_cap_area;
    if (to_plane) {
        Scalar end_area = 0.0;
        for (const auto& cap : caps) end_area += 0.5 * norm(cross(
            subtract(pos[last + cap[1]], pos[last + cap[0]]),
            subtract(pos[last + cap[2]], pos[last + cap[0]])));
        record.extrude_lateral_area = area_tmp - record.extrude_poly_cap_area - end_area;
    }
    record.extrude_mass_centroid = cm_tmp;
    return true;
}

/// 多个兼容平面多边形截面的直纹放样。外环和同序孔环的顶点逐一对应，截面间的
/// 线性插值仍须形成简单平面区域。完整三角闭壳、质量属性和包围盒都在分配 ID 前验证。
inline bool try_materialize_sweep_loft_body(KernelState& state, BodyRecord& record) {
    if (record.kind != BodyKind::Sweep || record.rep_kind != RepKind::ExactBRep || !record.bbox.is_valid ||
        !record.shells.empty() || record.label != "loft:polygon" || record.loft_profiles_xyz.size() < 2 ||
        record.loft_profiles_xyz.size() != record.loft_holes_xyz.size()) return false;
    const std::size_t stations = record.loft_profiles_xyz.size();
    const std::size_t hole_count = record.loft_holes_xyz.front().size();
    const std::size_t outer_count = record.loft_profiles_xyz.front().size();
    if (outer_count < 3 || outer_count > static_cast<std::size_t>(std::numeric_limits<int>::max() / 64)) return false;
    std::vector<std::size_t> ring_counts {outer_count};
    for (const auto& hole : record.loft_holes_xyz.front()) {
        if (hole.size() < 3) return false;
        ring_counts.push_back(hole.size());
    }
    std::size_t section_size = 0;
    for (const auto count : ring_counts) {
        if (count > static_cast<std::size_t>(std::numeric_limits<int>::max()) - section_size) return false;
        section_size += count;
    }
    if (section_size > static_cast<std::size_t>(std::numeric_limits<int>::max()) / stations) return false;

    const auto ring_center = [](std::span<const Point3> ring) {
        Point3 center {};
        for (const auto& p : ring) {
            center.x += p.x;
            center.y += p.y;
            center.z += p.z;
        }
        const Scalar inverse = 1.0 / static_cast<Scalar>(ring.size());
        return Point3 {center.x * inverse, center.y * inverse, center.z * inverse};
    };
    const Point3 first_center = ring_center(record.loft_profiles_xyz.front());
    const Point3 last_center = ring_center(record.loft_profiles_xyz.back());
    const Vec3 center_span = subtract(last_center, first_center);
    const Scalar center_span_length = norm(center_span);
    if (!std::isfinite(center_span_length) || center_span_length <= 1e-14) return false;
    const Vec3 first_raw_normal = newell_normal_unnormalized_poly(record.loft_profiles_xyz.front());
    const Scalar first_normal_length = norm(first_raw_normal);
    if (!std::isfinite(first_normal_length) || first_normal_length <= 1e-14) return false;
    Vec3 loft_axis = scale(first_raw_normal, 1.0 / first_normal_length);
    if (dot(loft_axis, center_span) < 0.0) loft_axis = scale(loft_axis, -1.0);
    const Scalar transverse_span = dot(center_span, loft_axis);
    if (!std::isfinite(transverse_span) || transverse_span <= 1e-14) return false;
    const Scalar plane_tol = std::max(Scalar(1e-7), state.config.tolerance.linear * Scalar(100.0));

    std::vector<std::vector<Point3>> sections;
    std::vector<std::vector<std::pair<int, int>>> section_boundaries;
    std::vector<std::vector<std::array<int, 3>>> section_caps;
    std::vector<Vec3> section_normals;
    sections.reserve(stations);
    section_boundaries.reserve(stations);
    section_caps.reserve(stations);
    section_normals.reserve(stations);
    Scalar previous_high = -std::numeric_limits<Scalar>::infinity();
    for (std::size_t station = 0; station < stations; ++station) {
        const auto& outer = record.loft_profiles_xyz[station];
        const auto& holes = record.loft_holes_xyz[station];
        if (outer.size() != outer_count || holes.size() != hole_count) return false;
        for (std::size_t hole = 0; hole < hole_count; ++hole) {
            if (holes[hole].size() != ring_counts[hole + 1]) return false;
        }
        const Vec3 raw_normal = newell_normal_unnormalized_poly(outer);
        const Scalar normal_length = norm(raw_normal);
        if (!std::isfinite(normal_length) || normal_length <= 1e-14) return false;
        Vec3 normal = scale(raw_normal, 1.0 / normal_length);
        Scalar alignment = dot(normal, loft_axis);
        if (!std::isfinite(alignment) || std::abs(alignment) < 0.25) return false;
        if (alignment < 0.0) normal = scale(normal, -1.0);
        std::vector<Point3> points;
        std::vector<std::pair<int, int>> boundary;
        std::vector<std::array<int, 3>> caps;
        if (!triangulate_extrude_region(outer, holes, normal, plane_tol, points, boundary, caps) ||
            points.size() != section_size || boundary.size() != section_size || caps.empty()) return false;
        if (!section_boundaries.empty() && boundary != section_boundaries.front()) return false;
        Scalar low = std::numeric_limits<Scalar>::infinity();
        Scalar high = -low;
        for (const auto& p : points) {
            const Scalar level = dot(subtract(p, first_center), loft_axis);
            if (!std::isfinite(level)) return false;
            low = std::min(low, level);
            high = std::max(high, level);
        }
        // Order sections along the first section's transverse normal, not the
        // center-to-center vector: a valid loft may translate or shear laterally.
        // Nonoverlapping slabs still conservatively reject reversed/crossing stations.
        if (station != 0 && low <= previous_high + plane_tol) return false;
        previous_high = high;
        sections.push_back(std::move(points));
        section_boundaries.push_back(std::move(boundary));
        section_caps.push_back(std::move(caps));
        section_normals.push_back(normal);
    }

    // Check three interior sections of every ruled interval. This admits coherent
    // translation/scale/rotation and concave or holed morphs while rejecting the
    // common correspondence twists that only look valid at their endpoints.
    for (std::size_t station = 0; station + 1 < stations; ++station) {
        for (const Scalar t : {Scalar(0.25), Scalar(0.5), Scalar(0.75)}) {
            std::vector<std::vector<Point3>> rings(ring_counts.size());
            std::size_t offset = 0;
            for (std::size_t ring = 0; ring < ring_counts.size(); ++ring) {
                rings[ring].reserve(ring_counts[ring]);
                for (std::size_t i = 0; i < ring_counts[ring]; ++i) {
                    const auto& a = sections[station][offset + i];
                    const auto& b = sections[station + 1][offset + i];
                    rings[ring].push_back({a.x + t * (b.x - a.x),
                                           a.y + t * (b.y - a.y),
                                           a.z + t * (b.z - a.z)});
                }
                offset += ring_counts[ring];
            }
            const Vec3 blended_normal {
                (1.0 - t) * section_normals[station].x + t * section_normals[station + 1].x,
                (1.0 - t) * section_normals[station].y + t * section_normals[station + 1].y,
                (1.0 - t) * section_normals[station].z + t * section_normals[station + 1].z};
            if (norm(blended_normal) <= 1e-14) return false;
            std::vector<std::vector<Point3>> holes(rings.begin() + 1, rings.end());
            std::vector<Point3> points;
            std::vector<std::pair<int, int>> boundary;
            std::vector<std::array<int, 3>> caps;
            if (!triangulate_extrude_region(rings.front(), holes, normalize(blended_normal), plane_tol,
                                            points, boundary, caps) ||
                points.size() != section_size || boundary != section_boundaries.front()) return false;
        }
    }

    std::vector<Point3> positions;
    positions.reserve(section_size * stations);
    for (const auto& section : sections) positions.insert(positions.end(), section.begin(), section.end());
    const int last = static_cast<int>((stations - 1) * section_size);
    std::vector<std::array<int, 3>> triangles;
    triangles.reserve(section_caps.front().size() + section_caps.back().size() +
                      2 * section_size * (stations - 1));
    for (const auto& cap : section_caps.front()) triangles.push_back({cap[0], cap[2], cap[1]});
    for (const auto& cap : section_caps.back()) triangles.push_back({last + cap[0], last + cap[1], last + cap[2]});
    for (std::size_t station = 0; station + 1 < stations; ++station) {
        const int lo = static_cast<int>(station * section_size);
        const int hi = static_cast<int>((station + 1) * section_size);
        for (const auto& [i, j] : section_boundaries.front()) {
            triangles.push_back({lo + i, lo + j, hi + j});
            triangles.push_back({lo + i, hi + j, hi + i});
        }
    }
    Scalar coordinate_scale = 1.0;
    for (const auto& p : positions) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
        coordinate_scale = std::max({coordinate_scale, std::abs(p.x), std::abs(p.y), std::abs(p.z)});
    }
    const Scalar triangle_tol = std::max(Scalar(1e-20), Scalar(256) *
        std::numeric_limits<Scalar>::epsilon() * coordinate_scale * coordinate_scale);
    std::map<std::pair<int, int>, int> edge_use_count;
    for (const auto& triangle : triangles) {
        const Scalar area2 = norm(cross(subtract(positions[triangle[1]], positions[triangle[0]]),
                                        subtract(positions[triangle[2]], positions[triangle[0]])));
        if (!std::isfinite(area2) || area2 <= triangle_tol) return false;
        for (int i = 0; i < 3; ++i) {
            const int a = triangle[static_cast<std::size_t>(i)];
            const int b = triangle[static_cast<std::size_t>((i + 1) % 3)];
            ++edge_use_count[{std::min(a, b), std::max(a, b)}];
        }
    }
    if (triangles.empty() || edge_use_count.empty() ||
        std::any_of(edge_use_count.begin(), edge_use_count.end(),
                    [](const auto& item) { return item.second != 2; })) return false;

    std::vector<Point3> local_positions;
    local_positions.reserve(positions.size());
    for (const auto& p : positions) {
        const Vec3 local = subtract(p, first_center);
        local_positions.push_back({local.x, local.y, local.z});
    }
    Scalar volume = 0.0, surface_area = 0.0;
    Point3 centroid {};
    std::array<Scalar, 9> inertia {};
    polyhedral_mass_properties_from_triangles(local_positions, triangles, volume, centroid, inertia, surface_area);
    if (!(volume > 1e-18)) {
        for (auto& triangle : triangles) std::swap(triangle[1], triangle[2]);
        polyhedral_mass_properties_from_triangles(local_positions, triangles, volume, centroid, inertia, surface_area);
    }
    centroid = add_point_vec(first_center, {centroid.x, centroid.y, centroid.z});
    if (!(volume > 1e-18) || !std::isfinite(volume) || !(surface_area > 0.0) ||
        !std::isfinite(surface_area) || !std::isfinite(centroid.x) || !std::isfinite(centroid.y) ||
        !std::isfinite(centroid.z) ||
        !std::all_of(inertia.begin(), inertia.end(), [](Scalar value) { return std::isfinite(value); })) return false;
    const Scalar inertia_scale = std::max({Scalar(1.0), std::abs(inertia[0]), std::abs(inertia[4]), std::abs(inertia[8])});
    const Scalar inertia_tol = Scalar(1024) * std::numeric_limits<Scalar>::epsilon() * inertia_scale;
    if (inertia[0] < -inertia_tol || inertia[4] < -inertia_tol || inertia[8] < -inertia_tol ||
        std::abs(inertia[1] - inertia[3]) > inertia_tol ||
        std::abs(inertia[2] - inertia[6]) > inertia_tol ||
        std::abs(inertia[5] - inertia[7]) > inertia_tol) return false;

    BoundingBox bbox {};
    for (const auto& p : positions) extend_materialization_bbox(bbox, p);
    if (!bbox.is_valid) return false;
    const Scalar bbox_tol = Scalar(256) * std::numeric_limits<Scalar>::epsilon() * coordinate_scale;
    if (centroid.x < bbox.min.x - bbox_tol || centroid.x > bbox.max.x + bbox_tol ||
        centroid.y < bbox.min.y - bbox_tol || centroid.y > bbox.max.y + bbox_tol ||
        centroid.z < bbox.min.z - bbox_tol || centroid.z > bbox.max.z + bbox_tol) return false;

    std::vector<VertexId> vertices(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        vertices[i] = VertexId {state.allocate_id()};
        state.vertices.emplace(vertices[i].value, VertexRecord {positions[i]});
    }
    std::vector<EdgeId> edges;
    std::map<std::pair<int, int>, int> edge_to_index;
    const auto edge_index_for_pair = [&](int a, int b) -> int {
        const auto key = std::make_pair(std::min(a, b), std::max(a, b));
        if (const auto found = edge_to_index.find(key); found != edge_to_index.end()) return found->second;
        const auto curve = create_materialized_line(state, positions[static_cast<std::size_t>(key.first)],
                                                    positions[static_cast<std::size_t>(key.second)]);
        const auto edge = EdgeId {state.allocate_id()};
        state.edges.emplace(edge.value, EdgeRecord {curve, vertices[static_cast<std::size_t>(key.first)],
                                                    vertices[static_cast<std::size_t>(key.second)]});
        const int index = static_cast<int>(edges.size());
        edges.push_back(edge);
        edge_to_index.emplace(key, index);
        return index;
    };
    const auto source_faces = std::span<const FaceId>(record.source_faces);
    std::vector<FaceId> faces;
    faces.reserve(triangles.size());
    for (const auto& triangle : triangles) {
        std::array<std::pair<int, bool>, 3> refs {};
        for (int i = 0; i < 3; ++i) {
            const int a = triangle[static_cast<std::size_t>(i)];
            const int b = triangle[static_cast<std::size_t>((i + 1) % 3)];
            refs[static_cast<std::size_t>(i)] = {edge_index_for_pair(a, b), a > b};
        }
        const Vec3 normal = cross(subtract(positions[triangle[1]], positions[triangle[0]]),
                                  subtract(positions[triangle[2]], positions[triangle[0]]));
        faces.push_back(create_materialized_polygon_face(
            state, edges, std::span<const std::pair<int, bool>>(refs), normal, source_faces));
    }
    const auto shell_id = ShellId {state.allocate_id()};
    ShellRecord shell;
    shell.faces = std::move(faces);
    shell.source_shells = record.source_shells;
    shell.source_faces = record.source_faces.empty() ? shell.faces : record.source_faces;
    state.shells.emplace(shell_id.value, std::move(shell));
    record.shells.push_back(shell_id);
    record.bbox = bbox;
    record.axis = loft_axis;
    record.b = transverse_span;
    record.sweep_polyhedral_mass_valid = true;
    record.sweep_polyhedral_volume = volume;
    record.sweep_cached_surface_area = surface_area;
    record.sweep_polyhedral_centroid = centroid;
    record.sweep_inertia_about_centroid = inertia;
    record.a = volume;
    record.extrude_mass_centroid = centroid;
    record.extrude_poly_cap_area = 0.0;
    for (const auto& cap : section_caps.front()) record.extrude_poly_cap_area += 0.5 * norm(cross(
        subtract(positions[cap[1]], positions[cap[0]]), subtract(positions[cap[2]], positions[cap[0]])));
    Scalar end_area = 0.0;
    for (const auto& cap : section_caps.back()) end_area += 0.5 * norm(cross(
        subtract(positions[last + cap[1]], positions[last + cap[0]]),
        subtract(positions[last + cap[2]], positions[last + cap[0]])));
    record.extrude_lateral_area = surface_area - record.extrude_poly_cap_area - end_area;
    return true;
}

/// 显式平面多边形随采样曲线的旋转最小标架扫掠。开放导轨生成两端盖，整圆/闭合样条导轨周期闭合；
/// 凹截面和孔复用约束区域剖分。先验证完整三角闭壳、质量和舍入坐标，再一次性分配对象。
inline bool try_materialize_sweep_curve_frame_body(KernelState& state, BodyRecord& record) {
    if (record.kind != BodyKind::Sweep || record.rep_kind != RepKind::ExactBRep || !record.bbox.is_valid ||
        !record.shells.empty() || record.sweep_frame_origins.size() < 3 ||
        record.sweep_frame_origins.size() != record.sweep_frame_u.size() ||
        record.sweep_frame_origins.size() != record.sweep_frame_v.size() ||
        record.label.size() < 12 || record.label.compare(0, 12, "sweep_curve:") != 0) return false;
    const auto& outer = record.extrude_profile_xyz;
    if (outer.size() < 3 || outer.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() / 64)) return false;
    const auto raw_normal = newell_normal_unnormalized_poly(std::span<const Point3>(outer.data(), outer.size()));
    const Scalar normal_length = norm(raw_normal);
    if (!std::isfinite(normal_length) || normal_length <= 1e-14) return false;
    const Vec3 profile_normal = scale(raw_normal, 1.0 / normal_length);
    const Scalar plane_tol = std::max(Scalar(1e-7), state.config.tolerance.linear * Scalar(100.0));

    std::vector<Point3> base;
    std::vector<std::pair<int, int>> boundary;
    std::vector<std::array<int, 3>> caps;
    if (record.extrude_holes_xyz.empty()) {
        base = outer;
        if (!triangulate_extrude_profile(base, profile_normal, caps)) return false;
        for (int i = 0; i < static_cast<int>(base.size()); ++i) {
            boundary.emplace_back(i, (i + 1) % static_cast<int>(base.size()));
        }
    } else if (!triangulate_extrude_region(outer, record.extrude_holes_xyz, profile_normal, plane_tol,
                                           base, boundary, caps)) {
        return false;
    }
    const std::size_t stations = record.sweep_frame_origins.size();
    if (base.empty() || base.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) / stations) return false;
    const Vec3 u0 = normalize(record.sweep_frame_u.front());
    const Vec3 v0 = normalize(record.sweep_frame_v.front());
    const Vec3 tangent0 = normalize(cross(u0, v0));
    if (norm(u0) <= 1e-14 || norm(v0) <= 1e-14 || norm(tangent0) <= 1e-14 ||
        std::abs(dot(u0, v0)) > 1e-8 || std::abs(dot(profile_normal, tangent0)) < 1.0 - 1e-6) return false;
    for (const auto& p : base) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
            std::abs(dot(tangent0, subtract(p, record.sweep_frame_origins.front()))) > plane_tol) return false;
    }

    std::vector<Point3> positions;
    positions.reserve(base.size() * stations);
    for (std::size_t station = 0; station < stations; ++station) {
        const auto& origin = record.sweep_frame_origins[station];
        const Vec3 u = normalize(record.sweep_frame_u[station]);
        Vec3 v = normalize(record.sweep_frame_v[station]);
        const Vec3 tangent = normalize(cross(u, v));
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
            norm(u) <= 1e-14 || norm(v) <= 1e-14 || norm(tangent) <= 1e-14 ||
            std::abs(dot(u, v)) > 1e-8) return false;
        // Recompute v after validation so accumulated frame drift cannot change
        // section scale or make the two coordinates nonorthogonal.
        v = normalize(cross(tangent, u));
        for (const auto& p : base) {
            const Vec3 offset = subtract(p, record.sweep_frame_origins.front());
            const Scalar x = dot(offset, u0), y = dot(offset, v0);
            const auto transformed = add_point_vec(origin,
                Vec3 {u.x * x + v.x * y, u.y * x + v.y * y, u.z * x + v.z * y});
            if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y) ||
                !std::isfinite(transformed.z)) return false;
            positions.push_back(transformed);
        }
    }
    // Independently recheck that the serialized frames describe a forward,
    // nonfolding tube. In particular, every corresponding section vertex must
    // cross the bisector plane in the rail direction; a positive mass alone is
    // insufficient because two local wall folds may cancel in the integral.
    const std::size_t segment_count = record.sweep_frame_closed ? stations : stations - 1;
    for (std::size_t station = 0; station < segment_count; ++station) {
        const std::size_t next = (station + 1) % stations;
        const Vec3 tangent_a = normalize(cross(record.sweep_frame_u[station], record.sweep_frame_v[station]));
        const Vec3 tangent_b = normalize(cross(record.sweep_frame_u[next], record.sweep_frame_v[next]));
        const Vec3 tangent_sum {tangent_a.x + tangent_b.x, tangent_a.y + tangent_b.y,
                                tangent_a.z + tangent_b.z};
        if (norm(tangent_sum) <= 1e-14 || dot(tangent_a, tangent_b) <= 0.5) return false;
        const Vec3 bisector = normalize(tangent_sum);
        const Vec3 origin_step = subtract(record.sweep_frame_origins[next], record.sweep_frame_origins[station]);
        const Scalar origin_advance = dot(origin_step, bisector);
        if (!std::isfinite(origin_advance) || origin_advance <= plane_tol ||
            origin_advance / norm(origin_step) <= 0.5) return false;
        for (std::size_t i = 0; i < base.size(); ++i) {
            const Vec3 wall_step = subtract(positions[next * base.size() + i],
                                            positions[station * base.size() + i]);
            const Scalar wall_advance = dot(wall_step, bisector);
            if (!std::isfinite(wall_advance) || wall_advance <= plane_tol * 0.1) return false;
        }
    }
    if (record.sweep_frame_closed) {
        // A periodic rail must return the transported in-plane axis to itself.
        // Otherwise the last wall contains a hidden twist discontinuity.
        const Vec3 last_tangent = normalize(cross(record.sweep_frame_u.back(), record.sweep_frame_v.back()));
        const Vec3 rotation_axis = cross(last_tangent, tangent0);
        const Scalar sine = norm(rotation_axis);
        const Scalar cosine = std::clamp(dot(last_tangent, tangent0), Scalar(-1.0), Scalar(1.0));
        Vec3 seam_u = normalize(record.sweep_frame_u.back());
        if (sine > 1e-14) {
            const Vec3 axis = scale(rotation_axis, 1.0 / sine);
            const auto first = scale(seam_u, cosine);
            const auto second = scale(cross(axis, seam_u), sine);
            const auto third = scale(axis, dot(axis, seam_u) * (1.0 - cosine));
            seam_u = normalize({first.x + second.x + third.x,
                                first.y + second.y + third.y,
                                first.z + second.z + third.z});
        }
        if (dot(seam_u, u0) < 1.0 - 1e-6) return false;
    }
    const int section_size = static_cast<int>(base.size());
    const int last = static_cast<int>(stations - 1) * section_size;
    const Scalar orientation = dot(profile_normal, tangent0) < 0.0 ? -1.0 : 1.0;
    std::vector<std::array<int, 3>> triangles;
    triangles.reserve((record.sweep_frame_closed ? 0 : 2 * caps.size()) + 2 * boundary.size() * segment_count);
    if (!record.sweep_frame_closed) {
        for (const auto& cap : caps) {
            triangles.push_back({cap[0], cap[2], cap[1]});
            triangles.push_back({last + cap[0], last + cap[1], last + cap[2]});
        }
    }
    for (std::size_t station = 0; station < segment_count; ++station) {
        const int lo = static_cast<int>(station * base.size());
        const int hi = static_cast<int>(((station + 1) % stations) * base.size());
        for (const auto& [i, j] : boundary) {
            triangles.push_back({lo + i, lo + j, hi + j});
            triangles.push_back({lo + i, hi + j, hi + i});
        }
    }
    if (orientation < 0.0) {
        for (auto& triangle : triangles) std::swap(triangle[1], triangle[2]);
    }
    const Scalar coordinate_scale = [&] {
        Scalar result = 1.0;
        for (const auto& p : positions) result = std::max({result, std::abs(p.x), std::abs(p.y), std::abs(p.z)});
        return result;
    }();
    const Scalar triangle_tol = std::max(Scalar(1e-20),
        Scalar(256) * std::numeric_limits<Scalar>::epsilon() * coordinate_scale * coordinate_scale);
    std::map<std::pair<int, int>, int> edge_use_count;
    for (const auto& triangle : triangles) {
        const auto area2 = norm(cross(subtract(positions[triangle[1]], positions[triangle[0]]),
                                      subtract(positions[triangle[2]], positions[triangle[0]])));
        if (!std::isfinite(area2) || area2 <= triangle_tol) return false;
        for (int i = 0; i < 3; ++i) {
            const int a = triangle[static_cast<std::size_t>(i)];
            const int b = triangle[static_cast<std::size_t>((i + 1) % 3)];
            ++edge_use_count[{std::min(a, b), std::max(a, b)}];
        }
    }
    if (triangles.empty() || edge_use_count.empty() ||
        std::any_of(edge_use_count.begin(), edge_use_count.end(),
                    [](const auto& item) { return item.second != 2; })) return false;

    std::vector<Point3> local_positions;
    local_positions.reserve(positions.size());
    for (const auto& p : positions) {
        const auto local = subtract(p, record.sweep_frame_origins.front());
        local_positions.push_back({local.x, local.y, local.z});
    }
    Scalar volume = 0.0, surface_area = 0.0;
    Point3 centroid {};
    std::array<Scalar, 9> inertia {};
    polyhedral_mass_properties_from_triangles(local_positions, triangles, volume, centroid, inertia, surface_area);
    if (!(volume > 1e-18)) {
        for (auto& triangle : triangles) std::swap(triangle[1], triangle[2]);
        polyhedral_mass_properties_from_triangles(local_positions, triangles, volume, centroid, inertia, surface_area);
    }
    centroid = add_point_vec(record.sweep_frame_origins.front(), {centroid.x, centroid.y, centroid.z});
    if (!(volume > 1e-18) || !std::isfinite(volume) || !std::isfinite(surface_area) || !(surface_area > 0.0) ||
        !std::isfinite(centroid.x) || !std::isfinite(centroid.y) || !std::isfinite(centroid.z) ||
        !std::all_of(inertia.begin(), inertia.end(), [](Scalar value) { return std::isfinite(value); })) return false;
    const Scalar inertia_scale = std::max({Scalar(1.0), std::abs(inertia[0]), std::abs(inertia[4]),
                                           std::abs(inertia[8])});
    const Scalar inertia_tol = Scalar(1024) * std::numeric_limits<Scalar>::epsilon() * inertia_scale;
    if (inertia[0] < -inertia_tol || inertia[4] < -inertia_tol || inertia[8] < -inertia_tol ||
        std::abs(inertia[1] - inertia[3]) > inertia_tol ||
        std::abs(inertia[2] - inertia[6]) > inertia_tol ||
        std::abs(inertia[5] - inertia[7]) > inertia_tol) return false;

    BoundingBox bbox {};
    for (const auto& p : positions) extend_materialization_bbox(bbox, p);
    if (!bbox.is_valid) return false;
    const Scalar bbox_tol = Scalar(256) * std::numeric_limits<Scalar>::epsilon() * coordinate_scale;
    if (centroid.x < bbox.min.x - bbox_tol || centroid.x > bbox.max.x + bbox_tol ||
        centroid.y < bbox.min.y - bbox_tol || centroid.y > bbox.max.y + bbox_tol ||
        centroid.z < bbox.min.z - bbox_tol || centroid.z > bbox.max.z + bbox_tol) return false;
    std::vector<VertexId> vertices(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        vertices[i] = VertexId {state.allocate_id()};
        state.vertices.emplace(vertices[i].value, VertexRecord {positions[i]});
    }
    std::vector<EdgeId> edges;
    std::map<std::pair<int, int>, int> edge_to_index;
    const auto edge_index_for_pair = [&](int a, int b) -> int {
        const auto key = std::make_pair(std::min(a, b), std::max(a, b));
        if (const auto found = edge_to_index.find(key); found != edge_to_index.end()) return found->second;
        const auto curve = create_materialized_line(state, positions[static_cast<std::size_t>(key.first)],
                                                    positions[static_cast<std::size_t>(key.second)]);
        const auto edge = EdgeId {state.allocate_id()};
        state.edges.emplace(edge.value, EdgeRecord {curve, vertices[static_cast<std::size_t>(key.first)],
                                                    vertices[static_cast<std::size_t>(key.second)]});
        const int index = static_cast<int>(edges.size());
        edges.push_back(edge);
        edge_to_index.emplace(key, index);
        return index;
    };
    const auto source_faces = std::span<const FaceId>(record.source_faces);
    std::vector<FaceId> faces;
    faces.reserve(triangles.size());
    for (const auto& triangle : triangles) {
        std::array<std::pair<int, bool>, 3> refs {};
        for (int i = 0; i < 3; ++i) {
            const int a = triangle[static_cast<std::size_t>(i)];
            const int b = triangle[static_cast<std::size_t>((i + 1) % 3)];
            refs[static_cast<std::size_t>(i)] = {edge_index_for_pair(a, b), a > b};
        }
        const auto normal = cross(subtract(positions[static_cast<std::size_t>(triangle[1])],
                                           positions[static_cast<std::size_t>(triangle[0])]),
                                  subtract(positions[static_cast<std::size_t>(triangle[2])],
                                           positions[static_cast<std::size_t>(triangle[0])]));
        faces.push_back(create_materialized_polygon_face(
            state, edges, std::span<const std::pair<int, bool>>(refs), normal, source_faces));
    }

    // A closed sweep of a holed section has one disconnected boundary shell
    // for the outer ring and one for every swept hole. Open sweeps remain a
    // single shell because their triangulated end caps connect all rings.
    std::vector<std::size_t> component_parent(triangles.size());
    for (std::size_t i = 0; i < component_parent.size(); ++i) component_parent[i] = i;
    const auto component_root = [&](std::size_t face) {
        while (component_parent[face] != face) {
            component_parent[face] = component_parent[component_parent[face]];
            face = component_parent[face];
        }
        return face;
    };
    std::map<std::pair<int, int>, std::size_t> first_face_of_edge;
    for (std::size_t face = 0; face < triangles.size(); ++face) {
        const auto& triangle = triangles[face];
        for (int i = 0; i < 3; ++i) {
            const int a = triangle[static_cast<std::size_t>(i)];
            const int b = triangle[static_cast<std::size_t>((i + 1) % 3)];
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            const auto [it, inserted] = first_face_of_edge.emplace(key, face);
            if (!inserted) {
                const auto lhs = component_root(face);
                const auto rhs = component_root(it->second);
                if (lhs != rhs) component_parent[rhs] = lhs;
            }
        }
    }
    std::map<std::size_t, std::vector<FaceId>> shell_faces;
    for (std::size_t face = 0; face < faces.size(); ++face) {
        shell_faces[component_root(face)].push_back(faces[face]);
    }
    for (auto& [component, connected_faces] : shell_faces) {
        (void)component;
        const auto shell_id = ShellId {state.allocate_id()};
        ShellRecord shell;
        shell.faces = std::move(connected_faces);
        shell.source_shells = record.source_shells;
        shell.source_faces = record.source_faces.empty() ? shell.faces : record.source_faces;
        state.shells.emplace(shell_id.value, std::move(shell));
        record.shells.push_back(shell_id);
    }
    record.bbox = bbox;
    record.sweep_polyhedral_mass_valid = true;
    record.sweep_polyhedral_volume = volume;
    record.sweep_cached_surface_area = surface_area;
    record.sweep_polyhedral_centroid = centroid;
    record.sweep_inertia_about_centroid = inertia;
    record.a = volume;
    record.extrude_poly_cap_area = 0.0;
    if (!record.sweep_frame_closed) {
        for (const auto& cap : caps) {
            record.extrude_poly_cap_area += 0.5 * norm(cross(
                subtract(positions[cap[1]], positions[cap[0]]),
                subtract(positions[cap[2]], positions[cap[0]])));
        }
    }
    record.extrude_lateral_area = surface_area - 2.0 * record.extrude_poly_cap_area;
    record.extrude_mass_centroid = centroid;
    return true;
}

struct RevolveProfileRegion {
    Vec3 normal {};
    Vec3 radial_direction {};
    std::vector<Point3> points;
    std::vector<std::pair<int, int>> boundary;
    std::vector<std::array<int, 3>> caps;
    std::vector<Scalar> signed_radius;
    std::vector<bool> on_axis;
    Scalar snap_tolerance {0.0};
};

/// Validate and flatten a meridian polygonal region before a revolution allocates
/// topology. Hole winding is immaterial; the region triangulator normalizes the
/// outer/hole orientations and rejects intersections, nesting and touching rings.
/// A holed region must remain wholly off axis. The axis-edge solid special case is
/// retained for a single outer ring only.
inline bool prepare_revolve_profile_region(const KernelState& state, const BodyRecord& record,
                                           std::size_t station_limit, RevolveProfileRegion& region) {
    region = {};
    const auto& outer = record.revolve_profile_xyz;
    if (outer.size() < 3 || station_limit == 0 ||
        outer.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) / station_limit) {
        return false;
    }
    const Vec3 axis = normalize(record.axis);
    if (!std::isfinite(axis.x) || !std::isfinite(axis.y) || !std::isfinite(axis.z) || norm(axis) <= 1e-14 ||
        !std::isfinite(record.origin.x) || !std::isfinite(record.origin.y) || !std::isfinite(record.origin.z)) {
        return false;
    }
    const auto raw_normal = newell_normal_unnormalized_poly(
        std::span<const Point3>(outer.data(), outer.size()));
    const Scalar normal_length = norm(raw_normal);
    if (!std::isfinite(normal_length) || normal_length <= 1e-14) return false;
    region.normal = scale(raw_normal, 1.0 / normal_length);
    const Scalar plane_tol = std::max(Scalar(1e-7), state.config.tolerance.linear * Scalar(100.0));
    if (std::abs(dot(axis, region.normal)) > 1e-7 ||
        std::abs(dot(region.normal, subtract(record.origin, outer.front()))) > plane_tol) {
        return false;
    }

    if (record.revolve_holes_xyz.empty()) {
        region.points = outer;
        if (!triangulate_extrude_profile(
                std::span<const Point3>(outer.data(), outer.size()), region.normal, region.caps)) {
            return false;
        }
        for (int i = 0; i < static_cast<int>(outer.size()); ++i) {
            region.boundary.emplace_back(i, (i + 1) % static_cast<int>(outer.size()));
        }
    } else if (!triangulate_extrude_region(outer, record.revolve_holes_xyz, region.normal, plane_tol,
                                           region.points, region.boundary, region.caps)) {
        return false;
    }
    if (region.points.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) / station_limit) {
        return false;
    }

    Scalar coordinate_scale = 1.0;
    for (const auto& point : region.points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
            std::abs(dot(region.normal, subtract(point, outer.front()))) > plane_tol) {
            return false;
        }
        coordinate_scale = std::max({coordinate_scale, std::abs(point.x - record.origin.x),
                                     std::abs(point.y - record.origin.y),
                                     std::abs(point.z - record.origin.z)});
    }
    region.radial_direction = normalize(cross(region.normal, axis));
    if (norm(region.radial_direction) <= 1e-14) return false;
    region.snap_tolerance = std::max(
        Scalar(1e-12), Scalar(128) * std::numeric_limits<Scalar>::epsilon() * coordinate_scale);
    const Scalar clearance_tol = std::max(Scalar(1e-8), state.config.tolerance.linear * Scalar(10.0));
    region.signed_radius.resize(region.points.size());
    bool positive = false;
    bool negative = false;
    for (std::size_t i = 0; i < region.points.size(); ++i) {
        const Vec3 offset = subtract(region.points[i], record.origin);
        region.signed_radius[i] = dot(offset, region.radial_direction);
        const auto axial = scale(axis, dot(offset, axis));
        const Vec3 radial {offset.x - axial.x, offset.y - axial.y, offset.z - axial.z};
        const Scalar distance = norm(radial);
        if (!std::isfinite(distance) ||
            std::abs(distance - std::abs(region.signed_radius[i])) > plane_tol) {
            return false;
        }
        positive = positive || region.signed_radius[i] > region.snap_tolerance;
        negative = negative || region.signed_radius[i] < -region.snap_tolerance;
    }
    if (positive && negative) return false;
    if (negative) {
        region.radial_direction = scale(region.radial_direction, -1.0);
        for (auto& radius : region.signed_radius) radius = -radius;
    }

    region.on_axis.assign(region.points.size(), false);
    std::vector<int> axis_vertices;
    for (std::size_t i = 0; i < region.points.size(); ++i) {
        if (std::abs(region.signed_radius[i]) <= region.snap_tolerance) {
            region.on_axis[i] = true;
            axis_vertices.push_back(static_cast<int>(i));
        } else if (region.signed_radius[i] < clearance_tol) {
            return false;
        }
    }
    if (!axis_vertices.empty()) {
        if (!record.revolve_holes_xyz.empty() || axis_vertices.size() != 2) return false;
        const int a = axis_vertices[0];
        const int b = axis_vertices[1];
        const bool has_axis_edge = std::any_of(
            region.boundary.begin(), region.boundary.end(), [a, b](const auto& edge) {
                return (edge.first == a && edge.second == b) || (edge.first == b && edge.second == a);
            });
        if (!has_axis_edge) return false;
    }
    return !region.points.empty() && !region.boundary.empty() && !region.caps.empty();
}

/// 子午面多边形区域的整周旋转。轮廓与轴分离时形成环形闭壳；轮廓仅有一条边在轴上时，
/// 轴上端点在所有角向站共享，该零面积旋转边不生成面。全部验证、剖分和质量积分都在分配对象前完成。
inline bool try_materialize_sweep_revolve_full_body(KernelState& state, BodyRecord& record) {
    if (record.kind != BodyKind::Sweep || record.rep_kind != RepKind::ExactBRep || !record.bbox.is_valid ||
        !record.shells.empty() || !record.revolve_full_turn ||
        record.label.size() < 8 || record.label.compare(0, 8, "revolve:") != 0) {
        return false;
    }
    constexpr Scalar kPi = 3.1415926535897932384626433832795;
    constexpr int kStations = 48;
    const Vec3 u = normalize(record.axis);
    RevolveProfileRegion region;
    if (!prepare_revolve_profile_region(state, record, kStations, region)) return false;
    const auto& points = region.points;
    const auto& on_axis = region.on_axis;
    const Scalar snap_tol = region.snap_tolerance;

    const int n = static_cast<int>(points.size());
    std::vector<Point3> positions;
    positions.reserve(static_cast<std::size_t>(kStations * n));
    std::vector<int> vertex_at(static_cast<std::size_t>(kStations * n), -1);
    for (int i = 0; i < n; ++i) {
        if (!on_axis[static_cast<std::size_t>(i)]) continue;
        const auto offset = subtract(points[static_cast<std::size_t>(i)], record.origin);
        const auto projected = add_point_vec(record.origin, scale(u, dot(offset, u)));
        const int index = static_cast<int>(positions.size());
        positions.push_back(projected);
        for (int station = 0; station < kStations; ++station) {
            vertex_at[static_cast<std::size_t>(station * n + i)] = index;
        }
    }
    for (int station = 0; station < kStations; ++station) {
        const Scalar angle = 2.0 * kPi * static_cast<Scalar>(station) / static_cast<Scalar>(kStations);
        const Scalar cosine = std::cos(angle), sine = std::sin(angle);
        for (int i = 0; i < n; ++i) {
            if (on_axis[static_cast<std::size_t>(i)]) continue;
            const auto p = rodrigues_rotate_point_revolve(points[static_cast<std::size_t>(i)], record.origin,
                                                          u, cosine, sine);
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
            vertex_at[static_cast<std::size_t>(station * n + i)] = static_cast<int>(positions.size());
            positions.push_back(p);
        }
    }

    std::vector<std::array<int, 3>> triangles;
    triangles.reserve(2 * static_cast<std::size_t>(kStations) * region.boundary.size());
    const Scalar triangle_area_tol = std::max(Scalar(1e-20), snap_tol * snap_tol);
    const auto append_triangle = [&](int a, int b, int c, std::vector<std::array<int, 3>>& output) {
        if (a == b || b == c || c == a) return true;
        const auto area2 = norm(cross(subtract(positions[static_cast<std::size_t>(b)], positions[static_cast<std::size_t>(a)]),
                                      subtract(positions[static_cast<std::size_t>(c)], positions[static_cast<std::size_t>(a)])));
        if (!std::isfinite(area2) || area2 <= triangle_area_tol) return false;
        output.push_back({a, b, c});
        return true;
    };
    for (int station = 0; station < kStations; ++station) {
        const int next_station = (station + 1) % kStations;
        for (const auto& [i, next_i] : region.boundary) {
            if (on_axis[static_cast<std::size_t>(i)] && on_axis[static_cast<std::size_t>(next_i)]) continue;
            const int a = vertex_at[static_cast<std::size_t>(station * n + i)];
            const int b = vertex_at[static_cast<std::size_t>(station * n + next_i)];
            const int c = vertex_at[static_cast<std::size_t>(next_station * n + next_i)];
            const int d = vertex_at[static_cast<std::size_t>(next_station * n + i)];
            if (!append_triangle(a, b, c, triangles) || !append_triangle(a, c, d, triangles)) return false;
        }
    }
    if (triangles.empty()) return false;
    std::map<std::pair<int, int>, int> edge_use_count;
    for (const auto& triangle : triangles) {
        for (int i = 0; i < 3; ++i) {
            const int a = triangle[static_cast<std::size_t>(i)];
            const int b = triangle[static_cast<std::size_t>((i + 1) % 3)];
            ++edge_use_count[{std::min(a, b), std::max(a, b)}];
        }
    }
    if (edge_use_count.empty() || std::any_of(edge_use_count.begin(), edge_use_count.end(),
        [](const auto& item) { return item.second != 2; })) return false;

    Scalar volume = 0.0, surface_area = 0.0;
    Point3 centroid {};
    std::array<Scalar, 9> inertia {};
    polyhedral_mass_properties_from_triangles(positions, triangles, volume, centroid, inertia, surface_area);
    if (!(volume > 1e-18)) {
        for (auto& triangle : triangles) std::swap(triangle[1], triangle[2]);
        polyhedral_mass_properties_from_triangles(positions, triangles, volume, centroid, inertia, surface_area);
    }
    if (!(volume > 1e-18) || !std::isfinite(surface_area) || !(surface_area > 0.0)) return false;

    BoundingBox bbox {};
    for (const auto& p : positions) extend_materialization_bbox(bbox, p);
    if (!bbox.is_valid) return false;

    std::vector<VertexId> vertices(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        vertices[i] = VertexId {state.allocate_id()};
        state.vertices.emplace(vertices[i].value, VertexRecord {positions[i]});
    }
    std::vector<EdgeId> edges;
    std::map<std::pair<int, int>, int> edge_to_index;
    const auto edge_index_for_pair = [&](int a, int b) -> int {
        const auto key = std::make_pair(std::min(a, b), std::max(a, b));
        if (const auto it = edge_to_index.find(key); it != edge_to_index.end()) return it->second;
        const auto curve = create_materialized_line(state, positions[static_cast<std::size_t>(key.first)],
                                                    positions[static_cast<std::size_t>(key.second)]);
        const auto edge = EdgeId {state.allocate_id()};
        state.edges.emplace(edge.value, EdgeRecord {curve, vertices[static_cast<std::size_t>(key.first)],
                                                    vertices[static_cast<std::size_t>(key.second)]});
        const int index = static_cast<int>(edges.size());
        edges.push_back(edge);
        edge_to_index.emplace(key, index);
        return index;
    };
    const auto source_faces = std::span<const FaceId>(record.source_faces);
    std::vector<FaceId> faces;
    faces.reserve(triangles.size());
    for (const auto& triangle : triangles) {
        std::array<std::pair<int, bool>, 3> refs {};
        for (int i = 0; i < 3; ++i) {
            const int a = triangle[static_cast<std::size_t>(i)];
            const int b = triangle[static_cast<std::size_t>((i + 1) % 3)];
            refs[static_cast<std::size_t>(i)] = {edge_index_for_pair(a, b), a > b};
        }
        const auto normal = cross(subtract(positions[static_cast<std::size_t>(triangle[1])],
                                           positions[static_cast<std::size_t>(triangle[0])]),
                                  subtract(positions[static_cast<std::size_t>(triangle[2])],
                                           positions[static_cast<std::size_t>(triangle[0])]));
        faces.push_back(create_materialized_polygon_face(
            state, edges, std::span<const std::pair<int, bool>>(refs), normal, source_faces));
    }
    // With no start/end cap, each revolved boundary ring is a separate closed
    // shell. Keep that ownership explicit instead of placing disconnected face
    // components in one shell record.
    std::vector<std::size_t> component_parent(triangles.size());
    for (std::size_t i = 0; i < component_parent.size(); ++i) component_parent[i] = i;
    const auto component_root = [&](std::size_t face) {
        while (component_parent[face] != face) {
            component_parent[face] = component_parent[component_parent[face]];
            face = component_parent[face];
        }
        return face;
    };
    std::map<std::pair<int, int>, std::size_t> first_face_of_edge;
    for (std::size_t face = 0; face < triangles.size(); ++face) {
        const auto& triangle = triangles[face];
        for (int i = 0; i < 3; ++i) {
            const int a = triangle[static_cast<std::size_t>(i)];
            const int b = triangle[static_cast<std::size_t>((i + 1) % 3)];
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            const auto [it, inserted] = first_face_of_edge.emplace(key, face);
            if (!inserted) {
                const auto lhs = component_root(face);
                const auto rhs = component_root(it->second);
                if (lhs != rhs) component_parent[rhs] = lhs;
            }
        }
    }
    std::map<std::size_t, std::vector<FaceId>> shell_faces;
    for (std::size_t face = 0; face < faces.size(); ++face) {
        shell_faces[component_root(face)].push_back(faces[face]);
    }
    for (auto& [component, connected_faces] : shell_faces) {
        (void)component;
        const auto shell_id = ShellId {state.allocate_id()};
        ShellRecord shell;
        shell.faces = std::move(connected_faces);
        shell.source_shells = record.source_shells;
        shell.source_faces = record.source_faces.empty() ? shell.faces : record.source_faces;
        state.shells.emplace(shell_id.value, std::move(shell));
        record.shells.push_back(shell_id);
    }
    record.bbox = bbox;
    record.sweep_polyhedral_mass_valid = true;
    record.sweep_polyhedral_volume = volume;
    record.sweep_cached_surface_area = surface_area;
    record.sweep_polyhedral_centroid = centroid;
    record.sweep_inertia_about_centroid = inertia;
    record.a = volume;
    return true;
}

/// 子午面闭合多边形区域绕其平面内轴旋转 `< 2π`。角向按与整周旋转相同的上限分段，
/// 每段生成真实直纹三角侧壁，首尾用约束带孔区域剖分封盖。轮廓可与轴分离，或仅以一条
/// 连续边接触轴；轴上顶点跨站共享，避免产生零长边和退化侧壁。全部几何、流形和质量
/// 检查均在分配内核对象之前完成，因此拒绝路径不会污染模型或活动事务。
inline bool try_materialize_sweep_revolve_meridian_body(KernelState& state, BodyRecord& record) {
    if (record.kind != BodyKind::Sweep || record.rep_kind != RepKind::ExactBRep || !record.bbox.is_valid ||
        !record.shells.empty() || record.revolve_full_turn) {
        return false;
    }
    if (record.label.size() < 8 || record.label.compare(0, 8, "revolve:") != 0) {
        return false;
    }
    constexpr Scalar kPi = 3.1415926535897932384626433832795;
    constexpr int kFullTurnSegments = 48;
    const Scalar ang = record.b;
    const Scalar two_pi = 2.0 * kPi;
    if (!std::isfinite(ang) || !(ang > 0.0) || ang >= two_pi - 1e-10) return false;
    const auto u = normalize(record.axis);
    const Point3 O = record.origin;
    const int segment_count = std::max(1, static_cast<int>(std::ceil(
        ang / (two_pi / static_cast<Scalar>(kFullTurnSegments)))));
    const int station_count = segment_count + 1;
    RevolveProfileRegion region;
    if (!prepare_revolve_profile_region(state, record, static_cast<std::size_t>(station_count), region)) {
        return false;
    }
    const auto& points = region.points;
    const auto& on_axis = region.on_axis;
    const auto& cap_triangles = region.caps;
    const Scalar snap_tol = region.snap_tolerance;
    const int n = static_cast<int>(points.size());
    std::vector<Point3> pos;
    pos.reserve(static_cast<std::size_t>(station_count * n));
    std::vector<int> vertex_at(static_cast<std::size_t>(station_count * n), -1);
    for (int i = 0; i < n; ++i) {
        if (!on_axis[static_cast<std::size_t>(i)]) continue;
        const auto offset = subtract(points[static_cast<std::size_t>(i)], O);
        const Point3 projected = add_point_vec(O, scale(u, dot(offset, u)));
        const int index = static_cast<int>(pos.size());
        pos.push_back(projected);
        for (int station = 0; station < station_count; ++station) {
            vertex_at[static_cast<std::size_t>(station * n + i)] = index;
        }
    }
    for (int station = 0; station < station_count; ++station) {
        const Scalar station_angle = ang * static_cast<Scalar>(station) / static_cast<Scalar>(segment_count);
        const Scalar cosine = std::cos(station_angle), sine = std::sin(station_angle);
        for (int i = 0; i < n; ++i) {
            if (on_axis[static_cast<std::size_t>(i)]) continue;
            const auto p = rodrigues_rotate_point_revolve(points[static_cast<std::size_t>(i)], O, u, cosine, sine);
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
            vertex_at[static_cast<std::size_t>(station * n + i)] = static_cast<int>(pos.size());
            pos.push_back(p);
        }
    }

    std::vector<std::array<int, 3>> tris;
    tris.reserve(2 * static_cast<std::size_t>(segment_count) * region.boundary.size() +
                 2 * cap_triangles.size());
    const Scalar triangle_area_tol = std::max(Scalar(1e-20), snap_tol * snap_tol);
    const auto append_triangle = [&](int a, int b, int c) {
        if (a == b || b == c || c == a) return true;
        const Scalar area2 = norm(cross(subtract(pos[static_cast<std::size_t>(b)], pos[static_cast<std::size_t>(a)]),
                                        subtract(pos[static_cast<std::size_t>(c)], pos[static_cast<std::size_t>(a)])));
        if (!std::isfinite(area2) || area2 <= triangle_area_tol) return false;
        tris.push_back({a, b, c});
        return true;
    };
    for (int station = 0; station < segment_count; ++station) {
        for (const auto& [i, next_i] : region.boundary) {
            if (on_axis[static_cast<std::size_t>(i)] && on_axis[static_cast<std::size_t>(next_i)]) continue;
            const int a = vertex_at[static_cast<std::size_t>(station * n + i)];
            const int b = vertex_at[static_cast<std::size_t>(station * n + next_i)];
            const int c = vertex_at[static_cast<std::size_t>((station + 1) * n + next_i)];
            const int d = vertex_at[static_cast<std::size_t>((station + 1) * n + i)];
            if (!append_triangle(a, b, c) || !append_triangle(a, c, d)) return false;
        }
    }
    for (const auto& triangle : cap_triangles) {
        const int a = vertex_at[static_cast<std::size_t>(triangle[0])];
        const int b = vertex_at[static_cast<std::size_t>(triangle[1])];
        const int c = vertex_at[static_cast<std::size_t>(triangle[2])];
        const int end = segment_count * n;
        if (!append_triangle(a, c, b) ||
            !append_triangle(vertex_at[static_cast<std::size_t>(end + triangle[0])],
                             vertex_at[static_cast<std::size_t>(end + triangle[1])],
                             vertex_at[static_cast<std::size_t>(end + triangle[2])])) return false;
    }
    if (tris.empty()) return false;

    std::map<std::pair<int, int>, int> edge_use_count;
    for (const auto& triangle : tris) {
        for (int i = 0; i < 3; ++i) {
            const int a = triangle[static_cast<std::size_t>(i)];
            const int b = triangle[static_cast<std::size_t>((i + 1) % 3)];
            ++edge_use_count[{std::min(a, b), std::max(a, b)}];
        }
    }
    if (edge_use_count.empty() || std::any_of(edge_use_count.begin(), edge_use_count.end(),
        [](const auto& item) { return item.second != 2; })) return false;

    Scalar vol_chk = 0.0;
    Point3 cm_tmp {};
    std::array<Scalar, 9> in_tmp {};
    Scalar area_tmp = 0.0;
    polyhedral_mass_properties_from_triangles(pos, tris, vol_chk, cm_tmp, in_tmp, area_tmp);
    if (!(vol_chk > 1e-18)) {
        for (auto& t : tris) {
            std::swap(t[1], t[2]);
        }
        polyhedral_mass_properties_from_triangles(pos, tris, vol_chk, cm_tmp, in_tmp, area_tmp);
    }
    if (!(vol_chk > 1e-18) || !std::isfinite(area_tmp) || !(area_tmp > 0.0)) return false;

    BoundingBox bbox {};
    for (const auto& p : pos) extend_materialization_bbox(bbox, p);
    if (!bbox.is_valid) return false;

    std::vector<VertexId> vid(pos.size());
    for (std::size_t i = 0; i < pos.size(); ++i) {
        vid[i] = VertexId {state.allocate_id()};
        state.vertices.emplace(vid[i].value, VertexRecord {pos[i]});
    }

    std::vector<EdgeId> edges;
    std::map<std::pair<int, int>, int> edge_to_index;
    auto edge_index_for_pair = [&](int a, int b) -> int {
        const int lo = std::min(a, b);
        const int hi = std::max(a, b);
        const auto key = std::make_pair(lo, hi);
        const auto it = edge_to_index.find(key);
        if (it != edge_to_index.end()) {
            return it->second;
        }
        const auto curve_id =
            create_materialized_line(state, pos[static_cast<std::size_t>(lo)], pos[static_cast<std::size_t>(hi)]);
        const auto eid = EdgeId {state.allocate_id()};
        state.edges.emplace(eid.value,
                            EdgeRecord {curve_id, vid[static_cast<std::size_t>(lo)], vid[static_cast<std::size_t>(hi)]});
        const int idx = static_cast<int>(edges.size());
        edges.push_back(eid);
        edge_to_index.emplace(key, idx);
        return idx;
    };
    auto coedge_ref = [&](int a, int b) -> std::pair<int, bool> {
        const int lo = std::min(a, b);
        const int hi = std::max(a, b);
        const int ei = edge_index_for_pair(lo, hi);
        const bool rev = (a == hi);
        return {ei, rev};
    };
    auto tri_normal = [&](int a, int b, int c) -> Vec3 {
        const auto pa = pos[static_cast<std::size_t>(a)];
        const auto pb = pos[static_cast<std::size_t>(b)];
        const auto pc = pos[static_cast<std::size_t>(c)];
        const auto e1 = subtract(pb, pa);
        const auto e2 = subtract(pc, pa);
        return safe_unit_normal(cross(e1, e2));
    };

    const auto source_faces = std::span<const FaceId>(record.source_faces);
    std::vector<FaceId> faces;
    faces.reserve(tris.size());

    for (const auto& t : tris) {
        const auto [e0, r0] = coedge_ref(t[0], t[1]);
        const auto [e1, r1] = coedge_ref(t[1], t[2]);
        const auto [e2, r2] = coedge_ref(t[2], t[0]);
        const std::array<std::pair<int, bool>, 3> refs {{{e0, r0}, {e1, r1}, {e2, r2}}};
        faces.push_back(create_materialized_polygon_face(state, edges, std::span<const std::pair<int, bool>>(refs),
                                                         tri_normal(t[0], t[1], t[2]), source_faces));
    }

    const auto shell_id = ShellId {state.allocate_id()};
    ShellRecord shell;
    shell.faces = std::move(faces);
    shell.source_shells = record.source_shells;
    if (record.source_faces.empty()) {
        shell.source_faces = shell.faces;
    } else {
        shell.source_faces = record.source_faces;
    }
    state.shells.emplace(shell_id.value, std::move(shell));
    record.shells.push_back(shell_id);

    record.sweep_polyhedral_mass_valid = true;
    record.sweep_polyhedral_volume = vol_chk;
    record.sweep_cached_surface_area = area_tmp;
    record.sweep_polyhedral_centroid = cm_tmp;
    record.sweep_inertia_about_centroid = in_tmp;
    record.bbox = bbox;
    record.a = vol_chk;
    return true;
}

inline void materialize_body_bbox_topology(KernelState& state, BodyRecord& record) {
    if (record.rep_kind != RepKind::ExactBRep || !record.bbox.is_valid || !record.shells.empty()) {
        return;
    }
    inherit_source_topology_from_owned_shells(state, record);
    infer_source_shells_from_source_faces(state, record);
    sanitize_source_references(state, record);
    if (try_materialize_sweep_extrude_prism_body(state, record)) {
        return;
    }
    if (try_materialize_sweep_revolve_meridian_body(state, record)) {
        return;
    }
    if (!materialize_body_from_source_faces(state, record) &&
        !materialize_body_from_source_shells(state, record)) {
        materialize_body_bbox_shell(state, record);
    }
}

}  // namespace axiom::detail
