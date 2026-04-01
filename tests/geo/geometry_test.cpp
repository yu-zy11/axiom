#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#include "axiom/sdk/kernel.h"

namespace {

bool approx(double lhs, double rhs, double eps = 1e-6) {
    return std::abs(lhs - rhs) <= eps;
}

}  // namespace

int main() {
    axiom::Kernel kernel;

    // ---- Stage 2: PCurve minimal support (polyline in UV space) ----
    {
        const std::array<axiom::Point2, 3> uv_poly {{ {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0} }};
        auto pc = kernel.pcurves().make_polyline(uv_poly);
        if (pc.status != axiom::StatusCode::Ok || !pc.value.has_value()) {
            std::cerr << "failed to create pcurve\n";
            return 1;
        }
        auto pc_domain = kernel.pcurve_service().domain(*pc.value);
        if (pc_domain.status != axiom::StatusCode::Ok || !pc_domain.value.has_value() ||
            !approx(pc_domain.value->min, 0.0) || !approx(pc_domain.value->max, 2.0)) {
            std::cerr << "unexpected pcurve domain\n";
            return 1;
        }
        auto pc_eval = kernel.pcurve_service().eval(*pc.value, 0.5, 1);
        if (pc_eval.status != axiom::StatusCode::Ok || !pc_eval.value.has_value() ||
            !approx(pc_eval.value->point.x, 0.5) || !approx(pc_eval.value->point.y, 0.0)) {
            std::cerr << "unexpected pcurve eval\n";
            return 1;
        }
        auto pc_ev2 = kernel.pcurve_service().eval(*pc.value, 0.5, 2);
        if (pc_ev2.status != axiom::StatusCode::Ok || !pc_ev2.value.has_value() ||
            !std::isfinite(pc_ev2.value->curvature) || !approx(pc_ev2.value->curvature, 0.0, 1e-3)) {
            std::cerr << "unexpected pcurve curvature on straight segment\n";
            return 1;
        }
        auto pc_closest_t = kernel.pcurve_service().closest_parameter(*pc.value, {1.2, 0.2});
        auto pc_closest_p = kernel.pcurve_service().closest_point(*pc.value, {1.2, 0.2});
        if (pc_closest_t.status != axiom::StatusCode::Ok || !pc_closest_t.value.has_value() ||
            pc_closest_p.status != axiom::StatusCode::Ok || !pc_closest_p.value.has_value()) {
            std::cerr << "unexpected pcurve closest result\n";
            return 1;
        }
        if (*pc_closest_t.value < pc_domain.value->min || *pc_closest_t.value > pc_domain.value->max) {
            std::cerr << "unexpected pcurve closest_parameter range\n";
            return 1;
        }
    }

    auto line = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    if (line.status != axiom::StatusCode::Ok || !line.value.has_value()) {
        std::cerr << "failed to create line\n";
        return 1;
    }

    // ---- Stage 2: missing curve types (line segment / parabola / hyperbola) ----
    auto seg = kernel.curves().make_line_segment({0.0, 0.0, 0.0}, {2.0, 0.0, 0.0});
    if (seg.status != axiom::StatusCode::Ok || !seg.value.has_value()) {
        std::cerr << "failed to create segment\n";
        return 1;
    }
    auto seg_domain = kernel.curve_service().domain(*seg.value);
    if (seg_domain.status != axiom::StatusCode::Ok || !seg_domain.value.has_value() ||
        !approx(seg_domain.value->min, 0.0) || !approx(seg_domain.value->max, 1.0)) {
        std::cerr << "unexpected segment domain\n";
        return 1;
    }
    auto seg_eval = kernel.curve_service().eval(*seg.value, 0.25, 1);
    if (seg_eval.status != axiom::StatusCode::Ok || !seg_eval.value.has_value() ||
        !approx(seg_eval.value->point.x, 0.5)) {
        std::cerr << "unexpected segment eval\n";
        return 1;
    }
    auto seg_cp = kernel.curve_service().closest_parameter(*seg.value, {100.0, 0.0, 0.0});
    if (seg_cp.status != axiom::StatusCode::Ok || !seg_cp.value.has_value() ||
        *seg_cp.value < 0.0 || *seg_cp.value > 1.0) {
        std::cerr << "unexpected segment closest_parameter\n";
        return 1;
    }

    auto parabola = kernel.curves().make_parabola({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 0.5);
    if (parabola.status != axiom::StatusCode::Ok || !parabola.value.has_value()) {
        std::cerr << "failed to create parabola\n";
        return 1;
    }
    auto parabola_domain = kernel.curve_service().domain(*parabola.value);
    if (parabola_domain.status != axiom::StatusCode::Ok || !parabola_domain.value.has_value() ||
        !approx(parabola_domain.value->min, -10.0) || !approx(parabola_domain.value->max, 10.0)) {
        if (parabola_domain.value.has_value()) {
            std::cerr << "unexpected parabola domain: [" << parabola_domain.value->min << ", " << parabola_domain.value->max << "]\n";
        } else {
            std::cerr << "unexpected parabola domain: no value\n";
        }
        return 1;
    }
    auto parabola_eval = kernel.curve_service().eval(*parabola.value, 0.5, 1);
    if (parabola_eval.status != axiom::StatusCode::Ok || !parabola_eval.value.has_value() ||
        !approx(parabola_eval.value->point.x, 0.5) || !approx(parabola_eval.value->point.y, 0.125)) {
        std::cerr << "unexpected parabola eval\n";
        return 1;
    }

    auto hyperbola = kernel.curves().make_hyperbola({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1.0, 0.5);
    if (hyperbola.status != axiom::StatusCode::Ok || !hyperbola.value.has_value()) {
        std::cerr << "failed to create hyperbola\n";
        return 1;
    }
    auto hyperbola_domain = kernel.curve_service().domain(*hyperbola.value);
    if (hyperbola_domain.status != axiom::StatusCode::Ok || !hyperbola_domain.value.has_value() ||
        !approx(hyperbola_domain.value->min, -10.0) || !approx(hyperbola_domain.value->max, 10.0)) {
        std::cerr << "unexpected hyperbola domain\n";
        return 1;
    }
    auto hyperbola_eval = kernel.curve_service().eval(*hyperbola.value, 0.0, 1);
    if (hyperbola_eval.status != axiom::StatusCode::Ok || !hyperbola_eval.value.has_value() ||
        !approx(hyperbola_eval.value->point.x, 1.0) || !approx(hyperbola_eval.value->point.y, 0.0)) {
        std::cerr << "unexpected hyperbola eval\n";
        return 1;
    }

    auto line_eval = kernel.curve_service().eval(*line.value, 2.0, 1);
    if (line_eval.status != axiom::StatusCode::Ok || !line_eval.value.has_value()) {
        std::cerr << "failed to eval line\n";
        return 1;
    }
    auto line_eval_high = kernel.curve_service().eval(*line.value, 0.5, 3);
    if (line_eval_high.status != axiom::StatusCode::Ok || !line_eval_high.value.has_value() ||
        line_eval_high.value->derivatives.size() != 3) {
        std::cerr << "unexpected line high-order derivative placeholder\n";
        return 1;
    }
    const std::vector<double> line_ts {0.0, 1.0, 2.0};
    auto line_eval_batch = kernel.curve_service().eval_batch(*line.value, line_ts, 1);
    auto line_bbox_batch = kernel.curve_service().bbox_batch(std::array<axiom::CurveId, 1> {*line.value});
    if (line_eval_batch.status != axiom::StatusCode::Ok || !line_eval_batch.value.has_value() ||
        line_bbox_batch.status != axiom::StatusCode::Ok || !line_bbox_batch.value.has_value() ||
        line_bbox_batch.value->size() != 1 ||
        line_eval_batch.value->size() != 3 || !approx(line_eval_batch.value->at(2).point.x, 2.0)) {
        std::cerr << "unexpected line eval batch result\n";
        return 1;
    }

    if (!approx(line_eval.value->point.x, 2.0) || !approx(line_eval.value->point.y, 0.0) ||
        !approx(line_eval.value->point.z, 0.0)) {
        std::cerr << "unexpected line eval result\n";
        return 1;
    }

    auto sphere = kernel.surfaces().make_sphere({0.0, 0.0, 0.0}, 5.0);
    if (sphere.status != axiom::StatusCode::Ok || !sphere.value.has_value()) {
        std::cerr << "failed to create sphere\n";
        return 1;
    }
    auto sphere_domain = kernel.surface_service().domain(*sphere.value);
    if (sphere_domain.status != axiom::StatusCode::Ok || !sphere_domain.value.has_value() ||
        !approx(sphere_domain.value->u.min, 0.0) ||
        !approx(sphere_domain.value->u.max, std::acos(-1.0) * 2.0, 1e-8) ||
        !approx(sphere_domain.value->v.min, 0.0) ||
        !approx(sphere_domain.value->v.max, std::acos(-1.0), 1e-8)) {
        std::cerr << "unexpected sphere domain\n";
        return 1;
    }

    auto surf_eval = kernel.surface_service().eval(*sphere.value, 0.0, 0.0, 1);
    auto sphere_bbox_batch = kernel.surface_service().bbox_batch(std::array<axiom::SurfaceId, 1> {*sphere.value});
    if (surf_eval.status != axiom::StatusCode::Ok || !surf_eval.value.has_value() ||
        sphere_bbox_batch.status != axiom::StatusCode::Ok || !sphere_bbox_batch.value.has_value() ||
        sphere_bbox_batch.value->size() != 1) {
        std::cerr << "failed to eval sphere\n";
        return 1;
    }

    if (!approx(surf_eval.value->point.z, 5.0)) {
        std::cerr << "unexpected sphere eval result\n";
        return 1;
    }
    // Curvature sanity for sphere: k1 == k2 == 1/r.
    if (!approx(surf_eval.value->k1, 1.0 / 5.0, 1e-8) || !approx(surf_eval.value->k2, 1.0 / 5.0, 1e-8)) {
        std::cerr << "unexpected sphere curvature\n";
        return 1;
    }
    auto sphere_uv_center = kernel.surface_service().closest_uv(*sphere.value, {0.0, 0.0, 0.0});
    if (sphere_uv_center.status != axiom::StatusCode::Ok || !sphere_uv_center.value.has_value() ||
        !approx(sphere_uv_center.value->first, 0.0) || !approx(sphere_uv_center.value->second, 0.0)) {
        std::cerr << "unexpected sphere center closest uv behavior\n";
        return 1;
    }

    auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value()) {
        std::cerr << "failed to create plane\n";
        return 1;
    }
    auto plane_domain = kernel.surface_service().domain(*plane.value);
    if (plane_domain.status != axiom::StatusCode::Ok || !plane_domain.value.has_value() ||
        !std::isinf(plane_domain.value->u.min) || !std::isinf(plane_domain.value->u.max) ||
        !std::isinf(plane_domain.value->v.min) || !std::isinf(plane_domain.value->v.max)) {
        std::cerr << "unexpected plane domain\n";
        return 1;
    }

    auto ellipse = kernel.curves().make_ellipse({0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 3.0, 0.0});
    if (ellipse.status != axiom::StatusCode::Ok || !ellipse.value.has_value()) {
        std::cerr << "failed to create ellipse\n";
        return 1;
    }
    {
        auto segment2 = kernel.curves().make_line_segment({0.0, 0.0, 0.0}, {1.0, 2.0, 0.0});
        if (segment2.status != axiom::StatusCode::Ok || !segment2.value.has_value()) {
            std::cerr << "failed to create segment\n";
            return 1;
        }
        auto parabola2 = kernel.curves().make_parabola({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 0.5);
        if (parabola2.status != axiom::StatusCode::Ok || !parabola2.value.has_value()) {
            std::cerr << "failed to create parabola\n";
            return 1;
        }
        auto hyperbola2 = kernel.curves().make_hyperbola({0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1.0, 0.5);
        if (hyperbola2.status != axiom::StatusCode::Ok || !hyperbola2.value.has_value()) {
            std::cerr << "failed to create hyperbola\n";
            return 1;
        }
        const std::array<axiom::Point3, 3> composite_poly {{ {0.0, 0.0, 0.0}, {1.0, 2.0, 0.0}, {2.0, 2.0, 0.0} }};
        auto composite2 = kernel.curves().make_composite_polyline(composite_poly);
        if (composite2.status != axiom::StatusCode::Ok || !composite2.value.has_value()) {
            std::cerr << "failed to create composite curve\n";
            return 1;
        }

    auto ellipse_eval = kernel.curve_service().eval(*ellipse.value, 0.0, 1);
    if (ellipse_eval.status != axiom::StatusCode::Ok || !ellipse_eval.value.has_value() ||
        !approx(ellipse_eval.value->point.x, 2.0) || !approx(ellipse_eval.value->point.y, 0.0)) {
        std::cerr << "unexpected ellipse eval result\n";
        return 1;
    }
        auto segment_eval = kernel.curve_service().eval(*segment2.value, 0.25, 1);
        auto segment_closest = kernel.curve_service().closest_parameter(*segment2.value, {0.5, 1.0, 0.0});
    if (segment_eval.status != axiom::StatusCode::Ok || !segment_eval.value.has_value() ||
        segment_closest.status != axiom::StatusCode::Ok || !segment_closest.value.has_value()) {
        std::cerr << "unexpected segment behavior\n";
        return 1;
    }
    if (*segment_closest.value < 0.0 || *segment_closest.value > 1.0) {
        std::cerr << "unexpected segment closest_parameter range\n";
        return 1;
    }
        auto parabola_eval2 = kernel.curve_service().eval(*parabola2.value, 1.0, 1);
        auto hyperbola_eval2 = kernel.curve_service().eval(*hyperbola2.value, 0.2, 1);
        auto composite_eval2 = kernel.curve_service().eval(*composite2.value, 0.1, 1);
        if (parabola_eval2.status != axiom::StatusCode::Ok || !parabola_eval2.value.has_value() ||
            hyperbola_eval2.status != axiom::StatusCode::Ok || !hyperbola_eval2.value.has_value() ||
            composite_eval2.status != axiom::StatusCode::Ok || !composite_eval2.value.has_value()) {
        std::cerr << "unexpected new curve eval result\n";
        return 1;
    }
    }
    auto ellipse_domain = kernel.curve_service().domain(*ellipse.value);
    if (ellipse_domain.status != axiom::StatusCode::Ok || !ellipse_domain.value.has_value() ||
        !approx(ellipse_domain.value->max, std::acos(-1.0) * 2.0, 1e-8)) {
        std::cerr << "unexpected ellipse domain\n";
        return 1;
    }
    // closest_parameter for angle-based curves should be normalized to [0, 2pi).
    auto ellipse_closest_t = kernel.curve_service().closest_parameter(*ellipse.value, {0.0, -3.0, 0.0});
    if (ellipse_closest_t.status != axiom::StatusCode::Ok || !ellipse_closest_t.value.has_value() ||
        *ellipse_closest_t.value < 0.0 || *ellipse_closest_t.value >= std::acos(-1.0) * 2.0) {
        std::cerr << "unexpected ellipse closest_parameter normalization\n";
        return 1;
    }

    auto tilted_ellipse =
        kernel.curves().make_ellipse({0.0, 0.0, 0.0}, {0.0, 2.0, 0.0}, {0.0, 0.0, 3.0});
    if (tilted_ellipse.status != axiom::StatusCode::Ok || !tilted_ellipse.value.has_value()) {
        std::cerr << "failed to create tilted ellipse\n";
        return 1;
    }

    auto tilted_ellipse_bbox = kernel.curve_service().bbox(*tilted_ellipse.value);
    if (tilted_ellipse_bbox.status != axiom::StatusCode::Ok || !tilted_ellipse_bbox.value.has_value() ||
        !approx(tilted_ellipse_bbox.value->min.x, 0.0) || !approx(tilted_ellipse_bbox.value->max.x, 0.0) ||
        !approx(tilted_ellipse_bbox.value->min.y, -2.0) || !approx(tilted_ellipse_bbox.value->max.y, 2.0) ||
        !approx(tilted_ellipse_bbox.value->min.z, -3.0) || !approx(tilted_ellipse_bbox.value->max.z, 3.0)) {
        std::cerr << "unexpected tilted ellipse bbox\n";
        return 1;
    }

    auto bezier = kernel.curves().make_bezier({{{0.0, 0.0, 0.0}, {1.0, 2.0, 0.0}, {2.0, 0.0, 0.0}}});
    if (bezier.status != axiom::StatusCode::Ok || !bezier.value.has_value()) {
        std::cerr << "failed to create bezier\n";
        return 1;
    }

    auto bezier_eval = kernel.curve_service().eval(*bezier.value, 0.5, 1);
    if (bezier_eval.status != axiom::StatusCode::Ok || !bezier_eval.value.has_value() ||
        !approx(bezier_eval.value->point.x, 1.0) || !approx(bezier_eval.value->point.y, 1.0)) {
        std::cerr << "unexpected bezier eval result\n";
        return 1;
    }

    auto bspline = kernel.curves().make_bspline({{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}}});
    if (bspline.status != axiom::StatusCode::Ok || !bspline.value.has_value()) {
        std::cerr << "failed to create bspline\n";
        return 1;
    }

    auto bspline_domain = kernel.curve_service().domain(*bspline.value);
    if (bspline_domain.status != axiom::StatusCode::Ok || !bspline_domain.value.has_value() ||
        !approx(bspline_domain.value->min, 0.0) || !approx(bspline_domain.value->max, 2.0)) {
        std::cerr << "unexpected bspline domain\n";
        return 1;
    }

    auto bspline_eval = kernel.curve_service().eval(*bspline.value, 1.5, 1);
    if (bspline_eval.status != axiom::StatusCode::Ok || !bspline_eval.value.has_value() ||
        !approx(bspline_eval.value->point.x, 1.0) || !approx(bspline_eval.value->point.y, 0.5)) {
        std::cerr << "unexpected bspline eval result\n";
        return 1;
    }
    auto bspline_eval_k = kernel.curve_service().eval(*bspline.value, 0.5, 2);
    if (bspline_eval_k.status != axiom::StatusCode::Ok || !bspline_eval_k.value.has_value() ||
        !approx(bspline_eval_k.value->curvature, 0.0, 1e-6)) {
        std::cerr << "unexpected bspline curve curvature on straight segment\n";
        return 1;
    }
    auto bspline_cubic = kernel.curves().make_bspline(
        {{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {2.0, 1.0, 0.0}}});
    if (bspline_cubic.status != axiom::StatusCode::Ok || !bspline_cubic.value.has_value()) {
        std::cerr << "failed to create 4-point bspline\n";
        return 1;
    }
    auto cubic_ev = kernel.curve_service().eval(*bspline_cubic.value, 1.0, 2);
    if (cubic_ev.status != axiom::StatusCode::Ok || !cubic_ev.value.has_value() ||
        !std::isfinite(cubic_ev.value->curvature)) {
        std::cerr << "unexpected cubic bspline eval/curvature\n";
        return 1;
    }
    auto circ_curve = kernel.curves().make_circle({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 2.0);
    if (circ_curve.status != axiom::StatusCode::Ok || !circ_curve.value.has_value()) {
        std::cerr << "failed to create circle for curvature test\n";
        return 1;
    }
    auto circ_curve_ev = kernel.curve_service().eval(*circ_curve.value, 0.25, 2);
    if (circ_curve_ev.status != axiom::StatusCode::Ok || !circ_curve_ev.value.has_value() ||
        !approx(circ_curve_ev.value->curvature, 0.5, 1e-6)) {
        std::cerr << "unexpected analytic circle curve curvature\n";
        return 1;
    }
    const std::vector<axiom::Point3> curve_query_points {{0.0, 0.0, 0.0}, {1.2, 0.2, 0.0}};
    auto bspline_closest_t_batch =
        kernel.curve_service().closest_parameters_batch(*bspline.value, curve_query_points);
    auto bspline_closest_p_batch =
        kernel.curve_service().closest_points_batch(*bspline.value, curve_query_points);
    if (bspline_closest_t_batch.status != axiom::StatusCode::Ok ||
        bspline_closest_p_batch.status != axiom::StatusCode::Ok ||
        !bspline_closest_t_batch.value.has_value() || !bspline_closest_p_batch.value.has_value() ||
        bspline_closest_t_batch.value->size() != 2 || bspline_closest_p_batch.value->size() != 2) {
        std::cerr << "unexpected curve closest batch result\n";
        return 1;
    }

    // ---- Missing curve types required by docs: line segment / parabola / hyperbola / composite ----
    {
        auto seg = kernel.curves().make_line_segment({0.0, 0.0, 0.0}, {2.0, 0.0, 0.0});
        if (seg.status != axiom::StatusCode::Ok || !seg.value.has_value()) {
            std::cerr << "failed to create line segment\n";
            return 1;
        }
        auto seg_domain = kernel.curve_service().domain(*seg.value);
        auto seg_eval = kernel.curve_service().eval(*seg.value, 0.5, 1);
        auto seg_closest = kernel.curve_service().closest_parameter(*seg.value, {10.0, 0.0, 0.0});
        if (seg_domain.status != axiom::StatusCode::Ok || !seg_domain.value.has_value() ||
            !approx(seg_domain.value->min, 0.0) || !approx(seg_domain.value->max, 1.0) ||
            seg_eval.status != axiom::StatusCode::Ok || !seg_eval.value.has_value() ||
            !approx(seg_eval.value->point.x, 1.0) ||
            seg_closest.status != axiom::StatusCode::Ok || !seg_closest.value.has_value() ||
            !approx(*seg_closest.value, 1.0)) {
            std::cerr << "unexpected line segment behavior\n";
            return 1;
        }
    }

    {
        auto parabola = kernel.curves().make_parabola({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1.0);
        if (parabola.status != axiom::StatusCode::Ok || !parabola.value.has_value()) {
            std::cerr << "failed to create parabola\n";
            return 1;
        }
        auto pe = kernel.curve_service().eval(*parabola.value, 2.0, 1);
        if (pe.status != axiom::StatusCode::Ok || !pe.value.has_value() || pe.value->point.y <= 0.0) {
            std::cerr << "unexpected parabola eval\n";
            return 1;
        }
    }

    {
        auto hyperbola = kernel.curves().make_hyperbola({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 2.0, 1.0);
        if (hyperbola.status != axiom::StatusCode::Ok || !hyperbola.value.has_value()) {
            std::cerr << "failed to create hyperbola\n";
            return 1;
        }
        auto he = kernel.curve_service().eval(*hyperbola.value, 0.0, 1);
        if (he.status != axiom::StatusCode::Ok || !he.value.has_value() || he.value->point.x < 2.0 - 1e-6) {
            std::cerr << "unexpected hyperbola eval\n";
            return 1;
        }
    }

    {
        const std::array<axiom::Point3, 3> pts{{{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 1.0, 0.0}}};
        auto composite = kernel.curves().make_composite_polyline(pts);
        if (composite.status != axiom::StatusCode::Ok || !composite.value.has_value()) {
            std::cerr << "failed to create composite polyline curve\n";
            return 1;
        }
        auto cd = kernel.curve_service().domain(*composite.value);
        auto ce = kernel.curve_service().eval(*composite.value, 1.0, 1);
        if (cd.status != axiom::StatusCode::Ok || !cd.value.has_value() ||
            !approx(cd.value->min, 0.0) || !approx(cd.value->max, 2.0) ||
            ce.status != axiom::StatusCode::Ok || !ce.value.has_value() ||
            !approx(ce.value->point.y, 1.0)) {
            std::cerr << "unexpected composite polyline behavior\n";
            return 1;
        }
    }

    // ---- Missing curve type required by docs: composite curve (chain of existing curves) ----
    {
        auto a = kernel.curves().make_line_segment({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto b = kernel.curves().make_line_segment({1.0, 0.0, 0.0}, {1.0, 2.0, 0.0});
        if (a.status != axiom::StatusCode::Ok || b.status != axiom::StatusCode::Ok ||
            !a.value.has_value() || !b.value.has_value()) {
            std::cerr << "failed to create child curves for composite chain\n";
            return 1;
        }
        const std::array<axiom::CurveId, 2> children {*a.value, *b.value};
        auto chain = kernel.curves().make_composite_chain(children);
        if (chain.status != axiom::StatusCode::Ok || !chain.value.has_value()) {
            std::cerr << "failed to create composite chain curve\n";
            return 1;
        }
        auto d = kernel.curve_service().domain(*chain.value);
        auto e0 = kernel.curve_service().eval(*chain.value, 0.25, 1);
        auto e1 = kernel.curve_service().eval(*chain.value, 1.25, 1);
        auto bb = kernel.curve_service().bbox(*chain.value);
        if (d.status != axiom::StatusCode::Ok || !d.value.has_value() ||
            !approx(d.value->min, 0.0) || !approx(d.value->max, 2.0) ||
            e0.status != axiom::StatusCode::Ok || !e0.value.has_value() ||
            e1.status != axiom::StatusCode::Ok || !e1.value.has_value() ||
            bb.status != axiom::StatusCode::Ok || !bb.value.has_value() || !bb.value->is_valid) {
            std::cerr << "unexpected composite chain behavior\n";
            return 1;
        }
        if (!approx(e0.value->point.x, 0.25) || !approx(e0.value->point.y, 0.0) ||
            !approx(e1.value->point.x, 1.0) || e1.value->point.y <= 0.0) {
            std::cerr << "unexpected composite chain eval mapping\n";
            return 1;
        }

        // Regression: closest_parameter on composite chain should land on the correct segment.
        auto t = kernel.curve_service().closest_parameter(*chain.value, {1.0, 0.6, 0.2});
        if (t.status != axiom::StatusCode::Ok || !t.value.has_value() ||
            *t.value < 1.0 || *t.value > 2.0) {
            std::cerr << "unexpected composite chain closest_parameter domain\n";
            return 1;
        }
        // Second segment length 2 in Y: GN must use true speed (derivatives[0]), not unit tangent.
        const double expect_tv = 1.0 + 0.99 / 2.0;
        auto t_vert = kernel.curve_service().closest_parameter(*chain.value, {1.0, 0.99, 0.0});
        if (t_vert.status != axiom::StatusCode::Ok || !t_vert.value.has_value() ||
            std::abs(*t_vert.value - expect_tv) > 0.06) {
            std::cerr << "unexpected composite chain closest_parameter on non-unit-speed segment\n";
            return 1;
        }
    }

    // ---- Missing surface type required by docs: Bezier surface (minimal semantics) ----
    {
        const std::array<axiom::Point3, 4> poles{{{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}}};
        auto bez_surf = kernel.surfaces().make_bezier(poles);
        if (bez_surf.status != axiom::StatusCode::Ok || !bez_surf.value.has_value()) {
            std::cerr << "failed to create bezier surface\n";
            return 1;
        }
        auto bd = kernel.surface_service().domain(*bez_surf.value);
        auto be = kernel.surface_service().eval(*bez_surf.value, 0.5, 0.5, 1);
        if (bd.status != axiom::StatusCode::Ok || !bd.value.has_value() ||
            !approx(bd.value->u.min, 0.0) || !approx(bd.value->u.max, 1.0) ||
            !approx(bd.value->v.min, 0.0) || !approx(bd.value->v.max, 1.0) ||
            be.status != axiom::StatusCode::Ok || !be.value.has_value()) {
            std::cerr << "unexpected bezier surface behavior\n";
            return 1;
        }
    }
    // closest_parameter for spline-like curves should stay within domain.
    auto bspline_single_t = kernel.curve_service().closest_parameter(*bspline.value, {100.0, 100.0, 0.0});
    if (bspline_single_t.status != axiom::StatusCode::Ok || !bspline_single_t.value.has_value() ||
        *bspline_single_t.value < bspline_domain.value->min || *bspline_single_t.value > bspline_domain.value->max) {
        std::cerr << "unexpected bspline closest_parameter clamping\n";
        return 1;
    }

    // Regression: closest_parameter should converge back near the true t on a Bezier curve.
    {
        const std::array<axiom::Point3, 3> poles{{{0.0, 0.0, 0.0}, {0.5, 1.0, 0.0}, {1.0, 0.0, 0.0}}};
        auto bez = kernel.curves().make_bezier(poles);
        if (bez.status != axiom::StatusCode::Ok || !bez.value.has_value()) {
            std::cerr << "failed to create bezier curve for closest_parameter regression\n";
            return 1;
        }
        const double t0 = 0.8;
        auto p0 = kernel.curve_service().eval(*bez.value, t0, 1);
        if (p0.status != axiom::StatusCode::Ok || !p0.value.has_value()) {
            std::cerr << "failed to eval bezier curve\n";
            return 1;
        }
        // Perturb off the curve.
        const axiom::Point3 q{p0.value->point.x + 0.03, p0.value->point.y - 0.02, 0.2};
        auto t = kernel.curve_service().closest_parameter(*bez.value, q);
        if (t.status != axiom::StatusCode::Ok || !t.value.has_value() ||
            std::abs(*t.value - t0) > 0.25) {
            std::cerr << "unexpected bezier closest_parameter refinement\n";
            return 1;
        }
    }

    // Regression: Bezier curve second derivative should be stable and correct (quadratic).
    {
        const std::array<axiom::Point3, 3> poles{{{0.0, 0.0, 0.0}, {0.5, 1.0, 0.0}, {1.0, 0.0, 0.0}}};
        auto bez = kernel.curves().make_bezier(poles);
        if (bez.status != axiom::StatusCode::Ok || !bez.value.has_value()) {
            std::cerr << "failed to create bezier curve for second derivative regression\n";
            return 1;
        }
        auto ev = kernel.curve_service().eval(*bez.value, 0.3, 2);
        if (ev.status != axiom::StatusCode::Ok || !ev.value.has_value() ||
            ev.value->derivatives.size() < 2) {
            std::cerr << "expected bezier curve to provide second derivative\n";
            return 1;
        }
        // Quadratic Bezier: r'' = 2*(P2 - 2*P1 + P0) = (0, -4, 0) for these poles.
        const auto d2 = ev.value->derivatives[1];
        if (!approx(d2.x, 0.0, 1e-6) || !approx(d2.y, -4.0, 1e-6) || !approx(d2.z, 0.0, 1e-6)) {
            std::cerr << "unexpected bezier second derivative\n";
            return 1;
        }
    }

    axiom::NURBSCurveDesc nurbs_desc;
    nurbs_desc.poles = {{0.0, 0.0, 0.0}, {1.0, 2.0, 0.0}, {2.0, 0.0, 0.0}};
    nurbs_desc.weights = {1.0, 2.0, 1.0};
    auto nurbs = kernel.curves().make_nurbs(nurbs_desc);
    if (nurbs.status != axiom::StatusCode::Ok || !nurbs.value.has_value()) {
        std::cerr << "failed to create nurbs\n";
        return 1;
    }

    auto nurbs_eval = kernel.curve_service().eval(*nurbs.value, 0.5, 1);
    if (nurbs_eval.status != axiom::StatusCode::Ok || !nurbs_eval.value.has_value() ||
        !approx(nurbs_eval.value->point.x, 1.0) || !approx(nurbs_eval.value->point.y, 1.333333333333, 1e-5)) {
        std::cerr << "unexpected nurbs eval result\n";
        return 1;
    }

    // Rational Bézier with unit weights matches polynomial Bézier: analytic second derivative.
    {
        const std::array<axiom::Point3, 3> poles{{{0.0, 0.0, 0.0}, {0.5, 1.0, 0.0}, {1.0, 0.0, 0.0}}};
        auto bez_w = kernel.curves().make_bezier(poles);
        axiom::NURBSCurveDesc nw;
        nw.poles = {{0.0, 0.0, 0.0}, {0.5, 1.0, 0.0}, {1.0, 0.0, 0.0}};
        nw.weights = {1.0, 1.0, 1.0};
        auto rat_w = kernel.curves().make_nurbs(nw);
        if (bez_w.status != axiom::StatusCode::Ok || rat_w.status != axiom::StatusCode::Ok ||
            !bez_w.value.has_value() || !rat_w.value.has_value()) {
            std::cerr << "failed to create curves for nurbs second-derivative regression\n";
            return 1;
        }
        auto e_b = kernel.curve_service().eval(*bez_w.value, 0.37, 2);
        auto e_r = kernel.curve_service().eval(*rat_w.value, 0.37, 2);
        if (e_b.status != axiom::StatusCode::Ok || e_r.status != axiom::StatusCode::Ok ||
            !e_b.value.has_value() || !e_r.value.has_value() || e_b.value->derivatives.size() < 2 ||
            e_r.value->derivatives.size() < 2) {
            std::cerr << "expected nurbs/bezier second derivatives\n";
            return 1;
        }
        const auto& db = e_b.value->derivatives[1];
        const auto& dr = e_r.value->derivatives[1];
        if (!approx(db.x, dr.x, 1e-5) || !approx(db.y, dr.y, 1e-5) || !approx(db.z, dr.z, 1e-5)) {
            std::cerr << "nurbs second derivative mismatch vs bezier\n";
            return 1;
        }
    }

    auto bspline_surface =
        kernel.surfaces().make_bspline({{{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 1.0}}});
    if (bspline_surface.status != axiom::StatusCode::Ok || !bspline_surface.value.has_value()) {
        std::cerr << "failed to create bspline surface\n";
        return 1;
    }

    auto bspline_surface_domain = kernel.surface_service().domain(*bspline_surface.value);
    if (bspline_surface_domain.status != axiom::StatusCode::Ok || !bspline_surface_domain.value.has_value() ||
        !approx(bspline_surface_domain.value->u.min, 0.0) || !approx(bspline_surface_domain.value->u.max, 1.0) ||
        !approx(bspline_surface_domain.value->v.min, 0.0) || !approx(bspline_surface_domain.value->v.max, 1.0)) {
        std::cerr << "unexpected bspline surface domain\n";
        return 1;
    }

    auto bspline_surface_eval = kernel.surface_service().eval(*bspline_surface.value, 0.5, 0.5, 1);
    if (bspline_surface_eval.status != axiom::StatusCode::Ok || !bspline_surface_eval.value.has_value() ||
        !approx(bspline_surface_eval.value->point.x, 0.5) || !approx(bspline_surface_eval.value->point.y, 0.5) ||
        !approx(bspline_surface_eval.value->point.z, 0.25)) {
        std::cerr << "unexpected bspline surface eval result\n";
        return 1;
    }
    const std::vector<std::pair<double, double>> bspline_uvs {{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}};
    auto bspline_surface_eval_batch = kernel.surface_service().eval_batch(*bspline_surface.value, bspline_uvs, 1);
    if (bspline_surface_eval_batch.status != axiom::StatusCode::Ok ||
        !bspline_surface_eval_batch.value.has_value() ||
        bspline_surface_eval_batch.value->size() != 3 ||
        !approx(bspline_surface_eval_batch.value->at(1).point.z, 0.25)) {
        std::cerr << "unexpected surface eval batch result\n";
        return 1;
    }
    // Bilinear patch with z = u*v at interior: saddle → principal curvatures opposite-signed and non-trivial
    // (finite-difference fundamental forms on the control grid; trace may drift slightly from 0).
    {
        const auto k1 = bspline_surface_eval.value->k1;
        const auto k2 = bspline_surface_eval.value->k2;
        if (!std::isfinite(k1) || !std::isfinite(k2) || std::abs(k1) < 0.12 || std::abs(k2) < 0.12 ||
            (k1 * k2 >= 0.0)) {
            std::cerr << "unexpected bspline surface principal curvature at saddle-like patch\n";
            return 1;
        }
    }

    auto flat_bspline_surface = kernel.surfaces().make_bspline(
        {{{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}}});
    if (flat_bspline_surface.status != axiom::StatusCode::Ok || !flat_bspline_surface.value.has_value()) {
        std::cerr << "failed to create flat bspline surface\n";
        return 1;
    }
    auto flat_bspline_eval =
        kernel.surface_service().eval(*flat_bspline_surface.value, 0.5, 0.5, 1);
    if (flat_bspline_eval.status != axiom::StatusCode::Ok || !flat_bspline_eval.value.has_value() ||
        !approx(flat_bspline_eval.value->point.z, 0.0, 1e-9)) {
        std::cerr << "unexpected flat bspline surface eval\n";
        return 1;
    }
    if (std::abs(flat_bspline_eval.value->k1) > 0.08 || std::abs(flat_bspline_eval.value->k2) > 0.08) {
        std::cerr << "unexpected flat bspline surface curvature (expect near zero)\n";
        return 1;
    }

    axiom::NURBSSurfaceDesc nurbs_surface_desc;
    nurbs_surface_desc.poles = {{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 1.0}};
    nurbs_surface_desc.weights = {1.0, 1.0, 1.0, 3.0};
    auto nurbs_surface = kernel.surfaces().make_nurbs(nurbs_surface_desc);
    if (nurbs_surface.status != axiom::StatusCode::Ok || !nurbs_surface.value.has_value()) {
        std::cerr << "failed to create nurbs surface\n";
        return 1;
    }

    auto nurbs_surface_eval = kernel.surface_service().eval(*nurbs_surface.value, 0.5, 0.5, 1);
    if (nurbs_surface_eval.status != axiom::StatusCode::Ok || !nurbs_surface_eval.value.has_value() ||
        !approx(nurbs_surface_eval.value->point.x, 0.666666666667, 1e-5) ||
        !approx(nurbs_surface_eval.value->point.y, 0.666666666667, 1e-5) ||
        !approx(nurbs_surface_eval.value->point.z, 0.5, 1e-5)) {
        std::cerr << "unexpected nurbs surface eval result\n";
        return 1;
    }
    const std::vector<axiom::Point3> surface_query_points {
        {0.666666666667, 0.666666666667, 0.5},
        {0.0, 0.0, 0.0}};
    auto nurbs_closest_uv_batch =
        kernel.surface_service().closest_uv_batch(*nurbs_surface.value, surface_query_points);
    auto nurbs_closest_point_batch =
        kernel.surface_service().closest_points_batch(*nurbs_surface.value, surface_query_points);
    if (nurbs_closest_uv_batch.status != axiom::StatusCode::Ok ||
        nurbs_closest_point_batch.status != axiom::StatusCode::Ok ||
        !nurbs_closest_uv_batch.value.has_value() ||
        !nurbs_closest_point_batch.value.has_value() ||
        nurbs_closest_uv_batch.value->size() != 2 ||
        nurbs_closest_point_batch.value->size() != 2) {
        std::cerr << "unexpected surface closest batch result\n";
        return 1;
    }
    // closest_uv for spline-like surfaces should stay within domain.
    auto nurbs_surface_domain = kernel.surface_service().domain(*nurbs_surface.value);
    auto nurbs_surface_uv = kernel.surface_service().closest_uv(*nurbs_surface.value, {100.0, 100.0, 100.0});
    if (nurbs_surface_domain.status != axiom::StatusCode::Ok || !nurbs_surface_domain.value.has_value() ||
        nurbs_surface_uv.status != axiom::StatusCode::Ok || !nurbs_surface_uv.value.has_value() ||
        nurbs_surface_uv.value->first < nurbs_surface_domain.value->u.min ||
        nurbs_surface_uv.value->first > nurbs_surface_domain.value->u.max ||
        nurbs_surface_uv.value->second < nurbs_surface_domain.value->v.min ||
        nurbs_surface_uv.value->second > nurbs_surface_domain.value->v.max) {
        std::cerr << "unexpected nurbs surface closest_uv clamping\n";
        return 1;
    }

    auto invalid_line = kernel.curves().make_line({0.0, 0.0, 0.0}, {0.0, 0.0, 0.0});
    if (invalid_line.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected invalid input for zero direction line\n";
        return 1;
    }

    auto invalid_circle = kernel.curves().make_circle({0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 1.0);
    if (invalid_circle.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected invalid input for zero normal circle\n";
        return 1;
    }

    auto invalid_ellipse =
        kernel.curves().make_ellipse({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {2.0, 0.0, 0.0});
    if (invalid_ellipse.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected invalid input for collinear ellipse axes\n";
        return 1;
    }
    axiom::NURBSCurveDesc invalid_nurbs_curve;
    invalid_nurbs_curve.poles = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}};
    invalid_nurbs_curve.weights = {1.0, -1.0};
    auto invalid_nurbs_c = kernel.curves().make_nurbs(invalid_nurbs_curve);
    if (invalid_nurbs_c.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected invalid input for invalid nurbs curve weights\n";
        return 1;
    }

    auto invalid_cylinder = kernel.surfaces().make_cylinder({0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 2.0);
    if (invalid_cylinder.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected invalid input for zero axis cylinder\n";
        return 1;
    }
    auto tilted_cylinder = kernel.surfaces().make_cylinder({0.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, 2.0);
    if (tilted_cylinder.status != axiom::StatusCode::Ok || !tilted_cylinder.value.has_value()) {
        std::cerr << "failed to create tilted cylinder\n";
        return 1;
    }
    auto tilted_cylinder_eval = kernel.surface_service().eval(*tilted_cylinder.value, 0.3, 1.2, 1);
    if (tilted_cylinder_eval.status != axiom::StatusCode::Ok || !tilted_cylinder_eval.value.has_value() ||
        !approx(std::sqrt(tilted_cylinder_eval.value->normal.x * tilted_cylinder_eval.value->normal.x +
                          tilted_cylinder_eval.value->normal.y * tilted_cylinder_eval.value->normal.y +
                          tilted_cylinder_eval.value->normal.z * tilted_cylinder_eval.value->normal.z),
                1.0, 1e-6)) {
        std::cerr << "unexpected tilted cylinder normal stability\n";
        return 1;
    }
    // Curvature sanity for cylinder: k1 == 1/r, k2 == 0.
    if (!approx(tilted_cylinder_eval.value->k1, 1.0 / 2.0, 1e-8) || !approx(tilted_cylinder_eval.value->k2, 0.0, 1e-12)) {
        std::cerr << "unexpected cylinder curvature\n";
        return 1;
    }
    auto moved_line = kernel.geometry_transform().transform_curve(
        *line.value, kernel.linear_algebra().make_translation({1.0, 2.0, 3.0}));
    auto moved_sphere = kernel.geometry_transform().transform_surface(
        *sphere.value, kernel.linear_algebra().make_translation({0.0, 0.0, 4.0}));
    if (moved_line.status != axiom::StatusCode::Ok || moved_sphere.status != axiom::StatusCode::Ok ||
        !moved_line.value.has_value() || !moved_sphere.value.has_value()) {
        std::cerr << "failed to transform geometry\n";
        return 1;
    }
    auto moved_line_eval = kernel.curve_service().eval(*moved_line.value, 0.0, 1);
    auto moved_sphere_eval = kernel.surface_service().eval(*moved_sphere.value, 0.0, 0.0, 1);
    if (moved_line_eval.status != axiom::StatusCode::Ok || moved_sphere_eval.status != axiom::StatusCode::Ok ||
        !moved_line_eval.value.has_value() || !moved_sphere_eval.value.has_value() ||
        !approx(moved_line_eval.value->point.x, 1.0) || !approx(moved_line_eval.value->point.y, 2.0) ||
        !approx(moved_line_eval.value->point.z, 3.0) || !approx(moved_sphere_eval.value->point.z, 9.0)) {
        std::cerr << "unexpected transformed geometry behavior\n";
        return 1;
    }

    // Curvature sanity checks for analytic surfaces.
    {
        auto torus = kernel.surfaces().make_torus({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 3.0, 1.0);
        if (torus.status != axiom::StatusCode::Ok || !torus.value.has_value()) {
            std::cerr << "failed to create torus\n";
            return 1;
        }
        // At v=0: k_minor ~= 1/r, k_major ~= 1/(R+r)
        auto te = kernel.surface_service().eval(*torus.value, 0.2, 0.0, 1);
        if (te.status != axiom::StatusCode::Ok || !te.value.has_value() ||
            !approx(te.value->k1, 1.0, 1e-6) || !approx(te.value->k2, 1.0 / 4.0, 1e-6)) {
            std::cerr << "unexpected torus curvature\n";
            return 1;
        }
    }

    // Stage 2 regression: reject NaN/Inf inputs instead of silently propagating.
    const double qnan = std::numeric_limits<double>::quiet_NaN();
    auto bad_curve_eval = kernel.curve_service().eval(*line.value, qnan, 1);
    if (bad_curve_eval.status == axiom::StatusCode::Ok) {
        std::cerr << "expected curve eval to reject NaN parameter\n";
        return 1;
    }
    auto bad_curve_cp = kernel.curve_service().closest_point(*line.value, {qnan, 0.0, 0.0});
    if (bad_curve_cp.status == axiom::StatusCode::Ok) {
        std::cerr << "expected curve closest point to reject NaN query point\n";
        return 1;
    }
    auto bad_surface_eval = kernel.surface_service().eval(*sphere.value, qnan, 0.0, 1);
    if (bad_surface_eval.status == axiom::StatusCode::Ok) {
        std::cerr << "expected surface eval to reject NaN parameter\n";
        return 1;
    }
    auto bad_surface_uv = kernel.surface_service().closest_uv(*sphere.value, {0.0, qnan, 0.0});
    if (bad_surface_uv.status == axiom::StatusCode::Ok) {
        std::cerr << "expected surface closest uv to reject NaN query point\n";
        return 1;
    }
    axiom::NURBSSurfaceDesc invalid_nurbs_surface;
    invalid_nurbs_surface.poles = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 1.0, 0.0}};
    invalid_nurbs_surface.weights = {1.0, 1.0, 1.0, 0.0};
    auto invalid_nurbs_s = kernel.surfaces().make_nurbs(invalid_nurbs_surface);
    if (invalid_nurbs_s.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected invalid input for invalid nurbs surface weights\n";
        return 1;
    }

    // ---- Stage 2: surfaces required by docs — revolved / swept / trimmed / offset ----
    {
        const double half_pi = std::acos(-1.0) * 0.5;
        axiom::Axis3 axis {{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
        auto gen = kernel.curves().make_line_segment({1.0, 0.0, 0.0}, {2.0, 0.0, 0.0});
        if (gen.status != axiom::StatusCode::Ok || !gen.value.has_value()) {
            std::cerr << "failed to create generatrix\n";
            return 1;
        }
        auto rev = kernel.surfaces().make_revolved(*gen.value, axis, half_pi);
        if (rev.status != axiom::StatusCode::Ok || !rev.value.has_value()) {
            std::cerr << "failed to create revolved surface\n";
            return 1;
        }
        auto rev_eval = kernel.surface_service().eval(*rev.value, half_pi, 0.0, 1);
        if (rev_eval.status != axiom::StatusCode::Ok || !rev_eval.value.has_value() ||
            !approx(rev_eval.value->point.x, 0.0) || !approx(rev_eval.value->point.y, 1.0)) {
            std::cerr << "unexpected revolved surface eval\n";
            return 1;
        }

        auto prof = kernel.curves().make_line_segment({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        if (prof.status != axiom::StatusCode::Ok || !prof.value.has_value()) {
            std::cerr << "failed to create profile\n";
            return 1;
        }
        auto sw = kernel.surfaces().make_swept_linear(*prof.value, {0.0, 0.0, 1.0}, 2.0);
        if (sw.status != axiom::StatusCode::Ok || !sw.value.has_value()) {
            std::cerr << "failed to create swept surface\n";
            return 1;
        }
        auto sw_ev = kernel.surface_service().eval(*sw.value, 1.0, 0.0, 1);
        if (sw_ev.status != axiom::StatusCode::Ok || !sw_ev.value.has_value() ||
            !approx(sw_ev.value->point.z, 2.0)) {
            std::cerr << "unexpected swept linear eval\n";
            return 1;
        }

        auto sph = kernel.surfaces().make_sphere({0.0, 0.0, 0.0}, 2.0);
        if (sph.status != axiom::StatusCode::Ok || !sph.value.has_value()) {
            std::cerr << "failed to create sphere for trimmed/offset\n";
            return 1;
        }
        auto trimmed = kernel.surfaces().make_trimmed(*sph.value, 0.1, 1.0, 0.2, 1.2);
        if (trimmed.status != axiom::StatusCode::Ok || !trimmed.value.has_value()) {
            std::cerr << "failed to create trimmed surface\n";
            return 1;
        }
        auto uv_trim = kernel.surface_service().closest_uv(*trimmed.value, {2.0, 0.0, 0.0});
        if (uv_trim.status != axiom::StatusCode::Ok || !uv_trim.value.has_value() ||
            uv_trim.value->first < 0.1 || uv_trim.value->first > 1.0 ||
            uv_trim.value->second < 0.2 || uv_trim.value->second > 1.2) {
            std::cerr << "unexpected trimmed closest_uv clamping\n";
            return 1;
        }

        auto off = kernel.surfaces().make_offset(*sph.value, 1.0);
        if (off.status != axiom::StatusCode::Ok || !off.value.has_value()) {
            std::cerr << "failed to create offset surface\n";
            return 1;
        }
        auto off_eval = kernel.surface_service().eval(*off.value, 0.0, 0.0, 1);
        if (off_eval.status != axiom::StatusCode::Ok || !off_eval.value.has_value() ||
            !approx(off_eval.value->point.z, 3.0)) {
            std::cerr << "unexpected offset eval\n";
            return 1;
        }
        // Avoid sphere polar singularity (u,v)=(0,0): check κ at generic interior parameters.
        auto off_curv = kernel.surface_service().eval(*off.value, 1.0, 0.7, 1);
        if (off_curv.status != axiom::StatusCode::Ok || !off_curv.value.has_value()) {
            std::cerr << "unexpected offset sphere eval for curvature\n";
            return 1;
        }
        const double exp_off_k = 1.0 / 3.0;
        if (!approx(off_curv.value->k1, exp_off_k, 8e-3) ||
            !approx(off_curv.value->k2, exp_off_k, 8e-3)) {
            std::cerr << "unexpected offset sphere principal curvatures\n";
            return 1;
        }

        auto sw_flat = kernel.surface_service().eval(*sw.value, 0.5, 0.5, 1);
        if (sw_flat.status != axiom::StatusCode::Ok || !sw_flat.value.has_value() ||
            std::abs(sw_flat.value->k1) > 0.05 || std::abs(sw_flat.value->k2) > 0.05) {
            std::cerr << "unexpected swept ruled-surface principal curvatures (expect near-flat)\n";
            return 1;
        }

        // Offset 曲面定义域应与基球面一致（不再误报 [0,1]²）。
        const double two_pi_dom = std::acos(-1.0) * 2.0;
        const double pi_dom = std::acos(-1.0);
        auto off_dom = kernel.surface_service().domain(*off.value);
        if (off_dom.status != axiom::StatusCode::Ok || !off_dom.value.has_value() ||
            !approx(off_dom.value->u.min, 0.0) || !approx(off_dom.value->v.min, 0.0) ||
            !approx(off_dom.value->u.max, two_pi_dom) || !approx(off_dom.value->v.max, pi_dom)) {
            std::cerr << "offset surface domain should inherit base sphere domain\n";
            return 1;
        }

        // 派生曲面 bbox：固定网格采样，显著收紧相对占位 50³。
        auto bbox_max_span = [](const axiom::BoundingBox& B) {
            return std::max({B.max.x - B.min.x, B.max.y - B.min.y, B.max.z - B.min.z});
        };
        auto rev_bb = kernel.surface_service().bbox(*rev.value);
        auto sw_bb = kernel.surface_service().bbox(*sw.value);
        auto trim_bb = kernel.surface_service().bbox(*trimmed.value);
        auto off_bb = kernel.surface_service().bbox(*off.value);
        if (rev_bb.status != axiom::StatusCode::Ok || !rev_bb.value.has_value() || !rev_bb.value->is_valid ||
            sw_bb.status != axiom::StatusCode::Ok || !sw_bb.value.has_value() || !sw_bb.value->is_valid ||
            trim_bb.status != axiom::StatusCode::Ok || !trim_bb.value.has_value() || !trim_bb.value->is_valid ||
            off_bb.status != axiom::StatusCode::Ok || !off_bb.value.has_value() || !off_bb.value->is_valid) {
            std::cerr << "derived surface bbox query failed\n";
            return 1;
        }
        if (bbox_max_span(*rev_bb.value) > 3.0 || bbox_max_span(*sw_bb.value) > 3.0) {
            std::cerr << "revolved/swept bbox unexpectedly loose (regression to placeholder scale?)\n";
            return 1;
        }
        if (bbox_max_span(*trim_bb.value) > 4.5) {
            std::cerr << "trimmed sphere patch bbox unexpectedly loose\n";
            return 1;
        }
        if (bbox_max_span(*off_bb.value) < 5.5 || bbox_max_span(*off_bb.value) > 6.6) {
            std::cerr << "offset sphere bbox span unexpected (expect ~6 for R=3 shell)\n";
            return 1;
        }
    }

    // Regression: refined closest_uv should recover parameters near a known point (Bezier surface).
    {
        const std::array<axiom::Point3, 4> poles{{{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}}};
        auto surf = kernel.surfaces().make_bezier(poles);
        if (surf.status != axiom::StatusCode::Ok || !surf.value.has_value()) {
            std::cerr << "failed to create bezier surface for closest_uv refine\n";
            return 1;
        }
        const double u0 = 0.25;
        const double v0 = 0.75;
        auto p0 = kernel.surface_service().eval(*surf.value, u0, v0, 1);
        if (p0.status != axiom::StatusCode::Ok || !p0.value.has_value()) {
            std::cerr << "failed to eval bezier surface\n";
            return 1;
        }
        const auto q = axiom::Point3{p0.value->point.x + 0.02, p0.value->point.y - 0.01, p0.value->point.z + 0.1};
        auto uv = kernel.surface_service().closest_uv(*surf.value, q);
        if (uv.status != axiom::StatusCode::Ok || !uv.value.has_value() ||
            std::abs(uv.value->first - u0) > 0.2 || std::abs(uv.value->second - v0) > 0.2) {
            std::cerr << "unexpected bezier surface closest_uv refinement\n";
            return 1;
        }
    }

    // Regression: trimmed closest_uv must remain inside trim domain.
    {
        auto sph = kernel.surfaces().make_sphere({0.0, 0.0, 0.0}, 2.0);
        auto tr = kernel.surfaces().make_trimmed(*sph.value, 0.1, 1.0, 0.2, 1.2);
        if (sph.status != axiom::StatusCode::Ok || tr.status != axiom::StatusCode::Ok ||
            !sph.value.has_value() || !tr.value.has_value()) {
            std::cerr << "failed to create trimmed surface for closest_uv\n";
            return 1;
        }
        auto uv = kernel.surface_service().closest_uv(*tr.value, {2.0, 0.0, 0.0});
        if (uv.status != axiom::StatusCode::Ok || !uv.value.has_value() ||
            uv.value->first < 0.1 || uv.value->first > 1.0 ||
            uv.value->second < 0.2 || uv.value->second > 1.2) {
            std::cerr << "unexpected trimmed closest_uv clamping\n";
            return 1;
        }
    }

    // ---- Stage 3 (minimal): curve-surface intersection (analytic pairs) ----
    {
        auto& isect = kernel.geometry_intersection();

        // Line-plane: single hit.
        auto ln = kernel.curves().make_line({0.0, 0.0, -1.0}, {0.0, 0.0, 1.0});
        auto pl = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        if (ln.status != axiom::StatusCode::Ok || pl.status != axiom::StatusCode::Ok ||
            !ln.value.has_value() || !pl.value.has_value()) {
            std::cerr << "failed to create line/plane for intersection\n";
            return 1;
        }
        auto hits_lp = isect.intersect_curve_surface(*ln.value, *pl.value);
        if (hits_lp.status != axiom::StatusCode::Ok || !hits_lp.value.has_value() ||
            hits_lp.value->size() != 1 || !approx(hits_lp.value->front().point.z, 0.0)) {
            std::cerr << "unexpected line-plane intersection\n";
            return 1;
        }

        // Segment-sphere: two hits at z=±2.
        auto seg = kernel.curves().make_line_segment({0.0, 0.0, -3.0}, {0.0, 0.0, 3.0});
        auto sph = kernel.surfaces().make_sphere({0.0, 0.0, 0.0}, 2.0);
        if (seg.status != axiom::StatusCode::Ok || sph.status != axiom::StatusCode::Ok ||
            !seg.value.has_value() || !sph.value.has_value()) {
            std::cerr << "failed to create segment/sphere for intersection\n";
            return 1;
        }
        auto hits_ss = isect.intersect_curve_surface(*seg.value, *sph.value);
        if (hits_ss.status != axiom::StatusCode::Ok || !hits_ss.value.has_value() ||
            hits_ss.value->size() != 2) {
            std::cerr << "unexpected segment-sphere intersection count\n";
            return 1;
        }
        const auto z0 = hits_ss.value->at(0).point.z;
        const auto z1 = hits_ss.value->at(1).point.z;
        if (!( (approx(z0, -2.0) && approx(z1, 2.0)) || (approx(z0, 2.0) && approx(z1, -2.0)) )) {
            std::cerr << "unexpected segment-sphere intersection locations\n";
            return 1;
        }
    }

    return 0;
}
