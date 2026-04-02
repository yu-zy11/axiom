#include "axiom/diag/error_codes.h"
#include "axiom/plugin/plugin_registry.h"
#include "axiom/plugin/plugin_sdk_version.h"
#include "axiom/sdk/kernel.h"

#include <iostream>
#include <string>
#include <vector>

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

/// 与 `SampleCurvePlugin` 行为相同，但 `type_name` 不同，便于在同宿主策略下连续注册多条曲线插件回归用例。
class SampleCurveAltPlugin final : public axiom::ICurvePlugin {
public:
    std::string type_name() const override { return "sample_curve_alt"; }
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
    {
        auto impl_names = kernel.plugins().registered_implementation_type_names_sorted();
        if (impl_names.status != axiom::StatusCode::Ok || !impl_names.value.has_value() ||
            impl_names.value->size() != 1 || (*impl_names.value)[0] != "sample_curve") {
            std::cerr << "registered_implementation_type_names_sorted mismatch after curve register\n";
            return 1;
        }
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
    axiom::PluginManifest exact_plus;
    exact_plus.name = "exact_plus";
    exact_plus.version = "1";
    exact_plus.vendor = "v";
    exact_plus.capabilities = {"c"};
    exact_plus.plugin_api_version = std::string(axiom::kPluginSdkApiVersion) + "+build";
    auto epr = k_api.plugins().register_curve_type(exact_plus, std::make_unique<SampleCurvePlugin>());
    if (epr.status != axiom::StatusCode::InvalidInput ||
        !has_warning_code(epr, axiom::diag_codes::kPluginVersionIncompatible)) {
        std::cerr << "expected Exact match to reject +build suffix on declared version\n";
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
    axiom::PluginManifest m_pre;
    m_pre.name = "pre_rel";
    m_pre.version = "1";
    m_pre.vendor = "v";
    m_pre.capabilities = {"c"};
    m_pre.plugin_api_version = "1.0.12-rc.1";
    if (k_minor.plugins().register_curve_type(m_pre, std::make_unique<SampleCurveAltPlugin>()).status != axiom::StatusCode::Ok) {
        std::cerr << "expected SameMinor to accept SemVer pre-release on same major.minor core\n";
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
    axiom::PluginManifest m_build_meta;
    m_build_meta.name = "build_meta";
    m_build_meta.version = "1";
    m_build_meta.vendor = "v";
    m_build_meta.capabilities = {"c"};
    m_build_meta.plugin_api_version = "1.8.0+beta";
    if (k_major_mode.plugins().register_curve_type(m_build_meta, std::make_unique<SampleCurveAltPlugin>()).status !=
        axiom::StatusCode::Ok) {
        std::cerr << "expected SameMajor to accept +build metadata when major matches\n";
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

    {
        axiom::Kernel k_imp;
        auto has_pi = k_imp.has_service_plugin_import();
        if (has_pi.status != axiom::StatusCode::Ok || !has_pi.value.has_value() || !*has_pi.value) {
            std::cerr << "has_service_plugin_import unexpected\n";
            return 1;
        }
        auto miss = k_imp.plugin_import_file("no_such_importer_ever", "x.step");
        if (miss.status != axiom::StatusCode::InvalidInput || miss.warnings.empty() ||
            miss.warnings[0].code != axiom::diag_codes::kPluginNotRegistered) {
            std::cerr << "plugin_import_file missing importer\n";
            return 1;
        }
        auto empty_imp = k_imp.plugin_import_file("  \t  ", "x.step");
        if (empty_imp.status != axiom::StatusCode::InvalidInput || empty_imp.warnings.empty() ||
            empty_imp.warnings[0].code != axiom::diag_codes::kPluginCapabilityIncomplete) {
            std::cerr << "plugin_import_file empty type_name\n";
            return 1;
        }

        class KernelBoxImporter final : public axiom::IImporterPlugin {
        public:
            explicit KernelBoxImporter(axiom::Kernel* k) : k_(k) {}
            std::string type_name() const override { return "kernel_box_importer"; }
            axiom::Result<axiom::BodyId> import_file(std::string_view) override {
                auto b = k_->primitives().box({0.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
                if (b.status != axiom::StatusCode::Ok || !b.value.has_value()) {
                    return axiom::error_result<axiom::BodyId>(b.status, b.diagnostic_id, std::move(b.warnings));
                }
                return axiom::ok_result(*b.value);
            }

        private:
            axiom::Kernel* k_;
        };

        class BogusBodyImporter final : public axiom::IImporterPlugin {
        public:
            std::string type_name() const override { return "bogus_body_importer"; }
            axiom::Result<axiom::BodyId> import_file(std::string_view) override {
                return axiom::ok_result(axiom::BodyId {999999999ull});
            }
        };

        axiom::PluginManifest mf_imp;
        mf_imp.name = "kb_imp";
        mf_imp.version = "1";
        mf_imp.vendor = "v";
        mf_imp.capabilities = {"io", "import"};
        if (k_imp.register_plugin_importer(mf_imp, std::make_unique<KernelBoxImporter>(&k_imp)).status !=
            axiom::StatusCode::Ok) {
            std::cerr << "register kernel_box_importer failed\n";
            return 1;
        }
        auto got = k_imp.plugin_import_file("kernel_box_importer", "unused.path");
        if (got.status != axiom::StatusCode::Ok || !got.value.has_value() || got.value->value == 0) {
            std::cerr << "plugin_import_file should return body id\n";
            return 1;
        }

        auto pol_r = k_imp.plugin_host_policy();
        if (pol_r.status != axiom::StatusCode::Ok || !pol_r.value.has_value()) {
            std::cerr << "plugin_host_policy for import test\n";
            return 1;
        }
        axiom::PluginHostPolicy pol_val = *pol_r.value;
        pol_val.auto_validate_body_after_plugin_importer = true;
        if (k_imp.set_plugin_host_policy(pol_val).status != axiom::StatusCode::Ok) {
            std::cerr << "set auto_validate policy failed\n";
            return 1;
        }
        auto ok2 = k_imp.plugin_import_file("kernel_box_importer", "b.path");
        if (ok2.status != axiom::StatusCode::Ok) {
            std::cerr << "plugin_import_file with auto_validate should pass on valid box\n";
            return 1;
        }

        axiom::PluginManifest mf_bad;
        mf_bad.name = "bogus_mf";
        mf_bad.version = "1";
        mf_bad.vendor = "v";
        mf_bad.capabilities = {"io"};
        if (k_imp.register_plugin_importer(mf_bad, std::make_unique<BogusBodyImporter>()).status !=
            axiom::StatusCode::Ok) {
            std::cerr << "register bogus importer failed\n";
            return 1;
        }
        auto bad_val = k_imp.plugin_import_file("bogus_body_importer", "c.path");
        if (bad_val.status == axiom::StatusCode::Ok) {
            std::cerr << "plugin_import_file expected post-import validate failure for invalid body id\n";
            return 1;
        }

        auto via_reg_imp = k_imp.plugins().invoke_registered_importer("kernel_box_importer", "d.path");
        if (via_reg_imp.status != axiom::StatusCode::Ok || !via_reg_imp.value.has_value() ||
            via_reg_imp.value->value == 0) {
            std::cerr << "invoke_registered_importer with host policy auto_validate should pass on valid box\n";
            return 1;
        }
        auto via_reg_bad = k_imp.plugins().invoke_registered_importer("bogus_body_importer", "e.path");
        if (via_reg_bad.status == axiom::StatusCode::Ok) {
            std::cerr << "invoke_registered_importer expected post-import validate failure for invalid body id\n";
            return 1;
        }

        auto jsn_pol = k_imp.plugin_discovery_report_json();
        if (jsn_pol.status != axiom::StatusCode::Ok || !jsn_pol.value.has_value() ||
            jsn_pol.value->find("auto_validate_body_after_plugin_importer") == std::string::npos ||
            jsn_pol.value->find("auto_validate_body_before_plugin_exporter") == std::string::npos ||
            jsn_pol.value->find("auto_validate_body_after_plugin_repair") == std::string::npos ||
            jsn_pol.value->find("auto_verify_curve_after_plugin_curve") == std::string::npos) {
            std::cerr << "plugin_discovery_report_json missing auto_validate policy fields\n";
            return 1;
        }
    }

    {
        axiom::Kernel k_exp;
        auto has_pe = k_exp.has_service_plugin_export();
        if (has_pe.status != axiom::StatusCode::Ok || !has_pe.value.has_value() || !*has_pe.value) {
            std::cerr << "has_service_plugin_export unexpected\n";
            return 1;
        }
        auto no_body = k_exp.plugin_export_file("any_exporter", axiom::BodyId {888888888ull}, "out.bin");
        if (no_body.status != axiom::StatusCode::InvalidInput || no_body.warnings.empty() ||
            no_body.warnings[0].code != axiom::diag_codes::kPluginExecutionFailure) {
            std::cerr << "plugin_export_file missing body should fail\n";
            return 1;
        }
        auto bx = k_exp.primitives().box({0.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
        if (bx.status != axiom::StatusCode::Ok || !bx.value.has_value()) {
            std::cerr << "box for export test failed\n";
            return 1;
        }
        auto miss_exp = k_exp.plugin_export_file("no_such_exporter_ever", *bx.value, "p.bin");
        if (miss_exp.status != axiom::StatusCode::InvalidInput || miss_exp.warnings.empty() ||
            miss_exp.warnings[0].code != axiom::diag_codes::kPluginNotRegistered) {
            std::cerr << "plugin_export_file missing exporter\n";
            return 1;
        }
        auto empty_exp = k_exp.plugin_export_file("  \t  ", *bx.value, "p.bin");
        if (empty_exp.status != axiom::StatusCode::InvalidInput || empty_exp.warnings.empty() ||
            empty_exp.warnings[0].code != axiom::diag_codes::kPluginCapabilityIncomplete) {
            std::cerr << "plugin_export_file empty type_name\n";
            return 1;
        }

        class RecordingExporter final : public axiom::IExporterPlugin {
        public:
            RecordingExporter(std::string* path_out, axiom::BodyId* body_out) : path_out_(path_out), body_out_(body_out) {}
            std::string type_name() const override { return "recording_exporter_test"; }
            axiom::Result<void> export_file(axiom::BodyId bid, std::string_view path) override {
                if (path_out_ != nullptr) {
                    *path_out_ = std::string(path);
                }
                if (body_out_ != nullptr) {
                    *body_out_ = bid;
                }
                return axiom::ok_void();
            }

        private:
            std::string* path_out_{};
            axiom::BodyId* body_out_{};
        };

        std::string last_path;
        axiom::BodyId last_body {};
        axiom::PluginManifest mex;
        mex.name = "rec_exp_mf";
        mex.version = "1";
        mex.vendor = "v";
        mex.capabilities = {"io", "export"};
        if (k_exp.register_plugin_exporter(mex, std::make_unique<RecordingExporter>(&last_path, &last_body)).status !=
            axiom::StatusCode::Ok) {
            std::cerr << "register recording exporter failed\n";
            return 1;
        }
        const std::string want_path = "/tmp/plugin_sdk_export_test.out";
        if (k_exp.plugin_export_file("recording_exporter_test", *bx.value, want_path).status != axiom::StatusCode::Ok) {
            std::cerr << "plugin_export_file should succeed\n";
            return 1;
        }
        if (last_path != want_path || last_body.value != bx.value->value) {
            std::cerr << "recording exporter did not receive path/body\n";
            return 1;
        }

        auto pol_e = k_exp.plugin_host_policy();
        if (pol_e.status != axiom::StatusCode::Ok || !pol_e.value.has_value()) {
            std::cerr << "plugin_host_policy export test\n";
            return 1;
        }
        axiom::PluginHostPolicy pol_pre = *pol_e.value;
        pol_pre.auto_validate_body_before_plugin_exporter = true;
        if (k_exp.set_plugin_host_policy(pol_pre).status != axiom::StatusCode::Ok) {
            std::cerr << "set pre_export validate policy failed\n";
            return 1;
        }
        last_path.clear();
        if (k_exp.plugin_export_file("recording_exporter_test", *bx.value, want_path + "2").status !=
            axiom::StatusCode::Ok) {
            std::cerr << "plugin_export_file with pre_validate should pass on valid box\n";
            return 1;
        }
        if (last_path != want_path + "2") {
            std::cerr << "second export path mismatch\n";
            return 1;
        }
    }

    {
        axiom::Kernel k_rep;
        auto has_pr = k_rep.has_service_plugin_repair();
        if (has_pr.status != axiom::StatusCode::Ok || !has_pr.value.has_value() || !*has_pr.value) {
            std::cerr << "has_service_plugin_repair unexpected\n";
            return 1;
        }
        auto no_body = k_rep.plugin_run_repair("any_repair", axiom::BodyId {777777777ull}, axiom::RepairMode::Safe);
        if (no_body.status != axiom::StatusCode::InvalidInput || no_body.warnings.empty() ||
            no_body.warnings[0].code != axiom::diag_codes::kPluginExecutionFailure) {
            std::cerr << "plugin_run_repair missing body should fail\n";
            return 1;
        }
        auto bx = k_rep.primitives().box({0.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
        if (bx.status != axiom::StatusCode::Ok || !bx.value.has_value()) {
            std::cerr << "box for repair plugin test failed\n";
            return 1;
        }
        auto miss_rep = k_rep.plugin_run_repair("no_such_repair_ever", *bx.value, axiom::RepairMode::Safe);
        if (miss_rep.status != axiom::StatusCode::InvalidInput || miss_rep.warnings.empty() ||
            miss_rep.warnings[0].code != axiom::diag_codes::kPluginNotRegistered) {
            std::cerr << "plugin_run_repair missing repair plugin\n";
            return 1;
        }
        auto empty_rep = k_rep.plugin_run_repair("  \t  ", *bx.value, axiom::RepairMode::Safe);
        if (empty_rep.status != axiom::StatusCode::InvalidInput || empty_rep.warnings.empty() ||
            empty_rep.warnings[0].code != axiom::diag_codes::kPluginCapabilityIncomplete) {
            std::cerr << "plugin_run_repair empty type_name\n";
            return 1;
        }

        class NoopRepairPlugin final : public axiom::IRepairPlugin {
        public:
            std::string type_name() const override { return "noop_repair_sdk_test"; }
            axiom::Result<axiom::OpReport> run(axiom::BodyId bid, axiom::RepairMode) override {
                return axiom::ok_result(axiom::OpReport {.status = axiom::StatusCode::Ok, .output = bid});
            }
        };

        axiom::PluginManifest mrep;
        mrep.name = "noop_rep_mf";
        mrep.version = "1";
        mrep.vendor = "v";
        mrep.capabilities = {"heal", "repair"};
        if (k_rep.register_plugin_repair(mrep, std::make_unique<NoopRepairPlugin>()).status != axiom::StatusCode::Ok) {
            std::cerr << "register noop repair plugin failed\n";
            return 1;
        }
        auto ran = k_rep.plugin_run_repair("noop_repair_sdk_test", *bx.value, axiom::RepairMode::ReportOnly);
        if (ran.status != axiom::StatusCode::Ok || !ran.value.has_value() ||
            ran.value->output.value != bx.value->value) {
            std::cerr << "plugin_run_repair should succeed with noop plugin\n";
            return 1;
        }

        auto pol_rep = k_rep.plugin_host_policy();
        if (pol_rep.status != axiom::StatusCode::Ok || !pol_rep.value.has_value()) {
            std::cerr << "plugin_host_policy repair test\n";
            return 1;
        }
        axiom::PluginHostPolicy pol_ar = *pol_rep.value;
        pol_ar.auto_validate_body_after_plugin_repair = true;
        if (k_rep.set_plugin_host_policy(pol_ar).status != axiom::StatusCode::Ok) {
            std::cerr << "set auto_validate after repair policy failed\n";
            return 1;
        }
        if (k_rep.plugin_run_repair("noop_repair_sdk_test", *bx.value, axiom::RepairMode::Safe).status !=
            axiom::StatusCode::Ok) {
            std::cerr << "plugin_run_repair with post-repair validate should pass on valid box\n";
            return 1;
        }

        auto jsn_r = k_rep.plugin_discovery_report_json();
        if (jsn_r.status != axiom::StatusCode::Ok || !jsn_r.value.has_value() ||
            jsn_r.value->find("auto_validate_body_after_plugin_repair") == std::string::npos ||
            jsn_r.value->find("auto_verify_curve_after_plugin_curve") == std::string::npos) {
            std::cerr << "plugin_discovery_report_json missing plugin repair/curve policy fields\n";
            return 1;
        }
    }

    {
        axiom::Kernel k_cv;
        auto has_pc = k_cv.has_service_plugin_curve();
        if (has_pc.status != axiom::StatusCode::Ok || !has_pc.value.has_value() || !*has_pc.value) {
            std::cerr << "has_service_plugin_curve unexpected\n";
            return 1;
        }
        auto has_pvc = k_cv.has_service_plugin_verify_curve();
        if (has_pvc.status != axiom::StatusCode::Ok || !has_pvc.value.has_value() || !*has_pvc.value) {
            std::cerr << "has_service_plugin_verify_curve unexpected\n";
            return 1;
        }
        axiom::PluginCurveDesc empty_desc {};
        auto miss_cv = k_cv.plugin_create_curve("no_such_curve_plugin_xyz", empty_desc);
        if (miss_cv.status != axiom::StatusCode::InvalidInput || miss_cv.warnings.empty() ||
            miss_cv.warnings[0].code != axiom::diag_codes::kPluginNotRegistered) {
            std::cerr << "plugin_create_curve missing plugin\n";
            return 1;
        }
        auto empty_cv = k_cv.plugin_create_curve("  \t  ", empty_desc);
        if (empty_cv.status != axiom::StatusCode::InvalidInput || empty_cv.warnings.empty() ||
            empty_cv.warnings[0].code != axiom::diag_codes::kPluginCapabilityIncomplete) {
            std::cerr << "plugin_create_curve empty implementation_type_name\n";
            return 1;
        }

        class KernelLineCurvePlugin final : public axiom::ICurvePlugin {
        public:
            explicit KernelLineCurvePlugin(axiom::Kernel* k) : k_(k) {}
            std::string type_name() const override { return "kernel_line_curve_sdk_test"; }
            axiom::Result<axiom::CurveId> create(const axiom::PluginCurveDesc&) override {
                auto ln = k_->curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
                if (ln.status != axiom::StatusCode::Ok || !ln.value.has_value()) {
                    return axiom::error_result<axiom::CurveId>(ln.status, ln.diagnostic_id, std::move(ln.warnings));
                }
                return axiom::ok_result(*ln.value);
            }

        private:
            axiom::Kernel* k_;
        };

        class BogusCurveIdPlugin final : public axiom::ICurvePlugin {
        public:
            std::string type_name() const override { return "bogus_curve_id_sdk_test"; }
            axiom::Result<axiom::CurveId> create(const axiom::PluginCurveDesc&) override {
                return axiom::ok_result(axiom::CurveId {888888888ull});
            }
        };

        axiom::PluginManifest mcv;
        mcv.name = "line_curve_mf";
        mcv.version = "1";
        mcv.vendor = "v";
        mcv.capabilities = {"curve", "geo"};
        if (k_cv.register_plugin_curve(mcv, std::make_unique<KernelLineCurvePlugin>(&k_cv)).status !=
            axiom::StatusCode::Ok) {
            std::cerr << "register kernel line curve plugin failed\n";
            return 1;
        }
        axiom::PluginCurveDesc d {};
        auto got_cv = k_cv.plugin_create_curve("kernel_line_curve_sdk_test", d);
        if (got_cv.status != axiom::StatusCode::Ok || !got_cv.value.has_value() || got_cv.value->value == 0) {
            std::cerr << "plugin_create_curve should return valid curve id\n";
            return 1;
        }

        auto via_reg = k_cv.plugins().invoke_registered_curve("kernel_line_curve_sdk_test", axiom::PluginCurveDesc {});
        if (via_reg.status != axiom::StatusCode::Ok || !via_reg.value.has_value()) {
            std::cerr << "invoke_registered_curve should succeed for line plugin\n";
            return 1;
        }
        if (k_cv.verify_after_plugin_curve(*via_reg.value).status != axiom::StatusCode::Ok) {
            std::cerr << "verify_after_plugin_curve should pass for registry-created line\n";
            return 1;
        }
        if (k_cv.verify_after_plugin_curve(axiom::CurveId {999999999ull}).status == axiom::StatusCode::Ok) {
            std::cerr << "verify_after_plugin_curve should fail for unknown curve id\n";
            return 1;
        }

        axiom::PluginManifest mbad;
        mbad.name = "bogus_cv_mf";
        mbad.version = "1";
        mbad.vendor = "v";
        mbad.capabilities = {"curve"};
        if (k_cv.register_plugin_curve(mbad, std::make_unique<BogusCurveIdPlugin>()).status !=
            axiom::StatusCode::Ok) {
            std::cerr << "register bogus curve plugin failed\n";
            return 1;
        }
        auto bad_cv = k_cv.plugin_create_curve("bogus_curve_id_sdk_test", axiom::PluginCurveDesc {});
        if (bad_cv.status == axiom::StatusCode::Ok) {
            std::cerr << "plugin_create_curve expected verify failure for bogus curve id\n";
            return 1;
        }

        auto pol_c = k_cv.plugin_host_policy();
        if (pol_c.status != axiom::StatusCode::Ok || !pol_c.value.has_value()) {
            std::cerr << "plugin_host_policy curve test\n";
            return 1;
        }
        axiom::PluginHostPolicy pol_off = *pol_c.value;
        pol_off.auto_verify_curve_after_plugin_curve = false;
        if (k_cv.set_plugin_host_policy(pol_off).status != axiom::StatusCode::Ok) {
            std::cerr << "set auto_verify curve off failed\n";
            return 1;
        }
        auto pass_bogus = k_cv.plugin_create_curve("bogus_curve_id_sdk_test", axiom::PluginCurveDesc {});
        if (pass_bogus.status != axiom::StatusCode::Ok || !pass_bogus.value.has_value() ||
            pass_bogus.value->value != 888888888ull) {
            std::cerr << "plugin_create_curve with verify off should return plugin id\n";
            return 1;
        }

        axiom::PluginCurveDesc mismatch {};
        mismatch.type_name = "not_the_impl_name";
        auto mm = k_cv.plugin_create_curve("kernel_line_curve_sdk_test", mismatch);
        if (mm.status != axiom::StatusCode::InvalidInput || mm.warnings.empty() ||
            mm.warnings[0].code != axiom::diag_codes::kPluginCapabilityIncomplete) {
            std::cerr << "plugin_create_curve desc type_name mismatch expected\n";
            return 1;
        }
    }

    {
        axiom::Kernel kv;
        axiom::PluginHostPolicy p0;
        p0.require_plugin_api_version_match = false;
        if (kv.set_plugin_host_policy(p0).status != axiom::StatusCode::Ok) {
            std::cerr << "validate_plugin_manifests prep: set policy\n";
            return 1;
        }
        axiom::PluginManifest ok_m;
        ok_m.name = "ok_manifest_api";
        ok_m.version = "1";
        ok_m.vendor = "v";
        ok_m.capabilities = {"x"};
        ok_m.plugin_api_version = std::string(axiom::kPluginSdkApiVersion);
        if (kv.plugins().register_manifest_only(ok_m).status != axiom::StatusCode::Ok) {
            std::cerr << "validate_plugin_manifests prep: register ok\n";
            return 1;
        }
        axiom::PluginManifest bad_m = ok_m;
        bad_m.name = "bad_manifest_api";
        bad_m.plugin_api_version = "0.0.0-axiom-test-incompatible";
        if (kv.plugins().register_manifest_only(bad_m).status != axiom::StatusCode::Ok) {
            std::cerr << "validate_plugin_manifests prep: register bad\n";
            return 1;
        }
        axiom::PluginHostPolicy p1;
        p1.require_plugin_api_version_match = true;
        p1.plugin_api_version_match_mode = axiom::PluginApiVersionMatchMode::Exact;
        if (kv.set_plugin_host_policy(p1).status != axiom::StatusCode::Ok) {
            std::cerr << "validate_plugin_manifests prep: tighten policy\n";
            return 1;
        }
        std::vector<std::string> details;
        auto vr = kv.validate_plugin_manifests(&details);
        if (vr.status != axiom::StatusCode::InvalidInput || details.size() != 1) {
            std::cerr << "validate_plugin_manifests expected one failure\n";
            return 1;
        }
        if (details[0].find("bad_manifest_api") == std::string::npos) {
            std::cerr << "validate_plugin_manifests detail should name failing manifest\n";
            return 1;
        }
        auto vr2 = kv.validate_plugin_manifests(nullptr);
        if (vr2.status != axiom::StatusCode::InvalidInput) {
            std::cerr << "validate_plugin_manifests without details should still fail\n";
            return 1;
        }
        axiom::Kernel kclean;
        details.clear();
        if (kclean.validate_plugin_manifests(&details).status != axiom::StatusCode::Ok || !details.empty()) {
            std::cerr << "validate_plugin_manifests empty registry should ok\n";
            return 1;
        }
    }

    return 0;
}
