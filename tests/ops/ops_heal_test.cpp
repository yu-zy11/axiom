#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "axiom/core/types.h"
#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

namespace {

// The new material boundary remains usable by the next modeling operation and
// by Heal's complete Strict gate, rather than terminating at an imprinted proxy.
bool rebuilt_boolean_heal_chain_regression() {
    axiom::Kernel kernel;
    axiom::DiagnosticId last_diagnostic {};
    axiom::Kernel* diagnostic_kernel = &kernel;
    const auto fail = [&](int line) {
        std::cerr << "rebuilt Boolean/Heal chain line=" << line << "\n";
        const auto diagnostic = diagnostic_kernel->diagnostics().get(last_diagnostic);
        if (diagnostic.value) for (const auto& issue : diagnostic.value->issues) {
            std::cerr << issue.stage << " " << issue.code << " " << issue.message << "\n";
            for (const auto& evidence : issue.numeric_evidence)
                std::cerr << "  " << evidence.name << "=" << evidence.value << " " << evidence.unit << "\n";
        }
        return false;
    };

    const auto a = kernel.primitives().box({0,0,0},2,2,2), b = kernel.primitives().box({1,1,1},2,2,2);
    const auto inner = kernel.primitives().box({0.25,0.25,0.25},0.5,0.5,0.5);
    if (!a.value || !b.value || !inner.value) return fail(__LINE__);
    axiom::BooleanRebuildOptions options;
    options.auto_repair = true;
    const auto united = kernel.booleans().run_rebuilt(axiom::BooleanOp::Union,*a.value,*b.value,options);
    last_diagnostic = united.diagnostic_id;
    if (!united.value || !united.value->output || united.value->repaired ||
        kernel.validate().validate_all(*united.value->output,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok)
        return fail(__LINE__);
    const auto cut = kernel.booleans().run_rebuilt(axiom::BooleanOp::Subtract,*united.value->output,*inner.value);
    last_diagnostic = cut.diagnostic_id;
    if (!cut.value || !cut.value->output ||
        kernel.validate().validate_all(*cut.value->output,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok)
        return fail(__LINE__);
    const auto mass = kernel.query().mass_properties(*cut.value->output);
    const auto source_bodies = kernel.topology().query().source_bodies_of_body(*cut.value->output);
    const auto cavity = kernel.topology().query().locate_point(*cut.value->output,{0.5,0.5,0.5});
    const auto material = kernel.topology().query().locate_point(*cut.value->output,{1.5,1.5,1.5});
    if (!mass.value || std::abs(mass.value->volume-14.875) > 1e-7 || std::abs(mass.value->area-43.5) > 1e-7 ||
        !source_bodies.value || std::find(source_bodies.value->begin(),source_bodies.value->end(),*united.value->output) ==
            source_bodies.value->end() || !cavity.value || cavity.value->location != axiom::BodyPointLocation::Outside ||
        !material.value || material.value->location != axiom::BodyPointLocation::Inside ||
        kernel.validate().validate_all(*a.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
        kernel.validate().validate_all(*b.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok)
        return fail(__LINE__);
    const auto aligned = kernel.primitives().box({1,0,0},2,2,2);
    const auto tunnel = kernel.primitives().box({0.25,0.25,0},0.5,0.5,2);
    if (!aligned.value || !tunnel.value) return fail(__LINE__);
    const auto fused = kernel.booleans().run_rebuilt(axiom::BooleanOp::Union,*a.value,*aligned.value,options);
    last_diagnostic = fused.diagnostic_id;
    if (!fused.value || !fused.value->output || fused.value->repaired) return fail(__LINE__);
    const auto perforated = kernel.booleans().run_rebuilt(
        axiom::BooleanOp::Subtract,*fused.value->output,*tunnel.value,options);
    last_diagnostic = perforated.diagnostic_id;
    if (!perforated.value || !perforated.value->output || perforated.value->repaired) return fail(__LINE__);
    const auto clipped = kernel.booleans().run_rebuilt(
        axiom::BooleanOp::Intersect,*perforated.value->output,*a.value,options);
    last_diagnostic = clipped.diagnostic_id;
    if (!clipped.value || !clipped.value->output || clipped.value->repaired) return fail(__LINE__);
    for (const bool final_clip : {false,true}) {
        const auto body = final_clip ? *clipped.value->output : *perforated.value->output;
        const auto properties = kernel.query().mass_properties(body);
        // The straight through-hole section is constant for every 0<z<2.
        // Require one fixed regular height and retain z=1 below as a checked
        // probe through artificial subdivision nodes, without moving any gates.
        const auto section = kernel.query().section_detailed(body,{{0,0,0.875},{0,0,1}});
        const auto opening = kernel.topology().query().locate_point(body,{0.5,0.5,1});
        const auto validation = kernel.validate().validate_all(body,axiom::ValidationMode::Strict);
        // Rectangular 3x2 or 2x2 section minus the 0.5x0.5 tunnel;
        // V=h*S and A=2*S+h*(outer_perimeter+hole_perimeter), h=2.
        if (!properties.value || std::abs(properties.value->volume-(final_clip ? 7.5 : 11.5)) > 1e-7 ||
            std::abs(properties.value->area-(final_clip ? 27.5 : 35.5)) > 1e-7 || !section.value ||
            std::abs(section.value->area-(final_clip ? 3.75 : 5.75)) > 1e-7 || !opening.value ||
            opening.value->location != axiom::BodyPointLocation::Outside ||
            validation.status != axiom::StatusCode::Ok) {
            std::cerr << "rebuilt chain final_clip=" << final_clip << " body=" << body.value
                      << " volume=" << (properties.value ? properties.value->volume : -1) << '/' << (final_clip ? 7.5 : 11.5)
                      << " area=" << (properties.value ? properties.value->area : -1) << '/' << (final_clip ? 27.5 : 35.5)
                      << " section=" << (section.value ? section.value->area : -1) << '/' << (final_clip ? 3.75 : 5.75)
                      << " opening=" << (opening.value ? static_cast<int>(opening.value->location) : -1)
                      << '/' << static_cast<int>(axiom::BodyPointLocation::Outside)
                      << " Strict=" << static_cast<int>(validation.status) << "\n";
            for (const auto diagnostic_id : {properties.diagnostic_id,section.diagnostic_id,opening.diagnostic_id,validation.diagnostic_id}) {
                const auto diagnostic = kernel.diagnostics().get(diagnostic_id);
                if (diagnostic.value) for (const auto& issue : diagnostic.value->issues) {
                    std::cerr << issue.stage << " " << issue.code << " " << issue.message << "\n";
                    for (const auto& evidence : issue.numeric_evidence)
                        std::cerr << "  " << evidence.name << "=" << evidence.value << " " << evidence.unit << "\n";
                }
            }
            return fail(__LINE__);
        }
        const auto original_section = kernel.query().section_detailed(body,{{0,0,1},{0,0,1}});
        if (original_section.status == axiom::StatusCode::Ok) {
            if (!original_section.value || std::abs(original_section.value->area-(final_clip ? 3.75 : 5.75)) > 1e-7)
                return fail(__LINE__);
        } else {
            last_diagnostic = original_section.diagnostic_id;
            const auto diagnostic = kernel.diagnostics().get(original_section.diagnostic_id);
            if (original_section.status != axiom::StatusCode::NumericalInstability || original_section.value || !diagnostic.value ||
                std::none_of(diagnostic.value->issues.begin(),diagnostic.value->issues.end(),[](const auto& issue) {
                    return issue.code == axiom::diag_codes::kQuerySectionFailure &&
                        issue.stage == "query.section.numeric" && issue.severity == axiom::IssueSeverity::Error;
                })) return fail(__LINE__);
        }
    }
    // Safe repair removes artificial coplanar seams, then the resulting real
    // boundary must support a new cavity cut and the same Strict gate.
    axiom::Kernel repaired_kernel;
    diagnostic_kernel = &repaired_kernel;
    constexpr double overlap = 2e-4;
    const auto repair_a = repaired_kernel.primitives().box({0,0,0},2,2,2);
    const auto repair_b = repaired_kernel.primitives().box({2-overlap,0,0},2,2,2);
    const auto repair_inner = repaired_kernel.primitives().box({0.25,0.25,0.25},0.5,0.5,0.5);
    if (!repair_a.value || !repair_b.value || !repair_inner.value ||
        repaired_kernel.set_linear_tolerance(1e-3).status != axiom::StatusCode::Ok) return fail(__LINE__);
    axiom::BooleanRebuildOptions safe_options;
    safe_options.preparation.intersection.tolerance.linear = 1e-6;
    const auto unhealed = repaired_kernel.booleans().run_rebuilt(
        axiom::BooleanOp::Union,*repair_a.value,*repair_b.value,safe_options);
    last_diagnostic = unhealed.diagnostic_id;
    const auto rejected = repaired_kernel.diagnostics().get(unhealed.diagnostic_id);
    if (unhealed.status == axiom::StatusCode::Ok || unhealed.value || !rejected.value ||
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
    safe_options.auto_repair = true;
    const auto healed = repaired_kernel.booleans().run_rebuilt(
        axiom::BooleanOp::Union,*repair_a.value,*repair_b.value,safe_options);
    last_diagnostic = healed.diagnostic_id;
    if (!healed.value || !healed.value->output || !healed.value->repaired ||
        repaired_kernel.validate().validate_all(*healed.value->output,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok)
        return fail(__LINE__);
    const auto cavity_cut = repaired_kernel.booleans().run_rebuilt(
        axiom::BooleanOp::Subtract,*healed.value->output,*repair_inner.value,safe_options);
    last_diagnostic = cavity_cut.diagnostic_id;
    if (!cavity_cut.value || !cavity_cut.value->output) return fail(__LINE__);
    const auto repair_query = repaired_kernel.topology().query();
    const auto repaired_mass = repair_query.body_mass_properties(*cavity_cut.value->output);
    axiom::BodySpatialQueryOptions query_options;
    query_options.position_tolerance = 1e-6;
    const auto repaired_section = repair_query.section(*cavity_cut.value->output,{{0,0,0.5},{0,0,1}},query_options);
    const auto cavity_location = repair_query.locate_point(*cavity_cut.value->output,{0.5,0.5,0.5},query_options);
    const auto healed_sources = repair_query.source_bodies_of_body(*cavity_cut.value->output);
    // L=4-overlap,w=h=2 minus a .5^3 cavity: V=4L-.125,
    // A=8L+8+1.5, mid-cavity S=2L-.25.
    return repaired_mass.value && std::abs(repaired_mass.value->volume-(15.875-4*overlap)) < 1e-7 &&
        std::abs(repaired_mass.value->area-(41.5-8*overlap)) < 1e-7 && repaired_section.value &&
        std::abs(repaired_section.value->area-(7.75-2*overlap)) < 1e-7 && cavity_location.value &&
        cavity_location.value->location == axiom::BodyPointLocation::Outside && healed_sources.value &&
        std::find(healed_sources.value->begin(),healed_sources.value->end(),*healed.value->output) != healed_sources.value->end() &&
        repaired_kernel.validate().validate_all(*cavity_cut.value->output,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok &&
        repaired_kernel.validate().validate_all(*repair_a.value,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok &&
        repaired_kernel.validate().validate_all(*repair_b.value,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok;
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

bool has_issue_stage(const axiom::DiagnosticReport& report, std::string_view stage) {
    for (const auto& issue : report.issues) {
        if (issue.stage == stage) {
            return true;
        }
    }
    return false;
}

bool test_holed_extrusions() {
    struct Model {
        axiom::ProfileRef profile;
        // Independent signed rectangle decomposition: x, y, width, height, sign.
        std::vector<std::array<double, 5>> rectangles;
    };
    const std::vector<Model> models {
        {{"single_hole", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}},
          {{{1,1,0}, {3,1,0}, {3,3,0}, {1,3,0}}}}, {{0,0,8,6,1}, {1,1,2,2,-1}}},
        {{"two_holes", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}},
          {{{1,1,0}, {3,1,0}, {3,3,0}, {1,3,0}}, {{5,2,0}, {7,2,0}, {7,5,0}, {5,5,0}}}},
         {{0,0,8,6,1}, {1,1,2,2,-1}, {5,2,2,3,-1}}},
        {{"concave_outer", {{0,0,0}, {6,0,0}, {6,2,0}, {2,2,0}, {2,6,0}, {0,6,0}},
          {{{0.5,0.5,0}, {1.5,0.5,0}, {1.5,1.5,0}, {0.5,1.5,0}}}},
         {{0,0,6,2,1}, {0,2,2,4,1}, {0.5,0.5,1,1,-1}}},
        {{"concave_hole", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}},
          {{{1,1,0}, {4,1,0}, {4,2,0}, {2,2,0}, {2,4,0}, {1,4,0}}}},
         {{0,0,8,6,1}, {1,1,3,1,-1}, {1,2,1,2,-1}}}
    };
    for (const auto& model : models) {
        double area = 0, mx = 0, my = 0, xx = 0, yy = 0, xy = 0;
        for (const auto& r : model.rectangles) {
            const double a = r[2] * r[3] * r[4], x = r[0] + r[2] / 2, y = r[1] + r[3] / 2;
            area += a;
            mx += a * x;
            my += a * y;
            xx += a * (x * x + r[2] * r[2] / 12);
            yy += a * (y * y + r[3] * r[3] / 12);
            xy += a * x * y;
        }
        const double cx = mx / area, cy = my / area;
        for (int variant = 0; variant < 8; ++variant) {
            for (const bool tilted : {false, true}) {
                const auto rotate = [tilted](axiom::Point3 p) -> axiom::Point3 {
                    if (!tilted) return p;
                    return {0.6*p.x - 0.48*p.y + 0.64*p.z,
                            0.8*p.x + 0.36*p.y - 0.48*p.z, 0.8*p.y + 0.6*p.z};
                };
                const auto world = [&](axiom::Point3 p) -> axiom::Point3 {
                    const auto q = rotate(p);
                    return {q.x + 10, q.y - 20, q.z + 30};
                };
                for (const axiom::Point3 delta : {axiom::Point3 {0,0,3}, {2,-1,3}, {-2,1,-3}}) {
                    axiom::Kernel kernel;
                    auto profile = model.profile;
                    std::size_t n = 0;
                    double surface_area = 2 * area;
                    for (std::size_t r = 0; r <= profile.holes_xyz.size(); ++r) {
                        auto& ring = r == 0 ? profile.polygon_xyz : profile.holes_xyz[r - 1];
                        n += ring.size();
                        for (std::size_t i = 0; i < ring.size(); ++i) {
                            const auto& a = ring[i];
                            const auto& b = ring[(i + 1) % ring.size()];
                            surface_area += std::hypot((b.y-a.y)*delta.z, (b.x-a.x)*delta.z,
                                                       (b.x-a.x)*delta.y - (b.y-a.y)*delta.x);
                        }
                        if (variant & (1 << r)) std::reverse(ring.begin(), ring.end());
                        std::rotate(ring.begin(), ring.begin() + variant % ring.size(), ring.end());
                        for (auto& p : ring) p = world(p);
                    }
                    if (variant & 4) std::reverse(profile.holes_xyz.begin(), profile.holes_xyz.end());
                    const auto d = rotate(delta);
                    const auto body = kernel.sweeps().extrude(profile, {d.x,d.y,d.z}, std::hypot(d.x,d.y,d.z));
                    const auto query = kernel.topology().query();
                    if (!body.value) {
                        std::cerr << "holed extrusion failed: " << model.profile.label << " variant=" << variant
                                  << " tilted=" << tilted << " delta=" << delta.x << ',' << delta.z << '\n';
                        return false;
                    }
                    const auto faces = query.faces_of_body(*body.value);
                    const auto edges = query.edges_of_body(*body.value);
                    const auto vertices = query.vertices_of_body(*body.value);
                    const auto shells = query.shells_of_body(*body.value);
                    const auto owned = query.has_body(*body.value);
                    const auto mass = kernel.query().mass_properties(*body.value);
                    const auto center = world({cx + delta.x/2, cy + delta.y/2, delta.z/2});
                    const std::size_t h = profile.holes_xyz.size();
                    const double volume = area * std::abs(delta.z);
                    if (body.status != axiom::StatusCode::Ok || !owned.value || !*owned.value ||
                        !faces.value || faces.value->size() != 4*n + 4*h - 4 ||
                        !edges.value || edges.value->size() != 6*n + 6*h - 6 ||
                        !vertices.value || vertices.value->size() != 2*n ||
                        !shells.value || shells.value->size() != 1 || !mass.value ||
                        std::abs(mass.value->volume - volume) > 1e-8 ||
                        std::abs(mass.value->area - surface_area) > 1e-8 ||
                        std::hypot(mass.value->centroid.x - center.x, mass.value->centroid.y - center.y,
                                   mass.value->centroid.z - center.z) > 1e-8 ||
                        kernel.validate().validate_all(*body.value, axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) {
                        std::cerr << "holed prism topology/mass/Strict mismatch: " << profile.label << '\n';
                        return false;
                    }
                    // Covariance of a uniform sheared prism = cap covariance + delta*delta^T/12.
                    const std::array<double, 3> dv {delta.x,delta.y,delta.z};
                    std::array<double, 9> covariance {xx/area-cx*cx, xy/area-cx*cy, 0,
                                                    xy/area-cx*cy, yy/area-cy*cy, 0, 0,0,0};
                    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) covariance[3*i+j] += dv[i]*dv[j]/12;
                    const auto rx = rotate({1,0,0}), ry = rotate({0,1,0}), rz = rotate({0,0,1});
                    const std::array<double, 9> rotation {rx.x,ry.x,rz.x, rx.y,ry.y,rz.y, rx.z,ry.z,rz.z};
                    std::array<double, 9> rotated {};
                    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
                        for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b)
                            rotated[3*i+j] += rotation[3*i+a]*covariance[3*a+b]*rotation[3*j+b];
                    const double trace = rotated[0]+rotated[4]+rotated[8];
                    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
                        const double expected = volume * ((i == j ? trace : 0) - rotated[3*i+j]);
                        if (std::abs(mass.value->inertia[3*i+j] - expected) > 1e-7) return false;
                    }
                    double queried_area = 0;
                    for (const auto face : *faces.value) {
                        const auto a = query.planar_face_area(face);
                        const auto owners = query.bodies_of_face(face);
                        const auto loops = query.loops_of_face(face);
                        if (!a.value || *a.value <= 0 || !owners.value || owners.value->size() != 1 ||
                            owners.value->front().value != body.value->value || !loops.value || loops.value->size() != 1) return false;
                        const auto corners = query.vertices_of_loop(loops.value->front());
                        if (!corners.value || corners.value->size() != 3) return false;
                        queried_area += *a.value;
                    }
                    for (const auto edge : *edges.value) {
                        const auto uses = query.coedge_count_of_edge(edge);
                        const auto neighbors = query.faces_of_edge(edge);
                        if (!uses.value || *uses.value != 2 || !neighbors.value || neighbors.value->size() != 2) return false;
                    }
                    if (std::abs(queried_area - surface_area) > 1e-8) return false;
                    const auto mesh = kernel.convert().brep_to_mesh(*body.value, {});
                    if (!mesh.value) return false;
                    const auto inspection = kernel.convert().inspect_mesh(*mesh.value);
                    if (!inspection.value || inspection.value->tessellation_strategy != "owned_topo_welded" ||
                        inspection.value->vertex_count != 2*n || inspection.value->triangle_count != faces.value->size() ||
                        inspection.value->connected_components != 1 || inspection.value->has_degenerate_triangles ||
                        inspection.value->has_out_of_range_indices) return false;
                    // The same holed profile must survive the public line-rail entry point.
                    if (variant == 0 && !tilted) {
                        const auto rail = kernel.curves().make_line_segment({50,60,70}, {50+d.x,60+d.y,70+d.z});
                        if (!rail.value) return false;
                        const auto swept = kernel.sweeps().sweep(profile, *rail.value);
                        if (!swept.value || kernel.validate().validate_all(*swept.value, axiom::ValidationMode::Strict).status !=
                                                axiom::StatusCode::Ok) return false;
                        const auto swept_mass = kernel.query().mass_properties(*swept.value);
                        if (!swept_mass.value || std::abs(swept_mass.value->volume-volume) > 1e-8 ||
                            std::abs(swept_mass.value->area-surface_area) > 1e-8) return false;
                    }
                }
            }
        }
    }

    axiom::Kernel kernel;
    const auto source = kernel.sweeps().extrude(models.front().profile, {0,0,1}, 3);
    const auto rail = kernel.curves().make_line_segment({0,0,0}, {0,0,3});
    if (!source.value || !rail.value) return false;
    // Failed operations within a caller's active transaction must not add writes;
    // rollback must still restore the caller's own topology independently.
    auto transaction = kernel.topology().begin_transaction();
    if (!transaction.create_vertex({20,20,20}).value) return false;
    const auto before_objects = kernel.object_count_total();
    const auto before_geometry = kernel.geometry_count();
    const auto before_bodies = kernel.body_count();
    const auto before_next = kernel.next_object_id();
    const auto before_writes = transaction.write_operation_count();
    const auto before_runtime = kernel.runtime_store_counts();
    if (!before_objects.value || !before_geometry.value || !before_bodies.value || !before_next.value ||
        !before_writes.value || !before_runtime.value) return false;
    const auto rejected = [&](const axiom::Result<axiom::BodyId>& result) {
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto objects = kernel.object_count_total(), geometry = kernel.geometry_count(), bodies = kernel.body_count();
        const auto next = kernel.next_object_id(), writes = transaction.write_operation_count();
        const auto runtime = kernel.runtime_store_counts();
        return result.status == axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value, axiom::diag_codes::kCoreParameterOutOfRange) &&
            objects.value == before_objects.value && geometry.value == before_geometry.value &&
            bodies.value == before_bodies.value && next.value == before_next.value && writes.value == before_writes.value &&
            runtime.value && runtime.value->mesh_records == before_runtime.value->mesh_records &&
            runtime.value->tessellation_cache_entries == before_runtime.value->tessellation_cache_entries &&
            runtime.value->curve_eval_cache_entries == before_runtime.value->curve_eval_cache_entries &&
            runtime.value->surface_eval_cache_entries == before_runtime.value->surface_eval_cache_entries;
    };
    const std::vector<std::vector<axiom::Point3>> bad_holes {
        {}, {{1,1,0}, {2,1,0}}, // empty/short
        {{1,1,0}, {2,1,0}, {3,1,0}}, // zero area
        {{1,1,0}, {3,1,0}, {3,1,0}, {1,3,0}}, // duplicate
        {{1,1,0}, {2,1,0}, {3,1,0}, {3,3,0}, {1,3,0}}, // collinear corner
        {{1,1,0}, {4,3,0}, {1,4,0}, {3,1,0}}, // self intersection
        {{1,1,0}, {3,1,0}, {3,3,0.1}, {1,3,0}}, // nonplanar
        {{1,1,0.1}, {3,1,0.1}, {3,3,0.1}, {1,3,0.1}}, // different plane
        {{9,1,0}, {10,1,0}, {10,2,0}, {9,2,0}}, // outside
        {{7,1,0}, {9,1,0}, {9,2,0}, {7,2,0}}, // crossing outer
        {{0,1,0}, {2,1,0}, {2,3,0}, {0,3,0}}, // shared segment
        {{0,2,0}, {1,1,0}, {2,2,0}, {1,3,0}}, // point on outer edge
        {{0,0,0}, {2,1,0}, {1,2,0}}, // shared outer vertex
        {{-1,-1,0}, {9,-1,0}, {9,7,0}, {-1,7,0}}, // encloses outer
        {{1,1,0}, {3,1,0}, {3,3,std::numeric_limits<double>::quiet_NaN()}},
        {{1,1,0}, {3,1,0}, {3,std::numeric_limits<double>::infinity(),0}}
    };
    std::vector<axiom::ProfileRef> invalid;
    for (const auto& hole : bad_holes) {
        auto profile = models.front().profile;
        profile.holes_xyz = {hole};
        invalid.push_back(profile);
    }
    for (const auto& hole : std::vector<std::vector<axiom::Point3>> {
             {{1.5,1.5,0}, {2.5,1.5,0}, {2.5,2.5,0}, {1.5,2.5,0}}, // nested
             {{2,2,0}, {4,2,0}, {4,4,0}, {2,4,0}}, // intersecting
             {{3,1,0}, {4,1,0}, {4,3,0}, {3,3,0}}, // shared edge
             {{3,3,0}, {4,3,0}, {4,4,0}, {3,4,0}}, // point contact
             models.front().profile.holes_xyz.front()}) { // coincident
        auto profile = models.front().profile;
        profile.holes_xyz.push_back(hole);
        invalid.push_back(profile);
        std::reverse(profile.holes_xyz.begin(), profile.holes_xyz.end());
        invalid.push_back(profile);
    }
    auto no_outer = models.front().profile;
    no_outer.polygon_xyz.clear();
    invalid.push_back(no_outer);
    auto collapsed = models.front().profile;
    for (auto& p : collapsed.polygon_xyz) p.z = 1e17;
    for (auto& p : collapsed.holes_xyz.front()) p.z = 1e17;
    invalid.push_back(collapsed);
    for (const auto& profile : invalid) {
        if (!rejected(kernel.sweeps().extrude(profile, {0,0,1}, 3)) ||
            !rejected(kernel.sweeps().sweep(profile, *rail.value)) ||
            !rejected(kernel.sweeps().extrude_scaled(profile, {0,0,1}, 3, {0,0,0}, 0.5))) {
            std::cerr << "invalid holed profile must fail without changing model/IDs/transaction/cache\n";
            return false;
        }
    }
    for (const auto direction : {axiom::Vec3 {1,0,0}, {0,0,0}, {0,0,std::numeric_limits<double>::infinity()}})
        if (!rejected(kernel.sweeps().extrude(models.front().profile, direction, 3))) return false;
    for (const double distance : {0.0, -1.0, 1e-20, std::numeric_limits<double>::infinity()})
        if (!rejected(kernel.sweeps().extrude(models.front().profile, {0,0,1}, distance))) return false;
    const std::array<axiom::ProfileRef, 2> loft_profiles {models.front().profile, models.front().profile};
    if (!rejected(kernel.sweeps().revolve(models.front().profile, {{0,0,0}, {0,0,1}}, 1)) ||
        !rejected(kernel.sweeps().loft(loft_profiles)) || transaction.rollback().status != axiom::StatusCode::Ok) return false;
    const auto after_rollback = kernel.object_count_total();
    if (!after_rollback.value || *after_rollback.value + 1 != *before_objects.value) return false;
    const auto retry = kernel.sweeps().extrude(models.back().profile, {1,0,-3}, std::sqrt(10.0));
    if (!retry.value || kernel.validate().validate_all(*retry.value, axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
        kernel.validate().validate_all(*source.value, axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) return false;
    return true;
}

bool test_scaled_extrusions() {
    struct Model {
        axiom::ProfileRef profile;
        // Signed rectangle decomposition independent of the topology triangulation.
        std::vector<std::array<double, 5>> rectangles;
    };
    const std::vector<Model> models {
        {{"rectangle", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}}}, {{0,0,8,6,1}}},
        {{"concave", {{0,0,0}, {6,0,0}, {6,2,0}, {2,2,0}, {2,6,0}, {0,6,0}}},
         {{0,0,6,2,1}, {0,2,2,4,1}}},
        {{"single_hole", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}},
          {{{1,1,0}, {3,1,0}, {3,3,0}, {1,3,0}}}}, {{0,0,8,6,1}, {1,1,2,2,-1}}},
        {{"two_holes", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}},
          {{{1,1,0}, {3,1,0}, {3,3,0}, {1,3,0}}, {{5,2,0}, {7,2,0}, {7,5,0}, {5,5,0}}}},
         {{0,0,8,6,1}, {1,1,2,2,-1}, {5,2,2,3,-1}}},
        {{"concave_hole", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}},
          {{{1,1,0}, {4,1,0}, {4,2,0}, {2,2,0}, {2,4,0}, {1,4,0}}}},
         {{0,0,8,6,1}, {1,1,3,1,-1}, {1,2,1,2,-1}}}
    };
    for (const auto& model : models) for (int variant = 0; variant < 6; ++variant)
    for (const bool tilted : {false, true}) for (const double end_scale : {0.0, 0.5, 1.0, 2.0})
    for (const axiom::Point3 delta : {axiom::Point3 {0,0,3}, {2,-1,3}, {-2,1,-3}}) {
        const bool apex = end_scale == 0.0;
        if ((apex && !model.profile.holes_xyz.empty()) || (!apex && variant >= 4)) continue;
        axiom::Kernel kernel;
        const auto rotate = [tilted](axiom::Point3 p) -> axiom::Point3 {
            if (!tilted) return p;
            return {0.6*p.x-0.48*p.y+0.64*p.z, 0.8*p.x+0.36*p.y-0.48*p.z, 0.8*p.y+0.6*p.z};
        };
        const auto world = [&](axiom::Point3 p) -> axiom::Point3 {
            const auto q = rotate(p);
            return {q.x+10, q.y-20, q.z+30};
        };
        // Include an off-center homothety; the center need not lie inside the material.
        const axiom::Point3 center = variant >= 4 ? axiom::Point3 {0,0,0} :
            (variant & 2 ? axiom::Point3 {-1,2,0} : axiom::Point3 {4,3,0});
        double area = 0;
        for (const auto& r : model.rectangles) area += r[2]*r[3]*r[4];
        const double volume = area*std::abs(delta.z)*(1+end_scale+end_scale*end_scale)/3;
        std::array<double, 3> mean {};
        std::array<double, 9> second {};
        // Three-point Gauss integrates the section moments (degree <= 4) exactly.
        // This oracle uses rectangles and section integration, not boundary triangles.
        const double node = std::sqrt(15.0)/10;
        for (const auto& tw : std::array<std::array<double, 2>, 3> {{{0.5-node,5.0/18}, {0.5,4.0/9}, {0.5+node,5.0/18}}}) {
            const double t = tw[0], s = 1+t*(end_scale-1);
            for (const auto& r : model.rectangles) {
                const double weight = r[2]*r[3]*r[4]*s*s*std::abs(delta.z)*tw[1]/volume;
                const std::array<double, 3> c {center.x+s*(r[0]+r[2]/2-center.x)+t*delta.x,
                                               center.y+s*(r[1]+r[3]/2-center.y)+t*delta.y, t*delta.z};
                for (int i = 0; i < 3; ++i) {
                    mean[i] += weight*c[i];
                    for (int j = 0; j < 3; ++j) second[3*i+j] += weight*c[i]*c[j];
                }
                second[0] += weight*s*s*r[2]*r[2]/12;
                second[4] += weight*s*s*r[3]*r[3]/12;
            }
        }
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) second[3*i+j] -= mean[i]*mean[j];
        auto profile = model.profile;
        std::size_t n = 0;
        double surface_area = area*(1+end_scale*end_scale);
        axiom::BoundingBox expected_bbox {};
        for (std::size_t r = 0; r <= profile.holes_xyz.size(); ++r) {
            auto& ring = r == 0 ? profile.polygon_xyz : profile.holes_xyz[r-1];
            n += ring.size();
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const auto& a = ring[i];
                const auto& b = ring[(i+1)%ring.size()];
                const double dx = b.x-a.x, dy = b.y-a.y;
                const double vx = delta.x+(end_scale-1)*(a.x-center.x);
                const double vy = delta.y+(end_scale-1)*(a.y-center.y);
                surface_area += (1+end_scale)/2*std::hypot(dy*delta.z, dx*delta.z, dx*vy-dy*vx);
                const auto top = world({center.x+end_scale*(a.x-center.x)+delta.x,
                                         center.y+end_scale*(a.y-center.y)+delta.y, delta.z});
                for (const auto p : {world(a), top}) {
                    if (!expected_bbox.is_valid) expected_bbox = {p,p,true};
                    expected_bbox.min.x = std::min(expected_bbox.min.x,p.x);
                    expected_bbox.min.y = std::min(expected_bbox.min.y,p.y);
                    expected_bbox.min.z = std::min(expected_bbox.min.z,p.z);
                    expected_bbox.max.x = std::max(expected_bbox.max.x,p.x);
                    expected_bbox.max.y = std::max(expected_bbox.max.y,p.y);
                    expected_bbox.max.z = std::max(expected_bbox.max.z,p.z);
                }
            }
            if (variant & (r == 0 ? 1 : 2)) std::reverse(ring.begin(),ring.end());
            std::rotate(ring.begin(),ring.begin()+variant%ring.size(),ring.end());
            for (auto& p : ring) p = world(p);
        }
        if (variant & 1) std::reverse(profile.holes_xyz.begin(),profile.holes_xyz.end());
        const auto d = rotate(delta);
        const double direction_scale = variant & 1 ? 7.0 : 1.0;
        const axiom::Vec3 direction {direction_scale*d.x,direction_scale*d.y,direction_scale*d.z};
        const auto body = kernel.sweeps().extrude_scaled(profile, direction, std::hypot(d.x,d.y,d.z),
                                                        world(center), end_scale);
        if (!body.value || body.status != axiom::StatusCode::Ok) {
            std::cerr << "scaled extrusion failed: " << profile.label << " scale=" << end_scale
                      << " variant=" << variant << " tilted=" << tilted << " dz=" << delta.z << '\n';
            return false;
        }
        const auto query = kernel.topology().query();
        const auto faces = query.faces_of_body(*body.value);
        const auto edges = query.edges_of_body(*body.value);
        const auto vertices = query.vertices_of_body(*body.value);
        const auto shells = query.shells_of_body(*body.value);
        const auto mass = kernel.query().mass_properties(*body.value);
        const auto expected_center = world({mean[0],mean[1],mean[2]});
        const std::size_t f = apex ? 2*n-2 : 4*n+4*profile.holes_xyz.size()-4;
        const std::size_t v = apex ? n+1 : 2*n;
        if (!faces.value || faces.value->size() != f || !edges.value || edges.value->size() != 3*f/2 ||
            !vertices.value || vertices.value->size() != v || !shells.value || shells.value->size() != 1 ||
            !mass.value || std::abs(mass.value->volume-volume) > 1e-8 ||
            std::abs(mass.value->area-surface_area) > 1e-8 ||
            std::hypot(mass.value->centroid.x-expected_center.x, mass.value->centroid.y-expected_center.y,
                       mass.value->centroid.z-expected_center.z) > 1e-8 ||
            kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) {
            std::cerr << "scaled extrusion topology/mass/Strict mismatch: " << profile.label << '\n';
            return false;
        }
        const auto rx = rotate({1,0,0}), ry = rotate({0,1,0}), rz = rotate({0,0,1});
        const std::array<double,9> rotation {rx.x,ry.x,rz.x, rx.y,ry.y,rz.y, rx.z,ry.z,rz.z};
        std::array<double,9> covariance {};
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b)
                covariance[3*i+j] += rotation[3*i+a]*second[3*a+b]*rotation[3*j+b];
        const double trace = covariance[0]+covariance[4]+covariance[8];
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            if (std::abs(mass.value->inertia[3*i+j]-volume*((i == j ? trace : 0)-covariance[3*i+j])) > 1e-7) {
                std::cerr << "scaled extrusion inertia mismatch\n";
                return false;
            }
        double queried_area = 0;
        for (const auto face : *faces.value) {
            const auto a = query.planar_face_area(face);
            const auto owners = query.bodies_of_face(face);
            if (!a.value || *a.value <= 0 || !owners.value || owners.value->size() != 1 ||
                owners.value->front().value != body.value->value) return false;
            queried_area += *a.value;
        }
        if (std::abs(queried_area-surface_area) > 1e-8) return false;
        for (const auto vertex : *vertices.value) {
            const auto exists = query.has_vertex(vertex);
            if (!exists.value || !*exists.value) return false;
        }
        for (const auto edge : *edges.value) {
            const auto uses = query.coedge_count_of_edge(edge);
            const auto neighbors = query.faces_of_edge(edge);
            const auto length = query.edge_length(edge);
            if (!uses.value || *uses.value != 2 || !neighbors.value || neighbors.value->size() != 2 ||
                !length.value || *length.value <= 0) return false;
        }
        if (apex) {
            // Find the shared apex through public edge endpoints and exact spoke
            // lengths; this also rejects disconnected coincident apex vertices.
            std::vector<double> expected_spokes;
            for (const auto& p : model.profile.polygon_xyz)
                expected_spokes.push_back(std::hypot(center.x+delta.x-p.x, center.y+delta.y-p.y, delta.z));
            std::sort(expected_spokes.begin(),expected_spokes.end());
            bool found_apex = false;
            for (const auto vertex : *vertices.value) {
                std::vector<double> spokes;
                for (const auto edge : *edges.value) {
                    const auto ends = query.vertices_of_edge(edge);
                    if (!ends.value || (*ends.value)[0] == (*ends.value)[1]) return false;
                    if ((*ends.value)[0] == vertex || (*ends.value)[1] == vertex) {
                        const auto length = query.edge_length(edge);
                        if (!length.value) return false;
                        spokes.push_back(*length.value);
                    }
                }
                std::sort(spokes.begin(),spokes.end());
                if (spokes.size() == expected_spokes.size() &&
                    std::equal(spokes.begin(),spokes.end(),expected_spokes.begin(),
                               [](double a, double b) { return std::abs(a-b) < 1e-8; })) found_apex = true;
            }
            if (!found_apex) return false;
        }
        for (const auto& bounds : {query.bbox_of_body_from_topology(*body.value), kernel.representation().bbox_of_body(*body.value)}) {
            if (!bounds.value || !bounds.value->is_valid ||
                std::hypot(bounds.value->min.x-expected_bbox.min.x,bounds.value->min.y-expected_bbox.min.y,
                           bounds.value->min.z-expected_bbox.min.z) > 1e-8 ||
                std::hypot(bounds.value->max.x-expected_bbox.max.x,bounds.value->max.y-expected_bbox.max.y,
                           bounds.value->max.z-expected_bbox.max.z) > 1e-8) return false;
        }
        const auto mesh = kernel.convert().brep_to_mesh(*body.value,{});
        if (!mesh.value) return false;
        const auto inspection = kernel.convert().inspect_mesh(*mesh.value);
        if (!inspection.value || inspection.value->tessellation_strategy != "owned_topo_welded" ||
            inspection.value->vertex_count != v || inspection.value->triangle_count != f ||
            inspection.value->connected_components != 1 || inspection.value->has_degenerate_triangles ||
            inspection.value->has_out_of_range_indices) return false;
        if (end_scale == 1.0) {
            const auto ordinary = kernel.sweeps().extrude(profile,{d.x,d.y,d.z},std::hypot(d.x,d.y,d.z));
            if (!ordinary.value) return false;
            const auto other = kernel.query().mass_properties(*ordinary.value);
            if (!other.value || std::abs(other.value->volume-volume) > 1e-8 ||
                std::abs(other.value->area-surface_area) > 1e-8) return false;
        }
    }

    // Minimum profile size: a tetrahedron with an independent closed-form oracle.
    for (const bool reverse : {false, true}) for (const double sign : {-1.0,1.0}) {
        axiom::Kernel kernel;
        axiom::ProfileRef triangle {"apex_triangle",{{0,0,0},{4,0,0},{0,3,0}}};
        if (reverse) std::reverse(triangle.polygon_xyz.begin(),triangle.polygon_xyz.end());
        const auto body = kernel.sweeps().extrude_scaled(triangle,{0,0,sign},2,{0,0,0},0);
        if (!body.value) return false;
        const auto mass = kernel.query().mass_properties(*body.value);
        const std::array<double,9> inertia {1.95,0.6,sign*0.4, 0.6,3.0,sign*0.3, sign*0.4,sign*0.3,3.75};
        if (!mass.value || std::abs(mass.value->volume-4) > 1e-10 ||
            std::abs(mass.value->area-(13+std::sqrt(61.0))) > 1e-10 ||
            std::hypot(mass.value->centroid.x-1,mass.value->centroid.y-0.75,mass.value->centroid.z-sign*0.5) > 1e-10 ||
            kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok)
            return false;
        for (std::size_t i = 0; i < inertia.size(); ++i)
            if (std::abs(mass.value->inertia[i]-inertia[i]) > 1e-10) return false;
        const auto query = kernel.topology().query();
        if (query.vertex_count_of_body(*body.value).value != 4 || query.face_count_of_body(*body.value).value != 4 ||
            query.edge_count_of_body(*body.value).value != 6) return false;
    }

    // Exercise both the positive-scale holed solid and the concave apex through
    // the same failure, warmed-cache and topology edit/rollback contract.
    for (const bool apex : {false, true}) {
        axiom::Kernel kernel;
        const auto& profile = models[apex ? 1 : 3].profile;
        const double source_scale = apex ? 0.0 : 0.5;
        const auto source = kernel.sweeps().extrude_scaled(profile,{0,0,1},3,{0,0,0},source_scale);
        if (!source.value || !kernel.convert().brep_to_mesh(*source.value,{}).value) return false;
        const auto source_mass = kernel.query().mass_properties(*source.value);
        const auto source_faces = kernel.topology().query().faces_of_body(*source.value);
        if (!source_mass.value || !source_faces.value || source_faces.value->empty()) return false;
        auto transaction = kernel.topology().begin_transaction();
        const auto temporary = transaction.create_vertex({20,20,20});
        if (!temporary.value) return false;
        const auto objects = kernel.object_count_total(), geometry = kernel.geometry_count(), bodies = kernel.body_count();
        const auto next = kernel.next_object_id(), writes = transaction.write_operation_count();
        const auto runtime = kernel.runtime_store_counts();
        if (!objects.value || !geometry.value || !bodies.value || !next.value || !writes.value || !runtime.value) return false;
        const auto rejected = [&](const axiom::ProfileRef& p, const axiom::Vec3& direction, double distance,
                                  const axiom::Point3& center, double s) {
            const auto result = kernel.sweeps().extrude_scaled(p,direction,distance,center,s);
            const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
            const auto after = kernel.runtime_store_counts();
            return result.status == axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
                has_issue_code(*diagnostic.value,axiom::diag_codes::kCoreParameterOutOfRange) &&
                kernel.object_count_total().value == objects.value && kernel.geometry_count().value == geometry.value &&
                kernel.body_count().value == bodies.value && kernel.next_object_id().value == next.value &&
                transaction.write_operation_count().value == writes.value && after.value &&
                after.value->mesh_records == runtime.value->mesh_records &&
                after.value->tessellation_cache_entries == runtime.value->tessellation_cache_entries &&
                after.value->face_tessellation_cache_entries == runtime.value->face_tessellation_cache_entries &&
                after.value->curve_eval_cache_entries == runtime.value->curve_eval_cache_entries &&
                after.value->surface_eval_cache_entries == runtime.value->surface_eval_cache_entries;
        };
        const double inf = std::numeric_limits<double>::infinity(), nan = std::numeric_limits<double>::quiet_NaN();
        for (const double s : {-1.0,1e-12,1e150,inf,nan})
            if (!rejected(profile,{0,0,1},3,{0,0,0},s)) return false;
        for (const auto& model : models) if (!model.profile.holes_xyz.empty())
            if (!rejected(model.profile,{0,0,1},3,{0,0,0},0.0)) return false;
        for (const double distance : {0.0,-1.0,1e-20,inf,nan})
            if (!rejected(profile,{0,0,1},distance,{0,0,0},source_scale)) return false;
        for (const auto direction : {axiom::Vec3 {0,0,0}, {1,0,0}, {1,0,1e-9}, {inf,0,1}, {0,nan,1}})
            if (!rejected(profile,direction,3,{0,0,0},source_scale)) return false;
        for (const auto center : {axiom::Point3 {0,0,0.1}, {inf,0,0}, {0,nan,0}})
            if (!rejected(profile,{0,0,1},3,center,source_scale)) return false;
        if (!apex && !rejected(profile,{0,0,1},3,{1e20,1e20,0},source_scale)) return false;
        std::vector<axiom::ProfileRef> invalid {
            {"empty"}, {"",profile.polygon_xyz}, {"short",{{0,0,0},{1,0,0}}},
            {"collinear",{{0,0,0},{1,0,0},{2,0,0}}},
            {"duplicate",{{0,0,0},{3,0,0},{3,3,0},{3,0,0},{0,3,0}}},
            {"near_collinear",{{0,0,0},{1,1e-16,0},{2,0,0},{2,2,0},{0,2,0}}},
            {"crossing",{{0,0,0},{4,3,0},{0,4,0},{3,0,0}}},
            {"nonplanar",{{0,0,0},{3,0,0},{3,3,0.1},{0,3,0}}},
            {"nan",{{0,0,0},{3,0,0},{0,nan,0}}}
        };
        auto collapsed = profile;
        for (auto& p : collapsed.polygon_xyz) p.x += 1e17;
        for (auto& ring : collapsed.holes_xyz) for (auto& p : ring) p.x += 1e17;
        invalid.push_back(collapsed);
        for (const auto& p : invalid) if (!rejected(p,{0,0,1},3,{0,0,0},source_scale)) return false;
        if (apex) {
            auto rounded = profile;
            for (auto& p : rounded.polygon_xyz) p.z = 1e16;
            // The nominal displacement is transverse, but rounds away in world coordinates.
            if (!rejected(rounded,{0,0,1},0.5,{0,0,1e16},0.0)) return false;
        }
        if (transaction.rollback().status != axiom::StatusCode::Ok) return false;
        const auto remains = kernel.topology().query().has_vertex(*temporary.value);
        const auto after_objects = kernel.object_count_total();
        if (!remains.value || *remains.value || !after_objects.value || *after_objects.value+1 != *objects.value) return false;
        // Query the created feature through a real topology edit and rollback.
        auto edit = kernel.topology().begin_transaction();
        if (edit.delete_face(source_faces.value->front()).status != axiom::StatusCode::Ok ||
            edit.rollback().status != axiom::StatusCode::Ok) return false;
        const auto restored = kernel.query().mass_properties(*source.value);
        const auto restored_faces = kernel.topology().query().faces_of_body(*source.value);
        if (!restored.value || std::abs(restored.value->volume-source_mass.value->volume) > 1e-8 ||
            std::abs(restored.value->area-source_mass.value->area) > 1e-8 ||
            restored.value->inertia != source_mass.value->inertia ||
            !restored_faces.value || restored_faces.value->size() != source_faces.value->size() ||
            kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) return false;
        const auto retry = kernel.sweeps().extrude_scaled(profile,{1,0,-3},std::sqrt(10.0),{0,0,0},apex ? 0.0 : 2.0);
        if (!retry.value || kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok)
            return false;
    }
    return true;
}

bool test_twisted_extrusions() {
    constexpr double pi = 3.1415926535897932384626433832795;
    struct Model {
        axiom::ProfileRef profile;
        axiom::Point3 center;
        axiom::Vec3 direction;
        double distance;
        double angle;
    };
    const std::vector<Model> models {
        {{"twisted_convex", {{-2,-1,0},{2,-1,0},{1,2,0},{-1,2,0}}},
         {0,0,0}, {0,0,3}, 4, pi/2},
        {{"twisted_concave", {{0,0,0},{5,0,0},{5,2,0},{2,2,0},{2,5,0},{0,5,0}}},
         {1,1,0}, {0,0,-7}, 3, -pi},
        {{"twisted_hole", {{-4,-3,0},{4,-3,0},{4,3,0},{-4,3,0}},
          {{{-2,-1,0},{-2,1,0},{2,1,0},{2,-1,0}}}},
         {0,0,0}, {0,0,1}, 5, 2*pi}
    };
    for (std::size_t model_index=0; model_index<models.size(); ++model_index) {
        axiom::Kernel kernel;
        auto model=models[model_index];
        if (model_index==1) std::reverse(model.profile.polygon_xyz.begin(),model.profile.polygon_xyz.end());
        const auto ring_area=[](const std::vector<axiom::Point3>& ring) {
            double value=0;
            for (std::size_t i=0; i<ring.size(); ++i) {
                const auto& a=ring[i];
                const auto& b=ring[(i+1)%ring.size()];
                value+=a.x*b.y-a.y*b.x;
            }
            return std::abs(value)/2;
        };
        double area=ring_area(model.profile.polygon_xyz);
        std::size_t boundary_vertices=model.profile.polygon_xyz.size();
        for (const auto& hole:model.profile.holes_xyz) {
            area-=ring_area(hole);
            boundary_vertices+=hole.size();
        }
        if (model_index==0) {
            const auto rotate=[](const axiom::Point3& p) -> axiom::Point3 {
                return {0.6*p.x-0.48*p.y+0.64*p.z,
                        0.8*p.x+0.36*p.y-0.48*p.z,
                        0.8*p.y+0.6*p.z};
            };
            for (auto& p:model.profile.polygon_xyz) {
                p=rotate(p);
                p={p.x+10,p.y-20,p.z+30};
            }
            for (auto& hole:model.profile.holes_xyz) for (auto& p:hole) {
                p=rotate(p);
                p={p.x+10,p.y-20,p.z+30};
            }
            model.center=rotate(model.center);
            model.center={model.center.x+10,model.center.y-20,model.center.z+30};
            const auto rotated_direction=rotate({model.direction.x,model.direction.y,model.direction.z});
            model.direction={rotated_direction.x,rotated_direction.y,rotated_direction.z};
        }
        const auto body=kernel.sweeps().extrude_twisted(
            model.profile,model.direction,model.distance,model.center,model.angle);
        if (!body.value || body.status!=axiom::StatusCode::Ok) {
            std::cerr<<"twisted extrusion failed: "<<model.profile.label<<'\n';
            return false;
        }
        const std::size_t segments=static_cast<std::size_t>(std::ceil(std::abs(model.angle)/(pi/24)));
        const std::size_t cap_triangles=boundary_vertices+2*model.profile.holes_xyz.size()-2;
        const std::size_t expected_faces=2*cap_triangles+2*boundary_vertices*segments;
        const std::size_t expected_vertices=boundary_vertices*(segments+1);
        const std::size_t expected_edges=3*expected_faces/2;
        const auto query=kernel.topology().query();
        const auto faces=query.faces_of_body(*body.value);
        const auto edges=query.edges_of_body(*body.value);
        const auto vertices=query.vertices_of_body(*body.value);
        const auto shells=query.shells_of_body(*body.value);
        const auto mass=kernel.query().mass_properties(*body.value);
        const auto topo_mass=query.body_mass_properties(*body.value);
        const auto bbox=query.bbox_of_body_from_topology(*body.value);
        if (!faces.value || faces.value->size()!=expected_faces ||
            !edges.value || edges.value->size()!=expected_edges ||
            !vertices.value || vertices.value->size()!=expected_vertices ||
            !shells.value || shells.value->size()!=1 || !mass.value || !topo_mass.value ||
            !(mass.value->volume>0) || std::abs(mass.value->volume-area*model.distance)>area*model.distance*0.005 ||
            std::abs(topo_mass.value->volume-mass.value->volume)>1e-8 ||
            std::abs(topo_mass.value->area-mass.value->area)>1e-8 ||
            !bbox.value || !bbox.value->is_valid ||
            kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok) {
            std::cerr<<"twisted extrusion topology/mass/Strict mismatch: "<<model.profile.label<<'\n';
            return false;
        }
        const auto shell_owners=query.bodies_of_shell(shells.value->front());
        if (!shell_owners.value || shell_owners.value->size()!=1 ||
            shell_owners.value->front().value!=body.value->value) return false;
        double queried_area=0;
        for (const auto face:*faces.value) {
            const auto owners=query.bodies_of_face(face);
            const auto loops=query.loops_of_face(face);
            const auto face_area=query.planar_face_area(face);
            if (!owners.value || owners.value->size()!=1 || owners.value->front().value!=body.value->value ||
                !loops.value || loops.value->size()!=1 || !face_area.value || !(*face_area.value>0)) return false;
            queried_area+=*face_area.value;
        }
        if (std::abs(queried_area-mass.value->area)>1e-8) return false;
        for (const auto edge:*edges.value) {
            const auto uses=query.coedge_count_of_edge(edge);
            const auto adjacent=query.faces_of_edge(edge);
            const auto owners=query.owner_count_of_edge(edge);
            if (!uses.value || *uses.value!=2 || !adjacent.value || adjacent.value->size()!=2 ||
                !owners.value || *owners.value!=1) return false;
        }
        const auto mesh=kernel.convert().brep_to_mesh(*body.value,{});
        if (!mesh.value) return false;
        const auto inspection=kernel.convert().inspect_mesh(*mesh.value);
        if (!inspection.value || inspection.value->tessellation_strategy!="owned_topo_welded" ||
            inspection.value->vertex_count!=expected_vertices || inspection.value->triangle_count!=expected_faces ||
            inspection.value->connected_components!=1 || inspection.value->has_degenerate_triangles ||
            inspection.value->has_out_of_range_indices) return false;
    }

    // Zero twist is the ordinary extrusion contract, including direction normalization.
    {
        axiom::Kernel kernel;
        const axiom::ProfileRef profile {"zero_twist",{{0,0,0},{3,0,0},{3,2,0},{0,2,0}}};
        const auto twisted=kernel.sweeps().extrude_twisted(profile,{0,0,-9},4,{1,1,0},0);
        const auto ordinary=kernel.sweeps().extrude(profile,{0,0,-1},4);
        if (!twisted.value || !ordinary.value) return false;
        const auto a=kernel.query().mass_properties(*twisted.value);
        const auto b=kernel.query().mass_properties(*ordinary.value);
        if (!a.value || !b.value || std::abs(a.value->volume-b.value->volume)>1e-10 ||
            std::abs(a.value->area-b.value->area)>1e-10 ||
            std::hypot(a.value->centroid.x-b.value->centroid.x,a.value->centroid.y-b.value->centroid.y,
                       a.value->centroid.z-b.value->centroid.z)>1e-10 ||
            kernel.topology().query().face_count_of_body(*twisted.value).value!=
                kernel.topology().query().face_count_of_body(*ordinary.value).value) return false;
    }

    axiom::Kernel kernel;
    const auto profile=models.back().profile;
    const auto source=kernel.sweeps().extrude_twisted(profile,{0,0,1},3,{0,0,0},pi/2);
    if (!source.value || !kernel.convert().brep_to_mesh(*source.value,{}).value) return false;
    const auto source_mass=kernel.query().mass_properties(*source.value);
    const auto source_faces=kernel.topology().query().faces_of_body(*source.value);
    if (!source_mass.value || !source_faces.value || source_faces.value->empty()) return false;
    auto transaction=kernel.topology().begin_transaction();
    const auto temporary=transaction.create_vertex({70,71,72});
    const auto objects=kernel.object_count_total(), geometry=kernel.geometry_count(), bodies=kernel.body_count();
    const auto next=kernel.next_object_id(), writes=transaction.write_operation_count();
    const auto runtime=kernel.runtime_store_counts();
    if (!temporary.value || !objects.value || !geometry.value || !bodies.value || !next.value || !writes.value ||
        !runtime.value) return false;
    const auto rejected=[&](const axiom::ProfileRef& p,const axiom::Vec3& direction,double distance,
                            const axiom::Point3& center,double angle) {
        const auto result=kernel.sweeps().extrude_twisted(p,direction,distance,center,angle);
        const auto diagnostic=kernel.diagnostics().get(result.diagnostic_id);
        const auto after=kernel.runtime_store_counts();
        return result.status==axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,axiom::diag_codes::kCoreParameterOutOfRange) &&
            kernel.object_count_total().value==objects.value && kernel.geometry_count().value==geometry.value &&
            kernel.body_count().value==bodies.value && kernel.next_object_id().value==next.value &&
            transaction.write_operation_count().value==writes.value && after.value &&
            after.value->mesh_records==runtime.value->mesh_records &&
            after.value->tessellation_cache_entries==runtime.value->tessellation_cache_entries &&
            after.value->face_tessellation_cache_entries==runtime.value->face_tessellation_cache_entries &&
            after.value->curve_eval_cache_entries==runtime.value->curve_eval_cache_entries &&
            after.value->surface_eval_cache_entries==runtime.value->surface_eval_cache_entries;
    };
    const double inf=std::numeric_limits<double>::infinity();
    const double nan=std::numeric_limits<double>::quiet_NaN();
    for (const double angle:{-2*pi-1e-6,2*pi+1e-6,inf,nan})
        if (!rejected(profile,{0,0,1},3,{0,0,0},angle)) return false;
    for (const double distance:{0.0,-1.0,1e-20,inf,nan})
        if (!rejected(profile,{0,0,1},distance,{0,0,0},pi/2)) return false;
    for (const auto direction:{axiom::Vec3 {0,0,0},{1,0,0},{1,0,1e-8},{inf,0,1},{0,nan,1}})
        if (!rejected(profile,direction,3,{0,0,0},pi/2)) return false;
    for (const auto center:{axiom::Point3 {0,0,0.1},{inf,0,0},{0,nan,0},{1e308,1e308,0}})
        if (!rejected(profile,{0,0,1},3,center,pi/2)) return false;
    const std::vector<axiom::ProfileRef> invalid {
        {"empty"}, {"",profile.polygon_xyz}, {"short",{{0,0,0},{1,0,0}}},
        {"collinear",{{0,0,0},{1,0,0},{2,0,0}}},
        {"duplicate",{{0,0,0},{3,0,0},{3,3,0},{3,0,0},{0,3,0}}},
        {"crossing",{{0,0,0},{4,3,0},{0,4,0},{3,0,0}}},
        {"nonplanar",{{0,0,0},{3,0,0},{3,3,0.1},{0,3,0}}},
        {"nan",{{0,0,0},{3,0,0},{0,nan,0}}},
        {"bad_hole",profile.polygon_xyz,{{{3,1,0},{5,1,0},{5,2,0},{3,2,0}}}},
        {"touching_hole",profile.polygon_xyz,{{{-4,0,0},{-2,0,0},{-2,1,0},{-4,1,0}}}}
    };
    for (const auto& p:invalid) if (!rejected(p,{0,0,1},3,{0,0,0},pi/2)) return false;
    if (transaction.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto remains=kernel.topology().query().has_vertex(*temporary.value);
    const auto after_rollback=kernel.object_count_total();
    if (!remains.value || *remains.value || !after_rollback.value || *after_rollback.value+1!=*objects.value)
        return false;
    auto edit=kernel.topology().begin_transaction();
    if (edit.delete_face(source_faces.value->front()).status!=axiom::StatusCode::Ok ||
        edit.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto restored=kernel.query().mass_properties(*source.value);
    const auto restored_faces=kernel.topology().query().faces_of_body(*source.value);
    if (!restored.value || std::abs(restored.value->volume-source_mass.value->volume)>1e-8 ||
        std::abs(restored.value->area-source_mass.value->area)>1e-8 ||
        restored.value->inertia!=source_mass.value->inertia || !restored_faces.value ||
        restored_faces.value->size()!=source_faces.value->size() ||
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok)
        return false;
    const auto retry=kernel.sweeps().extrude_twisted(profile,{0,0,-11},3,{0,0,0},-pi/3);
    return retry.value &&
        kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok;
}

bool test_extrusion_section_laws() {
    constexpr double pi = 3.1415926535897932384626433832795;
    struct Model {
        axiom::ProfileRef profile;
        std::vector<std::array<double,5>> rectangles;
        std::vector<axiom::ExtrusionLawStation> law;
    };
    const std::vector<Model> models {
        {{"law_bulge",{{-2,-1,0},{2,-1,0},{2,1,0},{-2,1,0}}},
         {{-2,-1,4,2,1}}, {{0,1,0},{0.25,2,0},{0.75,0.5,0},{1,1,0}}},
        {{"law_concave",{{0,0,0},{5,0,0},{5,2,0},{2,2,0},{2,5,0},{0,5,0}}},
         {{0,0,5,2,1},{0,2,2,3,1}}, {{0,1,0},{0.3,0.75,0},{0.6,1.5,0},{1,0.5,0}}},
        {{"law_two_holes",{{-4,-3,0},{4,-3,0},{4,3,0},{-4,3,0}},
          {{{-3,-1,0},{-1,-1,0},{-1,1,0},{-3,1,0}},{{1,-1,0},{3,-1,0},{3,1,0},{1,1,0}}}},
         {{-4,-3,8,6,1},{-3,-1,2,2,-1},{1,-1,2,2,-1}},
         {{0,1,0},{0.5,1.5,0},{1,0.75,0}}},
        {{"law_twist_reverse",{{-2,-1,0},{2,-1,0},{2,1,0},{-2,1,0}}},
         {{-2,-1,4,2,1}}, {{0,1,0},{0.4,1.5,pi/2},{0.6,1.5,pi/2},{1,0.75,0}}},
        {{"law_twisted_hole",{{-4,-3,0},{4,-3,0},{4,3,0},{-4,3,0}},
          {{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}}},
         {{-4,-3,8,6,1},{-1,-1,2,2,-1}}, {{0,1,0},{0.5,1.5,-pi/2},{1,0.75,-pi}}},
        {{"law_twisted_concave",{{0,0,0},{5,0,0},{5,2,0},{2,2,0},{2,5,0},{0,5,0}}},
         {{0,0,5,2,1},{0,2,2,3,1}}, {{0,1,0},{0.5,1.5,pi/4},{1,0.75,pi/2}}}
    };
    for (std::size_t model_index = 0; model_index < models.size(); ++model_index)
    for (const bool tilted : {false,true}) {
        axiom::Kernel kernel;
        auto model = models[model_index];
        const double height = 5, direction_sign = tilted ? -1 : 1;
        const axiom::Point3 center = model_index == 1 ? axiom::Point3 {-5,6,0} : axiom::Point3 {0.5,0.25,0};
        const auto rotate = [tilted](axiom::Point3 p) -> axiom::Point3 {
            if (!tilted) return p;
            return {0.6*p.x-0.48*p.y+0.64*p.z,0.8*p.x+0.36*p.y-0.48*p.z,0.8*p.y+0.6*p.z};
        };
        const auto world = [&](axiom::Point3 p) -> axiom::Point3 {
            const auto q = rotate(p);
            return {q.x+10,q.y-20,q.z+30};
        };
        // Independent volume/moment integration over signed rectangles and each
        // caller interval. With zero twist, three Gauss points are exact for the
        // fourth-degree mass moments of a linearly scaled extrusion.
        const double node = std::sqrt(15.0)/10;
        const std::array<std::array<double,2>,3> gauss {{{0.5-node,5.0/18},{0.5,4.0/9},{0.5+node,5.0/18}}};
        double expected_volume = 0;
        std::array<double,3> first {};
        std::array<double,9> second {};
        bool has_twist = false;
        std::size_t intervals = 0;
        axiom::BoundingBox expected_bounds {};
        for (std::size_t k = 1; k < model.law.size(); ++k) {
            const auto& a = model.law[k-1];
            const auto& b = model.law[k];
            has_twist = has_twist || a.twist_angle != 0 || b.twist_angle != 0;
            for (const auto& r : model.rectangles) for (const auto& gx : gauss)
            for (const auto& gy : gauss) for (const auto& gt : gauss) {
                const double s = a.scale+gt[0]*(b.scale-a.scale);
                const double fraction = a.fraction+gt[0]*(b.fraction-a.fraction);
                const double weight = r[2]*r[3]*r[4]*height*(b.fraction-a.fraction)*s*s*gx[1]*gy[1]*gt[1];
                const auto p = world({center.x+s*(r[0]+r[2]*gx[0]-center.x),
                                      center.y+s*(r[1]+r[3]*gy[0]-center.y),direction_sign*height*fraction});
                const std::array<double,3> q {p.x,p.y,p.z};
                expected_volume += weight;
                for (int i = 0; i < 3; ++i) {
                    first[i] += weight*q[i];
                    for (int j = 0; j < 3; ++j) second[3*i+j] += weight*q[i]*q[j];
                }
            }
            const auto count = static_cast<std::size_t>(std::max({1.0,
                std::ceil(std::abs(b.twist_angle-a.twist_angle)/(pi/24)),
                std::ceil(std::abs(b.scale-a.scale)/std::min(a.scale,b.scale)/0.25)}));
            intervals += count;
            for (std::size_t j = 0; j <= count; ++j) {
                const double t = static_cast<double>(j)/static_cast<double>(count);
                const double s = a.scale+t*(b.scale-a.scale);
                const double angle = direction_sign*(a.twist_angle+t*(b.twist_angle-a.twist_angle));
                const double z = direction_sign*height*(a.fraction+t*(b.fraction-a.fraction));
                for (std::size_t ring = 0; ring <= model.profile.holes_xyz.size(); ++ring) {
                    const auto& points = ring == 0 ? model.profile.polygon_xyz : model.profile.holes_xyz[ring-1];
                    for (const auto& p : points) {
                        const double x = s*(p.x-center.x), y = s*(p.y-center.y);
                        const auto q = world({center.x+x*std::cos(angle)-y*std::sin(angle),
                                              center.y+x*std::sin(angle)+y*std::cos(angle),z});
                        if (!expected_bounds.is_valid) expected_bounds = {q,q,true};
                        expected_bounds.min.x = std::min(expected_bounds.min.x,q.x);
                        expected_bounds.min.y = std::min(expected_bounds.min.y,q.y);
                        expected_bounds.min.z = std::min(expected_bounds.min.z,q.z);
                        expected_bounds.max.x = std::max(expected_bounds.max.x,q.x);
                        expected_bounds.max.y = std::max(expected_bounds.max.y,q.y);
                        expected_bounds.max.z = std::max(expected_bounds.max.z,q.z);
                    }
                }
            }
        }
        std::size_t ring_vertices = 0;
        for (std::size_t ring = 0; ring <= model.profile.holes_xyz.size(); ++ring) {
            auto& points = ring == 0 ? model.profile.polygon_xyz : model.profile.holes_xyz[ring-1];
            ring_vertices += points.size();
            if (tilted || ring != 0) std::reverse(points.begin(),points.end());
            std::rotate(points.begin(),points.begin()+model_index%points.size(),points.end());
            for (auto& p : points) p = world(p);
        }
        if (tilted) std::reverse(model.profile.holes_xyz.begin(),model.profile.holes_xyz.end());
        const auto d = rotate({0,0,7*direction_sign});
        const auto body = kernel.sweeps().extrude_with_law(model.profile,{d.x,d.y,d.z},height,world(center),model.law);
        if (!body.value || body.status != axiom::StatusCode::Ok) {
            std::cerr << "section law extrusion failed: " << model.profile.label << " tilted=" << tilted << '\n';
            return false;
        }
        const auto query = kernel.topology().query();
        const auto faces = query.faces_of_body(*body.value);
        const auto edges = query.edges_of_body(*body.value);
        const auto vertices = query.vertices_of_body(*body.value);
        const auto shells = query.shells_of_body(*body.value);
        const auto mass = kernel.query().mass_properties(*body.value);
        const auto actual = query.body_mass_properties(*body.value);
        const std::size_t face_count = 2*(ring_vertices+2*model.profile.holes_xyz.size()-2)+2*ring_vertices*intervals;
        const double volume_tol = has_twist ? expected_volume*0.015 : 1e-8;
        if (!faces.value || faces.value->size() != face_count || !edges.value || edges.value->size() != 3*face_count/2 ||
            !vertices.value || vertices.value->size() != ring_vertices*(intervals+1) ||
            !shells.value || shells.value->size() != 1 || !mass.value || !actual.value ||
            std::abs(mass.value->volume-expected_volume) > volume_tol ||
            std::abs(actual.value->volume-mass.value->volume) > 1e-8 ||
            std::abs(actual.value->area-mass.value->area) > 1e-8 ||
            kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) {
            std::cerr << "section law topology/mass/Strict mismatch: " << model.profile.label << '\n';
            return false;
        }
        for (int i = 0; i < 9; ++i)
            if (std::abs(actual.value->inertia[i]-mass.value->inertia[i]) > 1e-7) return false;
        if (!has_twist) {
            for (auto& value : first) value /= expected_volume;
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
                second[3*i+j] = second[3*i+j]/expected_volume-first[i]*first[j];
            const double trace = second[0]+second[4]+second[8];
            if (std::hypot(mass.value->centroid.x-first[0],mass.value->centroid.y-first[1],
                           mass.value->centroid.z-first[2]) > 1e-8) return false;
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
                if (std::abs(mass.value->inertia[3*i+j]-expected_volume*((i == j ? trace : 0)-second[3*i+j])) > 1e-6)
                    return false;
        }
        for (const auto bounds : {query.bbox_of_body_from_topology(*body.value),kernel.representation().bbox_of_body(*body.value)})
            if (!bounds.value || !bounds.value->is_valid ||
                std::hypot(bounds.value->min.x-expected_bounds.min.x,bounds.value->min.y-expected_bounds.min.y,
                           bounds.value->min.z-expected_bounds.min.z) > 1e-8 ||
                std::hypot(bounds.value->max.x-expected_bounds.max.x,bounds.value->max.y-expected_bounds.max.y,
                           bounds.value->max.z-expected_bounds.max.z) > 1e-8) return false;
        double face_area = 0;
        for (const auto face : *faces.value) {
            const auto owners = query.bodies_of_face(face);
            const auto loops = query.loops_of_face(face);
            const auto area = query.planar_face_area(face);
            if (!owners.value || owners.value->size() != 1 || owners.value->front().value != body.value->value ||
                !loops.value || loops.value->size() != 1 || !area.value || *area.value <= 0) return false;
            face_area += *area.value;
        }
        if (std::abs(face_area-mass.value->area) > 1e-8) return false;
        for (const auto edge : *edges.value) {
            const auto uses = query.coedge_count_of_edge(edge);
            const auto adjacent = query.faces_of_edge(edge);
            const auto length = query.edge_length(edge);
            if (!uses.value || *uses.value != 2 || !adjacent.value || adjacent.value->size() != 2 ||
                !length.value || *length.value <= 0) return false;
        }
        const auto mesh = kernel.convert().brep_to_mesh(*body.value,{});
        if (!mesh.value) return false;
        const auto inspection = kernel.convert().inspect_mesh(*mesh.value);
        if (!inspection.value || inspection.value->tessellation_strategy != "owned_topo_welded" ||
            inspection.value->vertex_count != vertices.value->size() || inspection.value->triangle_count != face_count ||
            inspection.value->connected_components != 1 || inspection.value->has_degenerate_triangles ||
            inspection.value->has_out_of_range_indices) return false;
    }

    // Two-key laws retain the existing scale-only and twist-only semantics.
    {
        axiom::Kernel kernel;
        const auto& profile = models.front().profile;
        for (const auto end : {axiom::ExtrusionLawStation {1,1,0},{1,1.5,0},{1,1,pi/2},{1,1,-2*pi}}) {
            const std::array<axiom::ExtrusionLawStation,2> law {{{0,1,0},end}};
            const auto body = kernel.sweeps().extrude_with_law(profile,{0,0,1},5,{0,0,0},law);
            const auto reference = end.twist_angle == 0 ? kernel.sweeps().extrude_scaled(profile,{0,0,1},5,{0,0,0},end.scale)
                : kernel.sweeps().extrude_twisted(profile,{0,0,1},5,{0,0,0},end.twist_angle);
            if (!body.value || !reference.value) return false;
            const auto a = kernel.query().mass_properties(*body.value);
            const auto b = kernel.query().mass_properties(*reference.value);
            if (!a.value || !b.value || std::abs(a.value->volume-b.value->volume) > 1e-8 ||
                std::abs(a.value->area-b.value->area) > 1e-8 ||
                std::hypot(a.value->centroid.x-b.value->centroid.x,a.value->centroid.y-b.value->centroid.y,
                           a.value->centroid.z-b.value->centroid.z) > 1e-8) return false;
            for (int i = 0; i < 9; ++i) if (std::abs(a.value->inertia[i]-b.value->inertia[i]) > 1e-7) return false;
        }
    }

    // Every failure, including intermediate rounded sections and law sampling
    // overflow, must preserve existing bodies/caches and active transaction writes.
    axiom::Kernel kernel;
    const auto& profile = models.front().profile;
    const auto& good_law = models.front().law;
    const auto source = kernel.sweeps().extrude_with_law(profile,{0,0,1},5,{0,0,0},good_law);
    if (!source.value || !kernel.convert().brep_to_mesh(*source.value,{}).value) return false;
    const auto source_mass = kernel.query().mass_properties(*source.value);
    const auto source_faces = kernel.topology().query().faces_of_body(*source.value);
    if (!source_mass.value || !source_faces.value || source_faces.value->empty()) return false;
    auto transaction = kernel.topology().begin_transaction();
    const auto temporary = transaction.create_vertex({71,72,73});
    const auto objects = kernel.object_count_total(), geometry = kernel.geometry_count(), bodies = kernel.body_count();
    const auto next = kernel.next_object_id(), writes = transaction.write_operation_count();
    const auto runtime = kernel.runtime_store_counts();
    if (!temporary.value || !objects.value || !geometry.value || !bodies.value || !next.value || !writes.value || !runtime.value)
        return false;
    const auto rejected = [&](const axiom::ProfileRef& p, const axiom::Vec3& direction, double distance,
                              const axiom::Point3& center, const std::vector<axiom::ExtrusionLawStation>& law) {
        const auto result = kernel.sweeps().extrude_with_law(p,direction,distance,center,law);
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto after = kernel.runtime_store_counts();
        return result.status == axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,axiom::diag_codes::kCoreParameterOutOfRange) &&
            kernel.object_count_total().value == objects.value && kernel.geometry_count().value == geometry.value &&
            kernel.body_count().value == bodies.value && kernel.next_object_id().value == next.value &&
            transaction.write_operation_count().value == writes.value && after.value &&
            after.value->mesh_records == runtime.value->mesh_records &&
            after.value->tessellation_cache_entries == runtime.value->tessellation_cache_entries &&
            after.value->face_tessellation_cache_entries == runtime.value->face_tessellation_cache_entries &&
            after.value->curve_eval_cache_entries == runtime.value->curve_eval_cache_entries &&
            after.value->surface_eval_cache_entries == runtime.value->surface_eval_cache_entries;
    };
    const double inf = std::numeric_limits<double>::infinity(), nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<std::vector<axiom::ExtrusionLawStation>> invalid_laws {
        {}, {{0,1,0}}, {{0.1,1,0},{1,1,0}}, {{0,1,0},{0.9,1,0}},
        {{0,2,0},{1,1,0}}, {{0,1,0.1},{1,1,0}},
        {{0,1,0},{0.5,1,0},{0.5,1.2,0},{1,1,0}}, {{0,1,0},{0.7,1,0},{0.3,1,0},{1,1,0}},
        {{0,1,0},{-0.5,1,0},{1,1,0}}, {{0,1,0},{1.5,1,0},{1,1,0}},
        {{0,1,0},{nan,1,0},{1,1,0}}, {{0,1,0},{0.5,inf,0},{1,1,0}},
        {{0,1,0},{0.5,1,nan},{1,1,0}}, {{0,1,0},{1,1,inf}},
        {{0,1,0},{1,0,0}}, {{0,1,0},{0.5,-1,0},{1,1,0}},
        {{0,1,0},{0.5,1,2*pi},{1,1,0}}, {{0,1,0},{1,1,2*pi+1e-5}},
        {{0,1,0},{0.5,1e-20,0},{1,1,0}}, {{0,1,0},{1,1e200,0}},
        {{0,1,0},{1e-20,1,pi/4},{1,1,pi/2}}
    };
    for (const auto& law : invalid_laws) if (!rejected(profile,{0,0,1},5,{0,0,0},law)) return false;
    std::vector<axiom::ExtrusionLawStation> excessive_keys(4098);
    for (std::size_t i = 0; i < excessive_keys.size(); ++i)
        excessive_keys[i] = {static_cast<double>(i)/static_cast<double>(excessive_keys.size()-1),1,0};
    if (!rejected(profile,{0,0,1},5,{0,0,0},excessive_keys)) return false;
    // Intermediate numerical collapse with a moderate scale ratio (no budget failure).
    auto rounded = profile;
    for (auto& p : rounded.polygon_xyz) p.x += 1e16;
    if (!rejected(rounded,{0,0,1},5,{1e16,0,0},{{0,1,0},{0.5,0.25,0},{1,1,0}})) return false;
    for (const double distance : {0.0,-1.0,1e-20,inf,nan})
        if (!rejected(profile,{0,0,1},distance,{0,0,0},good_law)) return false;
    for (const auto direction : {axiom::Vec3 {0,0,0},{1,0,0},{1,0,1},{inf,0,1},{0,nan,1}})
        if (!rejected(profile,direction,5,{0,0,0},good_law)) return false;
    for (const auto center : {axiom::Point3 {0,0,0.1},{inf,0,0},{0,nan,0},{1e308,1e308,0}})
        if (!rejected(profile,{0,0,1},5,center,good_law)) return false;
    const std::vector<axiom::ProfileRef> invalid_profiles {
        {"empty"},{"",profile.polygon_xyz},{"short",{{0,0,0},{1,0,0}}},
        {"collinear",{{0,0,0},{1,0,0},{2,0,0}}},
        {"duplicate",{{0,0,0},{3,0,0},{3,3,0},{3,0,0},{0,3,0}}},
        {"crossing",{{0,0,0},{4,3,0},{0,4,0},{3,0,0}}},
        {"nonplanar",{{0,0,0},{3,0,0},{3,3,0.1},{0,3,0}}},
        {"nan",{{0,0,0},{3,0,0},{0,nan,0}}},
        {"outside_hole",profile.polygon_xyz,{{{1,0,0},{3,0,0},{3,0.5,0},{1,0.5,0}}}},
        {"touching_hole",profile.polygon_xyz,{{{-2,0,0},{-1,0,0},{-1,0.5,0},{-2,0.5,0}}}},
        {"nested_holes",{{-4,-4,0},{4,-4,0},{4,4,0},{-4,4,0}},
          {{{-2,-2,0},{2,-2,0},{2,2,0},{-2,2,0}},{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}}}
    };
    for (const auto& p : invalid_profiles) if (!rejected(p,{0,0,1},5,{0,0,0},good_law)) return false;
    // A geometrically valid holed region can still place its nonadjacent side
    // triangles in tolerance contact. Reject the real walls before allocation.
    const axiom::ProfileRef near_contact {"law_wall_contact",profile.polygon_xyz,
        {{{1,-0.5,0},{2-5e-7,-0.5,0},{2-5e-7,0.5,0},{1,0.5,0}}}};
    if (!rejected(near_contact,{0,0,1},5,{0,0,0},{{0,1,0},{1,1,0}})) return false;
    if (transaction.rollback().status != axiom::StatusCode::Ok) return false;
    const auto remains = kernel.topology().query().has_vertex(*temporary.value);
    const auto after_rollback = kernel.object_count_total();
    if (!remains.value || *remains.value || !after_rollback.value || *after_rollback.value+1 != *objects.value)
        return false;
    auto edit = kernel.topology().begin_transaction();
    if (edit.delete_face(source_faces.value->front()).status != axiom::StatusCode::Ok) return false;
    if (kernel.topology().query().body_mass_properties(*source.value).status == axiom::StatusCode::Ok) return false;
    if (edit.rollback().status != axiom::StatusCode::Ok) return false;
    const auto restored = kernel.query().mass_properties(*source.value);
    const auto restored_faces = kernel.topology().query().faces_of_body(*source.value);
    if (!restored.value || !restored_faces.value || restored_faces.value->size() != source_faces.value->size() ||
        restored.value->inertia != source_mass.value->inertia ||
        std::abs(restored.value->volume-source_mass.value->volume) > 1e-8 ||
        std::abs(restored.value->area-source_mass.value->area) > 1e-8 ||
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) return false;
    const auto retry = kernel.sweeps().extrude_with_law(profile,{0,0,-11},5,{0,0,0},models[3].law);
    return retry.value && kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok;
}

bool test_extrusions_to_plane() {
    struct Model {
        axiom::ProfileRef profile;
        // Signed rectangle decomposition independent of the topology triangulation.
        std::vector<std::array<double, 5>> rectangles;
    };
    const std::vector<Model> models {
        {{"rectangle", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}}}, {{0,0,8,6,1}}},
        {{"concave", {{0,0,0}, {6,0,0}, {6,2,0}, {2,2,0}, {2,6,0}, {0,6,0}}},
         {{0,0,6,2,1}, {0,2,2,4,1}}},
        {{"single_hole", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}},
          {{{1,1,0}, {3,1,0}, {3,3,0}, {1,3,0}}}}, {{0,0,8,6,1}, {1,1,2,2,-1}}},
        {{"two_holes", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}},
          {{{1,1,0}, {3,1,0}, {3,3,0}, {1,3,0}}, {{5,2,0}, {7,2,0}, {7,5,0}, {5,5,0}}}},
         {{0,0,8,6,1}, {1,1,2,2,-1}, {5,2,2,3,-1}}},
        {{"concave_hole", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}},
          {{{1,1,0}, {4,1,0}, {4,2,0}, {2,2,0}, {2,4,0}, {1,4,0}}}},
         {{0,0,8,6,1}, {1,1,3,1,-1}, {1,2,1,2,-1}}}
    };
    for (const auto& model : models) for (int variant = 0; variant < 4; ++variant)
    for (const bool tilted : {false, true}) for (const bool slanted : {false, true})
    for (const axiom::Point3 d : {axiom::Point3 {0,0,1}, {0.4,-0.2,1}, {-0.4,0.2,-1}}) {
        axiom::Kernel kernel;
        const auto rotate = [tilted](axiom::Point3 p) -> axiom::Point3 {
            if (!tilted) return p;
            return {0.6*p.x-0.48*p.y+0.64*p.z, 0.8*p.x+0.36*p.y-0.48*p.z, 0.8*p.y+0.6*p.z};
        };
        const auto world = [&](axiom::Point3 p) -> axiom::Point3 {
            const auto q = rotate(p);
            return {q.x+10,q.y-20,q.z+30};
        };
        // Local target: sign*z = 4 + a*x + b*y; travel along (dx,dy,sign).
        const double a = slanted ? 0.2 : 0, b = slanted ? -0.1 : 0;
        const double denominator = 1-a*d.x-b*d.y;
        const auto height = [&](double x, double y) { return (4+a*x+b*y)/denominator; };
        double area = 0, volume = 0;
        std::array<double,3> mean {};
        std::array<double,9> second {};
        const double node = std::sqrt(15.0)/10;
        const std::array<std::array<double,2>,3> gauss {{{0.5-node,5.0/18},{0.5,4.0/9},{0.5+node,5.0/18}}};
        // Independent signed-rectangle integration over x,y and ray height.
        // Three-point Gauss is exact for these polynomial moments (degree <= 3).
        for (const auto& r : model.rectangles) {
            area += r[2]*r[3]*r[4];
            volume += r[2]*r[3]*r[4]*height(r[0]+r[2]/2,r[1]+r[3]/2);
            for (const auto& gx : gauss) for (const auto& gy : gauss) for (const auto& gz : gauss) {
                const double x = r[0]+r[2]*gx[0], y = r[1]+r[3]*gy[0], h = height(x,y);
                const double t = h*gz[0], weight = r[2]*r[3]*r[4]*h*gx[1]*gy[1]*gz[1];
                const std::array<double,3> q {x+d.x*t,y+d.y*t,d.z*t};
                for (int i = 0; i < 3; ++i) {
                    mean[i] += weight*q[i];
                    for (int j = 0; j < 3; ++j) second[3*i+j] += weight*q[i]*q[j];
                }
            }
        }
        for (auto& v : mean) v /= volume;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            second[3*i+j] = second[3*i+j]/volume-mean[i]*mean[j];
        double surface_area = area*(1+std::sqrt(1+a*a+b*b)/denominator);
        axiom::BoundingBox expected_bbox {};
        auto profile = model.profile;
        std::size_t n = 0;
        for (std::size_t r = 0; r <= profile.holes_xyz.size(); ++r) {
            auto& ring = r == 0 ? profile.polygon_xyz : profile.holes_xyz[r-1];
            n += ring.size();
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const auto& p = ring[i];
                const auto& q = ring[(i+1)%ring.size()];
                const double ex = q.x-p.x, ey = q.y-p.y, h = height(p.x,p.y);
                surface_area += 0.5*(h+height(q.x,q.y))*std::hypot(ey*d.z,ex*d.z,ex*d.y-ey*d.x);
                for (const auto w : {world(p),world({p.x+d.x*h,p.y+d.y*h,d.z*h})}) {
                    if (!expected_bbox.is_valid) expected_bbox = {w,w,true};
                    expected_bbox.min.x = std::min(expected_bbox.min.x,w.x);
                    expected_bbox.min.y = std::min(expected_bbox.min.y,w.y);
                    expected_bbox.min.z = std::min(expected_bbox.min.z,w.z);
                    expected_bbox.max.x = std::max(expected_bbox.max.x,w.x);
                    expected_bbox.max.y = std::max(expected_bbox.max.y,w.y);
                    expected_bbox.max.z = std::max(expected_bbox.max.z,w.z);
                }
            }
            if (variant & (r == 0 ? 1 : 2)) std::reverse(ring.begin(),ring.end());
            std::rotate(ring.begin(),ring.begin()+variant%ring.size(),ring.end());
            for (auto& p : ring) p = world(p);
        }
        if (variant & 1) std::reverse(profile.holes_xyz.begin(),profile.holes_xyz.end());
        const auto direction = rotate(d), normal = rotate({-a,-b,d.z});
        const double ds = variant & 1 ? 7 : 1, ns = variant & 2 ? -3 : 1;
        const axiom::Plane plane {world({0,0,4*d.z}),{ns*normal.x,ns*normal.y,ns*normal.z}};
        const auto body = kernel.sweeps().extrude_to_plane(profile,
            {ds*direction.x,ds*direction.y,ds*direction.z},plane);
        if (!body.value || body.status != axiom::StatusCode::Ok) {
            std::cerr << "extrusion to plane failed: " << profile.label << " variant=" << variant
                      << " tilted=" << tilted << " slanted=" << slanted << " dz=" << d.z << '\n';
            return false;
        }
        const auto query = kernel.topology().query();
        const auto faces = query.faces_of_body(*body.value);
        const auto edges = query.edges_of_body(*body.value);
        const auto vertices = query.vertices_of_body(*body.value);
        const auto shells = query.shells_of_body(*body.value);
        const auto mass = kernel.query().mass_properties(*body.value);
        const auto expected_center = world({mean[0],mean[1],mean[2]});
        const std::size_t f = 4*n+4*profile.holes_xyz.size()-4, v = 2*n;
        if (!faces.value || faces.value->size() != f || !edges.value || edges.value->size() != 3*f/2 ||
            !vertices.value || vertices.value->size() != v || !shells.value || shells.value->size() != 1 ||
            !mass.value || std::abs(mass.value->volume-volume) > 1e-8 ||
            std::abs(mass.value->area-surface_area) > 1e-8 ||
            std::hypot(mass.value->centroid.x-expected_center.x, mass.value->centroid.y-expected_center.y,
                       mass.value->centroid.z-expected_center.z) > 1e-8 ||
            kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) {
            std::cerr << "extrusion to plane topology/mass/Strict mismatch: " << profile.label << '\n';
            return false;
        }
        const auto rx = rotate({1,0,0}), ry = rotate({0,1,0}), rz = rotate({0,0,1});
        const std::array<double,9> rotation {rx.x,ry.x,rz.x, rx.y,ry.y,rz.y, rx.z,ry.z,rz.z};
        std::array<double,9> covariance {};
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b)
                covariance[3*i+j] += rotation[3*i+a]*second[3*a+b]*rotation[3*j+b];
        const double trace = covariance[0]+covariance[4]+covariance[8];
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            if (std::abs(mass.value->inertia[3*i+j]-volume*((i == j ? trace : 0)-covariance[3*i+j])) > 1e-7) {
                std::cerr << "extrusion to plane inertia mismatch\n";
                return false;
            }
        double queried_area = 0;
        for (const auto face : *faces.value) {
            const auto a = query.planar_face_area(face);
            const auto owners = query.bodies_of_face(face);
            if (!a.value || *a.value <= 0 || !owners.value || owners.value->size() != 1 ||
                owners.value->front().value != body.value->value) return false;
            queried_area += *a.value;
        }
        if (std::abs(queried_area-surface_area) > 1e-8) return false;
        for (const auto vertex : *vertices.value) {
            const auto exists = query.has_vertex(vertex);
            if (!exists.value || !*exists.value) return false;
        }
        for (const auto edge : *edges.value) {
            const auto uses = query.coedge_count_of_edge(edge);
            const auto neighbors = query.faces_of_edge(edge);
            const auto length = query.edge_length(edge);
            if (!uses.value || *uses.value != 2 || !neighbors.value || neighbors.value->size() != 2 ||
                !length.value || *length.value <= 0) return false;
        }
        for (const auto& bounds : {query.bbox_of_body_from_topology(*body.value), kernel.representation().bbox_of_body(*body.value)}) {
            if (!bounds.value || !bounds.value->is_valid ||
                std::hypot(bounds.value->min.x-expected_bbox.min.x,bounds.value->min.y-expected_bbox.min.y,
                           bounds.value->min.z-expected_bbox.min.z) > 1e-8 ||
                std::hypot(bounds.value->max.x-expected_bbox.max.x,bounds.value->max.y-expected_bbox.max.y,
                           bounds.value->max.z-expected_bbox.max.z) > 1e-8) return false;
        }
        const auto mesh = kernel.convert().brep_to_mesh(*body.value,{});
        if (!mesh.value) return false;
        const auto inspection = kernel.convert().inspect_mesh(*mesh.value);
        if (!inspection.value || inspection.value->tessellation_strategy != "owned_topo_welded" ||
            inspection.value->vertex_count != v || inspection.value->triangle_count != f ||
            inspection.value->connected_components != 1 || inspection.value->has_degenerate_triangles ||
            inspection.value->has_out_of_range_indices) return false;
        if (!slanted) {
            const auto ordinary = kernel.sweeps().extrude(profile,{direction.x,direction.y,direction.z},
                                                         4*std::hypot(d.x,d.y,d.z));
            if (!ordinary.value) return false;
            const auto other = kernel.query().mass_properties(*ordinary.value);
            if (!other.value || std::abs(other.value->volume-volume) > 1e-8 ||
                std::abs(other.value->area-surface_area) > 1e-8) return false;
        }
    }

    // Smallest profile: a triangular wedge with independent volume and vertex counts.
    axiom::Kernel kernel;
    axiom::ProfileRef triangle {"triangle",{{0,0,0},{2,0,0},{0,2,0}}};
    const auto wedge = kernel.sweeps().extrude_to_plane(triangle,{0,0,1},{{0,0,2},{-1,0,1}});
    if (!wedge.value) return false;
    const auto wedge_mass = kernel.query().mass_properties(*wedge.value);
    if (!wedge_mass.value || std::abs(wedge_mass.value->volume-16.0/3) > 1e-10 ||
        kernel.topology().query().vertex_count_of_body(*wedge.value).value != 6 ||
        kernel.topology().query().face_count_of_body(*wedge.value).value != 8 ||
        kernel.validate().validate_all(*wedge.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok)
        return false;

    const auto& profile = models[3].profile;
    const axiom::Plane plane {{0,0,4},{-0.2,0.1,1}};
    const auto source = kernel.sweeps().extrude_to_plane(profile,{0,0,1},plane);
    if (!source.value || !kernel.convert().brep_to_mesh(*source.value,{}).value) return false;
    const auto source_mass = kernel.query().mass_properties(*source.value);
    const auto source_faces = kernel.topology().query().faces_of_body(*source.value);
    if (!source_mass.value || !source_faces.value || source_faces.value->empty()) return false;
    auto transaction = kernel.topology().begin_transaction();
    const auto temporary = transaction.create_vertex({20,20,20});
    if (!temporary.value) return false;
    const auto objects = kernel.object_count_total(), geometry = kernel.geometry_count(), bodies = kernel.body_count();
    const auto next = kernel.next_object_id(), writes = transaction.write_operation_count();
    const auto runtime = kernel.runtime_store_counts();
    if (!objects.value || !geometry.value || !bodies.value || !next.value || !writes.value || !runtime.value) return false;
    const auto rejected = [&](const axiom::ProfileRef& p, const axiom::Vec3& d, const axiom::Plane& target) {
        const auto result = kernel.sweeps().extrude_to_plane(p,d,target);
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto after = kernel.runtime_store_counts();
        return result.status == axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,axiom::diag_codes::kCoreParameterOutOfRange) &&
            kernel.object_count_total().value == objects.value && kernel.geometry_count().value == geometry.value &&
            kernel.body_count().value == bodies.value && kernel.next_object_id().value == next.value &&
            transaction.write_operation_count().value == writes.value && after.value &&
            after.value->mesh_records == runtime.value->mesh_records &&
            after.value->tessellation_cache_entries == runtime.value->tessellation_cache_entries &&
            after.value->face_tessellation_cache_entries == runtime.value->face_tessellation_cache_entries &&
            after.value->curve_eval_cache_entries == runtime.value->curve_eval_cache_entries &&
            after.value->surface_eval_cache_entries == runtime.value->surface_eval_cache_entries;
    };
    const double inf = std::numeric_limits<double>::infinity(), nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto d : {axiom::Vec3 {0,0,0},{1,0,0},{1,0,1e-9},{0,0,-1},{inf,0,1},{0,nan,1}})
        if (!rejected(profile,d,plane)) return false;
    for (const auto target : {axiom::Plane {{0,0,4},{0,0,0}}, {{0,0,4},{1,0,0}},
             {{0,0,4},{1,0,1e-9}}, {{0,0,-1},{0,0,1}}, {{0,0,0},{0,0,1}},
             {{0,0,1e-12},{0,0,1}}, {{0,0,0},{-1,0,1}}, {{4,0,0},{-1,0,1}},
             {{inf,0,4},{0,0,1}}, {{0,nan,4},{0,0,1}}, {{0,0,4},{inf,0,1}},
             {{0,0,4},{0,nan,1}}, {{0,0,1e308},{0,0,1}}})
        if (!rejected(profile,{0,0,1},target)) return false;
    const std::vector<axiom::ProfileRef> invalid {
        {"empty"}, {"",profile.polygon_xyz}, {"short",{{0,0,0},{1,0,0}}},
        {"collinear",{{0,0,0},{1,0,0},{2,0,0}}},
        {"duplicate",{{0,0,0},{3,0,0},{3,3,0},{3,0,0},{0,3,0}}},
        {"crossing",{{0,0,0},{4,3,0},{0,4,0},{3,0,0}}},
        {"nonplanar",{{0,0,0},{3,0,0},{3,3,0.1},{0,3,0}}},
        {"nan",{{0,0,0},{3,0,0},{0,nan,0}}},
        {"bad_hole",profile.polygon_xyz,{{{7,1,0},{9,1,0},{9,3,0},{7,3,0}}}},
        {"touching_hole",profile.polygon_xyz,{{{0,1,0},{2,1,0},{2,3,0},{0,3,0}}}},
        {"nested_holes",profile.polygon_xyz,
            {{{1,1,0},{5,1,0},{5,5,0},{1,5,0}},{{2,2,0},{3,2,0},{3,3,0},{2,3,0}}}}
    };
    for (const auto& p : invalid) if (!rejected(p,{0,0,1},plane)) return false;
    // A positive ray distance may still collapse in world coordinates.
    auto rounded = triangle;
    for (auto& p : rounded.polygon_xyz) p.z = 1e16;
    if (!rejected(rounded,{0,0,1},{{0,0,1e16},{-0.1,0,1}})) return false;
    if (transaction.rollback().status != axiom::StatusCode::Ok) return false;
    const auto remains = kernel.topology().query().has_vertex(*temporary.value);
    if (!remains.value || *remains.value) return false;
    auto edit = kernel.topology().begin_transaction();
    if (edit.delete_face(source_faces.value->front()).status != axiom::StatusCode::Ok ||
        edit.rollback().status != axiom::StatusCode::Ok) return false;
    const auto restored = kernel.query().mass_properties(*source.value);
    const auto restored_faces = kernel.topology().query().faces_of_body(*source.value);
    if (!restored.value || std::abs(restored.value->volume-source_mass.value->volume) > 1e-8 ||
        std::abs(restored.value->area-source_mass.value->area) > 1e-8 ||
        restored.value->inertia != source_mass.value->inertia ||
        !restored_faces.value || restored_faces.value->size() != source_faces.value->size() ||
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) return false;
    const auto retry = kernel.sweeps().extrude_to_plane(profile,{0.4,-0.2,-1},{{0,0,-4},{-0.2,0.1,-1}});
    if (!retry.value || kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok)
        return false;
    return true;
}

bool test_holed_polygon_revolutions() {
    constexpr double pi = 3.1415926535897932384626433832795;
    struct Model {
        axiom::ProfileRef profile;
    };
    const std::vector<Model> models {
        {{"revolve_one_hole",
          {{2,0,-4},{8,0,-4},{8,0,4},{2,0,4}},
          {{{3,0,-2},{5,0,-2},{5,0,1},{3,0,1}}}}},
        {{"revolve_two_holes",
          {{1,0,-5},{9,0,-5},{9,0,5},{1,0,5}},
          {{{2,0,-4},{4,0,-4},{4,0,-1},{2,0,-1}},
           {{5,0,1},{8,0,1},{8,0,4},{5,0,4}}}}},
        {{"revolve_concave_rings",
          {{1,0,-5},{9,0,-5},{9,0,5},{6,0,5},{6,0,2},{4,0,2},{4,0,5},{1,0,5}},
          {{{2,0,-3},{7,0,-3},{7,0,0},{5,0,0},{5,0,-1},{2,0,-1}}}}},
    };
    const std::array<axiom::Vec3,2> axes {{{0,0,1},{2.0/3.0,-1.0/3.0,2.0/3.0}}};
    const std::array<axiom::Vec3,2> radial {{{1,0,0},{1.0/std::sqrt(5.0),2.0/std::sqrt(5.0),0}}};
    const std::array<double,2> angles {{pi/3.0,2.0*pi}};
    const axiom::Point3 origin {-17,11,6};
    const auto ring_area_moment = [](const std::vector<axiom::Point3>& ring) {
        double twice_area = 0.0, radial_numerator = 0.0;
        for (std::size_t i=0;i<ring.size();++i) {
            const auto& a=ring[i];
            const auto& b=ring[(i+1)%ring.size()];
            const double cross=a.x*b.z-b.x*a.z;
            twice_area+=cross;
            radial_numerator+=(a.x+b.x)*cross;
        }
        const double area=std::abs(twice_area)/2.0;
        return std::array<double,2> {area,area*std::abs(radial_numerator/(3.0*twice_area))};
    };
    for (const auto& model : models) {
        const auto outer_mass=ring_area_moment(model.profile.polygon_xyz);
        double profile_area=outer_mass[0], radial_moment=outer_mass[1];
        for (const auto& hole : model.profile.holes_xyz) {
            const auto hole_mass=ring_area_moment(hole);
            profile_area-=hole_mass[0];
            radial_moment-=hole_mass[1];
        }
        double boundary_surface_full=0.0;
        for (std::size_t r=0;r<=model.profile.holes_xyz.size();++r) {
            const auto& ring=r==0?model.profile.polygon_xyz:model.profile.holes_xyz[r-1];
            for (std::size_t i=0;i<ring.size();++i) {
                const auto& a=ring[i];
                const auto& b=ring[(i+1)%ring.size()];
                boundary_surface_full+=pi*(a.x+b.x)*std::hypot(b.x-a.x,b.z-a.z);
            }
        }
        for (std::size_t frame=0;frame<axes.size();++frame) {
            for (int variant=0;variant<4;++variant) {
                for (std::size_t angle_index=0;angle_index<angles.size();++angle_index) {
                    axiom::Kernel kernel;
                    auto profile=model.profile;
                    profile.label+="_"+std::to_string(frame)+"_"+std::to_string(variant)+"_"+
                                   std::to_string(angle_index);
                    std::size_t point_count=0;
                    for (std::size_t r=0;r<=profile.holes_xyz.size();++r) {
                        auto& ring=r==0?profile.polygon_xyz:profile.holes_xyz[r-1];
                        point_count+=ring.size();
                        if ((variant+r)&1) std::reverse(ring.begin(),ring.end());
                        std::rotate(ring.begin(),ring.begin()+(variant+r)%ring.size(),ring.end());
                        for (auto& p:ring) {
                            p={origin.x+radial[frame].x*p.x+axes[frame].x*p.z,
                               origin.y+radial[frame].y*p.x+axes[frame].y*p.z,
                               origin.z+radial[frame].z*p.x+axes[frame].z*p.z};
                        }
                    }
                    if (variant&2) std::reverse(profile.holes_xyz.begin(),profile.holes_xyz.end());
                    const double direction_sign=variant&1?-7.0:5.0;
                    const axiom::Axis3 axis {origin,{direction_sign*axes[frame].x,
                                                     direction_sign*axes[frame].y,
                                                     direction_sign*axes[frame].z}};
                    const double angle=angles[angle_index];
                    const auto body=kernel.sweeps().revolve(profile,axis,angle);
                    if (!body.value || body.status!=axiom::StatusCode::Ok) {
                        std::cerr << "holed polygon revolution failed: " << profile.label << '\n';
                        return false;
                    }
                    const bool full=angle_index==1;
                    const int segments=full?48:std::max(1,static_cast<int>(std::ceil(angle/(2*pi/48))));
                    const std::size_t cap_triangles=point_count+2*profile.holes_xyz.size()-2;
                    const std::size_t expected_vertices=point_count*static_cast<std::size_t>(full?segments:segments+1);
                    const std::size_t expected_faces=2*point_count*static_cast<std::size_t>(segments)+
                                                     (full?0:2*cap_triangles);
                    const std::size_t expected_edges=3*expected_faces/2;
                    const std::size_t expected_shells=full?profile.holes_xyz.size()+1:1;
                    const double exact_volume=radial_moment*angle;
                    const double exact_area=boundary_surface_full*angle/(2*pi)+(full?0:2*profile_area);
                    const auto query=kernel.topology().query();
                    const auto shells=query.shells_of_body(*body.value);
                    const auto faces=query.faces_of_body(*body.value);
                    const auto edges=query.edges_of_body(*body.value);
                    const auto vertices=query.vertices_of_body(*body.value);
                    const auto mass=kernel.query().mass_properties(*body.value);
                    if (!shells.value || shells.value->size()!=expected_shells || !faces.value ||
                        faces.value->size()!=expected_faces || !edges.value || edges.value->size()!=expected_edges ||
                        !vertices.value || vertices.value->size()!=expected_vertices || !mass.value ||
                        std::abs(mass.value->volume-exact_volume)>exact_volume*0.006 ||
                        std::abs(mass.value->area-exact_area)>exact_area*0.006 ||
                        mass.value->inertia[0]<=0 || mass.value->inertia[4]<=0 || mass.value->inertia[8]<=0 ||
                        kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status!=
                            axiom::StatusCode::Ok ||
                        kernel.topology().validate().validate_indices_consistency().status!=axiom::StatusCode::Ok ||
                        kernel.topology().validate().validate_body_topology_indices(*body.value).status!=
                            axiom::StatusCode::Ok) {
                        std::cerr << "holed revolution topology/mass/Strict mismatch: " << profile.label << '\n';
                        return false;
                    }
                    double queried_area=0.0;
                    for (const auto face:*faces.value) {
                        const auto area=query.planar_face_area(face);
                        const auto owners=query.bodies_of_face(face);
                        if (!area.value || *area.value<=0 || !owners.value || owners.value->size()!=1 ||
                            owners.value->front().value!=body.value->value) return false;
                        queried_area+=*area.value;
                    }
                    if (std::abs(queried_area-mass.value->area)>1e-8*mass.value->area) return false;
                    for (const auto edge:*edges.value) {
                        const auto uses=query.coedge_count_of_edge(edge);
                        const auto neighbors=query.faces_of_edge(edge);
                        const auto length=query.edge_length(edge);
                        if (!uses.value || *uses.value!=2 || !neighbors.value || neighbors.value->size()!=2 ||
                            !length.value || *length.value<=0) return false;
                    }
                    const auto topology_bbox=query.bbox_of_body_from_topology(*body.value);
                    const auto body_bbox=kernel.representation().bbox_of_body(*body.value);
                    if (!topology_bbox.value || !body_bbox.value || !topology_bbox.value->is_valid ||
                        !body_bbox.value->is_valid ||
                        std::hypot(topology_bbox.value->min.x-body_bbox.value->min.x,
                                   topology_bbox.value->min.y-body_bbox.value->min.y,
                                   topology_bbox.value->min.z-body_bbox.value->min.z)>1e-10 ||
                        std::hypot(topology_bbox.value->max.x-body_bbox.value->max.x,
                                   topology_bbox.value->max.y-body_bbox.value->max.y,
                                   topology_bbox.value->max.z-body_bbox.value->max.z)>1e-10) return false;
                    const auto mesh=kernel.convert().brep_to_mesh(*body.value,{});
                    const auto inspection=mesh.value?kernel.convert().inspect_mesh(*mesh.value):
                                                       axiom::Result<axiom::MeshInspectionReport>{};
                    if (!inspection.value || inspection.value->tessellation_strategy!="owned_topo_welded" ||
                        inspection.value->vertex_count!=expected_vertices ||
                        inspection.value->triangle_count!=expected_faces ||
                        inspection.value->connected_components!=expected_shells ||
                        inspection.value->has_degenerate_triangles || inspection.value->has_out_of_range_indices)
                        return false;
                }
            }
        }
    }

    axiom::Kernel kernel;
    axiom::ProfileRef source {"holed_revolution_source",{{2,0,-4},{8,0,-4},{8,0,4},{2,0,4}},
                              {{{3,0,-2},{5,0,-2},{5,0,2},{3,0,2}}}};
    const axiom::Axis3 z_axis {{0,0,0},{0,0,1}};
    const auto source_body=kernel.sweeps().revolve(source,z_axis,pi);
    const auto source_mass=source_body.value?kernel.query().mass_properties(*source_body.value):
                                              axiom::Result<axiom::MassProperties>{};
    const auto source_faces=source_body.value?kernel.topology().query().faces_of_body(*source_body.value):
                                                axiom::Result<std::vector<axiom::FaceId>>{};
    if (!source_body.value || !source_mass.value || !source_faces.value || source_faces.value->empty()) return false;
    auto transaction=kernel.topology().begin_transaction();
    const auto temporary=transaction.create_vertex({31,31,31});
    const auto objects=kernel.object_count_total(),geometry=kernel.geometry_count();
    const auto bodies=kernel.body_count(),next=kernel.next_object_id();
    const auto writes=transaction.write_operation_count();
    const auto runtime=kernel.runtime_store_counts();
    if (!temporary.value || !objects.value || !geometry.value || !bodies.value || !next.value ||
        !writes.value || !runtime.value) return false;
    const auto rejected=[&](const axiom::ProfileRef& profile) {
        const auto result=kernel.sweeps().revolve(profile,z_axis,pi);
        const auto diagnostic=kernel.diagnostics().get(result.diagnostic_id);
        const auto after=kernel.runtime_store_counts();
        return result.status==axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,axiom::diag_codes::kCoreParameterOutOfRange) &&
            kernel.object_count_total().value==objects.value && kernel.geometry_count().value==geometry.value &&
            kernel.body_count().value==bodies.value && kernel.next_object_id().value==next.value &&
            transaction.write_operation_count().value==writes.value && after.value &&
            after.value->mesh_records==runtime.value->mesh_records &&
            after.value->tessellation_cache_entries==runtime.value->tessellation_cache_entries &&
            after.value->face_tessellation_cache_entries==runtime.value->face_tessellation_cache_entries;
    };
    const std::vector<axiom::ProfileRef> invalid {
        {"holes_without_outer",{},source.holes_xyz},
        {"outside_hole",source.polygon_xyz,{{{9,0,-1},{10,0,-1},{10,0,1},{9,0,1}}}},
        {"touching_hole",source.polygon_xyz,{{{2,0,-1},{4,0,-1},{4,0,1},{2,0,1}}}},
        {"crossing_hole",source.polygon_xyz,{{{1,0,-1},{4,0,-1},{4,0,1},{1,0,1}}}},
        {"nested_holes",source.polygon_xyz,{{{3,0,-2},{6,0,-2},{6,0,2},{3,0,2}},
                                             {{4,0,-1},{5,0,-1},{5,0,1},{4,0,1}}}},
        {"overlap_holes",source.polygon_xyz,{{{3,0,-2},{6,0,-2},{6,0,1},{3,0,1}},
                                              {{5,0,-1},{7,0,-1},{7,0,2},{5,0,2}}}},
        {"nonplanar_hole",source.polygon_xyz,{{{3,0,-1},{5,0,-1},{5,0,1},{3,0.1,1}}}},
        {"axis_touch_with_hole",{{0,0,-4},{8,0,-4},{8,0,4},{0,0,4}},source.holes_xyz},
    };
    for (const auto& profile:invalid) if (!rejected(profile)) return false;
    if (transaction.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto remains=kernel.topology().query().has_vertex(*temporary.value);
    if (!remains.value || *remains.value) return false;
    auto edit=kernel.topology().begin_transaction();
    if (edit.delete_face(source_faces.value->front()).status!=axiom::StatusCode::Ok ||
        edit.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto restored=kernel.query().mass_properties(*source_body.value);
    const auto restored_faces=kernel.topology().query().faces_of_body(*source_body.value);
    if (!restored.value || std::abs(restored.value->volume-source_mass.value->volume)>1e-10 ||
        std::abs(restored.value->area-source_mass.value->area)>1e-10 ||
        restored.value->inertia!=source_mass.value->inertia || !restored_faces.value ||
        restored_faces.value->size()!=source_faces.value->size() ||
        kernel.validate().validate_all(*source_body.value,axiom::ValidationMode::Strict).status!=
            axiom::StatusCode::Ok) return false;
    std::reverse(source.polygon_xyz.begin(),source.polygon_xyz.end());
    std::reverse(source.holes_xyz.front().begin(),source.holes_xyz.front().end());
    const auto retry=kernel.sweeps().revolve(source,{{0,0,0},{0,0,-4}},2*pi);
    return retry.value &&
        kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok;
}

bool test_partial_polygon_revolutions() {
    constexpr double pi = 3.1415926535897932384626433832795;
    struct Model {
        std::string label;
        std::vector<axiom::Point3> meridian;
        std::size_t axis_vertices;
    };
    const std::vector<Model> models {
        {"partial_annular_rectangle", {{1,0,-1},{3,0,-1},{3,0,2},{1,0,2}}, 0},
        {"partial_annular_concave", {{1,0,-2},{4,0,-2},{4,0,-1},{2,0,-1},{2,0,2},{1,0,2}}, 0},
        {"partial_solid_triangle", {{0,0,-1},{3,0,-1},{0,0,2}}, 2},
        {"partial_solid_step", {{0,0,-2},{3,0,-2},{3,0,-1},{2,0,-1},{2,0,2},{0,0,2}}, 2},
    };
    const std::array<axiom::Vec3, 2> axes {{
        {0,0,1}, {2.0/3.0,-1.0/3.0,2.0/3.0}
    }};
    const std::array<axiom::Vec3, 2> radial {{
        {1,0,0}, {1.0/std::sqrt(5.0),2.0/std::sqrt(5.0),0}
    }};
    const std::array<double, 3> angles {{pi/6.0, 2.0*pi/3.0, 3.0*pi/2.0}};
    const axiom::Point3 origin {-11,7,4};
    for (const auto& model : models) {
        double signed_twice_area = 0.0, radial_moment_numerator = 0.0;
        for (std::size_t i = 0; i < model.meridian.size(); ++i) {
            const auto& a = model.meridian[i];
            const auto& b = model.meridian[(i+1)%model.meridian.size()];
            const double cross = a.x*b.z-b.x*a.z;
            signed_twice_area += cross;
            radial_moment_numerator += (a.x+b.x)*cross;
        }
        const double profile_area = std::abs(signed_twice_area)/2.0;
        const double centroid_radius = std::abs(radial_moment_numerator/(3.0*signed_twice_area));
        for (std::size_t frame = 0; frame < axes.size(); ++frame) {
            for (std::size_t angle_index = 0; angle_index < angles.size(); ++angle_index) {
                for (int variant = 0; variant < 2; ++variant) {
                    axiom::Kernel kernel;
                    axiom::ProfileRef profile;
                    profile.label = model.label+"_"+std::to_string(frame)+"_"+
                                    std::to_string(angle_index)+"_"+std::to_string(variant);
                    profile.polygon_xyz = model.meridian;
                    if (variant != 0) {
                        std::reverse(profile.polygon_xyz.begin(),profile.polygon_xyz.end());
                        std::rotate(profile.polygon_xyz.begin(),profile.polygon_xyz.begin()+1,
                                    profile.polygon_xyz.end());
                    }
                    for (auto& p : profile.polygon_xyz) {
                        p = {origin.x+radial[frame].x*p.x+axes[frame].x*p.z,
                             origin.y+radial[frame].y*p.x+axes[frame].y*p.z,
                             origin.z+radial[frame].z*p.x+axes[frame].z*p.z};
                    }
                    const double direction_sign = variant == 0 ? 4.0 : -3.0;
                    const axiom::Axis3 axis {origin,{direction_sign*axes[frame].x,
                                                     direction_sign*axes[frame].y,
                                                     direction_sign*axes[frame].z}};
                    const double angle = angles[angle_index];
                    const auto body = kernel.sweeps().revolve(profile,axis,angle);
                    if (!body.value || body.status!=axiom::StatusCode::Ok) {
                        std::cerr << "partial polygon revolution failed: " << profile.label << '\n';
                        return false;
                    }
                    const auto query = kernel.topology().query();
                    const auto shells = query.shells_of_body(*body.value);
                    const auto faces = query.faces_of_body(*body.value);
                    const auto edges = query.edges_of_body(*body.value);
                    const auto vertices = query.vertices_of_body(*body.value);
                    const auto mass = kernel.query().mass_properties(*body.value);
                    const int segments = std::max(1,static_cast<int>(std::ceil(angle/(2.0*pi/48.0))));
                    const std::size_t off_axis = model.meridian.size()-model.axis_vertices;
                    const std::size_t expected_vertices = off_axis*static_cast<std::size_t>(segments+1)+
                                                          model.axis_vertices;
                    const std::size_t active_edges = model.meridian.size()-(model.axis_vertices==0?0:1);
                    const std::size_t mixed_edges = model.axis_vertices==0?0:2;
                    const std::size_t expected_faces = static_cast<std::size_t>(segments)*
                        (2*active_edges-mixed_edges)+2*(model.meridian.size()-2);
                    const std::size_t expected_edges = 3*expected_faces/2;
                    const double exact_volume = profile_area*centroid_radius*angle;
                    if (!shells.value || shells.value->size()!=1 || !faces.value ||
                        faces.value->size()!=expected_faces || !edges.value || edges.value->size()!=expected_edges ||
                        !vertices.value || vertices.value->size()!=expected_vertices || !mass.value ||
                        std::abs(mass.value->volume-exact_volume)>exact_volume*0.004 ||
                        mass.value->area<=0 || mass.value->inertia[0]<=0 || mass.value->inertia[4]<=0 ||
                        mass.value->inertia[8]<=0 ||
                        std::abs(mass.value->inertia[1]-mass.value->inertia[3])>1e-8 ||
                        std::abs(mass.value->inertia[2]-mass.value->inertia[6])>1e-8 ||
                        std::abs(mass.value->inertia[5]-mass.value->inertia[7])>1e-8 ||
                        kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status!=
                            axiom::StatusCode::Ok ||
                        kernel.topology().validate().validate_indices_consistency().status!=axiom::StatusCode::Ok ||
                        kernel.topology().validate().validate_body_topology_indices(*body.value).status!=
                            axiom::StatusCode::Ok) {
                        std::cerr << "partial revolution topology/mass/Strict mismatch: " << profile.label << '\n';
                        return false;
                    }
                    double queried_area = 0.0;
                    for (const auto face : *faces.value) {
                        const auto area = query.planar_face_area(face);
                        const auto owners = query.bodies_of_face(face);
                        if (!area.value || *area.value<=0 || !owners.value || owners.value->size()!=1 ||
                            owners.value->front().value!=body.value->value) return false;
                        queried_area += *area.value;
                    }
                    if (std::abs(queried_area-mass.value->area)>1e-8*mass.value->area) return false;
                    for (const auto edge : *edges.value) {
                        const auto uses = query.coedge_count_of_edge(edge);
                        const auto neighbors = query.faces_of_edge(edge);
                        const auto length = query.edge_length(edge);
                        if (!uses.value || *uses.value!=2 || !neighbors.value || neighbors.value->size()!=2 ||
                            !length.value || *length.value<=0) return false;
                    }
                    const auto topology_bbox = query.bbox_of_body_from_topology(*body.value);
                    const auto body_bbox = kernel.representation().bbox_of_body(*body.value);
                    if (!topology_bbox.value || !body_bbox.value || !topology_bbox.value->is_valid ||
                        !body_bbox.value->is_valid ||
                        std::hypot(topology_bbox.value->min.x-body_bbox.value->min.x,
                                   topology_bbox.value->min.y-body_bbox.value->min.y,
                                   topology_bbox.value->min.z-body_bbox.value->min.z)>1e-10 ||
                        std::hypot(topology_bbox.value->max.x-body_bbox.value->max.x,
                                   topology_bbox.value->max.y-body_bbox.value->max.y,
                                   topology_bbox.value->max.z-body_bbox.value->max.z)>1e-10) return false;
                    const auto mesh = kernel.convert().brep_to_mesh(*body.value,{});
                    const auto inspection = mesh.value ? kernel.convert().inspect_mesh(*mesh.value) :
                                                        axiom::Result<axiom::MeshInspectionReport>{};
                    if (!inspection.value || inspection.value->tessellation_strategy!="owned_topo_welded" ||
                        inspection.value->vertex_count!=expected_vertices ||
                        inspection.value->triangle_count!=expected_faces ||
                        inspection.value->connected_components!=1 || inspection.value->has_degenerate_triangles ||
                        inspection.value->has_out_of_range_indices) return false;
                }
            }
        }
    }

    axiom::Kernel kernel;
    axiom::ProfileRef source_profile {"partial_revolution_source",{{0,0,-2},{3,0,-2},{3,0,2},{0,0,2}}};
    const axiom::Axis3 z_axis {{0,0,0},{0,0,1}};
    const auto source = kernel.sweeps().revolve(source_profile,z_axis,5.0*pi/4.0);
    const auto source_mass = source.value ? kernel.query().mass_properties(*source.value) :
                                            axiom::Result<axiom::MassProperties>{};
    const auto source_faces = source.value ? kernel.topology().query().faces_of_body(*source.value) :
                                             axiom::Result<std::vector<axiom::FaceId>>{};
    if (!source.value || !source_mass.value || !source_faces.value || source_faces.value->empty()) return false;
    auto transaction = kernel.topology().begin_transaction();
    const auto temporary = transaction.create_vertex({20,20,20});
    const auto objects = kernel.object_count_total(), geometry = kernel.geometry_count();
    const auto bodies = kernel.body_count(), next = kernel.next_object_id();
    const auto writes = transaction.write_operation_count();
    const auto runtime = kernel.runtime_store_counts();
    if (!temporary.value || !objects.value || !geometry.value || !bodies.value || !next.value ||
        !writes.value || !runtime.value) return false;
    const auto rejected = [&](const axiom::ProfileRef& profile, const axiom::Axis3& axis, double angle) {
        const auto result = kernel.sweeps().revolve(profile,axis,angle);
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto after = kernel.runtime_store_counts();
        return result.status==axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,axiom::diag_codes::kCoreParameterOutOfRange) &&
            kernel.object_count_total().value==objects.value && kernel.geometry_count().value==geometry.value &&
            kernel.body_count().value==bodies.value && kernel.next_object_id().value==next.value &&
            transaction.write_operation_count().value==writes.value && after.value &&
            after.value->mesh_records==runtime.value->mesh_records &&
            after.value->tessellation_cache_entries==runtime.value->tessellation_cache_entries &&
            after.value->face_tessellation_cache_entries==runtime.value->face_tessellation_cache_entries;
    };
    if (!rejected({"cross_axis",{{-1,0,-1},{1,0,-1},{1,0,1},{-1,0,1}}},z_axis,pi) ||
        !rejected({"isolated_axis",{{0,0,0},{2,0,-1},{2,0,1}}},z_axis,pi) ||
        !rejected({"near_axis",{{1e-9,0,-1},{2,0,-1},{2,0,1},{1e-9,0,1}}},z_axis,pi) ||
        !rejected({"self_crossing",{{1,0,-1},{3,0,1},{1,0,1},{3,0,-1}}},z_axis,pi) ||
        !rejected({"nonplanar",{{1,0,-1},{3,0,-1},{3,0,1},{1,0.1,1}}},z_axis,pi) ||
        !rejected(source_profile,{{0,1,0},{0,0,1}},pi) ||
        !rejected({"holed",source_profile.polygon_xyz,{{{1,0,-1},{2,0,-1},{2,0,1},{1,0,1}}}},z_axis,pi))
        return false;
    if (transaction.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto remains = kernel.topology().query().has_vertex(*temporary.value);
    if (!remains.value || *remains.value) return false;
    auto edit = kernel.topology().begin_transaction();
    if (edit.delete_face(source_faces.value->front()).status!=axiom::StatusCode::Ok ||
        edit.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto restored = kernel.query().mass_properties(*source.value);
    const auto restored_faces = kernel.topology().query().faces_of_body(*source.value);
    if (!restored.value || std::abs(restored.value->volume-source_mass.value->volume)>1e-10 ||
        std::abs(restored.value->area-source_mass.value->area)>1e-10 ||
        restored.value->inertia!=source_mass.value->inertia || !restored_faces.value ||
        restored_faces.value->size()!=source_faces.value->size() ||
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok)
        return false;
    std::reverse(source_profile.polygon_xyz.begin(),source_profile.polygon_xyz.end());
    const auto retry = kernel.sweeps().revolve(source_profile,{{0,0,0},{0,0,-9}},pi/3.0);
    return retry.value &&
        kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok;
}

bool test_directed_interval_revolutions() {
    constexpr double pi = 3.1415926535897932384626433832795;
    constexpr double two_pi = 2.0*pi;
    const axiom::Axis3 axis {{0,0,0},{0,0,7}};
    const axiom::ProfileRef profile {
        "directed_interval",
        {{2,0,-2},{5,0,-2},{5,0,2},{2,0,2}},
        {{{3,0,-1},{4,0,-1},{4,0,1},{3,0,1}}}
    };
    struct Interval {
        double start;
        double end;
        int quadrant;
    };
    const std::array<Interval,4> intervals {{
        {0.0,pi/2.0,0},
        {0.0,-pi/2.0,1},
        {-pi/4.0,pi/4.0,2},
        {pi/2.0,pi,3},
    }};
    // Outer area/radial first moment minus the rectangular hole.
    const double profile_area = 12.0-2.0;
    const double radial_moment = 12.0*3.5-2.0*3.5;
    const std::size_t point_count = 8;
    const std::size_t cap_triangles = point_count; // n + 2*h - 2, h=1.
    for (const auto& interval : intervals) {
        for (int variant=0;variant<4;++variant) {
            axiom::Kernel kernel;
            auto input=profile;
            input.label += "_"+std::to_string(interval.quadrant)+"_"+std::to_string(variant);
            if (variant&1) {
                std::reverse(input.polygon_xyz.begin(),input.polygon_xyz.end());
                std::reverse(input.holes_xyz.front().begin(),input.holes_xyz.front().end());
            }
            if (variant&2) {
                std::rotate(input.polygon_xyz.begin(),input.polygon_xyz.begin()+1,input.polygon_xyz.end());
                std::rotate(input.holes_xyz.front().begin(),input.holes_xyz.front().begin()+2,
                            input.holes_xyz.front().end());
            }
            const auto body=kernel.sweeps().revolve_between(input,axis,interval.start,interval.end);
            if (!body.value || body.status!=axiom::StatusCode::Ok) {
                std::cerr<<"directed interval revolution failed\n";
                return false;
            }
            const double span=std::abs(interval.end-interval.start);
            const int segments=std::max(1,static_cast<int>(std::ceil(span/(two_pi/48.0))));
            const std::size_t expected_vertices=point_count*static_cast<std::size_t>(segments+1);
            const std::size_t expected_faces=2*point_count*static_cast<std::size_t>(segments)+2*cap_triangles;
            const std::size_t expected_edges=3*expected_faces/2;
            const auto query=kernel.topology().query();
            const auto shells=query.shells_of_body(*body.value);
            const auto faces=query.faces_of_body(*body.value);
            const auto edges=query.edges_of_body(*body.value);
            const auto vertices=query.vertices_of_body(*body.value);
            const auto mass=kernel.query().mass_properties(*body.value);
            const auto bbox=kernel.representation().bbox_of_body(*body.value);
            if (!shells.value || shells.value->size()!=1 || !faces.value ||
                faces.value->size()!=expected_faces || !edges.value || edges.value->size()!=expected_edges ||
                !vertices.value || vertices.value->size()!=expected_vertices || !mass.value || !bbox.value ||
                !bbox.value->is_valid || std::abs(mass.value->volume-radial_moment*span)>
                    radial_moment*span*0.006 || mass.value->area<=2.0*profile_area ||
                mass.value->inertia[0]<=0 || mass.value->inertia[4]<=0 || mass.value->inertia[8]<=0 ||
                kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status!=
                    axiom::StatusCode::Ok ||
                kernel.topology().validate().validate_indices_consistency().status!=axiom::StatusCode::Ok ||
                kernel.topology().validate().validate_body_topology_indices(*body.value).status!=
                    axiom::StatusCode::Ok) {
                std::cerr<<"directed interval topology/mass validation failed: quadrant=" << interval.quadrant
                    << " variant=" << variant << " mass_status=" << static_cast<int>(mass.status) << "\n";
                const auto report = kernel.diagnostics().get(mass.diagnostic_id);
                if (report.value) for (const auto& issue : report.value->issues) std::cerr << issue.code << " " << issue.message << "\n";
                if (mass.value) std::cerr << "volume=" << mass.value->volume << " area=" << mass.value->area << "\n";
                return false;
            }
            // Independent polygonal references retain the angular sampling
            // error, and require both query facades to consume the real shell.
            const double sampled_volume=radial_moment*segments*std::sin(span/segments);
            const double sampled_section=7.0*segments*std::sin(span/segments);
            const auto topology_mass=query.body_mass_properties(*body.value);
            const auto section=kernel.query().section_detailed(*body.value,{{0,0,0},{0,0,1}});
            const auto nearest=kernel.query().closest_point(*body.value,{0,0,0});
            if (!topology_mass.value || std::abs(mass.value->volume-sampled_volume)>1e-9*sampled_volume ||
                std::abs(topology_mass.value->volume-sampled_volume)>1e-9*sampled_volume ||
                !section.value || std::abs(section.value->area-sampled_section)>1e-9*sampled_section ||
                !nearest.value || !nearest.value->nearest_boundary ||
                nearest.value->location!=axiom::BodyPointLocation::Outside ||
                std::abs(nearest.value->nearest_boundary->distance-2.0*std::cos(span/(2*segments)))>1e-9) {
                std::cerr<<"directed interval actual query mismatch: quadrant=" << interval.quadrant
                    << " variant=" << variant << "\n";
                return false;
            }
            constexpr double tol=1e-9;
            const bool placement_ok = interval.quadrant==0 ?
                bbox.value->min.x>=-tol && bbox.value->min.y>=-tol &&
                    mass.value->centroid.x>0 && mass.value->centroid.y>0 :
                interval.quadrant==1 ?
                bbox.value->min.x>=-tol && bbox.value->max.y<=tol &&
                    mass.value->centroid.x>0 && mass.value->centroid.y<0 :
                interval.quadrant==2 ?
                bbox.value->min.x>0 && bbox.value->min.y<0 && bbox.value->max.y>0 &&
                    mass.value->centroid.x>0 && std::abs(mass.value->centroid.y)<tol :
                bbox.value->max.x<=tol && bbox.value->min.y>=-tol &&
                    mass.value->centroid.x<0 && mass.value->centroid.y>0;
            if (!placement_ok) {
                std::cerr<<"directed interval angular placement mismatch\n";
                return false;
            }
            double queried_area=0.0;
            for (const auto face:*faces.value) {
                const auto area=query.planar_face_area(face);
                const auto owners=query.bodies_of_face(face);
                if (!area.value || *area.value<=0 || !owners.value || owners.value->size()!=1 ||
                    owners.value->front().value!=body.value->value) return false;
                queried_area+=*area.value;
            }
            if (std::abs(queried_area-mass.value->area)>1e-8*mass.value->area) return false;
            for (const auto edge:*edges.value) {
                const auto uses=query.coedge_count_of_edge(edge);
                const auto neighbors=query.faces_of_edge(edge);
                const auto length=query.edge_length(edge);
                if (!uses.value || *uses.value!=2 || !neighbors.value || neighbors.value->size()!=2 ||
                    !length.value || *length.value<=0) return false;
            }
            const auto topology_bbox=query.bbox_of_body_from_topology(*body.value);
            if (!topology_bbox.value || !topology_bbox.value->is_valid ||
                std::hypot(topology_bbox.value->min.x-bbox.value->min.x,
                           topology_bbox.value->min.y-bbox.value->min.y,
                           topology_bbox.value->min.z-bbox.value->min.z)>1e-10 ||
                std::hypot(topology_bbox.value->max.x-bbox.value->max.x,
                           topology_bbox.value->max.y-bbox.value->max.y,
                           topology_bbox.value->max.z-bbox.value->max.z)>1e-10) return false;
            const auto mesh=kernel.convert().brep_to_mesh(*body.value,{});
            const auto inspection=mesh.value?kernel.convert().inspect_mesh(*mesh.value):
                                               axiom::Result<axiom::MeshInspectionReport>{};
            if (!inspection.value || inspection.value->tessellation_strategy!="owned_topo_welded" ||
                inspection.value->vertex_count!=expected_vertices ||
                inspection.value->triangle_count!=expected_faces || inspection.value->connected_components!=1 ||
                inspection.value->has_degenerate_triangles || inspection.value->has_out_of_range_indices)
                return false;
        }
    }

    // Any start angle and either direction describe the same full-turn solid;
    // the periodic path must keep the two boundary components as distinct shells.
    axiom::Kernel periodic_kernel;
    const auto positive_full=periodic_kernel.sweeps().revolve_between(profile,axis,pi/3.0,pi/3.0+two_pi);
    const auto negative_full=periodic_kernel.sweeps().revolve_between(profile,axis,pi/3.0,pi/3.0-two_pi);
    for (const auto body:{positive_full,negative_full}) {
        if (!body.value) return false;
        const auto query=periodic_kernel.topology().query();
        const auto shells=query.shells_of_body(*body.value);
        const auto faces=query.faces_of_body(*body.value);
        const auto edges=query.edges_of_body(*body.value);
        const auto vertices=query.vertices_of_body(*body.value);
        const auto mass=periodic_kernel.query().mass_properties(*body.value);
        const auto bbox=periodic_kernel.representation().bbox_of_body(*body.value);
        if (!shells.value || shells.value->size()!=2 || !faces.value || faces.value->size()!=2*point_count*48 ||
            !edges.value || edges.value->size()!=3*faces.value->size()/2 || !vertices.value ||
            vertices.value->size()!=point_count*48 || !mass.value ||
            std::abs(mass.value->volume-radial_moment*two_pi)>radial_moment*two_pi*0.006 ||
            !bbox.value || bbox.value->min.x>=-4.9 || bbox.value->max.x<=4.9 ||
            bbox.value->min.y>=-4.9 || bbox.value->max.y<=4.9 ||
            periodic_kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status!=
                axiom::StatusCode::Ok) return false;
    }

    // Invalid intervals and geometry are rejected before body/topology/ID/cache or
    // active-transaction state changes. Rollback and a subsequent valid retry remain usable.
    axiom::Kernel kernel;
    const auto source=kernel.sweeps().revolve_between(profile,axis,-pi/3.0,pi/3.0);
    const auto source_mass=source.value?kernel.query().mass_properties(*source.value):
                                         axiom::Result<axiom::MassProperties>{};
    const auto source_faces=source.value?kernel.topology().query().faces_of_body(*source.value):
                                           axiom::Result<std::vector<axiom::FaceId>>{};
    if (!source.value || !source_mass.value || !source_faces.value || source_faces.value->empty()) return false;
    auto transaction=kernel.topology().begin_transaction();
    const auto temporary=transaction.create_vertex({37,37,37});
    const auto objects=kernel.object_count_total(),geometry=kernel.geometry_count();
    const auto bodies=kernel.body_count(),next=kernel.next_object_id();
    const auto writes=transaction.write_operation_count();
    const auto runtime=kernel.runtime_store_counts();
    if (!temporary.value || !objects.value || !geometry.value || !bodies.value || !next.value || !writes.value ||
        !runtime.value) return false;
    const auto rejected=[&](const axiom::ProfileRef& p,const axiom::Axis3& a,double start,double end) {
        const auto result=kernel.sweeps().revolve_between(p,a,start,end);
        const auto diagnostic=kernel.diagnostics().get(result.diagnostic_id);
        const auto after=kernel.runtime_store_counts();
        return result.status==axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,axiom::diag_codes::kCoreParameterOutOfRange) &&
            kernel.object_count_total().value==objects.value && kernel.geometry_count().value==geometry.value &&
            kernel.body_count().value==bodies.value && kernel.next_object_id().value==next.value &&
            transaction.write_operation_count().value==writes.value && after.value &&
            after.value->mesh_records==runtime.value->mesh_records &&
            after.value->tessellation_cache_entries==runtime.value->tessellation_cache_entries &&
            after.value->face_tessellation_cache_entries==runtime.value->face_tessellation_cache_entries;
    };
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double inf=std::numeric_limits<double>::infinity();
    auto nonplanar=profile;
    nonplanar.polygon_xyz.back().y=0.1;
    auto crossing=profile;
    crossing.polygon_xyz={{-1,0,-1},{2,0,-1},{2,0,1},{-1,0,1}};
    if (!rejected(profile,axis,0,0) || !rejected(profile,axis,0,two_pi+1e-4) ||
        !rejected(profile,axis,0,-two_pi-1e-4) || !rejected(profile,axis,nan,1) ||
        !rejected(profile,axis,0,inf) || !rejected(profile,{{0,0,0},{0,0,0}},0,pi/2.0) ||
        !rejected(nonplanar,axis,0,pi/2.0) || !rejected(crossing,axis,0,-pi/2.0)) return false;
    if (transaction.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto remains=kernel.topology().query().has_vertex(*temporary.value);
    if (!remains.value || *remains.value) return false;
    auto edit=kernel.topology().begin_transaction();
    if (edit.delete_face(source_faces.value->front()).status!=axiom::StatusCode::Ok ||
        edit.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto restored=kernel.query().mass_properties(*source.value);
    if (!restored.value || std::abs(restored.value->volume-source_mass.value->volume)>1e-10 ||
        std::abs(restored.value->area-source_mass.value->area)>1e-10 ||
        restored.value->inertia!=source_mass.value->inertia ||
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok)
        return false;
    const auto retry=kernel.sweeps().revolve_between(profile,{{0,0,0},{0,0,-5}},5.0*pi/4.0,pi/4.0);
    return retry.value &&
        kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok;
}

bool test_full_polygon_revolutions() {
    constexpr double pi = 3.1415926535897932384626433832795;
    constexpr int stations = 48;
    struct Model {
        std::string label;
        std::vector<axiom::Point3> meridian;
        std::size_t axis_vertices;
    };
    const std::vector<Model> models {
        {"annular_rectangle", {{1,0,-1},{3,0,-1},{3,0,2},{1,0,2}}, 0},
        {"annular_concave", {{1,0,-2},{4,0,-2},{4,0,-1},{2,0,-1},{2,0,2},{1,0,2}}, 0},
        {"solid_triangle", {{0,0,-1},{3,0,-1},{0,0,2}}, 2},
        {"solid_step", {{0,0,-2},{3,0,-2},{3,0,-1},{2,0,-1},{2,0,2},{0,0,2}}, 2},
    };
    const std::array<axiom::Vec3, 2> axes {{
        {0,0,1}, {2.0/3.0,-1.0/3.0,2.0/3.0}
    }};
    const std::array<axiom::Vec3, 2> radial {{
        {1,0,0}, {1.0/std::sqrt(5.0),2.0/std::sqrt(5.0),0}
    }};
    const axiom::Point3 origin {13,-7,5};
    for (std::size_t model_index = 0; model_index < models.size(); ++model_index) {
        const auto& model = models[model_index];
        double signed_twice_area = 0.0, radial_moment_numerator = 0.0;
        double exact_surface_area = 0.0;
        for (std::size_t i = 0; i < model.meridian.size(); ++i) {
            const auto& a = model.meridian[i];
            const auto& b = model.meridian[(i+1)%model.meridian.size()];
            const double cross = a.x*b.z-b.x*a.z;
            signed_twice_area += cross;
            radial_moment_numerator += (a.x+b.x)*cross;
            exact_surface_area += pi*(a.x+b.x)*std::hypot(b.x-a.x,b.z-a.z);
        }
        const double profile_area = std::abs(signed_twice_area)/2.0;
        const double centroid_radius = std::abs(radial_moment_numerator/(3.0*signed_twice_area));
        const double exact_volume = profile_area*centroid_radius*2.0*pi;
        for (std::size_t frame = 0; frame < axes.size(); ++frame) {
            for (int variant = 0; variant < 4; ++variant) {
                axiom::Kernel kernel;
                axiom::ProfileRef profile;
                profile.label = model.label+"_"+std::to_string(frame)+"_"+std::to_string(variant);
                profile.polygon_xyz = model.meridian;
                if (variant & 1) std::reverse(profile.polygon_xyz.begin(),profile.polygon_xyz.end());
                if (variant & 2) std::rotate(profile.polygon_xyz.begin(),profile.polygon_xyz.begin()+1,
                                             profile.polygon_xyz.end());
                for (auto& p : profile.polygon_xyz) {
                    p = {origin.x+radial[frame].x*p.x+axes[frame].x*p.z,
                         origin.y+radial[frame].y*p.x+axes[frame].y*p.z,
                         origin.z+radial[frame].z*p.x+axes[frame].z*p.z};
                }
                const double direction_sign = variant & 1 ? -5.0 : 3.0;
                const axiom::Axis3 axis {origin,{direction_sign*axes[frame].x,
                                                 direction_sign*axes[frame].y,
                                                 direction_sign*axes[frame].z}};
                const auto body = kernel.sweeps().revolve(profile,axis,2.0*pi);
                if (!body.value || body.status != axiom::StatusCode::Ok) {
                    std::cerr << "full polygon revolution failed: " << profile.label << '\n';
                    return false;
                }
                const auto query = kernel.topology().query();
                const auto shells = query.shells_of_body(*body.value);
                const auto faces = query.faces_of_body(*body.value);
                const auto edges = query.edges_of_body(*body.value);
                const auto vertices = query.vertices_of_body(*body.value);
                const auto mass = kernel.query().mass_properties(*body.value);
                const std::size_t off_axis = model.meridian.size()-model.axis_vertices;
                const std::size_t expected_vertices = off_axis*stations+model.axis_vertices;
                const std::size_t active_profile_edges = model.meridian.size()-(model.axis_vertices == 0 ? 0 : 1);
                const std::size_t mixed_edges = model.axis_vertices == 0 ? 0 : 2;
                const std::size_t expected_faces = stations*(2*active_profile_edges-mixed_edges);
                const std::size_t expected_edges = 3*expected_faces/2;
                if (!shells.value || shells.value->size() != 1 || !faces.value ||
                    faces.value->size() != expected_faces || !edges.value || edges.value->size() != expected_edges ||
                    !vertices.value || vertices.value->size() != expected_vertices || !mass.value ||
                    std::abs(mass.value->volume-exact_volume) > exact_volume*0.004 ||
                    std::abs(mass.value->area-exact_surface_area) > exact_surface_area*0.004 ||
                    mass.value->inertia[0] <= 0 || mass.value->inertia[4] <= 0 || mass.value->inertia[8] <= 0 ||
                    std::abs(mass.value->inertia[1]-mass.value->inertia[3]) > 1e-8 ||
                    std::abs(mass.value->inertia[2]-mass.value->inertia[6]) > 1e-8 ||
                    std::abs(mass.value->inertia[5]-mass.value->inertia[7]) > 1e-8 ||
                    kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status !=
                        axiom::StatusCode::Ok ||
                    kernel.topology().validate().validate_indices_consistency().status != axiom::StatusCode::Ok ||
                    kernel.topology().validate().validate_body_topology_indices(*body.value).status !=
                        axiom::StatusCode::Ok) {
                    std::cerr << "full revolution topology/mass/Strict mismatch: " << profile.label << '\n';
                    return false;
                }
                const auto center_offset = axiom::Vec3 {mass.value->centroid.x-origin.x,
                                                        mass.value->centroid.y-origin.y,
                                                        mass.value->centroid.z-origin.z};
                const double radial_center = std::hypot(
                    center_offset.x-axes[frame].x*(center_offset.x*axes[frame].x+
                                                  center_offset.y*axes[frame].y+
                                                  center_offset.z*axes[frame].z),
                    center_offset.y-axes[frame].y*(center_offset.x*axes[frame].x+
                                                  center_offset.y*axes[frame].y+
                                                  center_offset.z*axes[frame].z),
                    center_offset.z-axes[frame].z*(center_offset.x*axes[frame].x+
                                                  center_offset.y*axes[frame].y+
                                                  center_offset.z*axes[frame].z));
                if (radial_center > 1e-8) return false;
                double queried_area = 0.0;
                for (const auto face : *faces.value) {
                    const auto area = query.planar_face_area(face);
                    const auto owner = query.bodies_of_face(face);
                    if (!area.value || *area.value <= 0 || !owner.value || owner.value->size() != 1 ||
                        owner.value->front().value != body.value->value) return false;
                    queried_area += *area.value;
                }
                if (std::abs(queried_area-mass.value->area) > 1e-8*mass.value->area) return false;
                for (const auto edge : *edges.value) {
                    const auto uses = query.coedge_count_of_edge(edge);
                    const auto neighbors = query.faces_of_edge(edge);
                    const auto length = query.edge_length(edge);
                    if (!uses.value || *uses.value != 2 || !neighbors.value || neighbors.value->size() != 2 ||
                        !length.value || *length.value <= 0) return false;
                }
                const auto topology_bbox = query.bbox_of_body_from_topology(*body.value);
                const auto body_bbox = kernel.representation().bbox_of_body(*body.value);
                if (!topology_bbox.value || !body_bbox.value || !topology_bbox.value->is_valid ||
                    !body_bbox.value->is_valid ||
                    std::hypot(topology_bbox.value->min.x-body_bbox.value->min.x,
                               topology_bbox.value->min.y-body_bbox.value->min.y,
                               topology_bbox.value->min.z-body_bbox.value->min.z) > 1e-10 ||
                    std::hypot(topology_bbox.value->max.x-body_bbox.value->max.x,
                               topology_bbox.value->max.y-body_bbox.value->max.y,
                               topology_bbox.value->max.z-body_bbox.value->max.z) > 1e-10) return false;
                const auto mesh = kernel.convert().brep_to_mesh(*body.value,{});
                if (!mesh.value) return false;
                const auto inspection = kernel.convert().inspect_mesh(*mesh.value);
                if (!inspection.value || inspection.value->tessellation_strategy != "owned_topo_welded" ||
                    inspection.value->vertex_count != expected_vertices ||
                    inspection.value->triangle_count != expected_faces ||
                    inspection.value->connected_components != 1 || inspection.value->has_degenerate_triangles ||
                    inspection.value->has_out_of_range_indices) return false;
            }
        }
    }

    axiom::Kernel kernel;
    axiom::ProfileRef source_profile {"full_revolution_source",{{0,0,-2},{3,0,-2},{3,0,2},{0,0,2}}};
    const axiom::Axis3 z_axis {{0,0,0},{0,0,1}};
    const auto source = kernel.sweeps().revolve(source_profile,z_axis,2*pi);
    const auto source_mass = source.value ? kernel.query().mass_properties(*source.value) : axiom::Result<axiom::MassProperties>{};
    const auto source_faces = source.value ? kernel.topology().query().faces_of_body(*source.value) :
                                             axiom::Result<std::vector<axiom::FaceId>>{};
    if (!source.value || !source_mass.value || !source_faces.value || source_faces.value->empty()) return false;
    auto transaction = kernel.topology().begin_transaction();
    const auto temporary = transaction.create_vertex({20,20,20});
    const auto objects = kernel.object_count_total(), geometry = kernel.geometry_count();
    const auto bodies = kernel.body_count(), next = kernel.next_object_id();
    const auto writes = transaction.write_operation_count();
    const auto runtime = kernel.runtime_store_counts();
    if (!temporary.value || !objects.value || !geometry.value || !bodies.value || !next.value ||
        !writes.value || !runtime.value) return false;
    const auto rejected = [&](const axiom::ProfileRef& profile, const axiom::Axis3& axis, double angle) {
        const auto result = kernel.sweeps().revolve(profile,axis,angle);
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto after = kernel.runtime_store_counts();
        return result.status == axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,axiom::diag_codes::kCoreParameterOutOfRange) &&
            kernel.object_count_total().value == objects.value && kernel.geometry_count().value == geometry.value &&
            kernel.body_count().value == bodies.value && kernel.next_object_id().value == next.value &&
            transaction.write_operation_count().value == writes.value && after.value &&
            after.value->mesh_records == runtime.value->mesh_records &&
            after.value->tessellation_cache_entries == runtime.value->tessellation_cache_entries &&
            after.value->face_tessellation_cache_entries == runtime.value->face_tessellation_cache_entries;
    };
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (!rejected(source_profile,z_axis,0) || !rejected(source_profile,z_axis,-1) ||
        !rejected(source_profile,z_axis,2*pi+1e-4) || !rejected(source_profile,z_axis,nan) ||
        !rejected(source_profile,{{0,0,0},{0,0,0}},2*pi) ||
        !rejected(source_profile,{{nan,0,0},{0,0,1}},2*pi) ||
        !rejected({"cross_axis",{{-1,0,-1},{1,0,-1},{1,0,1},{-1,0,1}}},z_axis,2*pi) ||
        !rejected({"isolated_axis",{{0,0,0},{2,0,-1},{2,0,1}}},z_axis,2*pi) ||
        !rejected({"near_axis",{{1e-9,0,-1},{2,0,-1},{2,0,1},{1e-9,0,1}}},z_axis,2*pi) ||
        !rejected({"self_crossing",{{1,0,-1},{3,0,1},{1,0,1},{3,0,-1}}},z_axis,2*pi) ||
        !rejected({"nonplanar",{{1,0,-1},{3,0,-1},{3,0,1},{1,0.1,1}}},z_axis,2*pi) ||
        !rejected(source_profile,{{0,1,0},{0,0,1}},2*pi) ||
        !rejected({"holed",source_profile.polygon_xyz,{{{1,0,-1},{2,0,-1},{2,0,1},{1,0,1}}}},
                  z_axis,2*pi)) return false;
    if (transaction.rollback().status != axiom::StatusCode::Ok) return false;
    const auto remains = kernel.topology().query().has_vertex(*temporary.value);
    if (!remains.value || *remains.value) return false;
    auto edit = kernel.topology().begin_transaction();
    if (edit.delete_face(source_faces.value->front()).status != axiom::StatusCode::Ok ||
        edit.rollback().status != axiom::StatusCode::Ok) return false;
    const auto restored = kernel.query().mass_properties(*source.value);
    const auto restored_faces = kernel.topology().query().faces_of_body(*source.value);
    if (!restored.value || std::abs(restored.value->volume-source_mass.value->volume) > 1e-10 ||
        std::abs(restored.value->area-source_mass.value->area) > 1e-10 ||
        restored.value->inertia != source_mass.value->inertia || !restored_faces.value ||
        restored_faces.value->size() != source_faces.value->size() ||
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok)
        return false;
    std::reverse(source_profile.polygon_xyz.begin(),source_profile.polygon_xyz.end());
    const auto retry = kernel.sweeps().revolve(source_profile,{{0,0,0},{0,0,-7}},2*pi);
    if (!retry.value || kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status !=
        axiom::StatusCode::Ok) return false;
    return true;
}

bool test_polyline_sweeps() {
    struct Model {
        axiom::ProfileRef profile;
        // Independent signed rectangles: x, y, width, height, sign.
        std::vector<std::array<double, 5>> rectangles;
    };
    const std::vector<Model> models {
        {{"rectangle", {{0,0,0}, {4,0,0}, {4,3,0}, {0,3,0}}}, {{0,0,4,3,1}}},
        {{"concave", {{0,0,0}, {6,0,0}, {6,2,0}, {2,2,0}, {2,5,0}, {0,5,0}}},
         {{0,0,6,2,1}, {0,2,2,3,1}}},
        {{"hole", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}},
          {{{1,1,0}, {3,1,0}, {3,3,0}, {1,3,0}}}}, {{0,0,8,6,1}, {1,1,2,2,-1}}},
        {{"concave_and_second_hole", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}},
          {{{1,1,0}, {4,1,0}, {4,2,0}, {2,2,0}, {2,4,0}, {1,4,0}},
           {{5,3,0}, {7,3,0}, {7,5,0}, {5,5,0}}}},
         {{0,0,8,6,1}, {1,1,3,1,-1}, {1,2,1,2,-1}, {5,3,2,2,-1}}}
    };
    const std::vector<std::vector<axiom::Point3>> paths {
        {{0,0,0}, {1,2,6}}, // two-point polyline agrees with line sweep
        {{0,0,0}, {4,-2,1}, {-3,1,3}, {1,0,6}}, // extrema at bends, not endpoints
        {{0,0,0}, {1,0,1}, {2,0,2}, {0,2,6}} // redundant collinear station remains manifold
    };
    for (const auto& model : models) {
        double area = 0, mx = 0, my = 0, xx = 0, yy = 0, xy = 0;
        for (const auto& r : model.rectangles) {
            const double a = r[2]*r[3]*r[4], x = r[0]+r[2]/2, y = r[1]+r[3]/2;
            area += a; mx += a*x; my += a*y;
            xx += a*(x*x+r[2]*r[2]/12); yy += a*(y*y+r[3]*r[3]/12); xy += a*x*y;
        }
        const double cx = mx/area, cy = my/area;
        const std::array<double, 9> cap_cov {xx/area-cx*cx, xy/area-cx*cy, 0,
                                           xy/area-cx*cy, yy/area-cy*cy, 0, 0,0,0};
        for (auto path : paths) for (const bool reversed : {false, true}) {
            if (reversed) std::reverse(path.begin(), path.end());
            std::vector<axiom::Point3> offsets;
            for (const auto& p : path) offsets.push_back({p.x-path.front().x, p.y-path.front().y, p.z-path.front().z});
            double volume = 0;
            std::array<double, 3> mean {};
            std::array<double, 9> second {};
            for (std::size_t k = 1; k < offsets.size(); ++k) {
                const auto a = offsets[k-1], b = offsets[k];
                const double v = area*std::abs(b.z-a.z);
                const std::array<double, 3> d {b.x-a.x, b.y-a.y, b.z-a.z};
                const std::array<double, 3> center {cx+(a.x+b.x)/2, cy+(a.y+b.y)/2, (a.z+b.z)/2};
                volume += v;
                for (int i = 0; i < 3; ++i) {
                    mean[i] += v*center[i];
                    for (int j = 0; j < 3; ++j)
                        second[3*i+j] += v*(cap_cov[3*i+j]+d[i]*d[j]/12+center[i]*center[j]);
                }
            }
            for (auto& m : mean) m /= volume;
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
                second[3*i+j] = second[3*i+j]/volume - mean[i]*mean[j];
            for (const bool tilted : {false, true}) for (int variant = 0; variant < 4; ++variant) {
                axiom::Kernel kernel;
                const auto rotate = [tilted](axiom::Point3 p) -> axiom::Point3 {
                    if (!tilted) return p;
                    return {0.6*p.x-0.48*p.y+0.64*p.z, 0.8*p.x+0.36*p.y-0.48*p.z, 0.8*p.y+0.6*p.z};
                };
                const auto world = [&](axiom::Point3 p) -> axiom::Point3 {
                    const auto q = rotate(p);
                    return {q.x+11, q.y-7, q.z+5};
                };
                auto profile = model.profile;
                auto rings = profile.holes_xyz;
                rings.insert(rings.begin(), profile.polygon_xyz);
                std::size_t n = 0;
                double surface_area = 2*area;
                axiom::BoundingBox expected_bbox {};
                for (std::size_t r = 0; r < rings.size(); ++r) {
                    auto& ring = rings[r];
                    n += ring.size();
                    for (std::size_t i = 0; i < ring.size(); ++i) {
                        const auto a = ring[i], b = ring[(i+1)%ring.size()];
                        for (std::size_t k = 1; k < offsets.size(); ++k) {
                            const auto s = offsets[k-1], t = offsets[k];
                            const double dx = t.x-s.x, dy = t.y-s.y, dz = t.z-s.z;
                            surface_area += std::hypot((b.y-a.y)*dz, (b.x-a.x)*dz, (b.x-a.x)*dy-(b.y-a.y)*dx);
                        }
                        for (const auto& d : offsets) {
                            const auto p = world({a.x+d.x, a.y+d.y, a.z+d.z});
                            if (!expected_bbox.is_valid) expected_bbox = {p, p, true};
                            expected_bbox.min.x = std::min(expected_bbox.min.x, p.x);
                            expected_bbox.min.y = std::min(expected_bbox.min.y, p.y);
                            expected_bbox.min.z = std::min(expected_bbox.min.z, p.z);
                            expected_bbox.max.x = std::max(expected_bbox.max.x, p.x);
                            expected_bbox.max.y = std::max(expected_bbox.max.y, p.y);
                            expected_bbox.max.z = std::max(expected_bbox.max.z, p.z);
                        }
                    }
                    if (variant & (r == 0 ? 1 : 2)) std::reverse(ring.begin(), ring.end());
                    std::rotate(ring.begin(), ring.begin()+variant%ring.size(), ring.end());
                    for (auto& p : ring) p = world(p);
                }
                profile.polygon_xyz = rings.front();
                profile.holes_xyz.assign(rings.begin()+1, rings.end());
                if (variant & 2) std::reverse(profile.holes_xyz.begin(), profile.holes_xyz.end());
                std::vector<axiom::Point3> rail_points;
                for (const auto& p : path) {
                    const auto q = rotate(p);
                    rail_points.push_back({q.x+50, q.y+60, q.z+70});
                }
                const auto rail = kernel.curves().make_composite_polyline(rail_points);
                if (!rail.value) return false;
                const auto body = kernel.sweeps().sweep(profile, *rail.value);
                if (!body.value || body.status != axiom::StatusCode::Ok) {
                    std::cerr << "polyline sweep construction failed: " << profile.label << '\n';
                    return false;
                }
                const auto query = kernel.topology().query();
                const auto faces = query.faces_of_body(*body.value);
                const auto edges = query.edges_of_body(*body.value);
                const auto vertices = query.vertices_of_body(*body.value);
                const auto shells = query.shells_of_body(*body.value);
                const auto mass = kernel.query().mass_properties(*body.value);
                const auto center = world({mean[0], mean[1], mean[2]});
                const std::size_t f = 2*(n+2*profile.holes_xyz.size()-2)+2*n*(path.size()-1);
                if (!faces.value || faces.value->size() != f || !edges.value || edges.value->size() != 3*f/2 ||
                    !vertices.value || vertices.value->size() != n*path.size() || !shells.value || shells.value->size() != 1 ||
                    !mass.value || std::abs(mass.value->volume-volume) > 1e-8 ||
                    std::abs(mass.value->area-surface_area) > 1e-8 ||
                    std::hypot(mass.value->centroid.x-center.x, mass.value->centroid.y-center.y,
                               mass.value->centroid.z-center.z) > 1e-8 ||
                    kernel.validate().validate_all(*body.value, axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) {
                    std::cerr << "polyline sweep topology/mass/Strict mismatch: " << profile.label << '\n';
                    return false;
                }
                const auto rx = rotate({1,0,0}), ry = rotate({0,1,0}), rz = rotate({0,0,1});
                const std::array<double, 9> rotation {rx.x,ry.x,rz.x, rx.y,ry.y,rz.y, rx.z,ry.z,rz.z};
                std::array<double, 9> covariance {};
                for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
                    for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b)
                        covariance[3*i+j] += rotation[3*i+a]*second[3*a+b]*rotation[3*j+b];
                const double trace = covariance[0]+covariance[4]+covariance[8];
                for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
                    if (std::abs(mass.value->inertia[3*i+j]-volume*((i == j ? trace : 0)-covariance[3*i+j])) > 1e-7)
                        return false;
                double queried_area = 0;
                for (const auto face : *faces.value) {
                    const auto a = query.planar_face_area(face);
                    const auto owners = query.bodies_of_face(face);
                    if (!a.value || *a.value <= 0 || !owners.value || owners.value->size() != 1 ||
                        owners.value->front().value != body.value->value) return false;
                    queried_area += *a.value;
                }
                for (const auto edge : *edges.value) {
                    const auto uses = query.coedge_count_of_edge(edge);
                    const auto neighbors = query.faces_of_edge(edge);
                    if (!uses.value || *uses.value != 2 || !neighbors.value || neighbors.value->size() != 2) return false;
                }
                if (std::abs(queried_area-surface_area) > 1e-8) return false;
                const auto bbox = query.bbox_of_body_from_topology(*body.value);
                const auto cached_bbox = kernel.representation().bbox_of_body(*body.value);
                for (const auto& bounds : {bbox, cached_bbox}) {
                    if (!bounds.value || !bounds.value->is_valid ||
                        std::hypot(bounds.value->min.x-expected_bbox.min.x, bounds.value->min.y-expected_bbox.min.y,
                                   bounds.value->min.z-expected_bbox.min.z) > 1e-8 ||
                        std::hypot(bounds.value->max.x-expected_bbox.max.x, bounds.value->max.y-expected_bbox.max.y,
                                   bounds.value->max.z-expected_bbox.max.z) > 1e-8) return false;
                }
                const auto mesh = kernel.convert().brep_to_mesh(*body.value, {});
                if (!mesh.value) return false;
                const auto inspection = kernel.convert().inspect_mesh(*mesh.value);
                if (!inspection.value || inspection.value->tessellation_strategy != "owned_topo_welded" ||
                    inspection.value->vertex_count != n*path.size() || inspection.value->triangle_count != f ||
                    inspection.value->connected_components != 1 || inspection.value->has_degenerate_triangles ||
                    inspection.value->has_out_of_range_indices) return false;
                if (path.size() == 2) {
                    const auto line = kernel.curves().make_line_segment(rail_points.front(), rail_points.back());
                    if (!line.value) return false;
                    const auto straight = kernel.sweeps().sweep(profile, *line.value);
                    if (!straight.value) return false;
                    const auto other = kernel.query().mass_properties(*straight.value);
                    if (!other.value || std::abs(other.value->volume-volume) > 1e-8 ||
                        std::abs(other.value->area-surface_area) > 1e-8) return false;
                }
            }
        }
    }

    axiom::Kernel kernel;
    const auto rail = kernel.curves().make_composite_polyline(paths[1]);
    if (!rail.value) return false;
    const auto source = kernel.sweeps().sweep(models.back().profile, *rail.value);
    if (!source.value || !kernel.convert().brep_to_mesh(*source.value, {}).value) return false;
    const double huge = std::numeric_limits<double>::max();
    const std::vector<std::vector<axiom::Point3>> bad_paths {
        {{0,0,0}, {0,0,0}, {0,0,3}}, // repeated point
        {{0,0,0}, {1,0,0}, {0,0,3}}, // tangency at first segment
        {{0,0,0}, {0,0,1}, {1,0,1}, {1,0,3}}, // tangency at interior segment
        {{0,0,0}, {0,0,2}, {1,0,1}, {1,0,3}}, // backtracking despite positive total displacement
        {{0,0,0}, {0,0,1}, {0,0,0}}, // closed rail
        {{0,0,0}, {1,0,1e-9}, {0,0,3}}, // nearly tangent
        {{0,0,0}, {0,0,1e-20}, {0,0,3}}, // collapsed station
        {{0,0,0}, {huge,huge,1}, {0,0,3}} // intermediate overflow
    };
    std::vector<axiom::CurveId> invalid_rails;
    for (const auto& points : bad_paths) {
        const auto bad = kernel.curves().make_composite_polyline(points);
        if (!bad.value) return false;
        invalid_rails.push_back(*bad.value);
    }
    auto transaction = kernel.topology().begin_transaction();
    const auto temporary = transaction.create_vertex({20,20,20});
    if (!temporary.value) return false;
    const auto objects = kernel.object_count_total(), geometry = kernel.geometry_count(), bodies = kernel.body_count();
    const auto next = kernel.next_object_id(), writes = transaction.write_operation_count();
    const auto runtime = kernel.runtime_store_counts();
    if (!objects.value || !geometry.value || !bodies.value || !next.value || !writes.value || !runtime.value) return false;
    const auto rejected = [&](const axiom::ProfileRef& profile, axiom::CurveId input,
                              std::string_view code = axiom::diag_codes::kCoreParameterOutOfRange) {
        const auto result = kernel.sweeps().sweep(profile, input);
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto after = kernel.runtime_store_counts();
        return result.status == axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value, code) && kernel.object_count_total().value == objects.value &&
            kernel.geometry_count().value == geometry.value && kernel.body_count().value == bodies.value &&
            kernel.next_object_id().value == next.value && transaction.write_operation_count().value == writes.value &&
            after.value && after.value->mesh_records == runtime.value->mesh_records &&
            after.value->tessellation_cache_entries == runtime.value->tessellation_cache_entries &&
            after.value->curve_eval_cache_entries == runtime.value->curve_eval_cache_entries &&
            after.value->surface_eval_cache_entries == runtime.value->surface_eval_cache_entries;
    };
    for (const auto input : invalid_rails) if (!rejected(models.back().profile, input)) {
        std::cerr << "invalid polyline rail must fail without model/ID/transaction/cache writes\n";
        return false;
    }
    const std::vector<std::vector<axiom::Point3>> bad_profiles {
        {{0,0,0}, {1,0,0}}, {{0,0,0}, {1,0,0}, {2,0,0}},
        {{0,0,0}, {3,0,0}, {3,0,0}, {0,3,0}},
        {{0,0,0}, {4,3,0}, {0,4,0}, {3,0,0}},
        {{0,0,0}, {3,0,0}, {3,3,0.1}, {0,3,0}},
        {{0,0,0}, {3,0,0}, {0,std::numeric_limits<double>::quiet_NaN(),0}}
    };
    for (const auto& points : bad_profiles) if (!rejected({"bad", points}, *rail.value)) return false;
    auto bad_hole = models[2].profile;
    for (auto& p : bad_hole.holes_xyz.front()) p.x += 10;
    if (!rejected(bad_hole, *rail.value)) return false;
    bad_hole = models[2].profile;
    bad_hole.holes_xyz.push_back(bad_hole.holes_xyz.front());
    if (!rejected(bad_hole, *rail.value)) return false;
    auto touching_hole = models[2].profile;
    for (auto& p : touching_hole.holes_xyz.front()) p.x -= 1;
    if (!rejected(touching_hole, *rail.value)) return false;
    auto collapsed_profile = models.front().profile;
    for (auto& p : collapsed_profile.polygon_xyz) p.z = 1e17;
    if (!rejected(collapsed_profile, *rail.value)) return false;
    bad_hole.polygon_xyz.clear();
    if (!rejected(bad_hole, *rail.value) ||
        !rejected(models.front().profile, {}, axiom::diag_codes::kCoreInvalidHandle) ||
        !rejected({"", models.front().profile.polygon_xyz}, *rail.value, axiom::diag_codes::kCoreInvalidHandle)) return false;
    if (transaction.rollback().status != axiom::StatusCode::Ok) return false;
    const auto remains = kernel.topology().query().has_vertex(*temporary.value);
    const auto after_rollback = kernel.object_count_total();
    if (!remains.value || *remains.value || !after_rollback.value || *after_rollback.value+1 != *objects.value) return false;
    const auto retry = kernel.sweeps().sweep(models.back().profile, *rail.value);
    if (!retry.value || kernel.validate().validate_all(*retry.value, axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
        kernel.validate().validate_all(*source.value, axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) return false;
    return true;
}

bool test_curve_frame_sweeps() {
    const auto add = [](axiom::Point3 p, axiom::Vec3 a, double x, axiom::Vec3 b, double y) {
        return axiom::Point3 {p.x + a.x*x + b.x*y, p.y + a.y*x + b.y*y, p.z + a.z*x + b.z*y};
    };
    const auto cross = [](axiom::Vec3 a, axiom::Vec3 b) {
        return axiom::Vec3 {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
    };
    const auto unit = [](axiom::Vec3 a) {
        const double length = std::hypot(a.x,a.y,a.z);
        return axiom::Vec3 {a.x/length,a.y/length,a.z/length};
    };
    const auto make_profile = [&](axiom::Point3 origin, axiom::Vec3 tangent, int variant, bool hole) {
        tangent = unit(tangent);
        const axiom::Vec3 reference = std::abs(tangent.z) < 0.8 ? axiom::Vec3 {0,0,1} : axiom::Vec3 {0,1,0};
        const auto u = unit(cross(tangent,reference));
        const auto v = unit(cross(tangent,u));
        axiom::ProfileRef profile {"curve_frame",
            {add(origin,u,-0.8,v,-0.6), add(origin,u,0.8,v,-0.6), add(origin,u,0.8,v,0.6),
             add(origin,u,0.15,v,0.6), add(origin,u,0.15,v,0.15), add(origin,u,-0.8,v,0.15)}};
        if (hole) {
            profile.polygon_xyz = {add(origin,u,-1.0,v,-0.8), add(origin,u,1.0,v,-0.8),
                                   add(origin,u,1.0,v,0.8), add(origin,u,-1.0,v,0.8)};
            profile.holes_xyz = {{add(origin,u,-0.35,v,-0.25), add(origin,u,0.35,v,-0.25),
                                  add(origin,u,0.35,v,0.25), add(origin,u,-0.35,v,0.25)}};
        }
        auto rotate_ring = [variant](std::vector<axiom::Point3>& ring, int bit) {
            if ((variant & bit) != 0) std::reverse(ring.begin(),ring.end());
            const auto offset = static_cast<std::size_t>(variant) % ring.size();
            std::rotate(ring.begin(),ring.begin()+static_cast<std::ptrdiff_t>(offset),ring.end());
        };
        rotate_ring(profile.polygon_xyz,1);
        for (auto& ring : profile.holes_xyz) rotate_ring(ring,2);
        return profile;
    };
    enum class RailKind { Bezier, BSpline, Nurbs, Circle, Ellipse };
    const std::array<RailKind,5> kinds {
        RailKind::Bezier,RailKind::BSpline,RailKind::Nurbs,RailKind::Circle,RailKind::Ellipse};
    for (const auto kind : kinds) for (const bool hole : {false,true}) for (int variant = 0; variant < 4; ++variant) {
        axiom::Kernel kernel;
        axiom::Result<axiom::CurveId> rail;
        if (kind == RailKind::Bezier) {
            const std::vector<axiom::Point3> poles {{0,0,0},{4,0,0},{8,1.5,0.5},{12,3,1.5}};
            rail = kernel.curves().make_bezier(poles);
        } else if (kind == RailKind::BSpline) {
            rail = kernel.curves().make_bspline({{{0,0,0},{3,0,0},{6,0.8,0.2},{9,2,0.8},{12,3,1.5}},3,{}});
        } else if (kind == RailKind::Nurbs) {
            rail = kernel.curves().make_nurbs({{{0,0,0},{3,0,0},{6,1,0.3},{9,2.2,0.9},{12,3,1.5}},
                                                {1,0.8,1.3,0.9,1},4,{}});
        } else if (kind == RailKind::Circle) {
            rail = kernel.curves().make_circle({2,-3,4},{0.2,-0.3,1},12);
        } else {
            rail = kernel.curves().make_ellipse({2,-3,4},{12,0,0},{0,8,2.4});
        }
        if (!rail.value) return false;
        const auto domain = kernel.curve_service().domain(*rail.value);
        if (!domain.value) return false;
        const auto start = kernel.curve_service().eval(*rail.value,domain.value->min,1);
        if (!start.value) return false;
        const auto profile = make_profile(start.value->point,start.value->tangent,variant,hole);
        const auto body = kernel.sweeps().sweep(profile,*rail.value);
        if (!body.value || body.status != axiom::StatusCode::Ok) {
            std::cerr << "curve-frame sweep construction failed\n";
            return false;
        }
        const auto query = kernel.topology().query();
        const auto faces = query.faces_of_body(*body.value);
        const auto edges = query.edges_of_body(*body.value);
        const auto vertices = query.vertices_of_body(*body.value);
        const auto shells = query.shells_of_body(*body.value);
        const auto mass = kernel.query().mass_properties(*body.value);
        const std::size_t n = profile.polygon_xyz.size() + (hole ? profile.holes_xyz.front().size() : 0);
        if (!vertices.value || vertices.value->size() % n != 0) return false;
        const std::size_t stations = vertices.value->size()/n;
        const std::size_t cap_triangles = n + 2*profile.holes_xyz.size() - 2;
        const bool periodic = kind == RailKind::Circle || kind == RailKind::Ellipse;
        const std::size_t expected_faces = periodic
            ? 2*n*stations : 2*cap_triangles + 2*n*(stations-1);
        const std::size_t expected_shells = periodic ? 1 + profile.holes_xyz.size() : 1;
        const auto strict = kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict);
        if (!faces.value || faces.value->size() != expected_faces || !edges.value ||
            edges.value->size() != 3*expected_faces/2 || !vertices.value || vertices.value->size() != n*stations ||
            !shells.value || shells.value->size() != expected_shells || !mass.value || !(mass.value->volume > 0) ||
            !(mass.value->area > 0) || !std::isfinite(mass.value->centroid.x) ||
            !std::all_of(mass.value->inertia.begin(),mass.value->inertia.end(),
                         [](double value) { return std::isfinite(value); }) ||
            strict.status != axiom::StatusCode::Ok) {
            const auto strict_diagnostic = kernel.diagnostics().get(strict.diagnostic_id);
            std::cerr << "curve-frame sweep topology/mass/Strict mismatch: kind=" << static_cast<int>(kind)
                      << " hole=" << hole << " variant=" << variant << " faces="
                      << (faces.value ? faces.value->size() : 0) << '/' << expected_faces << " edges="
                      << (edges.value ? edges.value->size() : 0) << '/' << 3*expected_faces/2 << " vertices="
                      << (vertices.value ? vertices.value->size() : 0) << '/' << n*stations << " shells="
                      << (shells.value ? shells.value->size() : 0) << '/' << expected_shells
                      << " strict=" << static_cast<int>(strict.status)
                      << " issue=" << (strict_diagnostic.value && !strict_diagnostic.value->issues.empty()
                                           ? strict_diagnostic.value->issues.front().code : "none")
                      << '\n';
            return false;
        }
        double queried_area = 0.0;
        for (const auto face : *faces.value) {
            const auto area = query.planar_face_area(face);
            const auto owners = query.bodies_of_face(face);
            if (!area.value || !(*area.value > 0) || !owners.value || owners.value->size() != 1 ||
                owners.value->front().value != body.value->value) return false;
            queried_area += *area.value;
        }
        for (const auto edge : *edges.value) {
            const auto uses = query.coedge_count_of_edge(edge);
            const auto adjacent = query.faces_of_edge(edge);
            if (!uses.value || *uses.value != 2 || !adjacent.value || adjacent.value->size() != 2) return false;
        }
        if (std::abs(queried_area-mass.value->area) > 1e-7*std::max(1.0,mass.value->area)) return false;
        const auto topo_bbox = query.bbox_of_body_from_topology(*body.value);
        const auto rep_bbox = kernel.representation().bbox_of_body(*body.value);
        if (!topo_bbox.value || !rep_bbox.value || !topo_bbox.value->is_valid || !rep_bbox.value->is_valid ||
            std::hypot(topo_bbox.value->min.x-rep_bbox.value->min.x,
                       topo_bbox.value->min.y-rep_bbox.value->min.y,
                       topo_bbox.value->min.z-rep_bbox.value->min.z) > 1e-9 ||
            std::hypot(topo_bbox.value->max.x-rep_bbox.value->max.x,
                       topo_bbox.value->max.y-rep_bbox.value->max.y,
                       topo_bbox.value->max.z-rep_bbox.value->max.z) > 1e-9) return false;
        const auto mesh = kernel.convert().brep_to_mesh(*body.value,{});
        const auto inspection = mesh.value ? kernel.convert().inspect_mesh(*mesh.value)
                                           : axiom::Result<axiom::MeshInspectionReport>{};
        if (!inspection.value || inspection.value->tessellation_strategy != "owned_topo_welded" ||
            inspection.value->vertex_count != n*stations || inspection.value->triangle_count != expected_faces ||
            inspection.value->connected_components != expected_shells || inspection.value->has_degenerate_triangles ||
            inspection.value->has_out_of_range_indices) return false;
    }

    axiom::Kernel kernel;
    const std::vector<axiom::Point3> gentle_poles {{0,0,0},{4,0,0},{8,1,0.5},{12,2,1}};
    const auto good_rail = kernel.curves().make_bezier(gentle_poles);
    const auto circle = kernel.curves().make_circle({0,0,0},{0,0,1},8);
    const auto unsupported = kernel.curves().make_parabola({0,0,0},{1,0,0},{0,1,0},2);
    const auto closed_spline = kernel.curves().make_bezier(
        std::vector<axiom::Point3>{{0,0,0},{3,0,0},{3,3,0},{0,0,0}});
    const auto cusp = kernel.curves().make_bezier(
        std::vector<axiom::Point3>{{0,0,0},{0,0,0},{0,0,0},{0,0,0}});
    if (!good_rail.value || !circle.value || !unsupported.value || !closed_spline.value || !cusp.value) return false;
    const auto start = kernel.curve_service().eval(*good_rail.value,0,1);
    const auto circle_start = kernel.curve_service().eval(*circle.value,0,1);
    if (!start.value || !circle_start.value) return false;
    const auto good_profile = make_profile(start.value->point,start.value->tangent,0,true);
    const auto source = kernel.sweeps().sweep(good_profile,*good_rail.value);
    if (!source.value) return false;
    auto transaction = kernel.topology().begin_transaction();
    const auto temporary = transaction.create_vertex({90,91,92});
    const auto objects = kernel.object_count_total(), geometry = kernel.geometry_count(), bodies = kernel.body_count();
    const auto next = kernel.next_object_id(), writes = transaction.write_operation_count();
    const auto runtime = kernel.runtime_store_counts();
    if (!temporary.value || !objects.value || !geometry.value || !bodies.value || !next.value || !writes.value ||
        !runtime.value) return false;
    const auto rejected = [&](const axiom::ProfileRef& profile, axiom::CurveId rail) {
        const auto result = kernel.sweeps().sweep(profile,rail);
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto after = kernel.runtime_store_counts();
        return result.status == axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,axiom::diag_codes::kCoreParameterOutOfRange) &&
            kernel.object_count_total().value == objects.value && kernel.geometry_count().value == geometry.value &&
            kernel.body_count().value == bodies.value && kernel.next_object_id().value == next.value &&
            transaction.write_operation_count().value == writes.value && after.value &&
            after.value->mesh_records == runtime.value->mesh_records &&
            after.value->tessellation_cache_entries == runtime.value->tessellation_cache_entries &&
            after.value->curve_eval_cache_entries == runtime.value->curve_eval_cache_entries &&
            after.value->surface_eval_cache_entries == runtime.value->surface_eval_cache_entries;
    };
    auto tilted_profile = good_profile;
    for (auto& p : tilted_profile.polygon_xyz) p.x += 0.2*p.y;
    for (auto& ring : tilted_profile.holes_xyz) for (auto& p : ring) p.x += 0.2*p.y;
    auto bad_hole = good_profile;
    for (auto& p : bad_hole.holes_xyz.front()) p.y += 10;
    const auto large_circle_profile = make_profile(circle_start.value->point,circle_start.value->tangent,0,false);
    auto oversized = large_circle_profile;
    for (auto& p : oversized.polygon_xyz) {
        p.x = circle_start.value->point.x + 6*(p.x-circle_start.value->point.x);
        p.y = circle_start.value->point.y + 6*(p.y-circle_start.value->point.y);
        p.z = circle_start.value->point.z + 6*(p.z-circle_start.value->point.z);
    }
    if (!rejected(good_profile,*unsupported.value) || !rejected(good_profile,*closed_spline.value) ||
        !rejected(good_profile,*cusp.value) || !rejected(tilted_profile,*good_rail.value) ||
        !rejected(bad_hole,*good_rail.value) || !rejected(oversized,*circle.value)) return false;
    if (transaction.rollback().status != axiom::StatusCode::Ok) return false;
    const auto remains = kernel.topology().query().has_vertex(*temporary.value);
    const auto after_rollback = kernel.object_count_total();
    if (!remains.value || *remains.value || !after_rollback.value || *after_rollback.value+1 != *objects.value) return false;
    const auto retry = kernel.sweeps().sweep(good_profile,*good_rail.value);
    return retry.value &&
        kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok &&
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok;
}

bool test_closed_spline_sweeps() {
    const auto add = [](axiom::Point3 p, axiom::Vec3 a, double x, axiom::Vec3 b, double y) {
        return axiom::Point3 {p.x+a.x*x+b.x*y, p.y+a.y*x+b.y*y, p.z+a.z*x+b.z*y};
    };
    const auto cross = [](axiom::Vec3 a, axiom::Vec3 b) {
        return axiom::Vec3 {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
    };
    const auto unit = [](axiom::Vec3 a) {
        const double length = std::hypot(a.x,a.y,a.z);
        return axiom::Vec3 {a.x/length,a.y/length,a.z/length};
    };
    const auto profile_at = [&](axiom::Point3 origin, axiom::Vec3 tangent, int variant, bool hole,
                                double scale = 1.0) {
        tangent = unit(tangent);
        const auto reference = std::abs(tangent.z) < 0.8 ? axiom::Vec3 {0,0,1} : axiom::Vec3 {0,1,0};
        const auto u = unit(cross(tangent,reference));
        const auto v = unit(cross(tangent,u));
        axiom::ProfileRef profile {"closed_spline_section",
            {add(origin,u,-0.24*scale,v,-0.18*scale), add(origin,u,0.24*scale,v,-0.18*scale),
             add(origin,u,0.24*scale,v,0.18*scale), add(origin,u,-0.24*scale,v,0.18*scale)}};
        if (hole) {
            profile.polygon_xyz = {
                add(origin,u,-0.3*scale,v,-0.24*scale), add(origin,u,0.3*scale,v,-0.24*scale),
                add(origin,u,0.3*scale,v,0.24*scale), add(origin,u,-0.3*scale,v,0.24*scale)};
            profile.holes_xyz = {{
                add(origin,u,-0.1*scale,v,-0.08*scale), add(origin,u,0.1*scale,v,-0.08*scale),
                add(origin,u,0.1*scale,v,0.08*scale), add(origin,u,-0.1*scale,v,0.08*scale)}};
        }
        if (variant & 1) std::reverse(profile.polygon_xyz.begin(),profile.polygon_xyz.end());
        std::rotate(profile.polygon_xyz.begin(),
                    profile.polygon_xyz.begin()+static_cast<std::ptrdiff_t>(variant%profile.polygon_xyz.size()),
                    profile.polygon_xyz.end());
        for (auto& ring : profile.holes_xyz) {
            if (variant & 2) std::reverse(ring.begin(),ring.end());
            std::rotate(ring.begin(),ring.begin()+static_cast<std::ptrdiff_t>(variant%ring.size()),ring.end());
        }
        return profile;
    };
    enum class RailKind { Bezier, BSpline, Nurbs };
    const std::array<RailKind,3> kinds {RailKind::Bezier,RailKind::BSpline,RailKind::Nurbs};
    for (const auto kind : kinds) for (const bool spatial : {false,true})
        for (const bool hole : {false,true}) for (int variant = 0; variant < 4; ++variant) {
        axiom::Kernel kernel;
        std::vector<axiom::Point3> poles {
            {15,0,4}, {15,7,4.0+(spatial?2.0:0.0)}, {8,13,4.0+(spatial?3.0:0.0)},
            {-2,15,4.0+(spatial?1.0:0.0)}, {-12,8,4.0-(spatial?2.0:0.0)},
            {-14,-2,4.0-(spatial?3.0:0.0)}, {-8,-12,4.0-(spatial?1.0:0.0)},
            {3,-14,4.0+(spatial?2.0:0.0)}, {15,-7,4.0-(spatial?2.0:0.0)}, {15,0,4}
        };
        axiom::Result<axiom::CurveId> rail;
        if (kind == RailKind::Bezier) rail = kernel.curves().make_bezier(poles);
        else if (kind == RailKind::BSpline) rail = kernel.curves().make_bspline({poles,3,{}});
        else rail = kernel.curves().make_nurbs({poles,{1,0.8,1.2,0.9,1.1,0.85,1.15,0.9,1.05,1},3,{}});
        if (!rail.value) return false;
        const auto domain = kernel.curve_service().domain(*rail.value);
        if (!domain.value) return false;
        const auto start = kernel.curve_service().eval(*rail.value,domain.value->min,1);
        const auto end = kernel.curve_service().eval(*rail.value,domain.value->max,1);
        if (!start.value || !end.value ||
            std::hypot(start.value->point.x-end.value->point.x,start.value->point.y-end.value->point.y,
                       start.value->point.z-end.value->point.z) > 1e-10 ||
            start.value->tangent.x*end.value->tangent.x + start.value->tangent.y*end.value->tangent.y +
                start.value->tangent.z*end.value->tangent.z < 1.0-1e-8) return false;
        const auto profile = profile_at(start.value->point,start.value->tangent,variant,hole);
        const auto body = kernel.sweeps().sweep(profile,*rail.value);
        if (!body.value || body.status != axiom::StatusCode::Ok) {
            std::cerr << "closed spline sweep construction failed: kind=" << static_cast<int>(kind)
                      << " spatial=" << spatial << " hole=" << hole << " variant=" << variant << '\n';
            return false;
        }
        const auto query = kernel.topology().query();
        const auto faces = query.faces_of_body(*body.value);
        const auto edges = query.edges_of_body(*body.value);
        const auto vertices = query.vertices_of_body(*body.value);
        const auto shells = query.shells_of_body(*body.value);
        const auto mass = kernel.query().mass_properties(*body.value);
        const std::size_t section_vertices = profile.polygon_xyz.size() +
            (hole ? profile.holes_xyz.front().size() : 0);
        if (!vertices.value || vertices.value->empty() || vertices.value->size()%section_vertices != 0) return false;
        const std::size_t stations = vertices.value->size()/section_vertices;
        const std::size_t expected_faces = 2*section_vertices*stations;
        const std::size_t expected_shells = 1+profile.holes_xyz.size();
        if (stations < 32 || !faces.value || faces.value->size() != expected_faces || !edges.value ||
            edges.value->size() != 3*expected_faces/2 || !shells.value || shells.value->size() != expected_shells ||
            !mass.value || !(mass.value->volume > 0) || !(mass.value->area > 0) ||
            !std::isfinite(mass.value->centroid.x) ||
            !std::all_of(mass.value->inertia.begin(),mass.value->inertia.end(),
                         [](double value) { return std::isfinite(value); }) ||
            kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
            kernel.topology().validate().validate_body_topology_indices(*body.value).status != axiom::StatusCode::Ok) {
            std::cerr << "closed spline sweep topology/mass/Strict mismatch\n";
            return false;
        }
        double queried_area = 0.0;
        for (const auto face : *faces.value) {
            const auto area = query.planar_face_area(face);
            const auto owners = query.bodies_of_face(face);
            const auto loops = query.loops_of_face(face);
            if (!area.value || !(*area.value > 0) || !owners.value || owners.value->size() != 1 ||
                owners.value->front().value != body.value->value || !loops.value || loops.value->size() != 1) return false;
            queried_area += *area.value;
        }
        for (const auto edge : *edges.value) {
            const auto uses = query.coedge_count_of_edge(edge);
            const auto adjacent = query.faces_of_edge(edge);
            if (!uses.value || *uses.value != 2 || !adjacent.value || adjacent.value->size() != 2) return false;
        }
        if (std::abs(queried_area-mass.value->area) > 1e-7*std::max(1.0,mass.value->area)) return false;
        const auto topo_bbox = query.bbox_of_body_from_topology(*body.value);
        const auto rep_bbox = kernel.representation().bbox_of_body(*body.value);
        if (!topo_bbox.value || !rep_bbox.value || !topo_bbox.value->is_valid || !rep_bbox.value->is_valid ||
            std::hypot(topo_bbox.value->min.x-rep_bbox.value->min.x,
                       topo_bbox.value->min.y-rep_bbox.value->min.y,
                       topo_bbox.value->min.z-rep_bbox.value->min.z) > 1e-9 ||
            std::hypot(topo_bbox.value->max.x-rep_bbox.value->max.x,
                       topo_bbox.value->max.y-rep_bbox.value->max.y,
                       topo_bbox.value->max.z-rep_bbox.value->max.z) > 1e-9) return false;
        const auto mesh = kernel.convert().brep_to_mesh(*body.value,{});
        const auto inspection = mesh.value ? kernel.convert().inspect_mesh(*mesh.value)
                                           : axiom::Result<axiom::MeshInspectionReport>{};
        if (!inspection.value || inspection.value->tessellation_strategy != "owned_topo_welded" ||
            inspection.value->vertex_count != section_vertices*stations ||
            inspection.value->triangle_count != expected_faces ||
            inspection.value->connected_components != expected_shells || inspection.value->has_degenerate_triangles ||
            inspection.value->has_out_of_range_indices) return false;
    }

    axiom::Kernel kernel;
    const std::vector<axiom::Point3> good_poles {
        {12,0,0},{12,6,1},{6,11,2},{-4,11,-1},{-12,4,-2},
        {-10,-7,1},{0,-12,2},{10,-7,-1},{12,-6,-1},{12,0,0}
    };
    const auto good_rail = kernel.curves().make_bezier(good_poles);
    const auto discontinuous = kernel.curves().make_bezier(
        std::vector<axiom::Point3>{{8,0,0},{8,5,0},{0,9,0},{-8,0,0},{4,4,0},{8,0,0}});
    const auto cusp = kernel.curves().make_bezier(
        std::vector<axiom::Point3>{{8,0,0},{8,0,0},{0,9,0},{-8,0,0},{8,-5,0},{8,0,0}});
    if (!good_rail.value || !discontinuous.value || !cusp.value) return false;
    const auto start = kernel.curve_service().eval(*good_rail.value,0,1);
    const auto bad_start = kernel.curve_service().eval(*discontinuous.value,0,1);
    if (!start.value || !bad_start.value) return false;
    const auto good_profile = profile_at(start.value->point,start.value->tangent,0,true);
    const auto bad_profile = profile_at(bad_start.value->point,bad_start.value->tangent,0,false);
    const auto source = kernel.sweeps().sweep(good_profile,*good_rail.value);
    if (!source.value) return false;
    auto transaction = kernel.topology().begin_transaction();
    const auto temporary = transaction.create_vertex({120,121,122});
    const auto objects = kernel.object_count_total(), geometry = kernel.geometry_count(), bodies = kernel.body_count();
    const auto next = kernel.next_object_id(), writes = transaction.write_operation_count();
    const auto runtime = kernel.runtime_store_counts();
    if (!temporary.value || !objects.value || !geometry.value || !bodies.value || !next.value || !writes.value ||
        !runtime.value) return false;
    const auto rejected = [&](const axiom::ProfileRef& profile, axiom::CurveId rail) {
        const auto result = kernel.sweeps().sweep(profile,rail);
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto after = kernel.runtime_store_counts();
        return result.status == axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,axiom::diag_codes::kCoreParameterOutOfRange) &&
            kernel.object_count_total().value == objects.value && kernel.geometry_count().value == geometry.value &&
            kernel.body_count().value == bodies.value && kernel.next_object_id().value == next.value &&
            transaction.write_operation_count().value == writes.value && after.value &&
            after.value->mesh_records == runtime.value->mesh_records &&
            after.value->tessellation_cache_entries == runtime.value->tessellation_cache_entries &&
            after.value->curve_eval_cache_entries == runtime.value->curve_eval_cache_entries &&
            after.value->surface_eval_cache_entries == runtime.value->surface_eval_cache_entries;
    };
    auto tilted = good_profile;
    for (auto& p : tilted.polygon_xyz) p.y += 0.2*p.x;
    for (auto& ring : tilted.holes_xyz) for (auto& p : ring) p.y += 0.2*p.x;
    const auto oversized = profile_at(start.value->point,start.value->tangent,0,false,30.0);
    if (!rejected(bad_profile,*discontinuous.value) || !rejected(good_profile,*cusp.value) ||
        !rejected(tilted,*good_rail.value) || !rejected(oversized,*good_rail.value)) return false;
    if (transaction.rollback().status != axiom::StatusCode::Ok) return false;
    const auto remains = kernel.topology().query().has_vertex(*temporary.value);
    const auto after_rollback = kernel.object_count_total();
    if (!remains.value || *remains.value || !after_rollback.value || *after_rollback.value+1 != *objects.value) return false;
    const auto retry = kernel.sweeps().sweep(good_profile,*good_rail.value);
    return retry.value &&
        kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok &&
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok;
}

bool test_composite_chain_sweeps() {
    const auto add = [](axiom::Point3 p, axiom::Vec3 direction, double amount) {
        return axiom::Point3 {p.x+direction.x*amount,p.y+direction.y*amount,p.z+direction.z*amount};
    };
    const auto unit = [](axiom::Vec3 direction) {
        const double length = std::hypot(direction.x,direction.y,direction.z);
        return axiom::Vec3 {direction.x/length,direction.y/length,direction.z/length};
    };
    const auto open_profile = [](bool hole, int variant) {
        axiom::ProfileRef profile {"composite_chain_section",
            {{0,-0.22,-0.16},{0,0.22,-0.16},{0,0.22,0.16},{0,-0.22,0.16}}};
        if (hole) profile = {"composite_chain_holed_section",
            {{0,-0.3,-0.24},{0,0.3,-0.24},{0,0.3,0.24},{0,-0.3,0.24}},
            {{{0,-0.1,-0.08},{0,0.1,-0.08},{0,0.1,0.08},{0,-0.1,0.08}}}};
        if (variant&1) std::reverse(profile.polygon_xyz.begin(),profile.polygon_xyz.end());
        std::rotate(profile.polygon_xyz.begin(),profile.polygon_xyz.begin()+variant%profile.polygon_xyz.size(),
                    profile.polygon_xyz.end());
        for (auto& ring : profile.holes_xyz) {
            if (variant&2) std::reverse(ring.begin(),ring.end());
            std::rotate(ring.begin(),ring.begin()+variant%ring.size(),ring.end());
        }
        return profile;
    };
    enum class TailKind { Bezier, BSpline, Nurbs };
    for (const auto tail_kind : {TailKind::Bezier,TailKind::BSpline,TailKind::Nurbs})
        for (const bool spatial : {false,true}) for (const bool hole : {false,true})
            for (int variant = 0; variant < 2; ++variant) {
        axiom::Kernel kernel;
        const auto line = kernel.curves().make_line_segment({0,0,0},{4,0,0});
        const auto arc = kernel.curves().make_ellipse({4,3,0},{0,-3,0},{3,0,0});
        const axiom::Point3 arc_end {4+3*std::sin(1.0),3-3*std::cos(1.0),0};
        const auto arc_tangent = unit({3*std::cos(1.0),3*std::sin(1.0),0});
        std::vector<axiom::Point3> tail {
            arc_end,add(arc_end,arc_tangent,1.6),
            add(add(arc_end,arc_tangent,3.3),{0,0,1},spatial?0.5:0.0),
            add(add(arc_end,arc_tangent,5.2),{0,0,1},spatial?1.1:0.0)};
        axiom::Result<axiom::CurveId> tail_curve;
        if (tail_kind == TailKind::Bezier) tail_curve = kernel.curves().make_bezier(tail);
        else if (tail_kind == TailKind::BSpline) tail_curve = kernel.curves().make_bspline({tail,3,{}});
        else tail_curve = kernel.curves().make_nurbs({tail,{1,0.85,1.15,1},3,{}});
        if (!line.value || !arc.value || !tail_curve.value) return false;
        const std::array<axiom::CurveId,3> children {*line.value,*arc.value,*tail_curve.value};
        const auto rail = kernel.curves().make_composite_chain(children);
        if (!rail.value) return false;
        const auto profile = open_profile(hole,variant);
        const auto body = kernel.sweeps().sweep(profile,*rail.value);
        if (!body.value || body.status != axiom::StatusCode::Ok) {
            std::cerr << "composite chain sweep construction failed: tail=" << static_cast<int>(tail_kind)
                      << " spatial=" << spatial << " hole=" << hole << " variant=" << variant << '\n';
            return false;
        }
        const auto query = kernel.topology().query();
        const auto faces = query.faces_of_body(*body.value);
        const auto edges = query.edges_of_body(*body.value);
        const auto vertices = query.vertices_of_body(*body.value);
        const auto shells = query.shells_of_body(*body.value);
        const auto mass = kernel.query().mass_properties(*body.value);
        const std::size_t section_vertices = profile.polygon_xyz.size()+(hole?profile.holes_xyz.front().size():0);
        if (!vertices.value || vertices.value->empty() || vertices.value->size()%section_vertices != 0) return false;
        const std::size_t stations = vertices.value->size()/section_vertices;
        const std::size_t cap_triangles = 2*(section_vertices+2*profile.holes_xyz.size()-2);
        const std::size_t expected_faces = cap_triangles+2*section_vertices*(stations-1);
        if (stations < 45 || !faces.value || faces.value->size() != expected_faces || !edges.value ||
            edges.value->size() != 3*expected_faces/2 || !shells.value || shells.value->size() != 1 ||
            !mass.value || !(mass.value->volume>0) || !(mass.value->area>0) ||
            !std::isfinite(mass.value->centroid.x) ||
            !std::all_of(mass.value->inertia.begin(),mass.value->inertia.end(),
                         [](double value) { return std::isfinite(value); }) ||
            kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
            kernel.topology().validate().validate_body_topology_indices(*body.value).status != axiom::StatusCode::Ok) {
            std::cerr << "composite chain sweep topology/mass/Strict mismatch\n";
            return false;
        }
        double queried_area = 0.0;
        for (const auto face : *faces.value) {
            const auto area = query.planar_face_area(face);
            const auto owners = query.bodies_of_face(face);
            const auto loops = query.loops_of_face(face);
            if (!area.value || !(*area.value>0) || !owners.value || owners.value->size()!=1 ||
                owners.value->front().value != body.value->value || !loops.value || loops.value->size()!=1) return false;
            queried_area += *area.value;
        }
        for (const auto edge : *edges.value) {
            const auto uses = query.coedge_count_of_edge(edge);
            const auto adjacent = query.faces_of_edge(edge);
            if (!uses.value || *uses.value!=2 || !adjacent.value || adjacent.value->size()!=2) return false;
        }
        if (std::abs(queried_area-mass.value->area)>1e-7*std::max(1.0,mass.value->area)) return false;
        const auto bbox = query.bbox_of_body_from_topology(*body.value);
        const auto cached_bbox = kernel.representation().bbox_of_body(*body.value);
        if (!bbox.value || !cached_bbox.value || !bbox.value->is_valid || !cached_bbox.value->is_valid ||
            std::hypot(bbox.value->min.x-cached_bbox.value->min.x,bbox.value->min.y-cached_bbox.value->min.y,
                       bbox.value->min.z-cached_bbox.value->min.z)>1e-9 ||
            std::hypot(bbox.value->max.x-cached_bbox.value->max.x,bbox.value->max.y-cached_bbox.value->max.y,
                       bbox.value->max.z-cached_bbox.value->max.z)>1e-9) return false;
        const auto mesh = kernel.convert().brep_to_mesh(*body.value,{});
        const auto inspection = mesh.value ? kernel.convert().inspect_mesh(*mesh.value)
                                           : axiom::Result<axiom::MeshInspectionReport>{};
        if (!inspection.value || inspection.value->tessellation_strategy!="owned_topo_welded" ||
            inspection.value->vertex_count!=section_vertices*stations ||
            inspection.value->triangle_count!=expected_faces || inspection.value->connected_components!=1 ||
            inspection.value->has_degenerate_triangles || inspection.value->has_out_of_range_indices) return false;
    }

    // Four tangent-continuous cubic children form a periodic composite rail. The
    // duplicated final endpoint is removed by the sampler, so the owned shell has
    // walls only and no hidden cap pair at the chain seam.
    for (const bool hole : {false,true}) for (int variant = 0; variant < 4; ++variant) {
        axiom::Kernel kernel;
        constexpr double radius = 10.0;
        constexpr double handle = 5.522847498307936;
        const std::array<std::array<axiom::Point3,4>,4> poles {{
            {{{radius,0,0},{radius,handle,0},{handle,radius,0},{0,radius,0}}},
            {{{0,radius,0},{-handle,radius,0},{-radius,handle,0},{-radius,0,0}}},
            {{{-radius,0,0},{-radius,-handle,0},{-handle,-radius,0},{0,-radius,0}}},
            {{{0,-radius,0},{handle,-radius,0},{radius,-handle,0},{radius,0,0}}}
        }};
        std::array<axiom::CurveId,4> children {};
        for (std::size_t i = 0; i < poles.size(); ++i) {
            const auto child = kernel.curves().make_bezier(poles[i]);
            if (!child.value) return false;
            children[i] = *child.value;
        }
        const auto rail = kernel.curves().make_composite_chain(children);
        if (!rail.value) return false;
        axiom::ProfileRef profile {"closed_composite_section",
            {{radius-0.22,0,-0.16},{radius+0.22,0,-0.16},
             {radius+0.22,0,0.16},{radius-0.22,0,0.16}}};
        if (hole) profile = {"closed_composite_holed_section",
            {{radius-0.3,0,-0.24},{radius+0.3,0,-0.24},
             {radius+0.3,0,0.24},{radius-0.3,0,0.24}},
            {{{radius-0.1,0,-0.08},{radius+0.1,0,-0.08},
              {radius+0.1,0,0.08},{radius-0.1,0,0.08}}}};
        if (variant&1) std::reverse(profile.polygon_xyz.begin(),profile.polygon_xyz.end());
        std::rotate(profile.polygon_xyz.begin(),profile.polygon_xyz.begin()+variant%profile.polygon_xyz.size(),
                    profile.polygon_xyz.end());
        for (auto& ring : profile.holes_xyz) {
            if (variant&2) std::reverse(ring.begin(),ring.end());
            std::rotate(ring.begin(),ring.begin()+variant%ring.size(),ring.end());
        }
        const auto body = kernel.sweeps().sweep(profile,*rail.value);
        if (!body.value) return false;
        const auto vertices = kernel.topology().query().vertices_of_body(*body.value);
        const auto faces = kernel.topology().query().faces_of_body(*body.value);
        const auto shells = kernel.topology().query().shells_of_body(*body.value);
        const auto mass = kernel.query().mass_properties(*body.value);
        const std::size_t section_vertices = profile.polygon_xyz.size()+(hole?profile.holes_xyz.front().size():0);
        if (!vertices.value || vertices.value->size()%section_vertices!=0) return false;
        const std::size_t stations = vertices.value->size()/section_vertices;
        if (stations<64 || !faces.value || faces.value->size()!=2*section_vertices*stations || !shells.value ||
            shells.value->size()!=1+profile.holes_xyz.size() || !mass.value || !(mass.value->volume>0) ||
            kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok ||
            kernel.topology().validate().validate_body_topology_indices(*body.value).status!=axiom::StatusCode::Ok)
            return false;
    }

    axiom::Kernel kernel;
    const auto a = kernel.curves().make_line_segment({0,0,0},{4,0,0});
    const auto b = kernel.curves().make_bezier(
        std::array<axiom::Point3,4>{{{4,0,0},{6,0,0},{8,1,0},{10,2,0}}});
    if (!a.value || !b.value) return false;
    const auto good_chain = kernel.curves().make_composite_chain(std::array{*a.value,*b.value});
    if (!good_chain.value) return false;
    const auto good_profile = open_profile(true,0);
    const auto source = kernel.sweeps().sweep(good_profile,*good_chain.value);
    if (!source.value) return false;
    const auto gap_child = kernel.curves().make_line_segment({4.1,0,0},{8.1,0,0});
    const auto kink_child = kernel.curves().make_line_segment({4,0,0},{4,4,0});
    const auto cusp_child = kernel.curves().make_bezier(
        std::array<axiom::Point3,4>{{{4,0,0},{4,0,0},{7,1,0},{9,2,0}}});
    const auto parabola = kernel.curves().make_parabola({4,0,0},{1,0,0},{0,1,0},1);
    if (!gap_child.value || !kink_child.value || !cusp_child.value || !parabola.value) return false;
    const auto gap_chain = kernel.curves().make_composite_chain(std::array{*a.value,*gap_child.value});
    const auto kink_chain = kernel.curves().make_composite_chain(std::array{*a.value,*kink_child.value});
    const auto cusp_chain = kernel.curves().make_composite_chain(std::array{*a.value,*cusp_child.value});
    const auto unsupported_chain = kernel.curves().make_composite_chain(std::array{*a.value,*parabola.value});
    const auto nested_chain = good_chain.value ? kernel.curves().make_composite_chain(std::array{*good_chain.value})
                                               : axiom::Result<axiom::CurveId>{};
    if (!gap_chain.value || !kink_chain.value || !cusp_chain.value || !unsupported_chain.value ||
        !nested_chain.value) return false;
    auto transaction = kernel.topology().begin_transaction();
    const auto temporary = transaction.create_vertex({70,71,72});
    const auto objects = kernel.object_count_total(), geometry = kernel.geometry_count(), bodies = kernel.body_count();
    const auto next = kernel.next_object_id(), writes = transaction.write_operation_count();
    const auto runtime = kernel.runtime_store_counts();
    if (!temporary.value || !objects.value || !geometry.value || !bodies.value || !next.value || !writes.value ||
        !runtime.value) return false;
    const auto rejected = [&](const axiom::ProfileRef& profile, axiom::CurveId rail) {
        const auto result = kernel.sweeps().sweep(profile,rail);
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto after = kernel.runtime_store_counts();
        return result.status==axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,axiom::diag_codes::kCoreParameterOutOfRange) &&
            kernel.object_count_total().value==objects.value && kernel.geometry_count().value==geometry.value &&
            kernel.body_count().value==bodies.value && kernel.next_object_id().value==next.value &&
            transaction.write_operation_count().value==writes.value && after.value &&
            after.value->mesh_records==runtime.value->mesh_records &&
            after.value->tessellation_cache_entries==runtime.value->tessellation_cache_entries &&
            after.value->curve_eval_cache_entries==runtime.value->curve_eval_cache_entries &&
            after.value->surface_eval_cache_entries==runtime.value->surface_eval_cache_entries;
    };
    auto tilted = good_profile;
    for (auto& p : tilted.polygon_xyz) p.x += 0.2*p.y;
    for (auto& ring : tilted.holes_xyz) for (auto& p : ring) p.x += 0.2*p.y;
    auto oversized = good_profile;
    for (auto& p : oversized.polygon_xyz) { p.y*=40; p.z*=40; }
    for (auto& ring : oversized.holes_xyz) for (auto& p : ring) { p.y*=40; p.z*=40; }
    if (!rejected(good_profile,*gap_chain.value) || !rejected(good_profile,*kink_chain.value) ||
        !rejected(good_profile,*cusp_chain.value) || !rejected(good_profile,*unsupported_chain.value) ||
        !rejected(good_profile,*nested_chain.value) || !rejected(tilted,*good_chain.value) ||
        !rejected(oversized,*good_chain.value)) return false;
    if (transaction.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto remains = kernel.topology().query().has_vertex(*temporary.value);
    const auto after_rollback = kernel.object_count_total();
    if (!remains.value || *remains.value || !after_rollback.value || *after_rollback.value+1!=*objects.value)
        return false;
    const auto retry = kernel.sweeps().sweep(good_profile,*good_chain.value);
    return retry.value &&
        kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok &&
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok;
}

bool test_scaled_rail_sweeps() {
    const auto check_owned_body = [](axiom::Kernel& kernel, axiom::BodyId body,
                                     std::size_t section_vertices, std::size_t stations,
                                     std::size_t hole_count) {
        const auto query=kernel.topology().query();
        const auto faces=query.faces_of_body(body);
        const auto edges=query.edges_of_body(body);
        const auto vertices=query.vertices_of_body(body);
        const auto shells=query.shells_of_body(body);
        const auto mass=kernel.query().mass_properties(body);
        const auto bbox=query.bbox_of_body_from_topology(body);
        const auto cached_bbox=kernel.representation().bbox_of_body(body);
        const std::size_t cap_triangles=section_vertices+2*hole_count-2;
        const std::size_t expected_faces=2*cap_triangles+2*section_vertices*(stations-1);
        if (!faces.value || faces.value->size()!=expected_faces || !edges.value ||
            edges.value->size()!=3*expected_faces/2 || !vertices.value ||
            vertices.value->size()!=section_vertices*stations || !shells.value || shells.value->size()!=1 ||
            !mass.value || !(mass.value->volume>0) || !(mass.value->area>0) ||
            !std::isfinite(mass.value->centroid.x) || !std::isfinite(mass.value->centroid.y) ||
            !std::isfinite(mass.value->centroid.z) || mass.value->inertia[0]<=0 ||
            mass.value->inertia[4]<=0 || mass.value->inertia[8]<=0 || !bbox.value ||
            !cached_bbox.value || !bbox.value->is_valid || !cached_bbox.value->is_valid ||
            std::hypot(bbox.value->min.x-cached_bbox.value->min.x,
                       bbox.value->min.y-cached_bbox.value->min.y,
                       bbox.value->min.z-cached_bbox.value->min.z)>1e-9 ||
            std::hypot(bbox.value->max.x-cached_bbox.value->max.x,
                       bbox.value->max.y-cached_bbox.value->max.y,
                       bbox.value->max.z-cached_bbox.value->max.z)>1e-9 ||
            kernel.validate().validate_all(body,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok ||
            kernel.topology().validate().validate_indices_consistency().status!=axiom::StatusCode::Ok ||
            kernel.topology().validate().validate_body_topology_indices(body).status!=axiom::StatusCode::Ok)
            return false;
        double queried_area=0.0;
        for (const auto face:*faces.value) {
            const auto area=query.planar_face_area(face);
            const auto owners=query.bodies_of_face(face);
            if (!area.value || !(*area.value>0) || !owners.value || owners.value->size()!=1 ||
                owners.value->front().value!=body.value) return false;
            queried_area+=*area.value;
        }
        if (std::abs(queried_area-mass.value->area)>1e-8*std::max(1.0,mass.value->area)) return false;
        for (const auto edge:*edges.value) {
            const auto uses=query.coedge_count_of_edge(edge);
            const auto neighbors=query.faces_of_edge(edge);
            const auto length=query.edge_length(edge);
            if (!uses.value || *uses.value!=2 || !neighbors.value || neighbors.value->size()!=2 ||
                !length.value || !(*length.value>0)) return false;
        }
        const auto mesh=kernel.convert().brep_to_mesh(body,{});
        const auto inspection=mesh.value?kernel.convert().inspect_mesh(*mesh.value):
                                           axiom::Result<axiom::MeshInspectionReport>{};
        return inspection.value && inspection.value->tessellation_strategy=="owned_topo_welded" &&
            inspection.value->vertex_count==section_vertices*stations &&
            inspection.value->triangle_count==expected_faces && inspection.value->connected_components==1 &&
            !inspection.value->has_degenerate_triangles && !inspection.value->has_out_of_range_indices;
    };

    // Fixed-orientation line/polyline rails scale about the rail start in the
    // section plane. Every bend remains represented by an owned vertex ring.
    const std::array<std::vector<axiom::Point3>,2> linear_paths {
        std::vector<axiom::Point3>{{0,0,0},{0,0,6}},
        std::vector<axiom::Point3>{{0,0,0},{0.15,-0.1,2},{-0.1,0.2,4},{0.2,0.1,7}}
    };
    for (const bool hole:{false,true}) for (const double end_scale:{0.7,1.35})
        for (int variant=0;variant<4;++variant) for (const auto& path:linear_paths) {
        axiom::Kernel kernel;
        axiom::ProfileRef profile {"scaled_linear",
            {{-0.6,-0.45,0},{0.6,-0.45,0},{0.6,0.45,0},{-0.6,0.45,0}}};
        if (hole) profile.holes_xyz={{{-0.2,-0.14,0},{0.2,-0.14,0},{0.2,0.14,0},{-0.2,0.14,0}}};
        if (variant&1) std::reverse(profile.polygon_xyz.begin(),profile.polygon_xyz.end());
        std::rotate(profile.polygon_xyz.begin(),profile.polygon_xyz.begin()+variant%profile.polygon_xyz.size(),
                    profile.polygon_xyz.end());
        for (auto& ring:profile.holes_xyz) {
            if (variant&2) std::reverse(ring.begin(),ring.end());
            std::rotate(ring.begin(),ring.begin()+variant%ring.size(),ring.end());
        }
        const auto rail=path.size()==2?kernel.curves().make_line_segment(path.front(),path.back()):
                                       kernel.curves().make_composite_polyline(path);
        if (!rail.value) return false;
        const auto body=kernel.sweeps().sweep_scaled(profile,*rail.value,end_scale);
        const std::size_t section_vertices=4+(hole?4:0);
        if (!body.value || body.status!=axiom::StatusCode::Ok ||
            !check_owned_body(kernel,*body.value,section_vertices,path.size(),hole?1:0)) {
            std::cerr<<"scaled line/polyline sweep failed\n";
            return false;
        }
        std::vector<double> distance(path.size(),0.0);
        for (std::size_t i=1;i<path.size();++i)
            distance[i]=distance[i-1]+std::hypot(path[i].x-path[i-1].x,
                                                  path[i].y-path[i-1].y,
                                                  path[i].z-path[i-1].z);
        axiom::BoundingBox expected {};
        const auto extend=[&](const axiom::Point3& p) {
            if (!expected.is_valid) expected={p,p,true};
            expected.min.x=std::min(expected.min.x,p.x); expected.max.x=std::max(expected.max.x,p.x);
            expected.min.y=std::min(expected.min.y,p.y); expected.max.y=std::max(expected.max.y,p.y);
            expected.min.z=std::min(expected.min.z,p.z); expected.max.z=std::max(expected.max.z,p.z);
        };
        auto rings=profile.holes_xyz;
        rings.insert(rings.begin(),profile.polygon_xyz);
        for (std::size_t station=0;station<path.size();++station) {
            const double scale=1.0+(end_scale-1.0)*distance[station]/distance.back();
            for (const auto& ring:rings) for (const auto& p:ring)
                extend({path[station].x+scale*p.x,path[station].y+scale*p.y,path[station].z});
        }
        const auto bbox=kernel.representation().bbox_of_body(*body.value);
        if (!bbox.value || std::hypot(bbox.value->min.x-expected.min.x,bbox.value->min.y-expected.min.y,
                                      bbox.value->min.z-expected.min.z)>1e-10 ||
            std::hypot(bbox.value->max.x-expected.max.x,bbox.value->max.y-expected.max.y,
                       bbox.value->max.z-expected.max.z)>1e-10) return false;
    }

    // Open polynomial and mixed composite rails use their sampled arc length and
    // rotation-minimizing frames. Both growth and shrinkage preserve holes.
    enum class RailKind { Bezier, BSpline, Nurbs, Composite };
    for (const auto kind:{RailKind::Bezier,RailKind::BSpline,RailKind::Nurbs,RailKind::Composite})
        for (const bool hole:{false,true}) for (const double end_scale:{0.75,1.25}) {
        axiom::Kernel kernel;
        axiom::Result<axiom::CurveId> rail;
        if (kind==RailKind::Bezier) {
            rail=kernel.curves().make_bezier(
                std::array<axiom::Point3,4>{{{0,0,0},{4,0,0},{8,0.8,0.3},{12,2,0.8}}});
        } else if (kind==RailKind::BSpline) {
            rail=kernel.curves().make_bspline(
                {{{0,0,0},{3,0,0},{6,0.4,0.1},{9,1.2,0.4},{12,2,0.8}},3,{}});
        } else if (kind==RailKind::Nurbs) {
            rail=kernel.curves().make_nurbs(
                {{{0,0,0},{3,0,0},{6,0.5,0.15},{9,1.3,0.45},{12,2,0.8}},
                 {1,0.9,1.1,0.95,1},3,{}});
        } else {
            const auto line=kernel.curves().make_line_segment({0,0,0},{4,0,0});
            const auto tail=kernel.curves().make_bezier(
                std::array<axiom::Point3,4>{{{4,0,0},{6,0,0},{9,0.8,0.25},{12,2,0.8}}});
            if (!line.value || !tail.value) return false;
            rail=kernel.curves().make_composite_chain(std::array{*line.value,*tail.value});
        }
        if (!rail.value) return false;
        axiom::ProfileRef profile {"scaled_curve",
            {{0,-0.24,-0.18},{0,0.24,-0.18},{0,0.24,0.18},{0,-0.24,0.18}}};
        if (hole) profile={"scaled_curve_hole",
            {{0,-0.3,-0.23},{0,0.3,-0.23},{0,0.3,0.23},{0,-0.3,0.23}},
            {{{0,-0.1,-0.07},{0,0.1,-0.07},{0,0.1,0.07},{0,-0.1,0.07}}}};
        const auto body=kernel.sweeps().sweep_scaled(profile,*rail.value,end_scale);
        if (!body.value) {
            std::cerr<<"scaled curve-frame sweep failed\n";
            return false;
        }
        const auto vertices=kernel.topology().query().vertices_of_body(*body.value);
        const std::size_t section_vertices=4+(hole?4:0);
        if (!vertices.value || vertices.value->size()%section_vertices!=0 ||
            vertices.value->size()/section_vertices<16 ||
            !check_owned_body(kernel,*body.value,section_vertices,
                              vertices.value->size()/section_vertices,hole?1:0)) return false;
    }

    // Invalid laws and inconsistent rail placement fail before any body/topology,
    // ID, mesh/cache or active-transaction mutation. A rollback and retry remain valid.
    axiom::Kernel kernel;
    const auto good_rail=kernel.curves().make_bezier(
        std::array<axiom::Point3,4>{{{0,0,0},{4,0,0},{8,0.7,0.2},{12,1.8,0.7}}});
    const auto closed_rail=kernel.curves().make_circle({0,0,0},{0,0,1},8);
    const auto off_plane=kernel.curves().make_line_segment({1,0,0},{7,0,0});
    const axiom::ProfileRef profile {"scaled_transaction",
        {{0,-0.3,-0.2},{0,0.3,-0.2},{0,0.3,0.2},{0,-0.3,0.2}},
        {{{0,-0.1,-0.07},{0,0.1,-0.07},{0,0.1,0.07},{0,-0.1,0.07}}}};
    const axiom::ProfileRef closed_profile {"scaled_closed",
        {{7.7,0,-0.2},{8.3,0,-0.2},{8.3,0,0.2},{7.7,0,0.2}}};
    if (!good_rail.value || !closed_rail.value || !off_plane.value) return false;
    const auto source=kernel.sweeps().sweep_scaled(profile,*good_rail.value,1.2);
    const auto unit_scaled=kernel.sweeps().sweep_scaled(profile,*good_rail.value,1.0);
    const auto compatible=kernel.sweeps().sweep(profile,*good_rail.value);
    const auto source_mass=source.value?kernel.query().mass_properties(*source.value):
                                         axiom::Result<axiom::MassProperties>{};
    const auto source_faces=source.value?kernel.topology().query().faces_of_body(*source.value):
                                           axiom::Result<std::vector<axiom::FaceId>>{};
    const auto unit_mass=unit_scaled.value?kernel.query().mass_properties(*unit_scaled.value):
                                            axiom::Result<axiom::MassProperties>{};
    const auto compatible_mass=compatible.value?kernel.query().mass_properties(*compatible.value):
                                               axiom::Result<axiom::MassProperties>{};
    if (!source.value || !source_mass.value || !source_faces.value || source_faces.value->empty() ||
        !unit_scaled.value || !compatible.value || !unit_mass.value || !compatible_mass.value ||
        std::abs(unit_mass.value->volume-compatible_mass.value->volume)>1e-10 ||
        std::abs(unit_mass.value->area-compatible_mass.value->area)>1e-10 ||
        unit_mass.value->inertia!=compatible_mass.value->inertia) return false;
    auto transaction=kernel.topology().begin_transaction();
    const auto temporary=transaction.create_vertex({84,85,86});
    const auto objects=kernel.object_count_total(),geometry=kernel.geometry_count();
    const auto bodies=kernel.body_count(),next=kernel.next_object_id();
    const auto writes=transaction.write_operation_count();
    const auto runtime=kernel.runtime_store_counts();
    if (!temporary.value || !objects.value || !geometry.value || !bodies.value || !next.value || !writes.value ||
        !runtime.value) return false;
    const auto rejected=[&](const axiom::ProfileRef& input,axiom::CurveId rail,double scale,
                            std::string_view code=axiom::diag_codes::kCoreParameterOutOfRange) {
        const auto result=kernel.sweeps().sweep_scaled(input,rail,scale);
        const auto diagnostic=kernel.diagnostics().get(result.diagnostic_id);
        const auto after=kernel.runtime_store_counts();
        return result.status==axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,code) && kernel.object_count_total().value==objects.value &&
            kernel.geometry_count().value==geometry.value && kernel.body_count().value==bodies.value &&
            kernel.next_object_id().value==next.value && transaction.write_operation_count().value==writes.value &&
            after.value && after.value->mesh_records==runtime.value->mesh_records &&
            after.value->tessellation_cache_entries==runtime.value->tessellation_cache_entries &&
            after.value->face_tessellation_cache_entries==runtime.value->face_tessellation_cache_entries;
    };
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double inf=std::numeric_limits<double>::infinity();
    auto nonplanar=profile;
    nonplanar.polygon_xyz.back().x=0.1;
    if (!rejected(profile,*good_rail.value,0) || !rejected(profile,*good_rail.value,-0.5) ||
        !rejected(profile,*good_rail.value,nan) || !rejected(profile,*good_rail.value,inf) ||
        !rejected(profile,*closed_rail.value,1.1) || !rejected(profile,*off_plane.value,1.2) ||
        !rejected(nonplanar,*good_rail.value,1.2) ||
        !rejected(profile,{},1.2,axiom::diag_codes::kCoreInvalidHandle) ||
        !rejected({"implicit",{}},*good_rail.value,1.2) ||
        !rejected(closed_profile,*closed_rail.value,0.9)) return false;
    if (transaction.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto remains=kernel.topology().query().has_vertex(*temporary.value);
    if (!remains.value || *remains.value) return false;
    auto edit=kernel.topology().begin_transaction();
    if (edit.delete_face(source_faces.value->front()).status!=axiom::StatusCode::Ok ||
        edit.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto restored=kernel.query().mass_properties(*source.value);
    if (!restored.value || std::abs(restored.value->volume-source_mass.value->volume)>1e-10 ||
        std::abs(restored.value->area-source_mass.value->area)>1e-10 ||
        restored.value->inertia!=source_mass.value->inertia ||
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok)
        return false;
    const auto retry=kernel.sweeps().sweep_scaled(profile,*good_rail.value,0.8);
    return retry.value &&
        kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok;
}

bool check_sweep_law_body(axiom::Kernel& kernel, axiom::BodyId body, std::size_t ring_vertices,
                          std::size_t holes, bool closed) {
    const auto query = kernel.topology().query();
    const auto vertices = query.vertices_of_body(body);
    const auto faces = query.faces_of_body(body);
    const auto edges = query.edges_of_body(body);
    const auto shells = query.shells_of_body(body);
    if (!vertices.value || vertices.value->size()%ring_vertices != 0 || !faces.value || !edges.value ||
        !shells.value || shells.value->size() != (closed ? holes+1 : 1)) return false;
    const std::size_t stations = vertices.value->size()/ring_vertices;
    const std::size_t expected_faces = closed ? 2*ring_vertices*stations :
        2*(ring_vertices+2*holes-2)+2*ring_vertices*(stations-1);
    const auto mass = kernel.query().mass_properties(body);
    const auto actual = query.body_mass_properties(body);
    const auto bounds = query.bbox_of_body_from_topology(body);
    const auto cached = kernel.representation().bbox_of_body(body);
    if (stations < 3 || faces.value->size() != expected_faces || edges.value->size() != 3*expected_faces/2 ||
        !mass.value || !actual.value || !(mass.value->volume > 0) || !(mass.value->area > 0) ||
        std::abs(actual.value->volume-mass.value->volume) > 1e-8 ||
        std::abs(actual.value->area-mass.value->area) > 1e-8 || !bounds.value || !cached.value ||
        !bounds.value->is_valid || !cached.value->is_valid ||
        std::hypot(bounds.value->min.x-cached.value->min.x,bounds.value->min.y-cached.value->min.y,
                   bounds.value->min.z-cached.value->min.z) > 1e-9 ||
        std::hypot(bounds.value->max.x-cached.value->max.x,bounds.value->max.y-cached.value->max.y,
                   bounds.value->max.z-cached.value->max.z) > 1e-9 ||
        kernel.validate().validate_all(body,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
        kernel.topology().validate().validate_body_topology_indices(body).status != axiom::StatusCode::Ok)
        return false;
    for (std::size_t i = 0; i < 9; ++i)
        if (!std::isfinite(mass.value->inertia[i]) || std::abs(actual.value->inertia[i]-mass.value->inertia[i]) > 1e-7)
            return false;
    double area = 0;
    for (const auto face : *faces.value) {
        const auto owners = query.bodies_of_face(face);
        const auto loops = query.loops_of_face(face);
        const auto face_area = query.planar_face_area(face);
        if (!owners.value || owners.value->size() != 1 || owners.value->front().value != body.value ||
            !loops.value || loops.value->size() != 1 || !face_area.value || !(*face_area.value > 0)) return false;
        area += *face_area.value;
    }
    if (std::abs(area-mass.value->area) > 1e-8) return false;
    for (const auto edge : *edges.value) {
        const auto uses = query.coedge_count_of_edge(edge);
        const auto adjacent = query.faces_of_edge(edge);
        const auto length = query.edge_length(edge);
        if (!uses.value || *uses.value != 2 || !adjacent.value || adjacent.value->size() != 2 ||
            !length.value || !(*length.value > 0)) return false;
    }
    const auto mesh = kernel.convert().brep_to_mesh(body,{});
    if (!mesh.value) return false;
    const auto inspection = kernel.convert().inspect_mesh(*mesh.value);
    return inspection.value && inspection.value->tessellation_strategy == "owned_topo_welded" &&
        inspection.value->vertex_count == vertices.value->size() &&
        inspection.value->triangle_count == faces.value->size() &&
        inspection.value->connected_components == (closed ? holes+1 : 1) &&
        !inspection.value->has_degenerate_triangles && !inspection.value->has_out_of_range_indices;
}

bool test_sweep_scale_laws() {
    using Station = axiom::SweepScaleStation;
    const std::vector<Station> law {{0,1},{0.23,1.5},{0.42,1.5},{0.73,0.65},{1,1.1}};
    // An independent three-point Gauss integral over signed rectangles checks
    // volume, centroid and all nine inertia entries. Both the path corners and
    // caller law keys partition the integral; rail parameter spacing is irrelevant.
    for (const int shape : {0,1,2}) for (const bool transformed : {false,true}) {
        axiom::Kernel kernel;
        axiom::ProfileRef profile {"scale_law_box",{{-1,-0.75,0},{1,-0.75,0},{1,0.75,0},{-1,0.75,0}}};
        std::vector<std::array<double,5>> rectangles {{-1,-0.75,2,1.5,1}};
        if (shape == 1) {
            profile.holes_xyz = {{{-0.7,-0.3,0},{-0.3,-0.3,0},{-0.3,0.3,0},{-0.7,0.3,0}},
                                 {{0.3,-0.3,0},{0.7,-0.3,0},{0.7,0.3,0},{0.3,0.3,0}}};
            rectangles.push_back({-0.7,-0.3,0.4,0.6,-1});
            rectangles.push_back({0.3,-0.3,0.4,0.6,-1});
        } else if (shape == 2) {
            profile = {"scale_law_concave",{{-1,-1,0},{1,-1,0},{1,0,0},{0,0,0},{0,1,0},{-1,1,0}}};
            rectangles = {{-1,-1,2,1,1},{-1,0,1,1,1}};
        }
        const std::vector<axiom::Point3> path = shape == 0 ?
            std::vector<axiom::Point3>{{0,0,0},{0,0,8}} :
            std::vector<axiom::Point3>{{0,0,0},{0.2,-0.1,1.6},{0.6,0.3,5},{0.3,0.1,8}};
        const auto world = [transformed](axiom::Point3 p) -> axiom::Point3 {
            if (!transformed) return p;
            p.z = -p.z;
            return {10+0.6*p.x-0.48*p.y+0.64*p.z,
                    -20+0.8*p.x+0.36*p.y-0.48*p.z,30+0.8*p.y+0.6*p.z};
        };
        std::vector<double> lengths(path.size(),0);
        for (std::size_t i = 1; i < path.size(); ++i)
            lengths[i] = lengths[i-1]+std::hypot(path[i].x-path[i-1].x,path[i].y-path[i-1].y,path[i].z-path[i-1].z);
        std::vector<double> cuts;
        for (const auto length : lengths) cuts.push_back(length/lengths.back());
        for (const auto& key : law) cuts.push_back(key.fraction);
        std::sort(cuts.begin(),cuts.end());
        cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
        const double node = std::sqrt(15.0)/10;
        const std::array<std::array<double,2>,3> gauss {{{0.5-node,5.0/18},{0.5,4.0/9},{0.5+node,5.0/18}}};
        double volume = 0;
        std::array<double,3> first {};
        std::array<double,9> second {};
        for (std::size_t cut = 1; cut < cuts.size(); ++cut) {
            const double midpoint = (cuts[cut-1]+cuts[cut])*0.5;
            std::size_t segment = 1, key = 1;
            while (segment+1 < path.size() && lengths[segment]/lengths.back() < midpoint) ++segment;
            while (key+1 < law.size() && law[key].fraction < midpoint) ++key;
            const double dz_df = (path[segment].z-path[segment-1].z)*lengths.back()/
                                  (lengths[segment]-lengths[segment-1]);
            for (const auto& r : rectangles) for (const auto& gx : gauss)
            for (const auto& gy : gauss) for (const auto& gt : gauss) {
                const double f = cuts[cut-1]+gt[0]*(cuts[cut]-cuts[cut-1]);
                const double t = (f*lengths.back()-lengths[segment-1])/(lengths[segment]-lengths[segment-1]);
                const double s = law[key-1].scale+(law[key].scale-law[key-1].scale)*
                    (f-law[key-1].fraction)/(law[key].fraction-law[key-1].fraction);
                const auto& a = path[segment-1]; const auto& b = path[segment];
                const auto p = world({a.x+t*(b.x-a.x)+s*(r[0]+r[2]*gx[0]),
                                       a.y+t*(b.y-a.y)+s*(r[1]+r[3]*gy[0]),a.z+t*(b.z-a.z)});
                const double weight = r[2]*r[3]*r[4]*dz_df*(cuts[cut]-cuts[cut-1])*s*s*gx[1]*gy[1]*gt[1];
                const std::array<double,3> q {p.x,p.y,p.z};
                volume += weight;
                for (int i = 0; i < 3; ++i) {
                    first[i] += weight*q[i];
                    for (int j = 0; j < 3; ++j) second[3*i+j] += weight*q[i]*q[j];
                }
            }
        }
        std::size_t ring_vertices = profile.polygon_xyz.size();
        for (auto& ring : profile.holes_xyz) {
            ring_vertices += ring.size();
            if (transformed) std::reverse(ring.begin(),ring.end());
            for (auto& p : ring) p = world(p);
        }
        if (transformed) std::reverse(profile.polygon_xyz.begin(),profile.polygon_xyz.end());
        for (auto& p : profile.polygon_xyz) p = world(p);
        auto world_path = path;
        for (auto& p : world_path) p = world(p);
        const auto rail = path.size() == 2 ? kernel.curves().make_line_segment(world_path.front(),world_path.back()) :
                                            kernel.curves().make_composite_polyline(world_path);
        if (!rail.value) return false;
        const auto body = kernel.sweeps().sweep_with_scale_law(profile,*rail.value,law);
        if (!body.value || !check_sweep_law_body(kernel,*body.value,ring_vertices,profile.holes_xyz.size(),false)) {
            std::cerr << "piecewise arc-length scale sweep failed: " << shape << ", transformed=" << transformed << '\n';
            return false;
        }
        const auto mass = kernel.query().mass_properties(*body.value);
        if (!mass.value || std::abs(mass.value->volume-volume) > 1e-8) return false;
        for (auto& value : first) value /= volume;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            second[3*i+j] = second[3*i+j]/volume-first[i]*first[j];
        const double trace = second[0]+second[4]+second[8];
        if (std::hypot(mass.value->centroid.x-first[0],mass.value->centroid.y-first[1],mass.value->centroid.z-first[2]) > 1e-8)
            return false;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            if (std::abs(mass.value->inertia[3*i+j]-volume*((i == j ? trace : 0)-second[3*i+j])) > 1e-6) return false;
        // A peak at an interior key must survive even when both endpoint scales
        // are smaller. The line case has an exact independently known bound.
        if (shape == 0 && !transformed) {
            const auto bounds = kernel.topology().query().bbox_of_body_from_topology(*body.value);
            if (!bounds.value || std::abs(bounds.value->min.x+1.5) > 1e-10 ||
                std::abs(bounds.value->max.x-1.5) > 1e-10 || std::abs(bounds.value->max.z-8) > 1e-10) return false;
            const auto edges = kernel.topology().query().edges_of_body(*body.value);
            if (!edges.value) return false;
            // Every key has a complete ring of actual planar boundary edges.
            for (const auto& station : law) {
                std::size_t at_height = 0;
                for (const auto edge : *edges.value) {
                    const auto neighbors = kernel.topology().query().faces_of_edge(edge);
                    if (!neighbors.value) return false;
                    for (const auto face : *neighbors.value) {
                        const auto box = kernel.topology().query().bbox_of_face(face);
                        if (!box.value) return false;
                        if (std::abs(box.value->min.z-8*station.fraction) < 1e-10 ||
                            std::abs(box.value->max.z-8*station.fraction) < 1e-10) { ++at_height; break; }
                    }
                }
                if (at_height < ring_vertices) return false;
            }
        }
    }

    // Open spline/composite and periodic circular/elliptical rails use the same
    // law while retaining their actual transported stations and seam topology.
    for (int kind = 0; kind < 6; ++kind) {
        axiom::Kernel kernel;
        axiom::Result<axiom::CurveId> rail;
        if (kind == 0) rail = kernel.curves().make_bezier(std::array<axiom::Point3,4>{{{0,0,0},{4,0,0},{8,0.7,0.3},{12,2,0.8}}});
        if (kind == 1) rail = kernel.curves().make_bspline({{{0,0,0},{3,0,0},{6,0.4,0.1},{9,1.1,0.4},{12,2,0.8}},3,{}});
        if (kind == 2) rail = kernel.curves().make_nurbs({{{0,0,0},{3,0,0},{6,0.4,0.1},{9,1.1,0.4},{12,2,0.8}},
                                                         {1,0.9,1.1,0.95,1},3,{}});
        if (kind == 3) {
            const auto line = kernel.curves().make_line_segment({0,0,0},{4,0,0});
            const auto tail = kernel.curves().make_bezier(std::array<axiom::Point3,4>{{{4,0,0},{6,0,0},{9,0.7,0.3},{12,2,0.8}}});
            if (!line.value || !tail.value) return false;
            rail = kernel.curves().make_composite_chain(std::array{*line.value,*tail.value});
        }
        if (kind == 4) rail = kernel.curves().make_circle({0,0,0},{0,0,1},12);
        if (kind == 5) rail = kernel.curves().make_ellipse({0,0,0},{12,0,0},{0,9,0});
        if (!rail.value) return false;
        const auto domain = kernel.curve_service().domain(*rail.value);
        const auto start = domain.value ? kernel.curve_service().eval(*rail.value,domain.value->min,1) :
                                          axiom::Result<axiom::CurveEvalResult>{};
        if (!start.value) return false;
        const auto& origin = start.value->point;
        const bool closed = kind >= 4;
        axiom::ProfileRef profile {"scale_law_curve",{{0,-0.2,-0.15},{0,0.2,-0.15},{0,0.2,0.15},{0,-0.2,0.15}}};
        if (kind == 2) profile = {"scale_law_curve_concave",
            {{0,-0.2,-0.15},{0,0.2,-0.15},{0,0.2,0},{0,0,0},{0,0,0.15},{0,-0.2,0.15}}};
        if (kind == 3) profile.holes_xyz = {{{0,-0.07,-0.05},{0,0.07,-0.05},{0,0.07,0.05},{0,-0.07,0.05}}};
        if (closed) {
            const double tangent_length = std::hypot(start.value->tangent.x,start.value->tangent.y);
            if (!(tangent_length > 0)) return false;
            const double ux = start.value->tangent.y/tangent_length, uy = -start.value->tangent.x/tangent_length;
            const auto point = [&](double x, double y) -> axiom::Point3 {
                return {origin.x+x*ux,origin.y+x*uy,origin.z+y};
            };
            profile = {"scale_law_periodic",{point(-0.2,-0.15),point(0.2,-0.15),point(0.2,0.15),point(-0.2,0.15)},
                       {{point(-0.07,-0.05),point(0.07,-0.05),point(0.07,0.05),point(-0.07,0.05)}}};
        }
        auto keys = law;
        if (closed) keys.back().scale = 1;
        const auto body = kernel.sweeps().sweep_with_scale_law(profile,*rail.value,keys);
        std::size_t ring_vertices = profile.polygon_xyz.size();
        for (const auto& hole : profile.holes_xyz) ring_vertices += hole.size();
        if (!body.value || !check_sweep_law_body(kernel,*body.value,ring_vertices,profile.holes_xyz.size(),closed)) {
            std::cerr << "curve/periodic scale law sweep failed: " << kind << '\n';
            return false;
        }
    }

    // Active transaction failures must preserve model IDs, topology writes,
    // mesh/evaluation caches, and a previously materialized source body.
    axiom::Kernel kernel;
    const axiom::ProfileRef profile {"scale_law_transaction",{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    const auto rail = kernel.curves().make_line_segment({0,0,0},{0,0,8});
    const auto reversed = kernel.curves().make_composite_polyline(std::array<axiom::Point3,3>{{{0,0,0},{0,0,5},{0,0,3}}});
    const auto off_plane = kernel.curves().make_line_segment({0,0,1},{0,0,9});
    const auto circle = kernel.curves().make_circle({0,0,0},{0,0,1},8);
    const auto budget_rail = kernel.curves().make_composite_polyline(
        std::array<axiom::Point3,3>{{{0,0,0},{0.2,0,1.6},{0.3,0.1,8}}});
    if (!rail.value || !reversed.value || !off_plane.value || !circle.value || !budget_rail.value) return false;
    const auto source = kernel.sweeps().sweep_with_scale_law(profile,*rail.value,law);
    if (!source.value || !check_sweep_law_body(kernel,*source.value,4,0,false)) return false;
    const auto source_mass = kernel.query().mass_properties(*source.value);
    const auto source_faces = kernel.topology().query().faces_of_body(*source.value);
    if (!source_mass.value || !source_faces.value || source_faces.value->empty()) return false;
    auto transaction = kernel.topology().begin_transaction();
    const auto temporary = transaction.create_vertex({111,112,113});
    const auto objects = kernel.object_count_total(), geometry = kernel.geometry_count(), bodies = kernel.body_count();
    const auto next = kernel.next_object_id(), writes = transaction.write_operation_count();
    const auto runtime = kernel.runtime_store_counts();
    if (!temporary.value || !objects.value || !geometry.value || !bodies.value || !next.value || !writes.value || !runtime.value)
        return false;
    const auto rejected = [&](const axiom::ProfileRef& input, axiom::CurveId path, const std::vector<Station>& keys,
                              std::string_view code = axiom::diag_codes::kCoreParameterOutOfRange,
                              std::string_view stage = {}) {
        const auto result = kernel.sweeps().sweep_with_scale_law(input,path,keys);
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto after = kernel.runtime_store_counts();
        if (!stage.empty()) {
            if (!diagnostic.value || diagnostic.value->issues.size() != 1 ||
                diagnostic.value->issues.front().stage != stage) return false;
            const auto& evidence = diagnostic.value->issues.front().numeric_evidence;
            const auto count = std::find_if(evidence.begin(),evidence.end(),[](const auto& item) {
                return item.name == "law_key_count";
            });
            if (count == evidence.end() || count->value != static_cast<double>(keys.size())) return false;
        }
        return result.status == axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,code) && kernel.object_count_total().value == objects.value &&
            kernel.geometry_count().value == geometry.value && kernel.body_count().value == bodies.value &&
            kernel.next_object_id().value == next.value && transaction.write_operation_count().value == writes.value &&
            after.value && after.value->mesh_records == runtime.value->mesh_records &&
            after.value->tessellation_cache_entries == runtime.value->tessellation_cache_entries &&
            after.value->face_tessellation_cache_entries == runtime.value->face_tessellation_cache_entries &&
            after.value->curve_eval_cache_entries == runtime.value->curve_eval_cache_entries &&
            after.value->surface_eval_cache_entries == runtime.value->surface_eval_cache_entries;
    };
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    const std::vector<std::vector<Station>> invalid {
        {},{{0,1}},{{0.1,1},{1,1}},{{0,2},{1,1}},{{0,1},{0.9,1}},{{0,1},{0.5,1},{0.5,2},{1,1}},
        {{0,1},{0.7,1},{0.4,1},{1,1}},{{0,1},{0.5,0},{1,1}},{{0,1},{0.5,-1},{1,1}},
        {{0,1},{0.5,nan},{1,1}},{{0,1},{0.5,inf},{1,1}},{{0,1},{nan,1},{1,1}},
        {{0,1},{inf,1},{1,1}},{{0,1},{0.5,2000},{1,1}},{{0,1},{0.5,1e-200},{1,1}},
        {{0,1},{1e-18,1},{1,1}},{{0,1},{0.5,1},{std::nextafter(0.5,1.0),1},{1,1}}
    };
    for (const auto& keys : invalid) if (!rejected(profile,*rail.value,keys)) return false;
    std::vector<Station> too_many(4098);
    for (std::size_t i = 0; i < too_many.size(); ++i) too_many[i] = {static_cast<double>(i)/(too_many.size()-1),1};
    auto nonplanar = profile; nonplanar.polygon_xyz.back().z = 0.1;
    auto nonfinite = profile; nonfinite.polygon_xyz.front().x = inf;
    auto crossed = profile; std::swap(crossed.polygon_xyz[1],crossed.polygon_xyz[2]);
    auto touching_hole = profile;
    touching_hole.holes_xyz = {{{-1,-0.2,0},{0,-0.2,0},{0,0.2,0},{-1,0.2,0}}};
    const axiom::ProfileRef periodic_profile {"law_bad_seam",{{7.8,0,-0.15},{8.2,0,-0.15},{8.2,0,0.15},{7.8,0,0.15}}};
    const axiom::ProfileRef oversized_periodic {"law_tube_fold",{{3,0,-5},{13,0,-5},{13,0,5},{3,0,5}}};
    std::vector<Station> combined_budget(4097);
    for (std::size_t i = 0; i < combined_budget.size(); ++i)
        combined_budget[i] = {static_cast<double>(i)/4096,1};
    if (!rejected(profile,*budget_rail.value,combined_budget,axiom::diag_codes::kCoreParameterOutOfRange,"sweep_law_sampling") ||
        !rejected(profile,*rail.value,{{0,1},{0.5,1},{std::nextafter(0.5,1.0),1},{1,1}},
                  axiom::diag_codes::kCoreParameterOutOfRange,"sweep_law_sampling") ||
        !rejected(touching_hole,*rail.value,law,axiom::diag_codes::kCoreParameterOutOfRange,"sweep_law_materialization") ||
        !rejected(oversized_periodic,*circle.value,{{0,1},{0.5,1.5},{1,1}})) return false;
    if (!rejected(profile,*rail.value,too_many) || !rejected(profile,*reversed.value,law) ||
        !rejected(profile,*off_plane.value,law) || !rejected(nonplanar,*rail.value,law) ||
        !rejected(nonfinite,*rail.value,law) || !rejected(crossed,*rail.value,law) ||
        !rejected(touching_hole,*rail.value,law) || !rejected(periodic_profile,*circle.value,law) ||
        !rejected(profile,{},law,axiom::diag_codes::kCoreInvalidHandle) ||
        !rejected({"implicit",{}},*rail.value,law)) return false;
    if (transaction.rollback().status != axiom::StatusCode::Ok) return false;
    const auto remains = kernel.topology().query().has_vertex(*temporary.value);
    if (!remains.value || *remains.value) return false;
    auto edit = kernel.topology().begin_transaction();
    if (edit.delete_face(source_faces.value->front()).status != axiom::StatusCode::Ok ||
        edit.rollback().status != axiom::StatusCode::Ok) return false;
    const auto restored = kernel.query().mass_properties(*source.value);
    if (!restored.value || std::abs(restored.value->volume-source_mass.value->volume) > 1e-10 ||
        restored.value->inertia != source_mass.value->inertia ||
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok)
        return false;
    const std::array<Station,2> compatible_law {{{0,1},{1,1.25}}};
    const auto retry = kernel.sweeps().sweep_with_scale_law(profile,*rail.value,compatible_law);
    const auto compatible = kernel.sweeps().sweep_scaled(profile,*rail.value,1.25);
    const auto retry_mass = retry.value ? kernel.query().mass_properties(*retry.value) : axiom::Result<axiom::MassProperties>{};
    const auto compatible_mass = compatible.value ? kernel.query().mass_properties(*compatible.value) :
                                                    axiom::Result<axiom::MassProperties>{};
    return retry.value && compatible.value && retry_mass.value && compatible_mass.value &&
        std::abs(retry_mass.value->volume-compatible_mass.value->volume) < 1e-10 &&
        std::abs(retry_mass.value->area-compatible_mass.value->area) < 1e-10 &&
        kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok;
}

bool test_sweep_section_laws() {
    using Station = axiom::SweepLawStation;
    constexpr double pi = 3.1415926535897932384626433832795;
    const std::vector<Station> law {{0,1,0},{0.25,1.2,pi/3},{0.5,1.2,pi/3},
                                    {0.75,0.8,-pi/6},{1,1.1,pi/8}};
    // Compare to an independent integral of the continuous scale/roll law.
    // The sampled polyhedron is deliberately approximate; its cached moments
    // must separately agree with moments recomputed from actual owned topology.
    for (const int shape : {0,1,2}) for (const bool transformed : {false,true}) {
        axiom::Kernel kernel;
        axiom::ProfileRef profile {"joint_law_rectangle",{{-1,-0.75,0},{1,-0.75,0},{1,0.75,0},{-1,0.75,0}}};
        std::vector<std::array<double,5>> rectangles {{-1,-0.75,2,1.5,1}};
        if (shape == 1) {
            profile.holes_xyz = {{{-0.7,-0.25,0},{-0.3,-0.25,0},{-0.3,0.25,0},{-0.7,0.25,0}},
                                 {{0.3,-0.25,0},{0.7,-0.25,0},{0.7,0.25,0},{0.3,0.25,0}}};
            rectangles.push_back({-0.7,-0.25,0.4,0.5,-1});
            rectangles.push_back({0.3,-0.25,0.4,0.5,-1});
        } else if (shape == 2) {
            profile = {"joint_law_concave",{{-1,-1,0},{1,-1,0},{1,0,0},{0,0,0},{0,1,0},{-1,1,0}}};
            rectangles = {{-1,-1,2,1,1},{-1,0,1,1,1}};
        }
        const auto world = [transformed](axiom::Point3 p) -> axiom::Point3 {
            if (!transformed) return p;
            return {10+0.6*p.x-0.48*p.y+0.64*p.z,
                    -20+0.8*p.x+0.36*p.y-0.48*p.z,30+0.8*p.y+0.6*p.z};
        };
        const double sign = transformed ? -1 : 1, height = 6;
        const double node = std::sqrt(15.0)/10;
        const std::array<std::array<double,2>,3> gauss {{{0.5-node,5.0/18},{0.5,4.0/9},{0.5+node,5.0/18}}};
        double volume = 0;
        std::array<double,3> first {};
        std::array<double,9> second {};
        for (std::size_t k = 1; k < law.size(); ++k) for (int slice = 0; slice < 24; ++slice)
        for (const auto& r : rectangles) for (const auto& gx : gauss)
        for (const auto& gy : gauss) for (const auto& gt : gauss) {
            const auto& a = law[k-1]; const auto& b = law[k];
            const double t = (slice+gt[0])/24;
            const double f = a.fraction+t*(b.fraction-a.fraction);
            const double scale = a.scale+t*(b.scale-a.scale);
            const double angle = sign*(a.twist_angle+t*(b.twist_angle-a.twist_angle));
            const double x = scale*(r[0]+r[2]*gx[0]), y = scale*(r[1]+r[3]*gy[0]);
            const auto p = world({x*std::cos(angle)-y*std::sin(angle),
                                  x*std::sin(angle)+y*std::cos(angle),sign*height*f});
            const double weight = r[2]*r[3]*r[4]*height*(b.fraction-a.fraction)*scale*scale*
                                  gx[1]*gy[1]*gt[1]/24;
            const std::array<double,3> q {p.x,p.y,p.z};
            volume += weight;
            for (int i = 0; i < 3; ++i) {
                first[i] += weight*q[i];
                for (int j = 0; j < 3; ++j) second[3*i+j] += weight*q[i]*q[j];
            }
        }
        std::size_t ring_vertices = profile.polygon_xyz.size();
        const auto original_corner = profile.polygon_xyz.front();
        for (auto& ring : profile.holes_xyz) {
            ring_vertices += ring.size();
            std::reverse(ring.begin(),ring.end());
            for (auto& p : ring) p = world(p);
        }
        if (transformed) std::reverse(profile.polygon_xyz.begin(),profile.polygon_xyz.end());
        for (auto& p : profile.polygon_xyz) p = world(p);
        const auto rail = kernel.curves().make_line_segment(world({0,0,0}),world({0,0,sign*height}));
        if (!rail.value) return false;
        const auto body = kernel.sweeps().sweep_with_law(profile,*rail.value,law);
        if (!body.value || !check_sweep_law_body(kernel,*body.value,ring_vertices,profile.holes_xyz.size(),false)) {
            std::cerr << "joint scale/roll line sweep failed: " << shape << ", transformed=" << transformed << '\n';
            return false;
        }
        const auto mass = kernel.query().mass_properties(*body.value);
        if (!mass.value || std::abs(mass.value->volume-volume) > 0.015*volume) return false;
        for (auto& value : first) value /= volume;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            second[3*i+j] = second[3*i+j]/volume-first[i]*first[j];
        const double trace = second[0]+second[4]+second[8];
        if (std::hypot(mass.value->centroid.x-first[0],mass.value->centroid.y-first[1],mass.value->centroid.z-first[2]) > 0.025)
            return false;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            if (std::abs(mass.value->inertia[3*i+j]-volume*((i == j ? trace : 0)-second[3*i+j])) > 0.025*volume*trace)
                return false;
        // A requested intermediate corner must be a real boundary point after
        // scaling and rolling; its nearest face must belong to this result body.
        const double angle = sign*law[1].twist_angle, scale = law[1].scale;
        const auto key_point = world({scale*(original_corner.x*std::cos(angle)-original_corner.y*std::sin(angle)),
                                      scale*(original_corner.x*std::sin(angle)+original_corner.y*std::cos(angle)),
                                      sign*height*law[1].fraction});
        const auto location = kernel.topology().query().locate_point(*body.value,key_point);
        if (!location.value || location.value->location != axiom::BodyPointLocation::Boundary ||
            !location.value->nearest_boundary || location.value->nearest_boundary->distance > 1e-9) return false;
        const auto owner = kernel.topology().query().bodies_of_face(location.value->nearest_boundary->face);
        if (!owner.value || owner.value->size() != 1 || owner.value->front() != *body.value) return false;
    }

    // Nonuniform-length polyline corners must remain, and their sections roll
    // about the oriented plane normal rather than the oblique rail displacement.
    {
        axiom::Kernel kernel;
        const axiom::ProfileRef profile {"joint_law_polyline",{{-0.4,-0.3,0},{0.4,-0.3,0},{0.4,0.3,0},{-0.4,0.3,0}}};
        const std::array<axiom::Point3,4> path {{{0,0,0},{0.3,0.1,1},{-0.1,0.4,3},{0.6,0.2,6}}};
        const auto rail = kernel.curves().make_composite_polyline(path);
        if (!rail.value) return false;
        const auto body = kernel.sweeps().sweep_with_law(profile,*rail.value,law);
        if (!body.value || !check_sweep_law_body(kernel,*body.value,4,0,false)) return false;
        double length = 0;
        for (std::size_t i = 1; i < path.size(); ++i)
            length += std::hypot(path[i].x-path[i-1].x,path[i].y-path[i-1].y,path[i].z-path[i-1].z);
        const double fraction = std::hypot(0.3,0.1,1.0)/length;
        const double t = fraction/law[1].fraction;
        const double scale = 1+t*(law[1].scale-1), angle = t*law[1].twist_angle;
        const auto p = profile.polygon_xyz.front();
        const auto at_corner = kernel.topology().query().locate_point(*body.value,
            {path[1].x+scale*(p.x*std::cos(angle)-p.y*std::sin(angle)),
             path[1].y+scale*(p.x*std::sin(angle)+p.y*std::cos(angle)),path[1].z});
        if (!at_corner.value || !at_corner.value->nearest_boundary || at_corner.value->nearest_boundary->distance > 1e-9)
            return false;
    }

    // Curved transport, a tangent-continuous mixed rail, and positive/negative
    // full roll on periodic rails. Closed holed sweeps must retain separate shells.
    for (int kind = 0; kind < 6; ++kind) {
        axiom::Kernel kernel;
        axiom::Result<axiom::CurveId> rail;
        if (kind == 0) rail = kernel.curves().make_bezier(std::array<axiom::Point3,4>{{{0,0,0},{4,0,0},{8,0.4,0.2},{12,1,0.5}}});
        if (kind == 1) rail = kernel.curves().make_bspline({{{0,0,0},{3,0,0},{6,0.3,0.1},{9,0.6,0.3},{12,1,0.5}},3,{}});
        if (kind == 2) rail = kernel.curves().make_nurbs({{{0,0,0},{3,0,0},{6,0.3,0.1},{9,0.6,0.3},{12,1,0.5}},
                                                         {1,0.95,1.05,0.95,1},3,{}});
        if (kind == 3) {
            const auto line = kernel.curves().make_line_segment({0,0,0},{4,0,0});
            const auto tail = kernel.curves().make_bezier(std::array<axiom::Point3,4>{{{4,0,0},{6,0,0},{9,0.4,0.2},{12,1,0.5}}});
            if (!line.value || !tail.value) return false;
            rail = kernel.curves().make_composite_chain(std::array{*line.value,*tail.value});
        }
        if (kind == 4) rail = kernel.curves().make_circle({0,0,0},{0,0,1},12);
        if (kind == 5) rail = kernel.curves().make_ellipse({0,0,0},{12,0,0},{0,10,0});
        if (!rail.value) return false;
        axiom::ProfileRef profile {"joint_law_curve",{{0,-0.2,-0.15},{0,0.2,-0.15},{0,0.2,0.15},{0,-0.2,0.15}}};
        if (kind == 2) profile = {"joint_law_curve_concave",
            {{0,-0.2,-0.15},{0,0.2,-0.15},{0,0.2,0},{0,0,0},{0,0,0.15},{0,-0.2,0.15}}};
        if (kind == 3) profile.holes_xyz = {{{0,-0.06,-0.04},{0,0.06,-0.04},{0,0.06,0.04},{0,-0.06,0.04}}};
        const bool closed = kind >= 4;
        auto keys = law;
        if (closed) {
            const auto domain = kernel.curve_service().domain(*rail.value);
            if (!domain.value) return false;
            const auto start = kernel.curve_service().eval(*rail.value,domain.value->min,1);
            if (!start.value) return false;
            const double length = std::hypot(start.value->tangent.x,start.value->tangent.y);
            if (!(length > 0)) return false;
            const auto point = [&](double x, double y) -> axiom::Point3 {
                return {start.value->point.x+x*start.value->tangent.y/length,
                        start.value->point.y-x*start.value->tangent.x/length,start.value->point.z+y};
            };
            profile = {"joint_law_periodic",{point(-0.2,-0.15),point(0.2,-0.15),point(0.2,0.15),point(-0.2,0.15)},
                       {{point(-0.06,-0.04),point(0.06,-0.04),point(0.06,0.04),point(-0.06,0.04)}}};
            const double sign = kind == 4 ? 1 : -1;
            keys = {{0,1,0},{0.25,1.2,sign*pi/2},{0.5,0.8,sign*pi},{0.75,1.1,sign*1.5*pi},{1,1,sign*2*pi}};
        }
        const auto body = kernel.sweeps().sweep_with_law(profile,*rail.value,keys);
        std::size_t ring_vertices = profile.polygon_xyz.size();
        for (const auto& hole : profile.holes_xyz) ring_vertices += hole.size();
        if (!body.value || !check_sweep_law_body(kernel,*body.value,ring_vertices,profile.holes_xyz.size(),closed)) {
            std::cerr << "joint scale/roll curved sweep failed: " << kind << '\n';
            return false;
        }
        if (kind == 4) {
            // A closed reversal returns the same indexed ring without requiring
            // a full turn. A half-turn is still rejected even for a symmetric box.
            const std::array<Station,4> reversal {{{0,1,0},{0.3,1.1,pi/3},{0.7,0.9,-pi/6},{1,1,0}}};
            const auto reversed_roll = kernel.sweeps().sweep_with_law(profile,*rail.value,reversal);
            if (!reversed_roll.value || !check_sweep_law_body(kernel,*reversed_roll.value,ring_vertices,1,true)) return false;
        }
        // Zero roll keeps the existing scale-only topology and moments exactly.
        if (kind == 0) {
            auto zero_roll = keys;
            std::vector<axiom::SweepScaleStation> scales;
            for (auto& key : zero_roll) { key.twist_angle = 0; scales.push_back({key.fraction,key.scale}); }
            const auto joint = kernel.sweeps().sweep_with_law(profile,*rail.value,zero_roll);
            const auto reference = kernel.sweeps().sweep_with_scale_law(profile,*rail.value,scales);
            if (!joint.value || !reference.value) return false;
            const auto a = kernel.query().mass_properties(*joint.value), b = kernel.query().mass_properties(*reference.value);
            if (!a.value || !b.value || std::abs(a.value->volume-b.value->volume) > 1e-10 ||
                std::abs(a.value->area-b.value->area) > 1e-10 || a.value->inertia != b.value->inertia) return false;
        }
    }

    axiom::Kernel kernel;
    const axiom::ProfileRef profile {"joint_law_transaction",{{-1,-0.75,0},{1,-0.75,0},{1,0.75,0},{-1,0.75,0}}};
    const auto rail = kernel.curves().make_line_segment({0,0,0},{0,0,6});
    const auto off_plane = kernel.curves().make_line_segment({0,0,1},{0,0,7});
    const auto backwards = kernel.curves().make_composite_polyline(std::array<axiom::Point3,3>{{{0,0,0},{0,0,5},{0,0,3}}});
    const auto circle = kernel.curves().make_circle({0,0,0},{0,0,1},12);
    const auto budget_rail = kernel.curves().make_composite_polyline(
        std::array<axiom::Point3,3>{{{0,0,0},{0.3,0.1,1},{0.6,0.2,6}}});
    const auto shifted_rail = kernel.curves().make_line_segment({1e16,0,0},{1e16,0,6});
    if (!rail.value || !off_plane.value || !backwards.value || !circle.value || !budget_rail.value || !shifted_rail.value)
        return false;
    const auto source = kernel.sweeps().sweep_with_law(profile,*rail.value,law);
    if (!source.value || !check_sweep_law_body(kernel,*source.value,4,0,false)) return false;
    const auto source_mass = kernel.query().mass_properties(*source.value);
    const auto source_faces = kernel.topology().query().faces_of_body(*source.value);
    if (!source_mass.value || !source_faces.value || source_faces.value->empty()) return false;
    auto transaction = kernel.topology().begin_transaction();
    const auto temporary = transaction.create_vertex({111,112,113});
    const auto objects = kernel.object_count_total(), geometry = kernel.geometry_count(), bodies = kernel.body_count();
    const auto next = kernel.next_object_id(), writes = transaction.write_operation_count();
    const auto runtime = kernel.runtime_store_counts();
    if (!temporary.value || !objects.value || !geometry.value || !bodies.value || !next.value || !writes.value || !runtime.value)
        return false;
    const auto rejected = [&](const axiom::ProfileRef& input, axiom::CurveId path, const std::vector<Station>& keys,
                              std::string_view code = axiom::diag_codes::kCoreParameterOutOfRange,
                              std::string_view stage = {}) {
        const auto result = kernel.sweeps().sweep_with_law(input,path,keys);
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto after = kernel.runtime_store_counts();
        if (!stage.empty()) {
            if (!diagnostic.value || diagnostic.value->issues.size() != 1 || diagnostic.value->issues.front().stage != stage)
                return false;
            const auto& evidence = diagnostic.value->issues.front().numeric_evidence;
            const auto travel = std::find_if(evidence.begin(),evidence.end(),[](const auto& item) {
                return item.name == "absolute_twist_travel" && item.unit == "rad";
            });
            if (travel == evidence.end() || !(travel->value > 0)) return false;
        }
        return result.status == axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,code) && kernel.object_count_total().value == objects.value &&
            kernel.geometry_count().value == geometry.value && kernel.body_count().value == bodies.value &&
            kernel.next_object_id().value == next.value && transaction.write_operation_count().value == writes.value &&
            after.value && after.value->mesh_records == runtime.value->mesh_records &&
            after.value->tessellation_cache_entries == runtime.value->tessellation_cache_entries &&
            after.value->face_tessellation_cache_entries == runtime.value->face_tessellation_cache_entries &&
            after.value->curve_eval_cache_entries == runtime.value->curve_eval_cache_entries &&
            after.value->surface_eval_cache_entries == runtime.value->surface_eval_cache_entries;
    };
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    const std::vector<std::vector<Station>> invalid {
        {},{{0,1,0}},{{0.1,1,0},{1,1,0}},{{0,2,0},{1,1,0}},{{0,1,1},{1,1,0}},{{0,1,0},{0.9,1,0}},
        {{0,1,0},{0.5,1,0},{0.5,1,pi/4},{1,1,pi/2}},{{0,1,0},{0.7,1,0},{0.4,1,0},{1,1,0}},
        {{0,1,0},{0.5,0,0},{1,1,0}},{{0,1,0},{1,-1,0}},{{0,1,0},{1,nan,0}},{{0,1,0},{1,inf,0}},
        {{0,1,0},{nan,1,0},{1,1,0}},{{0,1,0},{1,1,nan}},{{0,1,0},{1,1,inf}},
        {{0,1,0},{1,1,2*pi+0.01}},{{0,1,0},{0.5,1,pi},{1,1,-0.01}},
        {{0,1,0},{0.5,1,2*pi},{1,1,0}},{{0,1,0},{0.5,2000,pi/2},{1,1,pi}},
        {{0,1,0},{1e-18,1,0},{1,1,pi/2}}
    };
    for (const auto& keys : invalid) if (!rejected(profile,*rail.value,keys)) return false;
    std::vector<Station> too_many(4098);
    for (std::size_t i = 0; i < too_many.size(); ++i) too_many[i] = {static_cast<double>(i)/(too_many.size()-1),1,0};
    std::vector<Station> combined_budget(4097);
    for (std::size_t i = 0; i < combined_budget.size(); ++i) {
        const double fraction = static_cast<double>(i)/4096;
        combined_budget[i] = {fraction,1,fraction*pi/2};
    }
    auto nonplanar = profile; nonplanar.polygon_xyz.back().z = 0.1;
    auto nonfinite = profile; nonfinite.polygon_xyz.front().x = inf;
    auto crossed = profile; std::swap(crossed.polygon_xyz[1],crossed.polygon_xyz[2]);
    auto touching = profile; touching.holes_xyz = {{{-1,-0.2,0},{0,-0.2,0},{0,0.2,0},{-1,0.2,0}}};
    auto nested = profile;
    nested.holes_xyz = {{{-0.5,-0.4,0},{0.5,-0.4,0},{0.5,0.4,0},{-0.5,0.4,0}},
                       {{-0.2,-0.1,0},{0.2,-0.1,0},{0.2,0.1,0},{-0.2,0.1,0}}};
    const axiom::ProfileRef periodic {"joint_law_bad_seam",{{11.8,0,-0.15},{12.2,0,-0.15},{12.2,0,0.15},{11.8,0,0.15}}};
    const axiom::ProfileRef rounded {"joint_law_rounded",{{1e16-4,-3,0},{1e16+4,-3,0},{1e16+4,3,0},{1e16-4,3,0}}};
    if (!rejected(profile,*rail.value,too_many) || !rejected(nonplanar,*rail.value,law) ||
        !rejected(nonfinite,*rail.value,law) || !rejected(crossed,*rail.value,law) ||
        !rejected(touching,*rail.value,law,axiom::diag_codes::kCoreParameterOutOfRange,"sweep_law_materialization") ||
        !rejected(nested,*rail.value,law) || !rejected(profile,*backwards.value,law) ||
        !rejected(profile,*off_plane.value,law) || !rejected({"implicit",{}},*rail.value,law) ||
        !rejected(profile,{},law,axiom::diag_codes::kCoreInvalidHandle) ||
        !rejected(rounded,*shifted_rail.value,{{0,1,0},{0.5,0.25,0},{1,1,pi/4}}) ||
        !rejected(profile,*budget_rail.value,combined_budget,axiom::diag_codes::kCoreParameterOutOfRange,"sweep_law_sampling") ||
        !rejected(periodic,*circle.value,{{0,1,0},{1,1,pi}},axiom::diag_codes::kCoreParameterOutOfRange,"sweep_law_seam") ||
        !rejected(profile,*rail.value,{{0,1,0},{0.5,1,pi/4},{std::nextafter(0.5,1.0),1,pi/4},{1,1,pi/2}},
                  axiom::diag_codes::kCoreParameterOutOfRange,"sweep_law_sampling")) return false;
    if (transaction.rollback().status != axiom::StatusCode::Ok) return false;
    const auto remains = kernel.topology().query().has_vertex(*temporary.value);
    if (!remains.value || *remains.value) return false;
    auto edit = kernel.topology().begin_transaction();
    if (edit.delete_face(source_faces.value->front()).status != axiom::StatusCode::Ok ||
        edit.rollback().status != axiom::StatusCode::Ok) return false;
    const auto restored = kernel.query().mass_properties(*source.value);
    if (!restored.value || restored.value->inertia != source_mass.value->inertia ||
        std::abs(restored.value->volume-source_mass.value->volume) > 1e-10 ||
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok)
        return false;
    const std::array<Station,2> twist {{{0,1,0},{1,1,-pi/2}}};
    const auto retry = kernel.sweeps().sweep_with_law(profile,*rail.value,twist);
    const auto reference = kernel.sweeps().extrude_twisted(profile,{0,0,1},6,{0,0,0},-pi/2);
    if (!retry.value || !reference.value || !check_sweep_law_body(kernel,*retry.value,4,0,false)) return false;
    const auto a = kernel.query().mass_properties(*retry.value), b = kernel.query().mass_properties(*reference.value);
    return a.value && b.value && std::abs(a.value->volume-b.value->volume) < 1e-10 &&
        std::abs(a.value->area-b.value->area) < 1e-10;
}

bool test_polygon_lofts() {
    struct Model {
        axiom::ProfileRef base;
        double area;
    };
    const std::vector<Model> models {
        {{"loft_two_holes", {{0,0,0},{8,0,0},{8,6,0},{0,6,0}},
           {{{1,1,0},{3,1,0},{3,3,0},{1,3,0}},
            {{5,2,0},{7,2,0},{7,5,0},{5,5,0}}}}, 38.0},
        {{"loft_concave", {{0,0,0},{6,0,0},{6,2,0},{2,2,0},{2,6,0},{0,6,0}},
           {{{0.5,0.5,0},{1.5,0.5,0},{1.5,1.5,0},{0.5,1.5,0}}}}, 19.0}
    };
    const std::array<double,3> levels {0.0,3.0,7.0};
    const std::array<double,3> scales {1.0,1.4,0.8};
    for (const auto& model : models) for (int variant=0;variant<4;++variant)
    for (const bool tilted : {false,true}) {
        axiom::Kernel kernel;
        const auto rotate=[tilted](axiom::Point3 p) {
            if (!tilted) return p;
            return axiom::Point3 {0.6*p.x-0.48*p.y+0.64*p.z,
                                  0.8*p.x+0.36*p.y-0.48*p.z,
                                  0.8*p.y+0.6*p.z};
        };
        const auto world=[&](axiom::Point3 p) {
            const auto q=rotate(p);
            return axiom::Point3 {q.x+11,q.y-17,q.z+23};
        };
        std::array<axiom::ProfileRef,3> profiles;
        axiom::BoundingBox expected_bbox {};
        std::size_t section_size=0;
        for (std::size_t station=0;station<profiles.size();++station) {
            profiles[station]=model.base;
            profiles[station].label += ":"+std::to_string(station);
            const auto transform_ring=[&](std::vector<axiom::Point3>& ring) {
                for (auto& p:ring) {
                    // A changing section plus in-plane shear exercises a genuine
                    // multi-station loft rather than the extrusion shortcut.
                    p=world({scales[station]*p.x+0.35*levels[station],
                             scales[station]*p.y-0.15*levels[station],levels[station]});
                    if (!expected_bbox.is_valid) expected_bbox={p,p,true};
                    else {
                        expected_bbox.min.x=std::min(expected_bbox.min.x,p.x);
                        expected_bbox.min.y=std::min(expected_bbox.min.y,p.y);
                        expected_bbox.min.z=std::min(expected_bbox.min.z,p.z);
                        expected_bbox.max.x=std::max(expected_bbox.max.x,p.x);
                        expected_bbox.max.y=std::max(expected_bbox.max.y,p.y);
                        expected_bbox.max.z=std::max(expected_bbox.max.z,p.z);
                    }
                }
                if (variant&(1<<station)) std::reverse(ring.begin(),ring.end());
            };
            transform_ring(profiles[station].polygon_xyz);
            for (auto& hole:profiles[station].holes_xyz) transform_ring(hole);
            if (variant==3) std::reverse(profiles[station].holes_xyz.begin(),profiles[station].holes_xyz.end());
            if (station==0) {
                section_size=profiles[station].polygon_xyz.size();
                for (const auto& hole:profiles[station].holes_xyz) section_size+=hole.size();
            }
        }
        const auto body=kernel.sweeps().loft(profiles);
        if (!body.value) {
            std::cerr<<"polygon loft failed: "<<model.base.label<<" variant="<<variant
                     <<" tilted="<<tilted<<'\n';
            return false;
        }
        const auto query=kernel.topology().query();
        const auto shells=query.shells_of_body(*body.value);
        const auto faces=query.faces_of_body(*body.value);
        const auto edges=query.edges_of_body(*body.value);
        const auto vertices=query.vertices_of_body(*body.value);
        const auto bbox=query.bbox_of_body_from_topology(*body.value);
        const auto mass=kernel.query().mass_properties(*body.value);
        const auto topo_mass=query.body_mass_properties(*body.value);
        const std::size_t holes=profiles.front().holes_xyz.size();
        const std::size_t cap_triangles=section_size+2*holes-2;
        const std::size_t expected_faces=2*cap_triangles+2*section_size*(profiles.size()-1);
        const std::size_t expected_edges=section_size*profiles.size()+expected_faces-2+2*holes;
        double expected_volume=0;
        for (std::size_t i=0;i+1<levels.size();++i) {
            expected_volume+=(levels[i+1]-levels[i])*model.area*
                (scales[i]*scales[i]+scales[i]*scales[i+1]+scales[i+1]*scales[i+1])/3;
        }
        if (body.status!=axiom::StatusCode::Ok || !shells.value || shells.value->size()!=1 ||
            !faces.value || faces.value->size()!=expected_faces ||
            !edges.value || edges.value->size()!=expected_edges ||
            !vertices.value || vertices.value->size()!=section_size*profiles.size() ||
            !bbox.value || !mass.value || !topo_mass.value ||
            std::abs(mass.value->volume-expected_volume)>1e-7 ||
            std::abs(topo_mass.value->volume-mass.value->volume)>1e-8 ||
            std::abs(topo_mass.value->area-mass.value->area)>1e-8 ||
            std::hypot(topo_mass.value->centroid.x-mass.value->centroid.x,
                       topo_mass.value->centroid.y-mass.value->centroid.y,
                       topo_mass.value->centroid.z-mass.value->centroid.z)>1e-8 ||
            std::abs(bbox.value->min.x-expected_bbox.min.x)>1e-9 ||
            std::abs(bbox.value->min.y-expected_bbox.min.y)>1e-9 ||
            std::abs(bbox.value->min.z-expected_bbox.min.z)>1e-9 ||
            std::abs(bbox.value->max.x-expected_bbox.max.x)>1e-9 ||
            std::abs(bbox.value->max.y-expected_bbox.max.y)>1e-9 ||
            std::abs(bbox.value->max.z-expected_bbox.max.z)>1e-9 ||
            kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok) {
            std::cerr<<"polygon loft topology/mass/bbox/Strict mismatch: "<<model.base.label<<'\n';
            return false;
        }
        for (const auto edge:*edges.value) {
            const auto uses=query.coedge_count_of_edge(edge);
            const auto adjacent=query.faces_of_edge(edge);
            if (!uses.value || *uses.value!=2 || !adjacent.value || adjacent.value->size()!=2) return false;
        }
        double queried_area=0;
        for (const auto face:*faces.value) {
            const auto area=query.planar_face_area(face);
            const auto owners=query.bodies_of_face(face);
            const auto loops=query.loops_of_face(face);
            if (!area.value || *area.value<=0 || !owners.value || owners.value->size()!=1 ||
                owners.value->front().value!=body.value->value || !loops.value || loops.value->size()!=1) return false;
            queried_area+=*area.value;
        }
        if (std::abs(queried_area-mass.value->area)>1e-8) return false;
        const auto mesh=kernel.convert().brep_to_mesh(*body.value,{});
        if (!mesh.value) return false;
        const auto inspection=kernel.convert().inspect_mesh(*mesh.value);
        if (!inspection.value || inspection.value->tessellation_strategy!="owned_topo_welded" ||
            inspection.value->vertex_count!=section_size*profiles.size() ||
            inspection.value->triangle_count!=expected_faces || inspection.value->connected_components!=1 ||
            inspection.value->has_degenerate_triangles || inspection.value->has_out_of_range_indices) return false;
    }

    axiom::Kernel kernel;
    const auto make_good=[] {
        std::array<axiom::ProfileRef,3> result {{
            {"loft_0",{{0,0,0},{4,0,0},{4,4,0},{0,4,0}},{{{1,1,0},{2,1,0},{2,2,0},{1,2,0}}}},
            {"loft_1",{{0,0,3},{5,0,3},{5,5,3},{0,5,3}},{{{1,1,3},{2.5,1,3},{2.5,2.5,3},{1,2.5,3}}}},
            {"loft_2",{{0,0,7},{3,0,7},{3,3,7},{0,3,7}},{{{0.75,0.75,7},{1.5,0.75,7},{1.5,1.5,7},{0.75,1.5,7}}}}
        }};
        return result;
    };
    const auto good=make_good();
    const auto source=kernel.sweeps().loft(good);
    if (!source.value) return false;
    auto transaction=kernel.topology().begin_transaction();
    const auto temporary=transaction.create_vertex({90,91,92});
    const auto objects=kernel.object_count_total(), geometry=kernel.geometry_count(), bodies=kernel.body_count();
    const auto next=kernel.next_object_id(), writes=transaction.write_operation_count();
    const auto runtime=kernel.runtime_store_counts();
    if (!temporary.value || !objects.value || !geometry.value || !bodies.value || !next.value || !writes.value ||
        !runtime.value) return false;
    const auto rejected=[&](std::span<const axiom::ProfileRef> profiles) {
        const auto result=kernel.sweeps().loft(profiles);
        const auto diagnostic=kernel.diagnostics().get(result.diagnostic_id);
        const auto after=kernel.runtime_store_counts();
        return result.status==axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,axiom::diag_codes::kCoreParameterOutOfRange) &&
            kernel.object_count_total().value==objects.value && kernel.geometry_count().value==geometry.value &&
            kernel.body_count().value==bodies.value && kernel.next_object_id().value==next.value &&
            transaction.write_operation_count().value==writes.value && after.value &&
            after.value->mesh_records==runtime.value->mesh_records &&
            after.value->tessellation_cache_entries==runtime.value->tessellation_cache_entries &&
            after.value->curve_eval_cache_entries==runtime.value->curve_eval_cache_entries &&
            after.value->surface_eval_cache_entries==runtime.value->surface_eval_cache_entries;
    };
    if (!rejected(std::span<const axiom::ProfileRef>(good.data(),1))) return false;
    std::vector<std::array<axiom::ProfileRef,3>> invalid;
    auto bad=good; bad[1].label.clear(); invalid.push_back(bad);
    bad=good; bad[1].polygon_xyz.pop_back(); invalid.push_back(bad);
    bad=good; bad[1].holes_xyz.clear(); invalid.push_back(bad);
    bad=good; bad[1].holes_xyz.front().pop_back(); invalid.push_back(bad);
    bad=good; bad[1].polygon_xyz[2].z+=0.2; invalid.push_back(bad);
    bad=good; bad[1].polygon_xyz={{0,0,3},{5,5,3},{0,5,3},{5,0,3}}; invalid.push_back(bad);
    bad=good; bad[1].holes_xyz.front()={{3,3,3},{6,3,3},{6,4,3},{3,4,3}}; invalid.push_back(bad);
    bad=good; for (auto& p:bad[1].polygon_xyz) p.z=0; for (auto& p:bad[1].holes_xyz.front()) p.z=0;
    invalid.push_back(bad);
    bad=good; for (auto& p:bad[1].polygon_xyz) p.z=8; for (auto& p:bad[1].holes_xyz.front()) p.z=8;
    invalid.push_back(bad);
    bad=good; std::rotate(bad[1].polygon_xyz.begin(),bad[1].polygon_xyz.begin()+2,bad[1].polygon_xyz.end());
    std::rotate(bad[1].holes_xyz.front().begin(),bad[1].holes_xyz.front().begin()+2,bad[1].holes_xyz.front().end());
    invalid.push_back(bad);
    bad=good; bad[1].polygon_xyz[1].x=std::numeric_limits<double>::quiet_NaN(); invalid.push_back(bad);
    bad=good; bad[1].polygon_xyz={{2,0,1},{2,5,1},{2,5,2.5},{2,0,2.5}};
    bad[1].holes_xyz.front()={{2,1,1.2},{2,2,1.2},{2,2,1.5},{2,1,1.5}}; invalid.push_back(bad);
    for (const auto& profiles:invalid) if (!rejected(profiles)) {
        std::cerr<<"invalid polygon loft must fail without model/ID/transaction/cache writes\n";
        return false;
    }
    if (transaction.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto remains=kernel.topology().query().has_vertex(*temporary.value);
    const auto after_rollback=kernel.object_count_total();
    if (!remains.value || *remains.value || !after_rollback.value || *after_rollback.value+1!=*objects.value) return false;
    const auto retry=kernel.sweeps().loft(good);
    return retry.value &&
        kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok &&
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok;
}

bool test_planar_face_thicken() {
    struct Model {
        axiom::ProfileRef profile;
        // Independent signed rectangle integrals: x, y, width, height, sign.
        std::vector<std::array<double,5>> rectangles;
    };
    const std::vector<Model> models {
        {{"rectangle",{{0,0,0},{6,0,0},{6,4,0},{0,4,0}}},{{0,0,6,4,1}}},
        {{"concave",{{0,0,0},{6,0,0},{6,2,0},{2,2,0},{2,6,0},{0,6,0}}},
            {{0,0,6,2,1},{0,2,2,4,1}}},
        {{"hole",{{0,0,0},{6,0,0},{6,4,0},{0,4,0}},
            {{{2,1,0},{4,1,0},{4,3,0},{2,3,0}}}},{{0,0,6,4,1},{2,1,2,2,-1}}}
    };
    const auto close = [](double a, double b) { return std::abs(a-b) <= 2e-8*std::max({1.0,std::abs(a),std::abs(b)}); };
    for (const auto& model : models) for (const bool tilted : {false,true})
        for (const bool reverse_normal : {false,true}) {
        axiom::Kernel kernel;
        auto& topo = kernel.topology().query();
        const std::array<double,9> rotation = tilted
            ? std::array<double,9>{.6,-.48,.64, .8,.36,-.48, 0,.8,.6}
            : std::array<double,9>{1,0,0, 0,1,0, 0,0,1};
        const auto world = [&](axiom::Point3 p) {
            return axiom::Point3{17+rotation[0]*p.x+rotation[1]*p.y+rotation[2]*p.z,
                -23+rotation[3]*p.x+rotation[4]*p.y+rotation[5]*p.z,
                31+rotation[6]*p.x+rotation[7]*p.y+rotation[8]*p.z};
        };
        const double sign = reverse_normal ? -1 : 1, thickness = 2;
        const axiom::Vec3 normal {sign*rotation[2],sign*rotation[5],sign*rotation[8]};
        const auto surface = kernel.surfaces().make_plane(world({0,0,0}),normal);
        if (!surface.value) return false;
        auto setup = kernel.topology().begin_transaction();
        const auto create_ring = [&](std::vector<axiom::Point3> points) -> axiom::Result<axiom::LoopId> {
            // Reverse the outer winding independently of the support normal.
            if (tilted) std::reverse(points.begin(),points.end());
            std::vector<axiom::VertexId> vertices;
            for (auto& p : points) {
                p = world(p);
                const auto vertex = setup.create_vertex(p);
                if (!vertex.value) return {};
                vertices.push_back(*vertex.value);
            }
            std::vector<axiom::CoedgeId> coedges;
            for (std::size_t j=0; j<points.size(); ++j) {
                const std::size_t next=(j+1)%points.size();
                const auto a=points[j], b=points[next];
                // Also exercise reversed coedges of truly trimmed segments:
                // the support endpoints extend beyond the Face boundary.
                const auto curve = reverse_normal
                    ? kernel.curves().make_line_segment({2*b.x-a.x,2*b.y-a.y,2*b.z-a.z},
                                                        {2*a.x-b.x,2*a.y-b.y,2*a.z-b.z})
                    : kernel.curves().make_line_segment(a,b);
                if (!curve.value) return {};
                const auto edge = reverse_normal
                    ? setup.create_trimmed_edge(*curve.value,1.0/3,2.0/3,vertices[next],vertices[j])
                    : setup.create_edge(*curve.value,vertices[j],vertices[next]);
                if (!edge.value) return {};
                const auto coedge = setup.create_coedge(*edge.value,reverse_normal);
                if (!coedge.value) return {};
                coedges.push_back(*coedge.value);
            }
            return setup.create_loop(coedges);
        };
        const auto outer = create_ring(model.profile.polygon_xyz);
        std::vector<axiom::LoopId> holes;
        for (const auto& ring : model.profile.holes_xyz) {
            const auto loop = create_ring(ring);
            if (!loop.value) return false;
            holes.push_back(*loop.value);
        }
        if (!outer.value) return false;
        const auto face = setup.create_face(*surface.value,*outer.value,holes);
        if (!face.value || setup.commit().status != axiom::StatusCode::Ok) return false;
        const auto source_loops = topo.loops_of_face(*face.value);
        const auto source_area = topo.planar_face_area(*face.value);
        const auto body = kernel.sweeps().thicken(*face.value,thickness);
        if (!body.value) { std::cerr << "planar thicken failed: " << model.profile.label << '\n'; return false; }
        double area=0, mx=0, my=0, xx=0, yy=0, xy=0, perimeter=0;
        std::size_t n = model.profile.polygon_xyz.size();
        for (const auto& r : model.rectangles) {
            const double a=r[2]*r[3]*r[4], x=r[0]+r[2]/2, y=r[1]+r[3]/2;
            area+=a; mx+=a*x; my+=a*y;
            xx+=a*(x*x+r[2]*r[2]/12); yy+=a*(y*y+r[3]*r[3]/12); xy+=a*x*y;
        }
        const auto ring_perimeter = [&](const std::vector<axiom::Point3>& ring) {
            for (std::size_t j=0; j<ring.size(); ++j)
                perimeter+=std::hypot(ring[j].x-ring[(j+1)%ring.size()].x,ring[j].y-ring[(j+1)%ring.size()].y);
        };
        ring_perimeter(model.profile.polygon_xyz);
        for (const auto& ring : model.profile.holes_xyz) { n+=ring.size(); ring_perimeter(ring); }
        const double volume=area*thickness, cx=mx/area, cy=my/area;
        const double vx=xx/area-cx*cx, vy=yy/area-cy*cy, cxy=xy/area-cx*cy;
        const std::array<double,9> local_inertia {volume*(vy+thickness*thickness/12),-volume*cxy,0,
            -volume*cxy,volume*(vx+thickness*thickness/12),0, 0,0,volume*(vx+vy)};
        std::array<double,9> inertia {};
        for (std::size_t a=0; a<3; ++a) for (std::size_t b=0; b<3; ++b)
            for (std::size_t c=0; c<3; ++c) for (std::size_t d=0; d<3; ++d)
                inertia[3*a+b]+=rotation[3*a+c]*local_inertia[3*c+d]*rotation[3*b+d];
        const auto center=world({cx,cy,sign*thickness/2});
        const auto shells=topo.shells_of_body(*body.value);
        const auto faces=topo.faces_of_body(*body.value);
        const auto edges=topo.edges_of_body(*body.value);
        const auto vertices=topo.vertices_of_body(*body.value);
        const auto sources=topo.source_faces_of_body(*body.value);
        const auto source_shells=topo.source_shells_of_body(*body.value);
        const auto source_bodies=topo.source_bodies_of_body(*body.value);
        const auto kind=kernel.representation().kind_of_body(*body.value);
        const auto mass=kernel.query().mass_properties(*body.value);
        const auto body_mass=topo.body_mass_properties(*body.value);
        const auto shell_mass=shells.value && shells.value->size()==1
            ? topo.shell_mass_properties(shells.value->front()) : axiom::Result<axiom::MassProperties>{};
        const std::size_t face_count=4*n+4*holes.size()-4;
        if (!shells.value || shells.value->size()!=1 || !faces.value || faces.value->size()!=face_count ||
            !edges.value || edges.value->size()!=6*n+6*holes.size()-6 || !vertices.value || vertices.value->size()!=2*n ||
            !sources.value || sources.value->size()!=1 || sources.value->front().value!=face.value->value ||
            !source_shells.value || !source_shells.value->empty() || !source_bodies.value || !source_bodies.value->empty() ||
            !kind.value || *kind.value!=axiom::RepKind::ExactBRep || !source_area.value || !close(*source_area.value,area) ||
            topo.loops_of_face(*face.value).value!=source_loops.value ||
            kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok) return false;
        for (const auto* result : {&mass,&body_mass,&shell_mass}) {
            if (!result->value || !close(result->value->volume,volume) ||
                !close(result->value->area,2*area+perimeter*thickness) ||
                !close(result->value->centroid.x,center.x) || !close(result->value->centroid.y,center.y) ||
                !close(result->value->centroid.z,center.z)) return false;
            for (std::size_t j=0; j<9; ++j) if (!close(result->value->inertia[j],inertia[j])) return false;
        }
        double boundary_area=0;
        for (const auto result_face : *faces.value) {
            const auto owners=topo.bodies_of_face(result_face);
            const auto provenance=topo.source_faces_of_face(result_face);
            const auto face_area=topo.planar_face_area(result_face);
            if (result_face.value==face.value->value || !owners.value || owners.value->size()!=1 ||
                owners.value->front().value!=body.value->value || !provenance.value || provenance.value->size()!=1 ||
                provenance.value->front().value!=face.value->value || !face_area.value) return false;
            boundary_area+=*face_area.value;
        }
        for (const auto edge : *edges.value) {
            const auto uses=topo.coedge_count_of_edge(edge);
            const auto adjacent=topo.faces_of_edge(edge);
            const auto length=topo.edge_length(edge);
            if (!uses.value || *uses.value!=2 || !adjacent.value || adjacent.value->size()!=2 ||
                !length.value || !(*length.value>0)) return false;
        }
        const auto mesh=kernel.convert().brep_to_mesh(*body.value,{});
        const auto inspection=mesh.value ? kernel.convert().inspect_mesh(*mesh.value) : axiom::Result<axiom::MeshInspectionReport>{};
        const auto section=kernel.query().section_detailed(*body.value,{world({0,0,sign}),normal});
        const auto nearest=kernel.query().closest_point(*body.value,world({-1,1,sign}));
        if (!close(boundary_area,mass.value->area) || !inspection.value ||
            inspection.value->tessellation_strategy!="owned_topo_welded" || inspection.value->vertex_count!=2*n ||
            inspection.value->triangle_count!=face_count || inspection.value->has_degenerate_triangles ||
            inspection.value->has_out_of_range_indices || !section.value || !close(section.value->area,area) ||
            !nearest.value || !nearest.value->nearest_boundary || !close(nearest.value->nearest_boundary->distance,1)) return false;

        if (model.profile.label=="rectangle" && !tilted && !reverse_normal) {
            // Current topology remains the authority after creation. A curved
            // edit must not revive the prism's historical creation mass/cache.
            const auto curved_surface=kernel.surfaces().make_sphere(world({0,0,0}),1);
            if (!curved_surface.value) return false;
            auto edit=kernel.topology().begin_transaction();
            if (edit.replace_surface(faces.value->front(),*curved_surface.value).status!=axiom::StatusCode::Ok) return false;
            const auto writes=edit.write_operation_count();
            const auto failed_mass=kernel.query().mass_properties(*body.value);
            const auto report=kernel.diagnostics().get(failed_mass.diagnostic_id);
            if (failed_mass.value || failed_mass.status!=axiom::StatusCode::NotImplemented || !report.value ||
                !has_issue_code(*report.value,axiom::diag_codes::kCoreOperationUnsupported) ||
                !has_issue_stage(*report.value,"query.mass_properties.support_gate") ||
                topo.body_mass_properties(*body.value).value ||
                topo.shell_mass_properties(shells.value->front()).value ||
                kernel.query().section_detailed(*body.value,{world({0,0,1}),normal}).value ||
                kernel.query().closest_point(*body.value,world({-1,1,1})).value ||
                edit.write_operation_count().value!=writes.value || edit.rollback().status!=axiom::StatusCode::Ok) return false;
            const auto restored=kernel.query().mass_properties(*body.value);
            const auto restored_section=kernel.query().section_detailed(*body.value,{world({0,0,1}),normal});
            const auto restored_nearest=kernel.query().closest_point(*body.value,world({-1,1,1}));
            if (!restored.value || !close(restored.value->volume,volume) || !restored_section.value ||
                !close(restored_section.value->area,area) || !restored_nearest.value ||
                topo.source_faces_of_body(*body.value).value!=sources.value ||
                kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok) return false;
        }
    }

    axiom::Kernel kernel;
    auto& topo=kernel.topology().query();
    const auto box=kernel.primitives().box({0,0,0},3,4,5);
    const auto sphere=kernel.primitives().sphere({0,0,0},2);
    const auto curved=kernel.surfaces().make_sphere({0,0,0},2);
    const auto displaced=kernel.surfaces().make_plane({0,0,100},{0,0,1});
    if (!box.value || !sphere.value || !curved.value || !displaced.value) return false;
    const auto faces=topo.faces_of_body(*box.value), proxy=topo.faces_of_body(*sphere.value);
    if (!faces.value || faces.value->empty() || !proxy.value || proxy.value->empty()) return false;
    const auto face=faces.value->front();
    const auto node=kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"stage3:thicken");
    if (!node.value) return false;
    auto txn=kernel.topology().begin_transaction();
    const auto temporary=txn.create_vertex({90,91,92});
    const auto rejected=[&](axiom::FaceId input, double thickness, axiom::StatusCode status,
                            std::string_view code, std::string_view stage) {
        const auto objects=kernel.object_count_total(), geometry=kernel.geometry_count(), next=kernel.next_object_id();
        const auto writes=txn.write_operation_count();
        const auto stores=kernel.runtime_store_counts();
        const auto invalid=kernel.eval_graph().is_invalid(*node.value);
        const auto recompute=kernel.eval_graph().recompute_count(*node.value);
        const auto result=kernel.sweeps().thicken(input,thickness);
        const auto report=kernel.diagnostics().get(result.diagnostic_id);
        const auto after=kernel.runtime_store_counts();
        return result.status==status && !result.value && report.value && has_issue_code(*report.value,code) &&
            has_issue_stage(*report.value,stage) && kernel.object_count_total().value==objects.value &&
            kernel.geometry_count().value==geometry.value && kernel.next_object_id().value==next.value &&
            txn.write_operation_count().value==writes.value && after.value && stores.value &&
            after.value->mesh_records==stores.value->mesh_records &&
            after.value->tessellation_cache_entries==stores.value->tessellation_cache_entries &&
            after.value->face_tessellation_cache_entries==stores.value->face_tessellation_cache_entries &&
            after.value->curve_eval_cache_entries==stores.value->curve_eval_cache_entries &&
            after.value->surface_eval_cache_entries==stores.value->surface_eval_cache_entries &&
            kernel.eval_graph().is_invalid(*node.value).value==invalid.value &&
            kernel.eval_graph().recompute_count(*node.value).value==recompute.value;
    };
    if (!temporary.value || !rejected({},1,axiom::StatusCode::InvalidInput,
            axiom::diag_codes::kCoreInvalidHandle,"thicken.input_gate")) return false;
    for (const double thickness : {0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        if (!rejected(face,thickness,axiom::StatusCode::InvalidInput,axiom::diag_codes::kModShellFailure,"thicken.input_gate")) return false;
    if (!rejected(face,1e-20,axiom::StatusCode::InvalidInput,axiom::diag_codes::kModShellFailure,"thicken.materialization") ||
        !rejected(proxy.value->front(),1,axiom::StatusCode::NotImplemented,
            axiom::diag_codes::kCoreOperationUnsupported,"thicken.support_gate") ||
        txn.replace_surface(face,*curved.value).status!=axiom::StatusCode::Ok ||
        !rejected(face,1,axiom::StatusCode::NotImplemented,axiom::diag_codes::kCoreOperationUnsupported,"thicken.support_gate") ||
        txn.replace_surface(face,*displaced.value).status!=axiom::StatusCode::Ok ||
        !rejected(face,1,axiom::StatusCode::InvalidTopology,axiom::diag_codes::kModShellFailure,"thicken.topology_gate") ||
        txn.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto remains=topo.has_vertex(*temporary.value);
    const auto retry=kernel.sweeps().thicken(face,1);
    const auto source_bodies=retry.value ? topo.source_bodies_of_body(*retry.value) : axiom::Result<std::vector<axiom::BodyId>>{};
    const auto source_shells=retry.value ? topo.source_shells_of_body(*retry.value) : axiom::Result<std::vector<axiom::ShellId>>{};
    const auto owners=topo.shells_of_face(face);
    const auto result_shells=retry.value ? topo.shells_of_body(*retry.value) : axiom::Result<std::vector<axiom::ShellId>>{};
    const auto shell_sources=result_shells.value && result_shells.value->size()==1
        ? topo.source_shells_of_shell(result_shells.value->front()) : axiom::Result<std::vector<axiom::ShellId>>{};
    return remains.value && !*remains.value && retry.value && source_bodies.value && source_bodies.value->size()==1 &&
        source_bodies.value->front().value==box.value->value &&
        source_shells.value && owners.value && source_shells.value==owners.value &&
        shell_sources.value && shell_sources.value==owners.value &&
        kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok &&
        kernel.validate().validate_all(*box.value,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok;
}

bool test_stage3_modeling_failures() {
    axiom::Kernel kernel;
    const axiom::ProfileRef square {"acceptance_square",{{0,0,0},{2,0,0},{2,2,0},{0,2,0}}};
    const axiom::ProfileRef meridian {"acceptance_meridian",{{2,0,0},{3,0,0},{3,0,4},{2,0,4}}};
    auto top=square;
    for (auto& p : top.polygon_xyz) p.z=4;
    const auto rail=kernel.curves().make_line_segment({0,0,0},{0,0,4});
    const auto tangent=kernel.curves().make_line_segment({0,0,0},{4,0,0});
    const auto unbounded=kernel.curves().make_line({0,0,0},{0,0,1});
    const auto source=kernel.sweeps().extrude(square,{0,0,1},4);
    const auto node=kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"stage3:modeling_failures");
    if (!rail.value || !tangent.value || !unbounded.value || !source.value || !node.value) return false;
    auto txn=kernel.topology().begin_transaction();
    const auto temporary=txn.create_vertex({90,91,92});
    const auto objects=kernel.object_count_total(), geometry=kernel.geometry_count(), next=kernel.next_object_id();
    const auto writes=txn.write_operation_count();
    const auto stores=kernel.runtime_store_counts();
    const auto invalid=kernel.eval_graph().is_invalid(*node.value);
    const auto recompute=kernel.eval_graph().recompute_count(*node.value);
    const auto rejected=[&](const auto& operation, std::string_view stage, std::string_view code) {
        const auto result=operation();
        const auto diagnostic=kernel.diagnostics().get(result.diagnostic_id);
        const auto after=kernel.runtime_store_counts();
        return result.status==axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value,code) && has_issue_stage(*diagnostic.value,stage) &&
            kernel.object_count_total().value==objects.value && kernel.geometry_count().value==geometry.value &&
            kernel.next_object_id().value==next.value && txn.write_operation_count().value==writes.value &&
            after.value && stores.value && after.value->mesh_records==stores.value->mesh_records &&
            after.value->tessellation_cache_entries==stores.value->tessellation_cache_entries &&
            after.value->face_tessellation_cache_entries==stores.value->face_tessellation_cache_entries &&
            after.value->curve_eval_cache_entries==stores.value->curve_eval_cache_entries &&
            after.value->surface_eval_cache_entries==stores.value->surface_eval_cache_entries &&
            kernel.eval_graph().is_invalid(*node.value).value==invalid.value &&
            kernel.eval_graph().recompute_count(*node.value).value==recompute.value;
    };
    auto empty=square; empty.label.clear();
    const axiom::ProfileRef collinear {"collinear",{{0,0,0},{1,0,0},{2,0,0}}};
    auto outside_hole=square;
    outside_hole.holes_xyz={{{3,3,0},{4,3,0},{4,4,0},{3,4,0}}};
    auto crossed=meridian;
    for (auto& p : crossed.polygon_xyz) p.x-=2.5;
    const auto code=axiom::diag_codes::kCoreParameterOutOfRange;
    if (!temporary.value ||
        !rejected([&] { return kernel.sweeps().extrude(empty,{0,0,1},4); },"extrude.input_gate",code) ||
        !rejected([&] { return kernel.sweeps().extrude(collinear,{0,0,1},4); },"extrude.input_gate",code) ||
        !rejected([&] { return kernel.sweeps().extrude(outside_hole,{0,0,1},4); },"extrude.materialization",code) ||
        !rejected([&] { return kernel.sweeps().revolve(meridian,{{0,0,0},{0,0,1}},0); },"revolve.input_gate",code) ||
        !rejected([&] { return kernel.sweeps().revolve(crossed,{{0,0,0},{0,0,1}},1); },"revolve.materialization",code) ||
        !rejected([&] { return kernel.sweeps().sweep(square,{}); },"sweep.input_gate",axiom::diag_codes::kCoreInvalidHandle) ||
        !rejected([&] { return kernel.sweeps().sweep(square,*unbounded.value); },"sweep.input_gate",code) ||
        !rejected([&] { return kernel.sweeps().sweep(square,*tangent.value); },"sweep.input_gate",code) ||
        !rejected([&] { return kernel.sweeps().sweep(outside_hole,*rail.value); },"sweep.materialization",code) ||
        !rejected([&] { return kernel.sweeps().loft(std::array{square}); },"loft.input_gate",code) ||
        !rejected([&] { return kernel.sweeps().loft(std::array{square,square}); },"loft.materialization",code) ||
        txn.rollback().status!=axiom::StatusCode::Ok) return false;
    const auto remains=kernel.topology().query().has_vertex(*temporary.value);
    const auto extrude=kernel.sweeps().extrude(square,{0,0,1},4);
    const auto revolve=kernel.sweeps().revolve(meridian,{{0,0,0},{0,0,1}},1);
    const auto sweep=kernel.sweeps().sweep(square,*rail.value);
    const auto loft=kernel.sweeps().loft(std::array{square,top});
    if (!remains.value || *remains.value || !extrude.value || !revolve.value || !sweep.value || !loft.value) return false;
    for (const auto body : {*source.value,*extrude.value,*revolve.value,*sweep.value,*loft.value})
        if (!kernel.query().mass_properties(body).value ||
            kernel.validate().validate_all(body,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok) return false;
    return true;
}

bool test_stage3_model_mass_references() {
    const double pi = std::acos(-1.0);
    const auto close = [](double actual, double expected) {
        return std::abs(actual-expected) <= 2e-8*std::max(1.0,std::abs(expected));
    };
    for (const bool tilted : {false,true}) {
        axiom::Kernel kernel;
        auto& topo = kernel.topology().query();
        const std::array<double,9> rotation = tilted
            ? std::array<double,9>{.6,-.48,.64, .8,.36,-.48, 0,.8,.6}
            : std::array<double,9>{1,0,0, 0,1,0, 0,0,1};
        const auto world = [&](axiom::Point3 p) {
            return axiom::Point3{17+rotation[0]*p.x+rotation[1]*p.y+rotation[2]*p.z,
                -23+rotation[3]*p.x+rotation[4]*p.y+rotation[5]*p.z,
                31+rotation[6]*p.x+rotation[7]*p.y+rotation[8]*p.z};
        };
        const axiom::Vec3 axis{rotation[2],rotation[5],rotation[8]};
        const auto check = [&](axiom::BodyId body, axiom::MassProperties reference) {
            reference.centroid = world(reference.centroid);
            const auto local_inertia = reference.inertia;
            reference.inertia = {};
            for (std::size_t i=0; i<3; ++i) for (std::size_t j=0; j<3; ++j)
                for (std::size_t a=0; a<3; ++a) for (std::size_t b=0; b<3; ++b)
                    reference.inertia[3*i+j] += rotation[3*i+a]*local_inertia[3*a+b]*rotation[3*j+b];
            const auto public_mass = kernel.query().mass_properties(body);
            const auto topology_mass = topo.body_mass_properties(body);
            const auto shells = topo.shells_of_body(body);
            const auto shell_mass = shells.value && shells.value->size() == 1
                ? topo.shell_mass_properties(shells.value->front()) : axiom::Result<axiom::MassProperties>{};
            for (const auto* result : {&public_mass,&topology_mass,&shell_mass}) {
                if (!result->value || !close(result->value->volume,reference.volume) ||
                    !close(result->value->area,reference.area) ||
                    !close(result->value->centroid.x,reference.centroid.x) ||
                    !close(result->value->centroid.y,reference.centroid.y) ||
                    !close(result->value->centroid.z,reference.centroid.z)) return false;
                for (std::size_t i=0; i<9; ++i)
                    if (!close(result->value->inertia[i],reference.inertia[i])) return false;
            }
            const auto faces = topo.faces_of_body(body);
            const auto edges = topo.edges_of_body(body);
            const auto vertices = topo.vertices_of_body(body);
            const auto kind = kernel.representation().kind_of_body(body);
            if (!faces.value || faces.value->empty() || !edges.value || edges.value->empty() ||
                !vertices.value || vertices.value->empty() || !kind.value || *kind.value != axiom::RepKind::ExactBRep)
                return false;
            for (const auto edge : *edges.value) {
                const auto uses = topo.coedge_count_of_edge(edge);
                const auto adjacent = topo.faces_of_edge(edge);
                const auto length = topo.edge_length(edge);
                if (!uses.value || *uses.value != 2 || !adjacent.value || adjacent.value->size() != 2 ||
                    !length.value || !(*length.value > 0)) return false;
            }
            double boundary_area = 0;
            for (const auto face : *faces.value) {
                const auto area = topo.planar_face_area(face);
                const auto owners = topo.bodies_of_face(face);
                if (!area.value || !owners.value || owners.value->size() != 1 || owners.value->front().value != body.value)
                    return false;
                boundary_area += *area.value;
            }
            const auto mesh = kernel.convert().brep_to_mesh(body,{});
            const auto inspection = mesh.value ? kernel.convert().inspect_mesh(*mesh.value)
                : axiom::Result<axiom::MeshInspectionReport>{};
            return close(boundary_area,reference.area) && inspection.value &&
                inspection.value->tessellation_strategy == "owned_topo_welded" &&
                !inspection.value->has_degenerate_triangles &&
                kernel.validate().validate_all(body,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok;
        };
        const axiom::ProfileRef square{"mass_square",{world({-1,-1,0}),world({1,-1,0}),
            world({1,1,0}),world({-1,1,0})}};
        const auto extrude = kernel.sweeps().extrude(square,axis,4);
        const auto rail = kernel.curves().make_line_segment(world({0,0,0}),world({0,0,4}));
        const auto sweep = rail.value ? kernel.sweeps().sweep(square,*rail.value) : axiom::Result<axiom::BodyId>{};
        // Independent rectangular-prism integrals. Straight rail sweep has no
        // curvature sampling error; translation/rotation do not change moments.
        axiom::MassProperties prism{};
        prism.volume = 16;
        prism.area = 40;
        prism.centroid = {0,0,2};
        prism.inertia = {80.0/3,0,0, 0,80.0/3,0, 0,0,32.0/3};
        if (!extrude.value || !sweep.value || !check(*extrude.value,prism) || !check(*sweep.value,prism)) return false;

        const axiom::ProfileRef top{"mass_square_top",{world({-2,-2,4}),world({2,-2,4}),
            world({2,2,4}),world({-2,2,4})}};
        const auto loft = kernel.sweeps().loft(std::array{square,top});
        // Integrate square cross-sections of side 2*(1+z/4), z in [0,4].
        // E[x^2]=31/35; Var[z]=291/245; these rational references do not
        // reuse triangle/tetrahedron integration. Planar lateral faces are exact.
        axiom::MassProperties frustum{};
        frustum.volume = 112.0/3;
        frustum.area = 20+12*std::sqrt(17.0);
        frustum.centroid = {0,0,17.0/7};
        frustum.inertia = {frustum.volume*508/245,0,0,
            0,frustum.volume*508/245,0, 0,0,frustum.volume*62/35};
        if (!loft.value || !check(*loft.value,frustum)) return false;

        const axiom::ProfileRef meridian{"mass_annular_meridian",{world({2,0,0}),world({3,0,0}),
            world({3,0,4}),world({2,0,4})}};
        for (const bool full : {false,true}) for (const double sign : {-1.0,1.0}) {
            const double span = full ? 2*pi : pi/2;
            const double start = .3, end = start+sign*span;
            const auto revolve = kernel.sweeps().revolve_between(meridian,{world({0,0,0}),axis},start,end);
            const auto vertices = revolve.value ? topo.vertex_count_of_body(*revolve.value) : axiom::Result<std::uint64_t>{};
            if (!revolve.value || !vertices.value || *vertices.value%4 != 0) return false;
            const std::size_t intervals = *vertices.value/4-(full ? 0 : 1);
            if (intervals == 0) return false;
            const double delta = span/static_cast<double>(intervals);
            std::vector<axiom::Point2> outer, inner;
            for (std::size_t i=0; i<intervals+(full ? 0 : 1); ++i) {
                const double angle = start+sign*delta*static_cast<double>(i);
                outer.push_back({3*std::cos(angle),3*std::sin(angle)});
                inner.push_back({2*std::cos(angle),2*std::sin(angle)});
            }
            std::reverse(inner.begin(),inner.end());
            std::vector<std::vector<axiom::Point2>> rings;
            if (full) rings = {outer,inner};
            else {
                outer.insert(outer.end(),inner.begin(),inner.end());
                rings.push_back(std::move(outer));
            }
            // Green's theorem on polygon edges, followed by the exact prism
            // product integral. Independent of production's surface triangles.
            double twice_area=0, mx=0, my=0, xx=0, yy=0, xy=0, perimeter=0;
            for (const auto& ring : rings) for (std::size_t i=0; i<ring.size(); ++i) {
                const auto a = ring[i], b = ring[(i+1)%ring.size()];
                const double cross = a.x*b.y-b.x*a.y;
                twice_area += cross;
                mx += (a.x+b.x)*cross/6;
                my += (a.y+b.y)*cross/6;
                xx += (a.x*a.x+a.x*b.x+b.x*b.x)*cross/12;
                yy += (a.y*a.y+a.y*b.y+b.y*b.y)*cross/12;
                xy += (2*a.x*a.y+a.x*b.y+b.x*a.y+2*b.x*b.y)*cross/24;
                perimeter += std::hypot(b.x-a.x,b.y-a.y);
            }
            const double area = sign*twice_area/2;
            const double cx = sign*mx/area, cy = sign*my/area;
            const double vx = sign*xx/area-cx*cx, vy = sign*yy/area-cy*cy;
            const double cxy = sign*xy/area-cx*cy;
            axiom::MassProperties sampled{};
            sampled.volume = 4*area;
            sampled.area = 2*area+4*perimeter;
            sampled.centroid = {cx,cy,2};
            sampled.inertia = {sampled.volume*(vy+4.0/3),-sampled.volume*cxy,0,
                -sampled.volume*cxy,sampled.volume*(vx+4.0/3),0, 0,0,sampled.volume*(vx+vy)};
            if (!check(*revolve.value,sampled)) return false;
            // The returned mass describes chords, not the smooth annular sector.
            // Volume/cap area relative error is 1-sin(delta)/delta <= delta^2/6;
            // arc-length relative error is <= delta^2/24, so total area has
            // the same delta^2/6 upper bound. Centroid/inertia converge as O(delta^2).
            const double low = std::min(start,end), high = std::max(start,end);
            const double smooth_area = 2.5*span;
            const double smooth_perimeter = 5*span+(full ? 0 : 2);
            const double smooth_cx = (19.0/3)*(std::sin(high)-std::sin(low))/smooth_area;
            const double smooth_cy = (19.0/3)*(std::cos(low)-std::cos(high))/smooth_area;
            const double smooth_xx = 65.0/4*(span/2+(std::sin(2*high)-std::sin(2*low))/4)/smooth_area;
            const double smooth_yy = 65.0/4*(span/2-(std::sin(2*high)-std::sin(2*low))/4)/smooth_area;
            const double smooth_xy = 65.0/16*(std::cos(2*low)-std::cos(2*high))/smooth_area;
            const double smooth_volume = 4*smooth_area;
            const std::array<double,9> smooth_inertia{
                smooth_volume*(smooth_yy-smooth_cy*smooth_cy+4.0/3),
                -smooth_volume*(smooth_xy-smooth_cx*smooth_cy),0,
                -smooth_volume*(smooth_xy-smooth_cx*smooth_cy),
                smooth_volume*(smooth_xx-smooth_cx*smooth_cx+4.0/3),0,
                0,0,smooth_volume*(smooth_xx+smooth_yy-smooth_cx*smooth_cx-smooth_cy*smooth_cy)};
            if (std::abs(sampled.volume-smooth_volume) > smooth_volume*delta*delta/6+1e-10 ||
                std::abs(sampled.area-(2*smooth_area+4*smooth_perimeter)) >
                    (2*smooth_area+4*smooth_perimeter)*delta*delta/6+1e-10 ||
                std::hypot(cx-smooth_cx,cy-smooth_cy) > 3*delta*delta+1e-10) return false;
            for (std::size_t i=0; i<9; ++i)
                if (std::abs(sampled.inertia[i]-smooth_inertia[i]) > smooth_volume*9*delta*delta+1e-10) return false;
        }
    }
    return true;
}

bool test_stage3_consistency_chain() {
    axiom::Kernel kernel;
    auto& topo = kernel.topology().query();
    auto& eval = kernel.eval_graph();
    const auto close = [](double a, double b) {
        return std::abs(a-b) <= 2e-8*std::max({1.0,std::abs(a),std::abs(b)});
    };
    const axiom::ProfileRef rectangle{"consistency_rectangle",{{0,0,0},{2,0,0},{2,3,0},{0,3,0}}};
    const axiom::ProfileRef perforated{"consistency_perforated",
        {{0,0,0},{4,0,0},{4,4,0},{0,4,0}},{{{1,1,0},{3,1,0},{3,3,0},{1,3,0}}}};
    const auto extrude = kernel.sweeps().extrude(perforated,{0,0,1},3);
    const auto rail = kernel.curves().make_line_segment({0,0,0},{0,0,4});
    const auto sweep = rail.value ? kernel.sweeps().sweep(rectangle,*rail.value) : axiom::Result<axiom::BodyId>{};
    auto upper = rectangle;
    for (auto& point : upper.polygon_xyz) point.z = 4;
    const auto loft = kernel.sweeps().loft(std::array{rectangle,upper});
    const double span = std::acos(-1.0)/2;
    const axiom::ProfileRef meridian{"consistency_meridian",{{2,0,0},{3,0,0},{3,0,4},{2,0,4}}};
    const auto revolve = kernel.sweeps().revolve_between(meridian,{{0,0,0},{0,0,1}},0,-span);
    const auto source = kernel.primitives().box({0,0,0},2,3,4);
    const auto source_faces = source.value ? topo.faces_of_body(*source.value) : axiom::Result<std::vector<axiom::FaceId>>{};
    if (!extrude.value || !sweep.value || !loft.value || !revolve.value || !source_faces.value || source_faces.value->empty()) return false;
    const auto thicken = kernel.sweeps().thicken(source_faces.value->front(),2);
    const auto remote = kernel.primitives().box({50,50,50},1,1,1);
    const auto revolve_vertices = topo.vertex_count_of_body(*revolve.value);
    if (!thicken.value || !remote.value || !revolve_vertices.value || *revolve_vertices.value % 4 != 0) return false;
    const double intervals = static_cast<double>(*revolve_vertices.value/4-1);
    if (!(intervals > 0)) return false;
    // The directed revolution is a chord-face polyhedron, with independently
    // computed polygon-annulus area and perimeter, not a smooth/bbox reference.
    const double annulus = 2.5*intervals*std::sin(span/intervals);
    const double perimeter = 10*intervals*std::sin(span/(2*intervals))+2;
    const std::array bodies{*extrude.value,*sweep.value,*loft.value,*revolve.value,*thicken.value};
    const std::array volumes{36.0,24.0,24.0,4*annulus,12.0};
    const std::array areas{96.0,52.0,52.0,2*annulus+4*perimeter,32.0};
    const std::array section_areas{12.0,6.0,6.0,annulus,6.0};
    const double revolve_center = 19/(15*intervals*std::tan(span/(2*intervals)));
    const std::array<axiom::Point3,5> centers{{{2,2,1.5},{1,1.5,2},{1,1.5,2},
        {revolve_center,-revolve_center,2},{1,1.5,-1}}};
    // These corners minimize distance to (60,60,mid_z) and to the remote
    // box's (50,50,50) corner. The directed revolve lies in quadrant IV.
    const std::array<axiom::Point3,5> nearest_corners{{{4,4,3},{2,3,4},{2,3,4},{3,0,4},{2,3,0}}};
    const auto remote_faces = topo.faces_of_body(*remote.value);
    const auto remote_shells = topo.shells_of_body(*remote.value);
    if (!remote_faces.value || !remote_shells.value || remote_shells.value->size()!=1) return false;
    for (std::size_t i=0; i<bodies.size(); ++i) {
        const auto body = bodies[i];
        const auto bound = eval.register_node(axiom::NodeKind::Analysis,"body:"+std::to_string(body.value));
        const auto consumer = eval.register_node(axiom::NodeKind::Analysis,"consistency:query_rep");
        if (!bound.value || !consumer.value || eval.add_dependency(*consumer.value,*bound.value).status != axiom::StatusCode::Ok) return false;
        const auto nodes = eval.nodes_of_body(body);
        const auto shells = topo.shells_of_body(body);
        const auto faces = topo.faces_of_body(body);
        const auto source_bodies = topo.source_bodies_of_body(body);
        const auto source_shells = topo.source_shells_of_body(body);
        const auto sources = topo.source_faces_of_body(body);
        const auto bbox = topo.bbox_of_body_from_topology(body);
        const auto mass = kernel.query().mass_properties(body);
        const auto boundary_mass = topo.body_mass_properties(body);
        const auto kind = kernel.representation().kind_of_body(body);
        if (!nodes.value || nodes.value->size()!=1 || nodes.value->front().value!=bound.value->value ||
            !shells.value || shells.value->size()!=1 || !faces.value || faces.value->empty() ||
            !source_bodies.value || !source_shells.value || !sources.value || !bbox.value ||
            !mass.value || !boundary_mass.value || !kind.value || *kind.value != axiom::RepKind::ExactBRep ||
            !close(mass.value->volume,volumes[i]) || !close(mass.value->area,areas[i]) ||
            !close(boundary_mass.value->volume,volumes[i]) || !close(boundary_mass.value->area,areas[i]) ||
            kernel.validate().validate_all(body,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) return false;
        for (const auto* result : {&mass,&boundary_mass})
            if (!close(result->value->centroid.x,centers[i].x) || !close(result->value->centroid.y,centers[i].y) ||
                !close(result->value->centroid.z,centers[i].z)) return false;
        if (i==4 && (*source_bodies.value!=std::vector<axiom::BodyId>{*source.value} ||
                     *sources.value!=std::vector<axiom::FaceId>{source_faces.value->front()})) return false;
        for (const auto id : *source_bodies.value) {
            const auto exists = topo.has_body(id);
            if (!exists.value || !*exists.value || id == body) return false;
        }
        for (const auto id : *source_shells.value) {
            const auto exists = topo.has_shell(id);
            if (!exists.value || !*exists.value || id == shells.value->front()) return false;
        }
        for (const auto id : *sources.value) {
            const auto exists = topo.has_face(id);
            if (!exists.value || !*exists.value || std::find(faces.value->begin(),faces.value->end(),id)!=faces.value->end()) return false;
        }
        double face_area = 0;
        for (const auto face : *faces.value) {
            const auto area = topo.face_area(face);
            const auto owners = topo.bodies_of_face(face);
            const auto support = topo.surface_of_face(face);
            const auto provenance = topo.source_faces_of_face(face);
            if (!area.value || !owners.value || *owners.value != std::vector<axiom::BodyId>{body} ||
                !support.value || support.value->value==0 || !provenance.value) return false;
            for (const auto id : *provenance.value) {
                const auto exists = topo.has_face(id);
                if (!exists.value || !*exists.value) return false;
            }
            face_area += *area.value;
        }
        if (!close(face_area,areas[i])) return false;
        for (std::size_t k=0; k<9; ++k)
            if (!close(mass.value->inertia[k],boundary_mass.value->inertia[k])) return false;
        const double mid_z = (bbox.value->min.z+bbox.value->max.z)/2;
        const auto section = kernel.query().section_detailed(body,{{0,0,mid_z},{0,0,1}});
        const auto nearest = kernel.query().closest_point(body,{60,60,mid_z});
        const auto pair = topo.closest_points(body,*remote.value);
        const auto distance = kernel.query().min_distance(body,*remote.value);
        const auto reverse = kernel.query().min_distance(*remote.value,body);
        const auto& corner = nearest_corners[i];
        const double point_distance = std::hypot(60-corner.x,60-corner.y);
        const double body_distance = std::hypot(50-corner.x,50-corner.y,50-corner.z);
        if (!section.value || !close(section.value->area,section_areas[i]) || section.value->triangles.empty() ||
            !nearest.value || !nearest.value->nearest_boundary || !pair.value || !distance.value || !reverse.value ||
            !close(nearest.value->nearest_boundary->distance,point_distance) ||
            !close(nearest.value->nearest_boundary->point.x,corner.x) ||
            !close(nearest.value->nearest_boundary->point.y,corner.y) ||
            !close(nearest.value->nearest_boundary->point.z,mid_z) || !close(*distance.value,body_distance) ||
            !close(pair.value->distance,*distance.value) || !close(*distance.value,*reverse.value) ||
            !close(pair.value->first_point.x,corner.x) || !close(pair.value->first_point.y,corner.y) ||
            !close(pair.value->first_point.z,corner.z) || !close(pair.value->second_point.x,50) ||
            !close(pair.value->second_point.y,50) || !close(pair.value->second_point.z,50) ||
            pair.value->first_shell!=shells.value->front() || pair.value->second_shell!=remote_shells.value->front() ||
            std::find(faces.value->begin(),faces.value->end(),pair.value->first_face)==faces.value->end() ||
            std::find(remote_faces.value->begin(),remote_faces.value->end(),pair.value->second_face)==remote_faces.value->end() ||
            nearest.value->nearest_boundary->shell != shells.value->front() ||
            std::find(faces.value->begin(),faces.value->end(),nearest.value->nearest_boundary->face)==faces.value->end()) return false;
        const auto full = kernel.convert().brep_to_mesh(body,{});
        const auto local = kernel.convert().brep_to_mesh_local(body,std::span<const axiom::FaceId>(faces.value->data(),1),{});
        const auto shell = kernel.convert().brep_to_mesh_shell(body,shells.value->front(),{});
        for (const auto& mesh : {full,local,shell}) {
            if (!mesh.value) return false;
            const auto report = kernel.convert().inspect_mesh(*mesh.value);
            if (!report.value || report.value->triangle_count==0 || report.value->connected_components!=1 ||
                report.value->has_out_of_range_indices || report.value->has_degenerate_triangles ||
                (report.value->tessellation_strategy.find("faces_welded")==std::string::npos &&
                 report.value->tessellation_strategy!="owned_topo_welded")) return false;
        }
        if (kernel.convert().brep_to_mesh(body,{}).value != full.value ||
            topo.source_faces_of_body(body).value != sources.value ||
            topo.faces_of_body(body).value != faces.value ||
            eval.is_invalid(*bound.value).value != std::optional<bool>{false} ||
            eval.is_invalid(*consumer.value).value != std::optional<bool>{false} ||
            eval.recompute_count(*bound.value).value != std::optional<std::uint64_t>{0}) return false;
    }
    return kernel.core_runtime_invariants_hold().value == std::optional<bool>{true};
}

bool test_stage3_model_query_chain() {
    axiom::Kernel kernel;
    auto& topo = kernel.topology().query();
    const auto close = [](double a, double b) { return std::abs(a-b) <= 2e-8*std::max({1.0,std::abs(a),std::abs(b)}); };
    const axiom::ProfileRef square{"query_square",{{0,0,0},{2,0,0},{2,2,0},{0,2,0}}};
    const auto extrude = kernel.sweeps().extrude(square,{0,0,1},4);
    const auto rail = kernel.curves().make_composite_polyline(
        std::array<axiom::Point3,3>{{{0,0,0},{1,0,2},{1,1,4}}});
    const auto sweep = rail.value ? kernel.sweeps().sweep(square,*rail.value) : axiom::Result<axiom::BodyId>{};
    auto top = square;
    for (auto& p : top.polygon_xyz) { p.x *= 2; p.y *= 2; p.z = 4; }
    const auto loft = kernel.sweeps().loft(std::array{square,top});
    const auto scaled = kernel.sweeps().extrude_scaled(square,{0,0,1},4,{0,0,0},2);
    const double pi = std::acos(-1.0);
    const auto twisted = kernel.sweeps().extrude_twisted(square,{0,0,1},4,{1,1,0},pi/2);
    const std::array<axiom::ExtrusionLawStation,3> extrusion_law{{{0,1,0},{.5,1.5,pi/8},{1,2,pi/4}}};
    const auto law = kernel.sweeps().extrude_with_law(square,{0,0,1},4,{0,0,0},extrusion_law);
    const auto straight = kernel.curves().make_line_segment({0,0,0},{0,0,4});
    const std::array<axiom::SweepLawStation,3> sweep_law{{{0,1,0},{.5,1.5,pi/8},{1,2,pi/4}}};
    const auto joint = straight.value ? kernel.sweeps().sweep_with_law(square,*straight.value,sweep_law)
                                      : axiom::Result<axiom::BodyId>{};
    if (!extrude.value || !sweep.value || !loft.value || !scaled.value || !twisted.value ||
        !law.value || !joint.value) return false;
    const auto node = kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"stage3:model_query");
    if (!node.value) return false;
    for (const auto body : {*extrude.value,*sweep.value,*loft.value,*scaled.value,*twisted.value,*law.value,*joint.value}) {
        // z=2 is an explicit law station, so its reference area is 4*1.5^2;
        // twisting alone preserves the square's area at the sampled station.
        const double area = body.value == loft.value->value || body.value == scaled.value->value ||
            body.value == law.value->value || body.value == joint.value->value ? 9 : 4;
        const auto shells = topo.shells_of_body(body);
        const auto faces = topo.faces_of_body(body);
        const auto sources = topo.source_faces_of_body(body);
        const auto kind = kernel.representation().kind_of_body(body);
        const auto topology_bbox = topo.bbox_of_body_from_topology(body);
        const auto mass = kernel.query().mass_properties(body);
        const auto dedicated_mass = topo.body_mass_properties(body);
        const auto before = kernel.runtime_store_counts();
        const auto objects = kernel.object_count_total();
        const auto invalid = kernel.eval_graph().is_invalid(*node.value);
        if (!shells.value || shells.value->size() != 1 || !faces.value || !sources.value ||
            !kind.value || *kind.value != axiom::RepKind::ExactBRep || !topology_bbox.value ||
            !mass.value || !dedicated_mass.value || !close(mass.value->volume,dedicated_mass.value->volume) ||
            !before.value || !objects.value) return false;
        const auto section = kernel.query().section_detailed(body,{{0,0,2},{0,0,1}});
        const auto dedicated = topo.section(body,{{0,0,2},{0,0,1}});
        const auto outside = kernel.query().closest_point(body,{-10,1,2});
        const auto distance = kernel.query().min_distance(body,*extrude.value);
        if (!section.value || !dedicated.value || !close(section.value->area,area) ||
            !close(dedicated.value->area,area) || section.value->triangles.empty() ||
            !outside.value || !outside.value->nearest_boundary || !distance.value || !close(*distance.value,0) ||
            outside.value->nearest_boundary->shell.value != shells.value->front().value ||
            std::none_of(faces.value->begin(),faces.value->end(),[&](axiom::FaceId id) {
                return id.value == outside.value->nearest_boundary->face.value;
            })) return false;
        const auto after = kernel.runtime_store_counts();
        if (!after.value || kernel.object_count_total().value != objects.value ||
            after.value->mesh_records != before.value->mesh_records ||
            after.value->tessellation_cache_entries != before.value->tessellation_cache_entries ||
            after.value->face_tessellation_cache_entries != before.value->face_tessellation_cache_entries ||
            after.value->curve_eval_cache_entries != before.value->curve_eval_cache_entries ||
            after.value->surface_eval_cache_entries != before.value->surface_eval_cache_entries ||
            kernel.eval_graph().is_invalid(*node.value).value != invalid.value ||
            topo.source_faces_of_body(body).value != sources.value) return false;
    }
    // Revolve stores a sampled annular prism. Its horizontal section is the
    // polygon annulus, whose independent area is n*(R^2-r^2)*sin(span/n)/2.
    // This deliberately differs from the smooth annulus formula 5*pi.
    const axiom::ProfileRef meridian{"query_annular_meridian",{{2,0,0},{3,0,0},{3,0,4},{2,0,4}}};
    for (const bool full : {false,true}) {
        const double span = full ? 2*pi : pi/2;
        const auto revolve = kernel.sweeps().revolve_between(meridian,{{0,0,0},{0,0,1}},0,span);
        if (!revolve.value) return false;
        const auto vertices = topo.vertex_count_of_body(*revolve.value);
        if (!vertices.value || *vertices.value%4 != 0) return false;
        const auto intervals = *vertices.value/4-(full ? 0 : 1);
        if (intervals == 0) return false;
        const double expected = 2.5*static_cast<double>(intervals)*std::sin(span/static_cast<double>(intervals));
        const auto section = kernel.query().section_detailed(*revolve.value,{{0,0,2},{0,0,1}});
        const auto mass = topo.body_mass_properties(*revolve.value);
        const auto outside = kernel.query().closest_point(*revolve.value,{0,0,2});
        if (!section.value || !close(section.value->area,expected) || !mass.value ||
            !close(mass.value->volume,4*expected) || !outside.value || !outside.value->nearest_boundary ||
            outside.value->location != axiom::BodyPointLocation::Outside ||
            !close(outside.value->nearest_boundary->distance,2*std::cos(span/(2*static_cast<double>(intervals))))) return false;
    }
    // A failed support-plane edit must invalidate all three query paths without
    // restoring cached pre-edit sweep mass, and rollback must restore the chain.
    const auto curved = kernel.surfaces().make_sphere({0,0,0},1);
    const auto faces = topo.faces_of_body(*extrude.value);
    if (!curved.value || !faces.value) return false;
    {
        auto txn = kernel.topology().begin_transaction();
        if (txn.replace_surface(faces.value->front(),*curved.value).status != axiom::StatusCode::Ok) return false;
        const auto writes = txn.write_operation_count();
        if (kernel.query().section_detailed(*extrude.value,{{0,0,2},{0,0,1}}).value ||
            kernel.query().min_distance(*extrude.value,*loft.value).value ||
            kernel.query().mass_properties(*extrude.value).value ||
            txn.write_operation_count().value != writes.value || txn.rollback().status != axiom::StatusCode::Ok) return false;
    }
    const auto restored = kernel.query().section_detailed(*extrude.value,{{0,0,2},{0,0,1}});
    return restored.value && close(restored.value->area,4) &&
        kernel.validate().validate_all(*extrude.value,axiom::ValidationMode::Strict).status == axiom::StatusCode::Ok;
}

}  // namespace

int main() {
    if (!rebuilt_boolean_heal_chain_regression()) {
        std::cerr << "rebuilt Boolean to Heal chain regression\n";
        return 1;
    }
    const auto run_stage = [](const char* name, const auto& test) {
        std::cerr << "[stage] " << name << " begin\n";
        const bool passed = test();
        std::cerr << "[stage] " << name << " " << (passed ? "passed" : "failed") << '\n';
        return passed;
    };
    if (!run_stage("stage3_consistency_chain", test_stage3_consistency_chain) ||
        !run_stage("planar_face_thicken", test_planar_face_thicken) ||
        !run_stage("stage3_modeling_failures", test_stage3_modeling_failures) ||
        !run_stage("stage3_model_mass_references", test_stage3_model_mass_references) ||
        !run_stage("stage3_model_query_chain", test_stage3_model_query_chain) ||
        !run_stage("holed_extrusions", test_holed_extrusions) ||
        !run_stage("polyline_sweeps", test_polyline_sweeps) ||
        !run_stage("curve_frame_sweeps", test_curve_frame_sweeps) ||
        !run_stage("closed_spline_sweeps", test_closed_spline_sweeps) ||
        !run_stage("composite_chain_sweeps", test_composite_chain_sweeps) ||
        !run_stage("scaled_rail_sweeps", test_scaled_rail_sweeps) ||
        !run_stage("sweep_scale_laws", test_sweep_scale_laws) ||
        !run_stage("sweep_section_laws", test_sweep_section_laws) ||
        !run_stage("polygon_lofts", test_polygon_lofts) ||
        !run_stage("scaled_extrusions", test_scaled_extrusions) ||
        !run_stage("twisted_extrusions", test_twisted_extrusions) ||
        !run_stage("extrusion_section_laws", test_extrusion_section_laws) ||
        !run_stage("holed_polygon_revolutions", test_holed_polygon_revolutions) ||
        !run_stage("partial_polygon_revolutions", test_partial_polygon_revolutions) ||
        !run_stage("directed_interval_revolutions", test_directed_interval_revolutions) ||
        !run_stage("full_polygon_revolutions", test_full_polygon_revolutions) ||
        !run_stage("extrusions_to_plane", test_extrusions_to_plane)) return 1;
    // Fresh primitive and derived indexing must preserve new and pre-existing
    // adjacency, including cloned source topology and explicit apex topology.
    {
        axiom::Kernel indexed;
        auto query = indexed.topology().query();
        std::vector<axiom::BodyId> bodies;
        for (int i = 0; i < 9; ++i) {
            const axiom::Point3 origin {i * 20.0, 0.0, 0.0};
            const auto body = i % 3 == 0 ? indexed.primitives().box(origin, 3.0, 4.0, 5.0) :
                i % 3 == 1 ? indexed.primitives().wedge(origin, 3.0, 4.0, 5.0) :
                             indexed.primitives().cylinder(origin, {0.0, 0.0, 1.0}, 2.0, 5.0);
            if (!body.value) return 1;
            bodies.push_back(*body.value);
            axiom::BooleanOptions options;
            options.diagnostics = false; // Inspect materialization before optional imprint/repair.
            const auto cloned = indexed.booleans().run(axiom::BooleanOp::Union, bodies.front(), *body.value, options);
            if (!cloned.value) return 1;
            bodies.push_back(cloned.value->output);
            const auto cloned_shells = query.shells_of_body(cloned.value->output);
            if (!cloned_shells.value || cloned_shells.value->size() != (i == 0 ? 1u : 2u)) {
                std::cerr << "cloned union should retain all source shells\n";
                return 1;
            }
            const axiom::ProfileRef profile {"indexed_scaled", {{0,0,0}, {4,0,0}, {0,3,0}}};
            const auto scaled = indexed.sweeps().extrude_scaled(profile, {1,0,3}, 3, {0,0,0},
                                                                 i % 2 == 0 ? 0.0 : 0.5);
            if (!scaled.value) return 1;
            bodies.push_back(*scaled.value);
            if (indexed.topology().validate().validate_indices_consistency().status != axiom::StatusCode::Ok)
                return 1;
            for (const auto existing : bodies) {
                if (indexed.topology().validate().validate_body_topology_indices(existing).status != axiom::StatusCode::Ok)
                    return 1;
                const auto shells = query.shells_of_body(existing);
                const auto faces = query.faces_of_body(existing);
                const auto edges = query.edges_of_body(existing);
                if (!shells.value || shells.value->empty() || !faces.value || faces.value->empty() ||
                    !edges.value || edges.value->empty()) return 1;
                for (const auto shell : *shells.value) {
                    const auto owners = query.bodies_of_shell(shell);
                    if (!owners.value || owners.value->size() != 1 || owners.value->front().value != existing.value)
                        return 1;
                }
                for (const auto face : *faces.value) {
                    const auto face_shells = query.shells_of_face(face);
                    const auto face_bodies = query.bodies_of_face(face);
                    if (!face_shells.value || face_shells.value->size() != 1 ||
                        std::find(shells.value->begin(), shells.value->end(), face_shells.value->front()) ==
                            shells.value->end() || !face_bodies.value ||
                        face_bodies.value->size() != 1 || face_bodies.value->front().value != existing.value)
                        return 1;
                }
                for (const auto edge : *edges.value) {
                    const auto coedges = query.coedges_of_edge(edge);
                    const auto loops = query.loops_of_edge(edge);
                    const auto edge_faces = query.faces_of_edge(edge);
                    const auto edge_shells = query.shells_of_edge(edge);
                    if (!coedges.value || coedges.value->size() != 2 || !loops.value || loops.value->size() != 2 ||
                        !edge_faces.value || edge_faces.value->size() != 2 || !edge_shells.value ||
                        edge_shells.value->size() != 1 ||
                        std::find(shells.value->begin(), shells.value->end(), edge_shells.value->front()) ==
                            shells.value->end()) {
                        std::cerr << "materialized adjacency missing, duplicated or linked to another body\n";
                        return 1;
                    }
                }
            }
        }
    }
    axiom::Kernel kernel;

    auto box_a = kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 10.0, 10.0);
    auto box_b = kernel.primitives().box({30.0, 30.0, 30.0}, 5.0, 5.0, 5.0);
    if (box_a.status != axiom::StatusCode::Ok || box_b.status != axiom::StatusCode::Ok ||
        !box_a.value.has_value() || !box_b.value.has_value()) {
        std::cerr << "failed to create boxes for ops/heal test\n";
        return 1;
    }

    auto wedge = kernel.primitives().wedge({1.0, 2.0, 3.0}, 2.0, 3.0, 4.0);
    if (wedge.status != axiom::StatusCode::Ok || !wedge.value.has_value()) {
        std::cerr << "failed to create wedge for mass_properties test\n";
        return 1;
    }
    auto wedge_props = kernel.query().mass_properties(*wedge.value);
    if (wedge_props.status != axiom::StatusCode::Ok || !wedge_props.value.has_value()) {
        std::cerr << "wedge mass_properties failed\n";
        return 1;
    }
    const auto expected_wedge_volume = 0.5 * 2.0 * 3.0 * 4.0;
    if (std::abs(wedge_props.value->volume - expected_wedge_volume) > 1e-9) {
        std::cerr << "wedge volume should use prism formula not bbox\n";
        return 1;
    }
    const auto expected_wedge_cx = 1.0 + 2.0 / 3.0;
    const auto expected_wedge_cy = 2.0 + 3.0 / 3.0;
    const auto expected_wedge_cz = 3.0 + 2.0;
    if (std::abs(wedge_props.value->centroid.x - expected_wedge_cx) > 1e-9 ||
        std::abs(wedge_props.value->centroid.y - expected_wedge_cy) > 1e-9 ||
        std::abs(wedge_props.value->centroid.z - expected_wedge_cz) > 1e-9) {
        std::cerr << "wedge centroid mismatch\n";
        return 1;
    }
    const auto wdx = 2.0;
    const auto wdy = 3.0;
    const auto wdz = 4.0;
    const auto wm = expected_wedge_volume;
    const auto w_io_xx = wm * wdy * wdy / 6.0 + wm * wdz * wdz / 3.0;
    const auto w_io_yy = wm * wdx * wdx / 6.0 + wm * wdz * wdz / 3.0;
    const auto w_io_zz = wm * (wdx * wdx + wdy * wdy) / 6.0;
    const auto wcx = wdx / 3.0;
    const auto wcy = wdy / 3.0;
    const auto wcz = wdz * 0.5;
    const auto w_ic_xx = w_io_xx - wm * (wcy * wcy + wcz * wcz);
    const auto w_ic_yy = w_io_yy - wm * (wcx * wcx + wcz * wcz);
    const auto w_ic_zz = w_io_zz - wm * (wcx * wcx + wcy * wcy);
    const auto w_ic_xy = -wm * wdx * wdy / 12.0 + wm * wcx * wcy;
    const auto w_ic_xz = -wm * wdx * wdz / 6.0 + wm * wcx * wcz;
    const auto w_ic_yz = -wm * wdy * wdz / 6.0 + wm * wcy * wcz;
    if (std::abs(wedge_props.value->inertia[0] - w_ic_xx) > 1e-5 ||
        std::abs(wedge_props.value->inertia[4] - w_ic_yy) > 1e-5 ||
        std::abs(wedge_props.value->inertia[8] - w_ic_zz) > 1e-5 ||
        std::abs(wedge_props.value->inertia[1] - w_ic_xy) > 1e-5 ||
        std::abs(wedge_props.value->inertia[3] - w_ic_xy) > 1e-5 ||
        std::abs(wedge_props.value->inertia[2] - w_ic_xz) > 1e-5 ||
        std::abs(wedge_props.value->inertia[6] - w_ic_xz) > 1e-5 ||
        std::abs(wedge_props.value->inertia[5] - w_ic_yz) > 1e-5 ||
        std::abs(wedge_props.value->inertia[7] - w_ic_yz) > 1e-5) {
        std::cerr << "wedge inertia tensor about centroid mismatch\n";
        return 1;
    }

    auto box_props = kernel.query().mass_properties(*box_a.value);
    if (box_props.status != axiom::StatusCode::Ok || !box_props.value.has_value()) {
        std::cerr << "box mass_properties failed\n";
        return 1;
    }
    const auto m = 1000.0;
    const auto expected_ixx = m / 12.0 * (10.0 * 10.0 + 10.0 * 10.0);
    if (std::abs(box_props.value->inertia[0] - expected_ixx) > 1e-3 ||
        std::abs(box_props.value->inertia[4] - expected_ixx) > 1e-3 ||
        std::abs(box_props.value->inertia[8] - expected_ixx) > 1e-3) {
        std::cerr << "box principal inertia about centroid unexpected\n";
        return 1;
    }

    auto torus = kernel.primitives().torus({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 4.0, 1.0);
    if (torus.status != axiom::StatusCode::Ok || !torus.value.has_value()) {
        std::cerr << "failed to create torus for mass_properties test\n";
        return 1;
    }
    auto torus_props = kernel.query().mass_properties(*torus.value);
    if (torus_props.status != axiom::StatusCode::Ok || !torus_props.value.has_value()) {
        std::cerr << "torus mass_properties failed\n";
        return 1;
    }
    const auto r_t = 4.0;
    const auto r_m = 1.0;
    const auto m_t = torus_props.value->volume;
    const auto i_ax_expected = m_t * (r_t * r_t + 0.75 * r_m * r_m);
    const auto i_tr_expected = m_t * (0.5 * r_t * r_t + 0.625 * r_m * r_m);
    if (std::abs(torus_props.value->inertia[8] - i_ax_expected) > 1e-3 * std::max(1.0, i_ax_expected) ||
        std::abs(torus_props.value->inertia[0] - i_tr_expected) > 1e-3 * std::max(1.0, i_tr_expected) ||
        std::abs(torus_props.value->inertia[4] - i_tr_expected) > 1e-3 * std::max(1.0, i_tr_expected)) {
        std::cerr << "torus inertia tensor mismatch\n";
        return 1;
    }

    axiom::ProfileRef tri_profile;
    tri_profile.label = "tri_vol";
    tri_profile.polygon_xyz = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 2.0, 0.0}};
    auto tri_extrude = kernel.sweeps().extrude(tri_profile, {0.0, 0.0, 1.0}, 3.0);
    if (tri_extrude.status != axiom::StatusCode::Ok || !tri_extrude.value.has_value()) {
        std::cerr << "triangle extrude failed for mass_properties test\n";
        return 1;
    }
    auto tri_mp = kernel.query().mass_properties(*tri_extrude.value);
    if (tri_mp.status != axiom::StatusCode::Ok || !tri_mp.value.has_value()) {
        std::cerr << "triangle extrude mass_properties failed\n";
        return 1;
    }
    const auto expected_prism_vol = 2.0 * 3.0;
    if (std::abs(tri_mp.value->volume - expected_prism_vol) > 1e-6) {
        std::cerr << "polygon extrude volume should use prism formula not bbox\n";
        return 1;
    }
    const auto expected_prism_area = 4.0 + 12.0 + 6.0 * std::sqrt(2.0);
    if (std::abs(tri_mp.value->area - expected_prism_area) > 1e-4) {
        std::cerr << "polygon extrude surface area mismatch\n";
        return 1;
    }
    if (std::abs(tri_mp.value->centroid.x - 2.0 / 3.0) > 1e-5 ||
        std::abs(tri_mp.value->centroid.y - 2.0 / 3.0) > 1e-5 ||
        std::abs(tri_mp.value->centroid.z - 1.5) > 1e-5) {
        std::cerr << "polygon extrude centroid mismatch\n";
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

    // Historical boolean results own compatibility topology. Provenance and
    // coincident bounds must not manufacture physical mass from their operands.
    const auto unsupported_mass = [&](axiom::BodyId body) {
        const auto result = kernel.query().mass_properties(body);
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        return result.status == axiom::StatusCode::NotImplemented && !result.value && report.value &&
            has_issue_code(*report.value, axiom::diag_codes::kCoreOperationUnsupported) &&
            has_issue_stage(*report.value, "query.mass_properties.support_gate");
    };
    axiom::BooleanOptions bool_opts;
    auto disjoint_union = kernel.booleans().run(axiom::BooleanOp::Union, *box_a.value, *box_b.value, bool_opts);
    if (disjoint_union.status != axiom::StatusCode::Ok || !disjoint_union.value.has_value()) {
        std::cerr << "disjoint union failed\n";
        return 1;
    }
    if (!unsupported_mass(disjoint_union.value->output)) {
        std::cerr << "proxy mass must fail with no value and stable support stage\n";
        return 1;
    }

    auto disjoint_sub = kernel.booleans().run(axiom::BooleanOp::Subtract, *box_a.value, *box_b.value, bool_opts);
    if (disjoint_sub.status != axiom::StatusCode::Ok || !disjoint_sub.value.has_value()) {
        std::cerr << "disjoint subtract failed\n";
        return 1;
    }
    if (!unsupported_mass(disjoint_sub.value->output)) {
        std::cerr << "proxy mass must fail with no value and stable support stage\n";
        return 1;
    }

    // 即使操作数可解析计算，结果代理拓扑也不得继承来源质量。
    auto ball_far = kernel.primitives().sphere({60.0, 5.0, 5.0}, 2.0);
    if (ball_far.status != axiom::StatusCode::Ok || !ball_far.value.has_value()) {
        std::cerr << "sphere for disjoint boolean mass test failed\n";
        return 1;
    }
    auto union_box_sphere =
        kernel.booleans().run(axiom::BooleanOp::Union, *box_a.value, *ball_far.value, bool_opts);
    if (union_box_sphere.status != axiom::StatusCode::Ok || !union_box_sphere.value.has_value()) {
        std::cerr << "disjoint union box+sphere failed\n";
        return 1;
    }
    if (!unsupported_mass(union_box_sphere.value->output)) {
        std::cerr << "proxy mass must fail with no value and stable support stage\n";
        return 1;
    }

    auto sub_sphere_box =
        kernel.booleans().run(axiom::BooleanOp::Subtract, *ball_far.value, *box_a.value, bool_opts);
    if (sub_sphere_box.status != axiom::StatusCode::Ok || !sub_sphere_box.value.has_value()) {
        std::cerr << "disjoint subtract sphere-box failed\n";
        return 1;
    }
    if (!unsupported_mass(sub_sphere_box.value->output)) {
        std::cerr << "proxy mass must fail with no value and stable support stage\n";
        return 1;
    }

    // AABB 面接触也不是实际布尔边界的质量证明。
    auto touch_c0 = kernel.primitives().box({0.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
    auto touch_c1 = kernel.primitives().box({1.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
    if (touch_c0.status != axiom::StatusCode::Ok || touch_c1.status != axiom::StatusCode::Ok ||
        !touch_c0.value.has_value() || !touch_c1.value.has_value()) {
        std::cerr << "touching cubes primitives failed\n";
        return 1;
    }
    auto u_touch = kernel.booleans().run(axiom::BooleanOp::Union, *touch_c0.value, *touch_c1.value, bool_opts);
    if (u_touch.status != axiom::StatusCode::Ok || !u_touch.value.has_value()) {
        std::cerr << "touching cubes union failed\n";
        return 1;
    }
    if (!unsupported_mass(u_touch.value->output)) {
        std::cerr << "proxy mass must fail with no value and stable support stage\n";
        return 1;
    }

    // 包含关系与相同 bbox 也不能证明结果边界。
    auto big_h = kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 10.0, 10.0);
    auto small_h = kernel.primitives().box({4.0, 4.0, 4.0}, 2.0, 2.0, 2.0);
    if (big_h.status != axiom::StatusCode::Ok || small_h.status != axiom::StatusCode::Ok ||
        !big_h.value.has_value() || !small_h.value.has_value()) {
        std::cerr << "contain-test boxes failed\n";
        return 1;
    }
    auto u_hole = kernel.booleans().run(axiom::BooleanOp::Union, *big_h.value, *small_h.value, bool_opts);
    if (u_hole.status != axiom::StatusCode::Ok || !u_hole.value.has_value()) {
        std::cerr << "union big contains small failed\n";
        return 1;
    }
    if (!unsupported_mass(u_hole.value->output)) {
        std::cerr << "proxy mass must fail with no value and stable support stage\n";
        return 1;
    }

    // 占位 Sweep 来源不能使结果质量成为受支持能力。
    auto sw_u = kernel.sweeps().extrude({"bool_sw"}, {0.0, 0.0, 1.0}, 3.0);
    auto far_cube = kernel.primitives().box({80.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
    if (sw_u.status != axiom::StatusCode::Ok || far_cube.status != axiom::StatusCode::Ok ||
        !sw_u.value.has_value() || !far_cube.value.has_value()) {
        std::cerr << "extrude/far cube for boolean mass failed\n";
        return 1;
    }
    auto u_sw_box = kernel.booleans().run(axiom::BooleanOp::Union, *sw_u.value, *far_cube.value, bool_opts);
    if (u_sw_box.status != axiom::StatusCode::Ok || !u_sw_box.value.has_value()) {
        std::cerr << "union extrude+box failed\n";
        return 1;
    }
    if (!unsupported_mass(u_sw_box.value->output)) {
        std::cerr << "proxy mass must fail with no value and stable support stage\n";
        return 1;
    }

    // 相交 bbox 不得冒充实际结果体。
    auto overlap_box = kernel.primitives().box({5.0, 0.0, 0.0}, 5.0, 5.0, 5.0);
    if (overlap_box.status != axiom::StatusCode::Ok || !overlap_box.value.has_value()) {
        std::cerr << "overlap box for intersect mass test failed\n";
        return 1;
    }
    auto inter_ab =
        kernel.booleans().run(axiom::BooleanOp::Intersect, *box_a.value, *overlap_box.value, bool_opts);
    if (inter_ab.status != axiom::StatusCode::Ok || !inter_ab.value.has_value()) {
        std::cerr << "box intersect for mass_properties failed\n";
        return 1;
    }
    if (!unsupported_mass(inter_ab.value->output)) {
        std::cerr << "proxy mass must fail with no value and stable support stage\n";
        return 1;
    }

    // 嵌套减法不得以来源差分冒充实际空腔拓扑。
    auto shell_outer = kernel.primitives().box({0.0, 0.0, 0.0}, 6.0, 6.0, 6.0);
    auto shell_inner = kernel.primitives().box({2.0, 2.0, 2.0}, 2.0, 2.0, 2.0);
    if (shell_outer.status != axiom::StatusCode::Ok || shell_inner.status != axiom::StatusCode::Ok ||
        !shell_outer.value.has_value() || !shell_inner.value.has_value()) {
        std::cerr << "nested boxes for subtract mass failed\n";
        return 1;
    }
    auto sub_nested =
        kernel.booleans().run(axiom::BooleanOp::Subtract, *shell_outer.value, *shell_inner.value, bool_opts);
    if (sub_nested.status != axiom::StatusCode::Ok || !sub_nested.value.has_value()) {
        std::cerr << "subtract nested boxes failed\n";
        return 1;
    }
    if (!unsupported_mass(sub_nested.value->output)) {
        std::cerr << "proxy mass must fail with no value and stable support stage\n";
        return 1;
    }

    auto bad_offset = kernel.modify().offset_body(*box_a.value, -6.0, {});
    if (bad_offset.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "expected failed inward offset\n";
        return 1;
    }
    auto bad_offset_diag = kernel.diagnostics().get(bad_offset.diagnostic_id);
    if (bad_offset_diag.status != axiom::StatusCode::Ok || !bad_offset_diag.value.has_value() ||
        !has_issue_stage(*bad_offset_diag.value, "modify.offset.self_intersection")) {
        std::cerr << "expected staged diagnostic for failed inward offset\n";
        return 1;
    }
    auto shell_too_thick = kernel.modify().shell_body(*box_a.value, {}, 4.9999995);
    if (shell_too_thick.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "expected failed shell for tolerance-near thick wall\n";
        return 1;
    }
    auto shell_thick_diag = kernel.diagnostics().get(shell_too_thick.diagnostic_id);
    if (shell_thick_diag.status != axiom::StatusCode::Ok || !shell_thick_diag.value.has_value() ||
        !has_issue_stage(*shell_thick_diag.value, "modify.shell.cavity_tolerance")) {
        std::cerr << "expected staged diagnostic for shell cavity tolerance failure\n";
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
    if (!unsupported_mass(*sweep_ok.value)) {
        std::cerr << "proxy mass must fail with no value and stable support stage\n";
        return 1;
    }

    // 子午面多边形 revolve：半周按 24 段生成真实三角化闭壳 BRep，并积分质量属性。
    axiom::ProfileRef tri_meridian;
    tri_meridian.label = "tri_meridian";
    tri_meridian.polygon_xyz = {{1.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, {1.0, 0.0, 2.0}};
    const axiom::Axis3 axis_z {{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
    const double pi = 3.14159265358979323846;
    auto rev_mer = kernel.sweeps().revolve(tri_meridian, axis_z, pi);
    if (rev_mer.status != axiom::StatusCode::Ok || !rev_mer.value.has_value()) {
        std::cerr << "meridian polygon revolve failed\n";
        return 1;
    }
    auto rev_mer_strict = kernel.validate().validate_topology(*rev_mer.value, axiom::ValidationMode::Strict);
    if (rev_mer_strict.status != axiom::StatusCode::Ok) {
        std::cerr << "meridian revolve failed strict topology validation\n";
        return 1;
    }
    auto rev_mer_all = kernel.validate().validate_all(*rev_mer.value, axiom::ValidationMode::Strict);
    if (rev_mer_all.status != axiom::StatusCode::Ok) {
        std::cerr << "meridian revolve should pass strict validate_all\n";
        return 1;
    }
    auto rev_mer_shells = kernel.topology().query().shells_of_body(*rev_mer.value);
    if (rev_mer_shells.status != axiom::StatusCode::Ok || !rev_mer_shells.value.has_value() ||
        rev_mer_shells.value->size() != 1) {
        std::cerr << "meridian revolve expected one shell\n";
        return 1;
    }
    auto rev_mer_faces = kernel.topology().query().faces_of_shell(rev_mer_shells.value->front());
    if (rev_mer_faces.status != axiom::StatusCode::Ok || !rev_mer_faces.value.has_value() ||
        rev_mer_faces.value->size() != 146) {
        std::cerr << "meridian revolve expected sampled lateral triangles and two caps\n";
        return 1;
    }
    auto rev_mer_mp = kernel.query().mass_properties(*rev_mer.value);
    if (rev_mer_mp.status != axiom::StatusCode::Ok || !rev_mer_mp.value.has_value()) {
        std::cerr << "meridian revolve mass_properties failed\n";
        return 1;
    }
    const double area_m = 2.0;
    const double r_bar_m = 5.0 / 3.0;
    const double pappus_vol_m = area_m * r_bar_m * pi;
    if (std::abs(rev_mer_mp.value->volume - pappus_vol_m) > 0.25) {
        std::cerr << "meridian revolve polyhedral volume should be near Pappus analytic volume\n";
        return 1;
    }
    if (rev_mer_mp.value->area <= 0.0 || rev_mer_mp.value->inertia[0] <= 0.0 ||
        rev_mer_mp.value->inertia[4] <= 0.0 || rev_mer_mp.value->inertia[8] <= 0.0) {
        std::cerr << "meridian revolve should report positive area and principal inertia diagonals\n";
        return 1;
    }

    // 非子午面（轮廓平面不含旋转轴）不得伪装成 bbox 壳。
    axiom::ProfileRef tri_xy;
    tri_xy.label = "tri_xy";
    tri_xy.polygon_xyz = {{1.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, {1.0, 2.0, 0.0}};
    auto rev_xy = kernel.sweeps().revolve(tri_xy, axis_z, pi);
    if (rev_xy.status != axiom::StatusCode::InvalidInput || rev_xy.value.has_value()) {
        std::cerr << "non-meridian revolve should be rejected instead of returning bbox topology\n";
        return 1;
    }

    std::array<axiom::ProfileRef, 2> loft_profiles {};
    loft_profiles[0].label = "lof1";
    loft_profiles[1].label = "lof2";
    auto loft_body = kernel.sweeps().loft(std::span<const axiom::ProfileRef>(loft_profiles));
    if (loft_body.status != axiom::StatusCode::InvalidInput || loft_body.value.has_value()) {
        std::cerr << "label-only loft must not return bbox placeholder topology\n";
        return 1;
    }

    // Stage-2 minimal: 矩形拉伸物化为三角剖分棱柱 BRep（顶/底各 2 三角 + 侧面 4×2，共 12 面；非工业「合并共面」语义）。
    axiom::ProfileRef rect;
    rect.label = "rect";
    rect.polygon_xyz = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {2.0, 1.0, 0.0}, {0.0, 1.0, 0.0}};
    auto prism = kernel.sweeps().extrude(rect, {0.0, 0.0, 1.0}, 3.0);
    if (prism.status != axiom::StatusCode::Ok || !prism.value.has_value()) {
        std::cerr << "polygon extrude failed\n";
        return 1;
    }
    auto prism_strict = kernel.validate().validate_topology(*prism.value, axiom::ValidationMode::Strict);
    if (prism_strict.status != axiom::StatusCode::Ok) {
        std::cerr << "polygon extrude failed strict topology validation\n";
        return 1;
    }
    auto prism_strict_all = kernel.validate().validate_all(*prism.value, axiom::ValidationMode::Strict);
    if (prism_strict_all.status != axiom::StatusCode::Ok) {
        std::cerr << "prism should pass strict validate_all including surface domain checks\n";
        return 1;
    }
    auto prism_manifold_std = kernel.validate().validate_manifold(*prism.value, axiom::ValidationMode::Standard);
    auto prism_manifold_strict = kernel.validate().validate_manifold(*prism.value, axiom::ValidationMode::Strict);
    if (prism_manifold_std.status != axiom::StatusCode::Ok || prism_manifold_strict.status != axiom::StatusCode::Ok) {
        std::cerr << "prism should pass validate_manifold Standard and Strict\n";
        return 1;
    }
    auto prism_shells = kernel.topology().query().shells_of_body(*prism.value);
    if (prism_shells.status != axiom::StatusCode::Ok || !prism_shells.value.has_value() || prism_shells.value->size() != 1) {
        std::cerr << "polygon extrude expected one shell\n";
        return 1;
    }
    auto prism_faces = kernel.topology().query().faces_of_shell(prism_shells.value->front());
    if (prism_faces.status != axiom::StatusCode::Ok || !prism_faces.value.has_value() || prism_faces.value->size() != 12) {
        std::cerr << "polygon rect extrude expected 12 triangulated prism faces\n";
        return 1;
    }
    const auto prism_edges = kernel.topology().query().edges_of_body(*prism.value);
    const auto prism_vertices = kernel.topology().query().vertices_of_body(*prism.value);
    const auto prism_owned = kernel.topology().query().has_body(*prism.value);
    if (!prism_edges.value || prism_edges.value->size() != 18 ||
        !prism_vertices.value || prism_vertices.value->size() != 8 ||
        !prism_owned.value || !*prism_owned.value) {
        std::cerr << "polygon rect extrude expected a queryable prism body with 18 edges and 8 vertices\n";
        return 1;
    }
    const auto objects_before_bad_profile = kernel.object_count_total();
    const auto bodies_before_bad_profile = kernel.body_count();
    const auto next_id_before_bad_profile = kernel.next_object_id();
    if (!objects_before_bad_profile.value || !bodies_before_bad_profile.value ||
        !next_id_before_bad_profile.value) return 1;
    const std::vector<std::vector<axiom::Point3>> invalid_extrude_profiles {
        {{0, 0, 0}, {4, 0, 0}, {0, 3, 0}, {3, 3, 0}, {1, -1, 0}}, // crossing, nonzero signed area
        {{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {2, 0, 0}, {0, 4, 0}}, // vertex on nonadjacent edge
        {{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {2, 2, 0}, {0, 4, 0}, {2, 2, 0}}, // repeated interior vertex
        {{0, 0, 0}, {2, 2, 0}, {0, 2, 0}, {2, 0, 0}}, // self intersecting
        {{0, 0, 0}, {2, 0, 0}, {2, 1, 0.1}, {0, 1, 0}},  // nonplanar
        {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}, // collinear corner
        {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}, // zero area
        {{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}, {0, 0, 0}}, // repeated endpoint
        {{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, std::numeric_limits<double>::infinity(), 0}}
    };
    for (const auto& points : invalid_extrude_profiles) {
        axiom::ProfileRef bad_profile {"invalid", points};
        const auto rejected = kernel.sweeps().extrude(bad_profile, {0, 0, 1}, 3);
        const auto diagnostic = kernel.diagnostics().get(rejected.diagnostic_id);
        const auto objects_after = kernel.object_count_total();
        const auto bodies_after = kernel.body_count();
        const auto next_id_after = kernel.next_object_id();
        if (rejected.status != axiom::StatusCode::InvalidInput || rejected.value ||
            !diagnostic.value || !has_issue_code(*diagnostic.value, axiom::diag_codes::kCoreParameterOutOfRange) ||
            !objects_after.value || *objects_after.value != *objects_before_bad_profile.value ||
            !bodies_after.value || *bodies_after.value != *bodies_before_bad_profile.value ||
            !next_id_after.value || *next_id_after.value != *next_id_before_bad_profile.value) {
            std::cerr << "invalid polygon extrusion should fail without materializing topology or body\n";
            return 1;
        }
    }
    const auto parallel_extrude = kernel.sweeps().extrude(rect, {1, 0, 0}, 3);
    const auto objects_after_parallel = kernel.object_count_total();
    if (parallel_extrude.status != axiom::StatusCode::InvalidInput ||
        !objects_after_parallel.value || *objects_after_parallel.value != *objects_before_bad_profile.value) {
        std::cerr << "parallel polygon extrusion should not materialize a bbox shell\n";
        return 1;
    }
    auto reversed_rect = rect;
    std::reverse(reversed_rect.polygon_xyz.begin(), reversed_rect.polygon_xyz.end());
    const auto reversed_prism = kernel.sweeps().extrude(reversed_rect, {0, 0, 1}, 3);
    const auto reversed_valid = reversed_prism.value ?
        kernel.validate().validate_all(*reversed_prism.value, axiom::ValidationMode::Strict) :
        axiom::Result<void> {};
    const auto reversed_faces = reversed_prism.value ?
        kernel.topology().query().faces_of_body(*reversed_prism.value) :
        axiom::Result<std::vector<axiom::FaceId>> {};
    if (reversed_prism.status != axiom::StatusCode::Ok || !reversed_prism.value ||
        reversed_valid.status != axiom::StatusCode::Ok ||
        !reversed_faces.value || reversed_faces.value->size() != 12) {
        std::cerr << "clockwise polygon extrusion should still create a valid triangulated prism\n";
        return 1;
    }

    // Exact concave prism model set: independent rectangle-union area/centroid
    // oracles catch caps that fill the notch (a fan can still look like a closed shell).
    const std::vector<axiom::ProfileRef> concave_profiles {
        {"L", {{0, 0, 0}, {3, 0, 0}, {3, 1, 0}, {1, 1, 0}, {1, 3, 0}, {0, 3, 0}}},
        {"U", {{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {3, 3, 0}, {3, 1, 0}, {1, 1, 0}, {1, 3, 0}, {0, 3, 0}}}
    };
    for (std::size_t shape = 0; shape < concave_profiles.size(); ++shape) {
        const double cap_area = shape == 0 ? 5.0 : 8.0;
        const axiom::Point3 cap_centroid = shape == 0 ? axiom::Point3 {1.1, 1.1, 0} :
                                                                      axiom::Point3 {2, 1.25, 0};
        const double x_edge_length = shape == 0 ? 6.0 : 8.0;
        const double y_edge_length = shape == 0 ? 6.0 : 10.0;
        for (const bool tilted : {false, true}) {
            // An orthonormal frame exercises a genuinely oblique profile plane.
            const auto rotate = [tilted](axiom::Point3 p) -> axiom::Point3 {
                if (!tilted) return p;
                return {0.6 * p.x - 0.48 * p.y + 0.64 * p.z,
                        0.8 * p.x + 0.36 * p.y - 0.48 * p.z, 0.8 * p.y + 0.6 * p.z};
            };
            const auto world = [&](axiom::Point3 p) -> axiom::Point3 {
                const auto q = rotate(p);
                return {q.x + 10, q.y - 20, q.z + 30};
            };
            for (const bool clockwise : {false, true}) {
                for (const bool shift_start : {false, true}) {
                    for (const axiom::Point3 delta : {axiom::Point3 {0, 0, 3}, {2, -1, 3}, {-2, 1, -3}}) {
                        auto profile = concave_profiles[shape];
                        if (clockwise) std::reverse(profile.polygon_xyz.begin(), profile.polygon_xyz.end());
                        if (shift_start) std::rotate(profile.polygon_xyz.begin(), profile.polygon_xyz.begin() + 3,
                                                     profile.polygon_xyz.end());
                        for (auto& p : profile.polygon_xyz) p = world(p);
                        const auto d = rotate(delta);
                        const double length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                        const auto body = kernel.sweeps().extrude(profile, {d.x, d.y, d.z}, length);
                        if (body.status != axiom::StatusCode::Ok || !body.value) {
                            std::cerr << "concave extrusion model set failed to materialize\n";
                            return 1;
                        }
                        const auto query = kernel.topology().query();
                        const auto shells = query.shells_of_body(*body.value);
                        const auto faces = query.faces_of_body(*body.value);
                        const auto edges = query.edges_of_body(*body.value);
                        const auto vertices = query.vertices_of_body(*body.value);
                        const auto owned = query.has_body(*body.value);
                        const auto mass = kernel.query().mass_properties(*body.value);
                        const auto expected_cm = world({cap_centroid.x + delta.x / 2,
                                                       cap_centroid.y + delta.y / 2, delta.z / 2});
                        const double area = 2 * cap_area + x_edge_length * std::hypot(delta.y, delta.z) +
                                                         y_edge_length * std::hypot(delta.x, delta.z);
                        const auto n = profile.polygon_xyz.size();
                        if (!owned.value || !*owned.value || !shells.value || shells.value->size() != 1 ||
                            !faces.value || faces.value->size() != 4 * n - 4 ||
                            !edges.value || edges.value->size() != 6 * n - 6 ||
                            !vertices.value || vertices.value->size() != 2 * n || !mass.value ||
                            std::abs(mass.value->volume - 3 * cap_area) > 1e-8 ||
                            std::abs(mass.value->area - area) > 1e-8 ||
                            std::abs(mass.value->centroid.x - expected_cm.x) > 1e-8 ||
                            std::abs(mass.value->centroid.y - expected_cm.y) > 1e-8 ||
                            std::abs(mass.value->centroid.z - expected_cm.z) > 1e-8 ||
                            kernel.validate().validate_all(*body.value, axiom::ValidationMode::Strict).status !=
                                axiom::StatusCode::Ok) {
                            std::cerr << "concave extrusion must preserve notch, mass and strict topology\n";
                            return 1;
                        }
                        double actual_area = 0;
                        for (const auto face : *faces.value) {
                            const auto face_area = query.planar_face_area(face);
                            const auto owners = query.bodies_of_face(face);
                            const auto loops = query.loops_of_face(face);
                            if (!face_area.value || *face_area.value <= 0 || !owners.value || owners.value->size() != 1 ||
                                owners.value->front().value != body.value->value || !loops.value || loops.value->size() != 1) return 1;
                            const auto loop_vertices = query.vertices_of_loop(loops.value->front());
                            if (!loop_vertices.value || loop_vertices.value->size() != 3) return 1;
                            actual_area += *face_area.value;
                        }
                        for (const auto edge : *edges.value) {
                            const auto coedges = query.coedge_count_of_edge(edge);
                            const auto endpoints = query.vertices_of_edge(edge);
                            if (!coedges.value || *coedges.value != 2 || !endpoints.value ||
                                (*endpoints.value)[0].value == (*endpoints.value)[1].value) return 1;
                        }
                        if (std::abs(actual_area - area) > 1e-8) return 1;
                        const auto mesh = kernel.convert().brep_to_mesh(*body.value, {});
                        if (mesh.status != axiom::StatusCode::Ok || !mesh.value) return 1;
                        const auto inspection = kernel.convert().inspect_mesh(*mesh.value);
                        if (!inspection.value || inspection.value->tessellation_strategy != "owned_topo_welded" ||
                            inspection.value->triangle_count != faces.value->size() ||
                            inspection.value->vertex_count != 2 * n || inspection.value->connected_components != 1 ||
                            inspection.value->has_out_of_range_indices || inspection.value->has_degenerate_triangles) {
                            std::cerr << "concave extrusion mesh conversion must use the real closed prism\n";
                            if (inspection.value) {
                                std::cerr << "profile=" << profile.label << " tilted=" << tilted
                                          << " clockwise=" << clockwise << " shift_start=" << shift_start
                                          << " strategy=" << inspection.value->tessellation_strategy
                                          << " vertices=" << inspection.value->vertex_count
                                          << " triangles=" << inspection.value->triangle_count
                                          << " components=" << inspection.value->connected_components << '\n';
                            }
                            return 1;
                        }
                        if (!tilted && !clockwise && !shift_start && delta.x == 0) {
                            // Default planar tessellation must weld shared positions; optional
                            // face-local UVs intentionally retain texture seams at those positions.
                            axiom::TessellationOptions uv_options;
                            uv_options.generate_texcoords = true;
                            const auto uv_mesh = kernel.convert().brep_to_mesh(*body.value, uv_options);
                            if (uv_mesh.status != axiom::StatusCode::Ok || !uv_mesh.value) return 1;
                            const auto uv_inspection = kernel.convert().inspect_mesh(*uv_mesh.value);
                            if (!uv_inspection.value || uv_mesh.value->value == mesh.value->value ||
                                uv_inspection.value->tessellation_strategy != "owned_topo_welded" ||
                                uv_inspection.value->triangle_count != faces.value->size() ||
                                uv_inspection.value->vertex_count <= 2 * n ||
                                uv_inspection.value->has_out_of_range_indices ||
                                uv_inspection.value->has_degenerate_triangles) {
                                std::cerr << "concave prism UV conversion must preserve texture seams\n";
                                return 1;
                            }
                            const auto report_path = std::filesystem::temp_directory_path() /
                                ("axiom_ops_concave_" + profile.label + "_mesh.json");
                            for (const bool with_uv : {false, true}) {
                                const auto mesh_id = with_uv ? *uv_mesh.value : *mesh.value;
                                if (kernel.convert().export_mesh_report_json(mesh_id, report_path.string()).status !=
                                    axiom::StatusCode::Ok) return 1;
                                std::ifstream in {report_path};
                                const std::string report((std::istreambuf_iterator<char>(in)),
                                                         std::istreambuf_iterator<char>());
                                in.close();
                                std::filesystem::remove(report_path);
                                const std::string expected = with_uv ? "\"has_texcoords\":true" :
                                                                      "\"has_texcoords\":false";
                                if (report.find(expected) == std::string::npos) {
                                    std::cerr << "planar tessellation must honor generate_texcoords\n";
                                    return 1;
                                }
                            }
                            const auto cached_mesh = kernel.convert().brep_to_mesh(*body.value, {});
                            if (!cached_mesh.value || cached_mesh.value->value != mesh.value->value) return 1;
                        }
                    }
                }
            }
        }
        // Line-segment sweep must inherit the same cap materialization, including the notch.
        const auto rail = kernel.curves().make_line_segment({50, 60, 70}, {50, 60, 73});
        if (!rail.value) return 1;
        const auto swept = kernel.sweeps().sweep(concave_profiles[shape], *rail.value);
        if (!swept.value || kernel.validate().validate_all(*swept.value, axiom::ValidationMode::Strict).status !=
                                axiom::StatusCode::Ok) return 1;
        const auto mass = kernel.query().mass_properties(*swept.value);
        if (!mass.value || std::abs(mass.value->volume - 3 * cap_area) > 1e-8 ||
            std::abs(mass.value->area - (2 * cap_area + 3 * (x_edge_length + y_edge_length))) > 1e-8) return 1;
    }

    // Linear sweep: the rail supplies only displacement; the world-space profile
    // stays at its input location. Exercise both windings, rail signs and planes.
    for (const bool yz_plane : {false, true}) {
        for (const bool clockwise : {false, true}) {
            for (const double sign : {-1.0, 1.0}) {
                axiom::ProfileRef sweep_profile {"linear_sweep",
                    {{10, 20, 30}, {12, 20, 30}, {10, 23, 30}}};
                axiom::Vec3 delta {2 * sign, -sign, 4 * sign};
                axiom::Point3 expected_centroid {10 + 2.0 / 3.0 + sign, 21 - sign / 2, 30 + 2 * sign};
                if (yz_plane) {
                    for (auto& p : sweep_profile.polygon_xyz) p = {p.z, p.x, p.y};
                    delta = {delta.z, delta.x, delta.y};
                    expected_centroid = {expected_centroid.z, expected_centroid.x, expected_centroid.y};
                }
                if (clockwise) std::reverse(sweep_profile.polygon_xyz.begin(), sweep_profile.polygon_xyz.end());
                const auto source_points = sweep_profile.polygon_xyz;
                const auto rail = kernel.curves().make_line_segment({100, 200, 300},
                    {100 + delta.x, 200 + delta.y, 300 + delta.z});
                if (!rail.value) return 1;
                const auto swept = kernel.sweeps().sweep(sweep_profile, *rail.value);
                if (swept.status != axiom::StatusCode::Ok || !swept.value) return 1;
                const auto valid = kernel.validate().validate_all(*swept.value, axiom::ValidationMode::Strict);
                const auto query = kernel.topology().query();
                const auto owned = query.has_body(*swept.value);
                const auto shells = query.shells_of_body(*swept.value);
                const auto faces = query.faces_of_body(*swept.value);
                const auto edges = query.edges_of_body(*swept.value);
                const auto vertices = query.vertices_of_body(*swept.value);
                const auto mass = kernel.query().mass_properties(*swept.value);
                const double expected_area = 6 + 2 * std::sqrt(17.0) + 3 * std::sqrt(20.0) + std::sqrt(224.0);
                if (valid.status != axiom::StatusCode::Ok || !owned.value || !*owned.value ||
                    !shells.value || shells.value->size() != 1 || !faces.value || faces.value->size() != 8 ||
                    !edges.value || edges.value->size() != 12 || !vertices.value || vertices.value->size() != 6 ||
                    !mass.value || std::abs(mass.value->volume - 12) > 1e-8 ||
                    std::abs(mass.value->area - expected_area) > 1e-8 ||
                    std::abs(mass.value->centroid.x - expected_centroid.x) > 1e-8 ||
                    std::abs(mass.value->centroid.y - expected_centroid.y) > 1e-8 ||
                    std::abs(mass.value->centroid.z - expected_centroid.z) > 1e-8) {
                    std::cerr << "linear sweep must create the actual oblique triangular prism\n";
                    return 1;
                }
                double topology_area = 0;
                for (const auto face : *faces.value) {
                    const auto area = query.planar_face_area(face);
                    if (!area.value || *area.value <= 0) return 1;
                    topology_area += *area.value;
                }
                for (const auto edge : *edges.value) {
                    const auto coedges = query.coedge_count_of_edge(edge);
                    if (!coedges.value || *coedges.value != 2) return 1;
                }
                if (std::abs(topology_area - expected_area) > 1e-8) {
                    std::cerr << "linear sweep faces must match analytic prism surface area\n";
                    return 1;
                }
                const auto bbox = query.bbox_of_body_from_topology(*swept.value);
                axiom::Point3 lo = source_points.front();
                axiom::Point3 hi = lo;
                for (const auto& p : source_points) {
                    for (const auto& q : {p, axiom::Point3 {p.x + delta.x, p.y + delta.y, p.z + delta.z}}) {
                        lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)};
                        hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
                    }
                }
                if (!bbox.value || !bbox.value->is_valid ||
                    std::abs(bbox.value->min.x - lo.x) > 1e-8 || std::abs(bbox.value->max.x - hi.x) > 1e-8 ||
                    std::abs(bbox.value->min.y - lo.y) > 1e-8 || std::abs(bbox.value->max.y - hi.y) > 1e-8 ||
                    std::abs(bbox.value->min.z - lo.z) > 1e-8 || std::abs(bbox.value->max.z - hi.z) > 1e-8) return 1;
            }
        }
    }
    const auto sweep_rail = kernel.curves().make_line_segment({0, 0, 0}, {0, 0, 3});
    const auto parallel_rail = kernel.curves().make_line_segment({0, 0, 0}, {3, 0, 0});
    const auto curved_rail = kernel.curves().make_circle({0, 0, 0}, {0, 0, 1}, 3);
    const auto unbounded_rail = kernel.curves().make_line({0, 0, 0}, {0, 0, 1});
    const auto huge = std::numeric_limits<double>::max();
    const auto overflow_rail = kernel.curves().make_line_segment({0, 0, -huge}, {0, 0, huge});
    if (!sweep_rail.value || !parallel_rail.value || !curved_rail.value ||
        !unbounded_rail.value || !overflow_rail.value) return 1;
    const auto sweep_objects_before = kernel.object_count_total();
    const auto sweep_bodies_before = kernel.body_count();
    const auto sweep_next_before = kernel.next_object_id();
    if (!sweep_objects_before.value || !sweep_bodies_before.value || !sweep_next_before.value) return 1;
    const auto rejected_sweep = [&](const axiom::ProfileRef& profile, axiom::CurveId rail, std::string_view code) {
        const auto result = kernel.sweeps().sweep(profile, rail);
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto objects = kernel.object_count_total();
        const auto bodies = kernel.body_count();
        const auto next = kernel.next_object_id();
        return result.status == axiom::StatusCode::InvalidInput && !result.value &&
            diagnostic.value && has_issue_code(*diagnostic.value, code) &&
            objects.value && *objects.value == *sweep_objects_before.value &&
            bodies.value && *bodies.value == *sweep_bodies_before.value &&
            next.value && *next.value == *sweep_next_before.value;
    };
    for (const auto& points : invalid_extrude_profiles) {
        if (!rejected_sweep({"invalid", points}, *sweep_rail.value, axiom::diag_codes::kCoreParameterOutOfRange)) return 1;
    }
    for (const auto rail : {*parallel_rail.value, *curved_rail.value, *unbounded_rail.value, *overflow_rail.value}) {
        if (!rejected_sweep(rect, rail, axiom::diag_codes::kCoreParameterOutOfRange)) return 1;
    }
    if (!rejected_sweep({"short", {{0, 0, 0}, {1, 0, 0}}}, *sweep_rail.value,
                         axiom::diag_codes::kCoreParameterOutOfRange) ||
        !rejected_sweep({"nan", {{0, 0, 0}, {1, 0, 0}, {0, std::numeric_limits<double>::quiet_NaN(), 0}}},
                         *sweep_rail.value, axiom::diag_codes::kCoreParameterOutOfRange) ||
        !rejected_sweep({"", rect.polygon_xyz}, *sweep_rail.value, axiom::diag_codes::kCoreInvalidHandle) ||
        !rejected_sweep(rect, {}, axiom::diag_codes::kCoreInvalidHandle)) return 1;
    const auto sweep_retry = kernel.sweeps().sweep(rect, *sweep_rail.value);
    if (!sweep_retry.value ||
        kernel.validate().validate_all(*sweep_retry.value, axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
        kernel.validate().validate_all(*prism.value, axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) return 1;

    const auto concave_objects_before = kernel.object_count_total();
    const auto concave_bodies_before = kernel.body_count();
    const auto concave_next_before = kernel.next_object_id();
    if (!concave_objects_before.value || !concave_bodies_before.value || !concave_next_before.value) return 1;
    const auto rejects_concave = [&](const axiom::ProfileRef& profile, axiom::Vec3 direction, double distance) {
        const auto result = kernel.sweeps().extrude(profile, direction, distance);
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const auto objects = kernel.object_count_total();
        const auto bodies = kernel.body_count();
        const auto next = kernel.next_object_id();
        return result.status == axiom::StatusCode::InvalidInput && !result.value && diagnostic.value &&
            has_issue_code(*diagnostic.value, axiom::diag_codes::kCoreParameterOutOfRange) &&
            objects.value && *objects.value == *concave_objects_before.value &&
            bodies.value && *bodies.value == *concave_bodies_before.value &&
            next.value && *next.value == *concave_next_before.value;
    };
    for (const auto& points : invalid_extrude_profiles) {
        for (const bool clockwise : {false, true}) {
            axiom::ProfileRef invalid {"invalid_concave", points};
            if (clockwise) std::reverse(invalid.polygon_xyz.begin(), invalid.polygon_xyz.end());
            if (!rejects_concave(invalid, {0, 0, 1}, 3)) return 1;
        }
    }
    for (const auto direction : {axiom::Vec3 {1, 0, 0}, {0, 0, 0},
                                {0, 0, std::numeric_limits<double>::quiet_NaN()}}) {
        if (!rejects_concave(concave_profiles.front(), direction, 3)) return 1;
    }
    for (const auto distance : {0.0, -1.0, 1e-20, std::numeric_limits<double>::infinity()}) {
        if (!rejects_concave(concave_profiles.front(), {0, 0, 1}, distance)) return 1;
    }
    // The requested offset rounds away in world coordinates: rejection occurs
    // in materialization after cap triangulation, still before any ID allocation.
    auto collapsed_concave = concave_profiles.front();
    for (auto& p : collapsed_concave.polygon_xyz) p.z = 1e16;
    if (!rejects_concave(collapsed_concave, {0, 0, 1}, 1)) return 1;
    const auto concave_retry = kernel.sweeps().extrude(concave_profiles.back(), {0, 0, -1}, 3);
    if (!concave_retry.value ||
        kernel.validate().validate_all(*concave_retry.value, axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
        kernel.validate().validate_all(*prism.value, axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) return 1;

    auto box_edges = kernel.topology().query().edges_of_body(*box_a.value);
    if (box_edges.status != axiom::StatusCode::Ok || !box_edges.value.has_value() ||
        box_edges.value->empty()) {
        std::cerr << "expected edges on box for fillet/chamfer test\n";
        return 1;
    }
    std::vector<axiom::EdgeId> single_edge {box_edges.value->front()};
    auto fillet_ok = kernel.blends().fillet_edges(*box_a.value, single_edge, 0.5);
    if (fillet_ok.status != axiom::StatusCode::Ok || !fillet_ok.value.has_value()) {
        std::cerr << "fillet_edges failed\n";
        return 1;
    }
    auto fillet_ok_diag = kernel.diagnostics().get(fillet_ok.value->diagnostic_id);
    if (fillet_ok_diag.status != axiom::StatusCode::Ok || !fillet_ok_diag.value.has_value() ||
        !has_issue_stage(*fillet_ok_diag.value, "blend.fillet.placeholder")) {
        std::cerr << "expected blend.fillet.placeholder staged diagnostic for fillet\n";
        return 1;
    }
    if (!has_warning_code(fillet_ok.value->warnings, axiom::diag_codes::kBlendApproximatePlaceholder)) {
        std::cerr << "expected fillet placeholder capability warning\n";
        return 1;
    }
    auto chamfer_ok = kernel.blends().chamfer_edges(*box_a.value, single_edge, 0.3);
    if (chamfer_ok.status != axiom::StatusCode::Ok || !chamfer_ok.value.has_value()) {
        std::cerr << "chamfer_edges failed\n";
        return 1;
    }
    if (!has_warning_code(chamfer_ok.value->warnings, axiom::diag_codes::kBlendApproximatePlaceholder)) {
        std::cerr << "expected chamfer placeholder capability warning\n";
        return 1;
    }

    if (box_edges.value->size() < 2) {
        std::cerr << "expected at least two edges for multi-edge blend diagnostic test\n";
        return 1;
    }
    std::vector<axiom::EdgeId> two_edges {(*box_edges.value)[0], (*box_edges.value)[1]};
    auto fillet_multi = kernel.blends().fillet_edges(*box_a.value, two_edges, 0.4);
    if (fillet_multi.status != axiom::StatusCode::Ok || !fillet_multi.value.has_value()) {
        std::cerr << "fillet_edges (multi) failed\n";
        return 1;
    }
    if (!has_warning_code(fillet_multi.value->warnings, axiom::diag_codes::kBlendMultiEdgeCornerPlaceholder)) {
        std::cerr << "expected multi-edge fillet corner placeholder warning\n";
        return 1;
    }
    auto fillet_multi_diag = kernel.diagnostics().get(fillet_multi.value->diagnostic_id);
    if (fillet_multi_diag.status != axiom::StatusCode::Ok || !fillet_multi_diag.value.has_value() ||
        !has_issue_code(*fillet_multi_diag.value, axiom::diag_codes::kBlendMultiEdgeCornerPlaceholder) ||
        !has_issue_stage(*fillet_multi_diag.value, "blend.fillet.multi_edge")) {
        std::cerr << "expected diagnostic issue kBlendMultiEdgeCornerPlaceholder for multi-edge fillet\n";
        return 1;
    }
    auto chamfer_multi = kernel.blends().chamfer_edges(*box_a.value, two_edges, 0.25);
    if (chamfer_multi.status != axiom::StatusCode::Ok || !chamfer_multi.value.has_value()) {
        std::cerr << "chamfer_edges (multi) failed\n";
        return 1;
    }
    if (!has_warning_code(chamfer_multi.value->warnings, axiom::diag_codes::kBlendMultiEdgeCornerPlaceholder)) {
        std::cerr << "expected multi-edge chamfer corner placeholder warning\n";
        return 1;
    }
    auto chamfer_multi_diag = kernel.diagnostics().get(chamfer_multi.value->diagnostic_id);
    if (chamfer_multi_diag.status != axiom::StatusCode::Ok || !chamfer_multi_diag.value.has_value() ||
        !has_issue_code(*chamfer_multi_diag.value, axiom::diag_codes::kBlendMultiEdgeCornerPlaceholder)) {
        std::cerr << "expected diagnostic issue kBlendMultiEdgeCornerPlaceholder for multi-edge chamfer\n";
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
    if (!kernel.query().mass_properties(*thicken_ok.value).value) {
        std::cerr << "planar thicken must expose current topology mass\n";
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

    // Face modification needs a closed boundary, not a single open coedge.
    auto v2 = txn.create_vertex({0.0, 1.0, 0.0});
    auto line12 = kernel.curves().make_line({1.0, 0.0, 0.0}, {-1.0, 1.0, 0.0});
    auto line20 = kernel.curves().make_line({0.0, 1.0, 0.0}, {0.0, -1.0, 0.0});
    if (!v2.value || !line12.value || !line20.value) {
        std::cerr << "failed to create triangle prerequisites\n";
        return 1;
    }
    auto edge12 = txn.create_edge(*line12.value, *v1.value, *v2.value);
    auto edge20 = txn.create_edge(*line20.value, *v2.value, *v0.value);
    if (!edge12.value || !edge20.value) {
        std::cerr << "failed to create triangle edges\n";
        return 1;
    }
    auto coedge12 = txn.create_coedge(*edge12.value, false);
    auto coedge20 = txn.create_coedge(*edge20.value, false);
    if (!coedge12.value || !coedge20.value) {
        std::cerr << "failed to create triangle coedges\n";
        return 1;
    }
    const std::array<axiom::CoedgeId, 3> coedges {*coedge.value, *coedge12.value, *coedge20.value};
    auto loop = txn.create_loop(coedges);
    if (loop.status != axiom::StatusCode::Ok || !loop.value.has_value()) {
        std::cerr << "failed to create loop\n";
        return 1;
    }
    if (kernel.topology().validate().validate_loop(*loop.value).status != axiom::StatusCode::Ok) {
        std::cerr << "invalid face modification boundary\n";
        return 1;
    }

    auto face = txn.create_face(*plane0.value, *loop.value, {});
    if (face.status != axiom::StatusCode::Ok || !face.value.has_value()) {
        std::cerr << "failed to create face\n";
        return 1;
    }
    auto face_commit = txn.commit();
    if (face_commit.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to commit face modification fixture\n";
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
        !has_issue_code(*delete_face_diag.value, axiom::diag_codes::kHealFeatureRemovedWarning) ||
        !has_issue_stage(*delete_face_diag.value, "modify.delete_face.heal")) {
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

    if (!unsupported_mass(replace_face.value->output)) {
        std::cerr << "proxy mass must fail with no value and stable support stage\n";
        return 1;
    }
    const auto source_mass_after_replace = kernel.query().mass_properties(*box_a.value);
    if (!source_mass_after_replace.value ||
        std::abs(source_mass_after_replace.value->volume-box_props.value->volume) > 1e-9 ||
        std::abs(source_mass_after_replace.value->area-box_props.value->area) > 1e-9 ||
        std::abs(source_mass_after_replace.value->centroid.x-box_props.value->centroid.x) > 1e-9 ||
        std::abs(source_mass_after_replace.value->centroid.y-box_props.value->centroid.y) > 1e-9 ||
        std::abs(source_mass_after_replace.value->centroid.z-box_props.value->centroid.z) > 1e-9) return 1;
    for (std::size_t i = 0; i < 9; ++i)
        if (std::abs(source_mass_after_replace.value->inertia[i]-box_props.value->inertia[i]) > 1e-9)
            return 1;

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
