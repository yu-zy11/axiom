#include "axiom/sdk/kernel.h"

#include "axiom/diag/error_codes.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/sdk/kernel_plugin_helpers.h"
#include "axiom/plugin/plugin_registry.h"
#include "axiom/plugin/plugin_sdk_version.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace axiom {
namespace {

Result<void> plugin_curve_host_consistency_core(detail::KernelState& st, CurveService& cs, CurveId cid) {
  if (!detail::has_curve(st, cid)) {
    return error_void(StatusCode::InvalidInput, {}, {});
  }
  const auto dom = cs.domain(cid);
  if (dom.status != StatusCode::Ok || !dom.value.has_value()) {
    return error_void(dom.status, dom.diagnostic_id, std::move(dom.warnings));
  }
  const auto& d = *dom.value;
  if (!std::isless(d.min, d.max)) {
    return error_void(StatusCode::OperationFailed, {}, {});
  }
  return ok_void({});
}

}  // namespace

using namespace kernel_plugin_helpers;

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
  const auto vendors = state_->plugin_registry.vendor_names();
  if (vendors.status != StatusCode::Ok || !vendors.value.has_value()) {
    return error_result<std::vector<std::string>>(vendors.status, vendors.diagnostic_id);
  }
  return ok_result(*vendors.value, state_->create_diagnostic("已查询插件厂商"));
}

Result<std::vector<std::string>> Kernel::plugin_capabilities() const {
  const auto caps = state_->plugin_registry.all_capabilities();
  if (caps.status != StatusCode::Ok || !caps.value.has_value()) {
    return error_result<std::vector<std::string>>(caps.status, caps.diagnostic_id);
  }
  return ok_result(*caps.value, state_->create_diagnostic("已查询插件能力标签"));
}

Result<std::vector<std::string>> Kernel::plugin_capabilities_histogram_lines() const {
  const auto hist = state_->plugin_registry.capabilities_histogram();
  if (hist.status != StatusCode::Ok || !hist.value.has_value()) return error_result<std::vector<std::string>>(hist.status, hist.diagnostic_id);
  std::vector<std::string> lines;
  for (const auto& kv : *hist.value) lines.push_back(kv.first + "=" + std::to_string(kv.second));
  std::sort(lines.begin(), lines.end());
  return ok_result(std::move(lines), state_->create_diagnostic("已生成插件能力直方"));
}

Result<std::string> Kernel::plugin_sdk_api_version() const {
  return ok_result(std::string(kPluginSdkApiVersion), state_->create_diagnostic("已查询插件 SDK API 版本"));
}

Result<std::vector<std::string>> Kernel::plugin_discovery_report_lines() const {
  std::vector<std::string> lines;
  const auto api = plugin_sdk_api_version();
  if (api.status == StatusCode::Ok && api.value.has_value()) {
    lines.push_back("plugin.sdk_api=" + *api.value);
  }
  const auto pol = state_->config.plugin_host_policy;
  lines.push_back(std::string("plugin.host.max_slots=") +
                  (pol.max_plugin_slots == 0 ? "unlimited" : std::to_string(pol.max_plugin_slots)));
  lines.push_back(std::string("plugin.host.unique_impl_type=") +
                  (pol.enforce_unique_implementation_type_name ? "true" : "false"));
  lines.push_back(std::string("plugin.host.require_manifest_name=") +
                  (pol.require_non_empty_manifest_name ? "true" : "false"));
  lines.push_back(std::string("plugin.host.unique_manifest_name=") +
                  (pol.require_unique_manifest_name ? "true" : "false"));
  lines.push_back(std::string("plugin.host.require_capabilities=") +
                  (pol.require_non_empty_capabilities ? "true" : "false"));
  lines.push_back(std::string("plugin.host.require_impl_type_name=") +
                  (pol.require_non_empty_plugin_type_name ? "true" : "false"));
  lines.push_back(std::string("plugin.host.require_plugin_api_version_match=") +
                  (pol.require_plugin_api_version_match ? "true" : "false"));
  lines.push_back(std::string("plugin.host.plugin_api_version_match_mode=") +
                  plugin_api_version_match_mode_tag(pol.plugin_api_version_match_mode));
  lines.push_back(std::string("plugin.expected_sdk_api=") + std::string(kPluginSdkApiVersion));
  lines.push_back(std::string("plugin.host.sandbox_level=") + plugin_sandbox_level_tag(pol.sandbox_level));
  lines.push_back(std::string("plugin.host.auto_validate_after_plugin_importer=") +
                  (pol.auto_validate_body_after_plugin_importer ? "true" : "false"));
  lines.push_back(std::string("plugin.host.auto_validate_before_plugin_exporter=") +
                  (pol.auto_validate_body_before_plugin_exporter ? "true" : "false"));
  const auto slots = state_->plugin_registry.total_plugin_slots();
  const auto mc = state_->plugin_registry.manifest_count();
  if (slots.status == StatusCode::Ok && slots.value.has_value()) {
    lines.push_back("plugin.slots=" + std::to_string(*slots.value));
  }
  if (mc.status == StatusCode::Ok && mc.value.has_value()) {
    lines.push_back("plugin.manifest_entries=" + std::to_string(*mc.value));
  }
  const auto caps = state_->plugin_registry.all_capabilities();
  if (caps.status == StatusCode::Ok && caps.value.has_value()) {
    lines.push_back("plugin.capabilities.count=" + std::to_string(caps.value->size()));
    for (const auto& c : *caps.value) lines.push_back(std::string("plugin.capability.") + c);
  }
  const auto types = state_->plugin_registry.plugin_types_present();
  if (types.status == StatusCode::Ok && types.value.has_value()) {
    for (const auto& t : *types.value) lines.push_back(std::string("plugin.kind.") + t);
  }
  const auto io_fmts = state_->plugin_registry.infer_supported_io_formats();
  if (io_fmts.status == StatusCode::Ok && io_fmts.value.has_value()) {
    lines.push_back("plugin.io_formats_inferred.count=" + std::to_string(io_fmts.value->size()));
    for (const auto& f : *io_fmts.value) lines.push_back(std::string("plugin.io_format.") + f);
  }
  std::vector<Issue> issues;
  issues.push_back(Issue {std::string(diag_codes::kPluginDiscoveryReport), IssueSeverity::Info,
                          "插件能力发现快照", {}, {}});
  return ok_result(std::move(lines),
                   state_->create_diagnostic("已生成插件能力发现报告", std::move(issues)));
}

