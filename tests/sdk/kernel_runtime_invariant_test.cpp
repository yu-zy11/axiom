#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "axiom/core/types.h"
#include "axiom/sdk/kernel.h"

namespace {

bool tess_metrics_empty(const axiom::TessellationCacheStats& s) {
    return s.body_cache_hits == 0 && s.body_cache_misses == 0 && s.body_cache_stale_evictions == 0 &&
           s.face_cache_hits == 0 && s.face_cache_misses == 0 && s.face_cache_stale_evictions == 0;
}

bool all_runtime_stores_empty(const axiom::KernelRuntimeStoreCounts& c) {
    return c.mesh_records == 0 && c.tessellation_cache_entries == 0 && c.face_tessellation_cache_entries == 0 &&
           c.intersection_records == 0 && c.curve_eval_cache_entries == 0 && c.surface_eval_cache_entries == 0 &&
           c.eval_node_records == 0 && c.diagnostic_reports == 0 && c.topology_query_op_count == 0 &&
           tess_metrics_empty(c.tessellation_metrics);
}

bool topology_write_breakdown_zero(const axiom::TopologyCommitWriteBreakdown& b) {
    return b.created_vertices == 0 && b.created_edges == 0 && b.created_coedges == 0 && b.created_loops == 0 &&
           b.created_faces == 0 && b.created_shells == 0 && b.created_bodies == 0 && b.deleted_faces == 0 &&
           b.deleted_shells == 0 && b.deleted_bodies == 0 && b.replaced_surfaces == 0 &&
           b.coedge_pcurve_binds == 0 && b.coedge_pcurve_clears == 0;
}

bool topology_write_breakdown_equal(const axiom::TopologyCommitWriteBreakdown& a,
                                    const axiom::TopologyCommitWriteBreakdown& b) {
    return a.created_vertices == b.created_vertices && a.created_edges == b.created_edges &&
           a.created_coedges == b.created_coedges && a.created_loops == b.created_loops &&
           a.created_faces == b.created_faces && a.created_shells == b.created_shells &&
           a.created_bodies == b.created_bodies && a.deleted_faces == b.deleted_faces &&
           a.deleted_shells == b.deleted_shells && a.deleted_bodies == b.deleted_bodies &&
           a.replaced_surfaces == b.replaced_surfaces && a.coedge_pcurve_binds == b.coedge_pcurve_binds &&
           a.coedge_pcurve_clears == b.coedge_pcurve_clears;
}

}  // namespace

