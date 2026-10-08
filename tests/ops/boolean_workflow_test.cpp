#include <array>
#include <cmath>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

namespace {

// The compatibility workflow consumes polygon-trimmed wires. Count/topology
// checks alone cannot establish that its final BooleanResult is a material solid.
bool check_geometric_preparation_workflow() {
    axiom::Kernel kernel;
    const auto a = kernel.primitives().box({0,0,0},2,2,2);
    const auto b = kernel.primitives().box({1,1,1},2,2,2);
    const auto wedge = kernel.primitives().wedge({0,0,0},2,2,2);
    const auto gap = kernel.primitives().box({1.2,1.2,0.25},0.4,0.4,0.5);
    if (!a.value || !b.value || !wedge.value || !gap.value) return false;
    const auto real = kernel.booleans().prepare_intersections(*a.value,*b.value);
    const auto separated = kernel.booleans().prepare_intersections(*wedge.value,*gap.value);
    if (!real.value || real.value->segments.size() != 6 || !separated.value ||
        separated.value->candidates.empty() || !separated.value->segments.empty()) return false;
    for (const bool phantom : {false,true}) {
        const auto before = kernel.intersection_count();
        const auto result = kernel.booleans().run(axiom::BooleanOp::Union,
            phantom ? *wedge.value : *a.value,phantom ? *gap.value : *b.value,{});
        const auto after = kernel.intersection_count();
        if (!result.value || !before.value || !after.value ||
            *after.value != *before.value+(phantom ? 0 : 1)) return false;
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        if (!diagnostic.value) return false;
        bool found_trim = false, found_store = false;
        for (const auto& issue : diagnostic.value->issues) {
            if (issue.code == axiom::diag_codes::kBoolIntersectionWiresStored) found_store = true;
            if (issue.code == axiom::diag_codes::kBoolIntersectionSegmentsBuilt) {
                if (issue.stage != "bool.intersect.trim") return false;
                for (const auto& evidence : issue.numeric_evidence)
                    if (evidence.name == "segments" && evidence.value == (phantom ? 0 : 6)) found_trim = true;
            }
        }
        if (!found_trim || found_store == phantom) return false;
    }
    return true;
}

// Invalid real face boundaries must fail before compatibility run creates an
// output body or intersection geometry, including while the caller owns a writer.
bool check_trim_failure_isolation() {
    for (const int model : {0,1,2}) for (const bool active_writer : {false,true}) {
        axiom::Kernel kernel;
        const double offset = model == 2 ? 1e12 : 0;
        const auto a = kernel.primitives().box({offset,offset,offset},2,2,2);
        // This compatibility input is a real planar face in a Generic shell.
        // Explicit topology avoids primitive bbox expansion of small extents.
        auto fixture = kernel.topology().begin_transaction();
        std::vector<axiom::Point3> points {{1,1,1},{1+5e-7,1,1},{2,1,1},{2,2,1},{1,2,1}};
        if (model == 1) {
            points.clear();
            for (std::size_t i = 0; i < 257; ++i) {
                const double angle = 2*std::acos(-1.0)*i/257;
                points.push_back({1+2*std::cos(angle),1+2*std::sin(angle),1});
            }
        } else if (model == 2) {
            points = {{offset+1,offset+1,offset+1},{offset+2,offset+1,offset+1},
                      {offset+2,offset+2,offset+1},{offset+1,offset+2,offset+1}};
        }
        std::vector<axiom::VertexId> vertices(points.size());
        std::vector<axiom::CoedgeId> coedges;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto vertex = fixture.create_vertex(points[i]);
            if (!vertex.value) return false;
            vertices[i] = *vertex.value;
        }
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto j = (i+1)%points.size();
            const auto p = points[i], q = points[j];
            const double length = std::hypot(q.x-p.x,q.y-p.y,q.z-p.z);
            const auto curve = kernel.curves().make_line(p,{(q.x-p.x)/length,(q.y-p.y)/length,(q.z-p.z)/length});
            const auto edge = curve.value ? fixture.create_edge(*curve.value,vertices[i],vertices[j])
                                          : axiom::Result<axiom::EdgeId>{};
            const auto coedge = edge.value ? fixture.create_coedge(*edge.value,false) : axiom::Result<axiom::CoedgeId>{};
            if (!coedge.value) return false;
            coedges.push_back(*coedge.value);
        }
        const auto plane = kernel.surfaces().make_plane({offset+1,offset+1,offset+1},{0,0,1});
        const auto loop = fixture.create_loop(coedges);
        const auto face = plane.value && loop.value ? fixture.create_face(*plane.value,*loop.value,{})
                                                    : axiom::Result<axiom::FaceId>{};
        const auto shell = face.value ? fixture.create_shell(std::array{*face.value}) : axiom::Result<axiom::ShellId>{};
        const auto thin = shell.value ? fixture.create_body(std::array{*shell.value}) : axiom::Result<axiom::BodyId>{};
        if (!thin.value || fixture.commit().status != axiom::StatusCode::Ok) return false;
        if (!a.value || !thin.value) return false;
        const auto query = kernel.topology().query();
        const auto source_edges = query.edges_of_body(*thin.value);
        if (!source_edges.value || source_edges.value->size() != points.size()) return false;
        if (model == 0) {
            bool short_reference = false;
            for (const auto edge : *source_edges.value) {
                const auto length = query.edge_length(edge);
                if (!length.value) return false;
                if (std::abs(*length.value-5e-7) < 1e-14) short_reference = true;
            }
            if (!short_reference) return false;
        }
        const auto counts = [&] {
            return std::array{kernel.body_count().value,kernel.geometry_count().value,kernel.topology_count().value,
                              kernel.intersection_count().value,kernel.eval_node_count().value,kernel.cache_entry_count().value};
        };
        auto transaction = kernel.topology().begin_transaction();
        if (!active_writer && transaction.rollback().status != axiom::StatusCode::Ok) return false;
        const auto sentinel = active_writer ? transaction.create_vertex({99,98,97}) : axiom::Result<axiom::VertexId>{};
        if (active_writer && !sentinel.value) return false;
        const auto writes = active_writer ? transaction.write_operation_count().value : std::optional<std::uint64_t>{};
        const auto bridge = [&] {
            const auto metrics = kernel.eval_graph_metrics();
            if (!metrics.value) return std::array<std::uint64_t,5>{};
            const auto& item = metrics.value->invalidation_bridge;
            return std::array{item.for_body_entries,item.for_faces_entries,item.for_bodies_batches,
                              item.for_bodies_list_size_total,item.downstream_invalidation_steps};
        };
        const auto topology = [&] {
            std::pair<std::vector<std::uint64_t>,std::vector<double>> snapshot;
            for (const auto body : {*a.value,*thin.value}) {
                const auto faces = query.faces_of_body(body);
                if (!faces.value) return decltype(snapshot){};
                snapshot.first.push_back(body.value);
                for (const auto face : *faces.value) {
                    const auto loops = query.loops_of_face(face);
                    const auto bbox = query.bbox_of_face(face);
                    if (!loops.value || !bbox.value || !bbox.value->is_valid) return decltype(snapshot){};
                    const auto& box = *bbox.value;
                    snapshot.first.insert(snapshot.first.end(),{face.value,loops.value->size()});
                    snapshot.second.insert(snapshot.second.end(),{box.min.x,box.min.y,box.min.z,box.max.x,box.max.y,box.max.z});
                    for (const auto loop : *loops.value) {
                        const auto edges = query.edges_of_loop(loop);
                        if (!edges.value) return decltype(snapshot){};
                        snapshot.first.insert(snapshot.first.end(),{loop.value,edges.value->size()});
                        for (const auto edge : *edges.value) {
                            const auto vertices = query.vertices_of_edge(edge);
                            if (!vertices.value) return decltype(snapshot){};
                            snapshot.first.insert(snapshot.first.end(),{edge.value,(*vertices.value)[0].value,(*vertices.value)[1].value});
                        }
                    }
                }
            }
            return snapshot;
        };
        const auto topology_baseline = topology();
        if (topology_baseline.first.empty()) return false;
        const auto bridge_baseline = bridge();
        const auto baseline = counts();
        const auto result = kernel.booleans().run(axiom::BooleanOp::Union,*a.value,*thin.value,{});
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto active = kernel.topology().has_active_write_transaction();
        const auto expected_status = model == 0 ? axiom::StatusCode::DegenerateGeometry :
                                     model == 1 ? axiom::StatusCode::OperationFailed : axiom::StatusCode::NumericalInstability;
        const auto expected_code = model == 0 ? axiom::diag_codes::kBoolInvalidInput :
                                   model == 1 ? axiom::diag_codes::kBoolPreparationBudgetExceeded : axiom::diag_codes::kBoolNumericalFailure;
        bool found = false;
        if (diagnostic.value) for (const auto& issue : diagnostic.value->issues)
            if (issue.code == expected_code && issue.stage == "bool.intersect.trim" &&
                issue.severity == axiom::IssueSeverity::Error && !issue.numeric_evidence.empty() &&
                issue.related_entities.size() >= 2 && issue.related_entities[0] == a.value->value &&
                issue.related_entities[1] == thin.value->value) found = true;
        if (result.status != expected_status || result.value || result.diagnostic_id.value == 0 || !found || counts() != baseline ||
            !active.value || *active.value != active_writer || bridge() != bridge_baseline || topology() != topology_baseline ||
            (active_writer && (transaction.write_operation_count().value != writes ||
                              !transaction.has_created_vertex(*sentinel.value).value.value_or(false)))) {
            std::cerr << "compatibility trim failure: model=" << model << " writer=" << active_writer
                      << " status=" << static_cast<int>(result.status) << " value=" << bool(result.value)
                      << " unchanged=" << (counts() == baseline) << "\n";
            if (diagnostic.value) for (const auto& issue : diagnostic.value->issues)
                std::cerr << "issue code=" << issue.code << " stage=" << issue.stage << "\n";
            return false;
        }
        if (active_writer && (!transaction.create_vertex({96,95,94}).value ||
            transaction.rollback().status != axiom::StatusCode::Ok)) return false;
    }
    return true;
}

}  // namespace

