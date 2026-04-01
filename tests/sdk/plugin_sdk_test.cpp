#include "axiom/diag/error_codes.h"
#include "axiom/plugin/plugin_registry.h"
#include "axiom/plugin/plugin_sdk_version.h"
#include "axiom/sdk/kernel.h"

#include <iostream>
#include <string>

namespace {

class EmptyNameCurvePlugin final : public axiom::ICurvePlugin {
public:
    std::string type_name() const override { return ""; }
    axiom::Result<axiom::CurveId> create(const axiom::PluginCurveDesc&) override {
        return axiom::error_result<axiom::CurveId>(axiom::StatusCode::OperationFailed);
    }
};

class SampleCurvePlugin final : public axiom::ICurvePlugin {
public:
    std::string type_name() const override { return "sample_curve"; }
    axiom::Result<axiom::CurveId> create(const axiom::PluginCurveDesc&) override {
        return axiom::error_result<axiom::CurveId>(axiom::StatusCode::OperationFailed);
    }
};

bool has_warning_code(const axiom::Result<void>& r, std::string_view code) {
    for (const auto& w : r.warnings) {
        if (w.code == code) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    axiom::Kernel kernel;

    axiom::PluginManifest bad;
    bad.name = "";
    bad.version = "0";
    bad.vendor = "t";
    bad.capabilities = {"curve"};
    auto reg_fail = kernel.plugins().register_curve_type(bad, std::make_unique<SampleCurvePlugin>());
    if (reg_fail.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(reg_fail, axiom::diag_codes::kPluginCapabilityIncomplete)) {
        std::cerr << "expected invalid manifest name rejection\n";
        return 1;
    }

    auto empty_type = kernel.plugins().register_curve_type(
        [&] {
            axiom::PluginManifest m;
            m.name = "e";
            m.version = "1";
            m.vendor = "t";
            m.capabilities = {"c"};
            return m;
        }(),
        std::make_unique<EmptyNameCurvePlugin>());
    if (empty_type.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(empty_type, axiom::diag_codes::kPluginCapabilityIncomplete)) {
        std::cerr << "expected empty type_name rejection\n";
        return 1;
    }

    axiom::PluginHostPolicy pol;
    pol.max_plugin_slots = 1;
    pol.require_unique_manifest_name = false;
    if (kernel.set_plugin_host_policy(pol).status != axiom::StatusCode::Ok) {
        std::cerr << "set_plugin_host_policy failed\n";
        return 1;
    }
    axiom::PluginManifest m1;
    m1.name = "a";
    m1.version = "1";
    m1.vendor = "v";
    m1.capabilities = {"io"};
    axiom::PluginManifest m2 = m1;
    m2.name = "b";
    if (kernel.plugins().register_curve_type(m1, std::make_unique<SampleCurvePlugin>()).status != axiom::StatusCode::Ok) {
        std::cerr << "first register failed\n";
        return 1;
    }
    auto over = kernel.plugins().register_curve_type(m2, std::make_unique<SampleCurvePlugin>());
    if (over.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(over, axiom::diag_codes::kPluginHostCapacityExceeded)) {
        std::cerr << "expected capacity rejection\n";
        return 1;
    }

    axiom::Kernel k2;
    axiom::PluginHostPolicy pol2;
    pol2.require_unique_manifest_name = true;
    if (k2.set_plugin_host_policy(pol2).status != axiom::StatusCode::Ok) {
        std::cerr << "set policy k2 failed\n";
        return 1;
    }
    axiom::PluginManifest u;
    u.name = "same";
    u.version = "1";
    u.vendor = "v";
    u.capabilities = {"x"};
    if (k2.plugins().register_curve_type(u, std::make_unique<SampleCurvePlugin>()).status != axiom::StatusCode::Ok) {
        std::cerr << "k2 first register failed\n";
        return 1;
    }
    auto dup = k2.plugins().register_repair_plugin(u, [&]() {
        struct R final : axiom::IRepairPlugin {
            std::string type_name() const override { return "r"; }
            axiom::Result<axiom::OpReport> run(axiom::BodyId, axiom::RepairMode) override {
                return axiom::error_result<axiom::OpReport>(axiom::StatusCode::OperationFailed);
            }
        };
        return std::make_unique<R>();
    }());
    if (dup.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(dup, axiom::diag_codes::kPluginDuplicateManifestName)) {
        std::cerr << "expected duplicate manifest name rejection\n";
        return 1;
    }

    axiom::Kernel k3;
    auto disc = k3.plugin_discovery_report_lines();
    if (disc.status != axiom::StatusCode::Ok || !disc.value.has_value() || disc.value->empty()) {
        std::cerr << "plugin_discovery_report_lines empty\n";
        return 1;
    }
    auto pcap = k3.plugin_capabilities();
    if (pcap.status != axiom::StatusCode::Ok || !pcap.value.has_value()) {
        std::cerr << "plugin_capabilities failed\n";
        return 1;
    }
    auto php = k3.plugin_host_policy();
    if (php.status != axiom::StatusCode::Ok || !php.value.has_value()) {
        std::cerr << "plugin_host_policy failed\n";
        return 1;
    }

    axiom::Kernel k4;
    auto jsn = k4.plugin_discovery_report_json();
    if (jsn.status != axiom::StatusCode::Ok || !jsn.value.has_value() || jsn.value->find("\"sdk_api\"") == std::string::npos ||
        jsn.value->find("\"manifests\"") == std::string::npos) {
        std::cerr << "plugin_discovery_report_json missing sdk_api or manifests\n";
        return 1;
    }
    axiom::PluginManifest bad_reg;
    bad_reg.name = "";
    bad_reg.version = "0";
    bad_reg.vendor = "t";
    bad_reg.capabilities = {"x"};
    auto diag_reg = k4.register_plugin_curve(bad_reg, std::make_unique<SampleCurvePlugin>());
    if (diag_reg.status != axiom::StatusCode::InvalidInput || diag_reg.diagnostic_id.value == 0) {
        std::cerr << "register_plugin_curve should attach diagnostic on failure\n";
        return 1;
    }

    axiom::Kernel k_api;
    axiom::PluginHostPolicy pol_api;
    pol_api.require_plugin_api_version_match = true;
    if (k_api.set_plugin_host_policy(pol_api).status != axiom::StatusCode::Ok) {
        std::cerr << "set_plugin_host_policy api test failed\n";
        return 1;
    }
    axiom::PluginManifest no_api;
    no_api.name = "na";
    no_api.version = "1";
    no_api.vendor = "v";
    no_api.capabilities = {"c"};
    auto miss = k_api.plugins().register_curve_type(no_api, std::make_unique<SampleCurvePlugin>());
    if (miss.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(miss, axiom::diag_codes::kPluginVersionIncompatible)) {
        std::cerr << "expected missing plugin_api_version rejection\n";
        return 1;
    }
    axiom::PluginManifest bad_api = no_api;
    bad_api.plugin_api_version = "0.9";
    auto badv = k_api.plugins().register_curve_type(bad_api, std::make_unique<SampleCurvePlugin>());
    if (badv.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(badv, axiom::diag_codes::kPluginVersionIncompatible)) {
        std::cerr << "expected wrong plugin_api_version rejection\n";
        return 1;
    }
    bad_api.plugin_api_version = std::string(axiom::kPluginSdkApiVersion);
    bad_api.name = "ok_api";
    if (k_api.plugins().register_curve_type(bad_api, std::make_unique<SampleCurvePlugin>()).status != axiom::StatusCode::Ok) {
        std::cerr << "expected matching plugin_api_version to register\n";
        return 1;
    }

    auto compat = k_api.plugin_api_compatibility_report_lines();
    if (compat.status != axiom::StatusCode::Ok || !compat.value.has_value() || compat.value->size() < 2) {
        std::cerr << "plugin_api_compatibility_report_lines unexpected\n";
        return 1;
    }
    bool saw_ok = false;
    for (const auto& ln : *compat.value) {
        if (ln.find("state=ok") != std::string::npos) {
            saw_ok = true;
        }
    }
    if (!saw_ok) {
        std::cerr << "expected state=ok in compatibility report\n";
        return 1;
    }

    axiom::Kernel k_minor;
    axiom::PluginHostPolicy pol_minor;
    pol_minor.require_plugin_api_version_match = true;
    pol_minor.plugin_api_version_match_mode = axiom::PluginApiVersionMatchMode::SameMinor;
    if (k_minor.set_plugin_host_policy(pol_minor).status != axiom::StatusCode::Ok) {
        std::cerr << "set_plugin_host_policy same_minor failed\n";
        return 1;
    }
    axiom::PluginManifest m_patch;
    m_patch.name = "patch_ok";
    m_patch.version = "1";
    m_patch.vendor = "v";
    m_patch.capabilities = {"c"};
    m_patch.plugin_api_version = "1.0.7";
    if (k_minor.plugins().register_curve_type(m_patch, std::make_unique<SampleCurvePlugin>()).status != axiom::StatusCode::Ok) {
        std::cerr << "expected SameMinor to accept 1.0.7 vs host 1.0\n";
        return 1;
    }
    axiom::PluginManifest m_major_only;
    m_major_only.name = "maj_only";
    m_major_only.version = "1";
    m_major_only.vendor = "v";
    m_major_only.capabilities = {"c"};
    m_major_only.plugin_api_version = "1";
    auto maj_only_reg = k_minor.plugins().register_curve_type(m_major_only, std::make_unique<SampleCurvePlugin>());
    if (maj_only_reg.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(maj_only_reg, axiom::diag_codes::kPluginVersionIncompatible)) {
        std::cerr << "expected SameMinor to reject bare major-only declaration\n";
        return 1;
    }
    axiom::PluginManifest m_wrong_min;
    m_wrong_min.name = "wrong_min";
    m_wrong_min.version = "1";
    m_wrong_min.vendor = "v";
    m_wrong_min.capabilities = {"c"};
    m_wrong_min.plugin_api_version = "1.1.0";
    auto wrong_min = k_minor.plugins().register_curve_type(m_wrong_min, std::make_unique<SampleCurvePlugin>());
    if (wrong_min.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(wrong_min, axiom::diag_codes::kPluginVersionIncompatible)) {
        std::cerr << "expected SameMinor to reject different minor\n";
        return 1;
    }

    axiom::Kernel k_major_mode;
    axiom::PluginHostPolicy pol_major_mode;
    pol_major_mode.require_plugin_api_version_match = true;
    pol_major_mode.plugin_api_version_match_mode = axiom::PluginApiVersionMatchMode::SameMajor;
    if (k_major_mode.set_plugin_host_policy(pol_major_mode).status != axiom::StatusCode::Ok) {
        std::cerr << "set_plugin_host_policy same_major failed\n";
        return 1;
    }
    axiom::PluginManifest m_smj;
    m_smj.name = "wide_minor";
    m_smj.version = "1";
    m_smj.vendor = "v";
    m_smj.capabilities = {"c"};
    m_smj.plugin_api_version = "1.9.9";
    if (k_major_mode.plugins().register_curve_type(m_smj, std::make_unique<SampleCurvePlugin>()).status != axiom::StatusCode::Ok) {
        std::cerr << "expected SameMajor to accept 1.9.9 vs host 1.0\n";
        return 1;
    }
    axiom::PluginManifest m_smj_bad;
    m_smj_bad.name = "wrong_major";
    m_smj_bad.version = "1";
    m_smj_bad.vendor = "v";
    m_smj_bad.capabilities = {"c"};
    m_smj_bad.plugin_api_version = "2.0";
    auto bad_maj = k_major_mode.plugins().register_curve_type(m_smj_bad, std::make_unique<SampleCurvePlugin>());
    if (bad_maj.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(bad_maj, axiom::diag_codes::kPluginVersionIncompatible)) {
        std::cerr << "expected SameMajor to reject different major\n";
        return 1;
    }

    axiom::Kernel k_val;
    auto bx = k_val.primitives().box({0.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
    if (bx.status != axiom::StatusCode::Ok || !bx.value.has_value()) {
        std::cerr << "box for validate_after_plugin_mutation failed\n";
        return 1;
    }
    auto v_after = k_val.validate_after_plugin_mutation(*bx.value, axiom::ValidationMode::Standard);
    if (v_after.status != axiom::StatusCode::Ok) {
        std::cerr << "validate_after_plugin_mutation failed on primitive box\n";
        return 1;
    }

    auto has_disc = k_val.has_service_plugin_discovery();
    if (has_disc.status != axiom::StatusCode::Ok || !has_disc.value.has_value() || !*has_disc.value) {
        std::cerr << "has_service_plugin_discovery unexpected\n";
        return 1;
    }

    auto missing_mf = k_val.plugins().find_manifest("no_such_plugin_manifest_ever");
    if (missing_mf.status != axiom::StatusCode::InvalidInput || missing_mf.warnings.empty() ||
        missing_mf.warnings[0].code != std::string(axiom::diag_codes::kPluginLoadFailure)) {
        std::cerr << "find_manifest missing should carry kPluginLoadFailure warning\n";
        return 1;
    }

    axiom::Kernel k_sb;
    axiom::PluginHostPolicy sb_pol;
    sb_pol.sandbox_level = axiom::PluginSandboxLevel::Annotated;
    if (k_sb.set_plugin_host_policy(sb_pol).status != axiom::StatusCode::Ok) {
        std::cerr << "set sandbox policy failed\n";
        return 1;
    }
    auto dlines = k_sb.plugin_discovery_report_lines();
    if (dlines.status != axiom::StatusCode::Ok || !dlines.value.has_value()) {
        std::cerr << "discovery lines for sandbox test failed\n";
        return 1;
    }
    bool saw_sandbox = false;
    for (const auto& ln : *dlines.value) {
        if (ln.find("plugin.host.sandbox_level=annotated") != std::string::npos) {
            saw_sandbox = true;
        }
    }
    if (!saw_sandbox) {
        std::cerr << "expected annotated sandbox in discovery lines\n";
        return 1;
    }

    axiom::Kernel k_unreg;
    auto no_curve = k_unreg.unregister_plugin_curve("no_such_curve_type");
    if (no_curve.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(no_curve, axiom::diag_codes::kPluginNotRegistered) || no_curve.diagnostic_id.value == 0) {
        std::cerr << "unregister missing curve should fail with kPluginNotRegistered and diagnostic\n";
        return 1;
    }
    auto empty_unreg = k_unreg.unregister_plugin_curve("  \t  ");
    if (empty_unreg.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(empty_unreg, axiom::diag_codes::kPluginCapabilityIncomplete)) {
        std::cerr << "unregister empty type_name should fail with kPluginCapabilityIncomplete\n";
        return 1;
    }
    axiom::PluginManifest um;
    um.name = "u1";
    um.version = "1";
    um.vendor = "v";
    um.capabilities = {"curve"};
    if (k_unreg.plugins().register_curve_type(um, std::make_unique<SampleCurvePlugin>()).status != axiom::StatusCode::Ok) {
        std::cerr << "register for unregister test failed\n";
        return 1;
    }
    if (k_unreg.unregister_plugin_curve("sample_curve").status != axiom::StatusCode::Ok) {
        std::cerr << "unregister_plugin_curve should succeed\n";
        return 1;
    }
    auto mf_u1 = k_unreg.plugins().has_manifest("u1");
    if (mf_u1.status != axiom::StatusCode::Ok || !mf_u1.value.has_value() || *mf_u1.value) {
        std::cerr << "manifest u1 should be removed with bound implementation_type_name\n";
        return 1;
    }
    auto still_there = k_unreg.plugins().has_curve_type("sample_curve");
    if (still_there.status != axiom::StatusCode::Ok || !still_there.value.has_value() || *still_there.value) {
        std::cerr << "curve plugin should be gone after unregister\n";
        return 1;
    }
    axiom::PluginManifest mismatch_m;
    mismatch_m.name = "mismatch_m";
    mismatch_m.version = "1";
    mismatch_m.vendor = "v";
    mismatch_m.capabilities = {"curve"};
    mismatch_m.implementation_type_name = "not_sample_curve";
    auto mis_reg = k_unreg.plugins().register_curve_type(mismatch_m, std::make_unique<SampleCurvePlugin>());
    if (mis_reg.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(mis_reg, axiom::diag_codes::kPluginCapabilityIncomplete)) {
        std::cerr << "expected implementation_type_name mismatch rejection\n";
        return 1;
    }
    auto no_mf = k_unreg.unregister_plugin_manifest("no_such_manifest_name_xyz");
    if (no_mf.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(no_mf, axiom::diag_codes::kPluginNotRegistered) || no_mf.diagnostic_id.value == 0) {
        std::cerr << "unregister_plugin_manifest missing should fail with diagnostic\n";
        return 1;
    }
    axiom::PluginManifest mf_only;
    mf_only.name = "mf_only";
    mf_only.version = "0";
    mf_only.vendor = "t";
    mf_only.capabilities = {"meta"};
    if (k_unreg.register_plugin_manifest_only(mf_only).status != axiom::StatusCode::Ok) {
        std::cerr << "register manifest only for unregister test failed\n";
        return 1;
    }
    if (k_unreg.unregister_plugin_manifest("mf_only").status != axiom::StatusCode::Ok) {
        std::cerr << "unregister_plugin_manifest should succeed\n";
        return 1;
    }
    auto hm = k_unreg.plugins().has_manifest("mf_only");
    if (hm.status != axiom::StatusCode::Ok || !hm.value.has_value() || *hm.value) {
        std::cerr << "manifest mf_only should be removed\n";
        return 1;
    }

    return 0;
}