Result<PluginHostPolicy> Kernel::plugin_host_policy() const {
  return ok_result(state_->config.plugin_host_policy, state_->create_diagnostic("已查询插件宿主策略"));
}

Result<void> Kernel::set_plugin_host_policy(PluginHostPolicy policy) {
  state_->config.plugin_host_policy = std::move(policy);
  state_->plugin_registry.set_host_policy(state_->config.plugin_host_policy);
  return ok_void(state_->create_diagnostic("已设置插件宿主策略"));
}

Result<bool> Kernel::has_service_plugin_registry() const {
  return ok_result(true, state_->create_diagnostic("已查询插件注册表服务可用性"));
}

Result<bool> Kernel::has_service_plugin_discovery() const {
  return ok_result(true, state_->create_diagnostic("已查询插件能力发现服务可用性"));
}

Result<bool> Kernel::has_service_plugin_import() const {
  return ok_result(true, state_->create_diagnostic("已查询插件宿主导入封装可用性"));
}

Result<bool> Kernel::has_service_plugin_export() const {
  return ok_result(true, state_->create_diagnostic("已查询插件宿主导出封装可用性"));
}

Result<bool> Kernel::has_service_plugin_repair() const {
  return ok_result(true, state_->create_diagnostic("已查询插件宿主修复封装可用性"));
}

