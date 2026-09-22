#include <algorithm>
#include <iostream>
#include <filesystem>
#include <fstream>

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

bool has_warning_code(const std::vector<axiom::Warning>& warnings, std::string_view code) {
    for (const auto& warning : warnings) {
        if (warning.code == code) {
            return true;
        }
    }
    return false;
}

// Every returned prep warning must remain searchable and exportable with its entity context.
bool check_prep_warning(axiom::Kernel& kernel, const axiom::OpReport& result,
                        axiom::BodyId lhs, axiom::BodyId rhs, std::string_view code) {
    const auto report = kernel.diagnostics().get(result.diagnostic_id);
    if (report.status != axiom::StatusCode::Ok || !report.value.has_value()) return false;
    const auto* issue = find_issue(*report.value, code);
    if (issue == nullptr || issue->severity != axiom::IssueSeverity::Warning || issue->stage != "bool.prep" ||
        issue->related_entities != std::vector<std::uint64_t>{lhs.value, rhs.value, result.output.value}) return false;
    const auto warning = std::find_if(result.warnings.begin(), result.warnings.end(),
                                    [code](const auto& item) { return item.code == code; });
    if (warning == result.warnings.end() || warning->message != issue->message) return false;
    const auto ids = kernel.diagnostics().find_by_issue_stage("bool.prep", 1000);
    if (!ids.value.has_value() || std::none_of(ids.value->begin(), ids.value->end(),
        [&](auto id) { return id.value == result.diagnostic_id.value; })) return false;
    const auto path = std::filesystem::temp_directory_path() / "axiom_boolean_prep_warning.json";
    if (kernel.diagnostics().export_report_json(result.diagnostic_id, path.string()).status !=
        axiom::StatusCode::Ok) return false;
    std::ifstream in {path};
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::filesystem::remove(path);
    const auto start = json.find("\"code\":\"" + std::string(code) + "\"");
    if (start == std::string::npos) return false;
    const auto item = json.substr(start, json.find('}', start) - start);
    return item.find("\"stage\":\"bool.prep\"") != std::string::npos &&
           item.find("\"related_entities\":[" + std::to_string(lhs.value) + "," + std::to_string(rhs.value) +
                     "," + std::to_string(result.output.value) + "]") != std::string::npos;
}

}  // namespace

