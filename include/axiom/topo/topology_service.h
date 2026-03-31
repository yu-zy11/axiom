#pragma once

#include <memory>
#include <span>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
struct TopologyTransactionState;
}

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
