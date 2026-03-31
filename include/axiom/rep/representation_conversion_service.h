#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
}

class RepresentationService {
public:
    explicit RepresentationService(std::shared_ptr<detail::KernelState> state);

    Result<RepKind> kind_of_body(BodyId body_id) const;
    Result<BoundingBox> bbox_of_body(BodyId body_id) const;
    Result<bool> classify_point(BodyId body_id, const Point3& point) const;
    Result<std::vector<bool>> classify_points_batch(BodyId body_id, std::span<const Point3> points) const;
    Result<Scalar> distance_to_body(BodyId body_id, const Point3& point) const;
    Result<std::vector<Scalar>> distances_to_body_batch(BodyId body_id, std::span<const Point3> points) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class RepresentationConversionService {
public:
    explicit RepresentationConversionService(std::shared_ptr<detail::KernelState> state);

    Result<MeshId> brep_to_mesh(BodyId body_id, const TessellationOptions& options);
    Result<BodyId> mesh_to_brep(MeshId mesh_id);
    Result<MeshId> implicit_to_mesh(ImplicitFieldId field_id, const TessellationOptions& options);
    Result<std::vector<MeshId>> brep_to_mesh_batch(std::span<const BodyId> body_ids, const TessellationOptions& options);
    Result<std::vector<BodyId>> mesh_to_brep_batch(std::span<const MeshId> mesh_ids);
    Result<std::uint64_t> mesh_vertex_count(MeshId mesh_id) const;
    Result<std::uint64_t> mesh_index_count(MeshId mesh_id) const;
    Result<std::uint64_t> mesh_triangle_count(MeshId mesh_id) const;
    Result<std::uint64_t> mesh_connected_components(MeshId mesh_id) const;
    Result<bool> mesh_has_out_of_range_indices(MeshId mesh_id) const;
    Result<bool> mesh_has_degenerate_triangles(MeshId mesh_id) const;
    Result<MeshInspectionReport> inspect_mesh(MeshId mesh_id) const;
    Result<void> export_mesh_report_json(MeshId mesh_id, std::string_view path) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

}  // namespace axiom
