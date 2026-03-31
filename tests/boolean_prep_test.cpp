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

bool has_warning_code(const std::vector<axiom::Warning>& warnings, std::string_view code) {
    for (const auto& warning : warnings) {
        if (warning.code == code) {
            return true;
        }
    }
    return false;
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

    std::filesystem::remove(prep_path);
    return 0;
}
