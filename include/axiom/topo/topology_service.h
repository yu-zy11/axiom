#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
struct TopologyTransactionState;
}

/// 拓扑事务隔离/并发语义占位：当前内核为单写入者 + 快照回滚，不等价于数据库 SERIALIZABLE，但可用于宿主侧协议对齐。
enum class TopologyIsolationLevel : std::uint8_t {
  Unspecified = 0,
  /// 单活动事务、修改前快照、回滚恢复（工程上接近「串行化」使用方式，但无跨进程锁）。
  SnapshotSerializable = 1,
};

class TopologyQueryService {
public:
    explicit TopologyQueryService(std::shared_ptr<detail::KernelState> state);

    Result<std::array<VertexId, 2>> vertices_of_edge(EdgeId edge_id) const;
    Result<std::vector<CoedgeId>> coedges_of_edge(EdgeId edge_id) const;
    Result<std::vector<LoopId>> loops_of_edge(EdgeId edge_id) const;
    Result<std::vector<FaceId>> faces_of_edge(EdgeId edge_id) const;
    Result<std::vector<ShellId>> shells_of_edge(EdgeId edge_id) const;
    Result<std::vector<EdgeId>> edges_of_loop(LoopId loop_id) const;
    Result<std::vector<LoopId>> loops_of_face(FaceId face_id) const;
    Result<SurfaceId> surface_of_face(FaceId face_id) const;
    Result<std::vector<ShellId>> shells_of_face(FaceId face_id) const;
    Result<std::vector<BodyId>> bodies_of_face(FaceId face_id) const;
    Result<std::vector<FaceId>> source_faces_of_face(FaceId face_id) const;
    Result<std::vector<FaceId>> faces_of_shell(ShellId shell_id) const;
    Result<std::vector<BodyId>> bodies_of_shell(ShellId shell_id) const;
    Result<std::vector<ShellId>> source_shells_of_shell(ShellId shell_id) const;
    Result<std::vector<FaceId>> source_faces_of_shell(ShellId shell_id) const;
    Result<std::vector<ShellId>> shells_of_body(BodyId body_id) const;
    Result<std::vector<BodyId>> source_bodies_of_body(BodyId body_id) const;
    Result<std::vector<ShellId>> source_shells_of_body(BodyId body_id) const;
    Result<std::vector<FaceId>> source_faces_of_body(BodyId body_id) const;
    Result<TopologySummary> summary_of_shell(ShellId shell_id) const;
    Result<TopologySummary> summary_of_body(BodyId body_id) const;
    Result<std::uint64_t> edge_count_of_loop(LoopId loop_id) const;
    Result<std::uint64_t> loop_count_of_face(FaceId face_id) const;
    Result<std::uint64_t> face_count_of_shell(ShellId shell_id) const;
    Result<std::uint64_t> shell_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> coedge_count_of_edge(EdgeId edge_id) const;
    Result<std::uint64_t> owner_count_of_edge(EdgeId edge_id) const;
    Result<std::uint64_t> owner_count_of_face(FaceId face_id) const;
    Result<std::uint64_t> owner_count_of_shell(ShellId shell_id) const;
    Result<bool> has_vertex(VertexId vertex_id) const;
    Result<bool> has_edge(EdgeId edge_id) const;
    Result<bool> has_loop(LoopId loop_id) const;
    Result<bool> has_face(FaceId face_id) const;
    Result<bool> has_shell(ShellId shell_id) const;
    Result<bool> has_body(BodyId body_id) const;
    Result<bool> is_edge_boundary(EdgeId edge_id) const;
    Result<bool> is_edge_non_manifold(EdgeId edge_id) const;
    Result<bool> is_face_orphan(FaceId face_id) const;
    Result<bool> is_shell_orphan(ShellId shell_id) const;
    Result<bool> is_body_derived(BodyId body_id) const;
    Result<BoundingBox> bbox_of_face(FaceId face_id) const;
    Result<BoundingBox> bbox_of_shell(ShellId shell_id) const;
    Result<BoundingBox> bbox_of_body_from_topology(BodyId body_id) const;
    Result<std::vector<FaceId>> faces_of_body(BodyId body_id) const;
    Result<std::vector<LoopId>> loops_of_body(BodyId body_id) const;
    Result<std::vector<EdgeId>> edges_of_body(BodyId body_id) const;
    Result<std::vector<VertexId>> vertices_of_body(BodyId body_id) const;
    Result<std::uint64_t> face_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> loop_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> edge_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> vertex_count_of_body(BodyId body_id) const;
    Result<bool> body_has_face(BodyId body_id, FaceId face_id) const;
    Result<bool> shell_has_face(ShellId shell_id, FaceId face_id) const;
    Result<bool> face_has_loop(FaceId face_id, LoopId loop_id) const;
    Result<bool> loop_has_edge(LoopId loop_id, EdgeId edge_id) const;
    Result<bool> edge_has_vertex(EdgeId edge_id, VertexId vertex_id) const;
    Result<std::uint64_t> shared_face_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> shared_edge_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> boundary_edge_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> non_manifold_edge_count_of_body(BodyId body_id) const;
    Result<bool> is_body_topology_empty(BodyId body_id) const;
    Result<PCurveId> pcurve_of_coedge(CoedgeId coedge_id) const;
    /// Trim bridge：外环每条 coedge 均须绑定有效 PCurve；在 PCurve 控制点（折线顶点）上求 UV 轴对齐包围盒。
    /// 语义对齐 `validate_face_trim_consistency` 所用 UV（与 `SurfaceService::closest_uv(face_surface, …)` 同参空间）。
    Result<Range2D> face_outer_loop_uv_bounds(FaceId face_id) const;
    /// 外环 PCurve（折线）按 coedge 顺序串联的 UV 折线（闭合环，已去重相邻重复点）；供 `make_trimmed_polygon`。
    Result<std::vector<Point2>> face_outer_loop_uv_polyline(FaceId face_id) const;
    /// 返回该面引用曲面的「修剪基曲面」：`Trimmed` 则沿 `base_surface_id` 解引用直至非 Trimmed。
    Result<SurfaceId> underlying_surface_for_face_trim(FaceId face_id) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class TopologyTransaction {
public:
    explicit TopologyTransaction(std::shared_ptr<detail::KernelState> state);
    TopologyTransaction(TopologyTransaction&&) noexcept = default;
    TopologyTransaction& operator=(TopologyTransaction&&) noexcept = default;
    TopologyTransaction(const TopologyTransaction&) = delete;
    TopologyTransaction& operator=(const TopologyTransaction&) = delete;
    ~TopologyTransaction() = default;

    Result<VertexId> create_vertex(const Point3& point);
    Result<EdgeId> create_edge(CurveId curve_id, VertexId v0, VertexId v1);
    Result<CoedgeId> create_coedge(EdgeId edge_id, bool reversed);
    Result<void> set_coedge_pcurve(CoedgeId coedge_id, PCurveId pcurve_id);
    Result<LoopId> create_loop(std::span<const CoedgeId> coedges);
    Result<FaceId> create_face(SurfaceId surface_id, LoopId outer_loop, std::span<const LoopId> inner_loops);
    Result<ShellId> create_shell(std::span<const FaceId> faces);
    Result<BodyId> create_body(std::span<const ShellId> shells);
    Result<void> delete_face(FaceId face_id);
    Result<void> delete_shell(ShellId shell_id);
    Result<void> delete_body(BodyId body_id);
    Result<void> replace_surface(FaceId face_id, SurfaceId replacement);
    Result<VersionId> commit();
    Result<void> rollback();
    Result<bool> is_active() const;
    Result<std::uint64_t> created_vertex_count() const;
    Result<std::uint64_t> created_edge_count() const;
    Result<std::uint64_t> created_coedge_count() const;
    Result<std::uint64_t> created_loop_count() const;
    Result<std::uint64_t> created_face_count() const;
    Result<std::uint64_t> created_shell_count() const;
    Result<std::uint64_t> created_body_count() const;
    Result<std::uint64_t> created_entity_count_total() const;
    Result<std::uint64_t> touched_face_count() const;
    Result<std::uint64_t> touched_shell_count() const;
    Result<std::uint64_t> touched_body_count() const;
    // Destructive ops in this transaction (for audit/metrics). Reset on rollback and clear_tracking_records.
    Result<std::uint64_t> deleted_face_count() const;
    Result<std::uint64_t> deleted_shell_count() const;
    Result<std::uint64_t> deleted_body_count() const;
    /// Successful `replace_surface` calls in this transaction (audit/metrics). Reset on rollback / clear_tracking_records.
    Result<std::uint64_t> replaced_surface_count() const;
    /// Successful `set_coedge_pcurve` with non-zero `PCurveId` (trim bridge bind). Reset on rollback / clear_tracking_records.
    Result<std::uint64_t> coedge_pcurve_bind_count() const;
    /// Successful `set_coedge_pcurve` to `PCurveId{}` after a non-zero binding was present (trim bridge clear). Reset on rollback / clear_tracking_records.
    Result<std::uint64_t> coedge_pcurve_clear_count() const;
    /// 本事务内每次**成功**的拓扑写操作各计 1（创建/删除实体、`replace_surface`、每次 `set_coedge_pcurve` 含清除）。提交后仍可查询；回滚或 `clear_tracking_records` 归零。
    Result<std::uint64_t> write_operation_count() const;
    /// 报告当前实现的有效隔离级别（见 `TopologyIsolationLevel` 注释）。
    Result<TopologyIsolationLevel> effective_isolation_level() const;
    Result<bool> has_created_vertex(VertexId vertex_id) const;
    Result<bool> has_created_edge(EdgeId edge_id) const;
    Result<bool> has_created_coedge(CoedgeId coedge_id) const;
    Result<bool> has_created_loop(LoopId loop_id) const;
    Result<bool> has_created_face(FaceId face_id) const;
    Result<bool> has_created_shell(ShellId shell_id) const;
    Result<bool> has_created_body(BodyId body_id) const;
    Result<bool> can_commit() const;
    Result<VersionId> preview_commit_version() const;
    Result<bool> has_snapshot_face(FaceId face_id) const;
    Result<bool> has_snapshot_shell(ShellId shell_id) const;
    Result<bool> has_snapshot_body(BodyId body_id) const;
    Result<void> clear_tracking_records();
    Result<std::vector<VertexId>> created_vertices() const;
    Result<std::vector<EdgeId>> created_edges() const;
    Result<std::vector<CoedgeId>> created_coedges() const;
    Result<std::vector<LoopId>> created_loops() const;
    Result<std::vector<FaceId>> created_faces() const;
    Result<std::vector<ShellId>> created_shells() const;
    Result<std::vector<BodyId>> created_bodies() const;

private:
    std::shared_ptr<detail::KernelState> state_;
    std::shared_ptr<detail::TopologyTransactionState> transaction_state_;
    std::vector<std::uint64_t> created_vertices_;
    std::vector<std::uint64_t> created_edges_;
    std::vector<std::uint64_t> created_coedges_;
    std::vector<std::uint64_t> created_loops_;
    std::vector<std::uint64_t> created_faces_;
    std::vector<std::uint64_t> created_shells_;
    std::vector<std::uint64_t> created_bodies_;
    std::uint64_t txn_deleted_faces_{0};
    std::uint64_t txn_deleted_shells_{0};
    std::uint64_t txn_deleted_bodies_{0};
    std::uint64_t txn_replaced_surfaces_{0};
    std::uint64_t txn_coedge_pcurve_binds_{0};
    std::uint64_t txn_coedge_pcurve_clears_{0};
    std::uint64_t txn_write_ops_{0};
    bool active_ {true};
};

class TopologyValidationService {
public:
    explicit TopologyValidationService(std::shared_ptr<detail::KernelState> state);

