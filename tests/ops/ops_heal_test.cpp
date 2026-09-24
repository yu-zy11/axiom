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
            !rejected(kernel.sweeps().sweep(profile, *rail.value))) {
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

}  // namespace

int main() {
    if (!test_holed_extrusions() || !test_polyline_sweeps()) return 1;
    // Fresh primitive indexing must preserve both new and pre-existing adjacency.
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
            for (const auto existing : bodies) {
                const auto shells = query.shells_of_body(existing);
                const auto faces = query.faces_of_body(existing);
                const auto edges = query.edges_of_body(existing);
                if (!shells.value || shells.value->size() != 1 || !faces.value || faces.value->empty() ||
                    !edges.value || edges.value->empty()) return 1;
                const auto shell = shells.value->front();
                const auto owners = query.bodies_of_shell(shell);
                if (!owners.value || owners.value->size() != 1 || owners.value->front().value != existing.value)
                    return 1;
                for (const auto face : *faces.value) {
                    const auto face_shells = query.shells_of_face(face);
                    const auto face_bodies = query.bodies_of_face(face);
                    if (!face_shells.value || face_shells.value->size() != 1 ||
                        face_shells.value->front().value != shell.value || !face_bodies.value ||
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
                        edge_shells.value->size() != 1 || edge_shells.value->front().value != shell.value) {
                        std::cerr << "primitive adjacency missing, duplicated or linked to another body\n";
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

    // 不相交两盒并集：质量属性应为两盒体积/表面积之和与体积加权质心，而非 union AABB 的单一长方体体积。
    axiom::BooleanOptions bool_opts;
    auto disjoint_union = kernel.booleans().run(axiom::BooleanOp::Union, *box_a.value, *box_b.value, bool_opts);
    if (disjoint_union.status != axiom::StatusCode::Ok || !disjoint_union.value.has_value()) {
        std::cerr << "disjoint union failed\n";
        return 1;
    }
    auto union_mp = kernel.query().mass_properties(disjoint_union.value->output);
    if (union_mp.status != axiom::StatusCode::Ok || !union_mp.value.has_value()) {
        std::cerr << "disjoint union mass_properties failed\n";
        return 1;
    }
    const double vol_a = 10.0 * 10.0 * 10.0;
    const double vol_b = 5.0 * 5.0 * 5.0;
    const double area_a = 2.0 * (10.0 * 10.0 + 10.0 * 10.0 + 10.0 * 10.0);
    const double area_b = 2.0 * (5.0 * 5.0 + 5.0 * 5.0 + 5.0 * 5.0);
    const double exp_vol = vol_a + vol_b;
    const double exp_area = area_a + area_b;
    const double cx = (vol_a * 5.0 + vol_b * 32.5) / exp_vol;
    if (std::abs(union_mp.value->volume - exp_vol) > 1e-6 || std::abs(union_mp.value->area - exp_area) > 1e-6 ||
        std::abs(union_mp.value->centroid.x - cx) > 1e-5 || std::abs(union_mp.value->centroid.y - cx) > 1e-5 ||
        std::abs(union_mp.value->centroid.z - cx) > 1e-5) {
        std::cerr << "disjoint union mass_properties should combine primitive boxes analytically\n";
        return 1;
    }

    auto disjoint_sub = kernel.booleans().run(axiom::BooleanOp::Subtract, *box_a.value, *box_b.value, bool_opts);
    if (disjoint_sub.status != axiom::StatusCode::Ok || !disjoint_sub.value.has_value()) {
        std::cerr << "disjoint subtract failed\n";
        return 1;
    }
    auto sub_mp = kernel.query().mass_properties(disjoint_sub.value->output);
    if (sub_mp.status != axiom::StatusCode::Ok || !sub_mp.value.has_value()) {
        std::cerr << "disjoint subtract mass_properties failed\n";
        return 1;
    }
    if (std::abs(sub_mp.value->volume - vol_a) > 1e-6 || std::abs(sub_mp.value->area - area_a) > 1e-6 ||
        std::abs(sub_mp.value->centroid.x - 5.0) > 1e-5 || std::abs(sub_mp.value->centroid.y - 5.0) > 1e-5 ||
        std::abs(sub_mp.value->centroid.z - 5.0) > 1e-5) {
        std::cerr << "disjoint subtract mass_properties should match left box only\n";
        return 1;
    }

    // 不相交 盒 + 球：并集用两解析体组合；减（左球右盒）退化为左球解析量（7.7 扩展，非仅限双盒）。
    const double pi_bs = 3.14159265358979323846;
    auto ball_far = kernel.primitives().sphere({60.0, 5.0, 5.0}, 2.0);
    if (ball_far.status != axiom::StatusCode::Ok || !ball_far.value.has_value()) {
        std::cerr << "sphere for disjoint boolean mass test failed\n";
        return 1;
    }
    const double v_ball = (4.0 / 3.0) * pi_bs * 8.0;
    const double a_ball = 4.0 * pi_bs * 4.0;
    auto union_box_sphere =
        kernel.booleans().run(axiom::BooleanOp::Union, *box_a.value, *ball_far.value, bool_opts);
    if (union_box_sphere.status != axiom::StatusCode::Ok || !union_box_sphere.value.has_value()) {
        std::cerr << "disjoint union box+sphere failed\n";
        return 1;
    }
    auto ubs_mp = kernel.query().mass_properties(union_box_sphere.value->output);
    if (ubs_mp.status != axiom::StatusCode::Ok || !ubs_mp.value.has_value()) {
        std::cerr << "union box+sphere mass_properties failed\n";
        return 1;
    }
    const double exp_vol_bs = vol_a + v_ball;
    const double exp_area_bs = area_a + a_ball;
    const double cx_bs = (vol_a * 5.0 + v_ball * 60.0) / exp_vol_bs;
    if (std::abs(ubs_mp.value->volume - exp_vol_bs) > 1e-5 || std::abs(ubs_mp.value->area - exp_area_bs) > 1e-4 ||
        std::abs(ubs_mp.value->centroid.x - cx_bs) > 1e-4 || std::abs(ubs_mp.value->centroid.y - 5.0) > 1e-5 ||
        std::abs(ubs_mp.value->centroid.z - 5.0) > 1e-5) {
        std::cerr << "disjoint union box+sphere mass_properties mismatch\n";
        return 1;
    }
    auto sub_sphere_box =
        kernel.booleans().run(axiom::BooleanOp::Subtract, *ball_far.value, *box_a.value, bool_opts);
    if (sub_sphere_box.status != axiom::StatusCode::Ok || !sub_sphere_box.value.has_value()) {
        std::cerr << "disjoint subtract sphere-box failed\n";
        return 1;
    }
    auto ssb_mp = kernel.query().mass_properties(sub_sphere_box.value->output);
    if (ssb_mp.status != axiom::StatusCode::Ok || !ssb_mp.value.has_value()) {
        std::cerr << "subtract sphere-box mass_properties failed\n";
        return 1;
    }
    if (std::abs(ssb_mp.value->volume - v_ball) > 1e-5 || std::abs(ssb_mp.value->area - a_ball) > 1e-4 ||
        std::abs(ssb_mp.value->centroid.x - 60.0) > 1e-5 || std::abs(ssb_mp.value->centroid.y - 5.0) > 1e-5 ||
        std::abs(ssb_mp.value->centroid.z - 5.0) > 1e-5) {
        std::cerr << "disjoint subtract (lhs sphere) mass_properties should match sphere only\n";
        return 1;
    }

    // AABB 面接触两单位立方并集：union 长方体体积 = 2，与两体体积之和一致，可走 Touching 解析组合路径。
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
    auto touch_mp = kernel.query().mass_properties(u_touch.value->output);
    if (touch_mp.status != axiom::StatusCode::Ok || !touch_mp.value.has_value() ||
        std::abs(touch_mp.value->volume - 2.0) > 1e-5) {
        std::cerr << "touching union mass_properties should sum volumes when union AABB matches sum\n";
        return 1;
    }

    // 大盒完全包含小盒：并集占位 bbox 为大盒，质量应取大盒解析量。
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
    auto hole_mp = kernel.query().mass_properties(u_hole.value->output);
    if (hole_mp.status != axiom::StatusCode::Ok || !hole_mp.value.has_value() ||
        std::abs(hole_mp.value->volume - 1000.0) > 1e-3) {
        std::cerr << "lhs-contains-rhs union mass_properties should match outer box\n";
        return 1;
    }

    // 不相交 占位 extrude + 远距盒：操作数之一为 Sweep，体积为缓存之和。
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
    auto sw_box_mp = kernel.query().mass_properties(u_sw_box.value->output);
    if (sw_box_mp.status != axiom::StatusCode::Ok || !sw_box_mp.value.has_value() ||
        std::abs(sw_box_mp.value->volume - 11.0) > 1e-4) {
        std::cerr << "disjoint union extrude+box mass_properties should sum operand volumes\n";
        return 1;
    }

    // 双盒 Intersect：结果 bbox 与 AABB 交一致时，质量属性按交叠长方体解析（依赖 BodyRecord.boolean_op）。
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
    auto inter_mp = kernel.query().mass_properties(inter_ab.value->output);
    if (inter_mp.status != axiom::StatusCode::Ok || !inter_mp.value.has_value() ||
        std::abs(inter_mp.value->volume - 125.0) > 1e-3) {
        std::cerr << "intersect mass_properties should use AABB overlap volume for two boxes\n";
        return 1;
    }

    // Subtract + 左盒完全包含右盒：体积差（216 - 8 = 208），质心/惯性走差分占位。
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
    auto sub_n_mp = kernel.query().mass_properties(sub_nested.value->output);
    if (sub_n_mp.status != axiom::StatusCode::Ok || !sub_n_mp.value.has_value() ||
        std::abs(sub_n_mp.value->volume - 208.0) > 1e-3) {
        std::cerr << "nested subtract mass_properties volume mismatch\n";
        return 1;
    }
    const double exp_cx = (216.0 * 3.0 - 8.0 * 3.0) / 208.0;
    if (std::abs(sub_n_mp.value->centroid.x - exp_cx) > 1e-4 ||
        std::abs(sub_n_mp.value->centroid.y - exp_cx) > 1e-4 ||
        std::abs(sub_n_mp.value->centroid.z - exp_cx) > 1e-4) {
        std::cerr << "nested subtract mass_properties centroid mismatch\n";
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
    auto placeholder_extrude_mp = kernel.query().mass_properties(*sweep_ok.value);
    if (placeholder_extrude_mp.status != axiom::StatusCode::Ok || !placeholder_extrude_mp.value.has_value()) {
        std::cerr << "placeholder extrude mass_properties failed\n";
        return 1;
    }
    // 占位 extrude：1×1 截面 × 距离 5，体积 5，表面积 2+4×5。
    if (std::abs(placeholder_extrude_mp.value->volume - 5.0) > 1e-9 ||
        std::abs(placeholder_extrude_mp.value->area - 22.0) > 1e-9 ||
        std::abs(placeholder_extrude_mp.value->centroid.z - 2.5) > 1e-9) {
        std::cerr << "placeholder extrude mass_properties mismatch\n";
        return 1;
    }

    // 子午面多边形 revolve：真实三角化闭壳 BRep（8 面）+ Eberly 体积/表面积/质心/惯性；与 Pappus 解析体积近似一致。
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
        rev_mer_faces.value->size() != 8) {
        std::cerr << "meridian revolve expected 8 triangular faces (2n lateral + 2(n-2) caps)\n";
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

    // 非子午面（轮廓平面不含旋转轴）：回退 bbox 壳，质量属性仍用 Pappus。
    axiom::ProfileRef tri_xy;
    tri_xy.label = "tri_xy";
    tri_xy.polygon_xyz = {{1.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, {1.0, 2.0, 0.0}};
    auto rev_xy = kernel.sweeps().revolve(tri_xy, axis_z, pi);
    if (rev_xy.status != axiom::StatusCode::Ok || !rev_xy.value.has_value()) {
        std::cerr << "non-meridian revolve failed\n";
        return 1;
    }
    auto rev_xy_strict = kernel.validate().validate_topology(*rev_xy.value, axiom::ValidationMode::Strict);
    if (rev_xy_strict.status != axiom::StatusCode::Ok) {
        std::cerr << "non-meridian revolve strict topology failed\n";
        return 1;
    }
    auto rev_xy_mp = kernel.query().mass_properties(*rev_xy.value);
    if (rev_xy_mp.status != axiom::StatusCode::Ok || !rev_xy_mp.value.has_value()) {
        std::cerr << "non-meridian revolve mass_properties failed\n";
        return 1;
    }
    const double r_bar_xy = std::sqrt((5.0 / 3.0) * (5.0 / 3.0) + (2.0 / 3.0) * (2.0 / 3.0));
    const double expected_xy_vol = area_m * r_bar_xy * pi;
    if (std::abs(rev_xy_mp.value->volume - expected_xy_vol) > 1e-5) {
        std::cerr << "non-meridian revolve volume should follow Pappus\n";
        return 1;
    }

    std::array<axiom::ProfileRef, 2> loft_profiles {};
    loft_profiles[0].label = "lof1";
    loft_profiles[1].label = "lof2";
    auto loft_body = kernel.sweeps().loft(std::span<const axiom::ProfileRef>(loft_profiles));
    if (loft_body.status != axiom::StatusCode::Ok || !loft_body.value.has_value()) {
        std::cerr << "loft failed\n";
        return 1;
    }
    auto loft_mp = kernel.query().mass_properties(*loft_body.value);
    if (loft_mp.status != axiom::StatusCode::Ok || !loft_mp.value.has_value()) {
        std::cerr << "loft mass_properties failed\n";
        return 1;
    }
    // 占位 loft：extent=max(2,n)=2，体积 8。
    if (std::abs(loft_mp.value->volume - 8.0) > 1e-9) {
        std::cerr << "loft volume should use cached extent^3\n";
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
    auto thicken_mp = kernel.query().mass_properties(*thicken_ok.value);
    if (thicken_mp.status != axiom::StatusCode::Ok || !thicken_mp.value.has_value()) {
        std::cerr << "thicken mass_properties failed\n";
        return 1;
    }
    // 10×10 轴对齐面 × 厚度 1：面面积估计 100，体积缓存 100。
    if (std::abs(thicken_mp.value->volume - 100.0) > 1e-6) {
        std::cerr << "thicken volume should use face-area×thickness not pure bbox volume\n";
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

    auto mp_box_a_ref = kernel.query().mass_properties(*box_a.value);
    auto mp_replace_face_body = kernel.query().mass_properties(replace_face.value->output);
    if (mp_box_a_ref.status != axiom::StatusCode::Ok || !mp_box_a_ref.value.has_value() ||
        mp_replace_face_body.status != axiom::StatusCode::Ok || !mp_replace_face_body.value.has_value() ||
        std::abs(mp_box_a_ref.value->volume - mp_replace_face_body.value->volume) > 1e-3) {
        std::cerr << "replace_face Modified body should inherit source mass when bbox unchanged\n";
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
