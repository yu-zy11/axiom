#pragma once

#include <cstddef>
#include <vector>

#include "axiom/core/types.h"

namespace axiom::detail {

Scalar axis_distance(Scalar value, Scalar min_v, Scalar max_v);
BoundingBox mesh_bbox_from_vertices(const std::vector<Point3>& vertices);
std::vector<Point3> bbox_corners(const BoundingBox& bbox);
std::vector<Index> triangulate_bbox(std::size_t slices_per_face);
bool is_valid_bbox(const BoundingBox& bbox);
bool has_valid_tessellation_options(const TessellationOptions& options);
std::size_t tessellation_slices_per_face(const TessellationOptions& options);
bool has_out_of_range_indices(const std::vector<Point3>& vertices, const std::vector<Index>& indices);
bool has_degenerate_triangles(const std::vector<Point3>& vertices, const std::vector<Index>& indices);
std::uint64_t mesh_connected_components(const std::vector<Index>& indices, std::size_t vertex_count);

}  // namespace axiom::detail
