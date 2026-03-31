#include "axiom/internal/rep/representation_internal_utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace axiom::detail {

Scalar axis_distance(Scalar value, Scalar min_v, Scalar max_v) {
    if (value < min_v) {
        return min_v - value;
    }
    if (value > max_v) {
        return value - max_v;
    }
    return 0.0;
}

BoundingBox mesh_bbox_from_vertices(const std::vector<Point3>& vertices) {
    if (vertices.empty()) {
        return BoundingBox {};
    }

    Point3 min = vertices.front();
    Point3 max = vertices.front();
    for (const auto& p : vertices) {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
        max.z = std::max(max.z, p.z);
    }
    return BoundingBox {min, max, true};
}

std::vector<Point3> bbox_corners(const BoundingBox& bbox) {
    return {
        {bbox.min.x, bbox.min.y, bbox.min.z},
        {bbox.max.x, bbox.min.y, bbox.min.z},
        {bbox.max.x, bbox.max.y, bbox.min.z},
        {bbox.min.x, bbox.max.y, bbox.min.z},
        {bbox.min.x, bbox.min.y, bbox.max.z},
        {bbox.max.x, bbox.min.y, bbox.max.z},
        {bbox.max.x, bbox.max.y, bbox.max.z},
        {bbox.min.x, bbox.max.y, bbox.max.z},
    };
}

std::vector<Index> triangulate_bbox(std::size_t slices_per_face) {
    const std::array<std::array<Index, 4>, 6> faces {{
        {0, 1, 2, 3},
        {4, 5, 6, 7},
        {0, 1, 5, 4},
        {2, 3, 7, 6},
        {1, 2, 6, 5},
        {0, 3, 7, 4},
    }};

    std::vector<Index> indices;
    indices.reserve(faces.size() * slices_per_face * 6);
    for (const auto& face : faces) {
        for (std::size_t i = 0; i < slices_per_face; ++i) {
            if ((i % 2) == 0) {
                indices.insert(indices.end(), {face[0], face[1], face[2], face[0], face[2], face[3]});
            } else {
                indices.insert(indices.end(), {face[0], face[1], face[3], face[1], face[2], face[3]});
            }
        }
    }
    return indices;
}

bool is_valid_bbox(const BoundingBox& bbox) {
    return bbox.is_valid &&
           bbox.max.x >= bbox.min.x &&
           bbox.max.y >= bbox.min.y &&
           bbox.max.z >= bbox.min.z;
}

bool has_valid_tessellation_options(const TessellationOptions& options) {
    return options.chordal_error > 0.0 && options.angular_error > 0.0;
}

std::size_t tessellation_slices_per_face(const TessellationOptions& options) {
    const auto slices_from_chordal = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(1.0 / options.chordal_error)));
    const auto slices_from_angular = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(180.0 / options.angular_error)));
    return std::min<std::size_t>(8, std::max(slices_from_chordal, slices_from_angular));
}

bool has_out_of_range_indices(const std::vector<Point3>& vertices, const std::vector<Index>& indices) {
    return std::any_of(indices.begin(), indices.end(),
                       [&vertices](Index idx) { return static_cast<std::size_t>(idx) >= vertices.size(); });
}

bool has_degenerate_triangles(const std::vector<Point3>& vertices, const std::vector<Index>& indices) {
    if ((indices.size() % 3) != 0 || has_out_of_range_indices(vertices, indices)) {
        return true;
    }

    constexpr Scalar kAreaEps = 1e-12;
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        const auto& p0 = vertices[indices[i]];
        const auto& p1 = vertices[indices[i + 1]];
        const auto& p2 = vertices[indices[i + 2]];
        const Vec3 e1 {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
        const Vec3 e2 {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
        const Vec3 cp {
            e1.y * e2.z - e1.z * e2.y,
            e1.z * e2.x - e1.x * e2.z,
            e1.x * e2.y - e1.y * e2.x
        };
        const auto area2 = cp.x * cp.x + cp.y * cp.y + cp.z * cp.z;
        if (area2 <= kAreaEps) {
            return true;
        }
    }
    return false;
}

std::uint64_t mesh_connected_components(const std::vector<Index>& indices, std::size_t vertex_count) {
    if (indices.empty() || vertex_count == 0) {
        return 0;
    }

    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> adjacency;
    adjacency.reserve(vertex_count);
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const auto a = static_cast<std::uint64_t>(indices[i]);
        const auto b = static_cast<std::uint64_t>(indices[i + 1]);
        const auto c = static_cast<std::uint64_t>(indices[i + 2]);
        adjacency[a].push_back(b);
        adjacency[a].push_back(c);
        adjacency[b].push_back(a);
        adjacency[b].push_back(c);
        adjacency[c].push_back(a);
        adjacency[c].push_back(b);
    }

    std::unordered_set<std::uint64_t> visited;
    visited.reserve(adjacency.size());
    std::uint64_t components = 0;
    for (const auto& [seed, _] : adjacency) {
        if (visited.contains(seed)) {
            continue;
        }
        ++components;
        std::queue<std::uint64_t> q;
        q.push(seed);
        visited.insert(seed);
        while (!q.empty()) {
            const auto current = q.front();
            q.pop();
            const auto it = adjacency.find(current);
            if (it == adjacency.end()) {
                continue;
            }
            for (const auto next : it->second) {
                if (!visited.contains(next)) {
                    visited.insert(next);
                    q.push(next);
                }
            }
        }
    }
    return components;
}

}  // namespace axiom::detail
