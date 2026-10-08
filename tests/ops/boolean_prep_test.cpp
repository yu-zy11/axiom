#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <utility>
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
    std::vector<axiom::DiagnosticId> failure_diagnostics;
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
            issue.related_entities != std::vector<std::uint64_t>{a.value, b.value} ||
            issue.numeric_evidence.size() < 4) return false;
        failure_diagnostics.push_back(result.diagnostic_id);
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
                         std::to_string(b.value) + "]") != std::string::npos &&
               json.find("\"numeric_evidence\":[") != std::string::npos;
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
    axiom::DiagnosticEvidencePolicy policy;
    policy.issue_code_prefix = "AXM-";
    policy.stage_prefix = "bool.prep.export.";
    const auto audit = kernel.diagnostics().audit_evidence(failure_diagnostics, policy);
    if (!audit.value || !audit.value->passed() ||
        audit.value->reports_inspected != failure_diagnostics.size() ||
        audit.value->matching_issues != failure_diagnostics.size()) return false;
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

// Keep the caps as single faces with actual inner loops, rather than relying
// only on the sweep materializer's triangulated representation of a hole.
axiom::Result<axiom::BodyId> make_reference_holed_prism(
    axiom::Kernel& kernel, double x_scale = 1,
    std::vector<std::pair<axiom::VertexId,axiom::Point3>>* reference_vertices = nullptr) {
    std::array<axiom::Point3,8> points {{{0,0,0},{4,0,0},{4,4,0},{0,4,0},
                                       {1,1,0},{1,3,0},{3,3,0},{3,1,0}}};
    for (auto& point : points) point.x *= x_scale;
    auto transaction = kernel.topology().begin_transaction();
    std::array<axiom::VertexId,16> vertices {};
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        auto p = points[i%8];
        if (i >= 8) p.z = 2;
        const auto vertex = transaction.create_vertex(p);
        if (!vertex.value) return {};
        vertices[i] = *vertex.value;
        if (reference_vertices) reference_vertices->push_back({*vertex.value,p});
    }
    const auto next = [](std::size_t i) { return (i/4)*4+(i+1)%4; };
    std::array<axiom::EdgeId,24> edges {};
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const std::size_t index = i%8;
        const std::size_t a = i < 16 ? index+(i >= 8 ? 8 : 0) : index;
        const std::size_t b = i < 16 ? next(index)+(i >= 8 ? 8 : 0) : index+8;
        auto p = points[a%8], q = points[b%8];
        if (a >= 8) p.z = 2;
        if (b >= 8) q.z = 2;
        const double length = std::hypot(q.x-p.x,q.y-p.y,q.z-p.z);
        const auto curve = kernel.curves().make_line(p,{(q.x-p.x)/length,(q.y-p.y)/length,(q.z-p.z)/length});
        const auto edge = curve.value ? transaction.create_edge(*curve.value,vertices[a],vertices[b])
                                     : axiom::Result<axiom::EdgeId>{};
        if (!edge.value) return {};
        edges[i] = *edge.value;
    }
    const auto make_loop = [&](const std::vector<std::pair<std::size_t,bool>>& refs) {
        std::vector<axiom::CoedgeId> coedges;
        for (const auto& [edge,reversed] : refs) {
            const auto coedge = transaction.create_coedge(edges[edge],reversed);
            if (!coedge.value) return axiom::Result<axiom::LoopId>{};
            coedges.push_back(*coedge.value);
        }
        return transaction.create_loop(coedges);
    };
    std::vector<axiom::FaceId> faces;
    for (std::size_t i = 0; i < 8; ++i) {
        const auto j = next(i);
        const auto loop = make_loop({{i,false},{16+j,false},{8+i,true},{16+i,true}});
        const auto p = points[i], q = points[j];
        const auto plane = kernel.surfaces().make_plane(p,{q.y-p.y,p.x-q.x,0});
        const auto face = loop.value && plane.value ? transaction.create_face(*plane.value,*loop.value,{})
                                                   : axiom::Result<axiom::FaceId>{};
        if (!face.value) return {};
        faces.push_back(*face.value);
    }
    for (const bool top : {false,true}) {
        std::array<axiom::LoopId,2> loops {};
        for (std::size_t ring = 0; ring < 2; ++ring) {
            std::vector<std::pair<std::size_t,bool>> refs;
            for (std::size_t i = 0; i < 4; ++i)
                refs.push_back({(top ? 8 : 0)+4*ring+(top ? i : 3-i),!top});
            const auto loop = make_loop(refs);
            if (!loop.value) return {};
            loops[ring] = *loop.value;
        }
        const auto plane = kernel.surfaces().make_plane({0,0,top ? 2.0 : 0.0},{0,0,top ? 1.0 : -1.0});
        const auto face = plane.value ? transaction.create_face(*plane.value,loops[0],std::array{loops[1]})
                                      : axiom::Result<axiom::FaceId>{};
        if (!face.value) return {};
        faces.push_back(*face.value);
    }
    const auto shell = transaction.create_shell(faces);
    const auto body = shell.value ? transaction.create_body(std::array{*shell.value}) : axiom::Result<axiom::BodyId>{};
    if (!body.value || transaction.commit().status != axiom::StatusCode::Ok) return {};
    return body;
}

// Frozen analytic references for the planar preparation slice. Expectations
// come from half-space equations and polygon intervals, never kernel bboxes or
// a second invocation of the production intersection routine.
bool planar_failure(int line) {
    std::cerr << "planar boolean preparation check failed at line " << line << "\n";
    return false;
}

