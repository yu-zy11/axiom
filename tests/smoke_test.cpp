#include <iostream>
#include <filesystem>
#include <array>

#include "axiom/plugin/plugin_registry.h"
#include "axiom/sdk/kernel.h"

namespace {

class DummyCurvePlugin final : public axiom::ICurvePlugin {
public:
    std::string type_name() const override { return "dummy_curve"; }
    axiom::Result<axiom::CurveId> create(const axiom::PluginCurveDesc&) override {
        return axiom::error_result<axiom::CurveId>(axiom::StatusCode::OperationFailed);
    }
};

class DummyRepairPlugin final : public axiom::IRepairPlugin {
public:
    std::string type_name() const override { return "dummy_repair"; }
    axiom::Result<axiom::OpReport> run(axiom::BodyId, axiom::RepairMode) override {
        return axiom::error_result<axiom::OpReport>(axiom::StatusCode::OperationFailed);
    }
};

class DummyImporterPlugin final : public axiom::IImporterPlugin {
public:
    std::string type_name() const override { return "dummy_importer"; }
    axiom::Result<axiom::BodyId> import_file(std::string_view) override {
        return axiom::error_result<axiom::BodyId>(axiom::StatusCode::OperationFailed);
    }
};

class DummyExporterPlugin final : public axiom::IExporterPlugin {
public:
    std::string type_name() const override { return "dummy_exporter"; }
    axiom::Result<void> export_file(axiom::BodyId, std::string_view) override {
        return axiom::error_void(axiom::StatusCode::OperationFailed);
    }
};

}  // namespace

