#include <array>
#include <iostream>

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

}  // namespace

int main() {
    axiom::Kernel kernel;

    auto box_a = kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 10.0, 10.0);
    auto box_b = kernel.primitives().box({30.0, 30.0, 30.0}, 5.0, 5.0, 5.0);
    if (box_a.status != axiom::StatusCode::Ok || box_b.status != axiom::StatusCode::Ok ||
        !box_a.value.has_value() || !box_b.value.has_value()) {
        std::cerr << "failed to create boxes for ops/heal test\n";
        return 1;
    }

    auto bad_intersection = kernel.booleans().run(axiom::BooleanOp::Intersect, *box_a.value, *box_b.value, {});
    if (bad_intersection.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "expected failed intersection for disjoint boxes\n";
        return 1;
    }

    auto bad_intersection_diag = kernel.diagnostics().get(bad_intersection.diagnostic_id);
    if (bad_intersection_diag.status != axiom::StatusCode::Ok || !bad_intersection_diag.value.has_value() ||
        !has_issue_code(*bad_intersection_diag.value, axiom::diag_codes::kBoolIntersectionFailure)) {
        std::cerr << "missing intersection failure diagnostic\n";
        return 1;
    }

    auto bad_offset = kernel.modify().offset_body(*box_a.value, -6.0, {});
    if (bad_offset.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "expected failed inward offset\n";
        return 1;
    }
    auto shell_too_thick = kernel.modify().shell_body(*box_a.value, {}, 4.9999995);
    if (shell_too_thick.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "expected failed shell for tolerance-near thick wall\n";
        return 1;
    }
    auto original_box_bbox_after_shell_fail = kernel.representation().bbox_of_body(*box_a.value);
    if (original_box_bbox_after_shell_fail.status != axiom::StatusCode::Ok ||
        !original_box_bbox_after_shell_fail.value.has_value() ||
        original_box_bbox_after_shell_fail.value->max.x != 10.0) {
        std::cerr << "shell failure should not mutate source body\n";
        return 1;
    }

    auto sweep_ok = kernel.sweeps().extrude({"profile"}, {0.0, 0.0, 1.0}, 5.0);
    auto sweep_bad = kernel.sweeps().extrude({"profile"}, {0.0, 0.0, 0.0}, 5.0);
    if (sweep_ok.status != axiom::StatusCode::Ok || !sweep_ok.value.has_value() ||
        sweep_bad.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "unexpected extrude behavior\n";
        return 1;
    }
    auto extrude_strict = kernel.validate().validate_topology(*sweep_ok.value, axiom::ValidationMode::Strict);
    if (extrude_strict.status != axiom::StatusCode::Ok) {
        std::cerr << "extrude result failed strict topology validation\n";
        return 1;
    }

    auto box_shells = kernel.topology().query().shells_of_body(*box_a.value);
    if (box_shells.status != axiom::StatusCode::Ok || !box_shells.value.has_value() || box_shells.value->empty()) {
        std::cerr << "expected shells on box_a for thicken test\n";
        return 1;
    }
    auto box_faces = kernel.topology().query().faces_of_shell(box_shells.value->front());
    if (box_faces.status != axiom::StatusCode::Ok || !box_faces.value.has_value() || box_faces.value->empty()) {
        std::cerr << "expected faces on box_a shell for thicken test\n";
        return 1;
    }
    auto thicken_ok = kernel.sweeps().thicken(box_faces.value->front(), 1.0);
    if (thicken_ok.status != axiom::StatusCode::Ok || !thicken_ok.value.has_value()) {
        std::cerr << "unexpected thicken failure\n";
        return 1;
    }
    auto thicken_strict = kernel.validate().validate_topology(*thicken_ok.value, axiom::ValidationMode::Strict);
    if (thicken_strict.status != axiom::StatusCode::Ok) {
        std::cerr << "thicken result failed strict topology validation\n";
        return 1;
    }

    auto plane0 = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    auto plane1 = kernel.surfaces().make_plane({0.0, 0.0, 1.0}, {0.0, 0.0, 1.0});
    auto line = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    if (plane0.status != axiom::StatusCode::Ok || plane1.status != axiom::StatusCode::Ok ||
        line.status != axiom::StatusCode::Ok || !plane0.value.has_value() || !plane1.value.has_value() ||
        !line.value.has_value()) {
        std::cerr << "failed to create topology prerequisites\n";
        return 1;
    }

    auto txn = kernel.topology().begin_transaction();
    auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
    auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
    if (v0.status != axiom::StatusCode::Ok || v1.status != axiom::StatusCode::Ok ||
        !v0.value.has_value() || !v1.value.has_value()) {
        std::cerr << "failed to create vertices\n";
        return 1;
    }

    auto edge = txn.create_edge(*line.value, *v0.value, *v1.value);
    if (edge.status != axiom::StatusCode::Ok || !edge.value.has_value()) {
        std::cerr << "failed to create edge\n";
        return 1;
    }

    auto coedge = txn.create_coedge(*edge.value, false);
    if (coedge.status != axiom::StatusCode::Ok || !coedge.value.has_value()) {
        std::cerr << "failed to create coedge\n";
        return 1;
    }

    const std::array<axiom::CoedgeId, 1> coedges {*coedge.value};
    auto loop = txn.create_loop(coedges);
    if (loop.status != axiom::StatusCode::Ok || !loop.value.has_value()) {
        std::cerr << "failed to create loop\n";
        return 1;
    }

    auto face = txn.create_face(*plane0.value, *loop.value, {});
    if (face.status != axiom::StatusCode::Ok || !face.value.has_value()) {
        std::cerr << "failed to create face\n";
        return 1;
    }

    auto replace_face = kernel.modify().replace_face(*box_a.value, *face.value, *plane1.value);
    auto delete_face_and_heal = kernel.modify().delete_face_and_heal(*box_a.value, *face.value);
    if (replace_face.status != axiom::StatusCode::Ok || !replace_face.value.has_value() ||
        delete_face_and_heal.status != axiom::StatusCode::Ok || !delete_face_and_heal.value.has_value()) {
        std::cerr << "failed modify operations on face\n";
        return 1;
    }

    auto delete_face_diag = kernel.diagnostics().get(delete_face_and_heal.value->diagnostic_id);
    if (delete_face_diag.status != axiom::StatusCode::Ok || !delete_face_diag.value.has_value() ||
        !has_issue_code(*delete_face_diag.value, axiom::diag_codes::kHealFeatureRemovedWarning)) {
        std::cerr << "expected warning diagnostic for delete_face_and_heal\n";
        return 1;
    }

    auto replace_face_sources = kernel.topology().query().source_faces_of_body(replace_face.value->output);
    auto delete_face_sources = kernel.topology().query().source_faces_of_body(delete_face_and_heal.value->output);
    auto replace_face_bodies = kernel.topology().query().source_bodies_of_body(replace_face.value->output);
    auto replace_face_shells = kernel.topology().query().source_shells_of_body(replace_face.value->output);
    auto delete_face_shells = kernel.topology().query().source_shells_of_body(delete_face_and_heal.value->output);
    auto replace_face_owned_shells = kernel.topology().query().shells_of_body(replace_face.value->output);
    auto delete_face_owned_shells = kernel.topology().query().shells_of_body(delete_face_and_heal.value->output);
    auto replace_face_owned_faces = replace_face_owned_shells.status == axiom::StatusCode::Ok && replace_face_owned_shells.value.has_value() &&
                                            replace_face_owned_shells.value->size() == 1
                                        ? kernel.topology().query().faces_of_shell(replace_face_owned_shells.value->front())
                                        : axiom::Result<std::vector<axiom::FaceId>> {};
    auto delete_face_owned_faces = delete_face_owned_shells.status == axiom::StatusCode::Ok && delete_face_owned_shells.value.has_value() &&
                                           delete_face_owned_shells.value->size() == 1
                                       ? kernel.topology().query().faces_of_shell(delete_face_owned_shells.value->front())
                                       : axiom::Result<std::vector<axiom::FaceId>> {};
    if (replace_face_sources.status != axiom::StatusCode::Ok || !replace_face_sources.value.has_value() ||
        delete_face_sources.status != axiom::StatusCode::Ok || !delete_face_sources.value.has_value() ||
        replace_face_bodies.status != axiom::StatusCode::Ok || !replace_face_bodies.value.has_value() ||
        replace_face_shells.status != axiom::StatusCode::Ok || !replace_face_shells.value.has_value() ||
        delete_face_shells.status != axiom::StatusCode::Ok || !delete_face_shells.value.has_value() ||
        replace_face_owned_shells.status != axiom::StatusCode::Ok || !replace_face_owned_shells.value.has_value() ||
        delete_face_owned_shells.status != axiom::StatusCode::Ok || !delete_face_owned_shells.value.has_value() ||
        replace_face_owned_faces.status != axiom::StatusCode::Ok || !replace_face_owned_faces.value.has_value() ||
        delete_face_owned_faces.status != axiom::StatusCode::Ok || !delete_face_owned_faces.value.has_value() ||
        replace_face_sources.value->size() != 1 || replace_face_sources.value->front().value != face.value->value ||
        delete_face_sources.value->size() != 1 || delete_face_sources.value->front().value != face.value->value ||
        replace_face_bodies.value->size() != 1 || replace_face_bodies.value->front().value != box_a.value->value ||
        !replace_face_shells.value->empty() ||
        !delete_face_shells.value->empty() ||
        replace_face_owned_shells.value->size() != 1 ||
        delete_face_owned_shells.value->size() != 1 ||
        replace_face_owned_faces.value->size() != 6 ||
        delete_face_owned_faces.value->size() != 6) {
        std::cerr << "modify result provenance is unexpected\n";
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
        !has_issue_code(*repair_diag.value, axiom::diag_codes::kHealRepairValidated)) {
        std::cerr << "expected repair warning diagnostic\n";
        return 1;
    }
    const auto* feature_removed_issue = find_issue(*repair_diag.value, axiom::diag_codes::kHealFeatureRemovedWarning);
    const auto* validated_issue = find_issue(*repair_diag.value, axiom::diag_codes::kHealRepairValidated);
    if (feature_removed_issue == nullptr || validated_issue == nullptr ||
        feature_removed_issue->related_entities.size() != 2 ||
        validated_issue->related_entities.size() != 2 ||
        feature_removed_issue->related_entities.front() != box_a.value->value ||
        validated_issue->related_entities.front() != box_a.value->value ||
        feature_removed_issue->related_entities.back() != auto_repair.value->output.value ||
        validated_issue->related_entities.back() != auto_repair.value->output.value) {
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
    auto validate_many = kernel.validate().validate_all_many(body_pair, axiom::ValidationMode::Standard);
    auto validate_geom_many = kernel.validate().validate_geometry_many(body_pair, axiom::ValidationMode::Standard);
    auto validate_topo_many = kernel.validate().validate_topology_many(body_pair, axiom::ValidationMode::Standard);
    auto bbox_many = kernel.validate().validate_bbox_many(body_pair);
    auto invalid_count = kernel.validate().count_invalid_in(body_pair, axiom::ValidationMode::Standard);
    auto valid_filtered = kernel.validate().filter_valid_bodies(body_pair, axiom::ValidationMode::Standard);
    auto invalid_filtered = kernel.validate().filter_invalid_bodies(body_pair, axiom::ValidationMode::Standard);
    auto geom_valid = kernel.validate().is_geometry_valid(*box_a.value, axiom::ValidationMode::Standard);
    auto topo_valid = kernel.validate().is_topology_valid(*box_a.value, axiom::ValidationMode::Standard);
    auto all_valid = kernel.validate().is_valid(*box_a.value, axiom::ValidationMode::Standard);
    if (validate_many.status != axiom::StatusCode::Ok || validate_geom_many.status != axiom::StatusCode::Ok ||
        validate_topo_many.status != axiom::StatusCode::Ok || bbox_many.status != axiom::StatusCode::Ok ||
        invalid_count.status != axiom::StatusCode::Ok || !invalid_count.value.has_value() || *invalid_count.value != 0 ||
        valid_filtered.status != axiom::StatusCode::Ok || !valid_filtered.value.has_value() || valid_filtered.value->size() != 2 ||
        invalid_filtered.status != axiom::StatusCode::Ok || !invalid_filtered.value.has_value() || !invalid_filtered.value->empty() ||
        geom_valid.status != axiom::StatusCode::Ok || !geom_valid.value.has_value() || !*geom_valid.value ||
        topo_valid.status != axiom::StatusCode::Ok || !topo_valid.value.has_value() || !*topo_valid.value ||
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
        summary.status != axiom::StatusCode::Ok || !summary.value.has_value() || summary.value->empty()) {
        std::cerr << "extended repair observable API behavior is unexpected\n";
        return 1;
    }

    auto first_offset = kernel.modify().offset_body(*box_a.value, 1.0, {});
    if (first_offset.status != axiom::StatusCode::Ok || !first_offset.value.has_value()) {
        std::cerr << "failed to create first derived offset body\n";
        return 1;
    }
    auto second_offset = kernel.modify().offset_body(first_offset.value->output, 0.5, {});
    if (second_offset.status != axiom::StatusCode::Ok || !second_offset.value.has_value()) {
        std::cerr << "failed to create second derived offset body\n";
        return 1;
    }

    auto first_offset_shells = kernel.topology().query().shells_of_body(first_offset.value->output);
    auto second_offset_shells = kernel.topology().query().shells_of_body(second_offset.value->output);
    auto second_offset_source_shells = kernel.topology().query().source_shells_of_body(second_offset.value->output);
    if (first_offset_shells.status != axiom::StatusCode::Ok || !first_offset_shells.value.has_value() ||
        second_offset_shells.status != axiom::StatusCode::Ok || !second_offset_shells.value.has_value() ||
        second_offset_source_shells.status != axiom::StatusCode::Ok || !second_offset_source_shells.value.has_value() ||
        first_offset_shells.value->size() != 1 || second_offset_shells.value->size() != 1 ||
        second_offset_source_shells.value->size() != 1 ||
        second_offset_source_shells.value->front().value != first_offset_shells.value->front().value) {
        std::cerr << "second-generation offset did not inherit source shell provenance as expected\n";
        return 1;
    }

    auto second_offset_owned_faces = kernel.topology().query().faces_of_shell(second_offset_shells.value->front());
    auto second_offset_source_faces = kernel.topology().query().source_faces_of_shell(second_offset_shells.value->front());
    if (second_offset_owned_faces.status != axiom::StatusCode::Ok || !second_offset_owned_faces.value.has_value() ||
        second_offset_source_faces.status != axiom::StatusCode::Ok || !second_offset_source_faces.value.has_value() ||
        second_offset_owned_faces.value->size() != 6 || second_offset_source_faces.value->size() != 6) {
        std::cerr << "second-generation offset shell layout is unexpected\n";
        return 1;
    }
    const bool cloned_face_ids_reused = second_offset_owned_faces.value->front().value == second_offset_source_faces.value->front().value;
    if (cloned_face_ids_reused) {
        std::cerr << "second-generation offset should clone source shell topology instead of reusing owned face ids\n";
        return 1;
    }

    auto degrade_source_txn = kernel.topology().begin_transaction();
    auto degrade_source = degrade_source_txn.delete_face(second_offset_source_faces.value->front());
    if (degrade_source.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to delete one source face for degraded-source reconstruction case\n";
        return 1;
    }
    auto degrade_commit = degrade_source_txn.commit();
    if (degrade_commit.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to commit degraded-source topology change\n";
        return 1;
    }

    auto third_offset = kernel.modify().offset_body(second_offset.value->output, 0.25, {});
    if (third_offset.status != axiom::StatusCode::Ok || !third_offset.value.has_value()) {
        std::cerr << "third-generation offset should still succeed when part of source topology is missing\n";
        return 1;
    }
    auto third_offset_shells = kernel.topology().query().shells_of_body(third_offset.value->output);
    auto third_offset_standard = kernel.validate().validate_topology(third_offset.value->output, axiom::ValidationMode::Standard);
    if (third_offset_shells.status != axiom::StatusCode::Ok || !third_offset_shells.value.has_value() ||
        third_offset_shells.value->size() != 1 || third_offset_standard.status != axiom::StatusCode::Ok) {
        std::cerr << "degraded-source reconstruction did not produce a valid owned topology fallback\n";
        return 1;
    }

    return 0;
}