Result<std::string> Kernel::plugin_discovery_report_json() const {
  const auto api = plugin_sdk_api_version();
  if (api.status != StatusCode::Ok || !api.value.has_value()) {
    return error_result<std::string>(api.status, api.diagnostic_id);
  }
  const auto& pol = state_->config.plugin_host_policy;
  const auto slots = state_->plugin_registry.total_plugin_slots();
  const auto mc = state_->plugin_registry.manifest_count();
  const auto caps = state_->plugin_registry.all_capabilities();
  const auto kinds = state_->plugin_registry.plugin_types_present();
  const auto fmts = state_->plugin_registry.infer_supported_io_formats();
  const auto manlist = state_->plugin_registry.manifests_sorted_by_name();
  if (slots.status != StatusCode::Ok || !slots.value.has_value() || mc.status != StatusCode::Ok ||
      !mc.value.has_value() || caps.status != StatusCode::Ok || !caps.value.has_value() ||
      kinds.status != StatusCode::Ok || !kinds.value.has_value() || fmts.status != StatusCode::Ok ||
      !fmts.value.has_value() || manlist.status != StatusCode::Ok || !manlist.value.has_value()) {
    return error_result<std::string>(StatusCode::OperationFailed, {});
  }
  std::ostringstream o;
  o << '{';
  o << "\"sdk_api\":\"" << json_escape_plugin_field(*api.value) << "\",";
  o << "\"host_policy\":{";
  o << "\"max_plugin_slots\":";
  if (pol.max_plugin_slots == 0) {
    o << "null";
  } else {
    o << static_cast<unsigned long long>(pol.max_plugin_slots);
  }
  o << ",\"enforce_unique_implementation_type_name\":" << (pol.enforce_unique_implementation_type_name ? "true" : "false")
    << ",\"require_non_empty_manifest_name\":" << (pol.require_non_empty_manifest_name ? "true" : "false")
    << ",\"require_unique_manifest_name\":" << (pol.require_unique_manifest_name ? "true" : "false")
    << ",\"require_non_empty_capabilities\":" << (pol.require_non_empty_capabilities ? "true" : "false")
    << ",\"require_non_empty_plugin_type_name\":" << (pol.require_non_empty_plugin_type_name ? "true" : "false")
    << ",\"require_plugin_api_version_match\":" << (pol.require_plugin_api_version_match ? "true" : "false")
    << ",\"plugin_api_version_match_mode\":\"" << json_escape_plugin_field(plugin_api_version_match_mode_tag(pol.plugin_api_version_match_mode))
    << "\""
    << ",\"expected_plugin_sdk_api\":\"" << json_escape_plugin_field(std::string(kPluginSdkApiVersion)) << "\""
    << ",\"sandbox_level\":\"" << json_escape_plugin_field(plugin_sandbox_level_tag(pol.sandbox_level)) << "\""
    << ",\"auto_validate_body_after_plugin_importer\":" << (pol.auto_validate_body_after_plugin_importer ? "true" : "false")
    << ",\"auto_validate_body_before_plugin_exporter\":" << (pol.auto_validate_body_before_plugin_exporter ? "true" : "false")
    << ",\"auto_validate_body_after_plugin_repair\":" << (pol.auto_validate_body_after_plugin_repair ? "true" : "false")
    << ",\"auto_verify_curve_after_plugin_curve\":" << (pol.auto_verify_curve_after_plugin_curve ? "true" : "false")
    << "},";
  o << "\"slots\":" << *slots.value << ",\"manifest_entries\":" << *mc.value << ",\"capabilities\":[";
  for (std::size_t i = 0; i < caps.value->size(); ++i) {
    if (i > 0) {
      o << ',';
    }
    o << '"' << json_escape_plugin_field((*caps.value)[i]) << '"';
  }
  o << "],\"plugin_kinds\":[";
  for (std::size_t i = 0; i < kinds.value->size(); ++i) {
    if (i > 0) {
      o << ',';
    }
    o << '"' << json_escape_plugin_field((*kinds.value)[i]) << '"';
  }
  o << "],\"io_formats_inferred\":[";
  for (std::size_t i = 0; i < fmts.value->size(); ++i) {
    if (i > 0) {
      o << ',';
    }
    o << '"' << json_escape_plugin_field((*fmts.value)[i]) << '"';
  }
  o << "],\"manifests\":[";
  for (std::size_t i = 0; i < manlist.value->size(); ++i) {
    if (i > 0) {
      o << ',';
    }
    const auto& mm = (*manlist.value)[i];
    const auto impl = trim_plugin_field(mm.implementation_type_name);
    o << "{\"name\":\"" << json_escape_plugin_field(mm.name) << "\",\"implementation_type_name\":\""
      << json_escape_plugin_field(std::string(impl)) << "\"}";
  }
  o << "]}";
  std::string body = o.str();
  return ok_result(std::move(body), state_->create_diagnostic("已生成插件能力 JSON 快照"));
}

Result<void> Kernel::register_plugin_curve(const PluginManifest& manifest, std::unique_ptr<ICurvePlugin> plugin) {
  PluginManifest copy = manifest;
  auto r = state_->plugin_registry.register_curve_type(copy, std::move(plugin));
  return attach_plugin_registration_diag(*state_, std::move(r));
}

Result<void> Kernel::register_plugin_repair(const PluginManifest& manifest, std::unique_ptr<IRepairPlugin> plugin) {
  PluginManifest copy = manifest;
  auto r = state_->plugin_registry.register_repair_plugin(copy, std::move(plugin));
  return attach_plugin_registration_diag(*state_, std::move(r));
}

Result<void> Kernel::register_plugin_importer(const PluginManifest& manifest, std::unique_ptr<IImporterPlugin> plugin) {
  PluginManifest copy = manifest;
  auto r = state_->plugin_registry.register_importer(copy, std::move(plugin));
  return attach_plugin_registration_diag(*state_, std::move(r));
}