int main() {
    axiom::Kernel kernel;
    if (kernel.set_enable_diagnostics(true).status != axiom::StatusCode::Ok) {
        std::cerr << "failed to enable diagnostics\n";
        return 1;
    }

    const auto lin_before = kernel.linear_tolerance();
    if (lin_before.status != axiom::StatusCode::Ok || !lin_before.value.has_value() || *lin_before.value <= 0.0) {
        std::cerr << "unexpected default linear tolerance\n";
        return 1;
    }
    if (kernel.set_linear_tolerance(-1.0).status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected invalid linear tolerance rejection\n";
        return 1;
    }
    if (kernel.set_linear_tolerance(std::nan("")).status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected non-finite linear tolerance rejection\n";
        return 1;
    }
    const auto lin_after_bad = kernel.linear_tolerance();
    if (lin_after_bad.status != axiom::StatusCode::Ok || !lin_after_bad.value.has_value() ||
        *lin_after_bad.value != *lin_before.value) {
        std::cerr << "invalid tolerance write must not change stored value\n";
        return 1;
    }

    const auto ang_before = kernel.angular_tolerance();
    if (ang_before.status != axiom::StatusCode::Ok || !ang_before.value.has_value()) {
        std::cerr << "unexpected angular tolerance query\n";
        return 1;
    }
    if (kernel.set_angular_tolerance(std::numeric_limits<double>::infinity()).status !=
        axiom::StatusCode::InvalidInput) {
        std::cerr << "expected non-finite angular tolerance rejection\n";
        return 1;
    }
    const auto ang_after_bad = kernel.angular_tolerance();
    if (ang_after_bad.status != axiom::StatusCode::Ok || !ang_after_bad.value.has_value() ||
        *ang_after_bad.value != *ang_before.value) {
        std::cerr << "invalid angular tolerance write must not change stored value\n";
        return 1;
    }

    if (kernel.set_precision_mode(axiom::PrecisionMode::ExactCritical).status != axiom::StatusCode::Ok) {
        std::cerr << "set_precision_mode failed\n";
        return 1;
    }
    const auto pm = kernel.precision_mode();
    if (pm.status != axiom::StatusCode::Ok || !pm.value.has_value() ||
        *pm.value != axiom::PrecisionMode::ExactCritical) {
        std::cerr << "precision_mode query mismatch\n";
        return 1;
    }
    if (kernel.set_enable_cache(false).status != axiom::StatusCode::Ok) {
        std::cerr << "set_enable_cache failed\n";
        return 1;
    }
    const auto ec = kernel.enable_cache();
    if (ec.status != axiom::StatusCode::Ok || !ec.value.has_value() || *ec.value) {
        std::cerr << "enable_cache should be false\n";
        return 1;
    }
    if (kernel.set_enable_cache(true).status != axiom::StatusCode::Ok) {
        std::cerr << "set_enable_cache restore failed\n";
        return 1;
    }

    if (kernel.export_runtime_observability_json("").status != axiom::StatusCode::InvalidInput) {
        std::cerr << "export_runtime_observability_json must reject empty path\n";
        return 1;
    }

    {
        auto kcw = kernel.kernel_config_numeric_wellformed();
        if (kcw.status != axiom::StatusCode::Ok || !kcw.value.has_value() || !*kcw.value) {
            std::cerr << "kernel_config_numeric_wellformed should pass on defaults\n";
            return 1;
        }
        auto tva0 = kernel.topology_version_audit_consistent();
        if (tva0.status != axiom::StatusCode::Ok || !tva0.value.has_value() || !*tva0.value) {
            std::cerr << "topology_version_audit_consistent should pass before any commit\n";
            return 1;
        }
        auto egmap0 = kernel.eval_graph_store_maps_consistent();
        if (egmap0.status != axiom::StatusCode::Ok || !egmap0.value.has_value() || !*egmap0.value) {
            std::cerr << "eval_graph_store_maps_consistent should pass on empty eval graph\n";
            return 1;
        }
        auto cri0 = kernel.core_runtime_invariants_hold();
        if (cri0.status != axiom::StatusCode::Ok || !cri0.value.has_value() || !*cri0.value) {
            std::cerr << "core_runtime_invariants_hold should pass on fresh kernel\n";
            return 1;
        }
    }

    auto v0 = kernel.topology_version_next();
    if (v0.status != axiom::StatusCode::Ok || !v0.value.has_value() || *v0.value == 0) {
        std::cerr << "topology_version_next unexpected\n";
        return 1;
    }
    {
        auto txn = kernel.topology().begin_transaction();
        auto committed = txn.commit();
        if (committed.status != axiom::StatusCode::Ok || !committed.value.has_value()) {
            std::cerr << "empty topology commit failed\n";
            return 1;
        }
    }
    auto v1 = kernel.topology_version_next();
    if (v1.status != axiom::StatusCode::Ok || !v1.value.has_value() || *v1.value != *v0.value + 1) {
        std::cerr << "topology_version_next should increment after commit\n";
        return 1;
    }

    {
        auto audit = kernel.topology_commit_audit();
        if (audit.status != axiom::StatusCode::Ok || !audit.value.has_value()) {
            std::cerr << "topology_commit_audit failed after empty commit\n";
            return 1;
        }
        const auto& a = *audit.value;
        if (a.committed_transaction_count != 1 || a.committed_write_operations_total != 0 ||
            a.last_committed_version != *v0.value || a.last_commit_write_operations != 0) {
            std::cerr << "topology_commit_audit mismatch for empty commit\n";
            return 1;
        }
        if (!topology_write_breakdown_zero(a.last_commit_write_breakdown) ||
            !topology_write_breakdown_zero(a.cumulative_commit_write_breakdown)) {
            std::cerr << "empty commit must leave write breakdown counters at zero\n";
            return 1;
        }
        auto tva1 = kernel.topology_version_audit_consistent();
        if (tva1.status != axiom::StatusCode::Ok || !tva1.value.has_value() || !*tva1.value) {
            std::cerr << "topology_version_audit_consistent should pass after empty commit\n";
            return 1;
        }
        auto cri1 = kernel.core_runtime_invariants_hold();
        if (cri1.status != axiom::StatusCode::Ok || !cri1.value.has_value() || !*cri1.value) {
            std::cerr << "core_runtime_invariants_hold should pass after empty commit\n";
            return 1;
        }
    }

    std::uint64_t cum_vertices_before_vertex_txn = 0;
    {
        auto au = kernel.topology_commit_audit();
        if (au.status != axiom::StatusCode::Ok || !au.value.has_value()) {
            std::cerr << "topology_commit_audit before vertex txn failed\n";
            return 1;
        }
        cum_vertices_before_vertex_txn = au.value->cumulative_commit_write_breakdown.created_vertices;
    }
    {
        auto txn_v = kernel.topology().begin_transaction();
        auto vx = txn_v.create_vertex({0.25, 0.5, 0.75});
        if (vx.status != axiom::StatusCode::Ok || !vx.value.has_value()) {
            std::cerr << "create_vertex in audit test failed\n";
            return 1;
        }
        if (txn_v.commit().status != axiom::StatusCode::Ok) {
            std::cerr << "commit vertex txn failed\n";
            return 1;
        }
        auto au2 = kernel.topology_commit_audit();
        if (au2.status != axiom::StatusCode::Ok || !au2.value.has_value()) {
            std::cerr << "topology_commit_audit after vertex txn failed\n";
            return 1;
        }
        const auto& a2 = *au2.value;
        if (a2.last_commit_write_breakdown.created_vertices != 1 || a2.last_commit_write_operations != 1) {
            std::cerr << "vertex txn last_commit breakdown mismatch\n";
            return 1;
        }
        if (a2.cumulative_commit_write_breakdown.created_vertices != cum_vertices_before_vertex_txn + 1) {
            std::cerr << "vertex txn cumulative created_vertices mismatch\n";
            return 1;
        }
    }

    auto box = kernel.primitives().box({0.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
    if (box.status != axiom::StatusCode::Ok || !box.value.has_value()) {
        std::cerr << "box creation failed\n";
        return 1;
    }

    axiom::TessellationOptions tess;
    tess.chordal_error = 0.2;
    tess.angular_error = 10.0;
    auto mesh_r = kernel.convert().brep_to_mesh(*box.value, tess);
    if (mesh_r.status != axiom::StatusCode::Ok || !mesh_r.value.has_value()) {
        std::cerr << "brep_to_mesh failed\n";
        return 1;
    }

    auto tcs = kernel.tessellation_cache_stats();
    if (tcs.status != axiom::StatusCode::Ok || !tcs.value.has_value() ||
        tcs.value->body_cache_misses == 0) {
        std::cerr << "expected body tessellation cache miss on first brep_to_mesh\n";
        return 1;
    }

    auto mesh_ok = kernel.runtime_tessellation_caches_consistent();
    if (mesh_ok.status != axiom::StatusCode::Ok || !mesh_ok.value.has_value() || !*mesh_ok.value) {
        std::cerr << "tessellation caches should be consistent with mesh store after brep_to_mesh\n";
        return 1;
    }

    auto prune0 = kernel.prune_stale_tessellation_cache_entries();
    if (prune0.status != axiom::StatusCode::Ok || !prune0.value.has_value() || *prune0.value != 0) {
        std::cerr << "prune_stale_tessellation_cache_entries should remove nothing when consistent\n";
        return 1;
    }

    {
        auto tmp = kernel.io().temp_path_for("axiom_tess_stats", ".json");
        if (tmp.status != axiom::StatusCode::Ok || !tmp.value.has_value()) {
            std::cerr << "temp_path_for tess stats failed\n";
            return 1;
        }
        if (kernel.export_tessellation_cache_stats_json(*tmp.value).status != axiom::StatusCode::Ok) {
            std::cerr << "export_tessellation_cache_stats_json failed\n";
            return 1;
        }
        std::ifstream in(*tmp.value);
        std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (json.find("\"body_cache_misses\"") == std::string::npos) {
            std::cerr << "exported tessellation stats json missing body_cache_misses\n";
            return 1;
        }
        std::filesystem::remove(*tmp.value);
    }

    auto node = kernel.eval_graph().register_node(axiom::NodeKind::Analysis, "inv_test");
    if (node.status != axiom::StatusCode::Ok || !node.value.has_value()) {
        std::cerr << "eval node register failed\n";
        return 1;
    }

    {
        auto eg = kernel.eval_graph_metrics();
        if (eg.status != axiom::StatusCode::Ok || !eg.value.has_value()) {
            std::cerr << "eval_graph_metrics failed\n";
            return 1;
        }
        const auto& g = *eg.value;
        if (g.node_count != 1 || g.invalid_node_count != 0 || g.recompute_events_total != 0 ||
            g.max_per_node_recompute_count != 0 || g.nodes_with_recompute_nonzero != 0 ||
            std::abs(g.mean_recompute_events_per_node) > 1e-12 ||
            g.dependency_from_node_count != 0 || g.dependency_edge_count != 0 ||
            g.body_binding_record_count != 0 || g.invalidation_bridge.for_body_entries != 0 ||
            g.invalidation_bridge.for_faces_entries != 0 || g.invalidation_bridge.for_bodies_batches != 0 ||
            g.invalidation_bridge.for_bodies_list_size_total != 0 ||
            g.invalidation_bridge.downstream_invalidation_steps != 0) {
            std::cerr << "eval_graph_metrics unexpected after register_node\n";
            return 1;
        }
        if (kernel.eval_graph().invalidate(*node.value).status != axiom::StatusCode::Ok) {
            std::cerr << "eval invalidate failed\n";
            return 1;
        }
        auto eg_inv = kernel.eval_graph_metrics();
        if (eg_inv.status != axiom::StatusCode::Ok || !eg_inv.value.has_value() ||
            eg_inv.value->invalid_node_count != 1 ||
            eg_inv.value->telemetry.invalidate_node_calls != 1 ||
            eg_inv.value->invalidation_bridge.for_body_entries != 0) {
            std::cerr << "eval_graph_metrics invalid_node_count after invalidate\n";
            return 1;
        }
        if (kernel.eval_graph().recompute(*node.value).status != axiom::StatusCode::Ok) {
            std::cerr << "eval recompute failed\n";
            return 1;
        }
        auto eg_done = kernel.eval_graph_metrics();
        auto trc = kernel.eval_graph().total_recompute_count();
        if (eg_done.status != axiom::StatusCode::Ok || !eg_done.value.has_value() ||
            trc.status != axiom::StatusCode::Ok || !trc.value.has_value()) {
            std::cerr << "eval_graph_metrics or total_recompute_count failed\n";
            return 1;
        }
        if (eg_done.value->recompute_events_total != *trc.value || *trc.value < 1 ||
            eg_done.value->invalid_node_count != 0 ||
            eg_done.value->max_per_node_recompute_count < 1 ||
            eg_done.value->nodes_with_recompute_nonzero < 1 ||
            std::abs(eg_done.value->mean_recompute_events_per_node -
                     static_cast<double>(eg_done.value->recompute_events_total) /
                         static_cast<double>(eg_done.value->node_count)) > 1e-9 ||
            std::abs(eg_done.value->mean_recompute_events_per_touched_node -
                     static_cast<double>(eg_done.value->recompute_events_total) /
                         static_cast<double>(eg_done.value->nodes_with_recompute_nonzero)) > 1e-9 ||
            eg_done.value->telemetry.recompute_finish_events != 1 ||
            eg_done.value->telemetry.recompute_single_root_max_finish_nodes != 1 ||
            eg_done.value->telemetry.recompute_single_root_max_stack_depth != 1 ||
            eg_done.value->invalidation_bridge.for_body_entries != 0) {
            std::cerr << "eval_graph_metrics must align with recompute/invalidate semantics\n";
            return 1;
        }
    }

    {
        auto n2 = kernel.eval_graph().register_node(axiom::NodeKind::Analysis, "dep_tail");
        if (n2.status != axiom::StatusCode::Ok || !n2.value.has_value()) {
            std::cerr << "second eval node register failed\n";
            return 1;
        }
        if (kernel.eval_graph().add_dependency(*n2.value, *node.value).status != axiom::StatusCode::Ok) {
            std::cerr << "eval add_dependency failed\n";
            return 1;
        }
        auto eg_dep = kernel.eval_graph_metrics();
        if (eg_dep.status != axiom::StatusCode::Ok || !eg_dep.value.has_value() ||
            eg_dep.value->dependency_from_node_count != 1 || eg_dep.value->dependency_edge_count != 1) {
            std::cerr << "eval_graph_metrics dependency counts mismatch\n";
            return 1;
        }
        const std::string body_lbl = std::string("body:") + std::to_string(box.value->value);
        auto nb = kernel.eval_graph().register_node(axiom::NodeKind::Analysis, body_lbl);
        if (nb.status != axiom::StatusCode::Ok || !nb.value.has_value()) {
            std::cerr << "body-bound eval node register failed\n";
            return 1;
        }
        auto eg_bind = kernel.eval_graph_metrics();
        if (eg_bind.status != axiom::StatusCode::Ok || !eg_bind.value.has_value() ||
            eg_bind.value->body_binding_record_count != 1 || eg_bind.value->body_binding_reference_total != 1 ||
            eg_bind.value->invalidation_bridge.for_body_entries != 0) {
            std::cerr << "eval_graph_metrics body binding counts mismatch\n";
            return 1;
        }
        if (kernel.eval_graph().invalidate_body(*box.value).status != axiom::StatusCode::Ok) {
            std::cerr << "eval invalidate_body failed\n";
            return 1;
        }
        auto eg_ib = kernel.eval_graph_metrics();
        if (eg_ib.status != axiom::StatusCode::Ok || !eg_ib.value.has_value() ||
            eg_ib.value->telemetry.invalidate_body_calls != 1 ||
            eg_ib.value->invalidation_bridge.for_body_entries != 1 ||
            eg_ib.value->invalidation_bridge.downstream_invalidation_steps != 2) {
            // 1 步来自此前对 `node` 的 `invalidate`；另 1 步来自体绑定节点 `nb` 的下游传播入口。
            std::cerr << "eval_graph_metrics bridge must reflect invalidate_eval_for_body\n";
            return 1;
        }
        const std::vector<axiom::NodeId> batch = {*node.value, *n2.value, *nb.value};
        if (kernel.eval_graph().invalidate_many(batch).status != axiom::StatusCode::Ok) {
            std::cerr << "eval invalidate_many failed\n";
            return 1;
        }
        auto eg_batch = kernel.eval_graph_metrics();
        if (eg_batch.status != axiom::StatusCode::Ok || !eg_batch.value.has_value() ||
            eg_batch.value->invalid_node_count != 3 ||
            eg_batch.value->telemetry.invalidate_many_batches != 1 ||
            eg_batch.value->telemetry.invalidate_many_node_total != 3) {
            std::cerr << "eval_graph_metrics invalidate_many telemetry mismatch\n";
            return 1;
        }
        if (kernel.eval_graph().recompute_many(batch).status != axiom::StatusCode::Ok) {
            std::cerr << "eval recompute_many failed\n";
            return 1;
        }
        auto eg_rebatch = kernel.eval_graph_metrics();
        if (eg_rebatch.status != axiom::StatusCode::Ok || !eg_rebatch.value.has_value() ||
            eg_rebatch.value->telemetry.recompute_many_batches != 1 ||
            eg_rebatch.value->telemetry.recompute_many_root_total != 3) {
            std::cerr << "eval_graph_metrics recompute_many telemetry mismatch\n";
            return 1;
        }
        auto egmap_busy = kernel.eval_graph_store_maps_consistent();
        if (egmap_busy.status != axiom::StatusCode::Ok || !egmap_busy.value.has_value() ||
            !*egmap_busy.value) {
            std::cerr << "eval_graph_store_maps_consistent must pass after eval graph operations\n";
            return 1;
        }
    }

    {
        auto tmp_a = kernel.io().temp_path_for("axiom_topo_audit", ".json");
        auto tmp_e = kernel.io().temp_path_for("axiom_eval_metrics", ".json");
        if (tmp_a.status != axiom::StatusCode::Ok || !tmp_a.value.has_value() || tmp_e.status != axiom::StatusCode::Ok ||
            !tmp_e.value.has_value()) {
            std::cerr << "temp_path_for observability export failed\n";
            return 1;
        }
        if (kernel.export_topology_commit_audit_json(*tmp_a.value).status != axiom::StatusCode::Ok ||
            kernel.export_eval_graph_metrics_json(*tmp_e.value).status != axiom::StatusCode::Ok) {
            std::cerr << "export topology/eval observability json failed\n";
            return 1;
        }
        std::ifstream in_a(*tmp_a.value);
        std::string ja((std::istreambuf_iterator<char>(in_a)), std::istreambuf_iterator<char>());
        if (ja.find("\"committed_transaction_count\"") == std::string::npos ||
            ja.find("\"cumulative_commit_write_breakdown\"") == std::string::npos) {
            std::cerr << "topology commit audit json missing expected keys\n";
            return 1;
        }
        std::ifstream in_e(*tmp_e.value);
        std::string je((std::istreambuf_iterator<char>(in_e)), std::istreambuf_iterator<char>());
        if (je.find("\"body_binding_reference_total\"") == std::string::npos ||
            je.find("\"max_per_node_recompute_count\"") == std::string::npos ||
            je.find("\"mean_recompute_events_per_node\"") == std::string::npos ||
            je.find("\"mean_recompute_events_per_touched_node\"") == std::string::npos ||
            je.find("\"invalidate_many_batches\"") == std::string::npos ||
            je.find("\"recompute_many_batches\"") == std::string::npos ||
            je.find("\"invalidate_node_redundant_calls\"") == std::string::npos ||
            je.find("\"recompute_root_already_valid_calls\"") == std::string::npos ||
            je.find("\"eval_graph_state_read_calls\"") == std::string::npos ||
            je.find("\"invalidation_bridge\"") == std::string::npos ||
            je.find("\"for_body_entries\"") == std::string::npos ||
            je.find("\"downstream_invalidation_steps\"") == std::string::npos) {
            std::cerr << "eval graph metrics json missing expected keys\n";
            return 1;
        }
        std::filesystem::remove(*tmp_a.value);
        std::filesystem::remove(*tmp_e.value);
    }

    {
        auto tmp_o = kernel.io().temp_path_for("axiom_runtime_obs", ".json");
        if (tmp_o.status != axiom::StatusCode::Ok || !tmp_o.value.has_value()) {
            std::cerr << "temp_path_for runtime observability bundle failed\n";
            return 1;
        }
        if (kernel.export_runtime_observability_json(*tmp_o.value).status != axiom::StatusCode::Ok) {
            std::cerr << "export_runtime_observability_json failed\n";
            return 1;
        }
        std::ifstream in_o(*tmp_o.value);
        std::string jo((std::istreambuf_iterator<char>(in_o)), std::istreambuf_iterator<char>());
        if (jo.find("\"topology_version_next\"") == std::string::npos ||
            jo.find("\"topology_commit_audit\"") == std::string::npos ||
            jo.find("\"eval_graph_metrics\"") == std::string::npos ||
            jo.find("\"max_per_node_recompute_count\"") == std::string::npos ||
            jo.find("\"mean_recompute_events_per_node\"") == std::string::npos ||
            jo.find("\"mean_recompute_events_per_touched_node\"") == std::string::npos ||
            jo.find("\"runtime_store_counts\"") == std::string::npos ||
            jo.find("\"tessellation_metrics\"") == std::string::npos ||
            jo.find("\"rep_stage_snapshot\"") == std::string::npos ||
            jo.find("\"default_tessellation_options\"") == std::string::npos ||
            jo.find("\"default_conversion_error_budget\"") == std::string::npos ||
            jo.find("\"derivation\":\"tessellation_options_v1\"") == std::string::npos) {
            std::cerr << "runtime observability bundle json missing expected sections\n";
            return 1;
        }
        std::filesystem::remove(*tmp_o.value);
    }

    {
        auto cap = kernel.capability_report_lines();
        if (cap.status != axiom::StatusCode::Ok || !cap.value.has_value()) {
            std::cerr << "capability_report_lines failed\n";
            return 1;
        }
        bool saw_bind_refs = false;
        bool saw_inv_many = false;
        bool saw_bridge = false;
        bool saw_downstream = false;
        bool saw_bundle_export = false;
        bool saw_store_maps_invariant = false;
        bool saw_mean_recompute_touched = false;
        for (const auto& ln : *cap.value) {
            if (ln.rfind("Core.EvalGraph.MeanRecomputePerTouchedNode=", 0) == 0) {
                saw_mean_recompute_touched = true;
            }
            if (ln.rfind("Core.EvalGraph.BodyBindingRefs=", 0) == 0) {
                saw_bind_refs = true;
            }
            if (ln.rfind("Core.EvalGraph.Telemetry.InvalidateManyBatches=", 0) == 0) {
                saw_inv_many = true;
            }
            if (ln.rfind("Core.EvalGraph.Bridge.ForBodyEntries=", 0) == 0) {
                saw_bridge = true;
            }
            if (ln.rfind("Core.EvalGraph.Bridge.DownstreamInvalidationSteps=", 0) == 0) {
                saw_downstream = true;
            }
            if (ln == "Core.Export.RuntimeObservabilityJson=1") {
                saw_bundle_export = true;
            }
            if (ln == "Core.EvalGraph.StoreMapsCheckedByCoreInvariants=1") {
                saw_store_maps_invariant = true;
            }
        }
        if (!saw_bind_refs || !saw_inv_many || !saw_bridge || !saw_downstream || !saw_bundle_export ||
            !saw_store_maps_invariant || !saw_mean_recompute_touched) {
            std::cerr << "capability report missing Core.EvalGraph or Core.Export lines\n";
            return 1;
        }
    }

    auto summ = kernel.topology().query().summary_of_body(*box.value);
    if (summ.status != axiom::StatusCode::Ok || !summ.value.has_value()) {
        std::cerr << "topology summary failed\n";
        return 1;
    }
    (void)summ;

    auto line = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    if (line.status != axiom::StatusCode::Ok || !line.value.has_value()) {
        std::cerr << "line creation failed\n";
        return 1;
    }
    auto ev = kernel.curve_service().eval(*line.value, 0.25, 0);
    if (ev.status != axiom::StatusCode::Ok || !ev.value.has_value()) {
        std::cerr << "curve eval failed\n";
        return 1;
    }
    (void)ev;

    auto snap = kernel.runtime_store_counts();
    if (snap.status != axiom::StatusCode::Ok || !snap.value.has_value()) {
        std::cerr << "runtime_store_counts failed\n";
        return 1;
    }
    const auto& s0 = *snap.value;
    if (s0.mesh_records == 0 || s0.tessellation_cache_entries == 0 || s0.eval_node_records == 0 ||
        s0.topology_query_op_count == 0 || s0.curve_eval_cache_entries == 0) {
        std::cerr << "expected non-empty runtime stores before reset\n";
        return 1;
    }
    if (s0.diagnostic_reports == 0) {
        std::cerr << "expected diagnostic reports after operations with diagnostics enabled\n";
        return 1;
    }
    if (s0.tessellation_metrics.body_cache_misses != tcs.value->body_cache_misses ||
        s0.tessellation_metrics.body_cache_hits != tcs.value->body_cache_hits) {
        std::cerr << "runtime_store_counts.tessellation_metrics must match tessellation_cache_stats\n";
        return 1;
    }

    {
        auto cri_pre_reset = kernel.core_runtime_invariants_hold();
        if (cri_pre_reset.status != axiom::StatusCode::Ok || !cri_pre_reset.value.has_value() ||
            !*cri_pre_reset.value) {
            std::cerr << "core_runtime_invariants_hold should pass before reset\n";
            return 1;
        }
    }

    auto topo_audit_before_reset = kernel.topology_commit_audit();
    if (topo_audit_before_reset.status != axiom::StatusCode::Ok || !topo_audit_before_reset.value.has_value()) {
        std::cerr << "topology_commit_audit before reset failed\n";
        return 1;
    }

    auto topology_next_before_reset = kernel.topology_version_next();
    if (topology_next_before_reset.status != axiom::StatusCode::Ok || !topology_next_before_reset.value.has_value()) {
        std::cerr << "topology_version_next before reset failed\n";
        return 1;
    }

    if (kernel.reset_runtime_stores().status != axiom::StatusCode::Ok) {
        std::cerr << "reset_runtime_stores failed\n";
        return 1;
    }

    auto snap1 = kernel.runtime_store_counts();
    if (snap1.status != axiom::StatusCode::Ok || !snap1.value.has_value()) {
        std::cerr << "runtime_store_counts after reset failed\n";
        return 1;
    }
    if (!all_runtime_stores_empty(*snap1.value)) {
        std::cerr << "reset_runtime_stores must empty all runtime stores in KernelRuntimeStoreCounts\n";
        return 1;
    }

    auto tcs1 = kernel.tessellation_cache_stats();
    if (tcs1.status != axiom::StatusCode::Ok || !tcs1.value.has_value() ||
        tcs1.value->body_cache_hits != 0 || tcs1.value->body_cache_misses != 0) {
        std::cerr << "reset must zero tessellation cache stats\n";
        return 1;
    }

    auto v_after_reset = kernel.topology_version_next();
    if (v_after_reset.status != axiom::StatusCode::Ok || !v_after_reset.value.has_value() ||
        *v_after_reset.value != *topology_next_before_reset.value) {
        std::cerr << "reset must not change topology version counter\n";
        return 1;
    }

    auto topo_audit_after_reset = kernel.topology_commit_audit();
    if (topo_audit_after_reset.status != axiom::StatusCode::Ok || !topo_audit_after_reset.value.has_value()) {
        std::cerr << "topology_commit_audit after reset failed\n";
        return 1;
    }
    const auto& ab = *topo_audit_before_reset.value;
    const auto& aa = *topo_audit_after_reset.value;
    if (ab.committed_transaction_count != aa.committed_transaction_count ||
        ab.committed_write_operations_total != aa.committed_write_operations_total ||
        ab.last_committed_version != aa.last_committed_version ||
        ab.last_commit_write_operations != aa.last_commit_write_operations ||
        !topology_write_breakdown_equal(ab.last_commit_write_breakdown, aa.last_commit_write_breakdown) ||
        !topology_write_breakdown_equal(ab.cumulative_commit_write_breakdown,
                                        aa.cumulative_commit_write_breakdown)) {
        std::cerr << "reset_runtime_stores must not alter topology commit audit fields\n";
        return 1;
    }

    auto eg_after = kernel.eval_graph_metrics();
    if (eg_after.status != axiom::StatusCode::Ok || !eg_after.value.has_value() ||
        eg_after.value->node_count != 0 || eg_after.value->invalid_node_count != 0 ||
        eg_after.value->recompute_events_total != 0 || eg_after.value->max_per_node_recompute_count != 0 ||
        eg_after.value->nodes_with_recompute_nonzero != 0 ||
        std::abs(eg_after.value->mean_recompute_events_per_node) > 1e-12 ||
        eg_after.value->dependency_from_node_count != 0 || eg_after.value->dependency_edge_count != 0 ||
        eg_after.value->body_binding_record_count != 0 || eg_after.value->body_binding_reference_total != 0 ||
        eg_after.value->telemetry.invalidate_node_calls != 0 ||
        eg_after.value->telemetry.invalidate_body_calls != 0 ||
        eg_after.value->telemetry.invalidate_many_batches != 0 ||
        eg_after.value->telemetry.invalidate_many_node_total != 0 ||
        eg_after.value->telemetry.recompute_many_batches != 0 ||
        eg_after.value->telemetry.recompute_many_root_total != 0 ||
        eg_after.value->telemetry.recompute_transitive_dedup_skips != 0 ||
        eg_after.value->telemetry.recompute_finish_events != 0 ||
        eg_after.value->telemetry.recompute_single_root_max_finish_nodes != 0 ||
        eg_after.value->telemetry.recompute_single_root_max_stack_depth != 0 ||
        eg_after.value->invalidation_bridge.for_body_entries != 0 ||
        eg_after.value->invalidation_bridge.for_faces_entries != 0 ||
        eg_after.value->invalidation_bridge.for_bodies_batches != 0 ||
        eg_after.value->invalidation_bridge.for_bodies_list_size_total != 0 ||
        eg_after.value->invalidation_bridge.downstream_invalidation_steps != 0) {
        std::cerr << "reset_runtime_stores must clear eval graph stores\n";
        return 1;
    }

    {
        auto cri_post_reset = kernel.core_runtime_invariants_hold();
        if (cri_post_reset.status != axiom::StatusCode::Ok || !cri_post_reset.value.has_value() ||
            !*cri_post_reset.value) {
            std::cerr << "core_runtime_invariants_hold should pass after reset\n";
            return 1;
        }
    }

    auto body_n = kernel.body_count();
    if (body_n.status != axiom::StatusCode::Ok || !body_n.value.has_value() || *body_n.value == 0) {
        std::cerr << "reset must not remove bodies\n";
        return 1;
    }

    const auto lin_final = kernel.linear_tolerance();
    if (lin_final.status != axiom::StatusCode::Ok || !lin_final.value.has_value() ||
        *lin_final.value != *lin_before.value) {
        std::cerr << "reset must not change kernel config tolerances\n";
        return 1;
    }

    return 0;
}
