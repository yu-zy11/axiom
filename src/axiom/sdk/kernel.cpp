#include "axiom/sdk/kernel.h"

#include <memory>

#include "axiom/diag/error_codes.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/plugin/plugin_sdk_version.h"
#include "axiom/internal/sdk/kernel_plugin_helpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "axiom/internal/rep/representation_internal_utils.h"

namespace axiom {
using namespace kernel_plugin_helpers;

namespace {

const char* precision_mode_tag(PrecisionMode m) {
  switch (m) {
    case PrecisionMode::FastFloat:
      return "FastFloat";
    case PrecisionMode::AdaptiveCertified:
      return "AdaptiveCertified";
    case PrecisionMode::ExactCritical:
      return "ExactCritical";
    default:
      return "?";
  }
}

void append_topology_breakdown_json(std::ostream& o, const TopologyCommitWriteBreakdown& b) {
  o << "{\"created_vertices\":" << b.created_vertices << ",\"created_edges\":" << b.created_edges
    << ",\"created_coedges\":" << b.created_coedges << ",\"created_loops\":" << b.created_loops
    << ",\"created_faces\":" << b.created_faces << ",\"created_shells\":" << b.created_shells
    << ",\"created_bodies\":" << b.created_bodies << ",\"deleted_faces\":" << b.deleted_faces
    << ",\"deleted_shells\":" << b.deleted_shells << ",\"deleted_bodies\":" << b.deleted_bodies
    << ",\"replaced_surfaces\":" << b.replaced_surfaces << ",\"coedge_pcurve_binds\":"
    << b.coedge_pcurve_binds << ",\"coedge_pcurve_clears\":" << b.coedge_pcurve_clears << "}";
}

bool topology_commit_write_breakdown_is_zero(const TopologyCommitWriteBreakdown& b) {
  return b.created_vertices == 0 && b.created_edges == 0 && b.created_coedges == 0 && b.created_loops == 0 &&
         b.created_faces == 0 && b.created_shells == 0 && b.created_bodies == 0 && b.deleted_faces == 0 &&
         b.deleted_shells == 0 && b.deleted_bodies == 0 && b.replaced_surfaces == 0 &&
         b.coedge_pcurve_binds == 0 && b.coedge_pcurve_clears == 0;
}

bool kernel_topology_version_audit_consistent(const detail::KernelState& s) {
  if (s.topology_commit_success_count == 0) {
    return s.topology_last_committed_version == 0 && s.topology_last_commit_write_operations == 0 &&
           s.topology_committed_write_operations_total == 0 &&
           topology_commit_write_breakdown_is_zero(s.topology_last_commit_write_breakdown) &&
           topology_commit_write_breakdown_is_zero(s.topology_committed_write_breakdown_totals);
  }
  if (s.topology_last_committed_version == 0) {
    return false;
  }
  return s.topology_last_committed_version + 1 == s.next_version;
}

bool kernel_eval_graph_store_maps_consistent(const detail::KernelState& s) {
  const auto has_node = [&s](std::uint64_t id) { return s.eval_nodes.find(id) != s.eval_nodes.end(); };
  for (const auto& [id, kind] : s.eval_nodes) {
    (void)kind;
    if (s.eval_labels.find(id) == s.eval_labels.end()) {
      return false;
    }
  }
  for (const auto& [id, label] : s.eval_labels) {
    (void)label;
    if (!has_node(id)) {
      return false;
    }
  }
  for (const auto& [id, inv] : s.eval_invalid) {
    (void)inv;
    if (!has_node(id)) {
      return false;
    }
  }
  for (const auto& [id, cnt] : s.eval_recompute_count) {
    (void)cnt;
    if (!has_node(id)) {
      return false;
    }
  }
  for (const auto& [from, deps] : s.eval_dependencies) {
    if (!has_node(from)) {
      return false;
    }
    for (const auto to : deps) {
      if (!has_node(to)) {
        return false;
      }
    }
  }
  for (const auto& [to, froms] : s.eval_reverse_dependencies) {
    if (!has_node(to)) {
      return false;
    }
    for (const auto from : froms) {
      if (!has_node(from)) {
        return false;
      }
    }
  }
  for (const auto& [from, deps] : s.eval_dependencies) {
    for (const auto to : deps) {
      const auto rev_it = s.eval_reverse_dependencies.find(to);
      if (rev_it == s.eval_reverse_dependencies.end()) {
        return false;
      }
      if (std::find(rev_it->second.begin(), rev_it->second.end(), from) == rev_it->second.end()) {
        return false;
      }
    }
  }
  for (const auto& [to, froms] : s.eval_reverse_dependencies) {
    for (const auto from : froms) {
      const auto fwd_it = s.eval_dependencies.find(from);
      if (fwd_it == s.eval_dependencies.end()) {
        return false;
      }
      const auto& fd = fwd_it->second;
      if (std::find(fd.begin(), fd.end(), to) == fd.end()) {
        return false;
      }
    }
  }
  for (const auto& [body, nodes] : s.eval_body_bindings) {
    (void)body;
    for (const auto nid : nodes) {
      if (!has_node(nid)) {
        return false;
      }
    }
  }
  return true;
}

void append_topology_commit_audit_object(std::ostream& o, const KernelTopologyCommitAudit& a) {
  o << "{\"last_committed_version\":" << a.last_committed_version
    << ",\"last_commit_write_operations\":" << a.last_commit_write_operations
    << ",\"committed_transaction_count\":" << a.committed_transaction_count
    << ",\"committed_write_operations_total\":" << a.committed_write_operations_total
    << ",\"last_commit_write_breakdown\":";
  append_topology_breakdown_json(o, a.last_commit_write_breakdown);
  o << ",\"cumulative_commit_write_breakdown\":";
  append_topology_breakdown_json(o, a.cumulative_commit_write_breakdown);
  o << "}";
}

void append_eval_graph_metrics_object(std::ostream& o, const KernelEvalGraphMetrics& g) {
  const auto& t = g.telemetry;
  std::ostringstream mean_os;
  mean_os.setf(std::ios::fixed);
  mean_os << std::setprecision(12) << g.mean_recompute_events_per_node;
  std::ostringstream mean_touched_os;
  mean_touched_os.setf(std::ios::fixed);
  mean_touched_os << std::setprecision(12) << g.mean_recompute_events_per_touched_node;
  o << "{\"node_count\":" << g.node_count << ",\"invalid_node_count\":" << g.invalid_node_count
    << ",\"recompute_events_total\":" << g.recompute_events_total
    << ",\"max_per_node_recompute_count\":" << g.max_per_node_recompute_count
    << ",\"nodes_with_recompute_nonzero\":" << g.nodes_with_recompute_nonzero
    << ",\"mean_recompute_events_per_node\":" << mean_os.str()
    << ",\"mean_recompute_events_per_touched_node\":" << mean_touched_os.str()
    << ",\"dependency_from_node_count\":" << g.dependency_from_node_count
    << ",\"dependency_edge_count\":" << g.dependency_edge_count
    << ",\"body_binding_record_count\":" << g.body_binding_record_count
    << ",\"body_binding_reference_total\":" << g.body_binding_reference_total << ",\"telemetry\":{"
    << "\"invalidate_node_calls\":" << t.invalidate_node_calls
    << ",\"invalidate_body_calls\":" << t.invalidate_body_calls
    << ",\"invalidate_many_batches\":" << t.invalidate_many_batches
    << ",\"invalidate_many_node_total\":" << t.invalidate_many_node_total
    << ",\"recompute_many_batches\":" << t.recompute_many_batches
    << ",\"recompute_many_root_total\":" << t.recompute_many_root_total
    << ",\"recompute_transitive_dedup_skips\":" << t.recompute_transitive_dedup_skips
    << ",\"recompute_finish_events\":" << t.recompute_finish_events
    << ",\"invalidate_node_redundant_calls\":" << t.invalidate_node_redundant_calls
    << ",\"recompute_root_already_valid_calls\":" << t.recompute_root_already_valid_calls
    << ",\"eval_graph_state_read_calls\":" << t.eval_graph_state_read_calls
    << ",\"recompute_single_root_max_finish_nodes\":" << t.recompute_single_root_max_finish_nodes
    << ",\"recompute_single_root_max_stack_depth\":" << t.recompute_single_root_max_stack_depth << "}"
    << ",\"invalidation_bridge\":{"
    << "\"for_body_entries\":" << g.invalidation_bridge.for_body_entries
    << ",\"for_faces_entries\":" << g.invalidation_bridge.for_faces_entries
    << ",\"for_bodies_batches\":" << g.invalidation_bridge.for_bodies_batches
    << ",\"for_bodies_list_size_total\":" << g.invalidation_bridge.for_bodies_list_size_total
    << ",\"downstream_invalidation_steps\":" << g.invalidation_bridge.downstream_invalidation_steps << "}}";
}

void append_runtime_store_counts_object(std::ostream& o, const KernelRuntimeStoreCounts& c) {
  const auto& tm = c.tessellation_metrics;
  o << "{\"mesh_records\":" << c.mesh_records
    << ",\"tessellation_cache_entries\":" << c.tessellation_cache_entries
    << ",\"face_tessellation_cache_entries\":" << c.face_tessellation_cache_entries
    << ",\"intersection_records\":" << c.intersection_records
    << ",\"curve_eval_cache_entries\":" << c.curve_eval_cache_entries
    << ",\"surface_eval_cache_entries\":" << c.surface_eval_cache_entries
    << ",\"eval_node_records\":" << c.eval_node_records
    << ",\"diagnostic_reports\":" << c.diagnostic_reports
    << ",\"topology_query_op_count\":" << c.topology_query_op_count << ",\"tessellation_metrics\":{"
    << "\"body_cache_hits\":" << tm.body_cache_hits << ",\"body_cache_misses\":" << tm.body_cache_misses
    << ",\"body_cache_stale_evictions\":" << tm.body_cache_stale_evictions
    << ",\"face_cache_hits\":" << tm.face_cache_hits << ",\"face_cache_misses\":" << tm.face_cache_misses
    << ",\"face_cache_stale_evictions\":" << tm.face_cache_stale_evictions << "}}";
}

}  // namespace

Kernel::Kernel(const KernelConfig& config)
    : state_(std::make_shared<detail::KernelState>(config)),
      curve_factory_(state_),
      pcurve_factory_(state_),
      surface_factory_(state_),
      curve_service_(state_),
      pcurve_service_(state_),
      surface_service_(state_),
      geometry_transform_service_(state_),
      geometry_intersection_service_(state_),
      linear_algebra_service_(state_),
      predicate_service_(state_),
      tolerance_service_(state_),
      topology_service_(state_),
      primitive_service_(state_),
      sweep_service_(state_),
      boolean_service_(state_),
      modify_service_(state_),
      blend_service_(state_),
      query_service_(state_),
      validation_service_(state_),
      repair_service_(state_),
      representation_service_(state_),
      conversion_service_(state_),
      io_service_(state_),
      diagnostic_service_(state_),
      eval_graph_service_(state_) {
  state_->plugin_registry.bind_host_kernel_for_plugin_invocation(
      std::weak_ptr<detail::KernelState>(state_));
}

static std::vector<std::string> fixed_service_names() {
  return {
      "math.linear_algebra",
      "math.predicates",
      "math.tolerance",
      "geo.curves",
      "geo.surfaces",
      "geo.transform",
      "geo.intersection",
      "topo.topology",
      "rep.representation",
      "rep.conversion",
      "ops.boolean",
      "ops.modify",
      "ops.blend",
      "heal.validation",
      "heal.repair",
      "io.files",
      "diag.service",
      "eval.graph",
      "plugin.registry",
      "plugin.discovery",
      "plugin.import",
      "plugin.export",
      "plugin.repair",
      "plugin.curve",
      "plugin.verify_curve",
      "sdk.kernel"
  };
}

Result<KernelConfig> Kernel::config() const {
  return ok_result(state_->config, state_->create_diagnostic("已查询内核配置"));
}

Result<void> Kernel::set_enable_diagnostics(bool enabled) {
  state_->config.enable_diagnostics = enabled;
  return ok_void(state_->create_diagnostic("已设置诊断开关"));
}

Result<bool> Kernel::enable_diagnostics() const {
  return ok_result(state_->config.enable_diagnostics,
                   state_->create_diagnostic("已查询诊断开关"));
}

Result<void> Kernel::set_linear_tolerance(Scalar value) {
  if (!std::isfinite(value) || value <= 0.0) {
    return error_void(StatusCode::InvalidInput,
                      state_->create_diagnostic("线性容差设置失败"));
  }
  state_->config.tolerance.linear = value;
  return ok_void(state_->create_diagnostic("已设置线性容差"));
}

Result<Scalar> Kernel::linear_tolerance() const {
  return ok_result(state_->config.tolerance.linear,
                   state_->create_diagnostic("已查询线性容差"));
}

Result<void> Kernel::set_angular_tolerance(Scalar value) {
  if (!std::isfinite(value) || value <= 0.0) {
    return error_void(StatusCode::InvalidInput,
                      state_->create_diagnostic("角度容差设置失败"));
  }
  state_->config.tolerance.angular = value;
  return ok_void(state_->create_diagnostic("已设置角度容差"));
}

Result<Scalar> Kernel::angular_tolerance() const {
  return ok_result(state_->config.tolerance.angular,
                   state_->create_diagnostic("已查询角度容差"));
}

Result<void> Kernel::set_enable_cache(bool enabled) {
  state_->config.enable_cache = enabled;
  return ok_void(state_->create_diagnostic("已设置几何求值缓存开关"));
}

Result<bool> Kernel::enable_cache() const {
  return ok_result(state_->config.enable_cache, state_->create_diagnostic("已查询几何求值缓存开关"));
}

Result<void> Kernel::set_precision_mode(PrecisionMode mode) {
  state_->config.precision_mode = mode;
  return ok_void(state_->create_diagnostic("已设置精度模式"));
}

Result<PrecisionMode> Kernel::precision_mode() const {
  return ok_result(state_->config.precision_mode, state_->create_diagnostic("已查询精度模式"));
}

Result<VersionId> Kernel::topology_version_next() const {
  return ok_result(state_->next_version, state_->create_diagnostic("已查询拓扑提交版本号"));
}

Result<bool> Kernel::kernel_config_numeric_wellformed() const {
  const auto& tol = state_->config.tolerance;
  const bool ok = std::isfinite(tol.linear) && tol.linear > 0.0 && std::isfinite(tol.angular) && tol.angular > 0.0 &&
                  std::isfinite(tol.min_local) && tol.min_local > 0.0 && std::isfinite(tol.max_local) &&
                  tol.max_local > 0.0 && tol.min_local <= tol.max_local;
  return ok_result(ok, state_->create_diagnostic(ok ? "内核数值配置检查通过" : "内核数值配置未满足良定义条件"));
}

Result<bool> Kernel::topology_version_audit_consistent() const {
  const bool ok = kernel_topology_version_audit_consistent(*state_);
  return ok_result(ok, state_->create_diagnostic(ok ? "拓扑版本与提交审计一致" : "拓扑版本与提交审计不一致"));
}

Result<bool> Kernel::eval_graph_store_maps_consistent() const {
  const bool ok = kernel_eval_graph_store_maps_consistent(*state_);
  return ok_result(ok, state_->create_diagnostic(ok ? "EvalGraph 存储映射一致" : "EvalGraph 存储映射不一致"));
}

Result<bool> Kernel::core_runtime_invariants_hold() const {
  const auto cfg = kernel_config_numeric_wellformed();
  if (cfg.status != StatusCode::Ok || !cfg.value.has_value() || !*cfg.value) {
    return ok_result(false, state_->create_diagnostic("核心运行不变量：数值配置未通过"));
  }
  if (!kernel_topology_version_audit_consistent(*state_)) {
    return ok_result(false, state_->create_diagnostic("核心运行不变量：拓扑版本审计未通过"));
  }
  if (!kernel_eval_graph_store_maps_consistent(*state_)) {
    return ok_result(false, state_->create_diagnostic("核心运行不变量：EvalGraph 存储映射未通过"));
  }
  const auto tess = runtime_tessellation_caches_consistent();
  if (tess.status != StatusCode::Ok || !tess.value.has_value() || !*tess.value) {
    return ok_result(false, state_->create_diagnostic("核心运行不变量：三角化缓存一致性未通过"));
  }
  return ok_result(true, state_->create_diagnostic("核心运行不变量已满足"));
}

Result<KernelTopologyCommitAudit> Kernel::topology_commit_audit() const {
  KernelTopologyCommitAudit a;
  a.last_committed_version = state_->topology_last_committed_version;
  a.last_commit_write_operations = state_->topology_last_commit_write_operations;
  a.committed_transaction_count = state_->topology_commit_success_count;
  a.committed_write_operations_total = state_->topology_committed_write_operations_total;
  a.last_commit_write_breakdown = state_->topology_last_commit_write_breakdown;
  a.cumulative_commit_write_breakdown = state_->topology_committed_write_breakdown_totals;
  return ok_result(std::move(a), state_->create_diagnostic("已查询拓扑提交审计快照"));
}

Result<KernelEvalGraphMetrics> Kernel::eval_graph_metrics() const {
  KernelEvalGraphMetrics m;
  m.node_count = static_cast<std::uint64_t>(state_->eval_nodes.size());
  for (const auto& [id, kind] : state_->eval_nodes) {
    (void)kind;
    const auto it = state_->eval_invalid.find(id);
    if (it != state_->eval_invalid.end() && it->second) {
      ++m.invalid_node_count;
    }
  }
  for (const auto& [id, c] : state_->eval_recompute_count) {
    (void)id;
    m.recompute_events_total += c;
    m.max_per_node_recompute_count = std::max(m.max_per_node_recompute_count, c);
    if (c > 0) {
      ++m.nodes_with_recompute_nonzero;
    }
  }
  m.mean_recompute_events_per_node =
      m.node_count > 0 ? static_cast<double>(m.recompute_events_total) / static_cast<double>(m.node_count) : 0.0;
  m.mean_recompute_events_per_touched_node =
      m.nodes_with_recompute_nonzero > 0
          ? static_cast<double>(m.recompute_events_total) / static_cast<double>(m.nodes_with_recompute_nonzero)
          : 0.0;
  m.dependency_from_node_count = static_cast<std::uint64_t>(state_->eval_dependencies.size());
  for (const auto& [id, deps] : state_->eval_dependencies) {
    (void)id;
    m.dependency_edge_count += static_cast<std::uint64_t>(deps.size());
  }
  m.body_binding_record_count = static_cast<std::uint64_t>(state_->eval_body_bindings.size());
  for (const auto& [body, nodes] : state_->eval_body_bindings) {
    (void)body;
    m.body_binding_reference_total += static_cast<std::uint64_t>(nodes.size());
  }
  m.telemetry = state_->eval_telemetry;
  m.invalidation_bridge = state_->eval_invalidation_bridge;
  return ok_result(std::move(m), state_->create_diagnostic("已查询 EvalGraph 聚合指标"));
}

Result<bool> Kernel::runtime_tessellation_caches_consistent() const {
  for (const auto& e : state_->tessellation_cache) {
    if (state_->meshes.find(e.second.value) == state_->meshes.end()) {
      return ok_result(false, {});
    }
  }
  for (const auto& e : state_->face_tessellation_cache) {
    if (state_->meshes.find(e.second.value) == state_->meshes.end()) {
      return ok_result(false, {});
    }
  }
  return ok_result(true, {});
}

Result<std::uint64_t> Kernel::next_object_id() const {
  return ok_result(state_->next_id, state_->create_diagnostic("已查询下一个对象ID"));
}

Result<std::uint64_t> Kernel::next_diagnostic_id() const {
  return ok_result(state_->next_diag_id,
                   state_->create_diagnostic("已查询下一个诊断ID"));
}

Result<std::uint64_t> Kernel::object_count_total() const {
  const std::uint64_t total = static_cast<std::uint64_t>(state_->curves.size() + state_->surfaces.size() +
      state_->vertices.size() + state_->edges.size() + state_->coedges.size() + state_->loops.size() +
      state_->faces.size() + state_->shells.size() + state_->bodies.size() + state_->meshes.size() +
      state_->pcurves.size() +
      state_->intersections.size());
  return ok_result(total, state_->create_diagnostic("已查询对象总数"));
}

Result<std::uint64_t> Kernel::geometry_count() const {
  return ok_result(static_cast<std::uint64_t>(state_->curves.size() + state_->pcurves.size() + state_->surfaces.size()),
                   state_->create_diagnostic("已查询几何对象数量"));
}

Result<std::uint64_t> Kernel::topology_count() const {
  return ok_result(static_cast<std::uint64_t>(state_->vertices.size() + state_->edges.size() +
                                              state_->coedges.size() + state_->loops.size() +
                                              state_->faces.size() + state_->shells.size()),
                   state_->create_diagnostic("已查询拓扑对象数量"));
}

Result<std::uint64_t> Kernel::body_count() const {
  return ok_result(static_cast<std::uint64_t>(state_->bodies.size()),
                   state_->create_diagnostic("已查询体数量"));
}

Result<std::uint64_t> Kernel::mesh_count() const {
  return ok_result(static_cast<std::uint64_t>(state_->meshes.size()),
                   state_->create_diagnostic("已查询网格数量"));
}

Result<std::uint64_t> Kernel::intersection_count() const {
  return ok_result(static_cast<std::uint64_t>(state_->intersections.size()),
                   state_->create_diagnostic("已查询求交记录数量"));
}

Result<std::uint64_t> Kernel::eval_node_count() const {
  return ok_result(static_cast<std::uint64_t>(state_->eval_nodes.size()),
                   state_->create_diagnostic("已查询求值节点数量"));
}

Result<std::uint64_t> Kernel::cache_entry_count() const {
  return ok_result(static_cast<std::uint64_t>(state_->curve_eval_cache.size() +
                                              state_->surface_eval_cache.size()),
                   state_->create_diagnostic("已查询缓存条目数量"));
}

Result<TessellationCacheStats> Kernel::tessellation_cache_stats() const {
  return ok_result(state_->tessellation_cache_stats, state_->create_diagnostic("已查询三角化缓存统计"));
}

Result<void> Kernel::export_tessellation_cache_stats_json(std::string_view path) const {
  return conversion_service_.export_tessellation_cache_stats_json(path);
}

Result<void> Kernel::export_round_trip_report_json(const RoundTripReport& report, std::string_view path) const {
  return conversion_service_.export_round_trip_report_json(report, path);
}

Result<ConversionErrorBudget> Kernel::conversion_error_budget_for_tessellation(const TessellationOptions& options) const {
  return conversion_service_.conversion_error_budget_for_tessellation(options);
}

Result<void> Kernel::export_conversion_error_budget_json(const TessellationOptions& options,
                                                         std::string_view path) const {
  return conversion_service_.export_conversion_error_budget_json(options, path);
}

Result<void> Kernel::export_topology_commit_audit_json(std::string_view path) const {
  if (path.empty()) {
    return error_void(StatusCode::InvalidInput,
                      state_->create_diagnostic("拓扑提交审计导出失败：输出路径为空"));
  }
  std::ofstream out {std::string(path)};
  if (!out) {
    return error_void(StatusCode::OperationFailed,
                      state_->create_diagnostic("拓扑提交审计导出失败：无法打开输出文件"));
  }
  const auto ar = topology_commit_audit();
  if (ar.status != StatusCode::Ok || !ar.value.has_value()) {
    return error_void(ar.status, ar.diagnostic_id);
  }
  append_topology_commit_audit_object(out, *ar.value);
  return ok_void(state_->create_diagnostic("已导出拓扑提交审计 JSON"));
}

Result<void> Kernel::export_eval_graph_metrics_json(std::string_view path) const {
  if (path.empty()) {
    return error_void(StatusCode::InvalidInput,
                      state_->create_diagnostic("EvalGraph 指标导出失败：输出路径为空"));
  }
  std::ofstream out {std::string(path)};
  if (!out) {
    return error_void(StatusCode::OperationFailed,
                      state_->create_diagnostic("EvalGraph 指标导出失败：无法打开输出文件"));
  }
  const auto mr = eval_graph_metrics();
  if (mr.status != StatusCode::Ok || !mr.value.has_value()) {
    return error_void(mr.status, mr.diagnostic_id);
  }
  append_eval_graph_metrics_object(out, *mr.value);
  return ok_void(state_->create_diagnostic("已导出 EvalGraph 指标 JSON"));
}

Result<void> Kernel::export_runtime_observability_json(std::string_view path) const {
  if (path.empty()) {
    return error_void(StatusCode::InvalidInput,
                      state_->create_diagnostic("运行时可观测性导出失败：输出路径为空"));
  }
  std::ofstream out {std::string(path)};
  if (!out) {
    return error_void(StatusCode::OperationFailed,
                      state_->create_diagnostic("运行时可观测性导出失败：无法打开输出文件"));
  }
  const auto ar = topology_commit_audit();
  if (ar.status != StatusCode::Ok || !ar.value.has_value()) {
    return error_void(ar.status, ar.diagnostic_id);
  }
  const auto mr = eval_graph_metrics();
  if (mr.status != StatusCode::Ok || !mr.value.has_value()) {
    return error_void(mr.status, mr.diagnostic_id);
  }
  const auto sr = runtime_store_counts();
  if (sr.status != StatusCode::Ok || !sr.value.has_value()) {
    return error_void(sr.status, sr.diagnostic_id);
  }
  out << "{\"topology_version_next\":" << state_->next_version << ",\"topology_commit_audit\":";
  append_topology_commit_audit_object(out, *ar.value);
  out << ",\"eval_graph_metrics\":";
  append_eval_graph_metrics_object(out, *mr.value);
  out << ",\"runtime_store_counts\":";
  append_runtime_store_counts_object(out, *sr.value);
  const TessellationOptions rep_defaults{};
  const auto rep_budget = detail::conversion_error_budget_from_tessellation(rep_defaults);
  out << ",\"rep_stage_snapshot\":{";
  out << "\"default_tessellation_options\":" << detail::tessellation_budget_digest_json(rep_defaults);
  out << ",\"default_conversion_error_budget\":" << detail::conversion_error_budget_digest_json(rep_budget);
  out << "}}";
  return ok_void(state_->create_diagnostic("已导出运行时可观测性合并 JSON"));
}

Result<KernelRuntimeStoreCounts> Kernel::runtime_store_counts() const {
  KernelRuntimeStoreCounts out;
  out.mesh_records = static_cast<std::uint64_t>(state_->meshes.size());
  out.tessellation_cache_entries = static_cast<std::uint64_t>(state_->tessellation_cache.size());
  out.face_tessellation_cache_entries = static_cast<std::uint64_t>(state_->face_tessellation_cache.size());
  out.intersection_records = static_cast<std::uint64_t>(state_->intersections.size());
  out.curve_eval_cache_entries = static_cast<std::uint64_t>(state_->curve_eval_cache.size());
  out.surface_eval_cache_entries = static_cast<std::uint64_t>(state_->surface_eval_cache.size());
  out.eval_node_records = static_cast<std::uint64_t>(state_->eval_nodes.size());
  out.diagnostic_reports = static_cast<std::uint64_t>(state_->diagnostics.size());
  out.topology_query_op_count = state_->topology_query_op_count;
  out.tessellation_metrics = state_->tessellation_cache_stats;
  return ok_result(std::move(out), {});
}

Result<std::uint64_t> Kernel::prune_stale_tessellation_cache_entries() {
  std::uint64_t removed = 0;
  for (auto it = state_->tessellation_cache.begin(); it != state_->tessellation_cache.end();) {
    if (state_->meshes.find(it->second.value) == state_->meshes.end()) {
      ++state_->tessellation_cache_stats.body_cache_stale_evictions;
      ++removed;
      it = state_->tessellation_cache.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = state_->face_tessellation_cache.begin(); it != state_->face_tessellation_cache.end();) {
    if (state_->meshes.find(it->second.value) == state_->meshes.end()) {
      ++state_->tessellation_cache_stats.face_cache_stale_evictions;
      ++removed;
      it = state_->face_tessellation_cache.erase(it);
    } else {
      ++it;
    }
  }
  return ok_result(removed, state_->create_diagnostic("已修剪悬挂三角化缓存条目"));
}

Result<void> Kernel::clear_eval_caches() {
  state_->curve_eval_cache.clear();
  state_->surface_eval_cache.clear();
  return ok_void(state_->create_diagnostic("已清空求值缓存"));
}

Result<void> Kernel::clear_curve_eval_cache() {
  state_->curve_eval_cache.clear();
  return ok_void(state_->create_diagnostic("已清空曲线缓存"));
}

Result<void> Kernel::clear_surface_eval_cache() {
  state_->surface_eval_cache.clear();
  return ok_void(state_->create_diagnostic("已清空曲面缓存"));
}

Result<void> Kernel::clear_diagnostics_store() {
  state_->diagnostics.clear();
  return ok_void(state_->create_diagnostic("已清空诊断存储"));
}

Result<void> Kernel::clear_intersections_store() {
  state_->intersections.clear();
  return ok_void(state_->create_diagnostic("已清空求交存储"));
}

Result<void> Kernel::clear_mesh_store() {
  state_->meshes.clear();
  state_->tessellation_cache.clear();
  state_->face_tessellation_cache.clear();
  state_->tessellation_cache_stats = {};
  return ok_void(state_->create_diagnostic("已清空网格存储"));
}

Result<bool> Kernel::has_body_id(BodyId id) const {
  return ok_result(state_->bodies.find(id.value) != state_->bodies.end(),
                   state_->create_diagnostic("已查询体存在性"));
}

Result<bool> Kernel::has_curve_id(CurveId id) const {
  return ok_result(state_->curves.find(id.value) != state_->curves.end(),
                   state_->create_diagnostic("已查询曲线存在性"));
}

Result<bool> Kernel::has_pcurve_id(PCurveId id) const {
  return ok_result(state_->pcurves.find(id.value) != state_->pcurves.end(),
                   state_->create_diagnostic("已查询参数曲线存在性"));
}

Result<bool> Kernel::has_surface_id(SurfaceId id) const {
  return ok_result(state_->surfaces.find(id.value) != state_->surfaces.end(),
                   state_->create_diagnostic("已查询曲面存在性"));
}

Result<bool> Kernel::has_face_id(FaceId id) const {
  return ok_result(state_->faces.find(id.value) != state_->faces.end(),
                   state_->create_diagnostic("已查询面存在性"));
}

Result<bool> Kernel::has_shell_id(ShellId id) const {
  return ok_result(state_->shells.find(id.value) != state_->shells.end(),
                   state_->create_diagnostic("已查询壳存在性"));
}

Result<bool> Kernel::has_edge_id(EdgeId id) const {
  return ok_result(state_->edges.find(id.value) != state_->edges.end(),
                   state_->create_diagnostic("已查询边存在性"));
}

Result<void> Kernel::reset_runtime_stores() {
  state_->meshes.clear();
  state_->tessellation_cache.clear();
  state_->face_tessellation_cache.clear();
  state_->tessellation_cache_stats = {};
  state_->intersections.clear();
  state_->curve_eval_cache.clear();
  state_->surface_eval_cache.clear();
  state_->eval_invalid.clear();
  state_->eval_recompute_count.clear();
  state_->eval_dependencies.clear();
  state_->eval_reverse_dependencies.clear();
  state_->eval_nodes.clear();
  state_->eval_labels.clear();
  state_->eval_body_bindings.clear();
  state_->eval_telemetry = {};
  state_->eval_invalidation_bridge = {};
  state_->diagnostics.clear();
  state_->topology_query_op_count = 0;
  // 不在此处写入诊断存储：否则与“清空诊断”语义相矛盾，且会破坏 reset 后零报告的不变量门禁。
  return ok_void({});
}

Result<std::vector<std::string>> Kernel::services_available() const {
  return ok_result(fixed_service_names(), state_->create_diagnostic("已查询可用服务列表"));
}

Result<std::vector<std::string>> Kernel::module_names() const {
  return ok_result(std::vector<std::string>{
                       "core", "math", "geo", "topo", "rep", "ops", "heal", "io", "diag", "eval", "sdk", "plugin"},
                   state_->create_diagnostic("已查询模块名"));
}

Result<bool> Kernel::has_service_geometry() const { return ok_result(true, {}); }
Result<bool> Kernel::has_service_topology() const { return ok_result(true, {}); }
Result<bool> Kernel::has_service_io() const { return ok_result(true, {}); }
Result<bool> Kernel::has_service_diagnostics() const { return ok_result(true, {}); }
Result<bool> Kernel::has_service_eval() const { return ok_result(true, {}); }
Result<bool> Kernel::has_service_ops() const { return ok_result(true, {}); }
Result<bool> Kernel::has_service_heal() const { return ok_result(true, {}); }
Result<bool> Kernel::has_service_representation() const { return ok_result(true, {}); }
Result<bool> Kernel::has_service_conversion() const { return ok_result(true, {}); }
Result<bool> Kernel::has_service_math() const { return ok_result(true, {}); }
Result<bool> Kernel::has_service_geo() const { return ok_result(true, {}); }

Result<std::vector<std::string>> Kernel::io_supported_formats() const {
  // 与 IOService::detect_format 返回的格式 id 一致（小写）；IGES/BREP 为 Axiom 元数据/JSON 互操作子集。
  return ok_result(std::vector<std::string>{"3mf", "axmjson", "brep", "gltf", "iges", "obj", "step", "stl"},
                   state_->create_diagnostic("已查询IO支持格式"));
}

Result<bool> Kernel::io_can_import_format(std::string_view fmt) const {
  std::string key;
  key.reserve(fmt.size());
  for (const char c : fmt) {
    key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (key.empty()) {
    return ok_result(false, state_->create_diagnostic("已查询IO导入能力"));
  }
  // 与 IOService::import_auto 一致。
  const bool ok = (key == "step" || key == "axmjson" || key == "stl" || key == "gltf" || key == "iges" ||
                   key == "brep" || key == "obj" || key == "3mf");
  return ok_result(ok, state_->create_diagnostic("已查询IO导入能力"));
}

Result<bool> Kernel::io_can_export_format(std::string_view fmt) const {
  std::string key;
  key.reserve(fmt.size());
  for (const char c : fmt) {
    key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (key.empty()) {
    return ok_result(false, state_->create_diagnostic("已查询IO导出能力"));
  }
  // 与 IOService::export_auto 一致。
  const bool ok = (key == "step" || key == "axmjson" || key == "gltf" || key == "stl" || key == "iges" ||
                   key == "brep" || key == "obj" || key == "3mf");
  return ok_result(ok, state_->create_diagnostic("已查询IO导出能力"));
}

Result<std::vector<std::string>> Kernel::capability_report_lines() const {
  std::vector<std::string> lines;
  const auto modules = module_names();
  if (modules.status == StatusCode::Ok && modules.value.has_value()) {
    lines.push_back("Modules: " + std::to_string(modules.value->size()));
    for (const auto& m : *modules.value) lines.push_back(" - " + m);
  }
  const auto services = services_available();
  if (services.status == StatusCode::Ok && services.value.has_value()) {
    lines.push_back("Services: " + std::to_string(services.value->size()));
    for (const auto& s : *services.value) lines.push_back(" * " + s);
  }
  const auto formats = io_supported_formats();
  if (formats.status == StatusCode::Ok && formats.value.has_value()) {
    lines.push_back("IO Formats: " + std::to_string(formats.value->size()));
    for (const auto& f : *formats.value) lines.push_back(" ~ " + f);
  }
  const auto objs = object_count_total();
  if (objs.status == StatusCode::Ok && objs.value.has_value()) {
    lines.push_back("Objects.Total=" + std::to_string(*objs.value));
  }
  lines.push_back(std::string("Core.Topology.VersionNext=") + std::to_string(state_->next_version));
  lines.push_back("Core.Topology.Isolation.Effective=SnapshotSerializable");
  lines.push_back(std::string("Core.Topology.Audit.LastVersion=") +
                  std::to_string(state_->topology_last_committed_version));
  lines.push_back(std::string("Core.Topology.Audit.LastWriteOps=") +
                  std::to_string(state_->topology_last_commit_write_operations));
  lines.push_back(std::string("Core.Topology.Audit.CommittedTxns=") +
                  std::to_string(state_->topology_commit_success_count));
  lines.push_back(std::string("Core.Topology.Audit.CommittedWriteOpsTotal=") +
                  std::to_string(state_->topology_committed_write_operations_total));
  {
    const auto& b = state_->topology_committed_write_breakdown_totals;
    lines.push_back(std::string("Core.Topology.Audit.Cumulative.CreatedEntities=") +
                    std::to_string(topology_commit_breakdown_created_entities_total(b)));
  }
  lines.push_back(std::string("Core.EvalGraph.Nodes=") + std::to_string(state_->eval_nodes.size()));
  {
    std::uint64_t recompute_sum = 0;
    std::uint64_t touched = 0;
    for (const auto& [id, c] : state_->eval_recompute_count) {
      (void)id;
      recompute_sum += c;
      if (c > 0) {
        ++touched;
      }
    }
    lines.push_back(std::string("Core.EvalGraph.RecomputeTotal=") + std::to_string(recompute_sum));
    std::ostringstream mt;
    mt.setf(std::ios::fixed);
    mt << std::setprecision(12);
    mt << (touched > 0 ? static_cast<double>(recompute_sum) / static_cast<double>(touched) : 0.0);
    lines.push_back(std::string("Core.EvalGraph.MeanRecomputePerTouchedNode=") + mt.str());
  }
  {
    std::uint64_t dep_edges = 0;
    for (const auto& [id, deps] : state_->eval_dependencies) {
      (void)id;
      dep_edges += static_cast<std::uint64_t>(deps.size());
    }
    lines.push_back(std::string("Core.EvalGraph.DependencyEdges=") + std::to_string(dep_edges));
    lines.push_back(std::string("Core.EvalGraph.BodyBindingRecords=") +
                    std::to_string(state_->eval_body_bindings.size()));
    std::uint64_t body_bind_refs = 0;
    for (const auto& [bid, nodes] : state_->eval_body_bindings) {
      (void)bid;
      body_bind_refs += static_cast<std::uint64_t>(nodes.size());
    }
    lines.push_back(std::string("Core.EvalGraph.BodyBindingRefs=") + std::to_string(body_bind_refs));
  }
  {
    const auto& t = state_->eval_telemetry;
    lines.push_back(std::string("Core.EvalGraph.Telemetry.InvalidateNodeCalls=") + std::to_string(t.invalidate_node_calls));
    lines.push_back(std::string("Core.EvalGraph.Telemetry.InvalidateBodyCalls=") + std::to_string(t.invalidate_body_calls));
    lines.push_back(std::string("Core.EvalGraph.Telemetry.InvalidateManyBatches=") +
                    std::to_string(t.invalidate_many_batches));
    lines.push_back(std::string("Core.EvalGraph.Telemetry.InvalidateManyNodeTotal=") +
                    std::to_string(t.invalidate_many_node_total));
    lines.push_back(std::string("Core.EvalGraph.Telemetry.RecomputeManyBatches=") +
                    std::to_string(t.recompute_many_batches));
    lines.push_back(std::string("Core.EvalGraph.Telemetry.RecomputeManyRootTotal=") +
                    std::to_string(t.recompute_many_root_total));
    lines.push_back(std::string("Core.EvalGraph.Telemetry.RecomputeDedupSkips=") +
                    std::to_string(t.recompute_transitive_dedup_skips));
    lines.push_back(std::string("Core.EvalGraph.Telemetry.RecomputeFinishEvents=") +
                    std::to_string(t.recompute_finish_events));
    lines.push_back(std::string("Core.EvalGraph.Telemetry.InvalidateNodeRedundantCalls=") +
                    std::to_string(t.invalidate_node_redundant_calls));
    lines.push_back(std::string("Core.EvalGraph.Telemetry.RecomputeRootAlreadyValidCalls=") +
                    std::to_string(t.recompute_root_already_valid_calls));
    lines.push_back(std::string("Core.EvalGraph.Telemetry.EvalGraphStateReadCalls=") +
                    std::to_string(t.eval_graph_state_read_calls));
    lines.push_back(std::string("Core.EvalGraph.Telemetry.RecomputeSingleRootMaxFinishNodes=") +
                    std::to_string(t.recompute_single_root_max_finish_nodes));
    lines.push_back(std::string("Core.EvalGraph.Telemetry.RecomputeSingleRootMaxStackDepth=") +
                    std::to_string(t.recompute_single_root_max_stack_depth));
  }
  {
    const auto& br = state_->eval_invalidation_bridge;
    lines.push_back(std::string("Core.EvalGraph.Bridge.ForBodyEntries=") + std::to_string(br.for_body_entries));
    lines.push_back(std::string("Core.EvalGraph.Bridge.ForFacesEntries=") + std::to_string(br.for_faces_entries));
    lines.push_back(std::string("Core.EvalGraph.Bridge.ForBodiesBatches=") + std::to_string(br.for_bodies_batches));
    lines.push_back(std::string("Core.EvalGraph.Bridge.ForBodiesListSizeTotal=") +
                    std::to_string(br.for_bodies_list_size_total));
    lines.push_back(std::string("Core.EvalGraph.Bridge.DownstreamInvalidationSteps=") +
                    std::to_string(br.downstream_invalidation_steps));
  }
  lines.push_back("Core.EvalGraph.StoreMapsCheckedByCoreInvariants=1");
  lines.push_back(std::string("Core.Cache.GeoEval=") + (state_->config.enable_cache ? "on" : "off"));
  lines.push_back("Core.Export.RuntimeObservabilityJson=1");
  lines.push_back(std::string("Core.Precision.Mode=") + precision_mode_tag(state_->config.precision_mode));
  const auto& pol = state_->config.plugin_host_policy;
  const auto api_ver = plugin_sdk_api_version();
  const std::string api_str =
      (api_ver.status == StatusCode::Ok && api_ver.value.has_value()) ? *api_ver.value : std::string("?");
  lines.push_back("Plugin.SDK.API=" + api_str);
  lines.push_back(std::string("Plugin.Host.max_slots=") +
                  (pol.max_plugin_slots == 0 ? std::string("unlimited") : std::to_string(pol.max_plugin_slots)));
  lines.push_back(std::string("Plugin.Host.unique_impl_type=") +
                  (pol.enforce_unique_implementation_type_name ? "on" : "off"));
  lines.push_back(std::string("Plugin.Host.unique_manifest_name=") +
                  (pol.require_unique_manifest_name ? "on" : "off"));
  lines.push_back(std::string("Plugin.Host.require_capabilities=") +
                  (pol.require_non_empty_capabilities ? "on" : "off"));
  lines.push_back(std::string("Plugin.Host.require_impl_type_name=") +
                  (pol.require_non_empty_plugin_type_name ? "on" : "off"));
  lines.push_back(std::string("Plugin.Host.require_plugin_api_match=") +
                  (pol.require_plugin_api_version_match ? "on" : "off"));
  lines.push_back(std::string("Plugin.Host.plugin_api_version_match_mode=") +
                  plugin_api_version_match_mode_tag(pol.plugin_api_version_match_mode));
  lines.push_back(std::string("Plugin.Host.expected_sdk_api=") + std::string(kPluginSdkApiVersion));
  lines.push_back(std::string("Plugin.Host.sandbox_level=") + plugin_sandbox_level_tag(pol.sandbox_level));
  lines.push_back(std::string("Plugin.Host.auto_validate_after_plugin_importer=") +
                  (pol.auto_validate_body_after_plugin_importer ? "on" : "off"));
  lines.push_back(std::string("Plugin.Host.auto_validate_before_plugin_exporter=") +
                  (pol.auto_validate_body_before_plugin_exporter ? "on" : "off"));
  lines.push_back(std::string("Plugin.Host.auto_validate_after_plugin_repair=") +
                  (pol.auto_validate_body_after_plugin_repair ? "on" : "off"));
  const auto loaded = has_any_plugins();
  if (loaded.status == StatusCode::Ok && loaded.value.has_value()) {
    lines.push_back(std::string("Plugin.Loaded=") + (*loaded.value ? "yes" : "no"));
  }
  const auto slots = state_->plugin_registry.total_plugin_slots();
  const auto mc = state_->plugin_registry.manifest_count();
  if (slots.status == StatusCode::Ok && slots.value.has_value() && mc.status == StatusCode::Ok &&
      mc.value.has_value()) {
    lines.push_back("Plugin.Slots=" + std::to_string(*slots.value));
    lines.push_back("Plugin.Manifests=" + std::to_string(*mc.value));
  }
  return ok_result(std::move(lines), state_->create_diagnostic("已生成能力报告行"));
}

Result<std::string> Kernel::capability_report_txt() const {
  const auto lines = capability_report_lines();
  if (lines.status != StatusCode::Ok || !lines.value.has_value()) return error_result<std::string>(lines.status, lines.diagnostic_id);
  std::string out;
  for (const auto& l : *lines.value) out += l + "\n";
  return ok_result(std::move(out), state_->create_diagnostic("已生成能力报告文本"));
}

Result<SurfaceId>
Kernel::create_trimmed_surface_from_face_outer_loop_pcurves(FaceId face_id) {
  const auto base_r = topology().query().underlying_surface_for_face_trim(face_id);
  if (base_r.status != StatusCode::Ok || !base_r.value.has_value()) {
    return error_result<SurfaceId>(base_r.status, base_r.diagnostic_id);
  }
  const auto uv_r = topology().query().face_outer_loop_uv_bounds(face_id);
  if (uv_r.status != StatusCode::Ok || !uv_r.value.has_value()) {
    return error_result<SurfaceId>(uv_r.status, uv_r.diagnostic_id);
  }
  const auto &box = *uv_r.value;

  const auto poly_r = topology().query().face_outer_loop_uv_polyline(face_id);
  if (poly_r.status == StatusCode::Ok && poly_r.value.has_value() &&
      poly_r.value->size() >= 3) {
    const auto trim_ok = topology().validate().validate_face_trim_consistency(face_id);
    if (trim_ok.status != StatusCode::Ok) {
      return error_result<SurfaceId>(trim_ok.status, trim_ok.diagnostic_id);
    }
    std::vector<std::vector<Point2>> holes;
    const auto &frec = state_->faces.at(face_id.value);
    holes.reserve(frec.inner_loops.size());
    for (const auto &iloop : frec.inner_loops) {
      const auto hp = topology().query().face_loop_uv_polyline(face_id, iloop);
      if (hp.status != StatusCode::Ok || !hp.value.has_value()) {
        return error_result<SurfaceId>(hp.status, hp.diagnostic_id);
      }
      holes.push_back(std::move(*hp.value));
    }
    const auto &outer_pts = *poly_r.value;
    return surfaces().make_trimmed_polygon_with_holes(
        *base_r.value, box.u.min, box.u.max, box.v.min, box.v.max,
        std::span<const Point2>(outer_pts.data(), outer_pts.size()), holes);
  }

  return surfaces().make_trimmed(*base_r.value, box.u.min, box.u.max, box.v.min,
                                 box.v.max);
}

CurveFactory& Kernel::curves() { return curve_factory_; }
PCurveFactory& Kernel::pcurves() { return pcurve_factory_; }
SurfaceFactory& Kernel::surfaces() { return surface_factory_; }
CurveService& Kernel::curve_service() { return curve_service_; }
PCurveService& Kernel::pcurve_service() { return pcurve_service_; }
SurfaceService& Kernel::surface_service() { return surface_service_; }
GeometryTransformService& Kernel::geometry_transform() { return geometry_transform_service_; }
GeometryIntersectionService& Kernel::geometry_intersection() { return geometry_intersection_service_; }
LinearAlgebraService& Kernel::linear_algebra() { return linear_algebra_service_; }
PredicateService& Kernel::predicates() { return predicate_service_; }
ToleranceService& Kernel::tolerance() { return tolerance_service_; }
TopologyService& Kernel::topology() { return topology_service_; }
PrimitiveService& Kernel::primitives() { return primitive_service_; }
SweepService& Kernel::sweeps() { return sweep_service_; }
BooleanService& Kernel::booleans() { return boolean_service_; }
ModifyService& Kernel::modify() { return modify_service_; }
BlendService& Kernel::blends() { return blend_service_; }
QueryService& Kernel::query() { return query_service_; }
ValidationService& Kernel::validate() { return validation_service_; }
RepairService& Kernel::repair() { return repair_service_; }
RepresentationService& Kernel::representation() { return representation_service_; }
RepresentationConversionService& Kernel::convert() { return conversion_service_; }
IOService& Kernel::io() { return io_service_; }
DiagnosticService& Kernel::diagnostics() { return diagnostic_service_; }
EvalGraphService& Kernel::eval_graph() { return eval_graph_service_; }
PluginRegistry& Kernel::plugins() { return state_->plugin_registry; }

}  // namespace axiom
