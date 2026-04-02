#pragma once

#include <memory>
#include <vector>

#include "axiom/core/types.h"
#include "axiom/diag/diagnostic_service.h"
#include "axiom/eval/eval_services.h"
#include "axiom/geo/geometry_services.h"
#include "axiom/heal/heal_services.h"
#include "axiom/io/io_service.h"
#include "axiom/math/math_services.h"
#include "axiom/ops/ops_services.h"
#include "axiom/rep/representation_conversion_service.h"
#include "axiom/topo/topology_service.h"
#include "axiom/plugin/plugin_registry.h"

namespace axiom {
namespace detail {
struct KernelState;
}

/// 与 `Kernel::reset_runtime_stores()` 清空范围一致的可观测计数（不含曲线/曲面/拓扑/体等持久对象存储）。
struct KernelRuntimeStoreCounts {
    std::uint64_t mesh_records{};
    std::uint64_t tessellation_cache_entries{};
    std::uint64_t face_tessellation_cache_entries{};
    std::uint64_t intersection_records{};
    std::uint64_t curve_eval_cache_entries{};
    std::uint64_t surface_eval_cache_entries{};
    std::uint64_t eval_node_records{};
    std::uint64_t diagnostic_reports{};
    /// `TopologyQueryService` 顶层查询累计次数（仅统计，与拓扑数据无关）。
    std::uint64_t topology_query_op_count{};
    /// 与 `KernelState::tessellation_cache_stats` 一致；`clear_mesh_store` / `reset_runtime_stores` 时归零。
    TessellationCacheStats tessellation_metrics{};
};

/// 拓扑事务成功提交的累计审计（`KernelState` 持久字段；`reset_runtime_stores` 不清空）。
struct KernelTopologyCommitAudit {
    VersionId last_committed_version{};
    std::uint64_t last_commit_write_operations{};
    std::uint64_t committed_transaction_count{};
    std::uint64_t committed_write_operations_total{};
    /// 最近一次成功提交对应的分项（与当时 `TopologyTransaction::*_count` 一致）。
    TopologyCommitWriteBreakdown last_commit_write_breakdown{};
    /// 历次成功提交的分项之和（空提交对应全零行）。
    TopologyCommitWriteBreakdown cumulative_commit_write_breakdown{};
};

/// EvalGraph 侧聚合指标（与 `eval_nodes` / 失效与重算计数表一致；`reset_runtime_stores` 会清空图侧存储与 `EvalGraphTelemetry`）。
struct KernelEvalGraphMetrics {
    std::uint64_t node_count{};
    std::uint64_t invalid_node_count{};
    std::uint64_t recompute_events_total{};
    /// `eval_recompute_count` 中单节点重算次数的最大值（与 `max_recompute_count_node` 一致口径）。
    std::uint64_t max_per_node_recompute_count{};
    /// 重算计数大于 0 的节点数（传递规模与活跃重算门禁）。
    std::uint64_t nodes_with_recompute_nonzero{};
    /// `recompute_events_total / max(1, node_count)`，便于 CI 对比平均重算压力。
    double mean_recompute_events_per_node{0.0};
    /// `recompute_events_total / max(1, nodes_with_recompute_nonzero)`：仅统计「曾发生过重算」的节点，用于门禁「活跃子图重算深度」。
    double mean_recompute_events_per_touched_node{0.0};
    /// `eval_dependencies` 条目数（有显式出边的 `from` 节点）。
    std::uint64_t dependency_from_node_count{};
    /// 依赖边总数（各 `from` 的 `to` 列表长度之和）。
    std::uint64_t dependency_edge_count{};
    /// `eval_body_bindings` 中有绑定列表的体数。
    std::uint64_t body_binding_record_count{};
    /// 体→节点绑定列表长度之和（同一体多节点则大于 `body_binding_record_count`）。
    std::uint64_t body_binding_reference_total{};
    /// 与 `EvalGraphService::telemetry()` 同源；便于宿主/CI 单点读取而不必直连服务。
    EvalGraphTelemetry telemetry{};
    /// 与 `KernelState::eval_invalidation_bridge` 同源；统计内部传播入口（含非 SDK 直连路径）。
    EvalInvalidationBridgeMetrics invalidation_bridge{};
};

class Kernel {
public:
    explicit Kernel(const KernelConfig& config = {});
    Result<KernelConfig> config() const;
    Result<void> set_enable_diagnostics(bool enabled);
    Result<bool> enable_diagnostics() const;
    Result<void> set_linear_tolerance(Scalar value);
    Result<Scalar> linear_tolerance() const;
    Result<void> set_angular_tolerance(Scalar value);
    Result<Scalar> angular_tolerance() const;
    Result<void> set_enable_cache(bool enabled);
    Result<bool> enable_cache() const;
    Result<void> set_precision_mode(PrecisionMode mode);
    Result<PrecisionMode> precision_mode() const;
    /// 下一次成功 `TopologyTransaction::commit()` 将分配的 `VersionId`（与 `preview_commit_version` 语义一致）。
    Result<VersionId> topology_version_next() const;
    /// 线性/角度容差等为有限正数（与 `set_*_tolerance` 合法输入一致）；供 CI/宿主启动自检。
    Result<bool> kernel_config_numeric_wellformed() const;
    /// `topology_commit_success_count` 与 `next_version`、上次提交版本及累计写计数自洽（仅基于 `KernelState` 字段）。
    Result<bool> topology_version_audit_consistent() const;
    /// `eval_nodes` 与 `eval_labels`/`eval_invalid`/`eval_recompute_count`、依赖表及体绑定中的节点 id 一致；且 `eval_dependencies` 与 `eval_reverse_dependencies` 互为逆索引（不校验 `BodyId` 是否仍存在于 `bodies`）。
    Result<bool> eval_graph_store_maps_consistent() const;
    /// 数值配置合法、拓扑版本审计、EvalGraph 存储映射与三角化缓存一致性均通过。
    Result<bool> core_runtime_invariants_hold() const;
    /// 成功提交的版本与写操作累计快照；可与 `topology_version_next()` 对照做门禁。
    Result<KernelTopologyCommitAudit> topology_commit_audit() const;
    /// 节点数、当前失效节点数、`eval_recompute_count` 全表之和（建模—Eval 联合可观测）。
    Result<KernelEvalGraphMetrics> eval_graph_metrics() const;
    /// `tessellation_cache` / `face_tessellation_cache` 中引用的 `MeshId` 均在 `meshes` 中存在。
    Result<bool> runtime_tessellation_caches_consistent() const;
    Result<std::uint64_t> next_object_id() const;
    Result<std::uint64_t> next_diagnostic_id() const;
    Result<std::uint64_t> object_count_total() const;
    Result<std::uint64_t> geometry_count() const;
    Result<std::uint64_t> topology_count() const;
    Result<std::uint64_t> body_count() const;
    Result<std::uint64_t> mesh_count() const;
    Result<std::uint64_t> intersection_count() const;
    Result<std::uint64_t> eval_node_count() const;
    /// 曲线/曲面 **几何求值** 缓存条目（不含 `tessellation_cache` / `face_tessellation_cache`）；完整运行时缓存见 `runtime_store_counts()`。
    Result<std::uint64_t> cache_entry_count() const;
    Result<TessellationCacheStats> tessellation_cache_stats() const;
    Result<void> export_tessellation_cache_stats_json(std::string_view path) const;
    Result<void> export_round_trip_report_json(const RoundTripReport& report, std::string_view path) const;
    Result<ConversionErrorBudget> conversion_error_budget_for_tessellation(const TessellationOptions& options) const;
    Result<void> export_conversion_error_budget_json(const TessellationOptions& options, std::string_view path) const;
    /// 将 `topology_commit_audit()` 快照写成单行 JSON（供 CI/工具解析）；路径为空或无法打开文件时失败。
    Result<void> export_topology_commit_audit_json(std::string_view path) const;
    /// 将 `eval_graph_metrics()` 快照写成单行 JSON（含 `telemetry` 子对象）。
    Result<void> export_eval_graph_metrics_json(std::string_view path) const;
    /// 合并导出：`topology_version_next`、`topology_commit_audit`、`eval_graph_metrics`、`runtime_store_counts`（字段与分项 JSON 一致，供 CI 单文件归档）。
    Result<void> export_runtime_observability_json(std::string_view path) const;
    /// 剔除三角化缓存中指向已删除 `MeshId` 的条目；累计 `body_cache_stale_evictions` / `face_cache_stale_evictions`（与 Rep 路径 stale 语义一致）。
    Result<std::uint64_t> prune_stale_tessellation_cache_entries();
    /// 不写入诊断存储，便于在 `reset_runtime_stores()` 后立即做不变量断言。
    Result<KernelRuntimeStoreCounts> runtime_store_counts() const;
    Result<void> clear_eval_caches();
    Result<void> clear_curve_eval_cache();
    Result<void> clear_surface_eval_cache();
    Result<void> clear_diagnostics_store();
    Result<void> clear_intersections_store();
    Result<void> clear_mesh_store();
    Result<bool> has_body_id(BodyId id) const;
    Result<bool> has_curve_id(CurveId id) const;
    Result<bool> has_pcurve_id(PCurveId id) const;
    Result<bool> has_surface_id(SurfaceId id) const;
    Result<bool> has_face_id(FaceId id) const;
    Result<bool> has_shell_id(ShellId id) const;
    Result<bool> has_edge_id(EdgeId id) const;
    Result<void> reset_runtime_stores();
    // capability and availability
    Result<std::vector<std::string>> services_available() const;
    Result<std::vector<std::string>> module_names() const;
    Result<bool> has_service_geometry() const;
    Result<bool> has_service_topology() const;
    Result<bool> has_service_io() const;
    Result<bool> has_service_diagnostics() const;
    Result<bool> has_service_eval() const;
    Result<bool> has_service_ops() const;
    Result<bool> has_service_heal() const;
    Result<bool> has_service_representation() const;
    Result<bool> has_service_conversion() const;
    Result<bool> has_service_math() const;
    Result<bool> has_service_geo() const;
    Result<std::vector<std::string>> io_supported_formats() const;
    Result<bool> io_can_import_format(std::string_view fmt) const;
    Result<bool> io_can_export_format(std::string_view fmt) const;
    Result<std::vector<std::string>> capability_report_lines() const;
    Result<std::string> capability_report_txt() const;
    // plugin views via kernel
    Result<std::vector<std::string>> plugin_manifest_names() const;
    Result<std::uint64_t> plugin_total_count() const;
    Result<bool> has_any_plugins() const;
    Result<std::vector<std::string>> plugin_vendors() const;
    Result<std::vector<std::string>> plugin_capabilities_histogram_lines() const;
    /// 插件清单聚合后的能力标签（去重排序由注册表定义）。
    Result<std::vector<std::string>> plugin_capabilities() const;
    /// 稳定门面：进程内插件 SDK 与清单/能力摘要（用于能力发现与 CI 快照）。
    Result<std::string> plugin_sdk_api_version() const;
    Result<std::vector<std::string>> plugin_discovery_report_lines() const;
    Result<PluginHostPolicy> plugin_host_policy() const;
    Result<void> set_plugin_host_policy(PluginHostPolicy policy);
    Result<bool> has_service_plugin_registry() const;
    /// 与 `services_available()` 中的 `plugin.discovery` 一致。
    Result<bool> has_service_plugin_discovery() const;
    /// 与 `services_available()` 中的 `plugin.import` 一致（由 `Kernel::plugin_import_file` 提供宿主封装）。
    Result<bool> has_service_plugin_import() const;
    /// 与 `services_available()` 中的 `plugin.export` 一致（由 `Kernel::plugin_export_file` 提供宿主封装）。
    Result<bool> has_service_plugin_export() const;
    /// 与 `services_available()` 中的 `plugin.repair` 一致（由 `Kernel::plugin_run_repair` 提供宿主封装）。
    Result<bool> has_service_plugin_repair() const;
    /// 与 `services_available()` 中的 `plugin.curve` 一致（由 `Kernel::plugin_create_curve` 提供宿主封装）。
    Result<bool> has_service_plugin_curve() const;
    /// 与 `services_available()` 中的 `plugin.verify_curve` 一致（显式曲线一致性校验，见 `verify_after_plugin_curve`）。
    Result<bool> has_service_plugin_verify_curve() const;
    /// 能力发现 JSON 快照（供 CI/工具解析）；语义与 `plugin_discovery_report_lines` 对齐。
    Result<std::string> plugin_discovery_report_json() const;
    /// 注册失败且 `enable_diagnostics` 为真时写入诊断存储并返回非零 `diagnostic_id`。
    Result<void> register_plugin_curve(const PluginManifest& manifest, std::unique_ptr<ICurvePlugin> plugin);
    Result<void> register_plugin_repair(const PluginManifest& manifest, std::unique_ptr<IRepairPlugin> plugin);
    Result<void> register_plugin_importer(const PluginManifest& manifest, std::unique_ptr<IImporterPlugin> plugin);
    Result<void> register_plugin_exporter(const PluginManifest& manifest, std::unique_ptr<IExporterPlugin> plugin);
    Result<void> register_plugin_manifest_only(const PluginManifest& manifest);
    /// 注销失败且 `enable_diagnostics` 为真时写入诊断存储并返回非零 `diagnostic_id`。
    Result<void> unregister_plugin_curve(std::string_view implementation_type_name);
    Result<void> unregister_plugin_repair(std::string_view implementation_type_name);
    Result<void> unregister_plugin_importer(std::string_view implementation_type_name);
    Result<void> unregister_plugin_exporter(std::string_view implementation_type_name);
    /// 按清单 `name` 删除条目；未删除任何条目不成功。
    Result<void> unregister_plugin_manifest(std::string_view manifest_name);
    /// 各清单声明的 `plugin_api_version` 与当前内核期望的对比（供 CI/门禁；不修改注册表）。
    Result<std::vector<std::string>> plugin_api_compatibility_report_lines() const;
    /// 对插件注册表内全部清单做策略校验（与 `PluginRegistry::validate_all_manifests` 一致）；宿主策略变更后可据此批量预检。
    Result<void> validate_plugin_manifests(std::vector<std::string>* failure_details = nullptr) const;
    /// 插件修改或产出 `Body` 后建议调用，语义同 `validate().validate_all`（统一验证入口）。
    Result<void> validate_after_plugin_mutation(BodyId body_id, ValidationMode mode = ValidationMode::Standard);
    /// 在直接调用 `PluginRegistry::invoke_registered_curve` 等绕过 `plugin_create_curve` 的路径上，用于与 `plugin_create_curve`（在开启 `auto_verify_curve_after_plugin_curve` 时）相同的宿主一致性校验：`CurveId` 已注册且 `curve_service().domain` 为合法开区间（允许无穷界）。
    Result<void> verify_after_plugin_curve(CurveId curve_id);
    /// 调用已注册导入插件；若 `PluginHostPolicy::auto_validate_body_after_plugin_importer` 为真，成功后对返回体做 `validate_all`。
    /// 验证失败时返回非 Ok 且通常不带 `value`（实体仍可能已创建，需结合诊断与场景清理）。
    Result<BodyId> plugin_import_file(std::string_view implementation_type_name, std::string_view path,
                                      ValidationMode validation_mode = ValidationMode::Standard);
    /// 调用已注册导出插件；若 `PluginHostPolicy::auto_validate_body_before_plugin_exporter` 为真，先对 `BodyId` 做 `validate_all`。
    Result<void> plugin_export_file(std::string_view implementation_type_name, BodyId body_id, std::string_view path,
                                    ValidationMode validation_mode = ValidationMode::Standard);
    /// 调用已注册修复插件；若 `PluginHostPolicy::auto_validate_body_after_plugin_repair` 为真，在插件返回 Ok 后对结果体做 `validate_all`（见策略注释）。
    /// 验证失败时返回非 Ok 且通常不带 `value`（修复可能已生效，需结合诊断与场景处理）。
    Result<OpReport> plugin_run_repair(std::string_view implementation_type_name, BodyId body_id, RepairMode repair_mode,
                                       ValidationMode validation_mode = ValidationMode::Standard);
    /// 调用已注册曲线插件；若 `PluginHostPolicy::auto_verify_curve_after_plugin_curve` 为真，成功后校验 `CurveId` 已注册且 `curve_service().domain` 为合法开区间。
    /// 校验失败时返回非 Ok 且通常不带 `value`（曲线记录仍可能存在，需结合诊断处理）。
    Result<CurveId> plugin_create_curve(std::string_view implementation_type_name, PluginCurveDesc desc);
    /// Trim bridge：查询底层基曲面与外环 UV 盒；若外环可串联为折线（≥3 点）则先 `validate_face_trim_consistency`，再创建带 `trim_uv_loop` 及内环 `trim_uv_holes` 的修剪面，否则退回纯轴对齐盒。
    Result<SurfaceId> create_trimmed_surface_from_face_outer_loop_pcurves(FaceId face_id);

