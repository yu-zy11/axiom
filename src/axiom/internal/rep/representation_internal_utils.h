#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "axiom/core/types.h"
#include "axiom/internal/core/kernel_state.h"

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

// --- Industrial tessellation scaffolding (Stage 1.5) ---

std::string tessellation_cache_key(const BodyRecord& body, const TessellationOptions& options);
std::string tessellation_budget_digest_json(const TessellationOptions& options);
std::string face_tessellation_cache_key(const KernelState& state, FaceId face_id, const TessellationOptions& options);

// Curvature-sensitive resolution helpers.
std::size_t segments_for_circle(Scalar radius, const TessellationOptions& options);
std::size_t segments_for_length(Scalar length, const TessellationOptions& options);

// Primitive mesh generators (output indexed triangles, optional normals/uvs).
MeshRecord tessellate_box(const BodyRecord& body, const TessellationOptions& options);
MeshRecord tessellate_sphere(const BodyRecord& body, const TessellationOptions& options);
MeshRecord tessellate_cylinder(const BodyRecord& body, const TessellationOptions& options);
MeshRecord tessellate_cone(const BodyRecord& body, const TessellationOptions& options);
MeshRecord tessellate_torus(const BodyRecord& body, const TessellationOptions& options);

// Face-level tessellation (Topo-driven). Uses face boundary (outer loop) to generate a planar triangulation.
MeshRecord tessellate_face_planar(const KernelState& state, FaceId face_id, const TessellationOptions& options);

// Topo-driven face tessellation: analytic / trimmed parameter patches when possible, else planar fan.
MeshRecord tessellate_face(const KernelState& state, FaceId face_id, const TessellationOptions& options);

// Weld vertices by quantized position to improve connectivity across faces.
// If normals are present, they are averaged on weld.
// When texcoords are present (per-vertex), weld key includes quantized UV so seams stay valid.
void weld_mesh_vertices(MeshRecord& mesh, bool weld_normals);

// Derive conversion/round-trip budgets from user tessellation options (explicit industrial closure).
ConversionErrorBudget conversion_error_budget_from_tessellation(const TessellationOptions& options);

}  // namespace axiom::detail