int main() {
    axiom::Kernel kernel;

    auto line = kernel.curves().make_line({0.0, 0.0, -1.0}, {0.0, 0.0, 1.0});
    auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    auto bad_line = kernel.curves().make_line({0.0, 0.0, 1.0}, {1.0, 0.0, 0.0});
    if (line.status != axiom::StatusCode::Ok || plane.status != axiom::StatusCode::Ok ||
        bad_line.status != axiom::StatusCode::Ok || !line.value.has_value() ||
        !plane.value.has_value() || !bad_line.value.has_value()) {
        std::cerr << "failed to create curve/surface for boolean prep test\n";
        return 1;
    }

    auto line_plane = kernel.query().intersect(*line.value, *plane.value);
    auto bad_line_plane = kernel.query().intersect(*bad_line.value, *plane.value);
    if (line_plane.status != axiom::StatusCode::Ok || !line_plane.value.has_value()) {
        std::cerr << "expected line-plane intersection success\n";
        return 1;
    }
    if (bad_line_plane.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "expected parallel line-plane intersection failure\n";
        return 1;
    }

    auto plane2 = kernel.surfaces().make_plane({0.0, 0.0, 10.0}, {0.0, 0.0, 1.0});
    auto sphere0 = kernel.surfaces().make_sphere({0.0, 0.0, 0.0}, 5.0);
    auto sphere1 = kernel.surfaces().make_sphere({8.0, 0.0, 0.0}, 5.0);
    if (plane2.status != axiom::StatusCode::Ok || sphere0.status != axiom::StatusCode::Ok ||
        sphere1.status != axiom::StatusCode::Ok || !plane2.value.has_value() ||
        !sphere0.value.has_value() || !sphere1.value.has_value()) {
        std::cerr << "failed to create surfaces\n";
        return 1;
    }

    auto bad_plane_plane = kernel.query().intersect(*plane.value, *plane2.value);
    auto sphere_sphere = kernel.query().intersect(*sphere0.value, *sphere1.value);
    if (bad_plane_plane.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "expected parallel plane-plane failure\n";
        return 1;
    }
    if (sphere_sphere.status != axiom::StatusCode::Ok || !sphere_sphere.value.has_value()) {
        std::cerr << "expected sphere-sphere intersection success\n";
        return 1;
    }

    auto outer = kernel.primitives().box({0.0, 0.0, 0.0}, 20.0, 20.0, 20.0);
    auto inner = kernel.primitives().box({2.0, 2.0, 2.0}, 4.0, 4.0, 4.0);
    auto far = kernel.primitives().box({100.0, 100.0, 100.0}, 2.0, 2.0, 2.0);
    if (outer.status != axiom::StatusCode::Ok || inner.status != axiom::StatusCode::Ok ||
        far.status != axiom::StatusCode::Ok || !outer.value.has_value() ||
        !inner.value.has_value() || !far.value.has_value()) {
        std::cerr << "failed to create boolean bodies\n";
        return 1;
    }

    const auto bodies_before = kernel.body_count();
    const auto geometry_before = kernel.geometry_count();
    const auto topology_before = kernel.topology_count();
    auto subtract_empty = kernel.booleans().run(axiom::BooleanOp::Subtract, *inner.value, *outer.value, {});
    if (subtract_empty.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "expected subtract containment failure\n";
        return 1;
    }

    auto subtract_diag = kernel.diagnostics().get(subtract_empty.diagnostic_id);
    if (subtract_diag.status != axiom::StatusCode::Ok || !subtract_diag.value.has_value() ||
        !has_issue_code(*subtract_diag.value, axiom::diag_codes::kBoolClassificationFailure)) {
        std::cerr << "missing subtract containment diagnostic\n";
        return 1;
    }
    const auto* cls_issue = find_issue(*subtract_diag.value, axiom::diag_codes::kBoolClassificationFailure);
    if (cls_issue == nullptr || cls_issue->stage != "bool.abort.classify") {
        std::cerr << "expected bool.abort.classify stage on subtract containment failure\n";
        return 1;
    }

    auto intersect_disjoint = kernel.booleans().run(axiom::BooleanOp::Intersect, *outer.value, *far.value, {});
    if (intersect_disjoint.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "expected disjoint intersect failure\n";
        return 1;
    }
    auto intersect_disjoint_diag = kernel.diagnostics().get(intersect_disjoint.diagnostic_id);
    if (intersect_disjoint_diag.status != axiom::StatusCode::Ok || !intersect_disjoint_diag.value.has_value() ||
        !has_issue_code(*intersect_disjoint_diag.value, axiom::diag_codes::kBoolIntersectionFailure)) {
        std::cerr << "missing disjoint intersect diagnostic\n";
        return 1;
    }
    const auto* isect_issue = find_issue(*intersect_disjoint_diag.value, axiom::diag_codes::kBoolIntersectionFailure);
    if (isect_issue == nullptr || isect_issue->stage != "bool.abort.intersect") {
        std::cerr << "expected bool.abort.intersect stage on disjoint intersect failure\n";
        return 1;
    }

    const auto bodies_after = kernel.body_count();
    const auto geometry_after = kernel.geometry_count();
    const auto topology_after = kernel.topology_count();
    if (!bodies_before.value || !geometry_before.value || !topology_before.value ||
        bodies_before.value != bodies_after.value || geometry_before.value != geometry_after.value ||
        topology_before.value != topology_after.value || subtract_empty.value || intersect_disjoint.value ||
        cls_issue->related_entities != std::vector<std::uint64_t>{inner.value->value, outer.value->value} ||
        isect_issue->related_entities != std::vector<std::uint64_t>{outer.value->value, far.value->value}) {
        std::cerr << "boolean early failure polluted model or lost diagnostic entities\n";
        return 1;
    }

    auto union_disjoint = kernel.booleans().run(axiom::BooleanOp::Union, *outer.value, *far.value, {});
    if (union_disjoint.status != axiom::StatusCode::Ok || !union_disjoint.value.has_value()) {
        std::cerr << "expected disjoint union success\n";
        return 1;
    }
    if (!has_warning_code(union_disjoint.value->warnings, axiom::diag_codes::kBoolNearDegenerateWarning)) {
        std::cerr << "expected warning for disjoint union placeholder semantics\n";
        return 1;
    }
    auto union_diag = kernel.diagnostics().get(union_disjoint.value->diagnostic_id);
    if (union_diag.status != axiom::StatusCode::Ok || !union_diag.value.has_value() ||
        !has_issue_code(*union_diag.value, axiom::diag_codes::kBoolPrepCandidatesBuilt)) {
        std::cerr << "expected boolean prep candidate diagnostic issue\n";
        return 1;
    }

    if (!check_prep_warning(kernel, *union_disjoint.value, *outer.value, *far.value,
                            axiom::diag_codes::kBoolNearDegenerateWarning)) {
        std::cerr << "disjoint union warning lost stage/entity context\n";
        return 1;
    }

    auto overlap_a = kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 10.0, 10.0);
    auto overlap_b = kernel.primitives().box({5.0, 5.0, 5.0}, 10.0, 10.0, 10.0);
    if (overlap_a.status != axiom::StatusCode::Ok || overlap_b.status != axiom::StatusCode::Ok ||
        !overlap_a.value.has_value() || !overlap_b.value.has_value()) {
        std::cerr << "failed to create overlap bodies for local clip test\n";
        return 1;
    }
    auto intersect_overlap = kernel.booleans().run(axiom::BooleanOp::Intersect, *overlap_a.value, *overlap_b.value, {});
    if (intersect_overlap.status != axiom::StatusCode::Ok || !intersect_overlap.value.has_value()) {
        std::cerr << "expected overlap intersection success\n";
        return 1;
    }
    const auto prep_path = std::filesystem::temp_directory_path() / "axiom_boolean_prep_stats.json";
    auto exported_stats = kernel.booleans().export_boolean_prep_stats(*overlap_a.value, *overlap_b.value, prep_path.string());
    if (exported_stats.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to export boolean prep stats\n";
        return 1;
    }
    std::ifstream stats_in {prep_path};
    std::string stats_text((std::istreambuf_iterator<char>(stats_in)), std::istreambuf_iterator<char>());
    if (stats_text.find("\"overlap_candidates\"") == std::string::npos ||
        stats_text.find("\"local_clip_applied\":true") == std::string::npos) {
        std::cerr << "unexpected boolean prep stats json content\n";
        std::filesystem::remove(prep_path);
        return 1;
    }
    auto intersect_diag = kernel.diagnostics().get(intersect_overlap.value->diagnostic_id);
    if (intersect_diag.status != axiom::StatusCode::Ok || !intersect_diag.value.has_value() ||
        !has_issue_code(*intersect_diag.value, axiom::diag_codes::kBoolPrepCandidatesBuilt) ||
        !has_issue_code(*intersect_diag.value, axiom::diag_codes::kBoolLocalClipApplied)) {
        std::cerr << "expected local clip and prep diagnostics for overlap intersection\n";
        return 1;
    }

    if (!check_prep_warning(kernel, *intersect_overlap.value, *overlap_a.value, *overlap_b.value,
                            axiom::diag_codes::kBoolNearDegenerateWarning)) {
        std::cerr << "overlap intersection warning lost stage/entity context\n";
        return 1;
    }

    auto touching = kernel.primitives().box({10.0, 0.0, 0.0}, 10.0, 10.0, 10.0);
    if (!touching.value) return 1;
    auto degenerate = kernel.booleans().run(axiom::BooleanOp::Intersect, *overlap_a.value, *touching.value, {});
    if (!degenerate.value || !check_prep_warning(kernel, *degenerate.value, *overlap_a.value, *touching.value,
                                                axiom::diag_codes::kBoolNearDegenerateWarning)) {
        std::cerr << "touching intersection warning lost stage/entity context\n";
        return 1;
    }

    // Two separate shells enclose a bbox gap: bbox overlap has no shell-level candidate.
    auto left_shells = kernel.topology().query().shells_of_body(*outer.value);
    auto right_shells = kernel.topology().query().shells_of_body(*far.value);
    auto gap = kernel.primitives().box({40.0, 40.0, 40.0}, 2.0, 2.0, 2.0);
    if (!left_shells.value || !right_shells.value || !gap.value) return 1;
    auto shells = *left_shells.value;
    shells.insert(shells.end(), right_shells.value->begin(), right_shells.value->end());
    auto txn = kernel.topology().begin_transaction();
    auto compound = txn.create_body(shells);
    if (!compound.value || txn.commit().status != axiom::StatusCode::Ok) return 1;
    for (const auto op : {axiom::BooleanOp::Intersect, axiom::BooleanOp::Subtract}) {
        auto no_candidate = kernel.booleans().run(op, *compound.value, *gap.value, {});
        if (!no_candidate.value || !check_prep_warning(kernel, *no_candidate.value, *compound.value, *gap.value,
                                                       axiom::diag_codes::kBoolPrepNoCandidateWarning)) {
            std::cerr << "no-candidate warning lost stage/entity context\n";
            return 1;
        }
    }

    axiom::BooleanOptions quiet;
    quiet.diagnostics = false;
    auto quiet_union = kernel.booleans().run(axiom::BooleanOp::Union, *outer.value, *far.value, quiet);
    if (!quiet_union.value || quiet_union.diagnostic_id.value != 0 ||
        quiet_union.value->diagnostic_id.value != 0 ||
        !has_warning_code(quiet_union.value->warnings, axiom::diag_codes::kBoolNearDegenerateWarning)) {
        std::cerr << "disabling diagnostics changed returned warning contract\n";
        return 1;
    }

    std::filesystem::remove(prep_path);
    return 0;
}
