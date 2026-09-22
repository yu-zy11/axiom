#include <algorithm>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <limits>

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

// Export is read-only: failures may add diagnostics, but must not mutate the model.
bool check_prep_export_failures() {
    axiom::Kernel kernel;
    const auto lhs = kernel.primitives().box({0.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
    const auto overlap = kernel.primitives().box({1.0, 1.0, 1.0}, 2.0, 2.0, 2.0);
    const auto touching = kernel.primitives().box({2.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
    const auto far = kernel.primitives().box({10.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
    if (!lhs.value || !overlap.value || !touching.value || !far.value) return false;
    const auto bodies = kernel.body_count().value;
    const auto geometry = kernel.geometry_count().value;
    const auto topology = kernel.topology_count().value;
    const auto eval = kernel.eval_graph_metrics().value;
    if (!bodies || !geometry || !topology || !eval) return false;
    const auto root = std::filesystem::temp_directory_path() / "axiom_boolean_prep_export_test";
    std::filesystem::create_directory(root);
    const auto path = root / "stats.json";
    const auto diagnostic_path = root / "failure.json";
    const auto read_file = [](const std::filesystem::path& file) {
        std::ifstream in {file};
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    const auto unchanged = [&] {
        const auto after = kernel.eval_graph_metrics().value;
        return kernel.body_count().value == bodies && kernel.geometry_count().value == geometry &&
               kernel.topology_count().value == topology && after &&
               after->invalidation_bridge.for_bodies_batches == eval->invalidation_bridge.for_bodies_batches;
    };
    const auto check_failure = [&](axiom::BodyId a, axiom::BodyId b, const std::string& target,
                                   axiom::StatusCode status, std::string_view code, std::string_view stage) {
        const auto result = kernel.booleans().export_boolean_prep_stats(a, b, target);
        if (result.status != status || result.diagnostic_id.value == 0 || !unchanged()) return false;
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        if (!report.value || report.value->issues.size() != 1) return false;
        const auto& issue = report.value->issues.front();
        if (issue.code != code || issue.stage != stage || issue.severity != axiom::IssueSeverity::Error ||
            issue.related_entities != std::vector<std::uint64_t>{a.value, b.value}) return false;
        const auto stages = kernel.diagnostics().find_by_issue_stage(stage, 1000);
        const auto codes = kernel.diagnostics().find_by_issue_code(code, 1000);
        for (const auto* ids : {&stages, &codes}) {
            if (!ids->value || std::none_of(ids->value->begin(), ids->value->end(),
                [&](auto id) { return id.value == result.diagnostic_id.value; })) return false;
        }
        if (kernel.diagnostics().export_report_json(result.diagnostic_id, diagnostic_path.string()).status !=
            axiom::StatusCode::Ok) return false;
        const auto json = read_file(diagnostic_path);
        return json.find("\"code\":\"" + std::string(code) + "\"") != std::string::npos &&
               json.find("\"stage\":\"" + std::string(stage) + "\"") != std::string::npos &&
               json.find("\"related_entities\":[" + std::to_string(a.value) + "," +
                         std::to_string(b.value) + "]") != std::string::npos;
    };
    // Reject either invalid handle before opening/truncating an existing file.
    { std::ofstream out {path}; out << "preserve existing file"; }
    for (const auto invalid : {axiom::BodyId{}, axiom::BodyId{std::numeric_limits<std::uint64_t>::max()}}) {
        for (const bool invalid_lhs : {false, true}) {
            if (!check_failure(invalid_lhs ? invalid : *lhs.value, invalid_lhs ? *overlap.value : invalid,
                               path.string(), axiom::StatusCode::InvalidInput,
                               axiom::diag_codes::kBoolInvalidInput, "bool.prep.export.input") ||
                read_file(path) != "preserve existing file") return false;
        }
    }
    const auto absent = root / "absent.json";
    if (!check_failure({}, {}, absent.string(), axiom::StatusCode::InvalidInput,
                       axiom::diag_codes::kBoolInvalidInput, "bool.prep.export.input") ||
        std::filesystem::exists(absent)) return false;
    if (!check_failure(*lhs.value, *overlap.value, "", axiom::StatusCode::InvalidInput,
                       axiom::diag_codes::kBoolInvalidInput, "bool.prep.export.input")) return false;
    // A directory is a deterministic open failure even when the test runs as root.
    if (!check_failure(*lhs.value, *overlap.value, root.string(), axiom::StatusCode::OperationFailed,
                       axiom::diag_codes::kIoExportFailure, "bool.prep.export.open")) return false;
#ifdef __linux__
    // Small buffered output can fail only on close; it must not be reported as success.
    if (!check_failure(*lhs.value, *overlap.value, "/dev/full", axiom::StatusCode::OperationFailed,
                       axiom::diag_codes::kIoExportFailure, "bool.prep.export.write")) return false;
#endif
    // Retrying after rejection must export ordinary, touching, disjoint and identical inputs.
    for (const auto rhs : {*overlap.value, *touching.value, *far.value, *lhs.value}) {
        const auto result = kernel.booleans().export_boolean_prep_stats(*lhs.value, rhs, path.string());
        if (result.status != axiom::StatusCode::Ok || result.diagnostic_id.value == 0 || !unchanged()) return false;
        const auto json = read_file(path);
        if (json.empty() || json.front() != '{' || json.back() != '}' ||
            json.find("\"lhs_regions\":1") == std::string::npos ||
            json.find("\"rhs_regions\":1") == std::string::npos) return false;
        const bool has_overlap = rhs.value != far.value->value;
        if (json.find(has_overlap ? "\"local_clip_applied\":true" : "\"local_clip_applied\":false") ==
            std::string::npos) return false;
        if ((rhs.value == far.value->value || rhs.value == touching.value->value) &&
            json.find("\"overlap_volume_sum\":0") == std::string::npos) return false;
    }
    std::filesystem::remove(path);
    std::filesystem::remove(diagnostic_path);
    std::filesystem::remove(root);
    return true;
}

}  // namespace

int main() {
    if (!check_prep_export_failures()) {
        std::cerr << "boolean prep export failure contract regression\n";
        return 1;
    }
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
    const auto eval_before = kernel.eval_graph_metrics();
    if (!eval_before.value) return 1;
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

    // Disabling verbose diagnostics must not erase failure stage or input context.
    struct FailureCase {
        axiom::BooleanOp op;
        axiom::BodyId lhs;
        axiom::BodyId rhs;
        axiom::StatusCode status;
        std::string_view code;
        std::string_view stage;
    };
    const FailureCase failures[] = {
        {static_cast<axiom::BooleanOp>(-1), *outer.value, *inner.value, axiom::StatusCode::InvalidInput,
         axiom::diag_codes::kBoolInvalidInput, "bool.input"},
        {static_cast<axiom::BooleanOp>(4), *outer.value, *far.value, axiom::StatusCode::InvalidInput,
         axiom::diag_codes::kBoolInvalidInput, "bool.input"},
        {static_cast<axiom::BooleanOp>(std::numeric_limits<int>::max()), *outer.value, *outer.value,
         axiom::StatusCode::InvalidInput, axiom::diag_codes::kBoolInvalidInput, "bool.input"},
        {static_cast<axiom::BooleanOp>(-1), {}, {}, axiom::StatusCode::InvalidInput,
         axiom::diag_codes::kBoolInvalidInput, "bool.input"},
        {axiom::BooleanOp::Union, {}, *outer.value, axiom::StatusCode::InvalidInput,
         axiom::diag_codes::kBoolInvalidInput, "bool.input"},
        {axiom::BooleanOp::Union, *outer.value, {}, axiom::StatusCode::InvalidInput,
         axiom::diag_codes::kBoolInvalidInput, "bool.input"},
        {axiom::BooleanOp::Union, {}, {}, axiom::StatusCode::InvalidInput,
         axiom::diag_codes::kBoolInvalidInput, "bool.input"},
        {axiom::BooleanOp::Intersect, *outer.value, *far.value, axiom::StatusCode::OperationFailed,
         axiom::diag_codes::kBoolIntersectionFailure, "bool.abort.intersect"},
        {axiom::BooleanOp::Subtract, *inner.value, *outer.value, axiom::StatusCode::OperationFailed,
         axiom::diag_codes::kBoolClassificationFailure, "bool.abort.classify"},
    };
    for (const bool diagnostics : {false, true}) {
        axiom::BooleanOptions options;
        options.diagnostics = diagnostics;
        for (const auto& test : failures) {
            const auto failed = kernel.booleans().run(test.op, test.lhs, test.rhs, options);
            const auto report = kernel.diagnostics().get(failed.diagnostic_id);
            const auto eval_after = kernel.eval_graph_metrics();
            if (failed.status != test.status || failed.value || failed.diagnostic_id.value == 0 ||
                !report.value || (!diagnostics && report.value->issues.size() != 1) ||
                !eval_after.value || eval_after.value->invalidation_bridge.for_bodies_batches !=
                    eval_before.value->invalidation_bridge.for_bodies_batches) {
                std::cerr << "missing minimal boolean failure report\n";
                return 1;
            }
            const auto* issue = find_issue(*report.value, test.code);
            if (issue == nullptr || issue->severity != axiom::IssueSeverity::Error || issue->stage != test.stage ||
                issue->related_entities != std::vector<std::uint64_t>{test.lhs.value, test.rhs.value}) {
                std::cerr << "boolean failure lost stage or input context\n";
                return 1;
            }
            const auto ids = kernel.diagnostics().find_by_issue_stage(test.stage, 1000);
            const auto codes = kernel.diagnostics().find_by_issue_code(test.code, 1000);
            for (const auto* found : {&ids, &codes}) {
                if (!found->value || std::none_of(found->value->begin(), found->value->end(),
                    [&](auto id) { return id.value == failed.diagnostic_id.value; })) {
                    std::cerr << "boolean failure is not searchable\n";
                    return 1;
                }
            }
            const auto path = std::filesystem::temp_directory_path() / "axiom_boolean_early_failure.json";
            if (kernel.diagnostics().export_report_json(failed.diagnostic_id, path.string()).status !=
                axiom::StatusCode::Ok) return 1;
            std::ifstream in {path};
            const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            std::filesystem::remove(path);
            if (json.find("\"stage\":\"" + std::string(test.stage) + "\"") == std::string::npos ||
                json.find("\"code\":\"" + std::string(test.code) + "\"") == std::string::npos ||
                json.find("\"related_entities\":[" + std::to_string(test.lhs.value) + "," +
                          std::to_string(test.rhs.value) + "]") == std::string::npos ||
                kernel.body_count().value != bodies_before.value ||
                kernel.geometry_count().value != geometry_before.value ||
                kernel.topology_count().value != topology_before.value) {
                std::cerr << "boolean failure export mismatch or model pollution\n";
                return 1;
            }
        }
    }

    // The input gate must continue to accept all four declared operations after rejection.
    // These are the existing limited boolean semantics, not evidence of exact B-Rep results.
    for (const bool diagnostics : {false, true}) {
        axiom::BooleanOptions options;
        options.diagnostics = diagnostics;
        for (const auto op : {axiom::BooleanOp::Union, axiom::BooleanOp::Subtract,
                              axiom::BooleanOp::Intersect, axiom::BooleanOp::Split}) {
            const auto valid = kernel.booleans().run(op, *outer.value, *inner.value, options);
            if (valid.status != axiom::StatusCode::Ok || !valid.value || valid.value->output.value == 0) {
                std::cerr << "valid boolean operation rejected after invalid input\n";
                return 1;
            }
        }
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
