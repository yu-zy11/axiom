#include "axiom/sdk/kernel.h"

#include "axiom/internal/core/kernel_state.h"

namespace axiom {

Kernel::Kernel(const KernelConfig& config)
    : state_(std::make_shared<detail::KernelState>(config)),
      curve_factory_(state_),
      pcurve_factory_(state_),
      surface_factory_(state_),
      curve_service_(state_),
      pcurve_service_(state_),
      surface_service_(state_),
      geometry_transform_service_(state_),
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
  return ok_result(std::vector<std::string>{"step", "axmjson"},
                   state_->create_diagnostic("已查询IO支持格式"));
}

Result<bool> Kernel::io_can_import_format(std::string_view fmt) const {
  const auto fmts = io_supported_formats();
  if (fmts.status != StatusCode::Ok || !fmts.value.has_value()) return error_result<bool>(fmts.status, fmts.diagnostic_id);
  return ok_result(std::find(fmts.value->begin(), fmts.value->end(), fmt) != fmts.value->end(),
                   state_->create_diagnostic("已查询IO导入能力"));
}

Result<bool> Kernel::io_can_export_format(std::string_view fmt) const {
  return io_can_import_format(fmt);
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
  return ok_result(std::move(lines), state_->create_diagnostic("已生成能力报告行"));
}

Result<std::string> Kernel::capability_report_txt() const {
  const auto lines = capability_report_lines();
  if (lines.status != StatusCode::Ok || !lines.value.has_value()) return error_result<std::string>(lines.status, lines.diagnostic_id);
  std::string out;
  for (const auto& l : *lines.value) out += l + "\n";
  return ok_result(std::move(out), state_->create_diagnostic("已生成能力报告文本"));
}

Result<std::vector<std::string>> Kernel::plugin_manifest_names() const {
  const auto names = state_->plugin_registry.all_manifest_names();
  if (names.status != StatusCode::Ok) return error_result<std::vector<std::string>>(names.status, names.diagnostic_id);
  return ok_result(*names.value, state_->create_diagnostic("已查询插件清单名"));
}

Result<std::uint64_t> Kernel::plugin_total_count() const {
  const auto a = state_->plugin_registry.curve_plugin_count();
  const auto b = state_->plugin_registry.repair_plugin_count();
  const auto c = state_->plugin_registry.importer_count();
  const auto d = state_->plugin_registry.exporter_count();
  if (a.status != StatusCode::Ok || b.status != StatusCode::Ok || c.status != StatusCode::Ok || d.status != StatusCode::Ok) {
    return error_result<std::uint64_t>(StatusCode::OperationFailed, {});
  }
  return ok_result<std::uint64_t>(*a.value + *b.value + *c.value + *d.value, state_->create_diagnostic("已统计插件总数"));
}

Result<bool> Kernel::has_any_plugins() const {
  const auto e = state_->plugin_registry.empty();
  if (e.status != StatusCode::Ok || !e.value.has_value()) return error_result<bool>(e.status, e.diagnostic_id);
  return ok_result(!*e.value, state_->create_diagnostic("已查询是否存在插件"));
}

Result<std::vector<std::string>> Kernel::plugin_vendors() const {
  const auto names = state_->plugin_registry.all_manifest_names();
  (void)names;
  // Collect vendors from manifests
  std::vector<std::string> vendors;
  const auto manifests = state_->plugin_registry.find_by_capability(""); // empty -> gather all via manifests_ snapshot
  // find_by_capability with empty may not return all; fallback to manifest names then query each
  const auto all = state_->plugin_registry.manifest_count();
  (void)all;
  // Simpler: use all_manifest_names then state_->plugin_registry.find_manifest
  const auto mn = state_->plugin_registry.all_manifest_names();
  if (mn.status != StatusCode::Ok || !mn.value.has_value()) return error_result<std::vector<std::string>>(mn.status, mn.diagnostic_id);
  for (const auto& n : *mn.value) {
    const auto m = state_->plugin_registry.find_manifest(n);
    if (m.status == StatusCode::Ok && m.value.has_value()) {
      if (std::find(vendors.begin(), vendors.end(), m.value->vendor) == vendors.end()) vendors.push_back(m.value->vendor);
    }
  }
  std::sort(vendors.begin(), vendors.end());
  return ok_result(std::move(vendors), state_->create_diagnostic("已查询插件厂商"));
}

Result<std::vector<std::string>> Kernel::plugin_capabilities_histogram_lines() const {
  const auto hist = state_->plugin_registry.capabilities_histogram();
  if (hist.status != StatusCode::Ok || !hist.value.has_value()) return error_result<std::vector<std::string>>(hist.status, hist.diagnostic_id);
  std::vector<std::string> lines;
  for (const auto& kv : *hist.value) lines.push_back(kv.first + "=" + std::to_string(kv.second));
  std::sort(lines.begin(), lines.end());
  return ok_result(std::move(lines), state_->create_diagnostic("已生成插件能力直方"));
}
CurveFactory& Kernel::curves() { return curve_factory_; }
PCurveFactory& Kernel::pcurves() { return pcurve_factory_; }
SurfaceFactory& Kernel::surfaces() { return surface_factory_; }
CurveService& Kernel::curve_service() { return curve_service_; }
PCurveService& Kernel::pcurve_service() { return pcurve_service_; }
SurfaceService& Kernel::surface_service() { return surface_service_; }
GeometryTransformService& Kernel::geometry_transform() { return geometry_transform_service_; }
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