    CurveFactory& curves();
    PCurveFactory& pcurves();
    SurfaceFactory& surfaces();
    CurveService& curve_service();
    PCurveService& pcurve_service();
    SurfaceService& surface_service();
    GeometryTransformService& geometry_transform();
    GeometryIntersectionService& geometry_intersection();
    LinearAlgebraService& linear_algebra();
    PredicateService& predicates();
    ToleranceService& tolerance();
    TopologyService& topology();
    PrimitiveService& primitives();
    SweepService& sweeps();
    BooleanService& booleans();
    ModifyService& modify();
    BlendService& blends();
    QueryService& query();
    ValidationService& validate();
    RepairService& repair();
    RepresentationService& representation();
    RepresentationConversionService& convert();
    IOService& io();
    DiagnosticService& diagnostics();
    EvalGraphService& eval_graph();
    PluginRegistry& plugins();

private:
    std::shared_ptr<detail::KernelState> state_;
    CurveFactory curve_factory_;
    PCurveFactory pcurve_factory_;
    SurfaceFactory surface_factory_;
    CurveService curve_service_;
    PCurveService pcurve_service_;
    SurfaceService surface_service_;
    GeometryTransformService geometry_transform_service_;
    GeometryIntersectionService geometry_intersection_service_;
    LinearAlgebraService linear_algebra_service_;
    PredicateService predicate_service_;
    ToleranceService tolerance_service_;
    TopologyService topology_service_;
    PrimitiveService primitive_service_;
    SweepService sweep_service_;
    BooleanService boolean_service_;
    ModifyService modify_service_;
    BlendService blend_service_;
    QueryService query_service_;
    ValidationService validation_service_;
    RepairService repair_service_;
    RepresentationService representation_service_;
    RepresentationConversionService conversion_service_;
    IOService io_service_;
    DiagnosticService diagnostic_service_;
    EvalGraphService eval_graph_service_;
};

}  // namespace axiom
