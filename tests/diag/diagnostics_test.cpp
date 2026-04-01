#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

    auto invalid_box = kernel.primitives().box({0.0, 0.0, 0.0}, -1.0, 10.0, 10.0);
    if (invalid_box.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected invalid input for invalid box\n";
        return 1;
    }

    auto box_diag = kernel.diagnostics().get(invalid_box.diagnostic_id);
    if (box_diag.status != axiom::StatusCode::Ok || !box_diag.value.has_value()) {
        std::cerr << "missing diagnostic for invalid box\n";
        return 1;
    }

    if (!has_issue_code(*box_diag.value, axiom::diag_codes::kCoreParameterOutOfRange)) {
        std::cerr << "unexpected diagnostic code for invalid box\n";
        return 1;
    }

    auto valid_box = kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 10.0, 10.0);
    if (valid_box.status != axiom::StatusCode::Ok || !valid_box.value.has_value()) {
        std::cerr << "failed to create valid box\n";
        return 1;
    }
    if (valid_box.diagnostic_id.value == 0) {
        std::cerr << "expected success diagnostic to be enabled by default\n";
        return 1;
    }

    auto invalid_export = kernel.io().export_step(*valid_box.value, "", axiom::ExportOptions {});
    if (invalid_export.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected invalid input for invalid export path\n";
        return 1;
    }

    auto export_diag = kernel.diagnostics().get(invalid_export.diagnostic_id);
    if (export_diag.status != axiom::StatusCode::Ok || !export_diag.value.has_value()) {
        std::cerr << "missing diagnostic for invalid export\n";
        return 1;
    }

    if (!has_issue_code(*export_diag.value, axiom::diag_codes::kIoExportFailure)) {
        std::cerr << "unexpected diagnostic code for invalid export\n";
        return 1;
    }

    const auto invalid_import_path = std::filesystem::temp_directory_path() / "axiom_invalid_import_validation.step";
    {
        std::ofstream out {invalid_import_path};
        out << "ISO-10303-21;\n";
        out << "HEADER;\n";
        out << "AXIOM_LABEL invalid_import\n";
        out << "AXIOM_BBOX 3 3 3 1 1 1\n";
        out << "ENDSEC;\n";
        out << "END-ISO-10303-21;\n";
    }

    axiom::ImportOptions import_options;
    auto imported = kernel.io().import_step(invalid_import_path.string(), import_options);
    if (imported.status != axiom::StatusCode::Ok || !imported.value.has_value() || imported.warnings.empty()) {
        std::cerr << "expected successful import with validation warnings for invalid bbox metadata\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    auto import_diag = kernel.diagnostics().get(imported.diagnostic_id);
    if (import_diag.status != axiom::StatusCode::Ok || !import_diag.value.has_value() ||
        !has_issue_code(*import_diag.value, axiom::diag_codes::kIoPostImportValidation) ||
        !has_issue_code(*import_diag.value, axiom::diag_codes::kValDegenerateGeometry)) {
        std::cerr << "missing import validation diagnostic issues\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    import_options.auto_repair = true;
    auto repaired_import = kernel.io().import_step(invalid_import_path.string(), import_options);
    if (repaired_import.status != axiom::StatusCode::Ok || !repaired_import.value.has_value()) {
        std::cerr << "expected import auto repair to succeed\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    auto repaired_import_diag = kernel.diagnostics().get(repaired_import.diagnostic_id);
    auto repaired_import_valid = kernel.validate().validate_all(*repaired_import.value, axiom::ValidationMode::Standard);
    if (repaired_import_diag.status != axiom::StatusCode::Ok || !repaired_import_diag.value.has_value() ||
        !has_issue_code(*repaired_import_diag.value, axiom::diag_codes::kIoPostImportValidation) ||
        !has_issue_code(*repaired_import_diag.value, axiom::diag_codes::kValDegenerateGeometry) ||
        !has_issue_code(*repaired_import_diag.value, axiom::diag_codes::kHealFeatureRemovedWarning) ||
        !has_issue_code(*repaired_import_diag.value, axiom::diag_codes::kHealRepairValidated) ||
        repaired_import_valid.status != axiom::StatusCode::Ok) {
        std::cerr << "missing import auto repair diagnostic issues\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }
    const auto* imported_validation_issue = find_issue(*repaired_import_diag.value, axiom::diag_codes::kIoPostImportValidation);
    const auto* imported_repair_issue = find_issue(*repaired_import_diag.value, axiom::diag_codes::kHealRepairValidated);
    if (imported_validation_issue == nullptr || imported_repair_issue == nullptr ||
        imported_validation_issue->related_entities.size() != 1 ||
        imported_validation_issue->related_entities.front() == 0 ||
        imported_repair_issue->related_entities.size() != 2 ||
        imported_repair_issue->related_entities.front() == imported_repair_issue->related_entities.back()) {
        std::cerr << "import auto repair related entities are unexpected\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    axiom::KernelConfig silent_config;
    silent_config.enable_diagnostics = false;
    axiom::Kernel silent_kernel {silent_config};

    auto silent_valid_box = silent_kernel.primitives().box({0.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
    if (silent_valid_box.status != axiom::StatusCode::Ok || !silent_valid_box.value.has_value() ||
        silent_valid_box.diagnostic_id.value != 0) {
        std::cerr << "expected success diagnostics to be suppressible via kernel config\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    auto silent_invalid_box = silent_kernel.primitives().box({0.0, 0.0, 0.0}, -2.0, 2.0, 2.0);
    if (silent_invalid_box.status != axiom::StatusCode::InvalidInput || silent_invalid_box.diagnostic_id.value == 0) {
        std::cerr << "expected failure diagnostics to remain available when diagnostics are globally disabled\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    auto silent_invalid_diag = silent_kernel.diagnostics().get(silent_invalid_box.diagnostic_id);
    if (silent_invalid_diag.status != axiom::StatusCode::Ok || !silent_invalid_diag.value.has_value() ||
        !has_issue_code(*silent_invalid_diag.value, axiom::diag_codes::kCoreParameterOutOfRange)) {
        std::cerr << "missing failure diagnostic under global diagnostics suppression\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    axiom::Issue extra_issue;
    extra_issue.code = std::string(axiom::diag_codes::kHealFeatureRemovedWarning);
    extra_issue.severity = axiom::IssueSeverity::Warning;
    extra_issue.message = "manual issue \"quoted\"\nnext line";
    extra_issue.related_entities = {42, 84};
    auto append_issue = kernel.diagnostics().append_issue(valid_box.diagnostic_id, extra_issue);
    if (append_issue.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to append issue to diagnostic\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    auto appended_diag = kernel.diagnostics().get(valid_box.diagnostic_id);
    if (appended_diag.status != axiom::StatusCode::Ok || !appended_diag.value.has_value() ||
        !has_issue_code(*appended_diag.value, axiom::diag_codes::kHealFeatureRemovedWarning)) {
        std::cerr << "missing appended diagnostic issue\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    const auto json_path = std::filesystem::temp_directory_path() / "axiom_diag_export_test.json";
    auto exported = kernel.diagnostics().export_report_json(valid_box.diagnostic_id, json_path.string());
    if (exported.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to export diagnostic json\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }
    std::ifstream json_in {json_path};
    std::string json((std::istreambuf_iterator<char>(json_in)), std::istreambuf_iterator<char>());
    if (json.find("\"related_entities\":[42,84]") == std::string::npos ||
        json.find("\\\"quoted\\\"") == std::string::npos ||
        json.find("\\nnext line") == std::string::npos) {
        std::cerr << "exported diagnostic json is missing expected fields or escaping\n";
        std::filesystem::remove(invalid_import_path);
        std::filesystem::remove(json_path);
        return 1;
    }
    std::filesystem::remove(json_path);

    auto diag_count = kernel.diagnostics().count();
    auto warning_count = kernel.diagnostics().count_by_severity(axiom::IssueSeverity::Warning);
    auto diag_stats = kernel.diagnostics().stats();
    if (diag_count.status != axiom::StatusCode::Ok || warning_count.status != axiom::StatusCode::Ok ||
        diag_stats.status != axiom::StatusCode::Ok || !diag_count.value.has_value() ||
        !warning_count.value.has_value() || !diag_stats.value.has_value() ||
        *diag_count.value == 0 || *warning_count.value == 0 ||
        diag_stats.value->total < *diag_count.value || diag_stats.value->warning < *warning_count.value) {
        std::cerr << "unexpected diagnostic statistics\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    auto find_heal_warning = kernel.diagnostics().find_by_issue_code(axiom::diag_codes::kHealFeatureRemovedWarning, 5);
    if (find_heal_warning.status != axiom::StatusCode::Ok || !find_heal_warning.value.has_value() ||
        find_heal_warning.value->empty()) {
        std::cerr << "expected to find diagnostics by issue code\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }
    auto find_related = kernel.diagnostics().find_by_related_entity(42, 5);
    if (find_related.status != axiom::StatusCode::Ok || !find_related.value.has_value() ||
        find_related.value->empty()) {
        std::cerr << "expected to find diagnostics by related entity\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    auto recent_snapshot = kernel.diagnostics().snapshot_recent(3);
    if (recent_snapshot.status != axiom::StatusCode::Ok || !recent_snapshot.value.has_value() ||
        recent_snapshot.value->empty() || recent_snapshot.value->size() > 3) {
        std::cerr << "unexpected diagnostic snapshot result\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    auto exists_valid = kernel.diagnostics().exists(valid_box.diagnostic_id);
    auto issue_count_valid = kernel.diagnostics().issue_count(valid_box.diagnostic_id);
    auto warning_count_valid = kernel.diagnostics().warning_count(valid_box.diagnostic_id);
    auto error_count_valid = kernel.diagnostics().error_count(valid_box.diagnostic_id);
    auto fatal_count_valid = kernel.diagnostics().fatal_count(valid_box.diagnostic_id);
    auto has_code_valid = kernel.diagnostics().has_issue_code(valid_box.diagnostic_id, axiom::diag_codes::kHealFeatureRemovedWarning);
    auto has_entity_valid = kernel.diagnostics().has_related_entity(valid_box.diagnostic_id, 42);
    auto issues_all = kernel.diagnostics().issues_of(valid_box.diagnostic_id);
    auto issues_warn = kernel.diagnostics().issues_of_severity(valid_box.diagnostic_id, axiom::IssueSeverity::Warning);
    auto issues_code = kernel.diagnostics().issues_of_code(valid_box.diagnostic_id, axiom::diag_codes::kHealFeatureRemovedWarning);
    auto codes_unique = kernel.diagnostics().unique_issue_codes(valid_box.diagnostic_id);
    auto latest_id = kernel.diagnostics().latest_id();
    auto earliest_id = kernel.diagnostics().earliest_id();
    auto find_min_issue = kernel.diagnostics().find_with_min_issue_count(1, 10);
    auto find_with_warn = kernel.diagnostics().find_with_severity(axiom::IssueSeverity::Warning, 10);
    auto find_summary = kernel.diagnostics().find_summary_contains("盒体", 10);
    auto summaries_recent = kernel.diagnostics().summaries_recent(5);
    auto summary_of_valid = kernel.diagnostics().summary_of(valid_box.diagnostic_id);
    if (exists_valid.status != axiom::StatusCode::Ok || !exists_valid.value.has_value() || !*exists_valid.value ||
        issue_count_valid.status != axiom::StatusCode::Ok || !issue_count_valid.value.has_value() || *issue_count_valid.value == 0 ||
        warning_count_valid.status != axiom::StatusCode::Ok || !warning_count_valid.value.has_value() || *warning_count_valid.value == 0 ||
        error_count_valid.status != axiom::StatusCode::Ok || !error_count_valid.value.has_value() ||
        fatal_count_valid.status != axiom::StatusCode::Ok || !fatal_count_valid.value.has_value() ||
        has_code_valid.status != axiom::StatusCode::Ok || !has_code_valid.value.has_value() || !*has_code_valid.value ||
        has_entity_valid.status != axiom::StatusCode::Ok || !has_entity_valid.value.has_value() || !*has_entity_valid.value ||
        issues_all.status != axiom::StatusCode::Ok || !issues_all.value.has_value() || issues_all.value->empty() ||
        issues_warn.status != axiom::StatusCode::Ok || !issues_warn.value.has_value() || issues_warn.value->empty() ||
        issues_code.status != axiom::StatusCode::Ok || !issues_code.value.has_value() || issues_code.value->empty() ||
        codes_unique.status != axiom::StatusCode::Ok || !codes_unique.value.has_value() || codes_unique.value->empty() ||
        latest_id.status != axiom::StatusCode::Ok || !latest_id.value.has_value() ||
        earliest_id.status != axiom::StatusCode::Ok || !earliest_id.value.has_value() ||
        find_min_issue.status != axiom::StatusCode::Ok || !find_min_issue.value.has_value() || find_min_issue.value->empty() ||
        find_with_warn.status != axiom::StatusCode::Ok || !find_with_warn.value.has_value() || find_with_warn.value->empty() ||
        find_summary.status != axiom::StatusCode::Ok || !find_summary.value.has_value() || find_summary.value->empty() ||
        summaries_recent.status != axiom::StatusCode::Ok || !summaries_recent.value.has_value() || summaries_recent.value->empty() ||
        summary_of_valid.status != axiom::StatusCode::Ok || !summary_of_valid.value.has_value() || summary_of_valid.value->empty()) {
        std::cerr << "unexpected extended diagnostic query behavior\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    auto set_summary = kernel.diagnostics().set_summary(valid_box.diagnostic_id, "custom-summary");
    auto append_summary = kernel.diagnostics().append_summary_suffix(valid_box.diagnostic_id, "-suffix");
    auto remove_code = kernel.diagnostics().remove_issue_code(valid_box.diagnostic_id, axiom::diag_codes::kHealFeatureRemovedWarning);
    auto remove_warn = kernel.diagnostics().remove_issues_of_severity(valid_box.diagnostic_id, axiom::IssueSeverity::Warning);
    auto total_issues = kernel.diagnostics().total_issue_count();
    auto total_warn = kernel.diagnostics().total_warning_count();
    auto total_err = kernel.diagnostics().total_error_count();
    auto total_fatal = kernel.diagnostics().total_fatal_count();
    auto all_ids = kernel.diagnostics().all_ids();
    if (set_summary.status != axiom::StatusCode::Ok || append_summary.status != axiom::StatusCode::Ok ||
        remove_code.status != axiom::StatusCode::Ok || remove_warn.status != axiom::StatusCode::Ok ||
        total_issues.status != axiom::StatusCode::Ok || !total_issues.value.has_value() ||
        total_warn.status != axiom::StatusCode::Ok || !total_warn.value.has_value() ||
        total_err.status != axiom::StatusCode::Ok || !total_err.value.has_value() ||
        total_fatal.status != axiom::StatusCode::Ok || !total_fatal.value.has_value() ||
        all_ids.status != axiom::StatusCode::Ok || !all_ids.value.has_value() || all_ids.value->empty()) {
        std::cerr << "unexpected extended diagnostic mutation behavior\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    const auto exported_diag_path = std::filesystem::temp_directory_path() / "axiom_diagnostic_export.txt";
    auto export_report = kernel.diagnostics().export_report(valid_box.diagnostic_id, exported_diag_path.string());
    const auto exported_diag_json_path = std::filesystem::temp_directory_path() / "axiom_diagnostic_export.json";
    auto export_report_json = kernel.diagnostics().export_report_json(valid_box.diagnostic_id, exported_diag_json_path.string());
    if (export_report.status != axiom::StatusCode::Ok || export_report_json.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to export diagnostic report\n";
        std::filesystem::remove(invalid_import_path);
        std::filesystem::remove(exported_diag_path);
        std::filesystem::remove(exported_diag_json_path);
        return 1;
    }
    const auto exported_diag_many_path = std::filesystem::temp_directory_path() / "axiom_diagnostic_export_many.txt";
    const auto exported_diag_many_json_path = std::filesystem::temp_directory_path() / "axiom_diagnostic_export_many.json";
    std::array<axiom::DiagnosticId, 2> export_ids {valid_box.diagnostic_id, invalid_box.diagnostic_id};
    auto export_many_txt = kernel.diagnostics().export_reports_txt(export_ids, exported_diag_many_path.string());
    auto export_many_json = kernel.diagnostics().export_reports_json(export_ids, exported_diag_many_json_path.string());
    auto append_many = kernel.diagnostics().append_issue_many(export_ids, extra_issue);
    auto ids_by_sev = kernel.diagnostics().report_ids_by_severity(axiom::IssueSeverity::Warning, 10);
    auto ids_by_code = kernel.diagnostics().report_ids_by_code(axiom::diag_codes::kHealFeatureRemovedWarning, 10);
    auto ids_by_entity = kernel.diagnostics().report_ids_by_entity(42, 10);
    auto code_hist = kernel.diagnostics().issue_code_histogram();
    auto sev_hist = kernel.diagnostics().severity_histogram();
    auto entity_hist = kernel.diagnostics().entity_histogram();
    auto top_codes = kernel.diagnostics().top_issue_codes(5);
    auto top_entities = kernel.diagnostics().top_entities(5);
    std::array<axiom::BodyId, 1> body_ids_for_diag {*valid_box.value};
    std::array<axiom::FaceId, 1> face_ids_for_diag {axiom::FaceId{42}};
    std::array<axiom::ShellId, 1> shell_ids_for_diag {axiom::ShellId{42}};
    std::array<axiom::EdgeId, 1> edge_ids_for_diag {axiom::EdgeId{42}};
    std::array<axiom::VertexId, 1> vertex_ids_for_diag {axiom::VertexId{42}};
    auto by_bodies = kernel.diagnostics().diagnostics_for_bodies(body_ids_for_diag, 5);
    auto by_faces = kernel.diagnostics().diagnostics_for_faces(face_ids_for_diag, 5);
    auto by_shells = kernel.diagnostics().diagnostics_for_shells(shell_ids_for_diag, 5);
    auto by_edges = kernel.diagnostics().diagnostics_for_edges(edge_ids_for_diag, 5);
    auto by_vertices = kernel.diagnostics().diagnostics_for_vertices(vertex_ids_for_diag, 5);
    const auto grouped_code_txt = std::filesystem::temp_directory_path() / "axiom_diag_group_code.txt";
    const auto grouped_entity_txt = std::filesystem::temp_directory_path() / "axiom_diag_group_entity.txt";
    const auto grouped_sev_txt = std::filesystem::temp_directory_path() / "axiom_diag_group_sev.txt";
    const auto grouped_code_json = std::filesystem::temp_directory_path() / "axiom_diag_group_code.json";
    const auto grouped_entity_json = std::filesystem::temp_directory_path() / "axiom_diag_group_entity.json";
    const auto grouped_sev_json = std::filesystem::temp_directory_path() / "axiom_diag_group_sev.json";
    auto export_group_code_txt = kernel.diagnostics().export_grouped_by_code_txt(grouped_code_txt.string());
    auto export_group_entity_txt = kernel.diagnostics().export_grouped_by_entity_txt(grouped_entity_txt.string());
    auto export_group_sev_txt = kernel.diagnostics().export_grouped_by_severity_txt(grouped_sev_txt.string());
    auto export_group_code_json = kernel.diagnostics().export_grouped_by_code_json(grouped_code_json.string());
    auto export_group_entity_json = kernel.diagnostics().export_grouped_by_entity_json(grouped_entity_json.string());
    auto export_group_sev_json = kernel.diagnostics().export_grouped_by_severity_json(grouped_sev_json.string());
    auto summaries_by_ids = kernel.diagnostics().summaries_by_ids(export_ids);
    auto merged_report = kernel.diagnostics().merge_reports(export_ids, "merged");
    auto copied_id = kernel.diagnostics().copy_report(valid_box.diagnostic_id);
    std::array<axiom::Issue, 1> create_issues {extra_issue};
    auto created_id = kernel.diagnostics().create_report("created", create_issues);
    auto asc_ids = kernel.diagnostics().ids_sorted_asc();
    auto desc_ids = kernel.diagnostics().ids_sorted_desc();
    auto recent_code = kernel.diagnostics().recent_ids_with_issue_code(axiom::diag_codes::kHealFeatureRemovedWarning, 5);
    auto recent_entity = kernel.diagnostics().recent_ids_with_entity(42, 5);
    auto stats_of_ids = kernel.diagnostics().stats_of_ids(export_ids);
    std::array<axiom::DiagnosticId, 1> remove_ids {axiom::DiagnosticId{0}};
    if (created_id.status == axiom::StatusCode::Ok && created_id.value.has_value()) remove_ids[0] = *created_id.value;
    auto removed_count = kernel.diagnostics().remove_reports(remove_ids);
    auto keep_only = kernel.diagnostics().keep_only(export_ids);
    if (export_many_txt.status != axiom::StatusCode::Ok || export_many_json.status != axiom::StatusCode::Ok ||
        append_many.status != axiom::StatusCode::Ok ||
        ids_by_sev.status != axiom::StatusCode::Ok || !ids_by_sev.value.has_value() ||
        ids_by_code.status != axiom::StatusCode::Ok || !ids_by_code.value.has_value() ||
        ids_by_entity.status != axiom::StatusCode::Ok || !ids_by_entity.value.has_value() ||
        code_hist.status != axiom::StatusCode::Ok || !code_hist.value.has_value() ||
        sev_hist.status != axiom::StatusCode::Ok || !sev_hist.value.has_value() ||
        entity_hist.status != axiom::StatusCode::Ok || !entity_hist.value.has_value() ||
        top_codes.status != axiom::StatusCode::Ok || !top_codes.value.has_value() ||
        top_entities.status != axiom::StatusCode::Ok || !top_entities.value.has_value() ||
        by_bodies.status != axiom::StatusCode::Ok || !by_bodies.value.has_value() ||
        by_faces.status != axiom::StatusCode::Ok || !by_faces.value.has_value() ||
        by_shells.status != axiom::StatusCode::Ok || !by_shells.value.has_value() ||
        by_edges.status != axiom::StatusCode::Ok || !by_edges.value.has_value() ||
        by_vertices.status != axiom::StatusCode::Ok || !by_vertices.value.has_value() ||
        export_group_code_txt.status != axiom::StatusCode::Ok ||
        export_group_entity_txt.status != axiom::StatusCode::Ok ||
        export_group_sev_txt.status != axiom::StatusCode::Ok ||
        export_group_code_json.status != axiom::StatusCode::Ok ||
        export_group_entity_json.status != axiom::StatusCode::Ok ||
        export_group_sev_json.status != axiom::StatusCode::Ok ||
        summaries_by_ids.status != axiom::StatusCode::Ok || !summaries_by_ids.value.has_value() ||
        merged_report.status != axiom::StatusCode::Ok || !merged_report.value.has_value() ||
        copied_id.status != axiom::StatusCode::Ok || !copied_id.value.has_value() ||
        created_id.status != axiom::StatusCode::Ok || !created_id.value.has_value() ||
        asc_ids.status != axiom::StatusCode::Ok || !asc_ids.value.has_value() ||
        desc_ids.status != axiom::StatusCode::Ok || !desc_ids.value.has_value() ||
        recent_code.status != axiom::StatusCode::Ok || !recent_code.value.has_value() ||
        recent_entity.status != axiom::StatusCode::Ok || !recent_entity.value.has_value() ||
        stats_of_ids.status != axiom::StatusCode::Ok || !stats_of_ids.value.has_value() ||
        removed_count.status != axiom::StatusCode::Ok || !removed_count.value.has_value() ||
        keep_only.status != axiom::StatusCode::Ok || !keep_only.value.has_value()) {
        std::cerr << "unexpected extended diagnostic batch behavior\n";
        std::filesystem::remove(invalid_import_path);
        return 1;
    }

    std::ifstream exported_diag_in {exported_diag_path};
    std::string exported_diag_text((std::istreambuf_iterator<char>(exported_diag_in)),
                                   std::istreambuf_iterator<char>());
    if (exported_diag_text.find("DiagnosticId: " + std::to_string(valid_box.diagnostic_id.value)) == std::string::npos ||
        exported_diag_text.find("Summary: custom-summary-suffix") == std::string::npos) {
        std::cerr << "exported diagnostic report content is unexpected\n";
        std::filesystem::remove(invalid_import_path);
        std::filesystem::remove(exported_diag_path);
        return 1;
    }
    std::ifstream exported_diag_json_in {exported_diag_json_path};
    std::string exported_diag_json_text((std::istreambuf_iterator<char>(exported_diag_json_in)),
                                        std::istreambuf_iterator<char>());
    if (exported_diag_json_text.find("\"id\":" + std::to_string(valid_box.diagnostic_id.value)) == std::string::npos ||
        exported_diag_json_text.find("\"summary\":\"custom-summary-suffix\"") == std::string::npos) {
        std::cerr << "exported diagnostic json content is unexpected\n";
        std::filesystem::remove(invalid_import_path);
        std::filesystem::remove(exported_diag_path);
        std::filesystem::remove(exported_diag_json_path);
        return 1;
    }

    auto missing_diag = kernel.diagnostics().get(axiom::DiagnosticId {999999});
    if (missing_diag.status != axiom::StatusCode::InvalidInput || missing_diag.diagnostic_id.value == 0) {
        std::cerr << "missing diagnostic lookup should return structured failure\n";
        std::filesystem::remove(invalid_import_path);
        std::filesystem::remove(exported_diag_path);
        std::filesystem::remove(exported_diag_json_path);
        return 1;
    }

    auto missing_diag_report = kernel.diagnostics().get(missing_diag.diagnostic_id);
    if (missing_diag_report.status != axiom::StatusCode::Ok || !missing_diag_report.value.has_value() ||
        !has_issue_code(*missing_diag_report.value, axiom::diag_codes::kCoreInvalidHandle)) {
        std::cerr << "missing diagnostic lookup did not create expected error diagnostic\n";
        std::filesystem::remove(invalid_import_path);
        std::filesystem::remove(exported_diag_path);
        std::filesystem::remove(exported_diag_json_path);
        return 1;
    }

    auto append_missing = kernel.diagnostics().append_issue(axiom::DiagnosticId {999999}, extra_issue);
    if (append_missing.status != axiom::StatusCode::InvalidInput || append_missing.diagnostic_id.value == 0) {
        std::cerr << "append issue on missing diagnostic should fail with structured diagnostic\n";
        std::filesystem::remove(invalid_import_path);
        std::filesystem::remove(exported_diag_path);
        std::filesystem::remove(exported_diag_json_path);
        return 1;
    }

    auto export_empty_path = kernel.diagnostics().export_report(valid_box.diagnostic_id, "");
    if (export_empty_path.status != axiom::StatusCode::InvalidInput || export_empty_path.diagnostic_id.value == 0) {
        std::cerr << "export report with empty path should fail with structured diagnostic\n";
        std::filesystem::remove(invalid_import_path);
        std::filesystem::remove(exported_diag_path);
        std::filesystem::remove(exported_diag_json_path);
        return 1;
    }

    auto export_missing = kernel.diagnostics().export_report(axiom::DiagnosticId {999999}, exported_diag_path.string());
    if (export_missing.status != axiom::StatusCode::InvalidInput || export_missing.diagnostic_id.value == 0) {
        std::cerr << "export missing diagnostic should fail with structured diagnostic\n";
        std::filesystem::remove(invalid_import_path);
        std::filesystem::remove(exported_diag_path);
        std::filesystem::remove(exported_diag_json_path);
        return 1;
    }

    auto pruned_count = kernel.diagnostics().prune_to_max(2);
    auto count_after_prune = kernel.diagnostics().count();
    if (pruned_count.status != axiom::StatusCode::Ok || count_after_prune.status != axiom::StatusCode::Ok ||
        !pruned_count.value.has_value() || !count_after_prune.value.has_value() ||
        *count_after_prune.value > 4) {
        std::cerr << "unexpected diagnostic prune behavior\n";
        std::filesystem::remove(invalid_import_path);
        std::filesystem::remove(exported_diag_path);
        std::filesystem::remove(exported_diag_json_path);
        return 1;
    }

    auto clear_result = kernel.diagnostics().clear_all();
    auto count_after_clear = kernel.diagnostics().count();
    if (clear_result.status != axiom::StatusCode::Ok || count_after_clear.status != axiom::StatusCode::Ok ||
        !count_after_clear.value.has_value() || *count_after_clear.value > 2) {
        std::cerr << "unexpected diagnostic clear behavior\n";
        std::filesystem::remove(invalid_import_path);
        std::filesystem::remove(exported_diag_path);
        return 1;
    }

    std::filesystem::remove(invalid_import_path);
    std::filesystem::remove(exported_diag_path);
    std::filesystem::remove(exported_diag_json_path);
    std::filesystem::remove(exported_diag_many_path);
    std::filesystem::remove(exported_diag_many_json_path);
    std::filesystem::remove(grouped_code_txt);
    std::filesystem::remove(grouped_entity_txt);
    std::filesystem::remove(grouped_sev_txt);
    std::filesystem::remove(grouped_code_json);
    std::filesystem::remove(grouped_entity_json);
    std::filesystem::remove(grouped_sev_json);

    return 0;
}
