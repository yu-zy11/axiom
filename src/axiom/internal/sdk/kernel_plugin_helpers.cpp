#include "axiom/internal/sdk/kernel_plugin_helpers.h"

#include "axiom/diag/error_codes.h"
#include "axiom/geo/geometry_services.h"
#include "axiom/internal/core/kernel_state.h"

#include <cctype>
#include <cmath>
#include <sstream>

namespace axiom {
namespace kernel_plugin_helpers {

std::string_view trim_plugin_field(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return s;
}

std::string json_escape_plugin_field(std::string_view s) {
  std::string r;
  r.reserve(s.size() + 8);
  for (unsigned char c : s) {
    if (c == '"') {
      r += "\\\"";
    } else if (c == '\\') {
      r += "\\\\";
    } else if (c < 0x20) {
      r += ' ';
    } else {
      r += static_cast<char>(c);
    }
  }
  return r;
}

const char* plugin_sandbox_level_tag(PluginSandboxLevel level) {
  switch (level) {
    case PluginSandboxLevel::None:
      return "none";
    case PluginSandboxLevel::Annotated:
      return "annotated";
    default:
      return "unknown";
  }
}

const char* plugin_api_version_match_mode_tag(PluginApiVersionMatchMode mode) {
  switch (mode) {
    case PluginApiVersionMatchMode::Exact:
      return "exact";
    case PluginApiVersionMatchMode::SameMinor:
      return "same_minor";
    case PluginApiVersionMatchMode::SameMajor:
      return "same_major";
    default:
      return "unknown";
  }
}

Result<void> attach_plugin_registration_diag(detail::KernelState& st, Result<void> r) {
  if (r.status == StatusCode::Ok) {
    return r;
  }
  if (!st.config.enable_diagnostics) {
    return r;
  }
  std::vector<Issue> issues;
  issues.reserve(r.warnings.size() + 1);
  for (const auto& w : r.warnings) {
    issues.push_back(Issue {w.code, IssueSeverity::Error, w.message, {}, {}});
  }
  if (issues.empty()) {
    issues.push_back(
        Issue {std::string(diag_codes::kPluginLoadFailure), IssueSeverity::Error, "插件注册失败", {}, {}});
  }
  const DiagnosticId id = st.create_diagnostic("插件注册失败", std::move(issues));
  return error_void(r.status, id, std::move(r.warnings));
}

Result<void> attach_plugin_unregistration_diag(detail::KernelState& st, Result<void> r) {
  if (r.status == StatusCode::Ok) {
    return r;
  }
  if (!st.config.enable_diagnostics) {
    return r;
  }
  std::vector<Issue> issues;
  issues.reserve(r.warnings.size() + 1);
  for (const auto& w : r.warnings) {
    issues.push_back(Issue {w.code, IssueSeverity::Error, w.message, {}, {}});
  }
  if (issues.empty()) {
    issues.push_back(
        Issue {std::string(diag_codes::kPluginLoadFailure), IssueSeverity::Error, "插件注销失败", {}, {}});
  }
  const DiagnosticId id = st.create_diagnostic("插件注销失败", std::move(issues));
  return error_void(r.status, id, std::move(r.warnings));
}

Result<CurveId> attach_plugin_curve_create_diag(detail::KernelState& st, Result<CurveId> r) {
  if (r.status == StatusCode::Ok) {
    return r;
  }
  if (!st.config.enable_diagnostics) {
    return r;
  }
  std::vector<Issue> issues;
  issues.reserve(r.warnings.size() + 1);
  for (const auto& w : r.warnings) {
    issues.push_back(Issue {w.code, IssueSeverity::Error, w.message, {}, {}});
  }
  if (issues.empty()) {
    issues.push_back(
        Issue {std::string(diag_codes::kPluginExecutionFailure), IssueSeverity::Error, "插件曲线创建失败", {}, {}});
  }
  const DiagnosticId id = st.create_diagnostic("插件曲线创建失败", std::move(issues));
  return error_result<CurveId>(r.status, id, std::move(r.warnings));
}

Result<CurveId> attach_plugin_post_curve_verify_fail_diag(detail::KernelState& st, CurveId curve_id,
                                                          Result<void> v) {
  if (!st.config.enable_diagnostics) {
    return error_result<CurveId>(v.status, v.diagnostic_id, std::move(v.warnings));
  }
  std::vector<Issue> issues;
  issues.push_back(Issue {std::string(diag_codes::kPluginResultValidationWarning), IssueSeverity::Error,
                          "插件曲线创建返回成功但宿主一致性校验未通过（CurveId 未注册或参数域无效）",
                          {curve_id.value}, "plugin.post_curve.verify"});
  const DiagnosticId id = st.create_diagnostic("插件曲线创建后校验未通过", std::move(issues));
  return error_result<CurveId>(v.status, id, std::move(v.warnings));
}

Result<void> attach_plugin_curve_consistency_verify_diag(detail::KernelState& st, CurveId curve_id, Result<void> v) {
  if (v.status == StatusCode::Ok) {
    return v;
  }
  if (!st.config.enable_diagnostics) {
    return v;
  }
  std::vector<Issue> issues;
  issues.push_back(Issue {std::string(diag_codes::kPluginResultValidationWarning), IssueSeverity::Error,
                          "显式宿主曲线一致性校验未通过（CurveId 未注册或参数域无效）",
                          {curve_id.value}, "plugin.curve.verify_explicit"});
  const DiagnosticId id = st.create_diagnostic("插件曲线一致性校验未通过", std::move(issues));
  return error_void(v.status, id, std::move(v.warnings));
}

Result<BodyId> attach_plugin_import_body_diag(detail::KernelState& st, Result<BodyId> r) {
  if (r.status == StatusCode::Ok) {
    return r;
  }
  if (!st.config.enable_diagnostics) {
    return r;
  }
  std::vector<Issue> issues;
  issues.reserve(r.warnings.size() + 1);
  for (const auto& w : r.warnings) {
    issues.push_back(Issue {w.code, IssueSeverity::Error, w.message, {}, {}});
  }
  if (issues.empty()) {
    issues.push_back(
        Issue {std::string(diag_codes::kPluginExecutionFailure), IssueSeverity::Error, "插件导入失败", {}, {}});
  }
  const DiagnosticId id = st.create_diagnostic("插件导入失败", std::move(issues));
  return error_result<BodyId>(r.status, id, std::move(r.warnings));
}

Result<BodyId> attach_plugin_post_import_validate_fail_diag(detail::KernelState& st, BodyId body_id,
                                                            Result<void> v) {
  if (!st.config.enable_diagnostics) {
    return error_result<BodyId>(v.status, v.diagnostic_id, std::move(v.warnings));
  }
  std::vector<Issue> issues;
  issues.push_back(Issue {std::string(diag_codes::kPluginResultValidationWarning), IssueSeverity::Error,
                          "插件导入成功但宿主全量验证未通过（Body 仍保留，可结合 validate / 诊断定位）",
                          {body_id.value}, "plugin.post_import.validate"});
  const DiagnosticId id = st.create_diagnostic("插件导入后验证未通过", std::move(issues));
  return error_result<BodyId>(v.status, id, std::move(v.warnings));
}

Result<void> attach_plugin_export_body_diag(detail::KernelState& st, Result<void> r) {
  if (r.status == StatusCode::Ok) {
    return r;
  }
  if (!st.config.enable_diagnostics) {
    return r;
  }
  std::vector<Issue> issues;
  issues.reserve(r.warnings.size() + 1);
  for (const auto& w : r.warnings) {
    issues.push_back(Issue {w.code, IssueSeverity::Error, w.message, {}, {}});
  }
  if (issues.empty()) {
    issues.push_back(
        Issue {std::string(diag_codes::kPluginExecutionFailure), IssueSeverity::Error, "插件导出失败", {}, {}});
  }
  const DiagnosticId id = st.create_diagnostic("插件导出失败", std::move(issues));
  return error_void(r.status, id, std::move(r.warnings));
}

Result<void> attach_plugin_pre_export_validate_fail_diag(detail::KernelState& st, BodyId body_id,
                                                         Result<void> v) {
  if (!st.config.enable_diagnostics) {
    return error_void(v.status, v.diagnostic_id, std::move(v.warnings));
  }
  std::vector<Issue> issues;
  issues.push_back(Issue {std::string(diag_codes::kPluginResultValidationWarning), IssueSeverity::Error,
                          "插件导出前宿主全量验证未通过（未调用导出插件）",
                          {body_id.value}, "plugin.pre_export.validate"});
  const DiagnosticId id = st.create_diagnostic("插件导出前验证未通过", std::move(issues));
  return error_void(v.status, id, std::move(v.warnings));
}

Result<OpReport> attach_plugin_repair_exec_diag(detail::KernelState& st, Result<OpReport> r) {
  if (r.status == StatusCode::Ok) {
    return r;
  }
  if (!st.config.enable_diagnostics) {
    return r;
  }
  std::vector<Issue> issues;
  issues.reserve(r.warnings.size() + 1);
  for (const auto& w : r.warnings) {
    issues.push_back(Issue {w.code, IssueSeverity::Error, w.message, {}, {}});
  }
  if (issues.empty()) {
    issues.push_back(
        Issue {std::string(diag_codes::kPluginExecutionFailure), IssueSeverity::Error, "插件修复失败", {}, {}});
  }
  const DiagnosticId id = st.create_diagnostic("插件修复失败", std::move(issues));
  return error_result<OpReport>(r.status, id, std::move(r.warnings));
}

Result<OpReport> attach_plugin_post_repair_validate_fail_diag(detail::KernelState& st, BodyId body_id,
                                                              Result<void> v) {
  if (!st.config.enable_diagnostics) {
    return error_result<OpReport>(v.status, v.diagnostic_id, std::move(v.warnings));
  }
  std::vector<Issue> issues;
  issues.push_back(Issue {std::string(diag_codes::kPluginResultValidationWarning), IssueSeverity::Error,
                          "插件修复成功但宿主全量验证未通过（模型可能已变更，可结合 validate / 诊断定位）",
                          {body_id.value}, "plugin.post_repair.validate"});
  const DiagnosticId id = st.create_diagnostic("插件修复后验证未通过", std::move(issues));
  return error_result<OpReport>(v.status, id, std::move(v.warnings));
}

BodyId plugin_repair_validation_body(const detail::KernelState& st, BodyId input, const OpReport& rep) {
  if (rep.output.value != 0 && detail::has_body(st, rep.output)) {
    return rep.output;
  }
  return input;
}

Result<void> plugin_curve_host_consistency_check(const std::shared_ptr<detail::KernelState>& st, CurveId curve_id) {
  if (!st) {
    return error_void(StatusCode::InternalError, {}, {});
  }
  if (!detail::has_curve(*st, curve_id)) {
    return error_void(StatusCode::InvalidInput, {}, {});
  }
  CurveService cs(st);
  const auto dom = cs.domain(curve_id);
  if (dom.status != StatusCode::Ok || !dom.value.has_value()) {
    return error_void(dom.status, dom.diagnostic_id, std::move(dom.warnings));
  }
  const auto& d = *dom.value;
  if (!std::isless(d.min, d.max)) {
    return error_void(StatusCode::OperationFailed, {}, {});
  }
  return ok_void({});
}

}  // namespace kernel_plugin_helpers
}  // namespace axiom