    Result<void> validate_edge(EdgeId edge_id) const;
    Result<void> validate_vertex(VertexId vertex_id) const;
    Result<void> validate_coedge(CoedgeId coedge_id) const;
    Result<void> validate_loop(LoopId loop_id) const;
    // Trim bridge (Stage 2): validate that coedge pcurves in a loop are continuous and closed in UV space.
    // Requires every coedge in the loop to have a valid non-zero PCurveId.
    Result<void> validate_loop_pcurve_closedness(LoopId loop_id) const;
    // Trim bridge (Stage 2): face-level trim consistency.
    // Requires outer/inner loops to have complete coedge pcurves and each loop is UV-closed.
    Result<void> validate_face_trim_consistency(FaceId face_id) const;
    // Trim bridge (batch): validate trim for every face in a shell / body's owned shells.
    Result<void> validate_shell_trim_consistency(ShellId shell_id) const;
    Result<void> validate_body_trim_consistency(BodyId body_id) const;
    Result<void> validate_face(FaceId face_id) const;
    Result<void> validate_face_sources(FaceId face_id) const;
    Result<void> validate_shell(ShellId shell_id) const;
    // Strict closedness validation:
    // - kTopoOpenBoundary when any edge is used < 2 times within the shell
    // - kTopoNonManifoldEdge when any edge is used > 2 times within the shell
    Result<void> validate_shell_closedness(ShellId shell_id) const;
    Result<void> validate_shell_sources(ShellId shell_id) const;
    Result<void> validate_body(BodyId body_id) const;
    Result<void> validate_body_closedness(BodyId body_id) const;
    Result<void> validate_body_sources(BodyId body_id) const;
    Result<void> validate_body_bbox(BodyId body_id) const;
    /// Per-body index checks (coedge_to_loop / loop_to_faces / edge_to_coedges / face_to_shells) for topology reachable from the body's shells only.
    Result<void> validate_body_topology_indices(BodyId body_id) const;
    Result<void> validate_indices_consistency() const;
    Result<void> validate_face_many(std::span<const FaceId> face_ids) const;
    Result<void> validate_shell_many(std::span<const ShellId> shell_ids) const;
    Result<void> validate_body_many(std::span<const BodyId> body_ids) const;
    Result<bool> is_face_valid(FaceId face_id) const;
    Result<bool> is_shell_valid(ShellId shell_id) const;
    Result<bool> is_body_valid(BodyId body_id) const;
    Result<std::uint64_t> count_invalid_faces(std::span<const FaceId> face_ids) const;
    Result<std::uint64_t> count_invalid_shells(std::span<const ShellId> shell_ids) const;
    Result<std::uint64_t> count_invalid_bodies(std::span<const BodyId> body_ids) const;
    Result<FaceId> first_invalid_face(std::span<const FaceId> face_ids) const;
    Result<ShellId> first_invalid_shell(std::span<const ShellId> shell_ids) const;
    Result<BodyId> first_invalid_body(std::span<const BodyId> body_ids) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class TopologyService {
public:
    explicit TopologyService(std::shared_ptr<detail::KernelState> state);

    TopologyTransaction begin_transaction();
    TopologyQueryService& query();
    TopologyValidationService& validate();

private:
    std::shared_ptr<detail::KernelState> state_;
    TopologyQueryService query_service_;
    TopologyValidationService validation_service_;
};

}  // namespace axiom
