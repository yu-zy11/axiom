#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "axiom/core/types.h"
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

bool has_warning_code(const std::vector<axiom::Warning>& warnings, std::string_view code) {
    for (const auto& warning : warnings) {
        if (warning.code == code) {
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

int count_stage(const axiom::DiagnosticReport& report, std::string_view code, std::string_view stage) {
    int n = 0;
    for (const auto& issue : report.issues) {
        if (issue.code == code && issue.stage == stage) {
            ++n;
        }
    }
    return n;
}

}  // namespace

int main() {
    axiom::Kernel kernel;

    auto box_a = kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 10.0, 10.0);
    if (box_a.status != axiom::StatusCode::Ok || !box_a.value.has_value()) {
        std::cerr << "failed to create box for heal test\n";
        return 1;
    }

    auto remove_small_edges = kernel.repair().remove_small_edges(*box_a.value, 0.5, axiom::RepairMode::Safe);
    auto remove_small_faces_adaptive =
        kernel.repair().remove_small_faces(*box_a.value, 1e-9, axiom::RepairMode::Aggressive);
    auto merge_coplanar_adaptive =
        kernel.repair().merge_near_coplanar_faces(*box_a.value, 1e-9, axiom::RepairMode::Safe);
    auto auto_repair = kernel.repair().auto_repair(*box_a.value, axiom::RepairMode::Aggressive);
    if (remove_small_edges.status != axiom::StatusCode::Ok || !remove_small_edges.value.has_value() ||
        remove_small_faces_adaptive.status != axiom::StatusCode::Ok || !remove_small_faces_adaptive.value.has_value() ||
        merge_coplanar_adaptive.status != axiom::StatusCode::Ok || !merge_coplanar_adaptive.value.has_value() ||
        auto_repair.status != axiom::StatusCode::Ok || !auto_repair.value.has_value()) {
        std::cerr << "repair operations failed\n";
        return 1;
    }

    if (remove_small_edges.value->output.value == box_a.value->value ||
        auto_repair.value->output.value == box_a.value->value) {
        std::cerr << "expected repair operations to produce derived bodies\n";
        return 1;
    }
    if (!has_warning_code(merge_coplanar_adaptive.value->warnings, axiom::diag_codes::kHealFeatureRemovedWarning)) {
        std::cerr << "expected adaptive threshold warning for near coplanar merge\n";
        return 1;
    }
    auto adaptive_bbox = kernel.representation().bbox_of_body(remove_small_faces_adaptive.value->output);
    if (adaptive_bbox.status != axiom::StatusCode::Ok || !adaptive_bbox.value.has_value() ||
        adaptive_bbox.value->max.y >= 10.0) {
        std::cerr << "adaptive small-face threshold should produce measurable bbox shrink in aggressive mode\n";
        return 1;
    }

    auto repair_diag = kernel.diagnostics().get(auto_repair.value->diagnostic_id);
    if (repair_diag.status != axiom::StatusCode::Ok || !repair_diag.value.has_value() ||
        !has_issue_code(*repair_diag.value, axiom::diag_codes::kHealFeatureRemovedWarning) ||
        !has_issue_code(*repair_diag.value, axiom::diag_codes::kHealRepairValidated) ||
        !has_issue_code(*repair_diag.value, axiom::diag_codes::kHealRepairPipelineTrace) ||
        !has_issue_code(*repair_diag.value, axiom::diag_codes::kHealRepairReplaySummary)) {
        std::cerr << "expected repair warning diagnostic\n";
        return 1;
    }
    const auto* trace_issue = find_issue(*repair_diag.value, axiom::diag_codes::kHealRepairPipelineTrace);
    const auto* replay_issue = find_issue(*repair_diag.value, axiom::diag_codes::kHealRepairReplaySummary);
    if (trace_issue == nullptr || trace_issue->message != "auto_repair" || trace_issue->stage != "heal.repair_pipeline" ||
        trace_issue->related_entities.size() != 2 ||
        trace_issue->related_entities.front() != box_a.value->value ||
        trace_issue->related_entities.back() != auto_repair.value->output.value ||
        replay_issue == nullptr || replay_issue->message != "auto_repair" || replay_issue->stage != "heal.repair_replay") {
        std::cerr << "repair pipeline trace issue is unexpected\n";
        return 1;
    }
    const axiom::Issue* feature_removed_issue = nullptr;
    for (const auto& iss : repair_diag.value->issues) {
        if (iss.code == axiom::diag_codes::kHealFeatureRemovedWarning && iss.stage == "heal.auto_repair") {
            feature_removed_issue = &iss;
            break;
        }
    }
    const auto* validated_issue = find_issue(*repair_diag.value, axiom::diag_codes::kHealRepairValidated);
    if (feature_removed_issue == nullptr || validated_issue == nullptr ||
        feature_removed_issue->related_entities.size() != 2 ||
        validated_issue->related_entities.size() != 2 ||
        feature_removed_issue->related_entities.front() != box_a.value->value ||
        validated_issue->related_entities.front() != box_a.value->value ||
        feature_removed_issue->related_entities.back() != auto_repair.value->output.value ||
        validated_issue->related_entities.back() != auto_repair.value->output.value ||
        feature_removed_issue->stage != "heal.auto_repair" ||
        validated_issue->stage != "heal.auto_repair.post_validate") {
        std::cerr << "auto repair diagnostic related entities are unexpected\n";
        return 1;
    }

    auto repair_bodies = kernel.topology().query().source_bodies_of_body(auto_repair.value->output);
    auto repair_owned_shells = kernel.topology().query().shells_of_body(auto_repair.value->output);
    auto repair_owned_faces = repair_owned_shells.status == axiom::StatusCode::Ok && repair_owned_shells.value.has_value() &&
                                      repair_owned_shells.value->size() == 1
                                  ? kernel.topology().query().faces_of_shell(repair_owned_shells.value->front())
                                  : axiom::Result<std::vector<axiom::FaceId>> {};
    if (repair_bodies.status != axiom::StatusCode::Ok || !repair_bodies.value.has_value() ||
        repair_owned_shells.status != axiom::StatusCode::Ok || !repair_owned_shells.value.has_value() ||
        repair_owned_faces.status != axiom::StatusCode::Ok || !repair_owned_faces.value.has_value() ||
        repair_bodies.value->size() != 1 || repair_bodies.value->front().value != box_a.value->value ||
        repair_owned_shells.value->size() != 1 ||
        repair_owned_faces.value->size() != 6) {
        std::cerr << "repair provenance is unexpected\n";
        return 1;
    }

    auto repaired_valid = kernel.validate().validate_all(auto_repair.value->output, axiom::ValidationMode::Standard);
    auto repaired_strict_valid = kernel.validate().validate_topology(auto_repair.value->output, axiom::ValidationMode::Strict);
    if (repaired_valid.status != axiom::StatusCode::Ok || repaired_strict_valid.status != axiom::StatusCode::Ok) {
        std::cerr << "auto repaired body should validate\n";
        return 1;
    }

    std::array<axiom::BodyId, 2> body_pair {*box_a.value, auto_repair.value->output};
    auto validate_geom_strict_box = kernel.validate().validate_geometry(*box_a.value, axiom::ValidationMode::Strict);
    if (validate_geom_strict_box.status != axiom::StatusCode::Ok) {
        std::cerr << "primitive box should pass Strict geometry (bbox + surface/curve 参数域)\n";
        return 1;
    }
    auto validate_many = kernel.validate().validate_all_many(body_pair, axiom::ValidationMode::Standard);
    auto validate_geom_many = kernel.validate().validate_geometry_many(body_pair, axiom::ValidationMode::Standard);
    auto validate_tol_many_std = kernel.validate().validate_tolerance_many(body_pair, axiom::ValidationMode::Standard);
    auto validate_topo_many = kernel.validate().validate_topology_many(body_pair, axiom::ValidationMode::Standard);
    auto validate_manifold_many = kernel.validate().validate_manifold_many(body_pair, axiom::ValidationMode::Standard);
    auto validate_si_many = kernel.validate().validate_self_intersection_many(body_pair, axiom::ValidationMode::Strict);
    auto bbox_many = kernel.validate().validate_bbox_many(body_pair);
    auto invalid_count = kernel.validate().count_invalid_in(body_pair, axiom::ValidationMode::Standard);
    auto valid_filtered = kernel.validate().filter_valid_bodies(body_pair, axiom::ValidationMode::Standard);
    auto invalid_filtered = kernel.validate().filter_invalid_bodies(body_pair, axiom::ValidationMode::Standard);
    auto geom_valid = kernel.validate().is_geometry_valid(*box_a.value, axiom::ValidationMode::Standard);
    auto topo_valid = kernel.validate().is_topology_valid(*box_a.value, axiom::ValidationMode::Standard);
    auto manifold_valid = kernel.validate().is_manifold_valid(*box_a.value, axiom::ValidationMode::Standard);
    auto all_valid = kernel.validate().is_valid(*box_a.value, axiom::ValidationMode::Standard);
    if (validate_many.status != axiom::StatusCode::Ok || validate_geom_many.status != axiom::StatusCode::Ok ||
        validate_tol_many_std.status != axiom::StatusCode::Ok || validate_topo_many.status != axiom::StatusCode::Ok ||
        validate_manifold_many.status != axiom::StatusCode::Ok ||
        validate_si_many.status != axiom::StatusCode::Ok || bbox_many.status != axiom::StatusCode::Ok ||
        invalid_count.status != axiom::StatusCode::Ok || !invalid_count.value.has_value() || *invalid_count.value != 0 ||
        valid_filtered.status != axiom::StatusCode::Ok || !valid_filtered.value.has_value() || valid_filtered.value->size() != 2 ||
        invalid_filtered.status != axiom::StatusCode::Ok || !invalid_filtered.value.has_value() || !invalid_filtered.value->empty() ||
        geom_valid.status != axiom::StatusCode::Ok || !geom_valid.value.has_value() || !*geom_valid.value ||
        topo_valid.status != axiom::StatusCode::Ok || !topo_valid.value.has_value() || !*topo_valid.value ||
        manifold_valid.status != axiom::StatusCode::Ok || !manifold_valid.value.has_value() || !*manifold_valid.value ||
        all_valid.status != axiom::StatusCode::Ok || !all_valid.value.has_value() || !*all_valid.value) {
        std::cerr << "extended validation service behavior is unexpected\n";
        return 1;
    }
    auto first_invalid = kernel.validate().first_invalid_in(body_pair, axiom::ValidationMode::Standard);
    if (first_invalid.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "first_invalid_in should fail when no invalid body exists\n";
        return 1;
    }

    auto linear_est = kernel.repair().estimate_adaptive_linear_threshold(*box_a.value, 1e-9, axiom::RepairMode::Safe);
    auto angle_est = kernel.repair().estimate_adaptive_angle_threshold(*box_a.value, 1e-9, axiom::RepairMode::Safe);
    auto remove_small_faces_default = kernel.repair().remove_small_faces_default(*box_a.value, 0.01);
    auto remove_small_edges_default = kernel.repair().remove_small_edges_default(*box_a.value, 0.01);
    auto merge_default = kernel.repair().merge_near_coplanar_faces_default(*box_a.value, 0.01);
    auto auto_default = kernel.repair().auto_repair_default(*box_a.value);
    if (linear_est.status != axiom::StatusCode::Ok || !linear_est.value.has_value() || *linear_est.value <= 0.0 ||
        angle_est.status != axiom::StatusCode::Ok || !angle_est.value.has_value() || *angle_est.value <= 0.0 ||
        remove_small_faces_default.status != axiom::StatusCode::Ok || !remove_small_faces_default.value.has_value() ||
        remove_small_edges_default.status != axiom::StatusCode::Ok || !remove_small_edges_default.value.has_value() ||
        merge_default.status != axiom::StatusCode::Ok || !merge_default.value.has_value() ||
        auto_default.status != axiom::StatusCode::Ok || !auto_default.value.has_value()) {
        std::cerr << "extended repair default API behavior is unexpected\n";
        return 1;
    }
    auto diag_rsf = kernel.diagnostics().get(remove_small_faces_default.value->diagnostic_id);
    auto diag_rse = kernel.diagnostics().get(remove_small_edges_default.value->diagnostic_id);
    auto diag_mrg = kernel.diagnostics().get(merge_default.value->diagnostic_id);
    if (diag_rsf.status != axiom::StatusCode::Ok || !diag_rsf.value.has_value() ||
        diag_rse.status != axiom::StatusCode::Ok || !diag_rse.value.has_value() ||
        diag_mrg.status != axiom::StatusCode::Ok || !diag_mrg.value.has_value()) {
        std::cerr << "repair diagnostics missing for stage regression\n";
        return 1;
    }
    if (count_stage(*diag_rsf.value, axiom::diag_codes::kHealFeatureRemovedWarning, "heal.remove_small_faces") < 1) {
        std::cerr << "remove_small_faces issues should carry heal.remove_small_faces stage\n";
        return 1;
    }
    if (count_stage(*diag_rse.value, axiom::diag_codes::kHealFeatureRemovedWarning, "heal.remove_small_edges") < 1) {
        std::cerr << "remove_small_edges issues should carry heal.remove_small_edges stage\n";
        return 1;
    }
    if (count_stage(*diag_mrg.value, axiom::diag_codes::kHealRepairPipelineTrace, "heal.repair_pipeline") < 1) {
        std::cerr << "merge_near_coplanar should still emit repair pipeline trace\n";
        return 1;
    }
    std::array<axiom::BodyId, 1> only_box {*box_a.value};
    auto many_auto = kernel.repair().repair_many_auto(only_box, axiom::RepairMode::Safe);
    auto many_edge = kernel.repair().repair_many_remove_small_edges(only_box, 0.01, axiom::RepairMode::Safe);
    auto many_face = kernel.repair().repair_many_remove_small_faces(only_box, 0.01, axiom::RepairMode::Safe);
    auto many_merge = kernel.repair().repair_many_merge_near_coplanar_faces(only_box, 0.01, axiom::RepairMode::Safe);
    if (many_auto.status != axiom::StatusCode::Ok || !many_auto.value.has_value() || many_auto.value->size() != 1 ||
        many_edge.status != axiom::StatusCode::Ok || !many_edge.value.has_value() || many_edge.value->size() != 1 ||
        many_face.status != axiom::StatusCode::Ok || !many_face.value.has_value() || many_face.value->size() != 1 ||
        many_merge.status != axiom::StatusCode::Ok || !many_merge.value.has_value() || many_merge.value->size() != 1) {
        std::cerr << "extended repair batch API behavior is unexpected\n";
        return 1;
    }
    auto modified_output = kernel.repair().was_modified_output(many_auto.value->front());
    auto new_body_output = kernel.repair().output_is_new_body(many_auto.value->front(), *box_a.value);
    auto shrink_ratio = kernel.repair().body_bbox_shrink_ratio(*box_a.value, remove_small_faces_default.value->output);
    auto extent_change = kernel.repair().compare_bbox_extent_change(*box_a.value, remove_small_faces_default.value->output);
    auto ensure_valid = kernel.repair().ensure_valid_after_repair(many_auto.value->front(), axiom::ValidationMode::Standard);
    auto summary = kernel.repair().summarize_repair(many_auto.value->front());
    if (modified_output.status != axiom::StatusCode::Ok || !modified_output.value.has_value() || !*modified_output.value ||
        new_body_output.status != axiom::StatusCode::Ok || !new_body_output.value.has_value() || !*new_body_output.value ||
        shrink_ratio.status != axiom::StatusCode::Ok || !shrink_ratio.value.has_value() ||
        extent_change.status != axiom::StatusCode::Ok || !extent_change.value.has_value() ||
        ensure_valid.status != axiom::StatusCode::Ok ||
        summary.status != axiom::StatusCode::Ok || !summary.value.has_value() || summary.value->empty() ||
        summary.value->find("issue_codes=") == std::string::npos ||
        summary.value->find(std::string(axiom::diag_codes::kHealRepairPipelineTrace)) == std::string::npos ||
        summary.value->find("pipeline_ops=") == std::string::npos ||
        summary.value->find("auto_repair") == std::string::npos) {
        std::cerr << "extended repair observable API behavior is unexpected\n";
        return 1;
    }

    {
        axiom::Kernel tol_kernel;
        auto tol_box = tol_kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 10.0, 10.0);
        if (tol_box.status != axiom::StatusCode::Ok || !tol_box.value.has_value()) {
            std::cerr << "tol_kernel box failed\n";
            return 1;
        }
        auto set_huge = tol_kernel.set_linear_tolerance(5.0);
        if (set_huge.status != axiom::StatusCode::Ok) {
            std::cerr << "set_linear_tolerance failed\n";
            return 1;
        }
        auto strict_bad = tol_kernel.validate().validate_all(*tol_box.value, axiom::ValidationMode::Strict);
        if (strict_bad.status != axiom::StatusCode::ToleranceConflict) {
            std::cerr << "expected Strict validate_all to fail on excessive linear tolerance\n";
            return 1;
        }
        auto strict_diag = tol_kernel.diagnostics().get(strict_bad.diagnostic_id);
        if (strict_diag.status != axiom::StatusCode::Ok || !strict_diag.value.has_value() ||
            !has_issue_code(*strict_diag.value, axiom::diag_codes::kValToleranceConflict)) {
            std::cerr << "expected kValToleranceConflict on strict tolerance validation\n";
            return 1;
        }
        auto std_ok = tol_kernel.validate().validate_all(*tol_box.value, axiom::ValidationMode::Standard);
        if (std_ok.status != axiom::StatusCode::Ok) {
            std::cerr << "Standard validate_all should still pass with large linear tolerance\n";
            return 1;
        }
        auto tol_box_b = tol_kernel.primitives().box({20.0, 0.0, 0.0}, 10.0, 10.0, 10.0);
        if (tol_box_b.status != axiom::StatusCode::Ok || !tol_box_b.value.has_value()) {
            std::cerr << "second tol_kernel box failed\n";
            return 1;
        }
        std::array<axiom::BodyId, 2> tol_pair {*tol_box.value, *tol_box_b.value};
        auto tol_many_strict = tol_kernel.validate().validate_tolerance_many(tol_pair, axiom::ValidationMode::Strict);
        if (tol_many_strict.status != axiom::StatusCode::ToleranceConflict) {
            std::cerr << "validate_tolerance_many Strict should fail when linear tolerance exceeds policy vs model\n";
            return 1;
        }
        auto tol_many_std = tol_kernel.validate().validate_tolerance_many(tol_pair, axiom::ValidationMode::Standard);
        if (tol_many_std.status != axiom::StatusCode::Ok) {
            std::cerr << "validate_tolerance_many Standard should not apply Strict max_local gate\n";
            return 1;
        }
    }

    {
        axiom::KernelConfig thin_cfg {};
        thin_cfg.tolerance.max_local = 0.01;
        axiom::Kernel thin_import_kernel(thin_cfg);
        const auto uniq = std::to_string(static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto json_path =
            std::filesystem::temp_directory_path() / ("axiom_heal_thin_import_" + uniq + ".axmjson");
        {
            std::ofstream out(json_path);
            out << R"({"format":"AXMJSON","body_kind":"Imported","label":"thin_sheet","bbox_min_x":0,"bbox_min_y":0,"bbox_min_z":0,"bbox_max_x":10,"bbox_max_y":10,"bbox_max_z":0.01})";
        }
        axiom::ImportOptions imp;
        auto imported = thin_import_kernel.io().import_axmjson(json_path.string(), imp);
        std::filesystem::remove(json_path);
        if (imported.status != axiom::StatusCode::Ok || !imported.value.has_value()) {
            std::cerr << "thin sheet axmjson import failed\n";
            return 1;
        }
        auto set_lin = thin_import_kernel.set_linear_tolerance(0.002);
        if (set_lin.status != axiom::StatusCode::Ok) {
            std::cerr << "set_linear_tolerance on thin import kernel failed\n";
            return 1;
        }
        auto strict_sheet =
            thin_import_kernel.validate().validate_tolerance(*imported.value, axiom::ValidationMode::Strict);
        if (strict_sheet.status != axiom::StatusCode::ToleranceConflict) {
            std::cerr << "expected Strict validate_tolerance to fail on thin imported bbox vs linear tolerance\n";
            return 1;
        }
        auto thin_diag = thin_import_kernel.diagnostics().get(strict_sheet.diagnostic_id);
        if (thin_diag.status != axiom::StatusCode::Ok || !thin_diag.value.has_value() ||
            !has_issue_code(*thin_diag.value, axiom::diag_codes::kValModelFinerThanTolerance)) {
            std::cerr << "expected kValModelFinerThanTolerance on thin imported strict tolerance validation\n";
            return 1;
        }
        auto std_sheet =
            thin_import_kernel.validate().validate_tolerance(*imported.value, axiom::ValidationMode::Standard);
        if (std_sheet.status != axiom::StatusCode::Ok) {
            std::cerr << "Standard validate_tolerance should not apply imported thin-sheet strict gate\n";
            return 1;
        }
    }

    {
        axiom::KernelConfig bad_policy {};
        bad_policy.tolerance.linear = 1e-6;
        bad_policy.tolerance.angular = 1e-6;
        bad_policy.tolerance.min_local = 1e-2;
        bad_policy.tolerance.max_local = 1e-4;
        axiom::Kernel k_bad_tol(bad_policy);
        auto box_tol = k_bad_tol.primitives().box({0.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
        if (box_tol.status != axiom::StatusCode::Ok || !box_tol.value.has_value()) {
            std::cerr << "box for tolerance policy test failed\n";
            return 1;
        }
        auto strict_tol = k_bad_tol.validate().validate_tolerance(*box_tol.value, axiom::ValidationMode::Strict);
        if (strict_tol.status != axiom::StatusCode::ToleranceConflict) {
            std::cerr << "expected Strict validate_tolerance to fail when min_local > max_local\n";
            return 1;
        }
        auto std_tol = k_bad_tol.validate().validate_tolerance(*box_tol.value, axiom::ValidationMode::Standard);
        if (std_tol.status != axiom::StatusCode::Ok) {
            std::cerr << "Standard validate_tolerance should not apply min_local/max_local ordering gate\n";
            return 1;
        }
    }

    {
        axiom::Kernel si_kernel;
        auto fresh_box = si_kernel.primitives().box({0.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
        if (fresh_box.status != axiom::StatusCode::Ok || !fresh_box.value.has_value()) {
            std::cerr << "fresh box for self-intersection check failed\n";
            return 1;
        }
        auto box_strict_si =
            si_kernel.validate().validate_self_intersection(*fresh_box.value, axiom::ValidationMode::Strict);
        if (box_strict_si.status != axiom::StatusCode::Ok) {
            std::cerr << "fresh box should pass strict mesh-based self-intersection check\n";
            return 1;
        }
        auto shells = si_kernel.topology().query().shells_of_body(*fresh_box.value);
        if (shells.status != axiom::StatusCode::Ok || !shells.value.has_value() || shells.value->empty()) {
            std::cerr << "fresh box should expose owned shells for shell self-intersection API\n";
            return 1;
        }
        const axiom::ShellId shell0 = shells.value->front();
        auto shell_strict_si = si_kernel.validate().validate_self_intersection_shell(
            *fresh_box.value, shell0, axiom::ValidationMode::Strict);
        if (shell_strict_si.status != axiom::StatusCode::Ok) {
            std::cerr << "shell-level strict self-intersection should pass on fresh box\n";
            return 1;
        }
        auto all_shells_si = si_kernel.validate().validate_self_intersection_all_shells(
            *fresh_box.value, axiom::ValidationMode::Strict);
        if (all_shells_si.status != axiom::StatusCode::Ok) {
            std::cerr << "validate_self_intersection_all_shells should pass on fresh box\n";
            return 1;
        }
        const std::span<const axiom::ShellId> empty_shell_span;
        auto shell_many_empty = si_kernel.validate().validate_self_intersection_shell_many(
            *fresh_box.value, empty_shell_span, axiom::ValidationMode::Strict);
        if (shell_many_empty.status != axiom::StatusCode::InvalidInput) {
            std::cerr << "empty shell span should yield InvalidInput for validate_self_intersection_shell_many\n";
            return 1;
        }
    }

    {
        axiom::Kernel ndk;
        auto plane_z = ndk.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto line_x = ndk.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto line_y = ndk.curves().make_line({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        auto line_neg_x = ndk.curves().make_line({1.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
        auto line_neg_y = ndk.curves().make_line({0.0, 1.0, 0.0}, {0.0, -1.0, 0.0});
        if (plane_z.status != axiom::StatusCode::Ok || line_x.status != axiom::StatusCode::Ok ||
            line_y.status != axiom::StatusCode::Ok || line_neg_x.status != axiom::StatusCode::Ok ||
            line_neg_y.status != axiom::StatusCode::Ok || !plane_z.value.has_value() || !line_x.value.has_value() ||
            !line_y.value.has_value() || !line_neg_x.value.has_value() || !line_neg_y.value.has_value()) {
            std::cerr << "near-duplicate vertex test: plane/lines failed\n";
            return 1;
        }
        auto txn_nd = ndk.topology().begin_transaction();
        const double zeps = 1e-9;
        auto vA = txn_nd.create_vertex({0.0, 0.0, 0.0});
        auto vB = txn_nd.create_vertex({1e-10, 0.0, 0.0});
        auto v10 = txn_nd.create_vertex({1.0, 0.0, 0.0});
        auto v11 = txn_nd.create_vertex({1.0, 1.0, zeps});
        auto v01 = txn_nd.create_vertex({0.0, 1.0, zeps});
        if (vA.status != axiom::StatusCode::Ok || vB.status != axiom::StatusCode::Ok ||
            v10.status != axiom::StatusCode::Ok || v11.status != axiom::StatusCode::Ok ||
            v01.status != axiom::StatusCode::Ok || !vA.value.has_value() || !vB.value.has_value() ||
            !v10.value.has_value() || !v11.value.has_value() || !v01.value.has_value()) {
            std::cerr << "near-duplicate vertex test: vertices failed\n";
            return 1;
        }
        auto eAB = txn_nd.create_edge(*line_x.value, *vA.value, *vB.value);
        auto eB10 = txn_nd.create_edge(*line_x.value, *vB.value, *v10.value);
        auto e1011 = txn_nd.create_edge(*line_y.value, *v10.value, *v11.value);
        auto e1101 = txn_nd.create_edge(*line_neg_x.value, *v11.value, *v01.value);
        auto e01A = txn_nd.create_edge(*line_neg_y.value, *v01.value, *vA.value);
        if (eAB.status != axiom::StatusCode::Ok || eB10.status != axiom::StatusCode::Ok ||
            e1011.status != axiom::StatusCode::Ok || e1101.status != axiom::StatusCode::Ok ||
            e01A.status != axiom::StatusCode::Ok || !eAB.value.has_value() || !eB10.value.has_value() ||
            !e1011.value.has_value() || !e1101.value.has_value() || !e01A.value.has_value()) {
            std::cerr << "near-duplicate vertex test: edges failed\n";
            return 1;
        }
        auto c0 = txn_nd.create_coedge(*eAB.value, false);
        auto c1 = txn_nd.create_coedge(*eB10.value, false);
        auto c2 = txn_nd.create_coedge(*e1011.value, false);
        auto c3 = txn_nd.create_coedge(*e1101.value, false);
        auto c4 = txn_nd.create_coedge(*e01A.value, false);
        if (c0.status != axiom::StatusCode::Ok || c1.status != axiom::StatusCode::Ok ||
            c2.status != axiom::StatusCode::Ok || c3.status != axiom::StatusCode::Ok ||
            c4.status != axiom::StatusCode::Ok || !c0.value.has_value() || !c1.value.has_value() ||
            !c2.value.has_value() || !c3.value.has_value() || !c4.value.has_value()) {
            std::cerr << "near-duplicate vertex test: coedges failed\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 5> c_nd {*c0.value, *c1.value, *c2.value, *c3.value, *c4.value};
        auto lp = txn_nd.create_loop(c_nd);
        auto fc = txn_nd.create_face(*plane_z.value, *lp.value, {});
        auto sh = txn_nd.create_shell(std::array<axiom::FaceId, 1> {*fc.value});
        auto bd = txn_nd.create_body(std::array<axiom::ShellId, 1> {*sh.value});
        if (lp.status != axiom::StatusCode::Ok || fc.status != axiom::StatusCode::Ok ||
            sh.status != axiom::StatusCode::Ok || bd.status != axiom::StatusCode::Ok ||
            !lp.value.has_value() || !fc.value.has_value() || !sh.value.has_value() || !bd.value.has_value()) {
            std::cerr << "near-duplicate vertex test: topology assembly failed\n";
            return 1;
        }
        if (txn_nd.commit().status != axiom::StatusCode::Ok) {
            std::cerr << "near-duplicate vertex test: commit failed\n";
            return 1;
        }
        auto nd_std = ndk.validate().validate_geometry(*bd.value, axiom::ValidationMode::Standard);
        if (nd_std.status != axiom::StatusCode::Ok) {
            std::cerr << "near-duplicate body should pass Standard geometry (no near-dup gate)\n";
            return 1;
        }
        auto nd_strict = ndk.validate().validate_geometry(*bd.value, axiom::ValidationMode::Strict);
        if (nd_strict.status != axiom::StatusCode::DegenerateGeometry) {
            std::cerr << "expected Strict validate_geometry DegenerateGeometry on near-duplicate vertices body\n";
            return 1;
        }
        auto nd_diag = ndk.diagnostics().get(nd_strict.diagnostic_id);
        if (nd_diag.status != axiom::StatusCode::Ok || !nd_diag.value.has_value() ||
            !has_issue_code(*nd_diag.value, axiom::diag_codes::kValNearDuplicateVertices)) {
            std::cerr << "expected kValNearDuplicateVertices diagnostic for near-duplicate vertex body\n";
            return 1;
        }
    }

    {
        axiom::Kernel fnk;
        auto plane_wrong = fnk.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        auto line_x = fnk.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto line_y = fnk.curves().make_line({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        auto line_neg_x = fnk.curves().make_line({1.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
        auto line_neg_y = fnk.curves().make_line({0.0, 1.0, 0.0}, {0.0, -1.0, 0.0});
        if (plane_wrong.status != axiom::StatusCode::Ok || line_x.status != axiom::StatusCode::Ok ||
            line_y.status != axiom::StatusCode::Ok || line_neg_x.status != axiom::StatusCode::Ok ||
            line_neg_y.status != axiom::StatusCode::Ok || !plane_wrong.value.has_value() ||
            !line_x.value.has_value() || !line_y.value.has_value() || !line_neg_x.value.has_value() ||
            !line_neg_y.value.has_value()) {
            std::cerr << "face-normal test: plane/lines failed\n";
            return 1;
        }
        auto txn_fn = fnk.topology().begin_transaction();
        const double z_hi = 1e-2;
        auto fv0 = txn_fn.create_vertex({0.0, 0.0, 0.0});
        auto fv1 = txn_fn.create_vertex({1.0, 0.0, 0.0});
        auto fv2 = txn_fn.create_vertex({1.0, 1.0, z_hi});
        auto fv3 = txn_fn.create_vertex({0.0, 1.0, z_hi});
        if (fv0.status != axiom::StatusCode::Ok || fv1.status != axiom::StatusCode::Ok ||
            fv2.status != axiom::StatusCode::Ok || fv3.status != axiom::StatusCode::Ok ||
            !fv0.value.has_value() || !fv1.value.has_value() || !fv2.value.has_value() ||
            !fv3.value.has_value()) {
            std::cerr << "face-normal test: vertices failed\n";
            return 1;
        }
        auto fe0 = txn_fn.create_edge(*line_x.value, *fv0.value, *fv1.value);
        auto fe1 = txn_fn.create_edge(*line_y.value, *fv1.value, *fv2.value);
        auto fe2 = txn_fn.create_edge(*line_neg_x.value, *fv2.value, *fv3.value);
        auto fe3 = txn_fn.create_edge(*line_neg_y.value, *fv3.value, *fv0.value);
        if (fe0.status != axiom::StatusCode::Ok || fe1.status != axiom::StatusCode::Ok ||
            fe2.status != axiom::StatusCode::Ok || fe3.status != axiom::StatusCode::Ok ||
            !fe0.value.has_value() || !fe1.value.has_value() || !fe2.value.has_value() ||
            !fe3.value.has_value()) {
            std::cerr << "face-normal test: edges failed\n";
            return 1;
        }
        auto fco0 = txn_fn.create_coedge(*fe0.value, false);
        auto fco1 = txn_fn.create_coedge(*fe1.value, false);
        auto fco2 = txn_fn.create_coedge(*fe2.value, false);
        auto fco3 = txn_fn.create_coedge(*fe3.value, false);
        if (fco0.status != axiom::StatusCode::Ok || fco1.status != axiom::StatusCode::Ok ||
            fco2.status != axiom::StatusCode::Ok || fco3.status != axiom::StatusCode::Ok ||
            !fco0.value.has_value() || !fco1.value.has_value() || !fco2.value.has_value() ||
            !fco3.value.has_value()) {
            std::cerr << "face-normal test: coedges failed\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 4> fco {*fco0.value, *fco1.value, *fco2.value, *fco3.value};
        auto floop = txn_fn.create_loop(fco);
        auto fface = txn_fn.create_face(*plane_wrong.value, *floop.value, {});
        auto fshell = txn_fn.create_shell(std::array<axiom::FaceId, 1> {*fface.value});
        auto fbody = txn_fn.create_body(std::array<axiom::ShellId, 1> {*fshell.value});
        if (floop.status != axiom::StatusCode::Ok || fface.status != axiom::StatusCode::Ok ||
            fshell.status != axiom::StatusCode::Ok || fbody.status != axiom::StatusCode::Ok ||
            !floop.value.has_value() || !fface.value.has_value() || !fshell.value.has_value() ||
            !fbody.value.has_value()) {
            std::cerr << "face-normal test: topology failed\n";
            return 1;
        }
        if (txn_fn.commit().status != axiom::StatusCode::Ok) {
            std::cerr << "face-normal test: commit failed\n";
            return 1;
        }
        auto fn_strict = fnk.validate().validate_geometry(*fbody.value, axiom::ValidationMode::Strict);
        if (fn_strict.status != axiom::StatusCode::DegenerateGeometry) {
            std::cerr << "expected Strict validate_geometry DegenerateGeometry on mismatched plane normal face\n";
            return 1;
        }
        auto fn_diag = fnk.diagnostics().get(fn_strict.diagnostic_id);
        if (fn_diag.status != axiom::StatusCode::Ok || !fn_diag.value.has_value() ||
            !has_issue_code(*fn_diag.value, axiom::diag_codes::kValFaceNormalInconsistent)) {
            std::cerr << "expected kValFaceNormalInconsistent diagnostic for wrong plane normal face\n";
            return 1;
        }
    }

    return 0;
}
