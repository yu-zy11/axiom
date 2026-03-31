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

    auto ellipse_eval = kernel.curve_service().eval(*ellipse.value, 0.0, 1);
    if (ellipse_eval.status != axiom::StatusCode::Ok || !ellipse_eval.value.has_value() ||
        !approx(ellipse_eval.value->point.x, 2.0) || !approx(ellipse_eval.value->point.y, 0.0)) {
        std::cerr << "unexpected ellipse eval result\n";
        return 1;
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
    // closest_parameter for spline-like curves should stay within domain.
    auto bspline_single_t = kernel.curve_service().closest_parameter(*bspline.value, {100.0, 100.0, 0.0});
    if (bspline_single_t.status != axiom::StatusCode::Ok || !bspline_single_t.value.has_value() ||
        *bspline_single_t.value < bspline_domain.value->min || *bspline_single_t.value > bspline_domain.value->max) {
        std::cerr << "unexpected bspline closest_parameter clamping\n";
        return 1;
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

    return 0;
}
