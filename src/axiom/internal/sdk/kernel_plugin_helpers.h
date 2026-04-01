#pragma once

#include <string>
#include <string_view>

#include "axiom/core/result.h"
#include "axiom/core/types.h"

namespace axiom {

namespace detail {
struct KernelState;
}

namespace kernel_plugin_helpers {

std::string_view trim_plugin_field(std::string_view s);
std::string json_escape_plugin_field(std::string_view s);
const char* plugin_sandbox_level_tag(PluginSandboxLevel level);
const char* plugin_api_version_match_mode_tag(PluginApiVersionMatchMode mode);

Result<void> attach_plugin_registration_diag(detail::KernelState& st, Result<void> r);
Result<void> attach_plugin_unregistration_diag(detail::KernelState& st, Result<void> r);
Result<CurveId> attach_plugin_curve_create_diag(detail::KernelState& st, Result<CurveId> r);
Result<CurveId> attach_plugin_post_curve_verify_fail_diag(detail::KernelState& st, CurveId curve_id,
                                                          Result<void> v);
Result<void> attach_plugin_curve_consistency_verify_diag(detail::KernelState& st, CurveId curve_id, Result<void> v);
Result<BodyId> attach_plugin_import_body_diag(detail::KernelState& st, Result<BodyId> r);
Result<BodyId> attach_plugin_post_import_validate_fail_diag(detail::KernelState& st, BodyId body_id,
                                                            Result<void> v);
Result<void> attach_plugin_export_body_diag(detail::KernelState& st, Result<void> r);
Result<void> attach_plugin_pre_export_validate_fail_diag(detail::KernelState& st, BodyId body_id,
                                                        Result<void> v);
Result<OpReport> attach_plugin_repair_exec_diag(detail::KernelState& st, Result<OpReport> r);
Result<OpReport> attach_plugin_post_repair_validate_fail_diag(detail::KernelState& st, BodyId body_id,
                                                               Result<void> v);

BodyId plugin_repair_validation_body(const detail::KernelState& st, BodyId input, const OpReport& rep);

}  // namespace kernel_plugin_helpers
}  // namespace axiom
