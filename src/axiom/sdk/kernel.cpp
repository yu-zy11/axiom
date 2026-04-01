#include "axiom/sdk/kernel.h"

#include "axiom/diag/error_codes.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/plugin/plugin_sdk_version.h"
#include "axiom/internal/sdk/kernel_plugin_helpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace axiom {
using namespace kernel_plugin_helpers;

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
      eval_graph_service_(state_) {}

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
  if (value <= 0.0) {
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
  if (value <= 0.0) {
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
  return ok_void(state_->create_diagnostic("已重置运行时存储"));
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
