#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

// Heal observation must retain the manufactured wall and real inner boundary.
// The 4x5x6 stock and t=0.5 give closed V=120-3*4*5=60, A=148+94=242;
// an upper opening gives V=120-3*4*5.5=54, A=148+101-2*3*4=225.
bool offset_shell_heal_regression() {
    axiom::Kernel kernel;
    const auto stock=kernel.primitives().box({0,0,0},4,5,6);
    if (!stock.value) return false;
    auto& query=kernel.topology().query();
    const auto faces=query.faces_of_body(*stock.value);
    if (!faces.value) return false;
    axiom::FaceId top {};
    for (const auto face : *faces.value) {
        const auto bbox=query.bbox_of_face(face);
        if (bbox.value && std::abs(bbox.value->min.z-6)<1e-9 && std::abs(bbox.value->max.z-6)<1e-9) top=face;
    }
    if (!top.value) return false;
    for (const bool open : {false,true}) {
        const std::vector<axiom::FaceId> removed=open ? std::vector<axiom::FaceId>{top} : std::vector<axiom::FaceId>{};
        const auto result=kernel.modify().shell_body(*stock.value,removed,.5);
        if (!result.value) return false;
        const auto body=result.value->output;
        const auto source_faces=query.faces_of_body(body).value;
        const auto source_edges=query.edges_of_body(body).value;
        const auto validation=kernel.validate().validate_all(body,axiom::ValidationMode::Strict);
        if (validation.status!=axiom::StatusCode::Ok ||
            kernel.validate().validate_self_intersection_all_shells(body,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok ||
            kernel.topology().validate().validate_body_trim_consistency(body).status!=axiom::StatusCode::Ok) return false;
        const auto repaired=kernel.repair().auto_repair(body,axiom::RepairMode::ReportOnly);
        if (!repaired.value || repaired.value->output!=body) {
            const auto report=kernel.diagnostics().get(repaired.diagnostic_id);
            if (report.value) for (const auto& issue : report.value->issues) std::cerr << issue.code << ' ' << issue.stage << '\n';
            return false;
        }
        for (const auto checked : {body,repaired.value->output}) {
            const auto mass=query.body_mass_properties(checked);
            const auto void_point=query.locate_point(checked,{2,2.5,3});
            const auto wall=query.locate_point(checked,{.25,2.5,3});
            const auto boundary=query.locate_point(checked,{.5,2.5,3});
            if (!mass.value || std::abs(mass.value->volume-(open ? 54 : 60))>1e-7 ||
                std::abs(mass.value->area-(open ? 225 : 242))>1e-7 ||
                !void_point.value || void_point.value->location!=axiom::BodyPointLocation::Outside ||
                !wall.value || wall.value->location!=axiom::BodyPointLocation::Inside ||
                !boundary.value || boundary.value->location!=axiom::BodyPointLocation::Boundary ||
                kernel.validate().validate_all(checked,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok) return false;
        }
        if (query.faces_of_body(body).value!=source_faces || query.edges_of_body(body).value!=source_edges) return false;
    }
    return kernel.validate().validate_all(*stock.value,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok;
}

// Fixed first-generation sewing corpus: six disconnected planar patches of a
// 2x3x4 box. Only the top patch moves, and one side is deliberately reversed.
// Coordinates, incidence defects and displacement bounds are input oracles;
// they do not come from the repair planner or a bounding-box volume estimate.
axiom::Result<axiom::BodyId> disconnected_planar_box(axiom::Kernel& kernel, double gap,
                                                   bool omit_top = false) {
    std::array<std::array<axiom::Point3,4>,6> rings {{{{{0,0,0},{0,3,0},{2,3,0},{2,0,0}}},
        {{{0,0,0},{2,0,0},{2,0,4},{0,0,4}}}, {{{2,0,0},{2,3,0},{2,3,4},{2,0,4}}},
        {{{2,3,0},{0,3,0},{0,3,4},{2,3,4}}}, {{{0,3,0},{0,0,0},{0,0,4},{0,3,4}}},
        {{{0,0,4+gap},{2,0,4+gap},{2,3,4+gap},{0,3,4+gap}}}}};
    std::reverse(rings[2].begin(),rings[2].end());
    auto transaction = kernel.topology().begin_transaction();
    std::vector<axiom::FaceId> faces;
    for (std::size_t f = 0; f < (omit_top ? 5u : 6u); ++f) {
        const auto& p = rings[f];
        const axiom::Vec3 u {p[1].x-p[0].x,p[1].y-p[0].y,p[1].z-p[0].z};
        const axiom::Vec3 v {p[2].x-p[0].x,p[2].y-p[0].y,p[2].z-p[0].z};
        const auto plane = kernel.surfaces().make_plane(p[0],
            {u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x});
        if (!plane.value) return {};
        std::array<axiom::VertexId,4> vertices;
        std::array<axiom::CoedgeId,4> coedges;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto vertex = transaction.create_vertex(p[i]);
            if (!vertex.value) return {};
            vertices[i] = *vertex.value;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            const auto next = (i+1)%4;
            const auto curve = kernel.curves().make_line_segment(p[i],p[next]);
            const auto edge = curve.value ? transaction.create_edge(*curve.value,vertices[i],vertices[next])
                                          : axiom::Result<axiom::EdgeId>{};
            const auto coedge = edge.value ? transaction.create_coedge(*edge.value,false)
                                           : axiom::Result<axiom::CoedgeId>{};
            if (!coedge.value) return {};
            coedges[i] = *coedge.value;
        }
        const auto loop = transaction.create_loop(coedges);
        const auto face = loop.value ? transaction.create_face(*plane.value,*loop.value,{})
                                      : axiom::Result<axiom::FaceId>{};
        if (!face.value) return {};
        faces.push_back(*face.value);
    }
    const auto shell = transaction.create_shell(faces);
    const auto body = shell.value ? transaction.create_body(std::array {*shell.value})
                                  : axiom::Result<axiom::BodyId>{};
    if (!body.value || transaction.commit().status != axiom::StatusCode::Ok) return {};
    return body;
}

bool planar_import_repair_regression() {
    // Binary-exact tolerance makes the equality boundary reproducible.
    constexpr double tolerance = 1.0/1024;
    for (const double gap : {0.0,tolerance/2,tolerance,tolerance*1.01}) {
        axiom::KernelConfig config;
        config.tolerance.linear = tolerance;
        axiom::Kernel kernel {config};
        axiom::DiagnosticId last_diagnostic {};
        const auto fail = [&](int line) {
            std::cerr << "planar_import_repair_regression line=" << line << "\n";
            const auto diagnostic = kernel.diagnostics().get(last_diagnostic);
            if (diagnostic.value) for (const auto& issue : diagnostic.value->issues) {
                std::cerr << issue.stage << " " << issue.code << " " << issue.message << "\n";
                for (const auto& evidence : issue.numeric_evidence)
                    std::cerr << "  " << evidence.name << "=" << evidence.value << " " << evidence.unit << "\n";
            }
            return false;
        };
        const auto input = disconnected_planar_box(kernel,gap);
        if (!input.value) return fail(__LINE__);
        const auto body = *input.value;
        auto& query = kernel.topology().query();
        const auto input_faces = query.faces_of_body(body).value;
        const auto input_edges = query.edges_of_body(body).value;
        // Six unsewn quads have 24 one-sided edges and 24 independent vertices.
        if (!input_faces || input_faces->size() != 6 || !input_edges || input_edges->size() != 24 ||
            query.vertex_count_of_body(body).value != std::optional<std::uint64_t>{24} ||
            query.boundary_edge_count_of_body(body).value != std::optional<std::uint64_t>{24} ||
            kernel.validate().validate_all(body,axiom::ValidationMode::Standard).status == axiom::StatusCode::Ok)
            return fail(__LINE__);
        for (const auto edge : *input_edges) {
            const auto incident_faces = query.faces_of_edge(edge);
            const auto uses = query.coedges_of_edge(edge);
            if (!incident_faces.value || incident_faces.value->size() != 1 || !uses.value || uses.value->size() != 1)
                return fail(__LINE__);
        }
        const auto bound = kernel.eval_graph().register_node(axiom::NodeKind::Analysis,
            "body:"+std::to_string(body.value));
        const auto consumer = kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"heal:consumer");
        if (!bound.value || !consumer.value || kernel.eval_graph().add_dependency(*consumer.value,*bound.value).status !=
            axiom::StatusCode::Ok) return fail(__LINE__);
        const auto object_count = kernel.object_count_total().value;
        const auto geometry_count = kernel.geometry_count().value;
        const auto runtime = kernel.runtime_store_counts().value;
        const auto unchanged = [&] {
            const auto after = kernel.runtime_store_counts().value;
            return after && runtime && kernel.object_count_total().value == object_count &&
                kernel.geometry_count().value == geometry_count && query.faces_of_body(body).value == input_faces &&
                query.edges_of_body(body).value == input_edges &&
                after->mesh_records == runtime->mesh_records &&
                after->tessellation_cache_entries == runtime->tessellation_cache_entries &&
                after->face_tessellation_cache_entries == runtime->face_tessellation_cache_entries &&
                after->curve_eval_cache_entries == runtime->curve_eval_cache_entries &&
                after->surface_eval_cache_entries == runtime->surface_eval_cache_entries &&
                kernel.eval_graph().is_invalid(*bound.value).value == std::optional<bool>{false} &&
                kernel.eval_graph().is_invalid(*consumer.value).value == std::optional<bool>{false} &&
                kernel.eval_graph().recompute_count(*bound.value).value == std::optional<std::uint64_t>{0};
        };
        for (const auto mode : {axiom::RepairMode::ReportOnly,axiom::RepairMode::SuggestOnly}) {
            const auto report = kernel.repair().auto_repair(body,mode);
            last_diagnostic = report.diagnostic_id;
            if (!report.value || report.value->output != body || report.value->status == axiom::StatusCode::Ok ||
                !unchanged()) return fail(__LINE__);
        }
        for (int attempt = 0; attempt < (gap > tolerance ? 2 : 1); ++attempt) {
            const auto repaired = kernel.repair().auto_repair(body,axiom::RepairMode::Safe);
            last_diagnostic = repaired.diagnostic_id;
            const auto diagnostic = kernel.diagnostics().get(repaired.diagnostic_id);
            if (gap > tolerance) {
                if (repaired.status == axiom::StatusCode::Ok || repaired.value || !diagnostic.value || !unchanged() ||
                    std::none_of(diagnostic.value->issues.begin(),diagnostic.value->issues.end(),[](const auto& issue) {
                        return issue.code == axiom::diag_codes::kHealAutoRepairFailure &&
                            issue.stage == "heal.auto_repair.planar.orient" && issue.severity == axiom::IssueSeverity::Error &&
                            !issue.related_entities.empty() && !issue.numeric_evidence.empty() &&
                            std::all_of(issue.numeric_evidence.begin(),issue.numeric_evidence.end(),[](const auto& value) {
                                return std::isfinite(value.value);
                            });
                    })) return fail(__LINE__);
                continue;
            }
            if (!repaired.value || repaired.status != axiom::StatusCode::Ok || repaired.value->output == body ||
                !diagnostic.value) return fail(__LINE__);
            const auto output = repaired.value->output;
            const auto success = std::find_if(diagnostic.value->issues.begin(),diagnostic.value->issues.end(),[](const auto& issue) {
                return issue.code == axiom::diag_codes::kHealRepairValidated &&
                    issue.stage == "heal.auto_repair.planar.post_validate";
            });
            if (success == diagnostic.value->issues.end()) return fail(__LINE__);
            const auto displacement = std::find_if(success->numeric_evidence.begin(),success->numeric_evidence.end(),[](const auto& value) {
                return value.name == "maximum_vertex_displacement";
            });
            if (displacement == success->numeric_evidence.end() || std::abs(displacement->value-gap) > 1e-12 ||
                displacement->value > tolerance) return fail(__LINE__);
            const auto faces = query.faces_of_body(output);
            const auto vertex_count = query.vertex_count_of_body(output).value;
            const auto edge_count = query.edge_count_of_body(output).value;
            const auto boundary_count = query.boundary_edge_count_of_body(output).value;
            const auto strict = kernel.validate().validate_all(output,axiom::ValidationMode::Strict);
            const auto bound_invalid = kernel.eval_graph().is_invalid(*bound.value).value;
            const auto consumer_invalid = kernel.eval_graph().is_invalid(*consumer.value).value;
            if (!faces.value || faces.value->size() != 6 ||
                vertex_count != std::optional<std::uint64_t>{8} || edge_count != std::optional<std::uint64_t>{12} ||
                boundary_count != std::optional<std::uint64_t>{12} || strict.status != axiom::StatusCode::Ok ||
                bound_invalid != std::optional<bool>{true} || consumer_invalid != std::optional<bool>{true}) {
                std::cerr << "gap=" << gap << " faces=" << (faces.value ? faces.value->size() : 0)
                          << " vertices=" << vertex_count.value_or(0) << " edges=" << edge_count.value_or(0)
                          << " boundary=" << boundary_count.value_or(0) << " Strict=" << static_cast<int>(strict.status)
                          << " bound_invalid=" << bound_invalid.value_or(false)
                          << " consumer_invalid=" << consumer_invalid.value_or(false) << "\n";
                if (strict.status != axiom::StatusCode::Ok) last_diagnostic = strict.diagnostic_id;
                return fail(__LINE__);
            }
            // boundary_edge_count currently counts single-shell ownership;
            // actual closure is proved independently by two distinct incident
            // faces and oppositely directed uses of every shared edge.
            const auto output_edges = query.edges_of_body(output);
            if (!output_edges.value || output_edges.value->size() != 12) return fail(__LINE__);
            for (const auto edge : *output_edges.value) {
                const auto adjacent = query.faces_of_edge(edge);
                const auto uses = query.coedges_of_edge(edge);
                const auto loops = query.loops_of_edge(edge);
                if (!adjacent.value || adjacent.value->size() != 2 || adjacent.value->at(0) == adjacent.value->at(1) ||
                    !uses.value || uses.value->size() != 2 || uses.value->at(0) == uses.value->at(1) ||
                    !loops.value || loops.value->size() != 2) return fail(__LINE__);
                std::array<std::array<axiom::VertexId,2>,2> directed {};
                for (std::size_t i = 0; i < 2; ++i) {
                    const auto edges = query.edges_of_loop(loops.value->at(i));
                    const auto vertices = query.vertices_of_loop(loops.value->at(i));
                    if (!edges.value || !vertices.value || edges.value->size() != vertices.value->size() ||
                        vertices.value->empty()) return fail(__LINE__);
                    const auto found = std::find(edges.value->begin(),edges.value->end(),edge);
                    if (found == edges.value->end() || std::count(edges.value->begin(),edges.value->end(),edge) != 1)
                        return fail(__LINE__);
                    const auto index = static_cast<std::size_t>(found-edges.value->begin());
                    directed[i] = {vertices.value->at(index),vertices.value->at((index+1)%vertices.value->size())};
                }
                if (directed[0][0] != directed[1][1] || directed[0][1] != directed[1][0]) return fail(__LINE__);
            }
            // Direct public-ring triangle integrals: signed volume, area and all
            // eight expected corners, independently of mass/query bbox helpers.
            double signed_volume = 0, area = 0;
            std::array<bool,8> corners {};
            for (const auto face : *faces.value) {
                const auto surface = query.surface_of_face(face);
                const auto loops = query.loops_of_face(face);
                if (!surface.value || !loops.value || loops.value->size() != 1) return fail(__LINE__);
                const auto uv = query.face_loop_uv_polyline(face,loops.value->front());
                if (!uv.value || uv.value->size() < 4) return fail(__LINE__);
                std::vector<axiom::Point3> points;
                for (const auto p : *uv.value) {
                    const auto world = kernel.surface_service().eval(*surface.value,p.x,p.y,0);
                    if (!world.value) return fail(__LINE__);
                    const auto q = world.value->point;
                    const int x = std::abs(q.x) < 1e-9 ? 0 : std::abs(q.x-2) < 1e-9 ? 1 : -1;
                    const int y = std::abs(q.y) < 1e-9 ? 0 : std::abs(q.y-3) < 1e-9 ? 1 : -1;
                    const int z = std::abs(q.z) < 1e-9 ? 0 : std::abs(q.z-4) < 1e-9 ? 1 : -1;
                    if (x < 0 || y < 0 || z < 0) return fail(__LINE__);
                    corners[static_cast<std::size_t>(x+2*y+4*z)] = true;
                    points.push_back(q);
                }
                const auto p = points.front();
                for (std::size_t i = 1; i+1 < points.size(); ++i) {
                    const auto q = points[i], r = points[i+1];
                    area += std::hypot((q.y-p.y)*(r.z-p.z)-(q.z-p.z)*(r.y-p.y),
                        (q.z-p.z)*(r.x-p.x)-(q.x-p.x)*(r.z-p.z),
                        (q.x-p.x)*(r.y-p.y)-(q.y-p.y)*(r.x-p.x))/2;
                    signed_volume += (p.x*(q.y*r.z-q.z*r.y)+p.y*(q.z*r.x-q.x*r.z)+p.z*(q.x*r.y-q.y*r.x))/6;
                }
            }
            if (std::abs(signed_volume-24) > 1e-8 || std::abs(area-52) > 1e-8 ||
                !std::all_of(corners.begin(),corners.end(),[](bool present) { return present; }) ||
                query.faces_of_body(body).value != input_faces || query.edges_of_body(body).value != input_edges ||
                query.boundary_edge_count_of_body(body).value != std::optional<std::uint64_t>{24} ||
                kernel.validate().validate_all(body,axiom::ValidationMode::Standard).status == axiom::StatusCode::Ok)
                return fail(__LINE__);
        }
    }
    return true;
}


// Invalid angular policy lets valid planar sewing reach the actual Strict
// postcondition. This is a natural after-allocation failure, not an injected
// test hook; all derived records and invalidation must be undone together.
bool planar_post_validation_rollback_regression() {
    axiom::KernelConfig config;
    config.tolerance.angular = 0;
    axiom::Kernel kernel {config};
    const auto source = disconnected_planar_box(kernel,0.0);
    if (!source.value) return false;
    auto& query = kernel.topology().query();
    const auto faces = query.faces_of_body(*source.value).value;
    const auto edges = query.edges_of_body(*source.value).value;
    const auto node = kernel.eval_graph().register_node(axiom::NodeKind::Analysis,
        "body:"+std::to_string(source.value->value));
    const auto consumer = kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"heal:postcondition_consumer");
    if (!node.value || !consumer.value || kernel.eval_graph().add_dependency(*consumer.value,*node.value).status !=
        axiom::StatusCode::Ok) return false;
    const auto objects = kernel.object_count_total().value;
    const auto geometry = kernel.geometry_count().value;
    const auto bodies = kernel.body_count().value;
    const auto next_version = kernel.topology_version_next().value;
    const auto runtime = kernel.runtime_store_counts().value;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto repaired = kernel.repair().auto_repair(*source.value,axiom::RepairMode::Safe);
        const auto report = kernel.diagnostics().get(repaired.diagnostic_id);
        const auto after = kernel.runtime_store_counts().value;
        if (repaired.status == axiom::StatusCode::Ok || repaired.value || !report.value || !runtime || !after ||
            kernel.object_count_total().value != objects || kernel.geometry_count().value != geometry ||
            kernel.body_count().value != bodies || kernel.topology_version_next().value != next_version ||
            query.faces_of_body(*source.value).value != faces || query.edges_of_body(*source.value).value != edges ||
            after->mesh_records != runtime->mesh_records ||
            after->tessellation_cache_entries != runtime->tessellation_cache_entries ||
            after->face_tessellation_cache_entries != runtime->face_tessellation_cache_entries ||
            after->curve_eval_cache_entries != runtime->curve_eval_cache_entries ||
            after->surface_eval_cache_entries != runtime->surface_eval_cache_entries ||
            after->tessellation_metrics.body_cache_hits != runtime->tessellation_metrics.body_cache_hits ||
            after->tessellation_metrics.body_cache_misses != runtime->tessellation_metrics.body_cache_misses ||
            after->tessellation_metrics.face_cache_hits != runtime->tessellation_metrics.face_cache_hits ||
            after->tessellation_metrics.face_cache_misses != runtime->tessellation_metrics.face_cache_misses ||
            kernel.eval_graph().is_invalid(*node.value).value != std::optional<bool>{false} ||
            kernel.eval_graph().is_invalid(*consumer.value).value != std::optional<bool>{false} ||
            kernel.eval_graph().recompute_count(*node.value).value != std::optional<std::uint64_t>{0} ||
            std::none_of(report.value->issues.begin(),report.value->issues.end(),[](const auto& issue) {
                return issue.code == axiom::diag_codes::kHealAutoRepairFailure &&
                    issue.stage == "heal.auto_repair.planar.post_validate" && !issue.related_entities.empty() &&
                    std::any_of(issue.numeric_evidence.begin(),issue.numeric_evidence.end(),[](const auto& value) {
                        return value.name == "allocated_object_count" && value.value > 0 && std::isfinite(value.value);
                    }) && std::all_of(issue.numeric_evidence.begin(),issue.numeric_evidence.end(),[](const auto& value) {
                        return std::isfinite(value.value);
                    });
            })) return false;
    }
    // Retry the very same unmodified source after correcting the policy.
    if (kernel.set_angular_tolerance(1e-6).status != axiom::StatusCode::Ok) return false;
    const auto retry = kernel.repair().auto_repair(*source.value,axiom::RepairMode::Safe);
    return retry.value && retry.value->output != *source.value &&
        kernel.validate().validate_all(retry.value->output,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok &&
        query.faces_of_body(*source.value).value == faces && query.edges_of_body(*source.value).value == edges;
}

