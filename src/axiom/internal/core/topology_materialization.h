#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
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

/// 平面闭合多边形沿 `record.axis`（单位）拉伸 `record.b`：棱柱三角剖分 + 闭合流形 BRep（**凸**多边形扇形三角化；非平面/退化/拉伸方向平行于面则返回 false）。
inline bool try_materialize_sweep_extrude_prism_body(KernelState& state, BodyRecord& record) {
    if (record.kind != BodyKind::Sweep || record.rep_kind != RepKind::ExactBRep || !record.bbox.is_valid ||
        !record.shells.empty()) {
        return false;
    }
    if (record.label.size() < 8 || record.label.compare(0, 8, "extrude:") != 0) {
        return false;
    }
    const auto& poly_in = record.extrude_profile_xyz;
    const int n = static_cast<int>(poly_in.size());
    if (n < 3) {
        return false;
    }
    const Scalar h = record.b;
    if (!(h > 0.0)) {
        return false;
    }
    const Vec3 D = normalize(record.axis);
    if (norm(D) <= 1e-14) {
        return false;
    }
    const auto n_raw = newell_normal_unnormalized_poly(std::span<const Point3>(poly_in.data(), poly_in.size()));
    const auto n_len = norm(n_raw);
    if (n_len <= 1e-14) {
        return false;
    }
    const auto n_unit = scale(n_raw, 1.0 / n_len);
    const Scalar plane_tol = std::max(Scalar(1e-7), state.config.tolerance.linear * Scalar(100.0));
    const Point3& p0r = poly_in[0];
    const Scalar d0 = n_unit.x * p0r.x + n_unit.y * p0r.y + n_unit.z * p0r.z;
    for (const auto& pt : poly_in) {
        const Scalar dd = std::abs(n_unit.x * pt.x + n_unit.y * pt.y + n_unit.z * pt.z - d0);
        if (dd > plane_tol) {
            return false;
        }
    }
    const Scalar align = std::abs(dot(D, n_unit));
    if (align < Scalar(1e-6)) {
        return false;
    }

    std::vector<Point3> pos(static_cast<std::size_t>(2 * n));
    for (int i = 0; i < n; ++i) {
        pos[static_cast<std::size_t>(i)] = poly_in[static_cast<std::size_t>(i)];
        pos[static_cast<std::size_t>(n + i)] =
            add_point_vec(poly_in[static_cast<std::size_t>(i)], scale(D, h));
    }

    std::vector<std::array<int, 3>> tris;
    tris.reserve(static_cast<std::size_t>((n - 2) * 2 + n * 2));
    for (int k = 1; k < n - 1; ++k) {
        tris.push_back({0, k + 1, k});
    }
    for (int k = 1; k < n - 1; ++k) {
        tris.push_back({n, n + k, n + k + 1});
    }
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        tris.push_back({i, j, n + j});
        tris.push_back({i, n + j, n + i});
    }

    Scalar vol_chk = 0.0;
    Point3 cm_tmp {};
    std::array<Scalar, 9> in_tmp {};
    Scalar area_tmp = 0.0;
    polyhedral_mass_properties_from_triangles(pos, tris, vol_chk, cm_tmp, in_tmp, area_tmp);
    if (vol_chk < 0.0) {
        for (auto& t : tris) {
            std::swap(t[1], t[2]);
        }
        polyhedral_mass_properties_from_triangles(pos, tris, vol_chk, cm_tmp, in_tmp, area_tmp);
    }
    if (!(vol_chk > 1e-18)) {
        return false;
    }

    std::vector<VertexId> vid(static_cast<std::size_t>(2 * n));
    for (int i = 0; i < 2 * n; ++i) {
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
    return true;
}

/// 子午面闭合多边形绕其平面内轴旋转 `< 2π`：侧壁为直纹面的两三角形剖分 + 两侧平面封盖，闭合流形 BRep。
inline bool try_materialize_sweep_revolve_meridian_body(KernelState& state, BodyRecord& record) {
    if (record.kind != BodyKind::Sweep || record.rep_kind != RepKind::ExactBRep || !record.bbox.is_valid ||
        !record.shells.empty()) {
        return false;
    }
    if (record.label.size() < 8 || record.label.compare(0, 8, "revolve:") != 0) {
        return false;
    }
    const auto& poly = record.revolve_profile_xyz;
    const int n = static_cast<int>(poly.size());
    if (n < 3) {
        return false;
    }
    constexpr Scalar kPi = 3.14159265358979323846;
    const Scalar ang = record.b;
    const Scalar two_pi = 2.0 * kPi;
    if (!(ang > 0.0) || ang >= two_pi - 1e-3) {
        return false;
    }
    const auto n_raw = newell_normal_unnormalized_poly(std::span<const Point3>(poly.data(), poly.size()));
    if (norm(n_raw) <= 1e-14) {
        return false;
    }
    const auto n_unit = normalize(n_raw);
    const auto u = normalize(record.axis);
    const Scalar axis_in_plane_tol = std::max(1e-7, state.config.tolerance.linear * 10.0);
    if (std::abs(dot(u, n_unit)) > axis_in_plane_tol) {
        return false;
    }
    const Point3 O = record.origin;
    const Scalar ct = std::cos(ang);
    const Scalar st = std::sin(ang);

    std::vector<Point3> pos(static_cast<std::size_t>(2 * n));
    for (int i = 0; i < n; ++i) {
        pos[static_cast<std::size_t>(i)] = poly[static_cast<std::size_t>(i)];
        pos[static_cast<std::size_t>(n + i)] =
            rodrigues_rotate_point_revolve(poly[static_cast<std::size_t>(i)], O, u, ct, st);
    }

    std::vector<std::array<int, 3>> tris;
    tris.reserve(static_cast<std::size_t>(2 * n + 2 * std::max(0, n - 2)));
    for (int i = 0; i < n; ++i) {
        const int ip = (i + 1) % n;
        tris.push_back({i, ip, n + ip});
        tris.push_back({i, n + ip, n + i});
    }
    for (int k = 1; k < n - 1; ++k) {
        tris.push_back({0, k, k + 1});
    }
    for (int k = 1; k < n - 1; ++k) {
        tris.push_back({n, n + k + 1, n + k});
    }

    Scalar vol_chk = 0.0;
    Point3 cm_tmp {};
    std::array<Scalar, 9> in_tmp {};
    Scalar area_tmp = 0.0;
    polyhedral_mass_properties_from_triangles(pos, tris, vol_chk, cm_tmp, in_tmp, area_tmp);
    if (vol_chk < 0.0) {
        for (auto& t : tris) {
            std::swap(t[1], t[2]);
        }
        polyhedral_mass_properties_from_triangles(pos, tris, vol_chk, cm_tmp, in_tmp, area_tmp);
    }
    if (!(vol_chk > 1e-18)) {
        return false;
    }

    std::vector<VertexId> vid(static_cast<std::size_t>(2 * n));
    for (int i = 0; i < 2 * n; ++i) {
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

