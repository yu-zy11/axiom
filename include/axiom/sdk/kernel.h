#pragma once

#include <memory>
#include <vector>

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
    Result<std::uint64_t> next_object_id() const;
    Result<std::uint64_t> next_diagnostic_id() const;
    Result<std::uint64_t> object_count_total() const;
    Result<std::uint64_t> geometry_count() const;
    Result<std::uint64_t> topology_count() const;
    Result<std::uint64_t> body_count() const;
    Result<std::uint64_t> mesh_count() const;
    Result<std::uint64_t> intersection_count() const;
    Result<std::uint64_t> eval_node_count() const;
    Result<std::uint64_t> cache_entry_count() const;
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
    /// 插件修改或产出 `Body` 后建议调用，语义同 `validate().validate_all`（统一验证入口）。
    Result<void> validate_after_plugin_mutation(BodyId body_id, ValidationMode mode = ValidationMode::Standard);
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
    /// Trim bridge：由面外环 PCurve 推导底层曲面 UV 包围盒并创建修剪面；若可串联外环 UV 折线则写入 `trim_uv_loop`（多边形修剪），否则退回纯轴对齐盒。
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