bool check_planar_intersection_references() {
    axiom::Kernel kernel;
    const auto a = kernel.primitives().box({0,0,0},2,2,2);
    const auto b = kernel.primitives().box({1,1,1},2,2,2);
    if (!a.value || !b.value) return planar_failure(__LINE__);
    const auto counts = [&]() {
        return std::array{kernel.body_count().value,kernel.geometry_count().value,
                          kernel.topology_count().value,kernel.intersection_count().value,
                          kernel.eval_node_count().value,kernel.cache_entry_count().value};
    };
    const auto baseline = counts();
    const auto reference = kernel.booleans().prepare_intersections(*a.value,*b.value);
    if (!reference.value || reference.status != axiom::StatusCode::Ok || counts() != baseline ||
        reference.value->candidates.size() != 6 || reference.value->segments.size() != 6) return planar_failure(__LINE__);

    // Six independent edges: one A coordinate = 2, one B coordinate = 1,
    // and the third coordinate ranges from 1 to 2. Check completeness by
    // interval coverage, allowing triangulated faces to partition an edge.
    const auto check_cube_reference = [&](const axiom::BooleanIntersectionPreparation& preparation,
                                          const auto& inverse) {
        for (const auto& candidate : preparation.candidates) {
            for (const bool lhs : {false,true}) {
                const auto loops = kernel.topology().query().loops_of_face(lhs ? candidate.lhs_face : candidate.rhs_face);
                if (!loops.value || loops.value->empty()) return planar_failure(__LINE__);
                std::vector<axiom::EdgeId> actual;
                for (const auto loop : *loops.value) {
                    const auto edges = kernel.topology().query().edges_of_loop(loop);
                    if (!edges.value) return planar_failure(__LINE__);
                    actual.insert(actual.end(),edges.value->begin(),edges.value->end());
                }
                auto declared = lhs ? candidate.lhs_edges : candidate.rhs_edges;
                const auto less = [](axiom::EdgeId a,axiom::EdgeId b) { return a.value < b.value; };
                std::sort(actual.begin(),actual.end(),less);
                std::sort(declared.begin(),declared.end(),less);
                if (actual != declared) return planar_failure(__LINE__);
            }
        }
        std::array<std::vector<std::array<double,2>>,9> intervals;
        for (const auto& segment : preparation.segments) {
            if (segment.begin_hits.empty() || segment.end_hits.empty()) return planar_failure(__LINE__);
            const auto p = inverse(segment.begin), q = inverse(segment.end);
            const std::array<double,3> x {p.x,p.y,p.z}, y {q.x,q.y,q.z};
            bool found = false;
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
                if (i == j || std::abs(x[i]-2) > 1e-8 || std::abs(y[i]-2) > 1e-8 ||
                    std::abs(x[j]-1) > 1e-8 || std::abs(y[j]-1) > 1e-8) continue;
                const int k = 3-i-j;
                const double low = std::min(x[k],y[k]), high = std::max(x[k],y[k]);
                if (low < 1-1e-8 || high > 2+1e-8) return planar_failure(__LINE__);
                if (!segment.point_contact) intervals[3*i+j].push_back({low,high});
                found = true;
                break;
            }
            if (!found) return planar_failure(__LINE__);
            const auto pair = std::find_if(preparation.candidates.begin(),preparation.candidates.end(),
                [&](const auto& candidate) {
                    return candidate.lhs_face == segment.lhs_face && candidate.rhs_face == segment.rhs_face;
                });
            if (pair == preparation.candidates.end()) return planar_failure(__LINE__);
            for (const auto* hits : {&segment.begin_hits,&segment.end_hits}) for (const auto& hit : *hits) {
                if (hit.edge_fraction < 0 || hit.edge_fraction > 1) return planar_failure(__LINE__);
                const auto* edges = hit.face == pair->lhs_face ? &pair->lhs_edges :
                                    hit.face == pair->rhs_face ? &pair->rhs_edges : nullptr;
                if (!edges || std::find(edges->begin(),edges->end(),hit.edge) == edges->end()) return planar_failure(__LINE__);
                const auto owners = kernel.topology().query().faces_of_edge(hit.edge);
                if (!owners.value || std::find(owners.value->begin(),owners.value->end(),hit.face) == owners.value->end())
                    return planar_failure(__LINE__);
            }
        }
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) if (i != j) {
            auto& edge = intervals[3*i+j];
            std::sort(edge.begin(),edge.end());
            double end = 1, length = 0;
            for (const auto interval : edge) {
                if (interval[0] > end+1e-8) return planar_failure(__LINE__);
                end = std::max(end,interval[1]);
                length += interval[1]-interval[0];
            }
            if (std::abs(end-2) > 1e-8 || std::abs(length-1) > 1e-8) return planar_failure(__LINE__);
        }
        return true;
    };
    if (!check_cube_reference(*reference.value,[](axiom::Point3 p) { return p; })) return planar_failure(__LINE__);
    const auto reversed = kernel.booleans().prepare_intersections(*b.value,*a.value);
    if (!reversed.value || reversed.value->candidates.size() != 6 || reversed.value->segments.size() != 6 || counts() != baseline ||
        !check_cube_reference(*reversed.value,[](axiom::Point3 p) { return p; })) return planar_failure(__LINE__);

    // A fixed orthogonal rotation: Rz has cos=3/5, sin=4/5;
    // Rx has cos=12/13, sin=5/13, and R = Rx Rz.
    // This moves all face frames away from the world axes; R^T is the oracle.
    const auto rotate = [](axiom::Point3 p) -> axiom::Point3 {
        const double x = 0.6*p.x-0.8*p.y, y = 0.8*p.x+0.6*p.y;
        return {x,(12*y-5*p.z)/13,(5*y+12*p.z)/13};
    };
    const auto inverse = [](axiom::Point3 p) -> axiom::Point3 {
        const double y = (12*p.y+5*p.z)/13, z = (-5*p.y+12*p.z)/13;
        return {0.6*p.x+0.8*y,-0.8*p.x+0.6*y,z};
    };
    const auto make_rotated_cube = [&](double offset) {
        axiom::ProfileRef profile;
        profile.label = "s4-reference-rotated-cube";
        for (const axiom::Point3 p : {axiom::Point3{offset,offset,offset},
            axiom::Point3{offset+2,offset,offset},axiom::Point3{offset+2,offset+2,offset},
            axiom::Point3{offset,offset+2,offset}}) profile.polygon_xyz.push_back(rotate(p));
        return kernel.sweeps().extrude(profile,{0,-5.0/13,12.0/13},2);
    };
    const auto ra = make_rotated_cube(0), rb = make_rotated_cube(1);
    if (!ra.value || !rb.value) return planar_failure(__LINE__);
    const auto rotated = kernel.booleans().prepare_intersections(*ra.value,*rb.value);
    if (!rotated.value || !check_cube_reference(*rotated.value,inverse)) return planar_failure(__LINE__);

    // Wedge material: x>=0, y>=0, x+y<=2, 0<=z<=2. A box
    // entirely beyond x+y=2 overlaps its bbox but has no geometric intersection.
    const auto wedge = kernel.primitives().wedge({0,0,0},2,2,2);
    const auto gap = kernel.primitives().box({1.2,1.2,0.25},0.4,0.4,0.5);
    const auto cutter = kernel.primitives().box({0.5,0.5,-0.5},1,1,1);
    if (!wedge.value || !gap.value || !cutter.value) return planar_failure(__LINE__);
    const auto phantom = kernel.booleans().prepare_intersections(*wedge.value,*gap.value);
    if (!phantom.value || phantom.value->candidates.empty() || !phantom.value->segments.empty()) return planar_failure(__LINE__);
    const auto diagonal = kernel.booleans().prepare_intersections(*wedge.value,*cutter.value);
    if (!diagonal.value) return planar_failure(__LINE__);
    double diagonal_length = 0;
    for (const auto& segment : diagonal.value->segments) {
        for (const auto p : {segment.begin,segment.end,
            axiom::Point3{(segment.begin.x+segment.end.x)/2,(segment.begin.y+segment.end.y)/2,
                          (segment.begin.z+segment.end.z)/2}}) {
            if (p.x < 0.5-1e-8 || p.y < 0.5-1e-8 || p.x+p.y > 2+1e-8 || p.z < -1e-8 || p.z > 0.5+1e-8)
                return planar_failure(__LINE__);
        }
        if (std::abs(segment.begin.z-0.5) < 1e-8 && std::abs(segment.end.z-0.5) < 1e-8 &&
            std::abs(segment.begin.x+segment.begin.y-2) < 1e-8 &&
            std::abs(segment.end.x+segment.end.y-2) < 1e-8)
            diagonal_length += std::hypot(segment.end.x-segment.begin.x,segment.end.y-segment.begin.y);
    }
    if (std::abs(diagonal_length-std::sqrt(2.0)) > 1e-8) return planar_failure(__LINE__);
    // A box beyond the wedge's sloping face touches it only along one vertical
    // edge. Horizontal box faces yield genuine transverse single-point hits.
    const auto tangent = kernel.primitives().box({0.75,1.25,0.25},0.5,0.5,0.5);
    if (!tangent.value) return planar_failure(__LINE__);
    const auto contact = kernel.booleans().prepare_intersections(*wedge.value,*tangent.value);
    if (!contact.value || std::none_of(contact.value->segments.begin(),contact.value->segments.end(),
                                     [](const auto& segment) { return segment.point_contact; })) return planar_failure(__LINE__);
    for (const auto& segment : contact.value->segments) for (const auto p : {segment.begin,segment.end})
        if (std::abs(p.x-0.75) > 1e-8 || std::abs(p.y-1.25) > 1e-8 || p.z < 0.25-1e-8 || p.z > 0.75+1e-8)
            return planar_failure(__LINE__);

    // Both a concave U and a square with a square hole have the independent
    // cross-section [0,1] union [3,4] at y=2. Cutter faces at y=1.5/2.5
    // intersect the z=0 cap in those two intervals, total length exactly 2.
    const auto section_cutter = kernel.primitives().box({-1,1.5,-0.5},6,1,1);
    if (!section_cutter.value) return planar_failure(__LINE__);
    for (const int model : {0,1,2}) {
        const bool holed = model != 0;
        axiom::ProfileRef profile;
        profile.label = holed ? "s4-reference-square-hole" : "s4-reference-concave-U";
        if (holed) {
            profile.polygon_xyz = {{0,0,0},{4,0,0},{4,4,0},{0,4,0}};
            profile.holes_xyz = {{{1,1,0},{3,1,0},{3,3,0},{1,3,0}}};
        } else profile.polygon_xyz = {{0,0,0},{4,0,0},{4,4,0},{3,4,0},
                                      {3,1,0},{1,1,0},{1,4,0},{0,4,0}};
        std::vector<std::pair<axiom::VertexId,axiom::Point3>> reference_vertices;
        const auto prism = model == 2 ? make_reference_holed_prism(kernel,1,&reference_vertices)
                                     : kernel.sweeps().extrude(profile,{0,0,1},2);
        if (!prism.value) return planar_failure(__LINE__);
        const auto preparation = kernel.booleans().prepare_intersections(*prism.value,*section_cutter.value);
        if (!preparation.value || preparation.value->segments.empty()) return planar_failure(__LINE__);
        // Full boundary-intersection oracle, not only the two cap sections:
        // four z=0 cap intervals, four z=0.5 side intervals, and eight vertical
        // intervals. The U and holed square have the same material in this band.
        std::vector<std::array<axiom::Point3,2>> expected;
        for (const double y : {1.5,2.5}) for (const double x : {0.0,3.0})
            expected.push_back({axiom::Point3{x,y,0},axiom::Point3{x+1,y,0}});
        for (const double x : {0.0,1.0,3.0,4.0}) {
            expected.push_back({axiom::Point3{x,1.5,0.5},axiom::Point3{x,2.5,0.5}});
            for (const double y : {1.5,2.5})
                expected.push_back({axiom::Point3{x,y,0},axiom::Point3{x,y,0.5}});
        }
        std::vector<std::vector<std::array<double,2>>> coverage(expected.size());
        std::array<int,2> segment_counts {};
        std::size_t checked_source_hits = 0, checked_inner_hits = 0;
        const auto source_faces = kernel.topology().query().faces_of_body(*prism.value);
        if (!source_faces.value) return planar_failure(__LINE__);
        for (const auto& segment : preparation.value->segments) {
            if (model == 2) {
                for (const bool begin : {false,true}) {
                    const auto& hits = begin ? segment.begin_hits : segment.end_hits;
                    const auto actual = begin ? segment.begin : segment.end;
                    for (const auto& hit : hits) {
                        if (std::find(source_faces.value->begin(),source_faces.value->end(),hit.face) == source_faces.value->end())
                            continue;
                        const auto endpoints = kernel.topology().query().vertices_of_edge(hit.edge);
                        if (!endpoints.value || !std::isfinite(hit.edge_fraction) || hit.edge_fraction < 0 || hit.edge_fraction > 1)
                            return planar_failure(__LINE__);
                        std::array<axiom::Point3,2> points {};
                        for (std::size_t i = 0; i < 2; ++i) {
                            const auto vertex = std::find_if(reference_vertices.begin(),reference_vertices.end(),
                                [&](const auto& item) { return item.first == (*endpoints.value)[i]; });
                            if (vertex == reference_vertices.end()) return planar_failure(__LINE__);
                            points[i] = vertex->second;
                        }
                        const auto p = points[0], q = points[1];
                        const axiom::Point3 expected {p.x+hit.edge_fraction*(q.x-p.x),
                            p.y+hit.edge_fraction*(q.y-p.y),p.z+hit.edge_fraction*(q.z-p.z)};
                        if (std::hypot(expected.x-actual.x,expected.y-actual.y,expected.z-actual.z) > 1e-8)
                            return planar_failure(__LINE__);
                        ++checked_source_hits;
                        const auto loops = kernel.topology().query().loops_of_face(hit.face);
                        if (!loops.value) return planar_failure(__LINE__);
                        for (std::size_t i = 1; i < loops.value->size(); ++i) {
                            const auto edges = kernel.topology().query().edges_of_loop((*loops.value)[i]);
                            if (!edges.value) return planar_failure(__LINE__);
                            if (std::find(edges.value->begin(),edges.value->end(),hit.edge) != edges.value->end()) ++checked_inner_hits;
                        }
                    }
                }
            }
            bool matched = false;
            for (std::size_t i = 0; i < expected.size(); ++i) {
                const auto p = expected[i][0], q = expected[i][1];
                const std::array<double,3> start {p.x,p.y,p.z}, end {q.x,q.y,q.z};
                const std::array<double,3> a {segment.begin.x,segment.begin.y,segment.begin.z};
                const std::array<double,3> b {segment.end.x,segment.end.y,segment.end.z};
                int varying = -1;
                bool on_reference = true;
                for (int axis = 0; axis < 3; ++axis) {
                    if (start[axis] != end[axis]) varying = axis;
                    else if (std::abs(a[axis]-start[axis]) > 1e-8 ||
                             std::abs(b[axis]-start[axis]) > 1e-8) on_reference = false;
                }
                if (!on_reference || varying < 0) continue;
                const double low = (std::min(a[varying],b[varying])-start[varying]) /
                                   (end[varying]-start[varying]);
                const double high = (std::max(a[varying],b[varying])-start[varying]) /
                                    (end[varying]-start[varying]);
                if (low < -1e-8 || high > 1+1e-8) continue;
                if (!segment.point_contact) coverage[i].push_back({low,high});
                matched = true;
                break;
            }
            if (!matched || segment.begin_hits.empty() || segment.end_hits.empty()) return planar_failure(__LINE__);
            for (std::size_t side = 0; side < 2; ++side) {
                const double y = side == 0 ? 1.5 : 2.5;
                if (std::abs(segment.begin.z) < 1e-8 && std::abs(segment.end.z) < 1e-8 &&
                    std::abs(segment.begin.y-y) < 1e-8 && std::abs(segment.end.y-y) < 1e-8 && !segment.point_contact) {
                    ++segment_counts[side];
                }
            }
        }
        for (auto& intervals : coverage) {
            std::sort(intervals.begin(),intervals.end());
            double end = 0, length = 0;
            for (const auto interval : intervals) {
                if (interval[0] > end+1e-8) return planar_failure(__LINE__);
                end = std::max(end,interval[1]);
                length += interval[1]-interval[0];
            }
            if (std::abs(end-1) > 1e-8 || std::abs(length-1) > 1e-8) return planar_failure(__LINE__);
        }
        if (model == 2 && (segment_counts[0] != 2 || segment_counts[1] != 2 ||
                           checked_source_hits == 0 || checked_inner_hits == 0)) return planar_failure(__LINE__);
    }
    const auto far = kernel.primitives().box({10,10,10},1,1,1);
    const auto inner = kernel.primitives().box({0.5,0.5,0.5},0.5,0.5,0.5);
    if (!far.value || !inner.value) return planar_failure(__LINE__);
    for (const auto other : {*far.value,*inner.value}) {
        const auto empty = kernel.booleans().prepare_intersections(*a.value,other);
        if (!empty.value || !empty.value->candidates.empty() || !empty.value->segments.empty()) return planar_failure(__LINE__);
    }
    return true;
}