Result<void> Kernel::register_plugin_exporter(const PluginManifest& manifest, std::unique_ptr<IExporterPlugin> plugin) {
  PluginManifest copy = manifest;
  auto r = state_->plugin_registry.register_exporter(copy, std::move(plugin));
  return attach_plugin_registration_diag(*state_, std::move(r));
}

Result<void> Kernel::register_plugin_manifest_only(const PluginManifest& manifest) {
  PluginManifest copy = manifest;
  auto r = state_->plugin_registry.register_manifest_only(copy);
  return attach_plugin_registration_diag(*state_, std::move(r));
}

Result<void> Kernel::unregister_plugin_curve(std::string_view implementation_type_name) {
  auto r = state_->plugin_registry.unregister_curve_type(implementation_type_name);
  return attach_plugin_unregistration_diag(*state_, std::move(r));
}

Result<void> Kernel::unregister_plugin_repair(std::string_view implementation_type_name) {
  auto r = state_->plugin_registry.unregister_repair_plugin(implementation_type_name);
  return attach_plugin_unregistration_diag(*state_, std::move(r));
}

Result<void> Kernel::unregister_plugin_importer(std::string_view implementation_type_name) {
  auto r = state_->plugin_registry.unregister_importer(implementation_type_name);
  return attach_plugin_unregistration_diag(*state_, std::move(r));
}

Result<void> Kernel::unregister_plugin_exporter(std::string_view implementation_type_name) {
  auto r = state_->plugin_registry.unregister_exporter(implementation_type_name);
  return attach_plugin_unregistration_diag(*state_, std::move(r));
}

Result<void> Kernel::unregister_plugin_manifest(std::string_view manifest_name) {
  const auto key = trim_plugin_field(manifest_name);
  if (key.empty()) {
    return attach_plugin_unregistration_diag(
        *state_,
        error_void(StatusCode::InvalidInput, {},
                   {Warning {std::string(diag_codes::kPluginCapabilityIncomplete), "注销清单时 name 为空或仅空白"}}));
  }
  const auto removed = state_->plugin_registry.remove_manifest_by_name(key);
  if (removed.status != StatusCode::Ok) {
    return attach_plugin_unregistration_diag(*state_, error_void(removed.status, removed.diagnostic_id, {}));
  }
  if (!removed.value.has_value() || *removed.value == 0) {
    return attach_plugin_unregistration_diag(
        *state_,
        error_void(StatusCode::InvalidInput, {},
                   {Warning {std::string(diag_codes::kPluginNotRegistered), "未找到该名称的插件清单"}}));
  }
  return ok_void(state_->create_diagnostic("已注销插件清单"));
}

Result<std::vector<std::string>> Kernel::plugin_api_compatibility_report_lines() const {
  const auto sorted = state_->plugin_registry.manifests_sorted_by_name();
  if (sorted.status != StatusCode::Ok || !sorted.value.has_value()) {
    return error_result<std::vector<std::string>>(sorted.status, sorted.diagnostic_id);
  }
  const auto& pol = state_->config.plugin_host_policy;
  std::vector<std::string> lines;
  lines.reserve(sorted.value->size() + 2);
  lines.push_back(std::string("plugin.expected_sdk_api=") + std::string(kPluginSdkApiVersion));
  lines.push_back(std::string("plugin.api.compat.match_mode=") +
                  plugin_api_version_match_mode_tag(pol.plugin_api_version_match_mode));
  for (const auto& m : *sorted.value) {
    const auto declared = trim_plugin_field(m.plugin_api_version);
    std::string state;
    if (declared.empty()) {
      state = "unset";
    } else if (plugin_api_version_declared_compatible(declared, kPluginSdkApiVersion, pol.plugin_api_version_match_mode)) {
      state = "ok";
    } else {
      state = "mismatch";
    }
    std::string line = "plugin.api.compat manifest=" + m.name + " declared=";
    line += declared.empty() ? std::string("(none)") : std::string(declared);
    line += " expected=" + std::string(kPluginSdkApiVersion) + " state=" + state;
    lines.push_back(std::move(line));
  }
  return ok_result(std::move(lines), state_->create_diagnostic("已生成插件 API 兼容性报告"));
}

Result<void> Kernel::validate_after_plugin_mutation(BodyId body_id, ValidationMode mode) {
  return validate().validate_all(body_id, mode);
}

