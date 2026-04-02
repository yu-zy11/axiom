#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

namespace {

bool has_issue_code(const axiom::DiagnosticReport& report, std::string_view code) {
    for (const auto& issue : report.issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

const axiom::Issue* find_issue(const axiom::DiagnosticReport& report, std::string_view code) {
    for (const auto& issue : report.issues) {
        if (issue.code == code) {
            return &issue;
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    axiom::Kernel kernel;

    auto body = kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 20.0, 30.0);
    if (body.status != axiom::StatusCode::Ok || !body.value.has_value()) {
        std::cerr << "failed to create body for io test\n";
        return 1;
    }

    const auto uniq = std::to_string(
        static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto tmp = std::filesystem::temp_directory_path();
    const auto out_path = tmp / ("axiom_io_workflow_test_" + uniq + ".step");
    const auto out_json_path = tmp / ("axiom_io_workflow_test_" + uniq + ".axmjson");
    const auto out_gltf_path = tmp / ("axiom_io_workflow_test_" + uniq + ".gltf");
    const auto out_stl_path = tmp / ("axiom_io_workflow_test_" + uniq + ".stl");

    axiom::ExportOptions export_options;
    auto exported = kernel.io().export_step(*body.value, out_path.string(), export_options);
    if (exported.status != axiom::StatusCode::Ok) {
        std::cerr << "export failed\n";
        return 1;
    }

    axiom::ImportOptions import_options;
    auto imported = kernel.io().import_step(out_path.string(), import_options);
    if (imported.status != axiom::StatusCode::Ok || !imported.value.has_value()) {
        std::cerr << "import failed\n";
        return 1;
    }
    auto exported_json = kernel.io().export_axmjson(*body.value, out_json_path.string(), export_options);
    if (exported_json.status != axiom::StatusCode::Ok) {
        std::cerr << "axmjson export failed\n";
        std::filesystem::remove(out_path);
        return 1;
    }

    auto exported_gltf = kernel.io().export_gltf(*body.value, out_gltf_path.string(), export_options);
    if (exported_gltf.status != axiom::StatusCode::Ok) {
        std::cerr << "gltf export failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        return 1;
    }
    std::ifstream gltf_in {out_gltf_path};
    std::string gltf_text((std::istreambuf_iterator<char>(gltf_in)), std::istreambuf_iterator<char>());
    if (gltf_text.find("\"asset\"") == std::string::npos ||
        gltf_text.find("\"meshes\"") == std::string::npos ||
        gltf_text.find("\"buffers\"") == std::string::npos ||
        gltf_text.find("data:application/octet-stream;base64,") == std::string::npos) {
        std::cerr << "gltf export content is unexpected\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        return 1;
    }

    auto exported_stl = kernel.io().export_stl(*body.value, out_stl_path.string(), export_options);
    if (exported_stl.status != axiom::StatusCode::Ok) {
        std::cerr << "stl export failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        return 1;
    }
    std::ifstream stl_in {out_stl_path};
    std::string stl_text((std::istreambuf_iterator<char>(stl_in)), std::istreambuf_iterator<char>());
    if (stl_text.find("solid") == std::string::npos ||
        stl_text.find("facet normal") == std::string::npos ||
        stl_text.find("vertex") == std::string::npos ||
        stl_text.find("endsolid") == std::string::npos) {
        std::cerr << "stl export content is unexpected\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }

    auto imported_stl = kernel.io().import_auto(out_stl_path.string(), import_options);
    if (imported_stl.status != axiom::StatusCode::Ok || !imported_stl.value.has_value()) {
        std::cerr << "stl import failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }
    auto imported_stl_bbox = kernel.representation().bbox_of_body(*imported_stl.value);
    if (imported_stl_bbox.status != axiom::StatusCode::Ok || !imported_stl_bbox.value.has_value() ||
        imported_stl_bbox.value->max.x < 9.9 || imported_stl_bbox.value->max.y < 19.9 ||
        imported_stl_bbox.value->max.z < 29.9) {
        std::cerr << "stl import bbox was not plausible for box body\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }
    auto imported_stl_diag = kernel.diagnostics().get(imported_stl.diagnostic_id);
    if (imported_stl_diag.status != axiom::StatusCode::Ok || !imported_stl_diag.value.has_value() ||
        !has_issue_code(*imported_stl_diag.value, axiom::diag_codes::kIoPostImportValidation)) {
        std::cerr << "missing stl import post-validation diagnostic\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }

    auto imported_gltf = kernel.io().import_gltf(out_gltf_path.string(), import_options);
    if (imported_gltf.status != axiom::StatusCode::Ok || !imported_gltf.value.has_value()) {
        std::cerr << "gltf import failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }
    auto imported_gltf_bbox = kernel.representation().bbox_of_body(*imported_gltf.value);
    if (imported_gltf_bbox.status != axiom::StatusCode::Ok || !imported_gltf_bbox.value.has_value() ||
        imported_gltf_bbox.value->max.x < 9.9 || imported_gltf_bbox.value->max.y < 19.9 ||
        imported_gltf_bbox.value->max.z < 29.9) {
        std::cerr << "gltf import bbox was not plausible for box body\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }
    auto imported_gltf_diag = kernel.diagnostics().get(imported_gltf.diagnostic_id);
    if (imported_gltf_diag.status != axiom::StatusCode::Ok || !imported_gltf_diag.value.has_value() ||
        !has_issue_code(*imported_gltf_diag.value, axiom::diag_codes::kIoPostImportValidation)) {
        std::cerr << "missing gltf import post-validation diagnostic\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }

    auto imported_json = kernel.io().import_axmjson(out_json_path.string(), import_options);
    if (imported_json.status != axiom::StatusCode::Ok || !imported_json.value.has_value()) {
        std::cerr << "axmjson import failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        return 1;
    }
    auto imported_json_bbox = kernel.representation().bbox_of_body(*imported_json.value);
    if (imported_json_bbox.status != axiom::StatusCode::Ok || !imported_json_bbox.value.has_value() ||
        imported_json_bbox.value->max.x < 9.9 || imported_json_bbox.value->max.y < 19.9 ||
        imported_json_bbox.value->max.z < 29.9) {
        std::cerr << "axmjson import bbox was not preserved\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        return 1;
    }
    if (!imported.warnings.empty()) {
        std::cerr << "unexpected import warning for valid step\n";
        return 1;
    }

    auto import_diag = kernel.diagnostics().get(imported.diagnostic_id);
    if (import_diag.status != axiom::StatusCode::Ok || !import_diag.value.has_value() ||
        !has_issue_code(*import_diag.value, axiom::diag_codes::kIoPostImportValidation)) {
        std::cerr << "missing automatic import validation diagnostic\n";
        return 1;
    }

    auto valid = kernel.validate().validate_all(*imported.value, axiom::ValidationMode::Standard);
    if (valid.status != axiom::StatusCode::Ok) {
        std::cerr << "imported body validation failed\n";
        return 1;
    }

    const auto dirty_path = std::filesystem::temp_directory_path() / "axiom_io_workflow_dirty.step";
    {
        std::ofstream out {dirty_path};
        out << "ISO-10303-21;\n";
        out << "HEADER;\n";
        out << "AXIOM_LABEL dirty_import\n";
        out << "AXIOM_BBOX 4 4 4 1 1 1\n";
        out << "ENDSEC;\n";
        out << "END-ISO-10303-21;\n";
    }

    axiom::ImportOptions repair_import_options;
    repair_import_options.auto_repair = true;
    repair_import_options.repair_mode = axiom::RepairMode::Aggressive;
    auto repaired_import = kernel.io().import_step(dirty_path.string(), repair_import_options);
    if (repaired_import.status != axiom::StatusCode::Ok || !repaired_import.value.has_value()) {
        std::cerr << "auto repair import failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }
    if (repaired_import.warnings.empty()) {
        std::cerr << "expected repaired import to retain validation or repair warnings\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }
    auto repaired_import_diag = kernel.diagnostics().get(repaired_import.diagnostic_id);

    auto repaired_valid = kernel.validate().validate_all(*repaired_import.value, axiom::ValidationMode::Standard);
    auto repaired_sources = kernel.topology().query().source_bodies_of_body(*repaired_import.value);
    if (repaired_valid.status != axiom::StatusCode::Ok ||
        repaired_import_diag.status != axiom::StatusCode::Ok || !repaired_import_diag.value.has_value() ||
        !has_issue_code(*repaired_import_diag.value, axiom::diag_codes::kHealRepairValidated) ||
        !has_issue_code(*repaired_import_diag.value, axiom::diag_codes::kHealRepairPipelineTrace) ||
        !has_issue_code(*repaired_import_diag.value, axiom::diag_codes::kHealRepairReplaySummary) ||
        !has_issue_code(*repaired_import_diag.value, axiom::diag_codes::kIoPostImportRepairMode) ||
        repaired_sources.status != axiom::StatusCode::Ok || !repaired_sources.value.has_value() ||
        repaired_sources.value->size() != 1) {
        std::cerr << "auto repaired import did not produce a valid derived body\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }
    const auto* repaired_issue = find_issue(*repaired_import_diag.value, axiom::diag_codes::kHealRepairValidated);
    if (repaired_issue == nullptr || repaired_issue->related_entities.size() != 2 ||
        repaired_issue->related_entities.front() == repaired_issue->related_entities.back()) {
        std::cerr << "auto repaired import diagnostic does not expose expected related entities\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }

    auto exists_step = kernel.io().file_exists(out_path.string());
    auto regular_step = kernel.io().is_regular_file(out_path.string());
    auto size_step = kernel.io().file_size_bytes(out_path.string());
    auto ext_step = kernel.io().has_extension(out_path.string(), ".step");
    auto fmt_step = kernel.io().detect_format(out_path.string());
    auto is_step = kernel.io().is_step_path(out_path.string());
    auto is_axmjson = kernel.io().is_axmjson_path(out_json_path.string());
    auto is_gltf = kernel.io().is_gltf_path(out_gltf_path.string());
    auto is_stl = kernel.io().is_stl_path(out_stl_path.string());
    auto normalized = kernel.io().normalize_path(out_path.string());
    auto lines = kernel.io().count_lines(out_path.string());
    auto preview = kernel.io().read_text_preview(out_path.string(), 32);
    if (exists_step.status != axiom::StatusCode::Ok || !exists_step.value.has_value() || !*exists_step.value ||
        regular_step.status != axiom::StatusCode::Ok || !regular_step.value.has_value() || !*regular_step.value ||
        size_step.status != axiom::StatusCode::Ok || !size_step.value.has_value() || *size_step.value == 0 ||
        ext_step.status != axiom::StatusCode::Ok || !ext_step.value.has_value() || !*ext_step.value ||
        fmt_step.status != axiom::StatusCode::Ok || !fmt_step.value.has_value() || *fmt_step.value != "step" ||
        is_step.status != axiom::StatusCode::Ok || !is_step.value.has_value() || !*is_step.value ||
        is_axmjson.status != axiom::StatusCode::Ok || !is_axmjson.value.has_value() || !*is_axmjson.value ||
        is_gltf.status != axiom::StatusCode::Ok || !is_gltf.value.has_value() || !*is_gltf.value ||
        is_stl.status != axiom::StatusCode::Ok || !is_stl.value.has_value() || !*is_stl.value ||
        normalized.status != axiom::StatusCode::Ok || !normalized.value.has_value() || normalized.value->empty() ||
        lines.status != axiom::StatusCode::Ok || !lines.value.has_value() || *lines.value == 0 ||
        preview.status != axiom::StatusCode::Ok || !preview.value.has_value() || preview.value->empty()) {
        std::cerr << "io path/file helper behavior is unexpected\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }

    auto validate_import = kernel.io().validate_import_path(out_path.string());
    auto validate_export = kernel.io().validate_export_path(out_path.string());
    if (validate_import.status != axiom::StatusCode::Ok || validate_export.status != axiom::StatusCode::Ok) {
        std::cerr << "io path validation behavior is unexpected\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }
    const auto missing_validate = tmp / ("axiom_io_missing_validate_" + uniq + ".step");
    auto validate_missing = kernel.io().validate_import_path(missing_validate.string());
    if (validate_missing.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected InvalidInput for validate_import_path on missing file\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }
    auto validate_missing_diag = kernel.diagnostics().get(validate_missing.diagnostic_id);
    if (validate_missing_diag.status != axiom::StatusCode::Ok || !validate_missing_diag.value.has_value() ||
        !has_issue_code(*validate_missing_diag.value, axiom::diag_codes::kIoFileNotFound)) {
        std::cerr << "validate_import_path missing file should report kIoFileNotFound\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }

    auto temp_txt = kernel.io().temp_path_for("axiom_io_snapshot", ".txt");
    if (temp_txt.status != axiom::StatusCode::Ok || !temp_txt.value.has_value()) {
        std::cerr << "temp path generation failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }
    auto write_snapshot = kernel.io().write_text_snapshot(*temp_txt.value, "axiom-io-snapshot");
    auto export_summary = kernel.io().export_body_summary_txt(*body.value, *temp_txt.value);
    auto ensure_parent = kernel.io().ensure_parent_directory(*temp_txt.value);
    if (write_snapshot.status != axiom::StatusCode::Ok || export_summary.status != axiom::StatusCode::Ok ||
        ensure_parent.status != axiom::StatusCode::Ok) {
        std::cerr << "text snapshot/summary helpers failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }

    auto copied_step = kernel.io().temp_path_for("axiom_io_copy", ".step");
    if (copied_step.status != axiom::StatusCode::Ok || !copied_step.value.has_value()) {
        std::cerr << "copy temp path generation failed\n";
        return 1;
    }
    auto do_copy = kernel.io().copy_file(out_path.string(), *copied_step.value);
    auto rm_copy = kernel.io().remove_file(*copied_step.value);
    if (do_copy.status != axiom::StatusCode::Ok || rm_copy.status != axiom::StatusCode::Ok) {
        std::cerr << "copy/remove helper behavior is unexpected\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }

    auto import_default_step = kernel.io().import_step_default(out_path.string());
    auto import_default_json = kernel.io().import_axmjson_default(out_json_path.string());
    auto export_default_step_path = kernel.io().temp_path_for("axiom_io_export_default_step", ".step");
    auto export_default_json_path = kernel.io().temp_path_for("axiom_io_export_default_json", ".axmjson");
    if (import_default_step.status != axiom::StatusCode::Ok || !import_default_step.value.has_value() ||
        import_default_json.status != axiom::StatusCode::Ok || !import_default_json.value.has_value() ||
        export_default_step_path.status != axiom::StatusCode::Ok || !export_default_step_path.value.has_value() ||
        export_default_json_path.status != axiom::StatusCode::Ok || !export_default_json_path.value.has_value() ||
        kernel.io().export_step_default(*body.value, *export_default_step_path.value).status != axiom::StatusCode::Ok ||
        kernel.io().export_axmjson_default(*body.value, *export_default_json_path.value).status != axiom::StatusCode::Ok) {
        std::cerr << "default io wrappers behavior is unexpected\n";
        return 1;
    }

    auto auto_import_step = kernel.io().import_auto(out_path.string(), import_options);
    auto auto_import_json = kernel.io().import_auto(out_json_path.string(), import_options);
    auto auto_export_step_path = kernel.io().temp_path_for("axiom_io_export_auto_step", ".step");
    auto auto_export_json_path = kernel.io().temp_path_for("axiom_io_export_auto_json", ".axmjson");
    if (auto_import_step.status != axiom::StatusCode::Ok || !auto_import_step.value.has_value() ||
        auto_import_json.status != axiom::StatusCode::Ok || !auto_import_json.value.has_value() ||
        auto_export_step_path.status != axiom::StatusCode::Ok || !auto_export_step_path.value.has_value() ||
        auto_export_json_path.status != axiom::StatusCode::Ok || !auto_export_json_path.value.has_value() ||
        kernel.io().export_auto(*body.value, *auto_export_step_path.value, export_options).status != axiom::StatusCode::Ok ||
        kernel.io().export_auto(*body.value, *auto_export_json_path.value, export_options).status != axiom::StatusCode::Ok) {
        std::cerr << "auto io wrappers behavior is unexpected\n";
        return 1;
    }

    const std::vector<std::string> import_step_paths {out_path.string()};
    const std::vector<std::string> import_json_paths {out_json_path.string()};
    auto many_step = kernel.io().import_many_step(import_step_paths, import_options);
    auto many_json = kernel.io().import_many_axmjson(import_json_paths, import_options);
    auto many_auto = kernel.io().import_many_auto(import_json_paths, import_options);
    if (many_step.status != axiom::StatusCode::Ok || !many_step.value.has_value() || many_step.value->size() != 1 ||
        many_json.status != axiom::StatusCode::Ok || !many_json.value.has_value() || many_json.value->size() != 1 ||
        many_auto.status != axiom::StatusCode::Ok || !many_auto.value.has_value() || many_auto.value->size() != 1) {
        std::cerr << "batch import wrappers behavior is unexpected\n";
        return 1;
    }

    auto batch_step_1 = kernel.io().temp_path_for("axiom_io_batch_1", ".step");
    auto batch_step_2 = kernel.io().temp_path_for("axiom_io_batch_2", ".step");
    auto batch_json_1 = kernel.io().temp_path_for("axiom_io_batch_1", ".axmjson");
    auto batch_json_2 = kernel.io().temp_path_for("axiom_io_batch_2", ".axmjson");
    if (!batch_step_1.value.has_value() || !batch_step_2.value.has_value() ||
        !batch_json_1.value.has_value() || !batch_json_2.value.has_value()) {
        std::cerr << "batch export path generation failed\n";
        return 1;
    }
    const std::array<axiom::BodyId, 2> batch_bodies {*body.value, *imported.value};
    auto batch_validate_all = kernel.validate().validate_all_many(batch_bodies, axiom::ValidationMode::Standard);
    auto batch_validate_tol = kernel.validate().validate_tolerance_many(batch_bodies, axiom::ValidationMode::Standard);
    if (batch_validate_all.status != axiom::StatusCode::Ok || batch_validate_tol.status != axiom::StatusCode::Ok) {
        std::cerr << "batch bodies should pass Standard validate_all_many / validate_tolerance_many after io workflow\n";
        return 1;
    }
    const std::array<std::string, 2> batch_step_paths {*batch_step_1.value, *batch_step_2.value};
    const std::array<std::string, 2> batch_json_paths {*batch_json_1.value, *batch_json_2.value};
    if (kernel.io().export_many_step(batch_bodies, batch_step_paths, export_options).status != axiom::StatusCode::Ok ||
        kernel.io().export_many_axmjson(batch_bodies, batch_json_paths, export_options).status != axiom::StatusCode::Ok ||
        kernel.io().export_many_auto(batch_bodies, batch_step_paths, export_options).status != axiom::StatusCode::Ok) {
        std::cerr << "batch export wrappers behavior is unexpected\n";
        return 1;
    }

    auto step_warn_count = kernel.io().import_step_with_warnings_count(out_path.string(), import_options);
    auto json_warn_count = kernel.io().import_axmjson_with_warnings_count(out_json_path.string(), import_options);
    auto auto_warn_count = kernel.io().import_auto_with_warnings_count(out_path.string(), import_options);
    auto step_checked = kernel.io().export_step_checked(*body.value, *batch_step_1.value, export_options);
    auto json_checked = kernel.io().export_axmjson_checked(*body.value, *batch_json_1.value, export_options);
    auto auto_checked = kernel.io().export_auto_checked(*body.value, *batch_json_2.value, export_options);
    auto import_step_count = kernel.io().import_many_step_count(import_step_paths, import_options);
    auto import_json_count = kernel.io().import_many_axmjson_count(import_json_paths, import_options);
    auto import_auto_count = kernel.io().import_many_auto_count(import_step_paths, import_options);
    auto export_step_count = kernel.io().export_many_step_checked(batch_bodies, batch_step_paths, export_options);
    auto export_json_count = kernel.io().export_many_axmjson_checked(batch_bodies, batch_json_paths, export_options);
    auto export_auto_count = kernel.io().export_many_auto_checked(batch_bodies, batch_json_paths, export_options);
    if (step_warn_count.status != axiom::StatusCode::Ok || !step_warn_count.value.has_value() ||
        json_warn_count.status != axiom::StatusCode::Ok || !json_warn_count.value.has_value() ||
        auto_warn_count.status != axiom::StatusCode::Ok || !auto_warn_count.value.has_value() ||
        step_checked.status != axiom::StatusCode::Ok || !step_checked.value.has_value() || !*step_checked.value ||
        json_checked.status != axiom::StatusCode::Ok || !json_checked.value.has_value() || !*json_checked.value ||
        auto_checked.status != axiom::StatusCode::Ok || !auto_checked.value.has_value() || !*auto_checked.value ||
        import_step_count.status != axiom::StatusCode::Ok || !import_step_count.value.has_value() || *import_step_count.value != 1 ||
        import_json_count.status != axiom::StatusCode::Ok || !import_json_count.value.has_value() || *import_json_count.value != 1 ||
        import_auto_count.status != axiom::StatusCode::Ok || !import_auto_count.value.has_value() || *import_auto_count.value != 1 ||
        export_step_count.status != axiom::StatusCode::Ok || !export_step_count.value.has_value() || *export_step_count.value != 2 ||
        export_json_count.status != axiom::StatusCode::Ok || !export_json_count.value.has_value() || *export_json_count.value != 2 ||
        export_auto_count.status != axiom::StatusCode::Ok || !export_auto_count.value.has_value() || *export_auto_count.value != 2) {
        std::cerr << "io checked/count wrappers behavior is unexpected\n";
        return 1;
    }

    const std::array<std::string, 3> candidates {"/tmp/non-existent-axiom.file", out_json_path.string(), out_path.string()};
    auto scanned_formats = kernel.io().scan_formats(candidates);
    auto existing_count = kernel.io().count_existing_files(candidates);
    auto existing_files = kernel.io().filter_existing_files(candidates);
    auto missing_files = kernel.io().filter_missing_files(candidates);
    auto first_missing = kernel.io().first_missing_file(candidates);
    auto first_existing = kernel.io().first_existing_file(candidates);
    if (scanned_formats.status != axiom::StatusCode::Ok || !scanned_formats.value.has_value() || scanned_formats.value->size() != 3 ||
        existing_count.status != axiom::StatusCode::Ok || !existing_count.value.has_value() || *existing_count.value != 2 ||
        existing_files.status != axiom::StatusCode::Ok || !existing_files.value.has_value() || existing_files.value->size() != 2 ||
        missing_files.status != axiom::StatusCode::Ok || !missing_files.value.has_value() || missing_files.value->size() != 1 ||
        first_missing.status != axiom::StatusCode::Ok || !first_missing.value.has_value() || first_missing.value->empty() ||
        first_existing.status != axiom::StatusCode::Ok || !first_existing.value.has_value() || first_existing.value->empty()) {
        std::cerr << "io candidate filtering behavior is unexpected\n";
        return 1;
    }

    auto sanitized = kernel.io().sanitize_export_stem("a x/i:o*m?");
    auto composed = kernel.io().compose_path(std::filesystem::temp_directory_path().string(), "axiom_compose", "txt");
    auto changed = kernel.io().change_extension(out_path.string(), "axmjson");
    auto base = kernel.io().basename(out_path.string());
    auto dir = kernel.io().dirname(out_path.string());
    auto mtime = kernel.io().file_mtime_unix(out_path.string());
    if (sanitized.status != axiom::StatusCode::Ok || !sanitized.value.has_value() ||
        composed.status != axiom::StatusCode::Ok || !composed.value.has_value() ||
        changed.status != axiom::StatusCode::Ok || !changed.value.has_value() ||
        base.status != axiom::StatusCode::Ok || !base.value.has_value() || base.value->empty() ||
        dir.status != axiom::StatusCode::Ok || !dir.value.has_value() || dir.value->empty() ||
        mtime.status != axiom::StatusCode::Ok || !mtime.value.has_value()) {
        std::cerr << "io path compose behavior is unexpected\n";
        return 1;
    }

    auto text_file = kernel.io().temp_path_for("axiom_io_text_ops", ".txt");
    if (text_file.status != axiom::StatusCode::Ok || !text_file.value.has_value()) {
        std::cerr << "text file path generation failed\n";
        return 1;
    }
    auto touch = kernel.io().touch_empty_file(*text_file.value);
    auto append = kernel.io().append_text(*text_file.value, "line-1\nline-2\n");
    auto all_text = kernel.io().read_all_text(*text_file.value);
    auto same_text = kernel.io().compare_file_text(*text_file.value, *text_file.value);
    auto export_many_summary = kernel.io().export_bodies_summary_txt(batch_bodies, *text_file.value);
    auto import_candidate = kernel.io().import_auto_from_candidates(candidates, import_options);
    if (touch.status != axiom::StatusCode::Ok || append.status != axiom::StatusCode::Ok ||
        all_text.status != axiom::StatusCode::Ok || !all_text.value.has_value() ||
        same_text.status != axiom::StatusCode::Ok || !same_text.value.has_value() || !*same_text.value ||
        export_many_summary.status != axiom::StatusCode::Ok ||
        import_candidate.status != axiom::StatusCode::Ok || !import_candidate.value.has_value()) {
        std::cerr << "io text and candidate helpers behavior is unexpected\n";
        return 1;
    }

    const std::array<std::string, 3> io_paths {out_path.string(), out_json_path.string(), "/tmp/axiom_missing_unknown.ext"};
    auto count_importable = kernel.io().count_importable_paths(io_paths);
    auto count_exportable = kernel.io().count_exportable_paths(io_paths);
    auto step_only = kernel.io().filter_step_paths(io_paths);
    auto json_only = kernel.io().filter_axmjson_paths(io_paths);
    auto unknown_only = kernel.io().filter_unknown_format_paths(io_paths);
    auto normalized_many = kernel.io().normalize_paths(io_paths);
    std::array<std::string, 2> names {"n1", "n2"};
    auto composed_many = kernel.io().compose_paths(std::filesystem::temp_directory_path().string(), names, "txt");
    auto changed_many = kernel.io().change_extensions(io_paths, "bak");
    auto basenames = kernel.io().basenames(io_paths);
    auto dirnames = kernel.io().dirnames(io_paths);
    auto sizes = kernel.io().file_sizes(io_paths);
    auto mtimes = kernel.io().file_mtimes(io_paths);
    auto first_importable = kernel.io().first_importable_path(io_paths);
    auto first_exportable = kernel.io().first_exportable_path(io_paths);
    auto detect_with_paths = kernel.io().detect_formats_with_paths(io_paths);
    if (count_importable.status != axiom::StatusCode::Ok || !count_importable.value.has_value() || *count_importable.value < 2 ||
        count_exportable.status != axiom::StatusCode::Ok || !count_exportable.value.has_value() || *count_exportable.value < 2 ||
        step_only.status != axiom::StatusCode::Ok || !step_only.value.has_value() || step_only.value->size() != 1 ||
        json_only.status != axiom::StatusCode::Ok || !json_only.value.has_value() || json_only.value->size() != 1 ||
        unknown_only.status != axiom::StatusCode::Ok || !unknown_only.value.has_value() || unknown_only.value->size() != 1 ||
        normalized_many.status != axiom::StatusCode::Ok || !normalized_many.value.has_value() || normalized_many.value->size() != 3 ||
        composed_many.status != axiom::StatusCode::Ok || !composed_many.value.has_value() || composed_many.value->size() != 2 ||
        changed_many.status != axiom::StatusCode::Ok || !changed_many.value.has_value() || changed_many.value->size() != 3 ||
        basenames.status != axiom::StatusCode::Ok || !basenames.value.has_value() || basenames.value->size() != 3 ||
        dirnames.status != axiom::StatusCode::Ok || !dirnames.value.has_value() || dirnames.value->size() != 3 ||
        sizes.status != axiom::StatusCode::Ok || !sizes.value.has_value() || sizes.value->size() != 3 ||
        mtimes.status != axiom::StatusCode::Ok || !mtimes.value.has_value() || mtimes.value->size() != 3 ||
        first_importable.status != axiom::StatusCode::Ok || !first_importable.value.has_value() || first_importable.value->empty() ||
        first_exportable.status != axiom::StatusCode::Ok || !first_exportable.value.has_value() || first_exportable.value->empty() ||
        detect_with_paths.status != axiom::StatusCode::Ok || !detect_with_paths.value.has_value() || detect_with_paths.value->size() != 3) {
        std::cerr << "extended io path batch behavior is unexpected\n";
        return 1;
    }

    auto tmp_batch_a = kernel.io().temp_path_for("axiom_batch_a", ".txt");
    auto tmp_batch_b = kernel.io().temp_path_for("axiom_batch_b", ".txt");
    std::array<std::string, 2> batch_text_paths {*tmp_batch_a.value, *tmp_batch_b.value};
    if (kernel.io().ensure_parent_directories(batch_text_paths).status != axiom::StatusCode::Ok ||
        kernel.io().touch_empty_files(batch_text_paths).status != axiom::StatusCode::Ok ||
        kernel.io().append_text_many(batch_text_paths, "x\n").status != axiom::StatusCode::Ok) {
        std::cerr << "extended io batch file create/append behavior is unexpected\n";
        return 1;
    }
    auto all_many = kernel.io().read_all_text_many(batch_text_paths);
    auto line_many = kernel.io().count_lines_many(batch_text_paths);
    auto preview_many = kernel.io().read_text_preview_many(batch_text_paths, 4);
    auto compare_many = kernel.io().compare_file_text_many_equal(batch_text_paths, batch_text_paths);
    auto files_summary_lines = kernel.io().summarize_files_txt(batch_text_paths);
    auto summary_out = kernel.io().temp_path_for("axiom_files_summary", ".txt");
    auto export_files_summary = kernel.io().export_files_summary_txt(batch_text_paths, *summary_out.value);
    if (all_many.status != axiom::StatusCode::Ok || !all_many.value.has_value() || all_many.value->size() != 2 ||
        line_many.status != axiom::StatusCode::Ok || !line_many.value.has_value() || line_many.value->size() != 2 ||
        preview_many.status != axiom::StatusCode::Ok || !preview_many.value.has_value() || preview_many.value->size() != 2 ||
        compare_many.status != axiom::StatusCode::Ok || !compare_many.value.has_value() || *compare_many.value != 2 ||
        files_summary_lines.status != axiom::StatusCode::Ok || !files_summary_lines.value.has_value() || files_summary_lines.value->size() != 2 ||
        export_files_summary.status != axiom::StatusCode::Ok) {
        std::cerr << "extended io batch read/summary behavior is unexpected\n";
        return 1;
    }
    {
        const auto missing_read = tmp / ("axiom_missing_read_" + uniq + ".txt");
        const std::vector<std::string> bad_read {*tmp_batch_a.value, missing_read.string()};
        auto read_fail = kernel.io().read_all_text_many(bad_read);
        if (read_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected read_all_text_many failure when a file is missing\n";
            return 1;
        }
        auto read_diag = kernel.diagnostics().get(read_fail.diagnostic_id);
        const auto* read_ctx = read_diag.status == axiom::StatusCode::Ok && read_diag.value.has_value()
                                  ? find_issue(*read_diag.value, axiom::diag_codes::kIoBatchReadItemContext)
                                  : nullptr;
        if (read_ctx == nullptr || read_ctx->stage != "io.batch_read" ||
            !has_issue_code(*read_diag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "read_all_text_many batch failure missing batch context or merged read root cause\n";
            return 1;
        }
    }
    {
        const auto missing_cmp = tmp / ("axiom_missing_cmp_" + uniq + ".txt");
        const std::vector<std::string> lhs_cmp {*tmp_batch_a.value, *tmp_batch_b.value};
        const std::vector<std::string> rhs_cmp {*tmp_batch_a.value, missing_cmp.string()};
        auto cmp_fail = kernel.io().compare_file_text_many_equal(lhs_cmp, rhs_cmp);
        if (cmp_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected compare_file_text_many_equal failure when rhs file is missing\n";
            return 1;
        }
        auto cmp_diag = kernel.diagnostics().get(cmp_fail.diagnostic_id);
        const auto* cmp_ctx = cmp_diag.status == axiom::StatusCode::Ok && cmp_diag.value.has_value()
                                  ? find_issue(*cmp_diag.value, axiom::diag_codes::kIoBatchCompareItemContext)
                                  : nullptr;
        if (cmp_ctx == nullptr || cmp_ctx->stage != "io.batch_compare" ||
            !has_issue_code(*cmp_diag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "compare_file_text_many_equal batch failure missing batch compare context or root cause\n";
            return 1;
        }
    }
    {
        const std::vector<std::string> bad_append {*tmp_batch_a.value, std::string{}};
        auto append_fail = kernel.io().append_text_many(bad_append, "z\n");
        if (append_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected append_text_many failure when a path is empty\n";
            return 1;
        }
        auto append_diag = kernel.diagnostics().get(append_fail.diagnostic_id);
        const auto* append_ctx = append_diag.status == axiom::StatusCode::Ok && append_diag.value.has_value()
                                     ? find_issue(*append_diag.value, axiom::diag_codes::kIoBatchPathOpItemContext)
                                     : nullptr;
        if (append_ctx == nullptr || append_ctx->stage != "io.batch_path_op" ||
            !has_issue_code(*append_diag.value, axiom::diag_codes::kIoExportFailure)) {
            std::cerr << "append_text_many batch failure missing batch path_op context or merged export root cause\n";
            return 1;
        }
    }

    auto import_existing = kernel.io().import_existing_auto(io_paths, import_options);
    auto import_existing_count = kernel.io().import_existing_auto_count(io_paths, import_options);
    auto export_dir = std::filesystem::temp_directory_path() / "axiom_io_export_dir";
    std::filesystem::create_directories(export_dir);
    auto export_to_dir = kernel.io().export_auto_to_directory(batch_bodies, export_dir.string(), "step", export_options);
    auto export_to_dir_count = kernel.io().export_auto_to_directory_count(batch_bodies, export_dir.string(), "axmjson", export_options);
    auto body_summary_many_a = kernel.io().temp_path_for("axiom_body_summary_a", ".txt");
    auto body_summary_many_b = kernel.io().temp_path_for("axiom_body_summary_b", ".txt");
    std::array<std::string, 2> body_summary_paths {*body_summary_many_a.value, *body_summary_many_b.value};
    auto export_body_summaries_many = kernel.io().export_body_summaries_many(batch_bodies, body_summary_paths);
    if (import_existing.status != axiom::StatusCode::Ok || !import_existing.value.has_value() ||
        import_existing_count.status != axiom::StatusCode::Ok || !import_existing_count.value.has_value() ||
        export_to_dir.status != axiom::StatusCode::Ok ||
        export_to_dir_count.status != axiom::StatusCode::Ok || !export_to_dir_count.value.has_value() || *export_to_dir_count.value != 2 ||
        export_body_summaries_many.status != axiom::StatusCode::Ok) {
        std::cerr << "extended io import/export directory behavior is unexpected\n";
        return 1;
    }
    {
        std::array<std::string, 2> bad_summary_paths {*body_summary_many_a.value, export_dir.string()};
        auto summary_fail = kernel.io().export_body_summaries_many(batch_bodies, bad_summary_paths);
        if (summary_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected export_body_summaries_many failure when output path is a directory\n";
            return 1;
        }
        auto summary_diag = kernel.diagnostics().get(summary_fail.diagnostic_id);
        const auto* summary_ctx = summary_diag.status == axiom::StatusCode::Ok && summary_diag.value.has_value()
                                      ? find_issue(*summary_diag.value, axiom::diag_codes::kIoBatchExportItemContext)
                                      : nullptr;
        if (summary_ctx == nullptr || summary_ctx->stage != "io.batch_export" ||
            !has_issue_code(*summary_diag.value, axiom::diag_codes::kIoExportFailure)) {
            std::cerr << "export_body_summaries_many failure missing batch export context or root cause\n";
            return 1;
        }
    }
    if (kernel.io().remove_files(batch_text_paths).status != axiom::StatusCode::Ok) {
        std::cerr << "extended io batch remove behavior is unexpected\n";
        return 1;
    }

    std::vector<std::string> extra_paths {out_path.string(), out_json_path.string(), out_path.string()};
    auto canon = kernel.io().canonical_or_normalized_path(out_path.string());
    auto rel = kernel.io().relative_to(out_path.string(), std::filesystem::temp_directory_path().string());
    auto common_dir = kernel.io().common_parent_directory(extra_paths);
    auto unique = kernel.io().unique_paths(extra_paths);
    auto sorted = kernel.io().sort_paths_lex(extra_paths);
    auto by_fmt = kernel.io().count_by_format(extra_paths);
    auto only_step = kernel.io().paths_of_format(extra_paths, "step");
    if (canon.status != axiom::StatusCode::Ok || !canon.value.has_value() ||
        rel.status != axiom::StatusCode::Ok || !rel.value.has_value() ||
        common_dir.status != axiom::StatusCode::Ok || !common_dir.value.has_value() || common_dir.value->empty() ||
        unique.status != axiom::StatusCode::Ok || !unique.value.has_value() || unique.value->size() != 2 ||
        sorted.status != axiom::StatusCode::Ok || !sorted.value.has_value() || sorted.value->size() != 3 ||
        by_fmt.status != axiom::StatusCode::Ok || !by_fmt.value.has_value() || by_fmt.value->empty() ||
        only_step.status != axiom::StatusCode::Ok || !only_step.value.has_value() || only_step.value->size() != 2) {
        std::cerr << "extra io path organize behavior is unexpected\n";
        return 1;
    }

    auto move_src = kernel.io().temp_path_for("axiom_move_src", ".txt");
    auto move_dst = kernel.io().temp_path_for("axiom_move_dst", ".txt");
    auto rename_dst = kernel.io().temp_path_for("axiom_rename_dst", ".txt");
    if (!move_src.value.has_value() || !move_dst.value.has_value() || !rename_dst.value.has_value() ||
        kernel.io().write_text_snapshot(*move_src.value, "abc").status != axiom::StatusCode::Ok ||
        kernel.io().move_file(*move_src.value, *move_dst.value).status != axiom::StatusCode::Ok ||
        kernel.io().rename_file(*move_dst.value, *rename_dst.value).status != axiom::StatusCode::Ok) {
        std::cerr << "extra io move/rename behavior is unexpected\n";
        return 1;
    }

    auto scan_dir = std::filesystem::temp_directory_path() / "axiom_io_scan_dir";
    auto nested_dir = scan_dir / "nested";
    if (kernel.io().ensure_directory(scan_dir.string()).status != axiom::StatusCode::Ok ||
        kernel.io().ensure_directory(nested_dir.string()).status != axiom::StatusCode::Ok) {
        std::cerr << "ensure directory failed\n";
        return 1;
    }
    auto scan_a = scan_dir / "a.txt";
    auto scan_b = nested_dir / "b.txt";
    kernel.io().write_text_snapshot(scan_a.string(), "A");
    kernel.io().write_text_snapshot(scan_b.string(), "B");
    auto dir_exists = kernel.io().directory_exists(scan_dir.string());
    auto list_flat = kernel.io().list_files_in_directory(scan_dir.string());
    auto list_rec = kernel.io().list_files_recursive(scan_dir.string());
    auto cnt_flat = kernel.io().count_files_in_directory(scan_dir.string(), false);
    auto cnt_rec = kernel.io().count_files_in_directory(scan_dir.string(), true);
    if (dir_exists.status != axiom::StatusCode::Ok || !dir_exists.value.has_value() || !*dir_exists.value ||
        list_flat.status != axiom::StatusCode::Ok || !list_flat.value.has_value() || list_flat.value->size() != 1 ||
        list_rec.status != axiom::StatusCode::Ok || !list_rec.value.has_value() || list_rec.value->size() != 2 ||
        cnt_flat.status != axiom::StatusCode::Ok || !cnt_flat.value.has_value() || *cnt_flat.value != 1 ||
        cnt_rec.status != axiom::StatusCode::Ok || !cnt_rec.value.has_value() || *cnt_rec.value != 2) {
        std::cerr << "extra io directory scan behavior is unexpected\n";
        return 1;
    }

    auto line_file = kernel.io().temp_path_for("axiom_line_ops", ".txt");
    std::array<std::string, 3> lines_to_write {"alpha", "beta", "alpha-beta"};
    if (!line_file.value.has_value() ||
        kernel.io().write_lines(*line_file.value, lines_to_write).status != axiom::StatusCode::Ok) {
        std::cerr << "write lines failed\n";
        return 1;
    }
    auto lines_read = kernel.io().read_lines(*line_file.value);
    auto lines_hit = kernel.io().grep_lines_contains(*line_file.value, "alpha");
    auto replaced = kernel.io().replace_in_file_text(*line_file.value, "alpha", "A");
    auto prepended = kernel.io().prepend_text(*line_file.value, "head\n");
    auto stem = kernel.io().file_stem(*line_file.value);
    auto ext = kernel.io().extension_of(*line_file.value);
    auto with_stem = kernel.io().with_stem(*line_file.value, "axiom_line_ops_2");
    auto with_suffix = kernel.io().append_suffix_before_ext(*line_file.value, "_v2");
    auto seq = kernel.io().generate_sequential_paths(std::filesystem::temp_directory_path().string(), "axiom_seq_", "txt", 3);
    std::array<std::string, 2> writable_candidates {"/tmp/no/such/dir/file.txt", *line_file.value};
    auto writable = kernel.io().first_writable_path(writable_candidates);
    auto strict_import = kernel.io().import_auto_existing_strict(import_step_paths, import_options);
    auto fmt_hist = kernel.io().summarize_format_histogram_txt(io_paths);
    if (lines_read.status != axiom::StatusCode::Ok || !lines_read.value.has_value() || lines_read.value->size() != 3 ||
        lines_hit.status != axiom::StatusCode::Ok || !lines_hit.value.has_value() || lines_hit.value->size() != 2 ||
        replaced.status != axiom::StatusCode::Ok || !replaced.value.has_value() || *replaced.value == 0 ||
        prepended.status != axiom::StatusCode::Ok ||
        stem.status != axiom::StatusCode::Ok || !stem.value.has_value() || stem.value->empty() ||
        ext.status != axiom::StatusCode::Ok || !ext.value.has_value() || ext.value->empty() ||
        with_stem.status != axiom::StatusCode::Ok || !with_stem.value.has_value() || with_stem.value->empty() ||
        with_suffix.status != axiom::StatusCode::Ok || !with_suffix.value.has_value() || with_suffix.value->empty() ||
        seq.status != axiom::StatusCode::Ok || !seq.value.has_value() || seq.value->size() != 3 ||
        writable.status != axiom::StatusCode::Ok || !writable.value.has_value() || writable.value->empty() ||
        strict_import.status != axiom::StatusCode::Ok || !strict_import.value.has_value() || strict_import.value->empty() ||
        fmt_hist.status != axiom::StatusCode::Ok || !fmt_hist.value.has_value() || fmt_hist.value->empty()) {
        std::cerr << "extra io text/stem/hist behavior is unexpected\n";
        return 1;
    }
    auto conditional_export = kernel.io().export_auto_existing_only(batch_bodies, batch_step_paths, export_options);
    if (conditional_export.status != axiom::StatusCode::Ok || !conditional_export.value.has_value() ||
        *conditional_export.value != 2 ||
        kernel.io().truncate_file(*line_file.value).status != axiom::StatusCode::Ok) {
        std::cerr << "extra io conditional export/truncate behavior is unexpected\n";
        return 1;
    }

    const auto p_iges = tmp / ("axiom_io_interchange_" + uniq + ".iges");
    const auto p_brep = tmp / ("axiom_io_interchange_" + uniq + ".brep");
    const auto p_obj = tmp / ("axiom_io_interchange_" + uniq + ".obj");
    const auto p_3mf = tmp / ("axiom_io_interchange_" + uniq + ".3mf");
    const auto p_stl_report = tmp / ("axiom_io_mesh_report_" + uniq + ".stl");
    const auto p_sidecar = tmp / ("axiom_io_mesh_report_" + uniq + ".mesh_report.json");
    if (kernel.io().export_iges(*body.value, p_iges.string(), export_options).status != axiom::StatusCode::Ok ||
        kernel.io().export_brep(*body.value, p_brep.string(), export_options).status != axiom::StatusCode::Ok ||
        kernel.io().export_obj(*body.value, p_obj.string(), export_options).status != axiom::StatusCode::Ok ||
        kernel.io().export_3mf(*body.value, p_3mf.string(), export_options).status != axiom::StatusCode::Ok) {
        std::cerr << "interchange mesh export (iges/brep/obj/3mf) failed\n";
        return 1;
    }
    auto fmt_iges = kernel.io().detect_format(p_iges.string());
    auto fmt_brep = kernel.io().detect_format(p_brep.string());
    auto fmt_obj = kernel.io().detect_format(p_obj.string());
    auto fmt_3mf = kernel.io().detect_format(p_3mf.string());
    if (fmt_iges.status != axiom::StatusCode::Ok || !fmt_iges.value.has_value() || *fmt_iges.value != "iges" ||
        fmt_brep.status != axiom::StatusCode::Ok || !fmt_brep.value.has_value() || *fmt_brep.value != "brep" ||
        fmt_obj.status != axiom::StatusCode::Ok || !fmt_obj.value.has_value() || *fmt_obj.value != "obj" ||
        fmt_3mf.status != axiom::StatusCode::Ok || !fmt_3mf.value.has_value() || *fmt_3mf.value != "3mf") {
        std::cerr << "detect_format for iges/brep/obj/3mf mismatch\n";
        return 1;
    }
    auto imp_iges = kernel.io().import_auto(p_iges.string(), import_options);
    auto imp_brep = kernel.io().import_auto(p_brep.string(), import_options);
    auto imp_obj = kernel.io().import_auto(p_obj.string(), import_options);
    auto imp_3mf = kernel.io().import_auto(p_3mf.string(), import_options);
    if (imp_iges.status != axiom::StatusCode::Ok || !imp_iges.value.has_value() ||
        imp_brep.status != axiom::StatusCode::Ok || !imp_brep.value.has_value() ||
        imp_obj.status != axiom::StatusCode::Ok || !imp_obj.value.has_value() ||
        imp_3mf.status != axiom::StatusCode::Ok || !imp_3mf.value.has_value()) {
        std::cerr << "import_auto roundtrip for iges/brep/obj/3mf failed\n";
        return 1;
    }
    auto bb_iges = kernel.representation().bbox_of_body(*imp_iges.value);
    auto bb_brep = kernel.representation().bbox_of_body(*imp_brep.value);
    auto bb_obj = kernel.representation().bbox_of_body(*imp_obj.value);
    auto bb_3mf = kernel.representation().bbox_of_body(*imp_3mf.value);
    if (bb_iges.status != axiom::StatusCode::Ok || !bb_iges.value.has_value() || bb_iges.value->max.x < 9.9 ||
        bb_brep.status != axiom::StatusCode::Ok || !bb_brep.value.has_value() || bb_brep.value->max.x < 9.9 ||
        bb_obj.status != axiom::StatusCode::Ok || !bb_obj.value.has_value() || bb_obj.value->max.x < 9.9 ||
        bb_3mf.status != axiom::StatusCode::Ok || !bb_3mf.value.has_value() || bb_3mf.value->max.x < 9.9) {
        std::cerr << "interchange import bbox not plausible for box body\n";
        return 1;
    }
    std::ifstream iges_in {p_iges};
    std::string iges_txt((std::istreambuf_iterator<char>(iges_in)), std::istreambuf_iterator<char>());
    if (iges_txt.find("START") == std::string::npos || iges_txt.find("TERMINATE") == std::string::npos) {
        std::cerr << "iges export missing expected markers\n";
        return 1;
    }
    std::ifstream brep_in {p_brep};
    std::string brep_txt((std::istreambuf_iterator<char>(brep_in)), std::istreambuf_iterator<char>());
    if (brep_txt.find("AXIOM_BREP_INTERCHANGE") == std::string::npos ||
        brep_txt.find("\"format\"") == std::string::npos || brep_txt.find("AXIOM_BREP") == std::string::npos) {
        std::cerr << "brep export missing axiom interchange payload\n";
        return 1;
    }
    std::ifstream obj_in {p_obj};
    std::string obj_txt((std::istreambuf_iterator<char>(obj_in)), std::istreambuf_iterator<char>());
    if (obj_txt.find("v ") == std::string::npos || obj_txt.find("f ") == std::string::npos) {
        std::cerr << "obj export missing vertices/faces\n";
        return 1;
    }
    axiom::ExportOptions report_opts = export_options;
    report_opts.write_mesh_validation_report = true;
    auto ex_stl_report = kernel.io().export_stl(*body.value, p_stl_report.string(), report_opts);
    if (ex_stl_report.status != axiom::StatusCode::Ok) {
        std::cerr << "stl export with mesh validation sidecar failed\n";
        return 1;
    }
    if (!std::filesystem::exists(p_sidecar)) {
        std::cerr << "expected mesh_report sidecar next to stl export\n";
        return 1;
    }
    auto sidecar_diag = kernel.diagnostics().get(ex_stl_report.diagnostic_id);
    if (sidecar_diag.status != axiom::StatusCode::Ok || !sidecar_diag.value.has_value() ||
        !has_issue_code(*sidecar_diag.value, axiom::diag_codes::kIoExportMeshReportSidecar)) {
        std::cerr << "missing mesh export sidecar diagnostic code\n";
        return 1;
    }
    std::ifstream sidecar_in {p_sidecar};
    std::string sidecar_json((std::istreambuf_iterator<char>(sidecar_in)), std::istreambuf_iterator<char>());
    if (sidecar_json.find('{') == std::string::npos || sidecar_json.find('}') == std::string::npos) {
        std::cerr << "mesh_report sidecar is not plausible json\n";
        return 1;
    }

    const auto bad_ext_path = tmp / ("axiom_io_unknown_ext_" + uniq + ".unsupported_io_ext");
    {
        std::ofstream bf(bad_ext_path);
        bf << "not a registered interchange format\n";
    }
    auto imp_unknown = kernel.io().import_auto(bad_ext_path.string(), import_options);
    if (imp_unknown.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected InvalidInput for import_auto with unknown extension\n";
        std::filesystem::remove(bad_ext_path);
        return 1;
    }
    auto imp_unknown_diag = kernel.diagnostics().get(imp_unknown.diagnostic_id);
    if (imp_unknown_diag.status != axiom::StatusCode::Ok || !imp_unknown_diag.value.has_value() ||
        !has_issue_code(*imp_unknown_diag.value, axiom::diag_codes::kIoImportFailure)) {
        std::cerr << "missing kIoImportFailure diagnostic for unknown format import\n";
        std::filesystem::remove(bad_ext_path);
        return 1;
    }
    auto exp_unknown = kernel.io().export_auto(*body.value, bad_ext_path.string(), export_options);
    if (exp_unknown.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected InvalidInput for export_auto with unknown extension\n";
        std::filesystem::remove(bad_ext_path);
        return 1;
    }
    auto exp_unknown_diag = kernel.diagnostics().get(exp_unknown.diagnostic_id);
    if (exp_unknown_diag.status != axiom::StatusCode::Ok || !exp_unknown_diag.value.has_value() ||
        !has_issue_code(*exp_unknown_diag.value, axiom::diag_codes::kIoExportFailure)) {
        std::cerr << "missing kIoExportFailure diagnostic for unknown format export\n";
        std::filesystem::remove(bad_ext_path);
        return 1;
    }
    std::filesystem::remove(bad_ext_path);

    const auto bad_batch_item = tmp / ("axiom_io_batch_item_bad_" + uniq + ".unsupported_io_ext");
    {
        std::ofstream bf(bad_batch_item);
        bf << "batch item placeholder\n";
    }
    const std::vector<std::string> batch_import_mixed {out_path.string(), bad_batch_item.string()};
    auto batch_import_fail = kernel.io().import_many_auto(batch_import_mixed, import_options);
    if (batch_import_fail.status == axiom::StatusCode::Ok) {
        std::cerr << "expected batch import failure when second path has unknown extension\n";
        std::filesystem::remove(bad_batch_item);
        return 1;
    }
    auto batch_import_diag = kernel.diagnostics().get(batch_import_fail.diagnostic_id);
    const auto* batch_import_ctx = batch_import_diag.status == axiom::StatusCode::Ok && batch_import_diag.value.has_value()
                                      ? find_issue(*batch_import_diag.value, axiom::diag_codes::kIoBatchImportItemContext)
                                      : nullptr;
    if (batch_import_ctx == nullptr || batch_import_ctx->stage != "io.batch_import" ||
        !has_issue_code(*batch_import_diag.value, axiom::diag_codes::kIoImportFailure)) {
        std::cerr << "batch import failure diagnostic missing batch context or merged root cause\n";
        std::filesystem::remove(bad_batch_item);
        return 1;
    }

    const auto bad_batch_export_path = tmp / ("axiom_io_batch_export_bad_" + uniq + ".unsupported_io_ext");
    const std::array<axiom::BodyId, 2> batch_export_bodies {*body.value, *body.value};
    const std::array<std::string, 2> batch_export_paths {out_path.string(), bad_batch_export_path.string()};
    auto batch_export_fail = kernel.io().export_many_auto(batch_export_bodies, batch_export_paths, export_options);
    if (batch_export_fail.status == axiom::StatusCode::Ok) {
        std::cerr << "expected batch export failure when second path has unknown extension\n";
        std::filesystem::remove(bad_batch_item);
        return 1;
    }
    auto batch_export_diag = kernel.diagnostics().get(batch_export_fail.diagnostic_id);
    const auto* batch_export_ctx = batch_export_diag.status == axiom::StatusCode::Ok && batch_export_diag.value.has_value()
                                       ? find_issue(*batch_export_diag.value, axiom::diag_codes::kIoBatchExportItemContext)
                                       : nullptr;
    if (batch_export_ctx == nullptr || batch_export_ctx->stage != "io.batch_export" ||
        batch_export_ctx->related_entities.empty() || batch_export_ctx->related_entities[0] != body.value->value ||
        !has_issue_code(*batch_export_diag.value, axiom::diag_codes::kIoExportFailure)) {
        std::cerr << "batch export failure diagnostic missing batch context, body relation, or merged root cause\n";
        std::filesystem::remove(bad_batch_item);
        return 1;
    }
    std::filesystem::remove(bad_batch_item);

    {
        const std::vector<std::string> detect_mixed {out_path.string(), std::string{}};
        auto detect_fail = kernel.io().detect_formats_with_paths(detect_mixed);
        if (detect_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected detect_formats_with_paths failure when an item path is empty\n";
            return 1;
        }
        auto detect_diag = kernel.diagnostics().get(detect_fail.diagnostic_id);
        const auto* detect_ctx = detect_diag.status == axiom::StatusCode::Ok && detect_diag.value.has_value()
                                    ? find_issue(*detect_diag.value, axiom::diag_codes::kIoBatchDetectFormatItemContext)
                                    : nullptr;
        if (detect_ctx == nullptr || detect_ctx->stage != "io.batch_detect_format" ||
            !has_issue_code(*detect_diag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "detect_formats_with_paths batch failure missing batch context or merged root cause\n";
            return 1;
        }
    }

    {
        const std::vector<std::string> two_items {out_path.string(), std::string{}};
        auto vfail = kernel.io().validate_import_paths(two_items);
        if (vfail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_import_paths failure when an item path is empty\n";
            return 1;
        }
        auto vdiag = kernel.diagnostics().get(vfail.diagnostic_id);
        const auto* vctx = vdiag.status == axiom::StatusCode::Ok && vdiag.value.has_value()
                               ? find_issue(*vdiag.value, axiom::diag_codes::kIoBatchPathTransformItemContext)
                               : nullptr;
        if (vctx == nullptr || vctx->stage != "io.batch_validate_import" ||
            !has_issue_code(*vdiag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "validate_import_paths batch failure missing D-0015 context or merged root cause\n";
            return 1;
        }
    }

    {
        const std::vector<std::string> two_export {out_path.string(), std::string{}};
        auto vexp_fail = kernel.io().validate_export_paths(two_export);
        if (vexp_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_export_paths failure when an item path is empty\n";
            return 1;
        }
        auto vexp_diag = kernel.diagnostics().get(vexp_fail.diagnostic_id);
        const auto* vexp_ctx = vexp_diag.status == axiom::StatusCode::Ok && vexp_diag.value.has_value()
                                  ? find_issue(*vexp_diag.value, axiom::diag_codes::kIoBatchPathTransformItemContext)
                                  : nullptr;
        if (vexp_ctx == nullptr || vexp_ctx->stage != "io.batch_validate_export" ||
            !has_issue_code(*vexp_diag.value, axiom::diag_codes::kIoExportFailure)) {
            std::cerr << "validate_export_paths batch failure missing D-0015 context or merged root cause\n";
            return 1;
        }
    }

    {
        const std::vector<std::string> two_items {out_path.string(), std::string{}};
        auto nfail = kernel.io().normalize_paths(two_items);
        if (nfail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected normalize_paths failure when an item path is empty\n";
            return 1;
        }
        auto ndiag = kernel.diagnostics().get(nfail.diagnostic_id);
        const auto* nctx = ndiag.status == axiom::StatusCode::Ok && ndiag.value.has_value()
                               ? find_issue(*ndiag.value, axiom::diag_codes::kIoBatchPathTransformItemContext)
                               : nullptr;
        if (nctx == nullptr || nctx->stage != "io.batch_path_transform" ||
            !has_issue_code(*ndiag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "normalize_paths batch failure missing D-0015 context or merged root cause\n";
            return 1;
        }
    }
    {
        const std::vector<std::string> two_items {out_path.string(), std::string{}};
        auto cfail = kernel.io().change_extensions(two_items, "bak");
        if (cfail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected change_extensions failure when an item path is empty\n";
            return 1;
        }
        auto cdiag = kernel.diagnostics().get(cfail.diagnostic_id);
        const auto* cctx = cdiag.status == axiom::StatusCode::Ok && cdiag.value.has_value()
                               ? find_issue(*cdiag.value, axiom::diag_codes::kIoBatchPathTransformItemContext)
                               : nullptr;
        if (cctx == nullptr || cctx->stage != "io.batch_path_transform" ||
            !has_issue_code(*cdiag.value, axiom::diag_codes::kIoExportFailure)) {
            std::cerr << "change_extensions batch failure missing D-0015 context or merged root cause\n";
            return 1;
        }
    }
    {
        const std::array<std::string, 2> bad_names {"n1", ""};
        auto pfail = kernel.io().compose_paths(std::filesystem::temp_directory_path().string(), bad_names, "txt");
        if (pfail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected compose_paths failure when a name is empty\n";
            return 1;
        }
        auto pdiag = kernel.diagnostics().get(pfail.diagnostic_id);
        const auto* pctx = pdiag.status == axiom::StatusCode::Ok && pdiag.value.has_value()
                               ? find_issue(*pdiag.value, axiom::diag_codes::kIoBatchPathTransformItemContext)
                               : nullptr;
        if (pctx == nullptr || pctx->stage != "io.batch_path_transform" ||
            !has_issue_code(*pdiag.value, axiom::diag_codes::kIoExportFailure)) {
            std::cerr << "compose_paths batch failure missing D-0015 context or merged root cause\n";
            return 1;
        }
    }
    {
        const std::vector<std::string> two_items {out_path.string(), std::string{}};
        auto count_fail = kernel.io().count_by_format(two_items);
        if (count_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected count_by_format failure when an item path is empty\n";
            return 1;
        }
        auto count_diag = kernel.diagnostics().get(count_fail.diagnostic_id);
        const auto* count_ctx = count_diag.status == axiom::StatusCode::Ok && count_diag.value.has_value()
                                    ? find_issue(*count_diag.value, axiom::diag_codes::kIoBatchDetectFormatItemContext)
                                    : nullptr;
        if (count_ctx == nullptr || count_ctx->stage != "io.batch_detect_format" ||
            !has_issue_code(*count_diag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "count_by_format batch failure missing D-0011 context or merged root cause\n";
            return 1;
        }
    }
    {
        const std::vector<std::string> two_items {out_path.string(), std::string{}};
        auto pof_fail = kernel.io().paths_of_format(two_items, "step");
        if (pof_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected paths_of_format failure when an item path is empty\n";
            return 1;
        }
        auto pof_diag = kernel.diagnostics().get(pof_fail.diagnostic_id);
        const auto* pof_ctx = pof_diag.status == axiom::StatusCode::Ok && pof_diag.value.has_value()
                                  ? find_issue(*pof_diag.value, axiom::diag_codes::kIoBatchDetectFormatItemContext)
                                  : nullptr;
        if (pof_ctx == nullptr || pof_ctx->stage != "io.batch_detect_format" ||
            !has_issue_code(*pof_diag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "paths_of_format batch failure missing D-0011 context or merged root cause\n";
            return 1;
        }
    }

    const auto export_dir_bad = tmp / ("axiom_io_export_dir_bad_" + uniq);
    std::filesystem::create_directories(export_dir_bad);
    auto export_dir_bad_ext =
        kernel.io().export_auto_to_directory(batch_bodies, export_dir_bad.string(), "not_a_real_ext", export_options);
    if (export_dir_bad_ext.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected InvalidInput for export_auto_to_directory with unknown extension\n";
        std::filesystem::remove_all(export_dir_bad);
        return 1;
    }
    auto export_dir_bad_diag = kernel.diagnostics().get(export_dir_bad_ext.diagnostic_id);
    if (export_dir_bad_diag.status != axiom::StatusCode::Ok || !export_dir_bad_diag.value.has_value() ||
        !has_issue_code(*export_dir_bad_diag.value, axiom::diag_codes::kIoUnknownFormat)) {
        std::cerr << "export_auto_to_directory unknown ext should report kIoUnknownFormat\n";
        std::filesystem::remove_all(export_dir_bad);
        return 1;
    }
    std::filesystem::remove_all(export_dir_bad);

    const auto ro_root = tmp / ("axiom_io_readonly_" + uniq);
    std::filesystem::create_directories(ro_root);
    try {
        std::filesystem::permissions(ro_root,
                                       std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
    } catch (...) {
    }
    const auto ro_step_path = ro_root / "denied.step";
    auto export_ro = kernel.io().export_step(*body.value, ro_step_path.string(), export_options);
    if (export_ro.status != axiom::StatusCode::Ok) {
        auto ro_diag = kernel.diagnostics().get(export_ro.diagnostic_id);
        if (ro_diag.status != axiom::StatusCode::Ok || !ro_diag.value.has_value() ||
            !has_issue_code(*ro_diag.value, axiom::diag_codes::kIoExportPathNotWritable)) {
            std::cerr << "export to read-only directory should surface kIoExportPathNotWritable when write is denied\n";
            try {
                std::filesystem::permissions(ro_root, std::filesystem::perms::owner_all);
            } catch (...) {
            }
            std::filesystem::remove_all(ro_root);
            return 1;
        }
    }
    try {
        std::filesystem::permissions(ro_root, std::filesystem::perms::owner_all);
    } catch (...) {
    }
    std::filesystem::remove_all(ro_root);

    auto imp_empty = kernel.io().import_auto("", import_options);
    if (imp_empty.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected InvalidInput for empty import path\n";
        return 1;
    }

    std::filesystem::remove(*rename_dst.value);
    std::filesystem::remove(scan_a);
    std::filesystem::remove(scan_b);
    std::filesystem::remove(nested_dir);
    std::filesystem::remove(scan_dir);
    std::filesystem::remove(*line_file.value);

    std::filesystem::remove(p_iges);
    std::filesystem::remove(p_brep);
    std::filesystem::remove(p_obj);
    std::filesystem::remove(p_3mf);
    std::filesystem::remove(p_stl_report);
    std::filesystem::remove(p_sidecar);
    std::filesystem::remove(out_path);
    std::filesystem::remove(out_json_path);
    std::filesystem::remove(dirty_path);
    std::filesystem::remove(*temp_txt.value);
    std::filesystem::remove(*export_default_step_path.value);
    std::filesystem::remove(*export_default_json_path.value);
    std::filesystem::remove(*auto_export_step_path.value);
    std::filesystem::remove(*auto_export_json_path.value);
    std::filesystem::remove(*batch_step_1.value);
    std::filesystem::remove(*batch_step_2.value);
    std::filesystem::remove(*batch_json_1.value);
    std::filesystem::remove(*batch_json_2.value);
    std::filesystem::remove(*text_file.value);
    std::filesystem::remove(*summary_out.value);
    std::filesystem::remove(*body_summary_many_a.value);
    std::filesystem::remove(*body_summary_many_b.value);
    std::filesystem::remove(export_dir / "body_0.step");
    std::filesystem::remove(export_dir / "body_1.step");
    std::filesystem::remove(export_dir / "body_0.axmjson");
    std::filesystem::remove(export_dir / "body_1.axmjson");
    std::filesystem::remove(export_dir);
    return 0;
}
