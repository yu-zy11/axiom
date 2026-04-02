#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

int main() {
    axiom::Kernel kernel;

    auto a = kernel.primitives().box({0.0, 0.0, 0.0}, 100.0, 80.0, 30.0);
    auto b = kernel.primitives().cylinder({20.0, 20.0, 0.0}, {0.0, 0.0, 1.0}, 10.0, 30.0);
    if (a.status != axiom::StatusCode::Ok || b.status != axiom::StatusCode::Ok ||
        !a.value.has_value() || !b.value.has_value()) {
        std::cerr << "failed to create boolean inputs\n";
        return 1;
    }

    axiom::BooleanOptions options;
    options.tolerance = kernel.tolerance().global_policy();
    options.diagnostics = true;
    options.auto_repair = true;

    auto result = kernel.booleans().run(axiom::BooleanOp::Subtract, *a.value, *b.value, options);
    if (result.status != axiom::StatusCode::Ok || !result.value.has_value()) {
        std::cerr << "boolean run failed\n";
        return 1;
    }

    auto bool_diag = kernel.diagnostics().get(result.value->diagnostic_id);
    bool found_stage_summary = false;
    bool found_candidates_stage = false;
    bool found_face_candidates = false;
    bool found_intersection_curves = false;
    bool found_intersection_segments = false;
    bool found_intersection_stored = false;
    bool found_imprint_applied = false;
    bool found_imprint_segment_applied = false;
    bool found_split_stage = false;
    bool found_classify_stage = false;
    bool found_rebuild_stage = false;
    bool found_validate_stage = false;
    bool found_repair_stage = false;
    bool found_classification = false;
    bool found_rebuild = false;
    bool found_output_stage = false;
    if (bool_diag.status == axiom::StatusCode::Ok && bool_diag.value.has_value()) {
        for (const auto& issue : bool_diag.value->issues) {
            if (issue.code == axiom::diag_codes::kBoolStageCandidates) {
                found_candidates_stage = true;
            }
            if (issue.code == axiom::diag_codes::kBoolFaceCandidatesBuilt) {
                found_face_candidates = true;
            }
            if (issue.code == axiom::diag_codes::kBoolIntersectionCurvesBuilt) {
                found_intersection_curves = true;
            }
            if (issue.code == axiom::diag_codes::kBoolIntersectionSegmentsBuilt) {
                found_intersection_segments = true;
            }
            if (issue.code == axiom::diag_codes::kBoolIntersectionWiresStored) {
                found_intersection_stored = true;
            }
            if (issue.code == axiom::diag_codes::kBoolStageSplit) {
                found_split_stage = true;
            }
            if (issue.code == axiom::diag_codes::kBoolImprintApplied) {
                found_imprint_applied = true;
            }
            if (issue.code == axiom::diag_codes::kBoolImprintSegmentApplied) {
                found_imprint_segment_applied = true;
            }
            if (issue.code == axiom::diag_codes::kBoolStageClassify) {
                found_classify_stage = true;
            }
            if (issue.code == axiom::diag_codes::kBoolClassificationCompleted) {
                found_classification = true;
            }
            if (issue.code == axiom::diag_codes::kBoolStageRebuild) {
                found_rebuild_stage = true;
            }
            if (issue.code == axiom::diag_codes::kBoolStageValidate) {
                found_validate_stage = true;
            }
            if (issue.code == axiom::diag_codes::kBoolStageRepair) {
                found_repair_stage = true;
            }
            if (issue.code == axiom::diag_codes::kBoolRebuildCompleted) {
                found_rebuild = true;
            }
            if (issue.code == axiom::diag_codes::kBoolRunStageSummary) {
                found_stage_summary = true;
            }
            if (issue.code == axiom::diag_codes::kBoolStageOutputMaterialized) {
                found_output_stage = true;
            }
            if (issue.code == axiom::diag_codes::kBoolStageValidate && issue.stage != "bool.validate") {
                std::cerr << "expected Issue.stage bool.validate on boolean validate stage diagnostic\n";
                return 1;
            }
        }
    }
    if (!found_candidates_stage || !found_face_candidates || !found_intersection_curves || !found_intersection_segments ||
        !found_intersection_stored || !found_split_stage || !(found_imprint_segment_applied || found_imprint_applied) ||
        !found_classify_stage || !found_classification || !found_rebuild_stage || !found_validate_stage ||
        !found_rebuild || !found_stage_summary || !found_output_stage) {
        std::cerr << "expected boolean stage diagnostics (AXM-BOOL-D-0001/0004/0005/0006/0007/0008/0009 plus imprint)\n";
        return 1;
    }

    {
        const auto json_path = std::filesystem::temp_directory_path() / "axiom_boolean_diag_stage.json";
        auto exp = kernel.diagnostics().export_report_json(result.value->diagnostic_id, json_path.string());
        if (exp.status != axiom::StatusCode::Ok) {
            std::cerr << "boolean diagnostic json export failed\n";
            return 1;
        }
        std::ifstream in {json_path};
        const std::string json {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
        std::filesystem::remove(json_path);
        if (json.find("\"stage\":\"bool.validate\"") == std::string::npos ||
            json.find("\"stage\":\"bool.prep\"") == std::string::npos) {
            std::cerr << "expected workflow stage fields in exported boolean diagnostic json\n";
            return 1;
        }
    }

    auto valid = kernel.validate().validate_all(result.value->output, axiom::ValidationMode::Standard);
    auto strict_valid = kernel.validate().validate_topology(result.value->output, axiom::ValidationMode::Strict);
    auto owned_shells = kernel.topology().query().shells_of_body(result.value->output);
    std::vector<axiom::FaceId> owned_faces;
    if (owned_shells.status == axiom::StatusCode::Ok && owned_shells.value.has_value() && owned_shells.value->size() == 1) {
        auto shell_faces = kernel.topology().query().faces_of_shell(owned_shells.value->front());
        if (shell_faces.status == axiom::StatusCode::Ok && shell_faces.value.has_value()) {
            owned_faces = *shell_faces.value;
        }
    }
    if (valid.status != axiom::StatusCode::Ok ||
        strict_valid.status != axiom::StatusCode::Ok ||
        owned_shells.status != axiom::StatusCode::Ok || !owned_shells.value.has_value() ||
        owned_shells.value->size() != 1 ||
        owned_faces.size() != 7) {
        std::cerr << "boolean output validation failed\n";
        std::cerr << "  validate_all status=" << static_cast<int>(valid.status) << "\n";
        std::cerr << "  validate_topology(strict) status=" << static_cast<int>(strict_valid.status) << "\n";
        std::cerr << "  shells_of_body status=" << static_cast<int>(owned_shells.status)
                  << " has_value=" << (owned_shells.value.has_value() ? "true" : "false") << "\n";
        if (owned_shells.value.has_value()) {
            std::cerr << "  owned_shells size=" << owned_shells.value->size() << "\n";
        }
        std::cerr << "  owned_faces size=" << owned_faces.size() << "\n";
        if (strict_valid.diagnostic_id.value != 0) {
            auto diag = kernel.diagnostics().get(strict_valid.diagnostic_id);
            if (diag.status == axiom::StatusCode::Ok && diag.value.has_value()) {
                if (!diag.value->issues.empty()) {
                    std::cerr << "  strict_topology.issue0=" << diag.value->issues.front().code << "\n";
                } else {
                    std::cerr << "  strict_topology.no_issues summary=" << diag.value->summary << "\n";
                }
            }
        }
        return 1;
    }

    auto props = kernel.query().mass_properties(result.value->output);
    if (props.status != axiom::StatusCode::Ok || !props.value.has_value() || props.value->volume <= 0.0) {
        std::cerr << "invalid boolean output mass properties\n";
        return 1;
    }

    auto source_bodies = kernel.topology().query().source_bodies_of_body(result.value->output);
    auto source_faces = kernel.topology().query().source_faces_of_body(result.value->output);
    if (source_bodies.status != axiom::StatusCode::Ok || !source_bodies.value.has_value() ||
        source_bodies.value->size() != 2 ||
        source_faces.status != axiom::StatusCode::Ok || !source_faces.value.has_value() ||
        !source_faces.value->empty()) {
        std::cerr << "boolean provenance query failed\n";
        return 1;
    }

    const bool has_a = source_bodies.value->at(0).value == a.value->value || source_bodies.value->at(1).value == a.value->value;
    const bool has_b = source_bodies.value->at(0).value == b.value->value || source_bodies.value->at(1).value == b.value->value;
    if (!has_a || !has_b) {
        std::cerr << "boolean provenance does not include both source bodies\n";
        return 1;
    }

    auto disjoint_a = kernel.primitives().box({0.0, 0.0, 0.0}, 5.0, 5.0, 5.0);
    auto disjoint_b = kernel.primitives().box({20.0, 20.0, 20.0}, 3.0, 3.0, 3.0);
    if (disjoint_a.status != axiom::StatusCode::Ok || disjoint_b.status != axiom::StatusCode::Ok ||
        !disjoint_a.value.has_value() || !disjoint_b.value.has_value()) {
        std::cerr << "failed to create disjoint boolean inputs\n";
        return 1;
    }

    axiom::BooleanOptions silent_options;
    silent_options.diagnostics = false;
    auto silent_union = kernel.booleans().run(axiom::BooleanOp::Union, *disjoint_a.value, *disjoint_b.value, silent_options);
    if (silent_union.status != axiom::StatusCode::Ok || !silent_union.value.has_value() ||
        silent_union.value->diagnostic_id.value != 0 || silent_union.diagnostic_id.value != 0 ||
        silent_union.value->warnings.empty()) {
        std::cerr << "boolean diagnostics option did not suppress success diagnostics as expected\n";
        return 1;
    }

    // Intersect 里程碑：重叠体在开启诊断时走求交/imprint 链；owned 拓扑可含多壳（来源面局部物化），
    // 总面数须 > 6（非单壳纯 bbox 六面体占位）；Strict 须通过。
    {
        auto bx = kernel.primitives().box({0.0, 0.0, 0.0}, 100.0, 80.0, 30.0);
        auto cy = kernel.primitives().cylinder({20.0, 20.0, 0.0}, {0.0, 0.0, 1.0}, 10.0, 30.0);
        if (bx.status != axiom::StatusCode::Ok || cy.status != axiom::StatusCode::Ok ||
            !bx.value.has_value() || !cy.value.has_value()) {
            std::cerr << "failed to create boolean intersect inputs\n";
            return 1;
        }
        axiom::BooleanOptions ix_opts;
        ix_opts.diagnostics = true;
        ix_opts.tolerance = kernel.tolerance().global_policy();
        ix_opts.auto_repair = true;
        auto ix = kernel.booleans().run(axiom::BooleanOp::Intersect, *bx.value, *cy.value, ix_opts);
        if (ix.status != axiom::StatusCode::Ok || !ix.value.has_value()) {
            std::cerr << "boolean intersect run failed\n";
            return 1;
        }
        auto ix_faces_all = kernel.topology().query().faces_of_body(ix.value->output);
        auto ix_strict = kernel.validate().validate_topology(ix.value->output, axiom::ValidationMode::Strict);
        auto ix_valid = kernel.validate().validate_all(ix.value->output, axiom::ValidationMode::Standard);
        if (ix_faces_all.status != axiom::StatusCode::Ok || !ix_faces_all.value.has_value() ||
            ix_strict.status != axiom::StatusCode::Ok || ix_valid.status != axiom::StatusCode::Ok ||
            ix_faces_all.value->size() < 7) {
            std::cerr << "boolean intersect expected non-bbox owned topology (>=7 faces total) and strict/standard ok\n";
            std::cerr << "  faces status=" << static_cast<int>(ix_faces_all.status)
                      << " face_count=" << (ix_faces_all.value.has_value() ? ix_faces_all.value->size() : 0U) << "\n";
            std::cerr << "  strict status=" << static_cast<int>(ix_strict.status)
                      << " validate_all status=" << static_cast<int>(ix_valid.status) << "\n";
            return 1;
        }
        auto ix_diag = kernel.diagnostics().get(ix.value->diagnostic_id);
        bool ix_imprint = false;
        if (ix_diag.status == axiom::StatusCode::Ok && ix_diag.value.has_value()) {
            for (const auto& issue : ix_diag.value->issues) {
                if (issue.code == axiom::diag_codes::kBoolImprintApplied ||
                    issue.code == axiom::diag_codes::kBoolImprintSegmentApplied) {
                    ix_imprint = true;
                    break;
                }
            }
        }
        if (!ix_imprint) {
            std::cerr << "boolean intersect expected imprint stage diagnostic\n";
            return 1;
        }
        auto ix_props = kernel.query().mass_properties(ix.value->output);
        if (ix_props.status != axiom::StatusCode::Ok || !ix_props.value.has_value() || ix_props.value->volume <= 0.0) {
            std::cerr << "boolean intersect output mass properties invalid\n";
            return 1;
        }
    }

    // Union 里程碑：重叠并集体经来源面物化可产生多壳；总 owned 面数 > 6 且 Strict 通过（非仅合并包围盒的六面体单壳）。
    {
        auto u1 = kernel.primitives().box({0.0, 0.0, 0.0}, 40.0, 40.0, 20.0);
        auto u2 = kernel.primitives().box({20.0, 20.0, 0.0}, 40.0, 40.0, 20.0);
        if (u1.status != axiom::StatusCode::Ok || u2.status != axiom::StatusCode::Ok ||
            !u1.value.has_value() || !u2.value.has_value()) {
            std::cerr << "failed to create boolean union inputs\n";
            return 1;
        }
        axiom::BooleanOptions un_opts;
        un_opts.diagnostics = true;
        un_opts.tolerance = kernel.tolerance().global_policy();
        un_opts.auto_repair = true;
        auto un = kernel.booleans().run(axiom::BooleanOp::Union, *u1.value, *u2.value, un_opts);
        if (un.status != axiom::StatusCode::Ok || !un.value.has_value()) {
            std::cerr << "boolean union run failed\n";
            return 1;
        }
        auto un_faces_all = kernel.topology().query().faces_of_body(un.value->output);
        auto un_strict = kernel.validate().validate_topology(un.value->output, axiom::ValidationMode::Strict);
        auto un_valid = kernel.validate().validate_all(un.value->output, axiom::ValidationMode::Standard);
        if (un_faces_all.status != axiom::StatusCode::Ok || !un_faces_all.value.has_value() ||
            un_strict.status != axiom::StatusCode::Ok || un_valid.status != axiom::StatusCode::Ok ||
            un_faces_all.value->size() < 7) {
            std::cerr << "boolean union expected non-bbox owned topology (>=7 faces total) and strict/standard ok\n";
            std::cerr << "  face_count=" << (un_faces_all.value.has_value() ? un_faces_all.value->size() : 0U) << "\n";
            return 1;
        }
        auto un_props = kernel.query().mass_properties(un.value->output);
        if (un_props.status != axiom::StatusCode::Ok || !un_props.value.has_value() || un_props.value->volume <= 0.0) {
            std::cerr << "boolean union output mass properties invalid\n";
            return 1;
        }
    }

    // 解析球体 rhs：分类阶段应走 sphere_point_classification（工业布尔前置链路的可观测里程碑）。
    {
        auto box_sp = kernel.primitives().box({0.0, 0.0, 0.0}, 50.0, 50.0, 50.0);
        auto sph = kernel.primitives().sphere({25.0, 25.0, 15.0}, 8.0);
        if (box_sp.status != axiom::StatusCode::Ok || sph.status != axiom::StatusCode::Ok ||
            !box_sp.value.has_value() || !sph.value.has_value()) {
            std::cerr << "failed to create box/sphere boolean inputs\n";
            return 1;
        }
        axiom::BooleanOptions sph_opts;
        sph_opts.diagnostics = true;
        sph_opts.tolerance = kernel.tolerance().global_policy();
        sph_opts.auto_repair = true;
        auto rsp = kernel.booleans().run(axiom::BooleanOp::Subtract, *box_sp.value, *sph.value, sph_opts);
        if (rsp.status != axiom::StatusCode::Ok || !rsp.value.has_value()) {
            std::cerr << "box minus sphere boolean failed\n";
            return 1;
        }
        auto sp_diag = kernel.diagnostics().get(rsp.value->diagnostic_id);
        bool found_sphere_cls = false;
        if (sp_diag.status == axiom::StatusCode::Ok && sp_diag.value.has_value()) {
            for (const auto& issue : sp_diag.value->issues) {
                if (issue.code == axiom::diag_codes::kBoolClassificationCompleted &&
                    issue.message.find("sphere_point_classification") != std::string::npos) {
                    found_sphere_cls = true;
                    break;
                }
            }
        }
        if (!found_sphere_cls) {
            std::cerr << "expected sphere_point_classification in boolean classification diagnostic\n";
            return 1;
        }
    }

    return 0;
}
