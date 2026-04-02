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
    /// 仅对指定壳上的拓扑面做面片三角化并焊接（工业验证：壳级网格/自交分析入口；不含其它壳的面）。
    Result<MeshId> brep_to_mesh_shell(BodyId body_id, ShellId shell_id, const TessellationOptions& options);
    // Local re-tessellation: only the provided faces are recomputed (Topo-driven) when body has owned topology.
    Result<MeshId> brep_to_mesh_local(BodyId body_id, std::span<const FaceId> dirty_faces, const TessellationOptions& options);
    /// 创建 `MeshRep` 体，并把该网格记录的 `source_body` 指向新体，供后续 `brep_to_mesh` 嵌入返回同一网格。
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
    /// 导出体级/面级三角化缓存计数（JSON），供 CI/回归归档与门禁。
    Result<void> export_tessellation_cache_stats_json(std::string_view path) const;
    Result<RoundTripReport> verify_brep_mesh_round_trip(BodyId body_id, const TessellationOptions& options);
    Result<RoundTripReport> verify_mesh_brep_round_trip(MeshId mesh_id, const TessellationOptions& options);
    /// 将 `RoundTripReport`（含 `budget` 与 `tessellation_budget_digest`）导出为单行 JSON，供 CI/回归归档。
    Result<void> export_round_trip_report_json(const RoundTripReport& report, std::string_view path) const;
    /// 由 `TessellationOptions` 派生 round-trip / 转换用误差预算（与 `verify_*_round_trip` 内 `RoundTripReport::budget` 一致）。
    Result<ConversionErrorBudget> conversion_error_budget_for_tessellation(const TessellationOptions& options) const;
    /// 将 `conversion_error_budget_for_tessellation` 结果写成单行 JSON（含 `derivation` 标签）。
    Result<void> export_conversion_error_budget_json(const TessellationOptions& options, std::string_view path) const;
    /// 体级/面级三角化缓存命中、未命中与陈旧键淘汰计数（`Kernel::clear_mesh_store` 会重置）。
    Result<TessellationCacheStats> tessellation_cache_stats() const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

}  // namespace axiom