bool check_planar_preparation_failure_isolation() {
    axiom::Kernel kernel;
    const auto a = kernel.primitives().box({0,0,0},2,2,2);
    const auto b = kernel.primitives().box({1,1,1},2,2,2);
    const auto touching = kernel.primitives().box({2,0,0},2,2,2);
    const auto sphere = kernel.primitives().sphere({1,1,1},1);
    const auto cylinder = kernel.primitives().cylinder({1,1,1},{0,0,1},1,2);
    const auto unresolved = kernel.primitives().box({1e12,1e12,1e12},2,2,2);
    // Build valid explicit topology under 1e-9, then restore the declared
    // preparation policy 1e-6. The inner-ring horizontal edges are exactly 5e-7.
    if (kernel.set_linear_tolerance(1e-9).status != axiom::StatusCode::Ok) return planar_failure(__LINE__);
    const auto thin = make_reference_holed_prism(kernel,2.5e-7);
    if (!thin.value || kernel.set_linear_tolerance(1e-6).status != axiom::StatusCode::Ok)
        return planar_failure(__LINE__);
    const auto thin_edges = kernel.topology().query().edges_of_body(*thin.value);
    if (!thin_edges.value) return planar_failure(__LINE__);
    bool has_short_reference = false;
    for (const auto edge : *thin_edges.value) {
        const auto length = kernel.topology().query().edge_length(edge);
        if (!length.value) return planar_failure(__LINE__);
        if (std::abs(*length.value-5e-7) < 1e-14) has_short_reference = true;
    }
    if (!has_short_reference) return planar_failure(__LINE__);
    axiom::ProfileRef near_profile;
    near_profile.label = "s4-reference-near-parallel";
    constexpr double angle = 5e-7;
    for (const auto xy : {std::array{0.0,0.0},std::array{1.0,0.0},std::array{1.0,1.0},std::array{0.0,1.0}})
        near_profile.polygon_xyz.push_back({2-2.5e-7+std::cos(angle)*xy[0]-std::sin(angle)*xy[1],
                                           0.5+std::sin(angle)*xy[0]+std::cos(angle)*xy[1],0.5});
    const auto near_parallel = kernel.sweeps().extrude(near_profile,{0,0,1},1);
    // Two almost equally rotated cubes have overlapping face envelopes for
    // parallel-looking faces separated by one model unit. Their analytic line
    // is about 1e8 units away. Inputs resolve 1e-6, but that solve does not;
    // it must fail before an empty trim can silently claim no intersection.
    const auto make_oblique_cube = [&](double offset, double twist) {
        axiom::ProfileRef profile;
        profile.label = "s4-reference-unresolved-line";
        for (const axiom::Point3 p : {axiom::Point3{offset,offset,offset},
            axiom::Point3{offset+2,offset,offset},axiom::Point3{offset+2,offset+2,offset},
            axiom::Point3{offset,offset+2,offset}}) {
            const double x = std::cos(twist)*p.x-std::sin(twist)*p.y;
            const double y = std::sin(twist)*p.x+std::cos(twist)*p.y;
            const double rx = 0.6*x-0.8*y, ry = 0.8*x+0.6*y;
            profile.polygon_xyz.push_back({rx,(12*ry-5*p.z)/13,(5*ry+12*p.z)/13});
        }
        return kernel.sweeps().extrude(profile,{0,-5.0/13,12.0/13},2);
    };
    const auto oblique_a = make_oblique_cube(0,0), oblique_b = make_oblique_cube(1,1e-8);
    if (!a.value || !b.value || !touching.value || !sphere.value || !cylinder.value || !unresolved.value || !thin.value)
        return planar_failure(__LINE__);
    if (!near_parallel.value || !oblique_a.value || !oblique_b.value) return planar_failure(__LINE__);
    auto transaction = kernel.topology().begin_transaction();
    const auto sentinel = transaction.create_vertex({99,98,97});
    if (!sentinel.value) return planar_failure(__LINE__);
    const auto a_faces = kernel.topology().query().faces_of_body(*a.value);
    if (!a_faces.value || a_faces.value->empty()) return planar_failure(__LINE__);
    const auto open_shell = transaction.create_shell(std::array{a_faces.value->front()});
    const auto open_body = open_shell.value ? transaction.create_body(std::array{*open_shell.value})
                                           : axiom::Result<axiom::BodyId>{};
    if (!open_body.value) return planar_failure(__LINE__);
    const auto transaction_writes = transaction.write_operation_count().value;
    if (!transaction_writes) return planar_failure(__LINE__);
    const auto counts = [&]() {
        return std::array{kernel.body_count().value,kernel.geometry_count().value,kernel.topology_count().value,
                          kernel.intersection_count().value,kernel.eval_node_count().value,kernel.cache_entry_count().value};
    };
    // Capture public topology relations plus per-face coordinate extrema. This
    // detects in-place changes that store counts and transaction counters miss.
    const auto input_snapshot = [&]() {
        std::pair<std::vector<std::uint64_t>,std::vector<double>> snapshot;
        const auto query = kernel.topology().query();
        for (const auto body : {*a.value,*b.value,*touching.value,*sphere.value,*cylinder.value,
                               *unresolved.value,*thin.value,*near_parallel.value,*oblique_a.value,*oblique_b.value}) {
            const auto faces = query.faces_of_body(body);
            const auto shells = query.shells_of_body(body);
            if (!faces.value || !shells.value) return decltype(snapshot){};
            snapshot.first.push_back(body.value);
            snapshot.first.push_back(shells.value->size());
            for (const auto shell : *shells.value) snapshot.first.push_back(shell.value);
            snapshot.first.push_back(faces.value->size());
            for (const auto face : *faces.value) {
                const auto surface = query.surface_of_face(face);
                const auto loops = query.loops_of_face(face);
                const auto bbox = query.bbox_of_face(face);
                if (!surface.value || !loops.value || !bbox.value || !bbox.value->is_valid) return decltype(snapshot){};
                snapshot.first.insert(snapshot.first.end(),{face.value,surface.value->value,loops.value->size()});
                const auto& box = *bbox.value;
                snapshot.second.insert(snapshot.second.end(),{box.min.x,box.min.y,box.min.z,box.max.x,box.max.y,box.max.z});
                for (const auto loop : *loops.value) {
                    const auto edges = query.edges_of_loop(loop);
                    const auto vertices = query.vertices_of_loop(loop);
                    if (!edges.value || !vertices.value) return decltype(snapshot){};
                    snapshot.first.insert(snapshot.first.end(),{loop.value,edges.value->size(),vertices.value->size()});
                    for (const auto vertex : *vertices.value) snapshot.first.push_back(vertex.value);
                    for (const auto edge : *edges.value) {
                        const auto endpoints = query.vertices_of_edge(edge);
                        if (!endpoints.value) return decltype(snapshot){};
                        snapshot.first.insert(snapshot.first.end(),{edge.value,(*endpoints.value)[0].value,(*endpoints.value)[1].value});
                    }
                }
            }
        }
        return snapshot;
    };
    const auto bridge_snapshot = [&] {
        const auto metrics = kernel.eval_graph_metrics();
        if (!metrics.value) return std::array<std::uint64_t,5>{};
        const auto& bridge = metrics.value->invalidation_bridge;
        return std::array{bridge.for_body_entries,bridge.for_faces_entries,bridge.for_bodies_batches,
                          bridge.for_bodies_list_size_total,bridge.downstream_invalidation_steps};
    };
    const auto input_baseline = input_snapshot();
    if (input_baseline.first.empty()) return planar_failure(__LINE__);
    const auto bridge_baseline = bridge_snapshot();
    const auto baseline = counts();
    const auto expect = [&](axiom::BodyId lhs, axiom::BodyId rhs, const axiom::BooleanIntersectionOptions& options,
                             axiom::StatusCode status, std::string_view code, std::string_view stage) {
        const auto result = kernel.booleans().prepare_intersections(lhs,rhs,options);
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        const auto active = kernel.topology().has_active_write_transaction();
        const auto kept_sentinel = transaction.has_created_vertex(*sentinel.value);
        if (result.status != status || result.value || !report.value || !active.value || !*active.value ||
            !kept_sentinel.value || !*kept_sentinel.value || baseline != counts() ||
            input_snapshot() != input_baseline || bridge_snapshot() != bridge_baseline ||
            transaction.write_operation_count().value != transaction_writes) {
            std::cerr << "expected status=" << static_cast<int>(status) << " code=" << code << " stage=" << stage
                      << " lhs=" << lhs.value << " rhs=" << rhs.value
                      << " actual status=" << static_cast<int>(result.status) << " value=" << bool(result.value)
                      << " active=" << (active.value && *active.value) << " sentinel="
                      << (kept_sentinel.value && *kept_sentinel.value) << " counts=" << (baseline == counts())
                      << " writes=" << (transaction.write_operation_count().value == transaction_writes) << "\n";
            if (report.value) for (const auto& item : report.value->issues)
                std::cerr << "actual code=" << item.code << " stage=" << item.stage << " message=" << item.message << "\n";
            return planar_failure(__LINE__);
        }
        const auto* issue = find_issue(*report.value,code);
        if (!issue || issue->stage != stage || issue->numeric_evidence.empty() || issue->related_entities.size() < 2 ||
            issue->related_entities[0] != lhs.value || issue->related_entities[1] != rhs.value) {
            std::cerr << "expected code=" << code << " stage=" << stage << " lhs=" << lhs.value
                      << " rhs=" << rhs.value << " actual status=" << static_cast<int>(result.status) << "\n";
            for (const auto& item : report.value->issues) {
                std::cerr << "actual code=" << item.code << " stage=" << item.stage << " entities=";
                for (const auto id : item.related_entities) std::cerr << id << ",";
                std::cerr << " evidence=" << item.numeric_evidence.size() << " message=" << item.message << "\n";
            }
            return planar_failure(__LINE__);
        }
        for (const auto& evidence : issue->numeric_evidence) if (!std::isfinite(evidence.value)) return planar_failure(__LINE__);
        const auto stages = kernel.diagnostics().find_by_issue_stage(stage,1000);
        if (!stages.value || std::find(stages.value->begin(),stages.value->end(),result.diagnostic_id) == stages.value->end())
            return planar_failure(__LINE__);
        const auto path = std::filesystem::temp_directory_path()/"axiom_planar_boolean_failure.json";
        if (kernel.diagnostics().export_report_json(result.diagnostic_id,path.string()).status != axiom::StatusCode::Ok)
            return planar_failure(__LINE__);
        std::ifstream input {path};
        const std::string json((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        input.close();
        std::filesystem::remove(path);
        return json.find("\"code\":\""+std::string(code)+"\"") != std::string::npos &&
            json.find("\"stage\":\""+std::string(stage)+"\"") != std::string::npos && counts() == baseline;
    };
    axiom::BooleanIntersectionOptions options;
    if (!expect({},*a.value,options,axiom::StatusCode::InvalidInput,axiom::diag_codes::kBoolInvalidInput,"bool.prep.candidates"))
        return planar_failure(__LINE__);
    for (const auto unsupported : {*sphere.value,*cylinder.value})
        if (!expect(*a.value,unsupported,options,axiom::StatusCode::NotImplemented,
                    axiom::diag_codes::kBoolUnsupportedInput,"bool.prep.candidates")) return planar_failure(__LINE__);
    if (!expect(*a.value,*touching.value,options,axiom::StatusCode::NotImplemented,
                axiom::diag_codes::kBoolCoplanarUnsupported,"bool.intersect") ||
        !expect(*a.value,*a.value,options,axiom::StatusCode::NotImplemented,
                axiom::diag_codes::kBoolCoplanarUnsupported,"bool.intersect") ||
        !expect(*a.value,*unresolved.value,options,axiom::StatusCode::NumericalInstability,
                axiom::diag_codes::kBoolNumericalFailure,"bool.prep.candidates") ||
        !expect(*a.value,*thin.value,options,axiom::StatusCode::DegenerateGeometry,
                axiom::diag_codes::kBoolInvalidInput,"bool.prep.candidates") ||
        !expect(*a.value,*near_parallel.value,options,axiom::StatusCode::NumericalInstability,
                axiom::diag_codes::kBoolNumericalFailure,"bool.intersect") ||
        !expect(*a.value,*open_body.value,options,axiom::StatusCode::InvalidTopology,
                axiom::diag_codes::kBoolInvalidInput,"bool.prep.candidates")) return planar_failure(__LINE__);
    options.max_face_pairs = 1;
    if (!expect(*a.value,*b.value,options,axiom::StatusCode::OperationFailed,
                axiom::diag_codes::kBoolPreparationBudgetExceeded,"bool.prep.candidates")) return planar_failure(__LINE__);
    options = {};
    options.max_segments = 1;
    if (!expect(*a.value,*b.value,options,axiom::StatusCode::OperationFailed,
                axiom::diag_codes::kBoolPreparationBudgetExceeded,"bool.intersect")) return planar_failure(__LINE__);
    options = {};
    options.max_edges_per_face = 3;
    if (!expect(*a.value,*b.value,options,axiom::StatusCode::OperationFailed,
                axiom::diag_codes::kBoolPreparationBudgetExceeded,"bool.prep.candidates")) return planar_failure(__LINE__);
    options = {};
    options.tolerance.angular = 1e-10;
    for (const bool reversed : {false,true})
        if (!expect(reversed ? *oblique_b.value : *oblique_a.value,
                    reversed ? *oblique_a.value : *oblique_b.value,options,
                    axiom::StatusCode::NumericalInstability,
                    axiom::diag_codes::kBoolNumericalFailure,"bool.intersect")) return planar_failure(__LINE__);
    options = {};
    options.tolerance.precision_mode = axiom::PrecisionMode::ExactCritical;
    if (!expect(*a.value,*b.value,options,axiom::StatusCode::NotImplemented,
                axiom::diag_codes::kBoolUnsupportedInput,"bool.prep.candidates")) return planar_failure(__LINE__);
    for (const auto invalid : {0.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        options = {};
        options.tolerance.linear = invalid;
        if (!expect(*a.value,*b.value,options,axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kBoolInvalidInput,"bool.prep.candidates")) return planar_failure(__LINE__);
    }
    const auto success = kernel.booleans().prepare_intersections(*a.value,*b.value);
    const auto success_active = kernel.topology().has_active_write_transaction();
    if (!success.value || success.value->segments.size() != 6 || counts() != baseline ||
        input_snapshot() != input_baseline || bridge_snapshot() != bridge_baseline ||
        !success_active.value || !*success_active.value ||
        !transaction.has_created_vertex(*sentinel.value).value.value_or(false) ||
        transaction.write_operation_count().value != transaction_writes) return planar_failure(__LINE__);
    // The caller still owns the writer, and can continue writing and roll back.
    if (!transaction.create_vertex({96,95,94}).value || transaction.rollback().status != axiom::StatusCode::Ok) return planar_failure(__LINE__);
    const auto active = kernel.topology().has_active_write_transaction();
    return active.value && !*active.value &&
        kernel.validate().validate_topology(*a.value,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok &&
        kernel.validate().validate_topology(*b.value,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok;
}

// Independently integrate the returned triangles and recover each finite cut
// from subdivision edges. A bbox shell cannot satisfy these area/material oracles.
bool check_split_classification_references() {
    axiom::Kernel kernel;
    using Location = axiom::BooleanPointLocation;
    const auto distance = [](axiom::Point3 p, axiom::Point3 q) {
        return std::hypot(p.x-q.x,p.y-q.y,p.z-q.z);
    };
    const auto area = [](const std::array<axiom::Point3,3>& p) {
        const axiom::Vec3 a {p[1].x-p[0].x,p[1].y-p[0].y,p[1].z-p[0].z};
        const axiom::Vec3 b {p[2].x-p[0].x,p[2].y-p[0].y,p[2].z-p[0].z};
        return std::hypot(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x)/2;
    };
    // Return overlap parameters only when an actual triangular edge lies along
    // the finite intersection, independently of the production associations.
    const auto overlap = [&](axiom::Point3 p, axiom::Point3 q,
                             const axiom::BooleanIntersectionSegment& segment) {
        const axiom::Vec3 d {segment.end.x-segment.begin.x,segment.end.y-segment.begin.y,
                            segment.end.z-segment.begin.z};
        const double squared = d.x*d.x+d.y*d.y+d.z*d.z;
        if (squared <= 1e-18) return std::array<double,2>{1,0};
        const auto parameter = [&](axiom::Point3 point) {
            return ((point.x-segment.begin.x)*d.x+(point.y-segment.begin.y)*d.y+
                    (point.z-segment.begin.z)*d.z)/squared;
        };
        const auto on_line = [&](axiom::Point3 point, double t) {
            return distance(point,{segment.begin.x+t*d.x,segment.begin.y+t*d.y,segment.begin.z+t*d.z}) < 1e-8;
        };
        const double a = parameter(p), b = parameter(q);
        if (!on_line(p,a) || !on_line(q,b)) return std::array<double,2>{1,0};
        return std::array<double,2>{std::max(0.0,std::min(a,b)),std::min(1.0,std::max(a,b))};
    };
    const auto check = [&](axiom::BodyId lhs, axiom::BodyId rhs,
                           double lhs_area, double rhs_area, double lhs_inside, double rhs_inside,
                           const auto& lhs_location, const auto& rhs_location,
                           const auto& inverse, bool cube_winding,
                           const std::vector<std::pair<axiom::VertexId,axiom::Point3>>& known_vertices) {
        const auto result = kernel.booleans().prepare_split_classification(lhs,rhs);
        if (!result.value || result.status != axiom::StatusCode::Ok || result.value->fragments.empty()) {
            std::cerr << "split reference lhs=" << lhs.value << " rhs=" << rhs.value
                      << " status=" << static_cast<int>(result.status) << "\n";
            const auto report = kernel.diagnostics().get(result.diagnostic_id);
            if (report.value) for (const auto& issue : report.value->issues)
                std::cerr << issue.stage << " " << issue.code << " " << issue.message << "\n";
            return planar_failure(__LINE__);
        }
        const auto& preparation = *result.value;
        const auto query = kernel.topology().query();
        const auto lhs_faces = query.faces_of_body(lhs), rhs_faces = query.faces_of_body(rhs);
        if (!lhs_faces.value || !rhs_faces.value) return planar_failure(__LINE__);
        std::array<double,2> totals {}, insides {};
        std::map<std::uint64_t,double> face_areas;
        std::map<std::array<long long,8>,std::vector<std::array<std::size_t,3>>> subdivision_edges;
        std::size_t checked_source_interpolation = 0;
        for (std::size_t index = 0; index < preparation.fragments.size(); ++index) {
            const auto& fragment = preparation.fragments[index];
            const bool left = fragment.source_body == lhs;
            if (!left && fragment.source_body != rhs) return planar_failure(__LINE__);
            const auto& faces = left ? *lhs_faces.value : *rhs_faces.value;
            if (std::find(faces.begin(),faces.end(),fragment.source_face) == faces.end())
                return planar_failure(__LINE__);
            const double triangle_area = area(fragment.vertices);
            if (!std::isfinite(triangle_area) || triangle_area <= 1e-12) return planar_failure(__LINE__);
            face_areas[fragment.source_face.value] += triangle_area;
            totals[left ? 0 : 1] += triangle_area;
            axiom::Point3 centroid {};
            for (const auto p : fragment.vertices) {
                centroid.x += p.x/3; centroid.y += p.y/3; centroid.z += p.z/3;
            }
            if ((left ? lhs_location(inverse(centroid)) : rhs_location(inverse(centroid))) != Location::Boundary)
                return planar_failure(__LINE__);
            const auto location = left ? rhs_location(inverse(centroid)) : lhs_location(inverse(centroid));
            if (fragment.classification.location != location || location == Location::Boundary ||
                !fragment.classification.boundary_faces.empty()) return planar_failure(__LINE__);
            if (location == Location::Inside) insides[left ? 0 : 1] += triangle_area;
            // Interior samples toward every corner must remain in the same
            // material class; checking just a triangle centroid misses straddles.
            for (const auto p : fragment.vertices) {
                const axiom::Point3 sample {0.2*centroid.x+0.8*p.x,0.2*centroid.y+0.8*p.y,
                                           0.2*centroid.z+0.8*p.z};
                const auto sample_location = left ? rhs_location(inverse(sample)) : lhs_location(inverse(sample));
                if (sample_location != location) return planar_failure(__LINE__);
            }
            if (cube_winding) {
                const auto p = inverse(fragment.vertices[0]), q = inverse(fragment.vertices[1]),
                           r = inverse(fragment.vertices[2]);
                const std::array<double,3> normal {(q.y-p.y)*(r.z-p.z)-(q.z-p.z)*(r.y-p.y),
                    (q.z-p.z)*(r.x-p.x)-(q.x-p.x)*(r.z-p.z),
                    (q.x-p.x)*(r.y-p.y)-(q.y-p.y)*(r.x-p.x)};
                const auto c = inverse(centroid);
                const std::array<double,3> coords {c.x,c.y,c.z};
                const double low = left ? 0 : 1, high = low+2;
                bool outward = false;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    if (std::abs(coords[axis]-low) < 1e-8 && normal[axis] < -1e-12) outward = true;
                    if (std::abs(coords[axis]-high) < 1e-8 && normal[axis] > 1e-12) outward = true;
                }
                if (!outward) return planar_failure(__LINE__);
            }
            for (std::size_t side = 0; side < 3; ++side) if (fragment.source_edges[side].value != 0) {
                const auto owners = query.faces_of_edge(fragment.source_edges[side]);
                if (!owners.value || std::find(owners.value->begin(),owners.value->end(),fragment.source_face) ==
                    owners.value->end()) return planar_failure(__LINE__);
                for (const auto fraction : {fragment.source_edge_begin[side],fragment.source_edge_end[side]})
                    if (!std::isfinite(fraction) || fraction < -1e-8 || fraction > 1+1e-8)
                        return planar_failure(__LINE__);
                if (left && !known_vertices.empty()) {
                    const auto endpoints = query.vertices_of_edge(fragment.source_edges[side]);
                    if (!endpoints.value) return planar_failure(__LINE__);
                    std::array<axiom::Point3,2> points {};
                    for (std::size_t end = 0; end < 2; ++end) {
                        const auto found = std::find_if(known_vertices.begin(),known_vertices.end(),
                            [&](const auto& vertex) { return vertex.first == (*endpoints.value)[end]; });
                        if (found == known_vertices.end()) return planar_failure(__LINE__);
                        points[end] = found->second;
                    }
                    for (const bool begin : {false,true}) {
                        const auto fraction = begin ? fragment.source_edge_begin[side] : fragment.source_edge_end[side];
                        const auto p = points[0], q = points[1];
                        const axiom::Point3 expected {p.x+fraction*(q.x-p.x),p.y+fraction*(q.y-p.y),p.z+fraction*(q.z-p.z)};
                        if (distance(expected,fragment.vertices[begin ? side : (side+1)%3]) > 1e-8)
                            return planar_failure(__LINE__);
                        ++checked_source_interpolation;
                    }
                }
            }
            for (std::size_t side = 0; side < 3; ++side) {
                const auto p = fragment.vertices[side], q = fragment.vertices[(side+1)%3];
                auto a = std::array{std::llround(p.x*1e8),std::llround(p.y*1e8),std::llround(p.z*1e8)};
                auto b = std::array{std::llround(q.x*1e8),std::llround(q.y*1e8),std::llround(q.z*1e8)};
                const bool reversed = b < a;
                if (reversed) std::swap(a,b);
                subdivision_edges[{static_cast<long long>(fragment.source_body.value),
                    static_cast<long long>(fragment.source_face.value),a[0],a[1],a[2],b[0],b[1],b[2]}]
                    .push_back({index,side,static_cast<std::size_t>(reversed)});
            }
            for (const auto adjacent : fragment.adjacent_fragments) {
                if (adjacent >= preparation.fragments.size() || adjacent == index) return planar_failure(__LINE__);
                const auto& other = preparation.fragments[adjacent];
                if (other.source_body != fragment.source_body ||
                    std::find(other.adjacent_fragments.begin(),other.adjacent_fragments.end(),index) ==
                    other.adjacent_fragments.end()) return planar_failure(__LINE__);
                bool whole_edge = false;
                for (std::size_t a = 0; a < 3; ++a) for (std::size_t b = 0; b < 3; ++b)
                    if (distance(fragment.vertices[a],other.vertices[(b+1)%3]) < 1e-8 &&
                        distance(fragment.vertices[(a+1)%3],other.vertices[b]) < 1e-8) whole_edge = true;
                if (!whole_edge) return planar_failure(__LINE__);
            }
            for (const auto segment_index : fragment.intersection_segments) {
                if (segment_index >= preparation.intersection.segments.size()) return planar_failure(__LINE__);
                const auto& segment = preparation.intersection.segments[segment_index];
                if (fragment.source_face != (left ? segment.lhs_face : segment.rhs_face))
                    return planar_failure(__LINE__);
                bool touches = false;
                for (std::size_t side = 0; side < 3; ++side) {
                    const auto range = overlap(fragment.vertices[side],fragment.vertices[(side+1)%3],segment);
                    if (range[1]-range[0] > 1e-8) touches = true;
                    for (const auto p : {fragment.vertices[side],fragment.vertices[(side+1)%3]}) {
                        const axiom::Vec3 d {segment.end.x-segment.begin.x,segment.end.y-segment.begin.y,
                                            segment.end.z-segment.begin.z};
                        const double squared = d.x*d.x+d.y*d.y+d.z*d.z;
                        if (squared <= 1e-18) {
                            if (distance(p,segment.begin) < 1e-8) touches = true;
                        } else {
                            const double parameter = ((p.x-segment.begin.x)*d.x+(p.y-segment.begin.y)*d.y+
                                                      (p.z-segment.begin.z)*d.z)/squared;
                            const axiom::Point3 projected {segment.begin.x+parameter*d.x,segment.begin.y+parameter*d.y,
                                                           segment.begin.z+parameter*d.z};
                            if (parameter >= -1e-8 && parameter <= 1+1e-8 && distance(p,projected) < 1e-8)
                                touches = true;
                        }
                    }
                }
                if (!touches) {
                    std::cerr << "fragment " << index << " segment " << segment_index << " finite contact reference\n";
                    for (const auto p : fragment.vertices) std::cerr << "triangle " << p.x << " " << p.y << " " << p.z << "\n";
                    for (const auto p : {segment.begin,segment.end}) std::cerr << "segment " << p.x << " " << p.y << " " << p.z << "\n";
                    return planar_failure(__LINE__);
                }
            }
        }
        for (const auto& [edge,owners] : subdivision_edges) {
            if (owners.size() == 1) {
                if (preparation.fragments[owners[0][0]].source_edges[owners[0][1]].value == 0)
                    return planar_failure(__LINE__);
            } else if (owners.size() == 2) {
                if (owners[0][2] == owners[1][2]) return planar_failure(__LINE__);
                for (const bool first : {false,true}) {
                    const auto owner = owners[first ? 0 : 1][0], neighbor = owners[first ? 1 : 0][0];
                    const auto& adjacent = preparation.fragments[owner].adjacent_fragments;
                    if (std::find(adjacent.begin(),adjacent.end(),neighbor) == adjacent.end())
                        return planar_failure(__LINE__);
                }
            } else return planar_failure(__LINE__);
        }
        if (std::abs(totals[0]-lhs_area) > 1e-7 || std::abs(totals[1]-rhs_area) > 1e-7 ||
            std::abs(insides[0]-lhs_inside) > 1e-7 || std::abs(insides[1]-rhs_inside) > 1e-7)
            return planar_failure(__LINE__);
        for (const auto* faces : {&*lhs_faces.value,&*rhs_faces.value}) for (const auto face : *faces) {
            const auto original = query.planar_face_area(face);
            if (!original.value || std::abs(face_areas[face.value]-*original.value) > 1e-7)
                return planar_failure(__LINE__);
        }
        if (!known_vertices.empty() && checked_source_interpolation == 0) return planar_failure(__LINE__);
        for (std::size_t segment_index = 0; segment_index < preparation.intersection.segments.size(); ++segment_index) {
            const auto& segment = preparation.intersection.segments[segment_index];
            if (segment.point_contact) continue;
            for (const bool left : {false,true}) {
                std::vector<std::array<double,2>> intervals;
                for (const auto& fragment : preparation.fragments) {
                    if (fragment.source_body != (left ? lhs : rhs) ||
                        fragment.source_face != (left ? segment.lhs_face : segment.rhs_face)) continue;
                    for (std::size_t side = 0; side < 3; ++side) {
                        const auto range = overlap(fragment.vertices[side],fragment.vertices[(side+1)%3],segment);
                        if (range[1]-range[0] > 1e-8) {
                            if (std::find(fragment.intersection_segments.begin(),fragment.intersection_segments.end(),segment_index) ==
                                fragment.intersection_segments.end()) return planar_failure(__LINE__);
                            intervals.push_back(range);
                        }
                    }
                }
                std::sort(intervals.begin(),intervals.end());
                double end = 0;
                for (const auto interval : intervals) {
                    if (interval[0] > end+1e-8) return planar_failure(__LINE__);
                    end = std::max(end,interval[1]);
                }
                if (std::abs(end-1) > 1e-8) return planar_failure(__LINE__);
            }
        }
        for (const auto body : {lhs,rhs}) {
            const auto edges = query.edges_of_body(body);
            if (!edges.value) return planar_failure(__LINE__);
            for (const auto edge : *edges.value) {
                std::vector<std::array<double,2>> intervals;
                const auto expected_owners = query.faces_of_edge(edge);
                if (!expected_owners.value) return planar_failure(__LINE__);
                for (const auto& fragment : preparation.edge_fragments) {
                    if (fragment.source_body != body || fragment.source_edge != edge) continue;
                    if (fragment.begin_fraction < 0 || fragment.end_fraction > 1 ||
                        fragment.end_fraction <= fragment.begin_fraction ||
                        distance(fragment.begin,fragment.end) <= 1e-9) return planar_failure(__LINE__);
                    auto owners = fragment.adjacent_faces, expected = *expected_owners.value;
                    const auto less = [](axiom::FaceId a, axiom::FaceId b) { return a.value < b.value; };
                    std::sort(owners.begin(),owners.end(),less); std::sort(expected.begin(),expected.end(),less);
                    if (owners != expected) return planar_failure(__LINE__);
                    if (body == lhs && !known_vertices.empty()) {
                        const auto endpoints = query.vertices_of_edge(edge);
                        if (!endpoints.value) return planar_failure(__LINE__);
                        std::array<axiom::Point3,2> points {};
                        for (std::size_t end = 0; end < 2; ++end) {
                            const auto found = std::find_if(known_vertices.begin(),known_vertices.end(),
                                [&](const auto& vertex) { return vertex.first == (*endpoints.value)[end]; });
                            if (found == known_vertices.end()) return planar_failure(__LINE__);
                            points[end] = found->second;
                        }
                        for (const bool begin : {false,true}) {
                            const auto fraction = begin ? fragment.begin_fraction : fragment.end_fraction;
                            const auto p = points[0], q = points[1];
                            const axiom::Point3 interpolated {p.x+fraction*(q.x-p.x),p.y+fraction*(q.y-p.y),
                                                             p.z+fraction*(q.z-p.z)};
                            if (distance(interpolated,begin ? fragment.begin : fragment.end) > 1e-8)
                                return planar_failure(__LINE__);
                        }
                    }
                    intervals.push_back({fragment.begin_fraction,fragment.end_fraction});
                }
                std::sort(intervals.begin(),intervals.end());
                double end = 0;
                for (const auto interval : intervals) {
                    if (std::abs(interval[0]-end) > 1e-8) return planar_failure(__LINE__);
                    end = interval[1];
                }
                if (std::abs(end-1) > 1e-8) return planar_failure(__LINE__);
            }
        }
        for (const auto& segment : preparation.intersection.segments) for (const bool begin : {false,true}) {
            const auto& hits = begin ? segment.begin_hits : segment.end_hits;
            const auto point = begin ? segment.begin : segment.end;
            for (const auto& hit : hits) {
                bool knot_found = false;
                for (const auto& edge : preparation.edge_fragments) {
                    if (edge.source_edge != hit.edge) continue;
                    if (std::abs(edge.begin_fraction-hit.edge_fraction) < 1e-8 && distance(edge.begin,point) < 1e-8)
                        knot_found = true;
                    if (std::abs(edge.end_fraction-hit.edge_fraction) < 1e-8 && distance(edge.end,point) < 1e-8)
                        knot_found = true;
                }
                if (!knot_found) return planar_failure(__LINE__);
            }
        }
        return true;
    };
    const auto box_location = [](axiom::Point3 p, double low, double high) {
        const std::array<double,3> coordinates {p.x,p.y,p.z};
        bool boundary = false;
        for (const auto coordinate : coordinates) {
            if (coordinate < low-1e-8 || coordinate > high+1e-8) return Location::Outside;
            if (std::abs(coordinate-low) < 1e-8 || std::abs(coordinate-high) < 1e-8) boundary = true;
        }
        return boundary ? Location::Boundary : Location::Inside;
    };
    const auto lhs_location = [&](axiom::Point3 p) { return box_location(p,0,2); };
    const auto rhs_location = [&](axiom::Point3 p) { return box_location(p,1,3); };
    const auto identity = [](axiom::Point3 p) { return p; };
    const auto a = kernel.primitives().box({0,0,0},2,2,2), b = kernel.primitives().box({1,1,1},2,2,2);
    if (!a.value || !b.value ||
        !check(*a.value,*b.value,24,24,3,3,lhs_location,rhs_location,identity,true,{}))
        return planar_failure(__LINE__);
    const auto rotate = [](axiom::Point3 p) -> axiom::Point3 {
        const double x = 0.6*p.x-0.8*p.y, y = 0.8*p.x+0.6*p.y;
        return {x,(12*y-5*p.z)/13,(5*y+12*p.z)/13};
    };
    const auto inverse = [](axiom::Point3 p) -> axiom::Point3 {
        const double y = (12*p.y+5*p.z)/13, z = (-5*p.y+12*p.z)/13;
        return {0.6*p.x+0.8*y,-0.8*p.x+0.6*y,z};
    };
    const auto make_rotated = [&](double low) {
        axiom::ProfileRef profile;
        profile.label = "s4-split-rotated-reference-box";
        for (const auto p : {axiom::Point3{low,low,low},axiom::Point3{low+2,low,low},
                            axiom::Point3{low+2,low+2,low},axiom::Point3{low,low+2,low}})
            profile.polygon_xyz.push_back(rotate(p));
        return kernel.sweeps().extrude(profile,{0,-5.0/13,12.0/13},2);
    };
    const auto ra = make_rotated(0), rb = make_rotated(1);
    if (!ra.value || !rb.value) {
        for (const auto* result : {&ra,&rb}) {
            std::cerr << "rotated reference construction status=" << static_cast<int>(result->status) << "\n";
            const auto report = kernel.diagnostics().get(result->diagnostic_id);
            if (report.value) for (const auto& issue : report.value->issues)
                std::cerr << issue.stage << " " << issue.code << " " << issue.message << "\n";
        }
    }
    if (!ra.value || !rb.value ||
        !check(*ra.value,*rb.value,24,24,3,3,lhs_location,rhs_location,inverse,true,{}))
        return planar_failure(__LINE__);
    const auto cutter = kernel.primitives().box({-1,1.5,-0.5},6,1,1);
    if (!cutter.value) return planar_failure(__LINE__);
    for (const bool holed : {false,true}) {
        axiom::ProfileRef profile;
        profile.label = "s4-split-reference-concave-U";
        profile.polygon_xyz = {{0,0,0},{4,0,0},{4,4,0},{3,4,0},{3,1,0},{1,1,0},{1,4,0},{0,4,0}};
        std::vector<std::pair<axiom::VertexId,axiom::Point3>> known;
        const auto prism = holed ? make_reference_holed_prism(kernel,1,&known)
                                  : kernel.sweeps().extrude(profile,{0,0,1},2);
        if (!prism.value) return planar_failure(__LINE__);
        const auto prism_location = [holed](axiom::Point3 p) {
            if (p.x < -1e-8 || p.x > 4+1e-8 || p.y < -1e-8 || p.y > 4+1e-8 ||
                p.z < -1e-8 || p.z > 2+1e-8) return Location::Outside;
            if (p.x > 1+1e-8 && p.x < 3-1e-8 && p.y > 1+1e-8 && (!holed || p.y < 3-1e-8))
                return Location::Outside;
            const bool notch_side = (std::abs(p.x-1) < 1e-8 || std::abs(p.x-3) < 1e-8) &&
                                     p.y >= 1-1e-8 && (!holed || p.y <= 3+1e-8);
            const bool notch_bottom = std::abs(p.y-1) < 1e-8 && p.x >= 1-1e-8 && p.x <= 3+1e-8;
            const bool hole_top = holed && std::abs(p.y-3) < 1e-8 && p.x >= 1-1e-8 && p.x <= 3+1e-8;
            if (p.x < 1e-8 || p.x > 4-1e-8 || p.y < 1e-8 || p.y > 4-1e-8 ||
                p.z < 1e-8 || p.z > 2-1e-8 || notch_side || notch_bottom || hole_top)
                return Location::Boundary;
            return Location::Inside;
        };
        const auto cutter_location = [](axiom::Point3 p) {
            if (p.x < -1-1e-8 || p.x > 5+1e-8 || p.y < 1.5-1e-8 || p.y > 2.5+1e-8 ||
                p.z < -0.5-1e-8 || p.z > 0.5+1e-8) return Location::Outside;
            if (std::abs(p.x+1) < 1e-8 || std::abs(p.x-5) < 1e-8 || std::abs(p.y-1.5) < 1e-8 ||
                std::abs(p.y-2.5) < 1e-8 || std::abs(p.z+0.5) < 1e-8 || std::abs(p.z-0.5) < 1e-8)
                return Location::Boundary;
            return Location::Inside;
        };
        if (!check(*prism.value,*cutter.value,holed ? 72 : 64,26,4,4,
                   prism_location,cutter_location,identity,false,known)) return planar_failure(__LINE__);
        const std::array<axiom::Point3,5> points {{{0.5,2,1},{2,2,1},{2,0.5,1},{1,2,1},{5,2,1}}};
        const std::array<Location,5> expected {Location::Inside,Location::Outside,Location::Inside,
                                              Location::Boundary,Location::Outside};
        const auto classified = kernel.booleans().classify_points(*prism.value,points);
        if (!classified.value || classified.value->size() != points.size()) return planar_failure(__LINE__);
        for (std::size_t i = 0; i < points.size(); ++i) {
            if ((*classified.value)[i].location != expected[i] ||
                ((*classified.value)[i].boundary_faces.empty() != (expected[i] != Location::Boundary)))
                return planar_failure(__LINE__);
        }
    }
    // Containment has no intersection wire; source triangles still classify
    // from the actual opposite boundary.
    const auto inner = kernel.primitives().box({0.25,0.25,0.25},0.5,0.5,0.5);
    if (!inner.value) return planar_failure(__LINE__);
    const auto contained = kernel.booleans().prepare_split_classification(*a.value,*inner.value);
    if (!contained.value || !contained.value->intersection.segments.empty()) return planar_failure(__LINE__);
    std::array<double,2> areas {};
    for (const auto& fragment : contained.value->fragments) {
        const bool outer = fragment.source_body == *a.value;
        if (fragment.classification.location != (outer ? Location::Outside : Location::Inside))
            return planar_failure(__LINE__);
        areas[outer ? 0 : 1] += area(fragment.vertices);
    }
    if (std::abs(areas[0]-24) > 1e-8 || std::abs(areas[1]-1.5) > 1e-8) return planar_failure(__LINE__);
    const auto gap = kernel.primitives().box({2.0001,0.25,0.25},1,0.5,0.5);
    if (!gap.value) return planar_failure(__LINE__);
    const auto separated = kernel.booleans().prepare_split_classification(*a.value,*gap.value);
    if (!separated.value || !separated.value->intersection.segments.empty()) return planar_failure(__LINE__);
    for (const auto& fragment : separated.value->fragments)
        if (fragment.classification.location != Location::Outside) return planar_failure(__LINE__);
    const std::array<axiom::Point3,5> points {{{1,1,1},{2.0001,1,1},{1.9999,1,1},{2,1,1},{2,2,2}}};
    const auto classified = kernel.booleans().classify_points(*a.value,points);
    const std::array<Location,5> expected {Location::Inside,Location::Outside,Location::Inside,
                                          Location::Boundary,Location::Boundary};
    if (!classified.value || classified.value->size() != points.size()) return planar_failure(__LINE__);
    for (std::size_t i = 0; i < points.size(); ++i) {
        if ((*classified.value)[i].location != expected[i] ||
            (expected[i] == Location::Boundary && (*classified.value)[i].boundary_faces.empty()))
            return planar_failure(__LINE__);
    }
    return true;
}

}  // namespace

int main() {
    if (!check_split_classification_references()) {
        std::cerr << "planar boolean split/classification analytic regression\n";
        return 1;
    }
    if (!check_planar_intersection_references() || !check_planar_preparation_failure_isolation()) {
        std::cerr << "planar boolean preparation reference/isolation regression\n";
        return 1;
    }
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
    if (cls_issue == nullptr || cls_issue->stage != "bool.abort.classify" || cls_issue->numeric_evidence.empty()) {
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
    if (isect_issue == nullptr || isect_issue->stage != "bool.abort.intersect" ||
        isect_issue->numeric_evidence.empty()) {
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
    std::vector<axiom::DiagnosticId> failure_diagnostics;
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
                issue->related_entities != std::vector<std::uint64_t>{test.lhs.value, test.rhs.value} ||
                issue->numeric_evidence.size() < 3) {
                std::cerr << "boolean failure lost stage or input context\n";
                return 1;
            }
            failure_diagnostics.push_back(failed.diagnostic_id);
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
                json.find("\"numeric_evidence\":[") == std::string::npos ||
                json.find("\"name\":\"lhs_exists\"") == std::string::npos ||
                kernel.body_count().value != bodies_before.value ||
                kernel.geometry_count().value != geometry_before.value ||
                kernel.topology_count().value != topology_before.value) {
                std::cerr << "boolean failure export mismatch or model pollution\n";
                return 1;
            }
        }
    }

    axiom::DiagnosticEvidencePolicy bool_failure_policy;
    bool_failure_policy.issue_code_prefix = "AXM-BOOL-E-";
    bool_failure_policy.stage_prefix = "bool.";
    const auto first_failure_before = kernel.diagnostics().get(failure_diagnostics.front());
    const auto coverage = kernel.diagnostics().audit_evidence(failure_diagnostics, bool_failure_policy);
    if (!first_failure_before.value || coverage.status != axiom::StatusCode::Ok || !coverage.value ||
        !coverage.value->passed() || coverage.value->reports_inspected != failure_diagnostics.size() ||
        coverage.value->matching_issues != failure_diagnostics.size() ||
        coverage.value->complete_issues != failure_diagnostics.size() || !coverage.value->findings.empty()) {
        std::cerr << "boolean failure evidence coverage gate rejected a covered branch\n";
        return 1;
    }
    const auto coverage_path = std::filesystem::temp_directory_path() / "axiom_boolean_failure_coverage.json";
    if (kernel.diagnostics().export_evidence_audit_json(
            failure_diagnostics, bool_failure_policy, coverage_path.string()).status != axiom::StatusCode::Ok) {
        return 1;
    }
    std::ifstream coverage_in {coverage_path};
    const std::string coverage_json((std::istreambuf_iterator<char>(coverage_in)),
                                    std::istreambuf_iterator<char>());
    coverage_in.close();
    std::filesystem::remove(coverage_path);
    const auto first_failure_after = kernel.diagnostics().get(failure_diagnostics.front());
    if (coverage_json.find("\"passed\":true") == std::string::npos ||
        coverage_json.find("\"issues_missing_numeric_evidence\":0") == std::string::npos ||
        !first_failure_after.value ||
        first_failure_after.value->issues.size() != first_failure_before.value->issues.size() ||
        first_failure_after.value->issues.back().numeric_evidence.size() !=
            first_failure_before.value->issues.back().numeric_evidence.size() ||
        first_failure_after.value->issues.back().numeric_evidence.front().name !=
            first_failure_before.value->issues.back().numeric_evidence.front().name) {
        std::cerr << "boolean coverage export changed a source report\n";
        return 1;
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