int main() {
    if (!check_trim_failure_isolation()) {
        std::cerr << "boolean workflow trim failure isolation regression\n";
        return 1;
    }
    if (!check_geometric_preparation_workflow()) {
        std::cerr << "boolean workflow geometric wire regression\n";
        return 1;
    }
    axiom::Kernel kernel;
    const auto unsupported_mass = [&](axiom::BodyId body) {
        const auto result = kernel.query().mass_properties(body);
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        if (result.status != axiom::StatusCode::NotImplemented || result.value || !diagnostic.value) return false;
        for (const auto& issue : diagnostic.value->issues)
            if (issue.code == axiom::diag_codes::kCoreOperationUnsupported &&
                issue.stage == "query.mass_properties.support_gate") return true;
        return false;
    };
    const auto proxy_survives_reownership = [&](axiom::BodyId body) {
        const auto shells = kernel.topology().query().shells_of_body(body);
        if (!shells.value || shells.value->empty()) return false;
        for (const auto shell : *shells.value) {
            const auto faces = kernel.topology().query().faces_of_shell(shell);
            if (!faces.value) return false;
            auto txn = kernel.topology().begin_transaction();
            const auto copied_shell = txn.create_shell(*faces.value);
            const auto generic = copied_shell.value ? txn.create_body(std::array{*copied_shell.value})
                : axiom::Result<axiom::BodyId>{};
            // The new body's kind/source metadata cannot launder imprinted
            // proxy faces into material boundaries, even after deleting owners.
            if (!generic.value || !unsupported_mass(*generic.value) ||
                txn.delete_body(body).status != axiom::StatusCode::Ok ||
                txn.delete_shell(shell).status != axiom::StatusCode::Ok ||
                !unsupported_mass(*generic.value) || txn.rollback().status != axiom::StatusCode::Ok) return false;
        }
        return true;
    };

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

    // Historical BooleanResult provenance/topology is not a certificate of the
    // physical Boolean solid. Mass must not restore operand or bbox estimates.
    if (!unsupported_mass(result.value->output) || !proxy_survives_reownership(result.value->output)) {
        std::cerr << "boolean output must reject uncertified mass without partial values\n";
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
        if (!unsupported_mass(ix.value->output) || !proxy_survives_reownership(ix.value->output)) {
            std::cerr << "boolean intersect output must reject uncertified mass\n";
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
        if (!unsupported_mass(un.value->output) || !proxy_survives_reownership(un.value->output)) {
            std::cerr << "boolean union output must reject uncertified mass\n";
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
