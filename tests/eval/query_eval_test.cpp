#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

namespace {

bool approx(double lhs, double rhs, double eps = 1e-6) {
    return std::abs(lhs - rhs) <= eps;
}

bool has_issue_code(const axiom::DiagnosticReport& report, std::string_view code) {
    for (const auto& issue : report.issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

bool length_query_regression() {
    axiom::Kernel kernel;
    auto& geo = kernel.curve_service();
    auto& topo = kernel.topology().query();
    const auto equal = [](const axiom::Result<axiom::Scalar>& result, double expected) {
        return result.status == axiom::StatusCode::Ok && result.value &&
               std::abs(*result.value - expected) <= 1e-12 * std::max(1.0, std::abs(expected));
    };
    const auto failed = [&](const axiom::Result<axiom::Scalar>& result, axiom::StatusCode status,
                            std::string_view code) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        return result.status == status && !result.value && report.value && has_issue_code(*report.value, code);
    };
    const double pi = std::acos(-1.0);
    const auto line = kernel.curves().make_line({7, -2, 8}, {0, 0, 5});
    const auto segment = kernel.curves().make_line_segment({0, 0, 0}, {3, 4, 0});
    const auto circle = kernel.curves().make_circle({1, 2, 3}, {1, 2, 3}, 2);
    const auto polyline = kernel.curves().make_composite_polyline(
        std::array<axiom::Point3, 4>{{{0, 0, 0}, {3, 0, 0}, {3, 0, 0}, {3, 4, 0}}});
    const auto constant = kernel.curves().make_composite_polyline(
        std::array<axiom::Point3, 2>{{{1, 2, 3}, {1, 2, 3}}});
    const auto ellipse = kernel.curves().make_ellipse({0, 0, 0}, {3, 0, 0}, {0, 2, 0});
    const auto bezier = kernel.curves().make_bezier(
        std::array<axiom::Point3, 3>{{{0, 0, 0}, {1, 2, 0}, {2, 0, 0}}});
    if (!line.value || !segment.value || !circle.value || !polyline.value || !constant.value ||
        !ellipse.value || !bezier.value) return false;
    if (!equal(geo.length(*line.value, -3, 7), 10) ||
        !equal(geo.length(*segment.value), 5) || !equal(geo.length(*segment.value, .8, .2), 3) ||
        !equal(geo.length(*circle.value), 4 * pi) || !equal(geo.length(*circle.value, pi, 0), 2 * pi) ||
        !equal(geo.length(*polyline.value), 7) || !equal(geo.length(*polyline.value, .5, 2.5), 3.5) ||
        !equal(geo.length(*polyline.value, 1, 2), 0) || !equal(geo.length(*constant.value), 0) ||
        !equal(geo.length(*circle.value, 2 * pi, 2 * pi), 0)) return false;
    // Child domains are deliberately NOT rescaled: circle contributes one radian,
    // polyline contributes its first segment, nested chain its first child.
    const auto chain = kernel.curves().make_composite_chain(std::array{*segment.value, *circle.value, *polyline.value});
    if (!chain.value) return false;
    const auto nested = kernel.curves().make_composite_chain(std::array{*chain.value, *line.value});
    const auto unsupported_chain = kernel.curves().make_composite_chain(std::array{*segment.value, *bezier.value});
    if (!nested.value || !unsupported_chain.value || !equal(geo.length(*chain.value), 10) ||
        !equal(geo.length(*chain.value, .5, 2.5), 6) || !equal(geo.length(*nested.value), 6) ||
        !equal(geo.length(*unsupported_chain.value, 0, 1), 5)) return false;
    const auto big = kernel.curves().make_line_segment({-1e200, 0, 0}, {1e200, 0, 0});
    const auto overflow = kernel.curves().make_circle({0, 0, 0}, {0, 0, 1}, 1e308);
    if (!big.value || !overflow.value || !equal(geo.length(*big.value), 2e200) ||
        !equal(geo.length(*overflow.value, 0, .1), 1e307)) return false;
    const auto objects_before = kernel.object_count_total();
    const auto geometry_before = kernel.geometry_count();
    const auto runtime_before = kernel.runtime_store_counts();
    const auto invalid_before = kernel.eval_graph().invalid_node_count();
    if (!objects_before.value || !geometry_before.value || !runtime_before.value || !invalid_before.value) return false;
    for (const auto id : {*ellipse.value, *bezier.value, *unsupported_chain.value}) {
        if (!geo.length(id).value) return false;
    }
    if (!failed(geo.length({}), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
        !failed(geo.length(*line.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange) ||
        !failed(geo.length(*overflow.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange) ||
        !failed(geo.length(*line.value, -1e308, 1e308), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
        if (!failed(geo.length(*segment.value, bad, 0), axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange) ||
            !failed(geo.length(*segment.value, 0, bad), axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    }
    for (const auto id : {*segment.value, *circle.value, *polyline.value, *chain.value}) {
        if (!failed(geo.length(id, -.1, 0), axiom::StatusCode::InvalidInput, axiom::diag_codes::kGeoParameterOutOfDomain) ||
            !failed(geo.length(id, 0, 100), axiom::StatusCode::InvalidInput, axiom::diag_codes::kGeoParameterOutOfDomain)) return false;
    }
    if (!equal(geo.length(*segment.value), 5) || kernel.object_count_total().value != objects_before.value ||
        kernel.geometry_count().value != geometry_before.value || kernel.eval_graph().invalid_node_count().value != invalid_before.value)
        return false;
    const auto runtime_after = kernel.runtime_store_counts();
    if (!runtime_after.value || runtime_before.value->curve_eval_cache_entries != runtime_after.value->curve_eval_cache_entries ||
        runtime_before.value->surface_eval_cache_entries != runtime_after.value->surface_eval_cache_entries ||
        runtime_before.value->mesh_records != runtime_after.value->mesh_records ||
        runtime_before.value->tessellation_cache_entries != runtime_after.value->tessellation_cache_entries ||
        runtime_before.value->face_tessellation_cache_entries != runtime_after.value->face_tessellation_cache_entries ||
        runtime_before.value->intersection_records != runtime_after.value->intersection_records) return false;

    // Full boundary workflow: concave outer loop + two holes, independent winding,
    // reversed coedges, two planes and three model-unit scales (24 variants).
    for (double scale : {0.001, 1.0, 1000.0}) {
        for (bool tilted : {false, true}) {
            for (bool reverse_outer : {false, true}) {
                for (bool reverse_holes : {false, true}) {
                    const auto point = [&](double x, double y) -> axiom::Point3 {
                        return tilted ? axiom::Point3{10 + x * scale, 20 + .6 * y * scale, 30 + .8 * y * scale}
                                      : axiom::Point3{10 + x * scale, 20 + y * scale, 30};
                    };
                    const auto plane = kernel.surfaces().make_plane(point(0, 0), tilted ? axiom::Vec3{0, -.8, .6} : axiom::Vec3{0, 0, 1});
                    if (!plane.value) return false;
                    auto txn = kernel.topology().begin_transaction();
                    const auto make_loop = [&](std::vector<axiom::Point3> points, bool reverse) -> axiom::LoopId {
                        if (reverse) std::reverse(points.begin(), points.end());
                        std::vector<axiom::VertexId> vertices;
                        std::vector<axiom::CoedgeId> coedges;
                        for (const auto& p : points) {
                            const auto v = txn.create_vertex(p);
                            if (!v.value) return {};
                            vertices.push_back(*v.value);
                        }
                        for (std::size_t i = 0; i < points.size(); ++i) {
                            const auto j = (i + 1) % points.size();
                            const auto c = kernel.curves().make_line_segment(points[j], points[i]);
                            if (!c.value) return {};
                            const auto e = txn.create_edge(*c.value, vertices[j], vertices[i]);
                            const auto curve_length = geo.length(*c.value);
                            if (!e.value || !curve_length.value || !equal(topo.edge_length(*e.value), *curve_length.value)) return {};
                            const auto ce = txn.create_coedge(*e.value, true);
                            if (!ce.value) return {};
                            coedges.push_back(*ce.value);
                        }
                        const auto loop = txn.create_loop(coedges);
                        return loop.value.value_or(axiom::LoopId{});
                    };
                    const auto outer = make_loop({point(0, 0), point(6, 0), point(6, 4), point(3, 4), point(3, 6), point(0, 6)}, reverse_outer);
                    const auto hole1 = make_loop({point(1, 1), point(2, 1), point(2, 2), point(1, 2)}, reverse_holes);
                    const auto hole2 = make_loop({point(4, 1), point(5, 1), point(5, 2), point(4, 2)}, !reverse_holes);
                    if (!outer.value || !hole1.value || !hole2.value) return false;
                    const auto face = txn.create_face(*plane.value, outer, std::array{hole2, hole1});
                    if (!face.value) return false;
                    const auto writes = txn.write_operation_count();
                    const auto audit = topo.query_operation_count();
                    if (!equal(topo.face_boundary_length(*face.value), 32 * scale)) return false;
                    const auto audit_after = topo.query_operation_count();
                    if (!audit.value || !audit_after.value || *audit_after.value != *audit.value + 1 ||
                        !equal(topo.loop_length(outer), 24 * scale) || !equal(topo.loop_length(hole1), 4 * scale) ||
                        !failed(topo.edge_length({}), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
                        !failed(topo.loop_length({}), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
                        !failed(topo.face_boundary_length({}), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
                        txn.write_operation_count().value != writes.value || txn.commit().status != axiom::StatusCode::Ok) return false;
                    {
                        auto edit = kernel.topology().begin_transaction();
                        if (edit.delete_face(*face.value).status != axiom::StatusCode::Ok ||
                            !failed(topo.face_boundary_length(*face.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
                            edit.rollback().status != axiom::StatusCode::Ok || !equal(topo.face_boundary_length(*face.value), 32 * scale)) return false;
                    }
                }
            }
        }
    }
    // Public creation permits geometrically inconsistent edges: queries must reject
    // them without manufacturing a chord for a curved edge or mutating the transaction.
    auto txn = kernel.topology().begin_transaction();
    const auto v0 = txn.create_vertex({0, 0, 0});
    const auto v1 = txn.create_vertex({1.5, 2, 0});
    const auto v2 = txn.create_vertex({3, 4, 0});
    const auto off = txn.create_vertex({0, 0, 1});
    const auto outside = txn.create_vertex({6, 8, 0});
    const auto coincident = txn.create_vertex({0, 0, 0});
    if (!v0.value || !v1.value || !v2.value || !off.value || !outside.value || !coincident.value) return false;
    const auto partial = txn.create_edge(*segment.value, *v2.value, *v1.value);
    const auto mismatch = txn.create_edge(*segment.value, *v0.value, *off.value);
    const auto out_of_domain = txn.create_edge(*segment.value, *v0.value, *outside.value);
    const auto degenerate = txn.create_edge(*segment.value, *v0.value, *coincident.value);
    const auto curved = txn.create_edge(*circle.value, *v0.value, *v2.value);
    if (!partial.value || !mismatch.value || !out_of_domain.value || !degenerate.value || !curved.value) return false;
    const auto straight = kernel.curves().make_line({0, 0, 0}, {3, 4, 0});
    const auto huge_line = kernel.curves().make_line({0, 0, 0}, {1, 0, 0});
    const auto huge0 = txn.create_vertex({-1e308, 0, 0});
    const auto huge1 = txn.create_vertex({1e308, 0, 0});
    if (!straight.value || !huge_line.value || !huge0.value || !huge1.value) return false;
    const auto linear = txn.create_edge(*straight.value, *v0.value, *outside.value);
    const auto huge_edge = txn.create_edge(*huge_line.value, *huge0.value, *huge1.value);
    if (!linear.value || !huge_edge.value || !equal(topo.edge_length(*linear.value), 10) ||
        !failed(topo.edge_length(*huge_edge.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    const auto curved_return = txn.create_edge(*circle.value, *v2.value, *v0.value);
    if (!curved_return.value) return false;
    const auto ce0 = txn.create_coedge(*curved.value, false);
    const auto ce1 = txn.create_coedge(*curved_return.value, false);
    if (!ce0.value || !ce1.value) return false;
    const auto curved_loop = txn.create_loop(std::array{*ce0.value, *ce1.value});
    if (!curved_loop.value || !failed(topo.loop_length(*curved_loop.value), axiom::StatusCode::NotImplemented,
                                     axiom::diag_codes::kCoreOperationUnsupported)) return false;
    const auto curved_plane = kernel.surfaces().make_plane({1, 2, 3}, {1, 2, 3});
    if (!curved_plane.value) return false;
    const auto curved_face = txn.create_face(*curved_plane.value, *curved_loop.value, {});
    if (!curved_face.value || !failed(topo.face_boundary_length(*curved_face.value), axiom::StatusCode::NotImplemented,
                                     axiom::diag_codes::kCoreOperationUnsupported)) return false;
    const auto writes = txn.write_operation_count();
    const auto objects = kernel.object_count_total();
    const auto runtime_topo_before = kernel.runtime_store_counts();
    const auto geometry_topo_before = kernel.geometry_count();
    const auto invalid_topo_before = kernel.eval_graph().invalid_node_count();
    if (!equal(topo.edge_length(*partial.value), 2.5) ||
        !failed(topo.edge_length(*curved.value), axiom::StatusCode::NotImplemented, axiom::diag_codes::kCoreOperationUnsupported)) return false;
    for (const auto edge : {*mismatch.value, *out_of_domain.value, *degenerate.value}) {
        if (!failed(topo.edge_length(edge), axiom::StatusCode::InvalidTopology, axiom::diag_codes::kTopoCurveTopologyMismatch)) return false;
    }
    const auto runtime_topo_after = kernel.runtime_store_counts();
    if (!runtime_topo_before.value || !runtime_topo_after.value ||
        runtime_topo_before.value->curve_eval_cache_entries != runtime_topo_after.value->curve_eval_cache_entries ||
        runtime_topo_before.value->surface_eval_cache_entries != runtime_topo_after.value->surface_eval_cache_entries ||
        runtime_topo_before.value->mesh_records != runtime_topo_after.value->mesh_records ||
        runtime_topo_before.value->tessellation_cache_entries != runtime_topo_after.value->tessellation_cache_entries ||
        runtime_topo_before.value->face_tessellation_cache_entries != runtime_topo_after.value->face_tessellation_cache_entries ||
        runtime_topo_before.value->intersection_records != runtime_topo_after.value->intersection_records ||
        kernel.geometry_count().value != geometry_topo_before.value ||
        kernel.eval_graph().invalid_node_count().value != invalid_topo_before.value) return false;
    if (txn.write_operation_count().value != writes.value || kernel.object_count_total().value != objects.value ||
        !equal(topo.edge_length(*partial.value), 2.5) || txn.rollback().status != axiom::StatusCode::Ok ||
        !failed(topo.edge_length(*partial.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
        !failed(topo.loop_length(*curved_loop.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle)) return false;
    // Isolated Eval graph: successful and failed reads preserve the bound node.
    const auto box = kernel.primitives().box({0, 0, 0}, 2, 3, 4);
    if (!box.value) return false;
    const auto node = kernel.eval_graph().register_node(axiom::NodeKind::Geometry,
        std::string("body:") + std::to_string(box.value->value));
    const auto faces = topo.faces_of_body(*box.value);
    if (!node.value || !faces.value || faces.value->size() != 6 ||
        kernel.eval_graph().recompute(*node.value).status != axiom::StatusCode::Ok) return false;
    const auto invalid = kernel.eval_graph().is_invalid(*node.value);
    const auto recomputes = kernel.eval_graph().recompute_count(*node.value);
    for (const auto face : *faces.value) {
        if (!topo.face_boundary_length(face).value) return false;
    }
    if (!failed(topo.face_boundary_length({}), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
        kernel.eval_graph().is_invalid(*node.value).value != invalid.value ||
        kernel.eval_graph().recompute_count(*node.value).value != recomputes.value) return false;
    return true;
}

bool numerical_length_query_regression() {
    axiom::Kernel kernel;
    auto& factory = kernel.curves();
    auto& geo = kernel.curve_service();
    const double pi = std::acos(-1.0);
    const double quadratic_length = std::sqrt(5.0) / 2 + std::asinh(2.0) / 4;
    const auto equal = [](const axiom::Result<double>& r, double expected) {
        return r.status == axiom::StatusCode::Ok && r.value &&
               std::abs(*r.value - expected) <= 2e-8 * std::max(1e-3, std::abs(expected));
    };
    const auto failed = [&](const axiom::Result<double>& r, axiom::StatusCode status, std::string_view code) {
        const auto report = kernel.diagnostics().get(r.diagnostic_id);
        return r.status == status && !r.value && report.value && has_issue_code(*report.value, code);
    };
    // x=t, y=t^2, represented in three bases, scales and spatial frames.
    for (const double scale : {1e-3, 1.0, 1e3}) {
        for (const bool tilted : {false, true}) {
            const auto point = [&](double x, double y) -> axiom::Point3 {
                return tilted ? axiom::Point3{7 + .6 * scale * x, -3 + .8 * scale * x, 2 + scale * y}
                              : axiom::Point3{scale * x, scale * y, 0};
            };
            const std::vector<axiom::Point3> poles{point(0, 0), point(.5, 0), point(1, 1)};
            const auto bezier = factory.make_bezier(poles);
            axiom::BSplineCurveDesc desc;
            desc.poles = poles; desc.degree = 2; desc.knots = {2, 2, 2, 5, 5, 5};
            const auto spline = factory.make_bspline(desc);
            axiom::NURBSCurveDesc rational;
            rational.poles = poles; rational.degree = 2; rational.knots = desc.knots;
            rational.weights = {7, 7, 7};
            const auto nurbs = factory.make_nurbs(rational);
            if (!bezier.value || !spline.value || !nurbs.value) return false;
            for (const auto id : {*bezier.value, *spline.value, *nurbs.value}) {
                const auto domain = geo.domain(id);
                if (!domain.value || !equal(geo.length(id), scale * quadratic_length)) return false;
                const auto a = domain.value->min, b = domain.value->max, m = (a + b) / 2;
                const auto first = geo.length(id, a, m), last = geo.length(id, m, b);
                if (!first.value || !last.value || !equal(geo.length(id, b, a), scale * quadratic_length) ||
                    !equal(geo.length(id), *first.value + *last.value) || !equal(geo.length(id, m, m), 0)) return false;
                if (!failed(geo.length(id, a - 1, b), axiom::StatusCode::InvalidInput,
                            axiom::diag_codes::kGeoParameterOutOfDomain)) return false;
            }
        }
    }
    const auto ellipse = factory.make_ellipse({4, -3, 2}, {3, 0, 0}, {0, 0, 2});
    const auto round = factory.make_ellipse({0, 0, 0}, {2, 0, 0}, {0, 2, 0});
    const auto parabola = factory.make_parabola({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, .25);
    const auto hyperbola = factory.make_hyperbola({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 1, 1);
    if (!ellipse.value || !round.value || !parabola.value || !hyperbola.value ||
        !equal(geo.length(*ellipse.value), 15.8654395892905898) ||
        !equal(geo.length(*ellipse.value, 0, pi / 2), 15.8654395892905898 / 4) ||
        !equal(geo.length(*round.value), 4 * pi) ||
        !equal(geo.length(*parabola.value, 0, 1), quadratic_length) ||
        !equal(geo.length(*parabola.value), 10 * std::sqrt(401.0) + std::asinh(20.0) / 2)) return false;
    // Independent dense composite midpoint oracle for non-orthogonal conics.
    const auto skew = factory.make_ellipse({0, 0, 0}, {3, 0, 0}, {1, 2, 0});
    if (!skew.value) return false;
    double hyperbola_reference = 0, skew_reference = 0;
    constexpr int samples = 100000;
    for (int i = 0; i < samples; ++i) {
        const double t = (i + .5) / samples;
        hyperbola_reference += std::sqrt(std::cosh(2 * t)) / samples;
        const double angle = 2 * pi * t;
        skew_reference += std::hypot(-3 * std::sin(angle) + std::cos(angle), 2 * std::cos(angle)) * 2 * pi / samples;
    }
    if (!equal(geo.length(*hyperbola.value, 0, 1), hyperbola_reference) ||
        !equal(geo.length(*skew.value), skew_reference)) return false;
    axiom::NURBSCurveDesc quarter_desc;
    quarter_desc.poles = {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    quarter_desc.weights = {1, std::sqrt(.5), 1};
    quarter_desc.degree = 2; quarter_desc.knots = {0, 0, 0, 1, 1, 1};
    const auto quarter = factory.make_nurbs(quarter_desc);
    const auto cusp = factory.make_bezier(std::array<axiom::Point3, 3>{{{0, 0, 0}, {1, 0, 0}, {0, 0, 0}}});
    const auto constant = factory.make_bezier(std::array<axiom::Point3, 1>{{{7, 8, 9}}});
    if (!quarter.value || !cusp.value || !constant.value || !equal(geo.length(*quarter.value), pi / 2) ||
        !equal(geo.length(*quarter.value, .5, 0), pi / 4) || !equal(geo.length(*cusp.value), 1) ||
        !equal(geo.length(*constant.value), 0)) return false;
    // Degree-elevated quadratic and nonconstant-weight straight rational curve.
    std::vector<axiom::Point3> elevated;
    for (int i = 0; i <= 12; ++i) elevated.push_back({i / 12.0, i * (i - 1) / 132.0, 0});
    const auto high_degree = factory.make_bezier(elevated);
    axiom::NURBSCurveDesc rational_line;
    rational_line.poles = {{0, 0, 0}, {3, 4, 0}}; rational_line.weights = {1, 10};
    const auto weighted_line = factory.make_nurbs(rational_line);
    if (!high_degree.value || !weighted_line.value || !equal(geo.length(*high_degree.value), quadratic_length) ||
        !equal(geo.length(*weighted_line.value), 5)) return false;
    // Repeated/discontinuous, non-clamped, and extremely narrow knot spans.
    for (int variant = 0; variant < 3; ++variant) {
        axiom::BSplineCurveDesc desc;
        desc.degree = 1;
        if (variant == 0) {
            desc.poles = {{0, 0, 0}, {3, 0, 0}, {30, 40, 0}, {30, 44, 0}};
            desc.knots = {0, 0, .5, .5, 1, 1};
        } else {
            desc.poles = {{0, 0, 0}, {3, 0, 0}, {3, 4, 0}};
            desc.knots = variant == 1 ? std::vector<double>{0, 1, 2, 3, 4}
                                      : std::vector<double>{0, 0, 1e-30, 1, 1};
        }
        const auto spline = factory.make_bspline(desc);
        axiom::NURBSCurveDesc rational;
        rational.poles = desc.poles; rational.knots = desc.knots; rational.degree = 1;
        const auto nurbs = factory.make_nurbs(rational);
        if (!spline.value || !nurbs.value || !equal(geo.length(*spline.value), 7) || !equal(geo.length(*nurbs.value), 7)) return false;
    }
    const auto chain = factory.make_composite_chain(std::array{*quarter.value, *parabola.value, *cusp.value});
    if (!chain.value || !equal(geo.length(*chain.value), pi / 2 + quadratic_length + 1) ||
        !equal(geo.length(*chain.value, .5, 2), pi / 4 + quadratic_length)) return false;
    const auto nested = factory.make_composite_chain(std::array{*chain.value, *constant.value});
    if (!nested.value || !equal(geo.length(*nested.value), pi / 2)) return false;
    const auto moved = kernel.geometry_transform().transform_curve(*quarter.value,
        kernel.linear_algebra().make_translation({5, -3, 8}));
    if (!moved.value || !equal(geo.length(*moved.value), pi / 2) || !equal(geo.length(*quarter.value), pi / 2)) return false;
    axiom::BSplineCurveDesc shifted_desc;
    shifted_desc.poles = {{0, 0, 0}, {1, 0, 0}}; shifted_desc.degree = 1; shifted_desc.knots = {2, 2, 3, 3};
    const auto shifted = factory.make_bspline(shifted_desc);
    if (!shifted.value) return false;
    const auto invalid_chain = factory.make_composite_chain(std::array{*quarter.value, *shifted.value});
    if (!invalid_chain.value || !equal(geo.length(*invalid_chain.value, 0, 1), pi / 2) ||
        !failed(geo.length(*invalid_chain.value), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kGeoParameterOutOfDomain)) return false;
    shifted_desc.poles = {{5, 7, 8}, {5, 7, 8}};
    const auto constant_spline = factory.make_bspline(shifted_desc);
    axiom::NURBSCurveDesc constant_desc;
    constant_desc.poles = shifted_desc.poles; constant_desc.weights = {1, 5};
    const auto constant_nurbs = factory.make_nurbs(constant_desc);
    if (!constant_spline.value || !constant_nurbs.value || !equal(geo.length(*constant_spline.value), 0) ||
        !equal(geo.length(*constant_nurbs.value), 0)) return false;
    const auto huge = factory.make_bezier(std::array<axiom::Point3, 2>{{{-1e308, 0, 0}, {1e308, 0, 0}}});
    const auto invalid_curve = factory.make_bezier(std::array<axiom::Point3, 2>{{{0, 0, 0},
        {std::numeric_limits<double>::infinity(), 0, 0}}});
    if (!huge.value) return false;
    // Query success/failure is read-only, even within a topology transaction.
    auto txn = kernel.topology().begin_transaction();
    const auto v0 = txn.create_vertex({1, 0, 0}), v1 = txn.create_vertex({0, 1, 0});
    if (!v0.value || !v1.value) return false;
    const auto edge = txn.create_edge(*quarter.value, *v0.value, *v1.value);
    const auto node = kernel.eval_graph().register_node(axiom::NodeKind::Geometry, "curve:length-regression");
    if (!edge.value || !node.value || kernel.eval_graph().recompute(*node.value).status != axiom::StatusCode::Ok) return false;
    const auto writes = txn.write_operation_count();
    const auto objects = kernel.object_count_total();
    const auto geometry = kernel.geometry_count();
    const auto stores = kernel.runtime_store_counts();
    const auto recomputes = kernel.eval_graph().recompute_count(*node.value);
    if (!stores.value || !writes.value || !objects.value || !geometry.value) return false;
    axiom::CurveLengthOptions tight;
    tight.absolute_tolerance = 1e-11; tight.relative_tolerance = 1e-11;
    if (!equal(geo.length(*quarter.value, tight), pi / 2)) return false;
    axiom::CurveLengthOptions limited = tight;
    limited.max_evaluations = 1;
    const auto exhausted = geo.length(*quarter.value, limited);
    const auto report_path = std::filesystem::temp_directory_path() / "axiom_query_length_diagnostic.json";
    if (kernel.diagnostics().export_report_json(exhausted.diagnostic_id, report_path.string()).status != axiom::StatusCode::Ok) return false;
    std::ifstream report_file(report_path);
    const std::string report_json((std::istreambuf_iterator<char>(report_file)), std::istreambuf_iterator<char>{});
    report_file.close();
    std::filesystem::remove(report_path);
    if (report_json.find(axiom::diag_codes::kGeoLengthIntegrationFailure) == std::string::npos) return false;
    auto unattainable = tight;
    unattainable.absolute_tolerance = 0; unattainable.relative_tolerance = 1e-30; unattainable.max_evaluations = 1000;
    if (!failed(geo.length(*quarter.value, unattainable), axiom::StatusCode::OperationFailed,
                axiom::diag_codes::kGeoLengthIntegrationFailure)) return false;
    for (const bool relative_only : {false, true}) {
        auto options = tight;
        if (relative_only) options.absolute_tolerance = 0;
        else options.relative_tolerance = 0;
        if (!equal(geo.length(*quarter.value, options), pi / 2)) return false;
    }
    if (!failed(geo.length(*quarter.value, limited), axiom::StatusCode::OperationFailed, axiom::diag_codes::kGeoLengthIntegrationFailure) ||
        !failed(geo.length(*chain.value, limited), axiom::StatusCode::OperationFailed, axiom::diag_codes::kGeoLengthIntegrationFailure) ||
        !equal(geo.length(*quarter.value, .5, .5, limited), 0) || !equal(geo.length(*quarter.value, tight), pi / 2) ||
        !failed(geo.length(*huge.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange) ||
        !failed(geo.length({}), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle)) return false;
    if (invalid_curve.value && !failed(geo.length(*invalid_curve.value), axiom::StatusCode::OperationFailed,
                                      axiom::diag_codes::kGeoLengthIntegrationFailure)) return false;
    for (const double bad : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto options = tight; options.absolute_tolerance = bad;
        if (!failed(geo.length(*quarter.value, options), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange)) return false;
        options = tight; options.relative_tolerance = bad;
        if (!failed(geo.length(*quarter.value, options), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    }
    for (int variant = 0; variant < 2; ++variant) {
        auto options = tight;
        if (variant == 0) options.max_evaluations = 0;
        else options.absolute_tolerance = options.relative_tolerance = 0;
        if (!failed(geo.length(*quarter.value, options), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    }
    if (!failed(kernel.topology().query().edge_length(*edge.value), axiom::StatusCode::NotImplemented,
                axiom::diag_codes::kCoreOperationUnsupported) || !equal(geo.length(*quarter.value), pi / 2)) return false;
    const auto after = kernel.runtime_store_counts();
    if (!after.value || stores.value->curve_eval_cache_entries != after.value->curve_eval_cache_entries ||
        stores.value->surface_eval_cache_entries != after.value->surface_eval_cache_entries ||
        stores.value->mesh_records != after.value->mesh_records ||
        stores.value->tessellation_cache_entries != after.value->tessellation_cache_entries ||
        stores.value->face_tessellation_cache_entries != after.value->face_tessellation_cache_entries ||
        stores.value->intersection_records != after.value->intersection_records ||
        txn.write_operation_count().value != writes.value || kernel.object_count_total().value != objects.value ||
        kernel.geometry_count().value != geometry.value || kernel.eval_graph().is_invalid(*node.value).value != false ||
        kernel.eval_graph().recompute_count(*node.value).value != recomputes.value) return false;
    if (txn.rollback().status != axiom::StatusCode::Ok || !equal(geo.length(*quarter.value), pi / 2) ||
        !failed(kernel.topology().query().edge_length(*edge.value), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreInvalidHandle)) return false;
    return true;
}

}  // namespace

int main() {
    if (!numerical_length_query_regression()) {
        std::cerr << "numerical length query regression failed\n";
        return 1;
    }
    if (!length_query_regression()) {
        std::cerr << "analytic length query regression failed\n";
        return 1;
    }
    axiom::Kernel kernel;

    auto line = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    auto circle = kernel.curves().make_circle({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 2.0);
    auto tilted_circle = kernel.curves().make_circle({1.0, 2.0, 3.0}, {1.0, 0.0, 0.0}, 2.0);
    auto bezier = kernel.curves().make_bezier({{{0.0, 0.0, 0.0}, {1.0, 2.0, 0.0}, {2.0, 0.0, 0.0}}});
    auto bspline = kernel.curves().make_bspline({{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}}});
    axiom::NURBSCurveDesc nurbs_desc;
    nurbs_desc.poles = {{0.0, 0.0, 0.0}, {1.0, 2.0, 0.0}, {2.0, 0.0, 0.0}};
    nurbs_desc.weights = {1.0, 2.0, 1.0};
    auto nurbs = kernel.curves().make_nurbs(nurbs_desc);
    auto sphere = kernel.surfaces().make_sphere({0.0, 0.0, 0.0}, 5.0);
    auto cylinder = kernel.surfaces().make_cylinder({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 3.0);
    auto tilted_cylinder =
        kernel.surfaces().make_cylinder({1.0, 2.0, 3.0}, {1.0, 0.0, 0.0}, 3.0);
    auto cone = kernel.surfaces().make_cone({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, std::acos(-1.0) * 0.25);
    auto torus = kernel.surfaces().make_torus({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 5.0, 2.0);
    auto bspline_surface =
        kernel.surfaces().make_bspline({{{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 1.0}}});
    axiom::NURBSSurfaceDesc nurbs_surface_desc;
    nurbs_surface_desc.poles = {{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 1.0}};
    nurbs_surface_desc.weights = {1.0, 1.0, 1.0, 3.0};
    auto nurbs_surface = kernel.surfaces().make_nurbs(nurbs_surface_desc);
    if (line.status != axiom::StatusCode::Ok || circle.status != axiom::StatusCode::Ok ||
        tilted_circle.status != axiom::StatusCode::Ok || bezier.status != axiom::StatusCode::Ok ||
        bspline.status != axiom::StatusCode::Ok || nurbs.status != axiom::StatusCode::Ok ||
        sphere.status != axiom::StatusCode::Ok || cylinder.status != axiom::StatusCode::Ok ||
        tilted_cylinder.status != axiom::StatusCode::Ok || cone.status != axiom::StatusCode::Ok ||
        torus.status != axiom::StatusCode::Ok || bspline_surface.status != axiom::StatusCode::Ok ||
        nurbs_surface.status != axiom::StatusCode::Ok ||
        !line.value.has_value() || !circle.value.has_value() || !tilted_circle.value.has_value() ||
        !bezier.value.has_value() || !bspline.value.has_value() || !nurbs.value.has_value() ||
        !sphere.value.has_value() || !cylinder.value.has_value() ||
        !tilted_cylinder.value.has_value() || !cone.value.has_value() || !torus.value.has_value() ||
        !bspline_surface.value.has_value() || !nurbs_surface.value.has_value()) {
        std::cerr << "failed to create geometry for query/eval test\n";
        return 1;
    }

    // Ops materializes a box as planar faces; Topo measures current boundary geometry.
    const auto box = kernel.primitives().box({1.0, 2.0, 3.0}, 2.0, 3.0, 4.0);
    if (!box.value) return 1;
    const auto faces = kernel.topology().query().faces_of_body(*box.value);
    if (!faces.value || faces.value->size() != 6) return 1;
    std::array<double, 6> perimeters{};
    for (std::size_t i = 0; i < perimeters.size(); ++i) {
        const auto measured = kernel.topology().query().face_boundary_length((*faces.value)[i]);
        if (!measured.value || measured.status != axiom::StatusCode::Ok) return 1;
        perimeters[i] = *measured.value;
    }
    std::sort(perimeters.begin(), perimeters.end());
    if (perimeters != std::array<double, 6>{10, 10, 12, 12, 14, 14}) return 1;
    std::array<double, 6> areas{};
    for (std::size_t i = 0; i < areas.size(); ++i) {
        const auto measured = kernel.topology().query().planar_face_area((*faces.value)[i]);
        if (measured.status != axiom::StatusCode::Ok || !measured.value) return 1;
        areas[i] = *measured.value;
    }
    std::sort(areas.begin(), areas.end());
    if (areas != std::array<double, 6>{6.0, 6.0, 8.0, 8.0, 12.0, 12.0}) {
        std::cerr << "unexpected planar face area in squared model units\n";
        return 1;
    }
    const auto face = faces.value->front();
    const auto original = kernel.topology().query().planar_face_area(face);
    const auto original_boundary = kernel.topology().query().face_boundary_length(face);
    const auto missing = kernel.topology().query().planar_face_area(axiom::FaceId{});
    const auto missing_report = kernel.diagnostics().get(missing.diagnostic_id);
    if (!original.value || missing.status != axiom::StatusCode::InvalidInput || missing.value ||
        !missing_report.value || !has_issue_code(*missing_report.value, axiom::diag_codes::kCoreInvalidHandle) ||
        kernel.topology().query().planar_face_area(face).value != original.value) return 1;

    {
        auto txn = kernel.topology().begin_transaction();
        if (txn.replace_surface(face, *sphere.value).status != axiom::StatusCode::Ok) return 1;
        if (kernel.topology().query().face_boundary_length(face).value != original_boundary.value) return 1;
        const auto curved = kernel.topology().query().planar_face_area(face);
        const auto curved_report = kernel.diagnostics().get(curved.diagnostic_id);
        if (curved.status != axiom::StatusCode::NotImplemented || curved.value ||
            !curved_report.value || !has_issue_code(*curved_report.value, axiom::diag_codes::kCoreOperationUnsupported) ||
            txn.rollback().status != axiom::StatusCode::Ok ||
            kernel.topology().query().planar_face_area(face).value != original.value) return 1;
    }
    const auto displaced_plane = kernel.surfaces().make_plane({100.0, 100.0, 100.0}, {1.0, 1.0, 1.0});
    if (!displaced_plane.value) return 1;
    {
        auto txn = kernel.topology().begin_transaction();
        if (txn.replace_surface(face, *displaced_plane.value).status != axiom::StatusCode::Ok) return 1;
        const auto nonplanar = kernel.topology().query().planar_face_area(face);
        if (nonplanar.status != axiom::StatusCode::InvalidTopology || nonplanar.value ||
            txn.rollback().status != axiom::StatusCode::Ok ||
            kernel.topology().query().planar_face_area(face).value != original.value) return 1;
    }
    {
        auto txn = kernel.topology().begin_transaction();
        if (txn.delete_face(face).status != axiom::StatusCode::Ok) return 1;
        const auto deleted = kernel.topology().query().planar_face_area(face);
        const auto deleted_boundary = kernel.topology().query().face_boundary_length(face);
        if (deleted_boundary.status != axiom::StatusCode::InvalidInput || deleted_boundary.value) return 1;
        if (deleted.status != axiom::StatusCode::InvalidInput || deleted.value ||
            txn.rollback().status != axiom::StatusCode::Ok ||
            kernel.topology().query().planar_face_area(face).value != original.value) return 1;
    }
    if (kernel.topology().query().face_boundary_length(face).value != original_boundary.value) return 1;
    const auto hole_plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    if (!hole_plane.value) return 1;
    {
        auto txn = kernel.topology().begin_transaction();
        const auto make_square_loop = [&](const std::array<axiom::Point3, 4>& points) {
            std::array<axiom::VertexId, 4> vertices{};
            std::array<axiom::CoedgeId, 4> coedges{};
            for (std::size_t i = 0; i < 4; ++i) {
                const auto vertex = txn.create_vertex(points[i]);
                if (!vertex.value) return axiom::LoopId{};
                vertices[i] = *vertex.value;
            }
            for (std::size_t i = 0; i < 4; ++i) {
                const auto next = (i + 1) % 4;
                const auto curve = kernel.curves().make_line_segment(points[i], points[next]);
                if (!curve.value) return axiom::LoopId{};
                const auto edge = txn.create_edge(*curve.value, vertices[i], vertices[next]);
                if (!edge.value) return axiom::LoopId{};
                const auto coedge = txn.create_coedge(*edge.value, false);
                if (!coedge.value) return axiom::LoopId{};
                coedges[i] = *coedge.value;
            }
            const auto loop = txn.create_loop(coedges);
            return loop.value.value_or(axiom::LoopId{});
        };
        const auto outer = make_square_loop({{{0.0, 0.0, 0.0}, {4.0, 0.0, 0.0},
                                             {4.0, 4.0, 0.0}, {0.0, 4.0, 0.0}}});
        const auto inner = make_square_loop({{{1.0, 1.0, 0.0}, {1.0, 2.0, 0.0},
                                             {2.0, 2.0, 0.0}, {2.0, 1.0, 0.0}}});
        if (!outer.value || !inner.value) return 1;
        const auto holed_face = txn.create_face(*hole_plane.value, outer, std::array{inner});
        if (!holed_face.value || txn.commit().status != axiom::StatusCode::Ok) return 1;
        const auto holed_area = kernel.topology().query().planar_face_area(*holed_face.value);
        if (holed_area.status != axiom::StatusCode::Ok || !holed_area.value ||
            !approx(*holed_area.value, 15.0)) {
            std::cerr << "unexpected planar face area with an inner loop\n";
            return 1;
        }
    }

    auto line_t = kernel.curve_service().closest_parameter(*line.value, {2.5, 3.0, 0.0});
    if (line_t.status != axiom::StatusCode::Ok || !line_t.value.has_value() || !approx(*line_t.value, 2.5)) {
        std::cerr << "unexpected line closest parameter\n";
        return 1;
    }

    auto circle_t = kernel.curve_service().closest_parameter(*circle.value, {0.0, 2.0, 0.0});
    if (circle_t.status != axiom::StatusCode::Ok || !circle_t.value.has_value() ||
        !approx(*circle_t.value, std::acos(-1.0) * 0.5)) {
        std::cerr << "unexpected circle closest parameter\n";
        return 1;
    }

    auto tilted_circle_eval = kernel.curve_service().eval(*tilted_circle.value, std::acos(-1.0) * 0.5, 1);
    if (tilted_circle_eval.status != axiom::StatusCode::Ok || !tilted_circle_eval.value.has_value() ||
        !approx(tilted_circle_eval.value->point.x, 1.0) || !approx(tilted_circle_eval.value->point.y, 2.0) ||
        !approx(tilted_circle_eval.value->point.z, 5.0)) {
        std::cerr << "unexpected tilted circle eval\n";
        return 1;
    }

    auto tilted_circle_t = kernel.curve_service().closest_parameter(*tilted_circle.value, tilted_circle_eval.value->point);
    if (tilted_circle_t.status != axiom::StatusCode::Ok || !tilted_circle_t.value.has_value() ||
        !approx(*tilted_circle_t.value, std::acos(-1.0) * 0.5)) {
        std::cerr << "unexpected tilted circle closest parameter\n";
        return 1;
    }

    auto tilted_circle_closest =
        kernel.curve_service().closest_point(*tilted_circle.value, {1.0, 2.0, 8.0});
    if (tilted_circle_closest.status != axiom::StatusCode::Ok || !tilted_circle_closest.value.has_value() ||
        !approx(tilted_circle_closest.value->x, 1.0) || !approx(tilted_circle_closest.value->y, 2.0) ||
        !approx(tilted_circle_closest.value->z, 5.0)) {
        std::cerr << "unexpected tilted circle closest point\n";
        return 1;
    }

    auto bezier_t = kernel.curve_service().closest_parameter(*bezier.value, {1.0, 1.0, 0.0});
    if (bezier_t.status != axiom::StatusCode::Ok || !bezier_t.value.has_value() ||
        !approx(*bezier_t.value, 0.5, 0.05)) {
        std::cerr << "unexpected bezier closest parameter\n";
        return 1;
    }

    auto bspline_t = kernel.curve_service().closest_parameter(*bspline.value, {1.0, 0.5, 0.0});
    if (bspline_t.status != axiom::StatusCode::Ok || !bspline_t.value.has_value() ||
        !approx(*bspline_t.value, 1.5, 0.05)) {
        std::cerr << "unexpected bspline closest parameter\n";
        return 1;
    }

    auto nurbs_t = kernel.curve_service().closest_parameter(*nurbs.value, {1.0, 1.33, 0.0});
    if (nurbs_t.status != axiom::StatusCode::Ok || !nurbs_t.value.has_value() ||
        !approx(*nurbs_t.value, 0.5, 0.05)) {
        std::cerr << "unexpected nurbs closest parameter\n";
        return 1;
    }

    auto sphere_uv = kernel.surface_service().closest_uv(*sphere.value, {0.0, 0.0, 5.0});
    if (sphere_uv.status != axiom::StatusCode::Ok || !sphere_uv.value.has_value() ||
        !approx(sphere_uv.value->first, 0.0) || !approx(sphere_uv.value->second, 0.0)) {
        std::cerr << "unexpected sphere closest uv\n";
        return 1;
    }

    auto cylinder_uv = kernel.surface_service().closest_uv(*cylinder.value, {0.0, 3.0, 7.0});
    if (cylinder_uv.status != axiom::StatusCode::Ok || !cylinder_uv.value.has_value() ||
        !approx(cylinder_uv.value->first, std::acos(-1.0) * 0.5) || !approx(cylinder_uv.value->second, 7.0)) {
        std::cerr << "unexpected cylinder closest uv\n";
        return 1;
    }

    auto tilted_eval =
        kernel.surface_service().eval(*tilted_cylinder.value, std::acos(-1.0) * 0.5, 7.0, 1);
    if (tilted_eval.status != axiom::StatusCode::Ok || !tilted_eval.value.has_value() ||
        !approx(tilted_eval.value->point.x, 8.0) || !approx(tilted_eval.value->point.y, 2.0) ||
        !approx(tilted_eval.value->point.z, 6.0)) {
        std::cerr << "unexpected tilted cylinder eval\n";
        return 1;
    }

    auto tilted_uv = kernel.surface_service().closest_uv(*tilted_cylinder.value, tilted_eval.value->point);
    if (tilted_uv.status != axiom::StatusCode::Ok || !tilted_uv.value.has_value() ||
        !approx(tilted_uv.value->first, std::acos(-1.0) * 0.5) || !approx(tilted_uv.value->second, 7.0)) {
        std::cerr << "unexpected tilted cylinder closest uv\n";
        return 1;
    }

    auto cone_eval = kernel.surface_service().eval(*cone.value, 0.0, 2.0, 1);
    if (cone_eval.status != axiom::StatusCode::Ok || !cone_eval.value.has_value() ||
        !approx(cone_eval.value->point.x, 2.0) || !approx(cone_eval.value->point.y, 0.0) ||
        !approx(cone_eval.value->point.z, 2.0)) {
        std::cerr << "unexpected cone eval\n";
        return 1;
    }

    auto cone_uv = kernel.surface_service().closest_uv(*cone.value, cone_eval.value->point);
    if (cone_uv.status != axiom::StatusCode::Ok || !cone_uv.value.has_value() ||
        !approx(cone_uv.value->first, 0.0) || !approx(cone_uv.value->second, 2.0)) {
        std::cerr << "unexpected cone closest uv\n";
        return 1;
    }

    auto cone_closest = kernel.surface_service().closest_point(*cone.value, {4.0, 0.0, 1.0});
    if (cone_closest.status != axiom::StatusCode::Ok || !cone_closest.value.has_value() ||
        !approx(cone_closest.value->x, 2.5) || !approx(cone_closest.value->y, 0.0) ||
        !approx(cone_closest.value->z, 2.5)) {
        std::cerr << "unexpected cone closest point\n";
        return 1;
    }

    auto torus_eval = kernel.surface_service().eval(*torus.value, 0.0, 0.0, 1);
    if (torus_eval.status != axiom::StatusCode::Ok || !torus_eval.value.has_value() ||
        !approx(torus_eval.value->point.x, 7.0) || !approx(torus_eval.value->point.y, 0.0) ||
        !approx(torus_eval.value->point.z, 0.0)) {
        std::cerr << "unexpected torus eval\n";
        return 1;
    }

    auto torus_uv = kernel.surface_service().closest_uv(*torus.value, torus_eval.value->point);
    if (torus_uv.status != axiom::StatusCode::Ok || !torus_uv.value.has_value() ||
        !approx(torus_uv.value->first, 0.0) || !approx(torus_uv.value->second, 0.0)) {
        std::cerr << "unexpected torus closest uv\n";
        return 1;
    }

    auto torus_closest = kernel.surface_service().closest_point(*torus.value, {10.0, 0.0, 0.0});
    if (torus_closest.status != axiom::StatusCode::Ok || !torus_closest.value.has_value() ||
        !approx(torus_closest.value->x, 7.0) || !approx(torus_closest.value->y, 0.0) ||
        !approx(torus_closest.value->z, 0.0)) {
        std::cerr << "unexpected torus closest point\n";
        return 1;
    }

    auto bspline_surface_uv = kernel.surface_service().closest_uv(*bspline_surface.value, {0.5, 0.5, 0.25});
    if (bspline_surface_uv.status != axiom::StatusCode::Ok || !bspline_surface_uv.value.has_value() ||
        !approx(bspline_surface_uv.value->first, 0.5, 0.08) ||
        !approx(bspline_surface_uv.value->second, 0.5, 0.08)) {
        std::cerr << "unexpected bspline surface closest uv\n";
        return 1;
    }

    auto bspline_surface_closest =
        kernel.surface_service().closest_point(*bspline_surface.value, {0.5, 0.5, 0.25});
    if (bspline_surface_closest.status != axiom::StatusCode::Ok || !bspline_surface_closest.value.has_value() ||
        !approx(bspline_surface_closest.value->x, 0.5, 0.08) ||
        !approx(bspline_surface_closest.value->y, 0.5, 0.08) ||
        !approx(bspline_surface_closest.value->z, 0.25, 0.08)) {
        std::cerr << "unexpected bspline surface closest point\n";
        return 1;
    }

    auto nurbs_surface_uv = kernel.surface_service().closest_uv(*nurbs_surface.value, {0.666666666667, 0.666666666667, 0.5});
    if (nurbs_surface_uv.status != axiom::StatusCode::Ok || !nurbs_surface_uv.value.has_value() ||
        !approx(nurbs_surface_uv.value->first, 0.5, 0.08) ||
        !approx(nurbs_surface_uv.value->second, 0.5, 0.08)) {
        std::cerr << "unexpected nurbs surface closest uv\n";
        return 1;
    }

    auto body = kernel.primitives().box({0.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
    if (body.status != axiom::StatusCode::Ok || !body.value.has_value()) {
        std::cerr << "failed to create body for eval graph test\n";
        return 1;
    }

    const auto body_label = std::string("body:") + std::to_string(body.value->value);
    auto body_node = kernel.eval_graph().register_node(axiom::NodeKind::Geometry, body_label);
    auto cache_node = kernel.eval_graph().register_node(axiom::NodeKind::Cache, "cache:mass");
    auto analysis_node = kernel.eval_graph().register_node(axiom::NodeKind::Analysis, "analysis:report");
    if (body_node.status != axiom::StatusCode::Ok || cache_node.status != axiom::StatusCode::Ok ||
        analysis_node.status != axiom::StatusCode::Ok || !body_node.value.has_value() ||
        !cache_node.value.has_value() || !analysis_node.value.has_value()) {
        std::cerr << "failed to register eval graph nodes\n";
        return 1;
    }

    auto dep1 = kernel.eval_graph().add_dependency(*analysis_node.value, *cache_node.value);
    auto dep2 = kernel.eval_graph().add_dependency(*cache_node.value, *body_node.value);
    auto dep_cycle = kernel.eval_graph().add_dependency(*body_node.value, *analysis_node.value);
    if (dep1.status != axiom::StatusCode::Ok || dep2.status != axiom::StatusCode::Ok ||
        dep_cycle.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "unexpected eval graph dependency behavior\n";
        return 1;
    }
    auto dep_cycle_diag = kernel.diagnostics().get(dep_cycle.diagnostic_id);
    if (dep_cycle_diag.status != axiom::StatusCode::Ok || !dep_cycle_diag.value.has_value() ||
        !has_issue_code(*dep_cycle_diag.value, axiom::diag_codes::kEvalCycleDetected)) {
        std::cerr << "cycle dependency should carry eval cycle diagnostic code\n";
        return 1;
    }
    auto analysis_deps = kernel.eval_graph().dependencies_of(*analysis_node.value);
    auto body_dependents = kernel.eval_graph().dependents_of(*body_node.value);
    if (analysis_deps.status != axiom::StatusCode::Ok || !analysis_deps.value.has_value() ||
        analysis_deps.value->size() != 1 || analysis_deps.value->front().value != cache_node.value->value ||
        body_dependents.status != axiom::StatusCode::Ok || !body_dependents.value.has_value() ||
        body_dependents.value->size() != 1 || body_dependents.value->front().value != cache_node.value->value) {
        std::cerr << "unexpected eval graph dependency query behavior\n";
        return 1;
    }

    auto exists_body_node = kernel.eval_graph().exists(*body_node.value);
    auto kind_body_node = kernel.eval_graph().kind_of(*body_node.value);
    auto label_body_node = kernel.eval_graph().label_of(*body_node.value);
    auto set_label_body_node = kernel.eval_graph().set_label(*body_node.value, "body:relabeled");
    auto node_count = kernel.eval_graph().node_count();
    auto dep_count_analysis = kernel.eval_graph().dependency_count(*analysis_node.value);
    auto dependent_count_body = kernel.eval_graph().dependent_count(*body_node.value);
    auto has_dep = kernel.eval_graph().has_dependency(*analysis_node.value, *cache_node.value);
    auto all_nodes = kernel.eval_graph().all_nodes();
    auto find_by_label = kernel.eval_graph().find_by_label_token("body", 10);
    auto labels = kernel.eval_graph().labels_of_nodes(std::array<axiom::NodeId, 2>{*body_node.value, *cache_node.value});
    auto is_leaf_body = kernel.eval_graph().is_leaf(*body_node.value);
    auto is_root_analysis = kernel.eval_graph().is_root(*analysis_node.value);
    auto body_binding_count = kernel.eval_graph().body_binding_count(*body.value);
    auto body_nodes = kernel.eval_graph().nodes_of_body(*body.value);
    if (exists_body_node.status != axiom::StatusCode::Ok || !exists_body_node.value.has_value() || !*exists_body_node.value ||
        kind_body_node.status != axiom::StatusCode::Ok || !kind_body_node.value.has_value() || *kind_body_node.value != axiom::NodeKind::Geometry ||
        label_body_node.status != axiom::StatusCode::Ok || !label_body_node.value.has_value() || label_body_node.value->empty() ||
        set_label_body_node.status != axiom::StatusCode::Ok ||
        node_count.status != axiom::StatusCode::Ok || !node_count.value.has_value() || *node_count.value < 3 ||
        dep_count_analysis.status != axiom::StatusCode::Ok || !dep_count_analysis.value.has_value() || *dep_count_analysis.value != 1 ||
        dependent_count_body.status != axiom::StatusCode::Ok || !dependent_count_body.value.has_value() || *dependent_count_body.value != 1 ||
        has_dep.status != axiom::StatusCode::Ok || !has_dep.value.has_value() || !*has_dep.value ||
        all_nodes.status != axiom::StatusCode::Ok || !all_nodes.value.has_value() || all_nodes.value->size() < 3 ||
        find_by_label.status != axiom::StatusCode::Ok || !find_by_label.value.has_value() || find_by_label.value->empty() ||
        labels.status != axiom::StatusCode::Ok || !labels.value.has_value() || labels.value->size() != 2 ||
        is_leaf_body.status != axiom::StatusCode::Ok || !is_leaf_body.value.has_value() || !*is_leaf_body.value ||
        is_root_analysis.status != axiom::StatusCode::Ok || !is_root_analysis.value.has_value() || !*is_root_analysis.value ||
        body_binding_count.status != axiom::StatusCode::Ok || !body_binding_count.value.has_value() || *body_binding_count.value == 0 ||
        body_nodes.status != axiom::StatusCode::Ok || !body_nodes.value.has_value() || body_nodes.value->empty()) {
        std::cerr << "unexpected extended eval graph query behavior\n";
        return 1;
    }

    auto invalidate_body = kernel.eval_graph().invalidate_body(*body.value);
    if (invalidate_body.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to invalidate body-linked nodes\n";
        return 1;
    }

    auto body_invalid = kernel.eval_graph().is_invalid(*body_node.value);
    auto cache_invalid = kernel.eval_graph().is_invalid(*cache_node.value);
    auto analysis_invalid = kernel.eval_graph().is_invalid(*analysis_node.value);
    if (body_invalid.status != axiom::StatusCode::Ok || cache_invalid.status != axiom::StatusCode::Ok ||
        analysis_invalid.status != axiom::StatusCode::Ok || !body_invalid.value.has_value() ||
        !cache_invalid.value.has_value() || !analysis_invalid.value.has_value() ||
        !*body_invalid.value || !*cache_invalid.value || !*analysis_invalid.value) {
        std::cerr << "unexpected invalidation propagation\n";
        return 1;
    }

    auto recompute = kernel.eval_graph().recompute(*analysis_node.value);
    if (recompute.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to recompute analysis node\n";
        return 1;
    }
    auto total_recompute_before_reset = kernel.eval_graph().total_recompute_count();
    if (total_recompute_before_reset.status != axiom::StatusCode::Ok || !total_recompute_before_reset.value.has_value() ||
        *total_recompute_before_reset.value == 0) {
        std::cerr << "unexpected total recompute count\n";
        return 1;
    }
    auto body_count = kernel.eval_graph().recompute_count(*body_node.value);
    auto cache_count = kernel.eval_graph().recompute_count(*cache_node.value);
    auto analysis_count = kernel.eval_graph().recompute_count(*analysis_node.value);
    if (body_count.status != axiom::StatusCode::Ok || cache_count.status != axiom::StatusCode::Ok ||
        analysis_count.status != axiom::StatusCode::Ok || !body_count.value.has_value() ||
        !cache_count.value.has_value() || !analysis_count.value.has_value() ||
        *body_count.value != 1 || *cache_count.value != 1 || *analysis_count.value != 1) {
        std::cerr << "unexpected recompute counts\n";
        return 1;
    }

    auto egm_after_first = kernel.eval_graph_metrics();
    if (egm_after_first.status != axiom::StatusCode::Ok || !egm_after_first.value.has_value() ||
        egm_after_first.value->recompute_events_total != *total_recompute_before_reset.value ||
        egm_after_first.value->max_per_node_recompute_count != 1 ||
        egm_after_first.value->nodes_with_recompute_nonzero != 3 ||
        !approx(egm_after_first.value->mean_recompute_events_per_node, 1.0, 1e-9) ||
        !approx(egm_after_first.value->mean_recompute_events_per_touched_node, 1.0, 1e-9)) {
        std::cerr << "eval_graph_metrics recompute distribution unexpected after first recompute\n";
        return 1;
    }

    auto tel_after_first_recompute = kernel.eval_graph().telemetry();
    if (tel_after_first_recompute.status != axiom::StatusCode::Ok || !tel_after_first_recompute.value.has_value() ||
        tel_after_first_recompute.value->invalidate_body_calls != 1 ||
        tel_after_first_recompute.value->recompute_finish_events != 3 ||
        tel_after_first_recompute.value->recompute_single_root_max_finish_nodes != 3 ||
        tel_after_first_recompute.value->recompute_single_root_max_stack_depth != 3) {
        std::cerr << "unexpected eval graph telemetry after first recompute\n";
        return 1;
    }

    auto analysis_invalid_after = kernel.eval_graph().is_invalid(*analysis_node.value);
    if (analysis_invalid_after.status != axiom::StatusCode::Ok || !analysis_invalid_after.value.has_value() ||
        *analysis_invalid_after.value) {
        std::cerr << "expected analysis node to be valid after recompute\n";
        return 1;
    }

    auto offset_result = kernel.modify().offset_body(*body.value, 0.2, {});
    if (offset_result.status != axiom::StatusCode::Ok || !offset_result.value.has_value()) {
        std::cerr << "failed to run offset operation for eval graph linkage test\n";
        return 1;
    }

    auto body_invalid_after_offset = kernel.eval_graph().is_invalid(*body_node.value);
    auto cache_invalid_after_offset = kernel.eval_graph().is_invalid(*cache_node.value);
    auto analysis_invalid_after_offset = kernel.eval_graph().is_invalid(*analysis_node.value);
    if (body_invalid_after_offset.status != axiom::StatusCode::Ok ||
        cache_invalid_after_offset.status != axiom::StatusCode::Ok ||
        analysis_invalid_after_offset.status != axiom::StatusCode::Ok ||
        !body_invalid_after_offset.value.has_value() ||
        !cache_invalid_after_offset.value.has_value() ||
        !analysis_invalid_after_offset.value.has_value() ||
        !*body_invalid_after_offset.value ||
        !*cache_invalid_after_offset.value ||
        !*analysis_invalid_after_offset.value) {
        std::cerr << "expected eval graph to be invalidated by topology-changing operation\n";
        return 1;
    }

    auto recompute_after_offset = kernel.eval_graph().recompute(*analysis_node.value);
    if (recompute_after_offset.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to recompute analysis node after offset invalidation\n";
        return 1;
    }
    auto body_count_after_offset = kernel.eval_graph().recompute_count(*body_node.value);
    auto cache_count_after_offset = kernel.eval_graph().recompute_count(*cache_node.value);
    auto analysis_count_after_offset = kernel.eval_graph().recompute_count(*analysis_node.value);
    if (body_count_after_offset.status != axiom::StatusCode::Ok ||
        cache_count_after_offset.status != axiom::StatusCode::Ok ||
        analysis_count_after_offset.status != axiom::StatusCode::Ok ||
        !body_count_after_offset.value.has_value() ||
        !cache_count_after_offset.value.has_value() ||
        !analysis_count_after_offset.value.has_value() ||
        *body_count_after_offset.value != 2 ||
        *cache_count_after_offset.value != 2 ||
        *analysis_count_after_offset.value != 2) {
        std::cerr << "unexpected recompute counts after topology-linked invalidation\n";
        return 1;
    }

    auto tel_after_second_recompute = kernel.eval_graph().telemetry();
    if (tel_after_second_recompute.status != axiom::StatusCode::Ok || !tel_after_second_recompute.value.has_value() ||
        tel_after_second_recompute.value->recompute_finish_events != 6) {
        std::cerr << "unexpected eval graph telemetry after second recompute\n";
        return 1;
    }
    if (kernel.eval_graph().reset_telemetry().status != axiom::StatusCode::Ok) {
        std::cerr << "failed to reset eval graph telemetry\n";
        return 1;
    }
    auto tel_reset = kernel.eval_graph().telemetry();
    if (tel_reset.status != axiom::StatusCode::Ok || !tel_reset.value.has_value() ||
        tel_reset.value->invalidate_body_calls != 0 || tel_reset.value->recompute_finish_events != 0 ||
        tel_reset.value->invalidate_node_redundant_calls != 0 ||
        tel_reset.value->recompute_root_already_valid_calls != 0 ||
        tel_reset.value->recompute_single_root_max_finish_nodes != 0 ||
        tel_reset.value->recompute_single_root_max_stack_depth != 0) {
        std::cerr << "eval graph telemetry not cleared by reset_telemetry\n";
        return 1;
    }

    if (kernel.eval_graph().reset_recompute_count(*analysis_node.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().reset_all_recompute_counts().status != axiom::StatusCode::Ok) {
        std::cerr << "failed to reset recompute counters\n";
        return 1;
    }

    auto shared_leaf = kernel.eval_graph().register_node(axiom::NodeKind::Geometry, "shared:leaf");
    auto cache_branch_1 = kernel.eval_graph().register_node(axiom::NodeKind::Cache, "shared:cache:1");
    auto cache_branch_2 = kernel.eval_graph().register_node(axiom::NodeKind::Cache, "shared:cache:2");
    auto analysis_root = kernel.eval_graph().register_node(axiom::NodeKind::Analysis, "shared:analysis");
    if (shared_leaf.status != axiom::StatusCode::Ok || cache_branch_1.status != axiom::StatusCode::Ok ||
        cache_branch_2.status != axiom::StatusCode::Ok || analysis_root.status != axiom::StatusCode::Ok ||
        !shared_leaf.value.has_value() || !cache_branch_1.value.has_value() ||
        !cache_branch_2.value.has_value() || !analysis_root.value.has_value()) {
        std::cerr << "failed to create shared dependency graph\n";
        return 1;
    }
    if (kernel.eval_graph().add_dependency(*cache_branch_1.value, *shared_leaf.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().add_dependency(*cache_branch_2.value, *shared_leaf.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().add_dependency(*analysis_root.value, *cache_branch_1.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().add_dependency(*analysis_root.value, *cache_branch_2.value).status != axiom::StatusCode::Ok) {
        std::cerr << "failed to setup shared dependency graph\n";
        return 1;
    }
    if (kernel.eval_graph().invalidate(*shared_leaf.value).status != axiom::StatusCode::Ok) {
        std::cerr << "failed to invalidate shared dependency leaf\n";
        return 1;
    }
    if (kernel.eval_graph().recompute(*analysis_root.value).status != axiom::StatusCode::Ok) {
        std::cerr << "failed to recompute shared dependency graph\n";
        return 1;
    }
    auto shared_leaf_count = kernel.eval_graph().recompute_count(*shared_leaf.value);
    if (shared_leaf_count.status != axiom::StatusCode::Ok || !shared_leaf_count.value.has_value() ||
        *shared_leaf_count.value != 1) {
        std::cerr << "shared leaf should be recomputed once in DAG traversal\n";
        return 1;
    }
    if (kernel.eval_graph().invalidate_many(std::array<axiom::NodeId, 2>{*cache_branch_1.value, *cache_branch_2.value}).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().recompute_many(std::array<axiom::NodeId, 2>{*cache_branch_1.value, *cache_branch_2.value}).status != axiom::StatusCode::Ok) {
        std::cerr << "batch invalidate/recompute failed\n";
        return 1;
    }
    auto tel_recompute_many = kernel.eval_graph().telemetry();
    if (tel_recompute_many.status != axiom::StatusCode::Ok || !tel_recompute_many.value.has_value() ||
        tel_recompute_many.value->recompute_many_batches != 1 ||
        tel_recompute_many.value->recompute_many_root_total != 2) {
        std::cerr << "unexpected recompute_many telemetry\n";
        return 1;
    }
    if (kernel.eval_graph().clear_dependencies(*analysis_root.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().clear_dependents(*shared_leaf.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().remove_dependency(*cache_branch_1.value, *shared_leaf.value).status != axiom::StatusCode::Ok) {
        std::cerr << "dependency clear/remove failed\n";
        return 1;
    }
    auto invalid_list = kernel.eval_graph().invalid_nodes();
    auto valid_list = kernel.eval_graph().valid_nodes();
    auto kind_nodes = kernel.eval_graph().nodes_of_kind(axiom::NodeKind::Cache);
    auto ids_asc = kernel.eval_graph().ids_sorted_asc();
    auto ids_desc = kernel.eval_graph().ids_sorted_desc();
    auto invalid_ratio = kernel.eval_graph().invalid_ratio();
    auto recompute_counts = kernel.eval_graph().recompute_counts_of(std::array<axiom::NodeId, 2>{*shared_leaf.value, *analysis_root.value});
    auto total_dep_edges = kernel.eval_graph().total_dependency_edges();
    auto total_rev_edges = kernel.eval_graph().total_reverse_dependency_edges();
    auto isolated = kernel.eval_graph().isolated_nodes();
    auto pruned = kernel.eval_graph().prune_dangling_dependencies();
    auto relabeled = kernel.eval_graph().relabel_by_prefix("shared:", "eval:");
    auto relabel_many = kernel.eval_graph().relabel_many(std::array<axiom::NodeId, 1>{*analysis_root.value}, "batch:");
    auto dep_pairs = kernel.eval_graph().dependency_pairs();
    auto rev_pairs = kernel.eval_graph().reverse_dependency_pairs();
    auto max_recompute_node = kernel.eval_graph().max_recompute_count_node();
    auto min_recompute_node = kernel.eval_graph().min_recompute_count_node();
    auto nodes_min_recompute = kernel.eval_graph().nodes_with_min_recompute(0);
    auto nodes_max_recompute = kernel.eval_graph().nodes_with_max_recompute(100);
    auto invalid_by_kind = kernel.eval_graph().invalidate_by_kind(axiom::NodeKind::Cache);
    auto recompute_by_kind = kernel.eval_graph().recompute_by_kind(axiom::NodeKind::Cache);
    auto contains_token = kernel.eval_graph().contains_label_token("batch");
    auto label_hist = kernel.eval_graph().label_histogram_prefix(5);
    auto bound_bodies = kernel.eval_graph().body_binding_bodies();
    auto bound_body_count = kernel.eval_graph().bound_body_count();
    auto has_any_invalid = kernel.eval_graph().has_any_invalid();
    auto has_any_dep = kernel.eval_graph().has_any_dependency();
    auto invalid_kind_nodes = kernel.eval_graph().invalid_nodes_of_kind(axiom::NodeKind::Cache);
    auto valid_kind_nodes = kernel.eval_graph().valid_nodes_of_kind(axiom::NodeKind::Cache);
    if (invalid_list.status != axiom::StatusCode::Ok || !invalid_list.value.has_value() ||
        valid_list.status != axiom::StatusCode::Ok || !valid_list.value.has_value() ||
        kind_nodes.status != axiom::StatusCode::Ok || !kind_nodes.value.has_value() ||
        ids_asc.status != axiom::StatusCode::Ok || !ids_asc.value.has_value() ||
        ids_desc.status != axiom::StatusCode::Ok || !ids_desc.value.has_value() ||
        invalid_ratio.status != axiom::StatusCode::Ok || !invalid_ratio.value.has_value() ||
        recompute_counts.status != axiom::StatusCode::Ok || !recompute_counts.value.has_value() || recompute_counts.value->size() != 2 ||
        total_dep_edges.status != axiom::StatusCode::Ok || !total_dep_edges.value.has_value() ||
        total_rev_edges.status != axiom::StatusCode::Ok || !total_rev_edges.value.has_value() ||
        isolated.status != axiom::StatusCode::Ok || !isolated.value.has_value() ||
        pruned.status != axiom::StatusCode::Ok || !pruned.value.has_value() ||
        relabeled.status != axiom::StatusCode::Ok || !relabeled.value.has_value() ||
        relabel_many.status != axiom::StatusCode::Ok ||
        dep_pairs.status != axiom::StatusCode::Ok || !dep_pairs.value.has_value() ||
        rev_pairs.status != axiom::StatusCode::Ok || !rev_pairs.value.has_value() ||
        max_recompute_node.status != axiom::StatusCode::Ok || !max_recompute_node.value.has_value() ||
        min_recompute_node.status != axiom::StatusCode::Ok || !min_recompute_node.value.has_value() ||
        nodes_min_recompute.status != axiom::StatusCode::Ok || !nodes_min_recompute.value.has_value() ||
        nodes_max_recompute.status != axiom::StatusCode::Ok || !nodes_max_recompute.value.has_value() ||
        invalid_by_kind.status != axiom::StatusCode::Ok ||
        recompute_by_kind.status != axiom::StatusCode::Ok ||
        contains_token.status != axiom::StatusCode::Ok || !contains_token.value.has_value() || !*contains_token.value ||
        label_hist.status != axiom::StatusCode::Ok || !label_hist.value.has_value() ||
        bound_bodies.status != axiom::StatusCode::Ok || !bound_bodies.value.has_value() ||
        bound_body_count.status != axiom::StatusCode::Ok || !bound_body_count.value.has_value() ||
        has_any_invalid.status != axiom::StatusCode::Ok || !has_any_invalid.value.has_value() ||
        has_any_dep.status != axiom::StatusCode::Ok || !has_any_dep.value.has_value() || !*has_any_dep.value ||
        invalid_kind_nodes.status != axiom::StatusCode::Ok || !invalid_kind_nodes.value.has_value() ||
        valid_kind_nodes.status != axiom::StatusCode::Ok || !valid_kind_nodes.value.has_value()) {
        std::cerr << "invalid/valid/kind query failed\n";
        return 1;
    }
    if (kernel.eval_graph().remove_nodes_many(std::array<axiom::NodeId, 1>{*cache_branch_2.value}).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().clear_nodes_of_kind(axiom::NodeKind::Cache).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().unbind_body(*body.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().unbind_all_bodies().status != axiom::StatusCode::Ok) {
        std::cerr << "batch remove/clear/unbind failed\n";
        return 1;
    }
    if (kernel.eval_graph().remove_node(*analysis_root.value).status != axiom::StatusCode::Ok) {
        std::cerr << "remove node failed\n";
        return 1;
    }
    auto clear_graph = kernel.eval_graph().clear_graph();
    auto node_count_after_clear = kernel.eval_graph().node_count();
    if (clear_graph.status != axiom::StatusCode::Ok ||
        node_count_after_clear.status != axiom::StatusCode::Ok ||
        !node_count_after_clear.value.has_value() || *node_count_after_clear.value != 0) {
        std::cerr << "clear graph failed\n";
        return 1;
    }

    auto on_curve = kernel.predicates().point_on_curve({0.5, 0.0, 0.0}, *line.value, 1e-6);
    auto on_surface = kernel.predicates().point_on_surface({0.0, 0.0, 5.0}, *sphere.value, 1e-6);
    auto inside_body = kernel.predicates().point_in_body({1.0, 1.0, 1.0}, *body.value, 1e-6);
    auto outside_body = kernel.predicates().point_in_body({5.0, 5.0, 5.0}, *body.value, 1e-6);
    if (on_curve.status != axiom::StatusCode::Ok || !on_curve.value.has_value() || !*on_curve.value ||
        on_curve.diagnostic_id.value == 0 ||
        on_surface.status != axiom::StatusCode::Ok || !on_surface.value.has_value() || !*on_surface.value ||
        on_surface.diagnostic_id.value == 0 ||
        inside_body.status != axiom::StatusCode::Ok || !inside_body.value.has_value() || !*inside_body.value ||
        inside_body.diagnostic_id.value == 0 ||
        outside_body.status != axiom::StatusCode::Ok || !outside_body.value.has_value() || *outside_body.value) {
        std::cerr << "unexpected predicate service success behavior\n";
        return 1;
    }

    auto invalid_curve_pred = kernel.predicates().point_on_curve({0.0, 0.0, 0.0}, axiom::CurveId {999999}, 1e-6);
    if (invalid_curve_pred.status != axiom::StatusCode::InvalidInput || invalid_curve_pred.diagnostic_id.value == 0) {
        std::cerr << "invalid curve predicate should return structured failure\n";
        return 1;
    }
    auto invalid_curve_diag = kernel.diagnostics().get(invalid_curve_pred.diagnostic_id);
    if (invalid_curve_diag.status != axiom::StatusCode::Ok || !invalid_curve_diag.value.has_value() ||
        !has_issue_code(*invalid_curve_diag.value, axiom::diag_codes::kCoreInvalidHandle)) {
        std::cerr << "invalid curve predicate diagnostic is unexpected\n";
        return 1;
    }

    auto invalid_body_pred = kernel.predicates().point_in_body({0.0, 0.0, 0.0}, axiom::BodyId {999999}, 1e-6);
    if (invalid_body_pred.status != axiom::StatusCode::InvalidInput || invalid_body_pred.diagnostic_id.value == 0) {
        std::cerr << "invalid body predicate should return structured failure\n";
        return 1;
    }

    {
        axiom::Kernel k_tel;
        auto n = k_tel.eval_graph().register_node(axiom::NodeKind::Cache, "telemetry:probe");
        if (n.status != axiom::StatusCode::Ok || !n.value.has_value()) {
            std::cerr << "telemetry probe node register failed\n";
            return 1;
        }
        auto ex0 = k_tel.eval_graph().exists(*n.value);
        auto inv0 = k_tel.eval_graph().is_invalid(*n.value);
        auto rc0 = k_tel.eval_graph().recompute_count(*n.value);
        if (ex0.status != axiom::StatusCode::Ok || !ex0.value.has_value() || !*ex0.value ||
            inv0.status != axiom::StatusCode::Ok || !inv0.value.has_value() || *inv0.value ||
            rc0.status != axiom::StatusCode::Ok || !rc0.value.has_value() || *rc0.value != 0) {
            std::cerr << "telemetry probe initial state reads failed\n";
            return 1;
        }
        if (k_tel.eval_graph().invalidate(*n.value).status != axiom::StatusCode::Ok ||
            k_tel.eval_graph().invalidate(*n.value).status != axiom::StatusCode::Ok) {
            std::cerr << "telemetry probe double invalidate failed\n";
            return 1;
        }
        auto t1 = k_tel.eval_graph().telemetry();
        if (t1.status != axiom::StatusCode::Ok || !t1.value.has_value() ||
            t1.value->invalidate_node_redundant_calls != 1U) {
            std::cerr << "expected one redundant invalidate_node call on already-invalid root\n";
            return 1;
        }
        if (k_tel.eval_graph().recompute(*n.value).status != axiom::StatusCode::Ok ||
            k_tel.eval_graph().recompute(*n.value).status != axiom::StatusCode::Ok) {
            std::cerr << "telemetry probe double recompute failed\n";
            return 1;
        }
        auto t2 = k_tel.eval_graph().telemetry();
        if (t2.status != axiom::StatusCode::Ok || !t2.value.has_value() ||
            t2.value->recompute_root_already_valid_calls != 1U) {
            std::cerr << "expected one recompute_root_already_valid when root was not invalid\n";
            return 1;
        }
        (void)k_tel.eval_graph().exists(*n.value);
        (void)k_tel.eval_graph().is_invalid(*n.value);
        (void)k_tel.eval_graph().recompute_count(*n.value);
        auto t3 = k_tel.eval_graph().telemetry();
        if (t3.status != axiom::StatusCode::Ok || !t3.value.has_value() ||
            t3.value->eval_graph_state_read_calls != t2.value->eval_graph_state_read_calls + 3U) {
            std::cerr << "expected eval_graph_state_read_calls to increase by three per read batch\n";
            return 1;
        }
    }

    return 0;
}
