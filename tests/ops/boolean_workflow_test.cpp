#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
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

// A successful read-only split package and every rejected phase must preserve
// both the existing model and the caller's active writer, including provenance.
bool check_split_classification_isolation() {
    const auto failure = [](int line) {
        std::cerr << "split/classification isolation failure at line " << line << "\n";
        return false;
    };
    for (const bool active_writer : {false,true}) {
        axiom::Kernel kernel;
        const auto a = kernel.primitives().box({0,0,0},2,2,2);
        const auto b = kernel.primitives().box({1,1,1},2,2,2);
        const auto touching = kernel.primitives().box({2,0,0},2,2,2);
        const auto wedge = kernel.primitives().wedge({0,0,0},2,2,2);
        const auto tangent = kernel.primitives().box({0.75,1.25,0.25},0.5,0.5,0.5);
        const auto sphere = kernel.primitives().sphere({0,0,0},1);
        if (!a.value || !b.value || !touching.value || !wedge.value || !tangent.value || !sphere.value) {
            for (const auto* result : {&a,&b,&touching,&wedge,&tangent,&sphere}) {
                if (result->value) continue;
                std::cerr << "isolation fixture construction status=" << static_cast<int>(result->status) << "\n";
                const auto report = kernel.diagnostics().get(result->diagnostic_id);
                if (report.value) for (const auto& issue : report.value->issues)
                    std::cerr << issue.stage << " " << issue.code << " " << issue.message << "\n";
            }
            return failure(__LINE__);
        }
        const auto query = kernel.topology().query();
        const auto counts = [&] {
            return std::array{kernel.body_count().value,kernel.geometry_count().value,kernel.topology_count().value,
                              kernel.intersection_count().value,kernel.eval_node_count().value,kernel.cache_entry_count().value};
        };
        const auto bridge = [&] {
            const auto metrics = kernel.eval_graph_metrics();
            if (!metrics.value) return std::array<std::uint64_t,5>{};
            const auto& item = metrics.value->invalidation_bridge;
            return std::array{item.for_body_entries,item.for_faces_entries,item.for_bodies_batches,
                              item.for_bodies_list_size_total,item.downstream_invalidation_steps};
        };
        const auto cache_stats = [&] {
            const auto stats = kernel.tessellation_cache_stats();
            if (!stats.value) return std::array<std::uint64_t,6>{};
            const auto& item = *stats.value;
            return std::array{item.body_cache_hits,item.body_cache_misses,item.body_cache_stale_evictions,
                              item.face_cache_hits,item.face_cache_misses,item.face_cache_stale_evictions};
        };
        const auto input_snapshot = [&] {
            std::pair<std::vector<std::uint64_t>,std::vector<double>> snapshot;
            const auto check_query = [&](std::string_view label, const auto& result) {
                if (result.value) return;
                std::cerr << "input snapshot " << label << " status=" << static_cast<int>(result.status) << "\n";
                const auto report = kernel.diagnostics().get(result.diagnostic_id);
                if (report.value) for (const auto& issue : report.value->issues)
                    std::cerr << issue.stage << " " << issue.code << " " << issue.message << "\n";
            };
            for (const auto body : {*a.value,*b.value,*touching.value,*wedge.value,*tangent.value}) {
                const auto shells = query.shells_of_body(body);
                const auto faces = query.faces_of_body(body);
                const auto source_bodies = query.source_bodies_of_body(body);
                const auto source_shells = query.source_shells_of_body(body);
                const auto source_faces = query.source_faces_of_body(body);
                const auto representation = kernel.representation().kind_of_body(body);
                const auto representation_box = kernel.representation().bbox_of_body(body);
                check_query("shells",shells);
                check_query("faces",faces);
                check_query("source_bodies",source_bodies);
                check_query("source_shells",source_shells);
                check_query("source_faces",source_faces);
                check_query("representation",representation);
                check_query("representation_box",representation_box);
                if (!shells.value || !faces.value || !source_bodies.value || !source_shells.value || !source_faces.value ||
                    !representation.value || !representation_box.value || !representation_box.value->is_valid)
                    return decltype(snapshot){};
                snapshot.first.insert(snapshot.first.end(),{body.value,shells.value->size(),faces.value->size(),
                    static_cast<std::uint64_t>(*representation.value)});
                const auto& body_box = *representation_box.value;
                snapshot.second.insert(snapshot.second.end(),{body_box.min.x,body_box.min.y,body_box.min.z,
                                                              body_box.max.x,body_box.max.y,body_box.max.z});
                snapshot.first.push_back(source_faces.value->size());
                for (const auto source : *source_faces.value) snapshot.first.push_back(source.value);
                snapshot.first.push_back(source_bodies.value->size());
                for (const auto source : *source_bodies.value) snapshot.first.push_back(source.value);
                snapshot.first.push_back(source_shells.value->size());
                for (const auto source : *source_shells.value) snapshot.first.push_back(source.value);
                for (const auto shell : *shells.value) {
                    const auto shell_faces = query.faces_of_shell(shell);
                    const auto shell_sources = query.source_shells_of_shell(shell);
                    const auto shell_face_sources = query.source_faces_of_shell(shell);
                    check_query("shell_faces",shell_faces);
                    check_query("shell_sources",shell_sources);
                    check_query("shell_face_sources",shell_face_sources);
                    if (!shell_faces.value || !shell_sources.value || !shell_face_sources.value) return decltype(snapshot){};
                    snapshot.first.insert(snapshot.first.end(),{shell.value,shell_faces.value->size(),shell_sources.value->size(),
                                                               shell_face_sources.value->size()});
                    for (const auto face : *shell_faces.value) snapshot.first.push_back(face.value);
                    for (const auto source : *shell_sources.value) snapshot.first.push_back(source.value);
                    for (const auto source : *shell_face_sources.value) snapshot.first.push_back(source.value);
                }
                for (const auto face : *faces.value) {
                    const auto loops = query.loops_of_face(face);
                    const auto surface = query.surface_of_face(face);
                    const auto sources = query.source_faces_of_face(face);
                    const auto bbox = query.bbox_of_face(face);
                    const auto face_area = query.planar_face_area(face);
                    check_query("face_area",face_area);
                    if (!face_area.value) return decltype(snapshot){};
                    snapshot.second.push_back(*face_area.value);
                    check_query("loops",loops);
                    check_query("surface",surface);
                    check_query("sources",sources);
                    check_query("bbox",bbox);
                    if (!loops.value || !surface.value || !sources.value || !bbox.value || !bbox.value->is_valid)
                        return decltype(snapshot){};
                    snapshot.first.insert(snapshot.first.end(),{face.value,surface.value->value,loops.value->size(),sources.value->size()});
                    for (const auto source : *sources.value) snapshot.first.push_back(source.value);
                    const auto& box = *bbox.value;
                    snapshot.second.insert(snapshot.second.end(),{box.min.x,box.min.y,box.min.z,box.max.x,box.max.y,box.max.z});
                    for (const auto loop : *loops.value) {
                        const auto vertices = query.vertices_of_loop(loop);
                        const auto edges = query.edges_of_loop(loop);
                        check_query("vertices",vertices);
                        check_query("edges",edges);
                        if (!vertices.value || !edges.value) return decltype(snapshot){};
                        snapshot.first.insert(snapshot.first.end(),{loop.value,vertices.value->size(),edges.value->size()});
                        for (const auto vertex : *vertices.value) snapshot.first.push_back(vertex.value);
                        // Primitive faces may legitimately have no PCurve. Capture every
                        // incident binding, including zero, and retain UV coordinates when
                        // the corresponding public trim query has its complete input.
                        bool complete_pcurves = true;
                        for (const auto edge : *edges.value) {
                            const auto coedges = query.coedges_of_edge(edge);
                            check_query("coedges",coedges);
                            if (!coedges.value || coedges.value->empty()) return decltype(snapshot){};
                            auto incident_coedges = *coedges.value;
                            std::sort(incident_coedges.begin(),incident_coedges.end(),
                                      [](auto a, auto b) { return a.value < b.value; });
                            snapshot.first.push_back(incident_coedges.size());
                            for (const auto coedge : incident_coedges) {
                                const auto pcurve = query.pcurve_of_coedge(coedge);
                                check_query("pcurve",pcurve);
                                if (!pcurve.value) return decltype(snapshot){};
                                snapshot.first.insert(snapshot.first.end(),{coedge.value,pcurve.value->value});
                                complete_pcurves = complete_pcurves && pcurve.value->value != 0;
                            }
                        }
                        snapshot.first.push_back(complete_pcurves);
                        if (complete_pcurves) {
                            const auto uv = query.face_loop_uv_polyline(face,loop);
                            check_query("uv",uv);
                            if (!uv.value) return decltype(snapshot){};
                            snapshot.first.push_back(uv.value->size());
                            for (const auto point : *uv.value) {
                                snapshot.second.push_back(point.x); snapshot.second.push_back(point.y);
                            }
                        }
                        for (const auto edge : *edges.value) {
                            const auto endpoints = query.vertices_of_edge(edge);
                            const auto owners = query.faces_of_edge(edge);
                            const auto length = query.edge_length(edge);
                            check_query("endpoints",endpoints);
                            check_query("owners",owners);
                            check_query("length",length);
                            if (!endpoints.value || !owners.value || !length.value) return decltype(snapshot){};
                            snapshot.first.insert(snapshot.first.end(),{edge.value,(*endpoints.value)[0].value,
                                (*endpoints.value)[1].value,owners.value->size()});
                            auto incident_faces = *owners.value;
                            std::sort(incident_faces.begin(),incident_faces.end(),
                                      [](auto a, auto b) { return a.value < b.value; });
                            for (const auto owner : incident_faces) snapshot.first.push_back(owner.value);
                            snapshot.second.push_back(*length.value);
                        }
                    }
                }
            }
            return snapshot;
        };
        const auto committed_counts = counts();
        const auto committed_bridge = bridge();
        const auto committed_cache = cache_stats();
        const auto committed_input = input_snapshot();
        if (committed_input.first.empty()) return failure(__LINE__);
        auto transaction = kernel.topology().begin_transaction();
        if (!active_writer && transaction.rollback().status != axiom::StatusCode::Ok) return failure(__LINE__);
        const auto sentinel = active_writer ? transaction.create_vertex({99,98,97}) : axiom::Result<axiom::VertexId>{};
        if (active_writer && !sentinel.value) return failure(__LINE__);
        const auto writes = active_writer ? transaction.write_operation_count().value : std::optional<std::uint64_t>{};
        const auto baseline_counts = counts();
        const auto baseline_bridge = bridge();
        const auto baseline_cache = cache_stats();
        const auto unchanged = [&] {
            const auto active = kernel.topology().has_active_write_transaction();
            const auto current_counts = counts();
            const auto current_bridge = bridge();
            const auto current_cache = cache_stats();
            const auto current_input = input_snapshot();
            const bool count_ok = current_counts == baseline_counts, bridge_ok = current_bridge == baseline_bridge,
                       cache_ok = current_cache == baseline_cache, input_ok = current_input == committed_input,
                       active_ok = active.value && *active.value == active_writer,
                       writes_ok = !active_writer || transaction.write_operation_count().value == writes,
                       sentinel_ok = !active_writer || transaction.has_created_vertex(*sentinel.value).value.value_or(false);
            if (!count_ok || !bridge_ok || !cache_ok || !input_ok || !active_ok || !writes_ok || !sentinel_ok) {
                std::cerr << "isolation unchanged writer=" << active_writer << " counts=" << count_ok
                          << " bridge=" << bridge_ok << " cache=" << cache_ok << " input=" << input_ok
                          << " active=" << active_ok << " writes=" << writes_ok << " sentinel=" << sentinel_ok << "\n";
                const auto differences = [](std::string_view label, const auto& current, const auto& baseline) {
                    for (std::size_t i = 0; i < current.size() && i < baseline.size(); ++i)
                        if (current[i] != baseline[i])
                            std::cerr << label << "[" << i << "] baseline=" << baseline[i] << " current=" << current[i] << "\n";
                };
                for (std::size_t i = 0; i < current_counts.size(); ++i)
                    if (current_counts[i] != baseline_counts[i]) {
                        std::cerr << "counts[" << i << "] baseline=";
                        if (baseline_counts[i]) std::cerr << *baseline_counts[i];
                        else std::cerr << "missing";
                        std::cerr << " current=";
                        if (current_counts[i]) std::cerr << *current_counts[i];
                        else std::cerr << "missing";
                        std::cerr << "\n";
                    }
                differences("bridge",current_bridge,baseline_bridge);
                differences("cache",current_cache,baseline_cache);
                if (!input_ok) {
                    std::cerr << "input sizes IDs=" << current_input.first.size() << "/" << committed_input.first.size()
                              << " geometry=" << current_input.second.size() << "/" << committed_input.second.size() << "\n";
                    differences("input IDs",current_input.first,committed_input.first);
                    differences("input geometry",current_input.second,committed_input.second);
                }
            }
            return count_ok && bridge_ok && cache_ok && input_ok && active_ok && writes_ok && sentinel_ok;
        };
        if (!unchanged()) return failure(__LINE__);
        const auto print_result = [&](std::string_view label, const auto& result) {
            std::cerr << label << " status=" << static_cast<int>(result.status)
                      << " writer=" << active_writer << "\n";
            const auto report = kernel.diagnostics().get(result.diagnostic_id);
            if (report.value) for (const auto& issue : report.value->issues)
                std::cerr << issue.stage << " " << issue.code << " " << issue.message << "\n";
        };
        const auto expect_failure = [&](const auto& result, axiom::StatusCode status,
                                         std::string_view code, std::string_view stage) {
            if (result.status != status || result.value || result.diagnostic_id.value == 0 || !unchanged()) {
                print_result(stage,result);
                return failure(__LINE__);
            }
            const auto report = kernel.diagnostics().get(result.diagnostic_id);
            bool found = false;
            if (report.value) for (const auto& issue : report.value->issues)
                if (issue.code == code && issue.stage == stage && issue.severity == axiom::IssueSeverity::Error &&
                    !issue.related_entities.empty() && !issue.numeric_evidence.empty()) {
                    for (const auto& evidence : issue.numeric_evidence) if (!std::isfinite(evidence.value)) return failure(__LINE__);
                    found = true;
                }
            if (!found) {
                std::cerr << "split/classify expected code=" << code << " stage=" << stage
                          << " status=" << static_cast<int>(result.status) << "\n";
                if (report.value) for (const auto& issue : report.value->issues)
                    std::cerr << "actual code=" << issue.code << " stage=" << issue.stage << "\n";
                return failure(__LINE__);
            }
            const auto by_stage = kernel.diagnostics().find_by_issue_stage(stage,1000);
            const auto by_code = kernel.diagnostics().find_by_issue_code(code,1000);
            for (const auto* ids : {&by_stage,&by_code})
                if (!ids->value || std::find(ids->value->begin(),ids->value->end(),result.diagnostic_id) == ids->value->end())
                    return failure(__LINE__);
            const auto path = std::filesystem::temp_directory_path()/"axiom_split_classification_failure.json";
            if (kernel.diagnostics().export_report_json(result.diagnostic_id,path.string()).status != axiom::StatusCode::Ok)
                return failure(__LINE__);
            std::ifstream input {path};
            const std::string json((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
            input.close();
            std::filesystem::remove(path);
            return json.find("\"code\":\""+std::string(code)+"\"") != std::string::npos &&
                json.find("\"stage\":\""+std::string(stage)+"\"") != std::string::npos && unchanged();
        };
        axiom::BooleanSplitClassificationOptions split;
        split.max_fragments = 1;
        if (!expect_failure(kernel.booleans().prepare_split_classification(*a.value,*b.value,split),
                            axiom::StatusCode::OperationFailed,axiom::diag_codes::kBoolPreparationBudgetExceeded,"bool.split"))
            return failure(__LINE__);
        split = {};
        split.intersection.max_edges_per_face = 3;
        if (!expect_failure(kernel.booleans().prepare_split_classification(*a.value,*b.value,split),
                            axiom::StatusCode::OperationFailed,axiom::diag_codes::kBoolPreparationBudgetExceeded,
                            "bool.prep.candidates")) return failure(__LINE__);
        for (const auto pair : {std::array{*a.value,*touching.value},std::array{*a.value,*a.value}}) {
            if (!expect_failure(kernel.booleans().prepare_split_classification(pair[0],pair[1]),
                                axiom::StatusCode::NotImplemented,axiom::diag_codes::kBoolCoplanarUnsupported,
                                "bool.intersect")) return failure(__LINE__);
        }
        // This noncoplanar tangent contact has no material volume. The point
        // contacts and common edge remain real constraints while every open
        // source-face fragment is outside the opposite body.
        const auto tangent_split = kernel.booleans().prepare_split_classification(*wedge.value,*tangent.value);
        if (!tangent_split.value) print_result("tangent split",tangent_split);
        if (!tangent_split.value || !unchanged() ||
            std::none_of(tangent_split.value->intersection.segments.begin(),tangent_split.value->intersection.segments.end(),
                         [](const auto& segment) { return segment.point_contact; })) return failure(__LINE__);
        for (const auto& fragment : tangent_split.value->fragments)
            if (fragment.classification.location != axiom::BooleanPointLocation::Outside) return failure(__LINE__);
        for (const auto& segment : tangent_split.value->intersection.segments) for (const bool lhs : {false,true}) {
            const auto face = lhs ? segment.lhs_face : segment.rhs_face;
            const auto body = lhs ? *wedge.value : *tangent.value;
            bool point_found = false;
            std::vector<std::array<double,2>> intervals;
            const axiom::Vec3 d {segment.end.x-segment.begin.x,segment.end.y-segment.begin.y,
                                segment.end.z-segment.begin.z};
            const double squared = d.x*d.x+d.y*d.y+d.z*d.z;
            for (const auto& fragment : tangent_split.value->fragments) {
                if (fragment.source_body != body || fragment.source_face != face) continue;
                for (std::size_t side = 0; side < 3; ++side) {
                    const auto p = fragment.vertices[side], q = fragment.vertices[(side+1)%3];
                    if (std::hypot(p.x-segment.begin.x,p.y-segment.begin.y,p.z-segment.begin.z) < 1e-8)
                        point_found = true;
                    if (segment.point_contact) continue;
                    const double a = ((p.x-segment.begin.x)*d.x+(p.y-segment.begin.y)*d.y+
                                      (p.z-segment.begin.z)*d.z)/squared;
                    const double b = ((q.x-segment.begin.x)*d.x+(q.y-segment.begin.y)*d.y+
                                      (q.z-segment.begin.z)*d.z)/squared;
                    if (std::hypot(p.x-segment.begin.x-a*d.x,p.y-segment.begin.y-a*d.y,p.z-segment.begin.z-a*d.z) > 1e-8 ||
                        std::hypot(q.x-segment.begin.x-b*d.x,q.y-segment.begin.y-b*d.y,q.z-segment.begin.z-b*d.z) > 1e-8)
                        continue;
                    const std::array<double,2> range {std::max(0.0,std::min(a,b)),std::min(1.0,std::max(a,b))};
                    if (range[1]-range[0] > 1e-8) intervals.push_back(range);
                }
            }
            if (segment.point_contact) {
                if (!point_found) return failure(__LINE__);
            } else {
                std::sort(intervals.begin(),intervals.end());
                double end = 0;
                for (const auto range : intervals) {
                    if (range[0] > end+1e-8) return failure(__LINE__);
                    end = std::max(end,range[1]);
                }
                if (std::abs(end-1) > 1e-8) return failure(__LINE__);
            }
        }
        const std::array<axiom::Point3,1> ordinary {{{1,1,1}}};
        const std::array<axiom::Point3,1> uncertain {{{2+5e-7,1,1}}};
        if (!expect_failure(kernel.booleans().classify_points(*a.value,uncertain),
                            axiom::StatusCode::NumericalInstability,axiom::diag_codes::kBoolNumericalFailure,
                            "bool.classify")) return failure(__LINE__);
        const std::array<axiom::Point3,2> invalid {{{1,1,1},{std::numeric_limits<double>::quiet_NaN(),1,1}}};
        if (!expect_failure(kernel.booleans().classify_points(*a.value,invalid),
                            axiom::StatusCode::InvalidInput,axiom::diag_codes::kBoolInvalidInput,"bool.classify") ||
            !expect_failure(kernel.booleans().classify_points({},ordinary),
                            axiom::StatusCode::InvalidInput,axiom::diag_codes::kBoolInvalidInput,"bool.classify") ||
            !expect_failure(kernel.booleans().classify_points(*sphere.value,ordinary),
                            axiom::StatusCode::NotImplemented,axiom::diag_codes::kBoolUnsupportedInput,"bool.classify"))
            return failure(__LINE__);
        axiom::BooleanIntersectionOptions classify;
        classify.max_segments = 1;
        const std::array<axiom::Point3,2> too_many {{{1,1,1},{3,3,3}}};
        if (!expect_failure(kernel.booleans().classify_points(*a.value,too_many,classify),
                            axiom::StatusCode::OperationFailed,axiom::diag_codes::kBoolPreparationBudgetExceeded,
                            "bool.classify")) return failure(__LINE__);
        const auto success = kernel.booleans().prepare_split_classification(*a.value,*b.value);
        const auto classified = kernel.booleans().classify_points(*a.value,ordinary);
        const std::array<axiom::Point3,1> bbox_phantom {{{1.8,1.8,0.5}}};
        const auto wedge_classified = kernel.booleans().classify_points(*wedge.value,bbox_phantom);
        if (!success.value) print_result("ordinary split",success);
        if (!classified.value) print_result("ordinary classify",classified);
        if (!wedge_classified.value) print_result("wedge classify",wedge_classified);
        if (!success.value || success.value->fragments.empty() || !classified.value || classified.value->size() != 1 ||
            classified.value->front().location != axiom::BooleanPointLocation::Inside ||
            !wedge_classified.value || wedge_classified.value->size() != 1 ||
            wedge_classified.value->front().location != axiom::BooleanPointLocation::Outside || !unchanged()) return failure(__LINE__);
        if (active_writer) {
            if (!transaction.create_vertex({96,95,94}).value || transaction.rollback().status != axiom::StatusCode::Ok)
                return failure(__LINE__);
        }
        const auto active = kernel.topology().has_active_write_transaction();
        if (!active.value || *active.value || counts() != committed_counts || bridge() != committed_bridge ||
            cache_stats() != committed_cache ||
            input_snapshot() != committed_input ||
            kernel.validate().validate_topology(*a.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
            kernel.validate().validate_topology(*b.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok)
            return failure(__LINE__);
    }
    return true;
}

}  // namespace

int main() {
    if (!check_split_classification_isolation()) {
        std::cerr << "boolean split/classification isolation regression\n";
        return 1;
    }
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