int main() {
    axiom::Kernel kernel;

    auto box = kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 20.0, 30.0);
    if (box.status != axiom::StatusCode::Ok || !box.value.has_value()) {
        std::cerr << "box creation failed\n";
        return 1;
    }

    auto valid = kernel.validate().validate_all(*box.value, axiom::ValidationMode::Standard);
    if (valid.status != axiom::StatusCode::Ok) {
        std::cerr << "validation failed\n";
        return 1;
    }

    auto props = kernel.query().mass_properties(*box.value);
    if (props.status != axiom::StatusCode::Ok || !props.value.has_value()) {
        std::cerr << "mass properties failed\n";
        return 1;
    }

    if (props.value->volume != 6000.0) {
        std::cerr << "unexpected volume\n";
        return 1;
    }

    auto cfg = kernel.config();
    auto set_diag_off = kernel.set_enable_diagnostics(false);
    auto diag_on = kernel.enable_diagnostics();
    auto set_lin_tol = kernel.set_linear_tolerance(1e-5);
    auto lin_tol = kernel.linear_tolerance();
    auto set_ang_tol = kernel.set_angular_tolerance(1e-6);
    auto ang_tol = kernel.angular_tolerance();
    auto next_obj = kernel.next_object_id();
    auto next_diag = kernel.next_diagnostic_id();
    auto obj_count = kernel.object_count_total();
    auto geo_count = kernel.geometry_count();
    auto topo_count = kernel.topology_count();
    auto body_count = kernel.body_count();
    auto mesh_count = kernel.mesh_count();
    auto inter_count = kernel.intersection_count();
    auto eval_count = kernel.eval_node_count();
    auto cache_count = kernel.cache_entry_count();
    auto has_body = kernel.has_body_id(*box.value);
    if (cfg.status != axiom::StatusCode::Ok || !cfg.value.has_value() ||
        set_diag_off.status != axiom::StatusCode::Ok ||
        diag_on.status != axiom::StatusCode::Ok || !diag_on.value.has_value() || *diag_on.value ||
        set_lin_tol.status != axiom::StatusCode::Ok ||
        lin_tol.status != axiom::StatusCode::Ok || !lin_tol.value.has_value() || *lin_tol.value <= 0.0 ||
        set_ang_tol.status != axiom::StatusCode::Ok ||
        ang_tol.status != axiom::StatusCode::Ok || !ang_tol.value.has_value() || *ang_tol.value <= 0.0 ||
        next_obj.status != axiom::StatusCode::Ok || !next_obj.value.has_value() ||
        next_diag.status != axiom::StatusCode::Ok || !next_diag.value.has_value() ||
        obj_count.status != axiom::StatusCode::Ok || !obj_count.value.has_value() || *obj_count.value == 0 ||
        geo_count.status != axiom::StatusCode::Ok || !geo_count.value.has_value() ||
        topo_count.status != axiom::StatusCode::Ok || !topo_count.value.has_value() ||
        body_count.status != axiom::StatusCode::Ok || !body_count.value.has_value() || *body_count.value == 0 ||
        mesh_count.status != axiom::StatusCode::Ok || !mesh_count.value.has_value() ||
        inter_count.status != axiom::StatusCode::Ok || !inter_count.value.has_value() ||
        eval_count.status != axiom::StatusCode::Ok || !eval_count.value.has_value() ||
        cache_count.status != axiom::StatusCode::Ok || !cache_count.value.has_value() ||
        has_body.status != axiom::StatusCode::Ok || !has_body.value.has_value() || !*has_body.value) {
        std::cerr << "unexpected extended kernel core/sdk behavior\n";
        return 1;
    }
    auto has_curve = kernel.has_curve_id(axiom::CurveId {999999});
    auto has_surface = kernel.has_surface_id(axiom::SurfaceId {999999});
    auto has_face = kernel.has_face_id(axiom::FaceId {999999});
    auto has_shell = kernel.has_shell_id(axiom::ShellId {999999});
    auto has_edge = kernel.has_edge_id(axiom::EdgeId {999999});
    if (has_curve.status != axiom::StatusCode::Ok || !has_curve.value.has_value() || *has_curve.value ||
        has_surface.status != axiom::StatusCode::Ok || !has_surface.value.has_value() || *has_surface.value ||
        has_face.status != axiom::StatusCode::Ok || !has_face.value.has_value() || *has_face.value ||
        has_shell.status != axiom::StatusCode::Ok || !has_shell.value.has_value() || *has_shell.value ||
        has_edge.status != axiom::StatusCode::Ok || !has_edge.value.has_value() || *has_edge.value) {
        std::cerr << "unexpected kernel existence query behavior\n";
        return 1;
    }
    if (kernel.clear_curve_eval_cache().status != axiom::StatusCode::Ok ||
        kernel.clear_surface_eval_cache().status != axiom::StatusCode::Ok ||
        kernel.clear_eval_caches().status != axiom::StatusCode::Ok ||
        kernel.clear_mesh_store().status != axiom::StatusCode::Ok ||
        kernel.clear_intersections_store().status != axiom::StatusCode::Ok ||
        kernel.clear_diagnostics_store().status != axiom::StatusCode::Ok) {
        std::cerr << "unexpected kernel clear behavior\n";
        return 1;
    }
    if (kernel.reset_runtime_stores().status != axiom::StatusCode::Ok) {
        std::cerr << "unexpected kernel reset runtime behavior\n";
        return 1;
    }

    // kernel-scoped plugin registry
    auto& registry = kernel.plugins();
    axiom::PluginManifest m;
    m.name = "dummy";
    m.version = "1.0";
    m.vendor = "axiom";
    m.capabilities = {"io", "curve"};
    if (registry.register_curve_type(m, std::make_unique<DummyCurvePlugin>()).status != axiom::StatusCode::Ok ||
        registry.register_repair_plugin(m, std::make_unique<DummyRepairPlugin>()).status != axiom::StatusCode::Ok ||
        registry.register_importer(m, std::make_unique<DummyImporterPlugin>()).status != axiom::StatusCode::Ok ||
        registry.register_exporter(m, std::make_unique<DummyExporterPlugin>()).status != axiom::StatusCode::Ok) {
        std::cerr << "plugin registration failed\n";
        return 1;
    }
    auto curve_count = registry.curve_plugin_count();
    auto repair_count = registry.repair_plugin_count();
    auto importer_count = registry.importer_count();
    auto exporter_count = registry.exporter_count();
    auto manifest_count = registry.manifest_count();
    auto is_empty = registry.empty();
    auto has_manifest = registry.has_manifest("dummy");
    auto found_manifest = registry.find_manifest("dummy");
    auto names = registry.all_manifest_names();
    auto caps = registry.all_capabilities();
    auto by_cap = registry.find_by_capability("io");
    auto has_curve_plugin = registry.has_curve_type("dummy_curve");
    auto has_repair = registry.has_repair_type("dummy_repair");
    auto has_importer = registry.has_importer_type("dummy_importer");
    auto has_exporter = registry.has_exporter_type("dummy_exporter");
    auto type_counts = registry.plugin_type_counts();
    auto cap_hist = registry.capabilities_histogram();
    auto sorted_names = registry.manifest_names_sorted();
    auto has_vendor = registry.contains_vendor("axiom");
    auto by_vendor = registry.find_by_vendor("axiom");
    auto latest = registry.latest_manifest();
    auto total_slots = registry.total_plugin_slots();
    auto uniq_names = registry.manifest_names_unique();
    auto vendors = registry.vendor_names();
    auto vendor_hist = registry.vendor_histogram();
    auto cap_total = registry.capability_count_total();
    auto sorted_by_name = registry.manifests_sorted_by_name();
    auto sorted_by_vendor = registry.manifests_sorted_by_vendor();
    auto top_caps = registry.top_capabilities(2);
    std::array<std::string, 2> query_caps {"io", "curve"};
    auto any_caps = registry.manifests_with_any_capability(query_caps);
    auto without_cap = registry.manifests_without_capability("io");
    auto has_name_ci = registry.manifest_name_exists_case_insensitive("DUMMY");
    auto cap_exists = registry.capability_exists("curve");
    auto types_present = registry.plugin_types_present();
    auto formats = registry.infer_supported_io_formats();
    auto supports_step = registry.supports_io_format("step");
    auto manifest_text = registry.manifest_to_text("dummy");
    auto all_text_lines = registry.all_manifests_to_text_lines();
    auto count_cap = registry.count_manifests_with_capability("io");
    auto count_vendor = registry.count_manifests_by_vendor("axiom");
    auto first_name = registry.first_manifest_name();
    auto last_name = registry.last_manifest_name();
    auto page = registry.manifests_paginated(0, 2);
    auto summary_line = registry.registry_summary_line();
    if (curve_count.status != axiom::StatusCode::Ok || !curve_count.value.has_value() || *curve_count.value != 1 ||
        repair_count.status != axiom::StatusCode::Ok || !repair_count.value.has_value() || *repair_count.value != 1 ||
        importer_count.status != axiom::StatusCode::Ok || !importer_count.value.has_value() || *importer_count.value != 1 ||
        exporter_count.status != axiom::StatusCode::Ok || !exporter_count.value.has_value() || *exporter_count.value != 1 ||
        manifest_count.status != axiom::StatusCode::Ok || !manifest_count.value.has_value() || *manifest_count.value != 4 ||
        is_empty.status != axiom::StatusCode::Ok || !is_empty.value.has_value() || *is_empty.value ||
        has_manifest.status != axiom::StatusCode::Ok || !has_manifest.value.has_value() || !*has_manifest.value ||
        found_manifest.status != axiom::StatusCode::Ok || !found_manifest.value.has_value() ||
        names.status != axiom::StatusCode::Ok || !names.value.has_value() || names.value->empty() ||
        caps.status != axiom::StatusCode::Ok || !caps.value.has_value() || caps.value->empty() ||
        by_cap.status != axiom::StatusCode::Ok || !by_cap.value.has_value() || by_cap.value->empty() ||
        has_curve_plugin.status != axiom::StatusCode::Ok || !has_curve_plugin.value.has_value() || !*has_curve_plugin.value ||
        has_repair.status != axiom::StatusCode::Ok || !has_repair.value.has_value() || !*has_repair.value ||
        has_importer.status != axiom::StatusCode::Ok || !has_importer.value.has_value() || !*has_importer.value ||
        has_exporter.status != axiom::StatusCode::Ok || !has_exporter.value.has_value() || !*has_exporter.value ||
        type_counts.status != axiom::StatusCode::Ok || !type_counts.value.has_value() ||
        cap_hist.status != axiom::StatusCode::Ok || !cap_hist.value.has_value() ||
        sorted_names.status != axiom::StatusCode::Ok || !sorted_names.value.has_value() ||
        has_vendor.status != axiom::StatusCode::Ok || !has_vendor.value.has_value() || !*has_vendor.value ||
        by_vendor.status != axiom::StatusCode::Ok || !by_vendor.value.has_value() || by_vendor.value->empty() ||
        latest.status != axiom::StatusCode::Ok || !latest.value.has_value() ||
        total_slots.status != axiom::StatusCode::Ok || !total_slots.value.has_value() || *total_slots.value != 4 ||
        uniq_names.status != axiom::StatusCode::Ok || !uniq_names.value.has_value() || uniq_names.value->empty() ||
        vendors.status != axiom::StatusCode::Ok || !vendors.value.has_value() || vendors.value->empty() ||
        vendor_hist.status != axiom::StatusCode::Ok || !vendor_hist.value.has_value() ||
        cap_total.status != axiom::StatusCode::Ok || !cap_total.value.has_value() || *cap_total.value == 0 ||
        sorted_by_name.status != axiom::StatusCode::Ok || !sorted_by_name.value.has_value() || sorted_by_name.value->empty() ||
        sorted_by_vendor.status != axiom::StatusCode::Ok || !sorted_by_vendor.value.has_value() || sorted_by_vendor.value->empty() ||
        top_caps.status != axiom::StatusCode::Ok || !top_caps.value.has_value() || top_caps.value->empty() ||
        any_caps.status != axiom::StatusCode::Ok || !any_caps.value.has_value() || any_caps.value->empty() ||
        without_cap.status != axiom::StatusCode::Ok || !without_cap.value.has_value() ||
        has_name_ci.status != axiom::StatusCode::Ok || !has_name_ci.value.has_value() || !*has_name_ci.value ||
        cap_exists.status != axiom::StatusCode::Ok || !cap_exists.value.has_value() || !*cap_exists.value ||
        types_present.status != axiom::StatusCode::Ok || !types_present.value.has_value() || types_present.value->size() != 4 ||
        formats.status != axiom::StatusCode::Ok || !formats.value.has_value() ||
        supports_step.status != axiom::StatusCode::Ok || !supports_step.value.has_value() ||
        manifest_text.status != axiom::StatusCode::Ok || !manifest_text.value.has_value() || manifest_text.value->empty() ||
        all_text_lines.status != axiom::StatusCode::Ok || !all_text_lines.value.has_value() || all_text_lines.value->empty() ||
        count_cap.status != axiom::StatusCode::Ok || !count_cap.value.has_value() || *count_cap.value == 0 ||
        count_vendor.status != axiom::StatusCode::Ok || !count_vendor.value.has_value() || *count_vendor.value == 0 ||
        first_name.status != axiom::StatusCode::Ok || !first_name.value.has_value() || first_name.value->empty() ||
        last_name.status != axiom::StatusCode::Ok || !last_name.value.has_value() || last_name.value->empty() ||
        page.status != axiom::StatusCode::Ok || !page.value.has_value() || page.value->empty() ||
        summary_line.status != axiom::StatusCode::Ok || !summary_line.value.has_value() || summary_line.value->empty()) {
        std::cerr << "plugin registry query behavior failed\n";
        return 1;
    }
    auto manifest_txt = kernel.io().temp_path_for("axiom_plugin_manifests", ".txt");
    auto caps_txt = kernel.io().temp_path_for("axiom_plugin_caps", ".txt");
    auto summary_txt = kernel.io().temp_path_for("axiom_plugin_summary", ".txt");
    if (manifest_txt.status != axiom::StatusCode::Ok || !manifest_txt.value.has_value() ||
        caps_txt.status != axiom::StatusCode::Ok || !caps_txt.value.has_value() ||
        summary_txt.status != axiom::StatusCode::Ok || !summary_txt.value.has_value() ||
        registry.export_manifests_txt(*manifest_txt.value).status != axiom::StatusCode::Ok ||
        registry.export_capabilities_txt(*caps_txt.value).status != axiom::StatusCode::Ok ||
        registry.export_summary_txt(*summary_txt.value).status != axiom::StatusCode::Ok ||
        !std::filesystem::exists(*manifest_txt.value) || !std::filesystem::exists(*caps_txt.value) ||
        !std::filesystem::exists(*summary_txt.value)) {
        std::cerr << "plugin registry export behavior failed\n";
        return 1;
    }
    std::filesystem::remove(*manifest_txt.value);
    std::filesystem::remove(*caps_txt.value);
    std::filesystem::remove(*summary_txt.value);
    if (registry.register_manifest_only(m).status != axiom::StatusCode::Ok ||
        registry.remove_manifest_by_name("none").status != axiom::StatusCode::Ok ||
        registry.deduplicate_manifests_by_name().status != axiom::StatusCode::Ok ||
        registry.clear_plugins_keep_manifests().status != axiom::StatusCode::Ok ||
        registry.clear_manifests_keep_plugins().status != axiom::StatusCode::Ok ||
        registry.unregister_curve_type("dummy_curve").status != axiom::StatusCode::Ok ||
        registry.unregister_repair_plugin("dummy_repair").status != axiom::StatusCode::Ok ||
        registry.unregister_importer("dummy_importer").status != axiom::StatusCode::Ok ||
        registry.unregister_exporter("dummy_exporter").status != axiom::StatusCode::Ok ||
        registry.remove_manifests_by_vendor("axiom").status != axiom::StatusCode::Ok ||
        registry.remove_manifests_without_capabilities().status != axiom::StatusCode::Ok ||
        registry.clear().status != axiom::StatusCode::Ok) {
        std::cerr << "plugin registry mutation behavior failed\n";
        return 1;
    }

    auto services = kernel.services_available();
    auto modules = kernel.module_names();
    auto fmt = kernel.io_supported_formats();
    auto can_imp_step = kernel.io_can_import_format("step");
    auto can_exp_axm = kernel.io_can_export_format("axmjson");
    auto cap_lines = kernel.capability_report_lines();
    auto cap_txt = kernel.capability_report_txt();
    if (services.status != axiom::StatusCode::Ok || !services.value.has_value() || services.value->empty() ||
        modules.status != axiom::StatusCode::Ok || !modules.value.has_value() || modules.value->empty() ||
        fmt.status != axiom::StatusCode::Ok || !fmt.value.has_value() || fmt.value->size() != 2 ||
        can_imp_step.status != axiom::StatusCode::Ok || !can_imp_step.value.has_value() || !*can_imp_step.value ||
        can_exp_axm.status != axiom::StatusCode::Ok || !can_exp_axm.value.has_value() || !*can_exp_axm.value ||
        cap_lines.status != axiom::StatusCode::Ok || !cap_lines.value.has_value() || cap_lines.value->empty() ||
        cap_txt.status != axiom::StatusCode::Ok || !cap_txt.value.has_value() || cap_txt.value->empty()) {
        std::cerr << "kernel capability snapshot behavior failed\n";
        return 1;
    }

    return 0;
}