Result<BodyId> Kernel::plugin_import_file(std::string_view implementation_type_name, std::string_view path,
                                          ValidationMode validation_mode) {
  auto r = state_->plugin_registry.invoke_registered_importer(implementation_type_name, path);
  if (r.status != StatusCode::Ok || !r.value.has_value()) {
    return attach_plugin_import_body_diag(*state_, std::move(r));
  }
  const BodyId bid = *r.value;
  if (state_->config.plugin_host_policy.auto_validate_body_after_plugin_importer) {
    const auto v = validate().validate_all(bid, validation_mode);
    if (v.status != StatusCode::Ok) {
      return attach_plugin_post_import_validate_fail_diag(*state_, bid, v);
    }
  }
  return ok_result(bid, state_->create_diagnostic("插件导入完成"));
}

Result<void> Kernel::plugin_export_file(std::string_view implementation_type_name, BodyId body_id, std::string_view path,
                                         ValidationMode validation_mode) {
  if (!detail::has_body(*state_, body_id)) {
    return attach_plugin_export_body_diag(
        *state_, error_void(StatusCode::InvalidInput, {},
                            {Warning {std::string(diag_codes::kPluginExecutionFailure), "插件导出失败：Body 不存在"}}));
  }
  const auto& pol = state_->config.plugin_host_policy;
  if (pol.auto_validate_body_before_plugin_exporter) {
    const auto v = validate().validate_all(body_id, validation_mode);
    if (v.status != StatusCode::Ok) {
      return attach_plugin_pre_export_validate_fail_diag(*state_, body_id, v);
    }
  }
  auto r = state_->plugin_registry.invoke_registered_exporter(implementation_type_name, body_id, path);
  if (r.status != StatusCode::Ok) {
    return attach_plugin_export_body_diag(*state_, std::move(r));
  }
  return ok_void(state_->create_diagnostic("插件导出完成"));
}

Result<OpReport> Kernel::plugin_run_repair(std::string_view implementation_type_name, BodyId body_id, RepairMode repair_mode,
                                         ValidationMode validation_mode) {
  if (!detail::has_body(*state_, body_id)) {
    return attach_plugin_repair_exec_diag(
        *state_, error_result<OpReport>(StatusCode::InvalidInput, {},
                                        {Warning {std::string(diag_codes::kPluginExecutionFailure), "插件修复失败：Body 不存在"}}));
  }
  auto r = state_->plugin_registry.invoke_registered_repair(implementation_type_name, body_id, repair_mode);
  if (r.status != StatusCode::Ok || !r.value.has_value()) {
    return attach_plugin_repair_exec_diag(*state_, std::move(r));
  }
  OpReport report = *r.value;
  if (state_->config.plugin_host_policy.auto_validate_body_after_plugin_repair) {
    const BodyId target = plugin_repair_validation_body(*state_, body_id, report);
    const auto v = validate().validate_all(target, validation_mode);
    if (v.status != StatusCode::Ok) {
      return attach_plugin_post_repair_validate_fail_diag(*state_, target, v);
    }
  }
  return ok_result(std::move(report), state_->create_diagnostic("插件修复完成"));
}

Result<bool> Kernel::has_service_plugin_curve() const {
  return ok_result(true, state_->create_diagnostic("已查询插件曲线宿主封装可用性"));
}

Result<bool> Kernel::has_service_plugin_verify_curve() const {
  return ok_result(true, state_->create_diagnostic("已查询插件曲线显式一致性校验可用性"));
}

Result<void> Kernel::verify_after_plugin_curve(CurveId curve_id) {
  const auto v = plugin_curve_host_consistency_core(*state_, curve_service(), curve_id);
  if (v.status != StatusCode::Ok) {
    return attach_plugin_curve_consistency_verify_diag(*state_, curve_id, std::move(v));
  }
  return ok_void(state_->create_diagnostic("插件曲线一致性校验通过"));
}

Result<CurveId> Kernel::plugin_create_curve(std::string_view implementation_type_name, PluginCurveDesc desc) {
  auto r = state_->plugin_registry.invoke_registered_curve(implementation_type_name, desc);
  if (r.status != StatusCode::Ok || !r.value.has_value()) {
    return attach_plugin_curve_create_diag(*state_, std::move(r));
  }
  const CurveId cid = *r.value;
  if (state_->config.plugin_host_policy.auto_verify_curve_after_plugin_curve) {
    const auto v = plugin_curve_host_consistency_core(*state_, curve_service(), cid);
    if (v.status != StatusCode::Ok) {
      return attach_plugin_post_curve_verify_fail_diag(*state_, cid, std::move(v));
    }
  }
  return ok_result(cid, state_->create_diagnostic("插件曲线创建完成"));
}

}  // namespace axiom