// Validate a reconstructed cavity as two genuine, oppositely oriented closed
// boundaries. This exercises Strict's cross-shell checks and trim validation.
bool rebuilt_cavity_validation_regression() {
    axiom::Kernel kernel;
    const auto outer = kernel.primitives().box({0,0,0},2,2,2);
    const auto inner = kernel.primitives().box({0.25,0.25,0.25},0.5,0.5,0.5);
    if (!outer.value || !inner.value) return false;
    const auto result = kernel.booleans().run_rebuilt(axiom::BooleanOp::Subtract,*outer.value,*inner.value);
    if (!result.value || !result.value->output) return false;
    const auto output = *result.value->output;
    const auto query = kernel.topology().query();
    const auto regions = query.body_shell_regions(output);
    if (!regions.value || regions.value->size() != 2 ||
        kernel.validate().validate_all(output,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
        kernel.validate().validate_self_intersection_all_shells(output,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
        kernel.topology().validate().validate_body_trim_consistency(output).status != axiom::StatusCode::Ok)
        return false;
    std::size_t material_shells = 0, void_shells = 0;
    for (const auto& region : *regions.value) {
        const auto mass = query.shell_mass_properties(region.shell);
        if (!mass.value) return false;
        if (region.role == axiom::BodyShellRole::Material) {
            if (region.nesting_depth != 0 || region.parent_shell || std::abs(mass.value->volume-8) > 1e-7) return false;
            ++material_shells;
        } else {
            if (region.nesting_depth != 1 || !region.parent_shell || std::abs(mass.value->volume-0.125) > 1e-7) return false;
            ++void_shells;
        }
    }
    if (material_shells != 1 || void_shells != 1 ||
        kernel.validate().validate_all(*outer.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
        kernel.validate().validate_all(*inner.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok)
        return false;
    const auto tunnel = kernel.primitives().box({0.25,0.25,0},0.5,0.5,2);
    const auto touching = kernel.primitives().box({2,0,0},2,2,2);
    if (!tunnel.value || !touching.value) return false;
    for (const bool through_hole : {false,true}) {
        const auto rebuilt = kernel.booleans().run_rebuilt(
            through_hole ? axiom::BooleanOp::Subtract : axiom::BooleanOp::Union,
            *outer.value,through_hole ? *tunnel.value : *touching.value);
        if (!rebuilt.value || !rebuilt.value->output) return false;
        const auto body = *rebuilt.value->output;
        const auto material = query.body_shell_regions(body);
        const auto mass = query.body_mass_properties(body);
        const auto section = query.section(body,{{0,0,1},{0,0,1}});
        const auto hole_point = query.locate_point(body,{0.5,0.5,1});
        // The tunnel is one genus-one shell, not a nested void shell. Its
        // rectangular section has S=3.75 and P=10, giving V=7.5,A=27.5.
        if (!material.value || material.value->size() != 1 ||
            material.value->front().role != axiom::BodyShellRole::Material ||
            material.value->front().nesting_depth != 0 || material.value->front().parent_shell ||
            !mass.value || std::abs(mass.value->volume-(through_hole ? 7.5 : 16)) > 1e-7 ||
            std::abs(mass.value->area-(through_hole ? 27.5 : 40)) > 1e-7 ||
            !section.value || std::abs(section.value->area-(through_hole ? 3.75 : 8)) > 1e-7 ||
            !hole_point.value || hole_point.value->location != (through_hole ? axiom::BodyPointLocation::Outside
                                                                          : axiom::BodyPointLocation::Inside) ||
            kernel.validate().validate_all(body,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
            kernel.validate().validate_self_intersection_all_shells(body,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
            kernel.topology().validate().validate_body_trim_consistency(body).status != axiom::StatusCode::Ok)
            return false;
    }
    return true;
}

// Safe must preserve a genuine hole when joining coplanar subdivisions.
// An independent holed-prism section/perimeter oracle and signed public-ring
// integrals prevent a successful repair from silently replacing its boundary.
bool rebuilt_safe_hole_validation_regression() {
    axiom::Kernel kernel;
    axiom::DiagnosticId last_diagnostic {};
    const auto fail = [&](int line) {
        std::cerr << "Safe rebuilt hole line=" << line << "\n";
        const auto diagnostic = kernel.diagnostics().get(last_diagnostic);
        if (diagnostic.value) for (const auto& issue : diagnostic.value->issues)
            std::cerr << issue.stage << " " << issue.code << " " << issue.message << "\n";
        return false;
    };

    axiom::ProfileRef profile;
    profile.label = "s4-safe-repair-hole-boundary";
    profile.polygon_xyz = {{0,0,0},{4,0,0},{4,4,0},{0,4,0}};
    profile.holes_xyz = {{{1,1,0},{3,1,0},{3,3,0},{1,3,0}}};
    constexpr double overlap = 2e-4;
    const auto holed = kernel.sweeps().extrude(profile,{0,0,1},2);
    const auto extension = kernel.primitives().box({4-overlap,0,0},2,4,2);
    if (!holed.value || !extension.value || kernel.set_linear_tolerance(1e-3).status != axiom::StatusCode::Ok)
        return fail(__LINE__);
    axiom::BooleanRebuildOptions options;
    options.preparation.intersection.tolerance.linear = 1e-6;
    const auto direct = kernel.booleans().run_rebuilt(axiom::BooleanOp::Union,*holed.value,*extension.value,options);
    last_diagnostic = direct.diagnostic_id;
    const auto rejected = kernel.diagnostics().get(direct.diagnostic_id);
    if (direct.status == axiom::StatusCode::Ok || direct.value || !rejected.value ||
        std::none_of(rejected.value->issues.begin(),rejected.value->issues.end(),[](const auto& issue) {
            return issue.code == axiom::diag_codes::kBoolRebuildFailure && issue.stage == "bool.validate";
        }) || std::none_of(rejected.value->issues.begin(),rejected.value->issues.end(),[](const auto& issue) {
            if (issue.severity != axiom::IssueSeverity::Error) return false;
            if (issue.code == axiom::diag_codes::kValNearDuplicateVertices &&
                issue.stage == "heal.validate_geometry.near_duplicate_vertices") return true;
            if (issue.code == axiom::diag_codes::kValDegenerateGeometry &&
                (issue.stage == "heal.validate_geometry.edges" ||
                 issue.stage == "heal.validate_geometry.face_area")) return true;
            if (issue.stage != "heal.validate_topology.trim_consistency" ||
                (issue.code != axiom::diag_codes::kTopoFaceOuterLoopInvalid &&
                 issue.code != axiom::diag_codes::kTopoFaceInnerLoopInvalid)) return false;
            std::optional<double> area, threshold;
            for (const auto& evidence : issue.numeric_evidence) {
                if (evidence.name == "uv_loop_area") area = evidence.value;
                if (evidence.name == "uv_loop_area_threshold") threshold = evidence.value;
            }
            return area && threshold && std::isfinite(*area) && std::isfinite(*threshold) &&
                std::abs(*area) > 0 && *threshold > 0 && std::abs(*area) <= *threshold;
        })) return fail(__LINE__);
    options.auto_repair = true;
    const auto repaired = kernel.booleans().run_rebuilt(axiom::BooleanOp::Union,*holed.value,*extension.value,options);
    last_diagnostic = repaired.diagnostic_id;
    if (!repaired.value || !repaired.value->output || !repaired.value->repaired) return fail(__LINE__);
    const auto output = *repaired.value->output;
    const auto query = kernel.topology().query();
    const auto regions = query.body_shell_regions(output);
    const auto mass = query.body_mass_properties(output);
    axiom::BodySpatialQueryOptions query_options;
    query_options.position_tolerance = 1e-6;
    const auto section = query.section(output,{{0,0,1},{0,0,1}},query_options);
    const auto void_point = query.locate_point(output,{2,2,1},query_options);
    const auto seam_point = query.locate_point(output,{4-overlap/2,2,1},query_options);
    const auto faces = query.faces_of_body(output);
    const auto source_bodies = query.source_bodies_of_body(output);
    // Outer (6-overlap)*4 minus a 2*2 hole gives S=20-4d.
    // Total perimeter=2*(10-d)+8, V=2S, A=2S+2P.
    if (!regions.value || regions.value->size() != 1 || regions.value->front().role != axiom::BodyShellRole::Material ||
        !mass.value || std::abs(mass.value->volume-(40-8*overlap)) > 1e-7 ||
        std::abs(mass.value->area-(96-12*overlap)) > 1e-7 || !section.value ||
        std::abs(section.value->area-(20-4*overlap)) > 1e-7 || !void_point.value ||
        void_point.value->location != axiom::BodyPointLocation::Outside || !seam_point.value ||
        seam_point.value->location != axiom::BodyPointLocation::Inside || !faces.value ||
        !source_bodies.value || std::find(source_bodies.value->begin(),source_bodies.value->end(),*holed.value) == source_bodies.value->end() ||
        std::find(source_bodies.value->begin(),source_bodies.value->end(),*extension.value) == source_bodies.value->end() ||
        kernel.validate().validate_all(output,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
        kernel.validate().validate_self_intersection_all_shells(output,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
        kernel.topology().validate().validate_body_trim_consistency(output).status != axiom::StatusCode::Ok)
        return fail(__LINE__);
    std::size_t cap_holes = 0;
    double integrated_volume = 0, integrated_area = 0;
    for (const auto face : *faces.value) {
        const auto loops = query.loops_of_face(face);
        const auto surface = query.surface_of_face(face);
        const auto sources = query.source_faces_of_face(face);
        if (!loops.value || loops.value->empty() || !surface.value || !sources.value || sources.value->empty()) return fail(__LINE__);
        if (loops.value->size() > 1) {
            if (loops.value->size() != 2) return fail(__LINE__);
            ++cap_holes;
        }
        axiom::Vec3 outer_normal;
        for (std::size_t ring = 0; ring < loops.value->size(); ++ring) {
            const auto uv = query.face_loop_uv_polyline(face,(*loops.value)[ring]);
            if (!uv.value || uv.value->size() < 3) return fail(__LINE__);
            std::vector<axiom::Point3> points;
            for (const auto p : *uv.value) {
                const auto world = kernel.surface_service().eval(*surface.value,p.x,p.y,0);
                if (!world.value) return fail(__LINE__);
                points.push_back(world.value->point);
            }
            const auto p = points.front();
            axiom::Vec3 area_vector;
            for (std::size_t i = 1; i+1 < points.size(); ++i) {
                const auto q = points[i], r = points[i+1];
                area_vector.x += ((q.y-p.y)*(r.z-p.z)-(q.z-p.z)*(r.y-p.y))/2;
                area_vector.y += ((q.z-p.z)*(r.x-p.x)-(q.x-p.x)*(r.z-p.z))/2;
                area_vector.z += ((q.x-p.x)*(r.y-p.y)-(q.y-p.y)*(r.x-p.x))/2;
                integrated_volume += (p.x*(q.y*r.z-q.z*r.y)+p.y*(q.z*r.x-q.x*r.z)+p.z*(q.x*r.y-q.y*r.x))/6;
            }
            if (ring == 0) {
                const double norm = std::hypot(area_vector.x,area_vector.y,area_vector.z);
                if (!(norm > 0)) return fail(__LINE__);
                outer_normal = {area_vector.x/norm,area_vector.y/norm,area_vector.z/norm};
            }
            integrated_area += area_vector.x*outer_normal.x+area_vector.y*outer_normal.y+area_vector.z*outer_normal.z;
        }
    }
    return cap_holes == 2 && std::abs(integrated_volume-(40-8*overlap)) < 1e-7 &&
        std::abs(integrated_area-(96-12*overlap)) < 1e-7 &&
        kernel.validate().validate_all(*holed.value,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok &&
        kernel.validate().validate_all(*extension.value,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok;
}

// Coplanar triangle SAT must use in-plane separation axes. These open
// two-face fixtures exercise only self-intersection, not closed-shell validity.
bool coplanar_self_intersection_regression() {
    for (const bool overlapping : {false,true}) {
        axiom::Kernel kernel;
        auto transaction = kernel.topology().begin_transaction();
        const auto plane = kernel.surfaces().make_plane({0,0,0},{0,0,1});
        if (!plane.value) return false;
        const std::array<axiom::Point3,3> first {{{2.0/3,2.0/3,0},{0,2,0},{2,0,0}}};
        const std::array<axiom::Point3,3> second = overlapping
            ? std::array<axiom::Point3,3>{{{0.5,0.5,0},{0.5,1.5,0},{1.5,0.5,0}}}
            : std::array<axiom::Point3,3>{{{1.4,1.4,0},{1,2,0},{2,2,0}}};
        // The separated pair lies in x+y<=2 versus x+y>=2.8.
        // For the overlapping pair (.75,.75) is strictly inside both triangles.
        std::array<axiom::FaceId,2> faces {};
        for (std::size_t face_index = 0; face_index < 2; ++face_index) {
            const auto& points = face_index == 0 ? first : second;
            std::array<axiom::VertexId,3> vertices {};
            std::array<axiom::CoedgeId,3> coedges {};
            for (std::size_t i = 0; i < 3; ++i) {
                const auto vertex = transaction.create_vertex(points[i]);
                if (!vertex.value) return false;
                vertices[i] = *vertex.value;
            }
            for (std::size_t i = 0; i < 3; ++i) {
                const auto next = (i+1)%3;
                const auto curve = kernel.curves().make_line_segment(points[i],points[next]);
                const auto edge = curve.value ? transaction.create_edge(*curve.value,vertices[i],vertices[next])
                                             : axiom::Result<axiom::EdgeId>{};
                const auto coedge = edge.value ? transaction.create_coedge(*edge.value,false)
                                              : axiom::Result<axiom::CoedgeId>{};
                if (!coedge.value) return false;
                coedges[i] = *coedge.value;
            }
            const auto loop = transaction.create_loop(coedges);
            const auto face = loop.value ? transaction.create_face(*plane.value,*loop.value,{})
                                         : axiom::Result<axiom::FaceId>{};
            if (!face.value) return false;
            faces[face_index] = *face.value;
        }
        const auto shell = transaction.create_shell(faces);
        const auto body = shell.value ? transaction.create_body(std::array<axiom::ShellId,1>{*shell.value})
                                      : axiom::Result<axiom::BodyId>{};
        if (!body.value || transaction.commit().status != axiom::StatusCode::Ok) return false;
        const auto mesh = kernel.convert().brep_to_mesh_shell(*body.value,*shell.value,{});
        if (!mesh.value || kernel.convert().mesh_triangle_count(*mesh.value).value !=
            std::optional<std::uint64_t>{2}) return false;
        const auto bodies_before = kernel.body_count().value;
        const auto topology_before = kernel.topology_count().value;
        for (const bool shell_only : {false,true}) {
            const auto validation = shell_only
                ? kernel.validate().validate_self_intersection_shell(*body.value,*shell.value,axiom::ValidationMode::Strict)
                : kernel.validate().validate_self_intersection(*body.value,axiom::ValidationMode::Strict);
            const auto diagnostic = kernel.diagnostics().get(validation.diagnostic_id);
            const char* stage = shell_only ? "heal.validate_self_intersection.shell_mesh"
                                          : "heal.validate_self_intersection.mesh";
            const bool rejected = diagnostic.value && std::any_of(diagnostic.value->issues.begin(),
                diagnostic.value->issues.end(),[&](const auto& issue) {
                    return issue.code == axiom::diag_codes::kValSelfIntersection && issue.stage == stage &&
                        issue.severity == axiom::IssueSeverity::Error;
                });
            if ((overlapping ? validation.status != axiom::StatusCode::OperationFailed || !rejected
                             : validation.status != axiom::StatusCode::Ok) ||
                kernel.body_count().value != bodies_before || kernel.topology_count().value != topology_before) {
                std::cerr << "coplanar self-intersection overlapping=" << overlapping
                          << " shell_only=" << shell_only << "\n";
                if (diagnostic.value) for (const auto& issue : diagnostic.value->issues)
                    std::cerr << issue.stage << " " << issue.code << " " << issue.message << "\n";
                return false;
            }
        }
    }
    return true;
}

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

bool has_related_entity(const axiom::Issue& issue, std::uint64_t entity) {
    return std::find(issue.related_entities.begin(), issue.related_entities.end(), entity) !=
           issue.related_entities.end();
}

bool check_heal_failure_evidence_and_rollback() {
    axiom::Kernel kernel;
    const auto box = kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 10.0, 10.0);
    if (box.status != axiom::StatusCode::Ok || !box.value.has_value()) return false;

    std::vector<axiom::DiagnosticId> failure_diagnostics;
    const auto record_failure = [&](const auto& result) {
        if (result.status == axiom::StatusCode::Ok || result.diagnostic_id.value == 0) return false;
        failure_diagnostics.push_back(result.diagnostic_id);
        return true;
    };

    const axiom::BodyId missing_body {900000001};
    if (!record_failure(kernel.validate().validate_geometry(missing_body, axiom::ValidationMode::Strict)) ||
        !record_failure(kernel.validate().validate_topology(missing_body, axiom::ValidationMode::Strict)) ||
        !record_failure(kernel.validate().validate_manifold(missing_body, axiom::ValidationMode::Strict)) ||
        !record_failure(kernel.validate().validate_self_intersection(missing_body,
                                                                      axiom::ValidationMode::Strict)) ||
        !record_failure(kernel.validate().validate_bbox(missing_body)) ||
        !record_failure(kernel.repair().remove_small_edges(missing_body, 0.1,
                                                            axiom::RepairMode::Safe)) ||
        !record_failure(kernel.repair().remove_small_faces(*box.value, 0.0,
                                                            axiom::RepairMode::Safe)) ||
        !record_failure(kernel.repair().merge_near_coplanar_faces(
            *box.value, 0.0, axiom::RepairMode::Safe)) ||
        !record_failure(kernel.repair().repair_face_trim_pcurves(
            axiom::FaceId {900000002}, axiom::RepairMode::Safe))) {
        return false;
    }

    const std::span<const axiom::BodyId> no_bodies;
    const std::span<const axiom::FaceId> no_faces;
    if (!record_failure(kernel.validate().validate_geometry_many(
            no_bodies, axiom::ValidationMode::Standard)) ||
        !record_failure(kernel.validate().validate_topology_many(
            no_bodies, axiom::ValidationMode::Standard)) ||
        !record_failure(kernel.validate().validate_all_many(
            no_bodies, axiom::ValidationMode::Standard)) ||
        !record_failure(kernel.validate().validate_tolerance_many(
            no_bodies, axiom::ValidationMode::Standard)) ||
        !record_failure(kernel.validate().validate_bbox_many(no_bodies)) ||
        !record_failure(kernel.repair().sew_faces(no_faces, 1e-6,
                                                   axiom::RepairMode::Safe)) ||
        !record_failure(kernel.repair().repair_many_auto(
            no_bodies, axiom::RepairMode::Safe))) {
        return false;
    }

    const auto objects_before_single_rollback = kernel.object_count_total();
    const auto bodies_before_single_rollback = kernel.body_count();
    const auto rejected_large_edge = kernel.repair().remove_small_edges(
        *box.value, 1000.0, axiom::RepairMode::Safe);
    const auto objects_after_single_rollback = kernel.object_count_total();
    const auto bodies_after_single_rollback = kernel.body_count();
    if (!record_failure(rejected_large_edge) ||
        !objects_before_single_rollback.value || !objects_after_single_rollback.value ||
        !bodies_before_single_rollback.value || !bodies_after_single_rollback.value ||
        *objects_before_single_rollback.value != *objects_after_single_rollback.value ||
        *bodies_before_single_rollback.value != *bodies_after_single_rollback.value) {
        return false;
    }

    const std::array<axiom::BodyId, 2> batch_with_late_failure {*box.value, missing_body};
    const auto objects_before_batch_rollback = kernel.object_count_total();
    const auto bodies_before_batch_rollback = kernel.body_count();
    const auto rejected_batch = kernel.repair().repair_many_remove_small_faces(
        batch_with_late_failure, 0.01, axiom::RepairMode::Aggressive);
    const auto objects_after_batch_rollback = kernel.object_count_total();
    const auto bodies_after_batch_rollback = kernel.body_count();
    if (!record_failure(rejected_batch) ||
        !objects_before_batch_rollback.value || !objects_after_batch_rollback.value ||
        !bodies_before_batch_rollback.value || !bodies_after_batch_rollback.value ||
        *objects_before_batch_rollback.value != *objects_after_batch_rollback.value ||
        *bodies_before_batch_rollback.value != *bodies_after_batch_rollback.value) {
        return false;
    }

    axiom::DiagnosticEvidencePolicy policy;
    policy.issue_code_prefix = "AXM-";
    policy.stage_prefix = "heal.";
    const auto first_before = kernel.diagnostics().get(failure_diagnostics.front());
    const auto audit = kernel.diagnostics().audit_evidence(failure_diagnostics, policy);
    if (!first_before.value || audit.status != axiom::StatusCode::Ok || !audit.value ||
        !audit.value->passed() || audit.value->reports_inspected != failure_diagnostics.size() ||
        audit.value->matching_issues != audit.value->complete_issues ||
        audit.value->matching_issues < failure_diagnostics.size() || !audit.value->findings.empty()) {
        return false;
    }

    const auto report_path = std::filesystem::temp_directory_path() /
                             "axiom_heal_failure_evidence_report.json";
    if (kernel.diagnostics().export_report_json(
            failure_diagnostics.front(), report_path.string()).status != axiom::StatusCode::Ok) {
        return false;
    }
    std::ifstream report_in {report_path};
    const std::string report_json((std::istreambuf_iterator<char>(report_in)),
                                  std::istreambuf_iterator<char>());
    report_in.close();
    std::error_code remove_error;
    std::filesystem::remove(report_path, remove_error);
    if (report_json.find("\"stage\":\"heal.") == std::string::npos ||
        report_json.find("\"numeric_evidence\":[") == std::string::npos ||
        report_json.find("\"name\":\"status_code\"") == std::string::npos) {
        return false;
    }

    const auto audit_path = std::filesystem::temp_directory_path() /
                            "axiom_heal_failure_evidence_audit.json";
    if (kernel.diagnostics().export_evidence_audit_json(
            failure_diagnostics, policy, audit_path.string()).status != axiom::StatusCode::Ok) {
        return false;
    }
    std::ifstream audit_in {audit_path};
    const std::string audit_json((std::istreambuf_iterator<char>(audit_in)),
                                 std::istreambuf_iterator<char>());
    audit_in.close();
    std::filesystem::remove(audit_path, remove_error);
    const auto first_after = kernel.diagnostics().get(failure_diagnostics.front());
    if (audit_json.find("\"passed\":true") == std::string::npos ||
        audit_json.find("\"issues_missing_numeric_evidence\":0") == std::string::npos ||
        !first_after.value || first_after.value->issues.size() != first_before.value->issues.size() ||
        first_after.value->issues.front().numeric_evidence.size() !=
            first_before.value->issues.front().numeric_evidence.size()) {
        return false;
    }

    axiom::KernelConfig invalid_tolerance_config;
    invalid_tolerance_config.tolerance.linear = 0.0;
    axiom::Kernel invalid_tolerance_kernel {invalid_tolerance_config};
    const auto invalid_tol_box = invalid_tolerance_kernel.primitives().box(
        {0.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
    if (!invalid_tol_box.value) return false;
    const auto invalid_tol_objects_before = invalid_tolerance_kernel.object_count_total();
    const auto invalid_tol_bodies_before = invalid_tolerance_kernel.body_count();
    const auto rejected_auto = invalid_tolerance_kernel.repair().auto_repair(
        *invalid_tol_box.value, axiom::RepairMode::Aggressive);
    const auto invalid_tol_objects_after = invalid_tolerance_kernel.object_count_total();
    const auto invalid_tol_bodies_after = invalid_tolerance_kernel.body_count();
    if (rejected_auto.status != axiom::StatusCode::OperationFailed ||
        !invalid_tol_objects_before.value || !invalid_tol_objects_after.value ||
        !invalid_tol_bodies_before.value || !invalid_tol_bodies_after.value ||
        *invalid_tol_objects_before.value != *invalid_tol_objects_after.value ||
        *invalid_tol_bodies_before.value != *invalid_tol_bodies_after.value) {
        return false;
    }
    const std::array<axiom::DiagnosticId, 1> auto_failure_ids {rejected_auto.diagnostic_id};
    const auto auto_audit = invalid_tolerance_kernel.diagnostics().audit_evidence(
        auto_failure_ids, policy);
    return auto_audit.value && auto_audit.value->passed() &&
           auto_audit.value->matching_issues == auto_audit.value->complete_issues &&
           auto_audit.value->matching_issues >= 2;
}

}  // namespace

int main() {
    if (!offset_shell_heal_regression()) {
        std::cerr << "Stage 6 shell Strict/ReportOnly thickness regression failed\n";
        return 1;
    }
    if (!planar_post_validation_rollback_regression()) {
        std::cerr << "Stage 5 allocated planar postcondition rollback regression failed\n";
        return 1;
    }
    if (!planar_import_repair_regression()) {
        std::cerr << "Stage 5 planar repair independent reference regression failed\n";
        return 1;
    }
    if (!coplanar_self_intersection_regression()) {
        std::cerr << "coplanar mesh separation/overlap regression failed\n";
        return 1;
    }
    if (!rebuilt_safe_hole_validation_regression()) {
        std::cerr << "Safe rebuilt hole validation failed\n";
        return 1;
    }
    if (!rebuilt_cavity_validation_regression()) {
        std::cerr << "rebuilt cavity Strict validation regression\n";
        return 1;
    }
    if (!check_heal_failure_evidence_and_rollback()) {
        std::cerr << "HEAL failure evidence audit or rollback regression\n";
        return 1;
    }
    axiom::Kernel kernel;

    const auto bodies_before_invalid_validation = kernel.body_count();
    const auto objects_before_invalid_validation = kernel.object_count_total();
    const axiom::BodyId missing_body {987654321};
    const auto missing_geometry = kernel.validate().validate_geometry(missing_body, axiom::ValidationMode::Strict);
    const auto bodies_after_invalid_validation = kernel.body_count();
    const auto objects_after_invalid_validation = kernel.object_count_total();
    const auto missing_diag = kernel.diagnostics().get(missing_geometry.diagnostic_id);
    const auto missing_stage = kernel.diagnostics().find_by_issue_stage("heal.validate_geometry.input", 10);
    if (missing_geometry.status != axiom::StatusCode::InvalidInput ||
        bodies_before_invalid_validation.status != axiom::StatusCode::Ok ||
        !bodies_before_invalid_validation.value.has_value() ||
        bodies_after_invalid_validation.status != axiom::StatusCode::Ok ||
        !bodies_after_invalid_validation.value.has_value() ||
        objects_before_invalid_validation.status != axiom::StatusCode::Ok ||
        !objects_before_invalid_validation.value.has_value() ||
        objects_after_invalid_validation.status != axiom::StatusCode::Ok ||
        !objects_after_invalid_validation.value.has_value() ||
        *bodies_before_invalid_validation.value != *bodies_after_invalid_validation.value ||
        *objects_before_invalid_validation.value != *objects_after_invalid_validation.value ||
        missing_diag.status != axiom::StatusCode::Ok || !missing_diag.value.has_value() ||
        missing_stage.status != axiom::StatusCode::Ok || !missing_stage.value.has_value() || missing_stage.value->empty()) {
        std::cerr << "invalid geometry validation diagnostics or failure isolation is unexpected\n";
        return 1;
    }
    const auto* missing_issue = find_issue(*missing_diag.value, axiom::diag_codes::kValDegenerateGeometry);
    if (missing_issue == nullptr || missing_issue->stage != "heal.validate_geometry.input" ||
        !has_related_entity(*missing_issue, missing_body.value) ||
        missing_stage.value->back().value != missing_geometry.diagnostic_id.value) {
        std::cerr << "invalid geometry validation stage/entity evidence is unexpected\n";
        return 1;
    }

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
        auto strict_tol_diag = k_bad_tol.diagnostics().get(strict_tol.diagnostic_id);
        if (strict_tol_diag.status != axiom::StatusCode::Ok || !strict_tol_diag.value.has_value()) {
            std::cerr << "missing diagnostic for strict validate_tolerance failure\n";
            return 1;
        }
        bool saw_tol_stage = false;
        for (const auto& iss : strict_tol_diag.value->issues) {
            if (iss.stage == "heal.validation.tolerance") {
                saw_tol_stage = true;
                break;
            }
        }
        if (!saw_tol_stage) {
            std::cerr << "expected heal.validation.tolerance stage on tolerance validation failure\n";
            return 1;
        }
        auto tol_prefix_total = k_bad_tol.diagnostics().total_issues_with_stage_prefix("heal.validation.tolerance");
        if (tol_prefix_total.status != axiom::StatusCode::Ok || !tol_prefix_total.value.has_value() ||
            *tol_prefix_total.value < 1) {
            std::cerr << "expected total_issues_with_stage_prefix to count tolerance validation issues\n";
            return 1;
        }
        auto heal_val_total = k_bad_tol.diagnostics().total_issues_with_stage_prefix("heal.validation.");
        if (heal_val_total.status != axiom::StatusCode::Ok || !heal_val_total.value.has_value() ||
            *heal_val_total.value < *tol_prefix_total.value) {
            std::cerr << "expected heal.validation. prefix total to cover tolerance issues\n";
            return 1;
        }
        auto std_tol = k_bad_tol.validate().validate_tolerance(*box_tol.value, axiom::ValidationMode::Standard);
        if (std_tol.status != axiom::StatusCode::Ok) {
            std::cerr << "Standard validate_tolerance should not apply min_local/max_local ordering gate\n";
            return 1;
        }
    }

    {
        axiom::Kernel k_manifold;
        auto bad_manifold = k_manifold.validate().validate_manifold(axiom::BodyId {888888888},
                                                                    axiom::ValidationMode::Standard);
        if (bad_manifold.status != axiom::StatusCode::InvalidTopology || bad_manifold.diagnostic_id.value == 0) {
            std::cerr << "expected validate_manifold on missing body to fail topology\n";
            return 1;
        }
        auto manifold_diag = k_manifold.diagnostics().get(bad_manifold.diagnostic_id);
        if (manifold_diag.status != axiom::StatusCode::Ok || !manifold_diag.value.has_value()) {
            std::cerr << "missing diagnostic for validate_manifold failure\n";
            return 1;
        }
        bool saw_manifold_stage = false;
        for (const auto& iss : manifold_diag.value->issues) {
            if (iss.stage == "heal.validation.manifold") {
                saw_manifold_stage = true;
                break;
            }
        }
        if (!saw_manifold_stage) {
            std::cerr << "expected heal.validation.manifold stage on manifold validation failure\n";
            return 1;
        }
        auto mf_total = k_manifold.diagnostics().total_issues_with_stage_prefix("heal.validation.manifold");
        if (mf_total.status != axiom::StatusCode::Ok || !mf_total.value.has_value() || *mf_total.value < 1) {
            std::cerr << "expected total_issues_with_stage_prefix to count manifold validation issues\n";
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
        const auto bodies_before_si_failures = si_kernel.body_count();
        const auto objects_before_si_failures = si_kernel.object_count_total();
        const axiom::BodyId missing_si_body {987654322};
        const axiom::ShellId missing_si_shell {987654323};
        auto missing_body_si = si_kernel.validate().validate_self_intersection(
            missing_si_body, axiom::ValidationMode::Strict);
        auto missing_shell_si = si_kernel.validate().validate_self_intersection_shell(
            *fresh_box.value, missing_si_shell, axiom::ValidationMode::Strict);
        auto second_box = si_kernel.primitives().box({4.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
        if (second_box.status != axiom::StatusCode::Ok || !second_box.value.has_value()) {
            std::cerr << "second box for foreign shell regression failed\n";
            return 1;
        }
        auto second_shells = si_kernel.topology().query().shells_of_body(*second_box.value);
        if (second_shells.status != axiom::StatusCode::Ok || !second_shells.value.has_value() ||
            second_shells.value->empty()) {
            std::cerr << "second box should expose an owned shell\n";
            return 1;
        }
        const auto objects_after_second_box = si_kernel.object_count_total();
        auto foreign_shell_si = si_kernel.validate().validate_self_intersection_shell(
            *fresh_box.value, second_shells.value->front(), axiom::ValidationMode::Strict);
        const std::span<const axiom::ShellId> empty_shell_span;
        auto shell_many_empty = si_kernel.validate().validate_self_intersection_shell_many(
            *fresh_box.value, empty_shell_span, axiom::ValidationMode::Strict);
        const auto bodies_after_si_failures = si_kernel.body_count();
        const auto objects_after_si_failures = si_kernel.object_count_total();
        const auto missing_body_diag = si_kernel.diagnostics().get(missing_body_si.diagnostic_id);
        const auto missing_shell_diag = si_kernel.diagnostics().get(missing_shell_si.diagnostic_id);
        const auto foreign_shell_diag = si_kernel.diagnostics().get(foreign_shell_si.diagnostic_id);
        const auto empty_shell_diag = si_kernel.diagnostics().get(shell_many_empty.diagnostic_id);
        const auto body_input_stage = si_kernel.diagnostics().find_by_issue_stage(
            "heal.validate_self_intersection.input", 10);
        const auto shell_input_stage = si_kernel.diagnostics().find_by_issue_stage(
            "heal.validate_self_intersection.shell_input", 10);
        if (missing_body_si.status != axiom::StatusCode::InvalidInput ||
            missing_shell_si.status != axiom::StatusCode::InvalidInput ||
            foreign_shell_si.status != axiom::StatusCode::InvalidInput ||
            shell_many_empty.status != axiom::StatusCode::InvalidInput ||
            !missing_body_diag.value.has_value() || !missing_shell_diag.value.has_value() ||
            !foreign_shell_diag.value.has_value() || !empty_shell_diag.value.has_value()) {
            std::cerr << "self-intersection invalid input regressions should return diagnostics\n";
            return 1;
        }
        const auto* missing_body_issue = find_issue(*missing_body_diag.value, axiom::diag_codes::kValSelfIntersection);
        const auto* missing_shell_issue = find_issue(*missing_shell_diag.value, axiom::diag_codes::kValSelfIntersection);
        const auto* foreign_shell_issue = find_issue(*foreign_shell_diag.value, axiom::diag_codes::kValSelfIntersection);
        const auto* empty_shell_issue = find_issue(*empty_shell_diag.value, axiom::diag_codes::kValSelfIntersection);
        if (missing_body_issue == nullptr || missing_body_issue->stage != "heal.validate_self_intersection.input" ||
            !has_related_entity(*missing_body_issue, missing_si_body.value) || missing_shell_issue == nullptr ||
            missing_shell_issue->stage != "heal.validate_self_intersection.shell_input" ||
            !has_related_entity(*missing_shell_issue, fresh_box.value->value) ||
            !has_related_entity(*missing_shell_issue, missing_si_shell.value) || foreign_shell_issue == nullptr ||
            foreign_shell_issue->stage != "heal.validate_self_intersection.shell_input" ||
            !has_related_entity(*foreign_shell_issue, fresh_box.value->value) ||
            !has_related_entity(*foreign_shell_issue, second_shells.value->front().value) || empty_shell_issue == nullptr ||
            empty_shell_issue->stage != "heal.validate_self_intersection.shell_input" ||
            !has_related_entity(*empty_shell_issue, fresh_box.value->value) || !body_input_stage.value.has_value() ||
            body_input_stage.value->empty() || !shell_input_stage.value.has_value() || shell_input_stage.value->size() < 3) {
            std::cerr << "self-intersection failure stage/entity evidence is unexpected\n";
            return 1;
        }
        const auto si_json_path = std::filesystem::temp_directory_path() / "axiom_heal_self_intersection_failure.json";
        const auto si_json_export = si_kernel.diagnostics().export_report_json(foreign_shell_si.diagnostic_id,
                                                                               si_json_path.string());
        std::ifstream si_json_in(si_json_path);
        const std::string si_json((std::istreambuf_iterator<char>(si_json_in)), std::istreambuf_iterator<char>());
        std::filesystem::remove(si_json_path);
        if (si_json_export.status != axiom::StatusCode::Ok ||
            si_json.find("\"stage\":\"heal.validate_self_intersection.shell_input\"") == std::string::npos ||
            si_json.find(std::to_string(fresh_box.value->value)) == std::string::npos ||
            si_json.find(std::to_string(second_shells.value->front().value)) == std::string::npos ||
            !bodies_before_si_failures.value.has_value() || !bodies_after_si_failures.value.has_value() ||
            *bodies_after_si_failures.value != *bodies_before_si_failures.value + 1 ||
            !objects_before_si_failures.value.has_value() || !objects_after_second_box.value.has_value() ||
            !objects_after_si_failures.value.has_value() ||
            *objects_after_si_failures.value != *objects_after_second_box.value) {
            std::cerr << "self-intersection JSON evidence or failure non-pollution is unexpected\n";
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
        const auto nd_stage = ndk.diagnostics().find_by_issue_stage(
            "heal.validate_geometry.near_duplicate_vertices", 10);
        if (nd_diag.status != axiom::StatusCode::Ok || !nd_diag.value.has_value() ||
            !has_issue_code(*nd_diag.value, axiom::diag_codes::kValNearDuplicateVertices) ||
            nd_stage.status != axiom::StatusCode::Ok || !nd_stage.value.has_value() || nd_stage.value->empty()) {
            std::cerr << "expected kValNearDuplicateVertices diagnostic for near-duplicate vertex body\n";
            return 1;
        }
        const auto* nd_issue = find_issue(*nd_diag.value, axiom::diag_codes::kValNearDuplicateVertices);
        if (nd_issue == nullptr || nd_issue->stage != "heal.validate_geometry.near_duplicate_vertices" ||
            !has_related_entity(*nd_issue, bd.value->value)) {
            std::cerr << "near-duplicate diagnostic stage/entity evidence is unexpected\n";
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
        const auto* fn_issue = find_issue(*fn_diag.value, axiom::diag_codes::kValFaceNormalInconsistent);
        const auto fn_stage = fnk.diagnostics().find_by_issue_stage("heal.validate_geometry.face_normal", 10);
        const auto json_path = std::filesystem::temp_directory_path() / "axiom_heal_geometry_failure.json";
        const auto json_export = fnk.diagnostics().export_report_json(fn_strict.diagnostic_id, json_path.string());
        std::ifstream json_in(json_path);
        const std::string json((std::istreambuf_iterator<char>(json_in)), std::istreambuf_iterator<char>());
        std::error_code remove_error;
        std::filesystem::remove(json_path, remove_error);
        if (fn_issue == nullptr || fn_issue->stage != "heal.validate_geometry.face_normal" ||
            !has_related_entity(*fn_issue, fbody.value->value) || fn_stage.status != axiom::StatusCode::Ok ||
            !fn_stage.value.has_value() || fn_stage.value->empty() || json_export.status != axiom::StatusCode::Ok ||
            json.find("heal.validate_geometry.face_normal") == std::string::npos ||
            json.find(axiom::diag_codes::kValFaceNormalInconsistent) == std::string::npos) {
            std::cerr << "face-normal diagnostic stage/entity JSON evidence is unexpected\n";
            return 1;
        }
    }

    return 0;
}
