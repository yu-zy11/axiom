#include <cmath>
#include <iostream>
#include <vector>

#include "axiom/sdk/kernel.h"

namespace {

bool approx(double lhs, double rhs, double eps = 1e-6) {
    return std::abs(lhs - rhs) <= eps;
}

}  // namespace

int main() {
    axiom::Kernel kernel;

    const axiom::Vec3 x {1.0, 0.0, 0.0};
    const axiom::Vec3 y {0.0, 1.0, 0.0};
    const axiom::Vec3 z {0.0, 0.0, 1.0};

    auto angle = kernel.linear_algebra().angle_between(x, y);
    if (!approx(angle, std::acos(-1.0) * 0.5)) {
        std::cerr << "unexpected vector angle\n";
        return 1;
    }
    if (!approx(kernel.linear_algebra().scalar_triple_product(x, y, z), 1.0)) {
        std::cerr << "unexpected scalar triple product\n";
        return 1;
    }
    auto sum = kernel.linear_algebra().add(x, y);
    auto diff = kernel.linear_algebra().subtract(sum, y);
    auto scaled_v = kernel.linear_algebra().scale(x, 3.0);
    auto had = kernel.linear_algebra().hadamard({2.0, 3.0, 4.0}, {5.0, 6.0, 7.0});
    auto clamped_norm = kernel.linear_algebra().clamp_norm({10.0, 0.0, 0.0}, 2.0);
    auto basis = kernel.linear_algebra().orthonormal_basis({0.0, 0.0, 1.0});
    if (!approx(sum.x, 1.0) || !approx(sum.y, 1.0) || !approx(diff.x, 1.0) ||
        !approx(diff.y, 0.0) || !approx(scaled_v.x, 3.0) || !approx(had.z, 28.0) ||
        !approx(kernel.linear_algebra().norm(clamped_norm), 2.0) || !basis.has_value() ||
        !approx(kernel.linear_algebra().dot(basis->first, {0.0, 0.0, 1.0}), 0.0, 1e-6) ||
        !approx(kernel.linear_algebra().dot(basis->second, {0.0, 0.0, 1.0}), 0.0, 1e-6)) {
        std::cerr << "unexpected vector utility behavior\n";
        return 1;
    }

    const axiom::Point3 p0 {0.0, 0.0, 0.0};
    const axiom::Point3 p1 {2.0, 2.0, 2.0};
    if (!approx(kernel.linear_algebra().distance(p0, p1), std::sqrt(12.0))) {
        std::cerr << "unexpected point distance\n";
        return 1;
    }
    if (!approx(kernel.linear_algebra().squared_distance(p0, p1), 12.0) ||
        !approx(kernel.linear_algebra().manhattan_distance(p0, p1), 6.0)) {
        std::cerr << "unexpected point distance metrics\n";
        return 1;
    }
    auto mid = kernel.linear_algebra().midpoint(p0, p1);
    auto lerp = kernel.linear_algebra().lerp(p0, p1, 0.25);
    if (!approx(mid.x, 1.0) || !approx(mid.y, 1.0) || !approx(mid.z, 1.0) ||
        !approx(lerp.x, 0.5) || !approx(lerp.y, 0.5) || !approx(lerp.z, 0.5)) {
        std::cerr << "unexpected midpoint or lerp result\n";
        return 1;
    }

    auto proj = kernel.linear_algebra().project({2.0, 2.0, 0.0}, x);
    auto rej = kernel.linear_algebra().reject({2.0, 2.0, 0.0}, x);
    if (!approx(proj.x, 2.0) || !approx(proj.y, 0.0) || !approx(rej.y, 2.0) ||
        !kernel.linear_algebra().is_finite(proj) || !kernel.linear_algebra().is_near_zero({1e-10, 0.0, 0.0}, 1e-8)) {
        std::cerr << "unexpected projection/rejection behavior\n";
        return 1;
    }

    auto t = kernel.linear_algebra().make_translation({1.0, 2.0, 3.0});
    auto s = kernel.linear_algebra().make_scale(2.0, 2.0, 2.0);
    auto r = kernel.linear_algebra().make_rotation_axis_angle({0.0, 0.0, 1.0}, std::acos(-1.0) * 0.5);
    auto trs = kernel.linear_algebra().compose(t, kernel.linear_algebra().compose(r, s));
    auto p_trs = kernel.linear_algebra().transform(axiom::Point3{1.0, 0.0, 0.0}, trs);
    if (!approx(p_trs.x, 1.0) || !approx(p_trs.y, 4.0) || !approx(p_trs.z, 3.0)) {
        std::cerr << "unexpected transform compose behavior\n";
        return 1;
    }
    axiom::Transform3 inv {};
    if (!kernel.linear_algebra().invert_affine(trs, inv, 1e-12)) {
        std::cerr << "failed to invert affine transform\n";
        return 1;
    }
    auto back = kernel.linear_algebra().transform(p_trs, inv);
    if (!approx(back.x, 1.0) || !approx(back.y, 0.0) || !approx(back.z, 0.0)) {
        std::cerr << "unexpected transform inverse behavior\n";
        return 1;
    }
    if (!approx(kernel.linear_algebra().distance_point_to_line({1.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), 2.0) ||
        !approx(kernel.linear_algebra().distance_point_to_plane({0.0, 0.0, 3.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}), 3.0) ||
        !approx(kernel.linear_algebra().triangle_area({0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 2.0, 0.0}), 2.0) ||
        !approx(kernel.linear_algebra().tetrahedron_signed_volume(
                    {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}),
                1.0 / 6.0) ||
        !approx(kernel.linear_algebra().squared_norm({3.0, 4.0, 0.0}), 25.0)) {
        std::cerr << "unexpected geometry metric behavior\n";
        return 1;
    }

    if (kernel.predicates().orient2d({0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}) != axiom::Sign::Positive ||
        kernel.predicates().orient3d({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}) !=
            axiom::Sign::Positive ||
        kernel.predicates().orient2d_tol({0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}, 1e-12) != axiom::Sign::Zero ||
        kernel.predicates().orient3d_tol({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, 1e-12) !=
            axiom::Sign::Zero) {
        std::cerr << "unexpected orientation predicate behavior\n";
        return 1;
    }

    const axiom::BoundingBox b0 {{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}, true};
    const axiom::BoundingBox b1 {{0.9, 0.9, 0.9}, {2.0, 2.0, 2.0}, true};
    if (!kernel.predicates().aabb_intersects(b0, b1, 0.0) ||
        !kernel.predicates().point_in_bbox({1.01, 0.5, 0.5}, b0, 0.02) ||
        !kernel.predicates().point_equal_tol({1.0, 1.0, 1.0}, {1.0 + 1e-7, 1.0, 1.0}, 1e-6) ||
        !kernel.predicates().bbox_contains({{0.0, 0.0, 0.0}, {3.0, 3.0, 3.0}, true}, b0, 0.0) ||
        !kernel.predicates().vec_parallel({1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, 1e-6) ||
        !kernel.predicates().vec_orthogonal({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1e-6)) {
        std::cerr << "unexpected bbox/vector predicate behavior\n";
        return 1;
    }
    auto inter = kernel.predicates().bbox_intersection(b0, b1, 0.0);
    if (!kernel.predicates().bbox_valid(b0) || !inter.has_value() ||
        !approx(kernel.predicates().bbox_overlap_ratio(b0, b1, 0.0), 0.0004291845, 1e-6) ||
        !kernel.predicates().bbox_center_in(b0, {{-1.0, -1.0, -1.0}, {3.0, 3.0, 3.0}, true}, 0.0) ||
        !kernel.predicates().range1d_overlap({0.0, 1.0}, {0.5, 2.0}, 0.0) ||
        !kernel.predicates().range2d_overlap({{0.0, 1.0}, {0.0, 1.0}}, {{0.5, 2.0}, {-1.0, 0.2}}, 0.0) ||
        !kernel.predicates().point_in_sphere({0.0, 0.0, 1.0}, {0.0, 0.0, 0.0}, 1.0, 1e-9) ||
        !kernel.predicates().point_in_cylinder_approx({0.5, 0.0, 1.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 1.0, 2.0, 1e-9)) {
        std::cerr << "unexpected extended predicate behavior\n";
        return 1;
    }

    auto policy = kernel.tolerance().global_policy();
    policy.min_local = 2.0;
    policy.max_local = 1.0;
    auto clamped = kernel.tolerance().clamp_policy(policy);
    auto scaled = kernel.tolerance().scale_policy(clamped, 0.5);
    auto overridden = kernel.tolerance().override_policy(clamped, -0.1);
    auto merged = kernel.tolerance().merge_policy({0.0, 0.0, 0.0, 0.0, axiom::PrecisionMode::AdaptiveCertified}, clamped);
    auto with_angular = kernel.tolerance().with_angular(clamped, 0.25);
    auto loosened = kernel.tolerance().loosen_policy(clamped, 2.0);
    auto tightened = kernel.tolerance().tighten_policy(clamped, 2.0);
    if (!kernel.tolerance().is_valid_policy(clamped) || !kernel.tolerance().is_valid_policy(scaled) ||
        !kernel.tolerance().is_valid_policy(overridden) || !kernel.tolerance().is_valid_policy(merged) ||
        !kernel.tolerance().is_valid_policy(with_angular) || !kernel.tolerance().is_valid_policy(loosened) ||
        !kernel.tolerance().is_valid_policy(tightened) || clamped.max_local < clamped.min_local ||
        overridden.linear < 0.0 || merged.linear <= 0.0 || !approx(with_angular.angular, 0.25)) {
        std::cerr << "unexpected tolerance policy behavior\n";
        return 1;
    }

    if (!approx(kernel.tolerance().effective_linear(-1.0), kernel.tolerance().global_policy().linear) ||
        !approx(kernel.tolerance().effective_angular(0.0), kernel.tolerance().global_policy().angular) ||
        !approx(kernel.tolerance().normalize_linear_request(-1.0), kernel.tolerance().global_policy().linear) ||
        !approx(kernel.tolerance().normalize_angular_request(-1.0), kernel.tolerance().global_policy().angular) ||
        !approx(kernel.tolerance().resolve_linear_for_scale(1e-6, 10.0), 1e-5) ||
        !approx(kernel.tolerance().resolve_angular_for_scale(1e-6, 10.0), 1e-5) ||
        kernel.tolerance().compare_linear(1.0, 1.0000001, 1e-3) != 0 ||
        kernel.tolerance().compare_angular(0.1, 0.5, 1e-3) >= 0 ||
        !kernel.tolerance().within_linear(1.0, 1.00001, 1e-3) ||
        !kernel.tolerance().within_angular(0.1, 0.10001, 1e-3)) {
        std::cerr << "unexpected tolerance helper behavior\n";
        return 1;
    }

    const std::vector<axiom::Point3> pts {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};
    const std::vector<axiom::Vec3> vecs {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};
    const auto transformed_pts = kernel.linear_algebra().transform_points(pts, t);
    const auto transformed_vecs = kernel.linear_algebra().transform_vectors(vecs, r);
    if (transformed_pts.size() != 2 || transformed_vecs.size() != 2 ||
        !approx(transformed_pts[0].x, 2.0) || !approx(transformed_pts[0].y, 2.0) ||
        !approx(transformed_vecs[0].y, 1.0, 1e-5)) {
        std::cerr << "unexpected batch transform behavior\n";
        return 1;
    }
    auto centroid = kernel.linear_algebra().centroid(pts);
    auto avg = kernel.linear_algebra().average(vecs);
    if (!approx(centroid.x, 0.5) || !approx(centroid.y, 0.5) || !approx(centroid.z, 0.0) ||
        !approx(avg.x, 0.5) || !approx(avg.y, 0.5) || !approx(avg.z, 0.0)) {
        std::cerr << "unexpected centroid/average behavior\n";
        return 1;
    }

    auto line = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    auto sphere = kernel.surfaces().make_sphere({0.0, 0.0, 0.0}, 2.0);
    auto box = kernel.primitives().box({0.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
    auto tiny_box = kernel.primitives().box({0.0, 0.0, 0.0}, 0.1, 0.1, 0.1);
    if (line.status != axiom::StatusCode::Ok || sphere.status != axiom::StatusCode::Ok ||
        box.status != axiom::StatusCode::Ok || tiny_box.status != axiom::StatusCode::Ok ||
        !line.value.has_value() || !sphere.value.has_value() ||
        !box.value.has_value() || !tiny_box.value.has_value()) {
        std::cerr << "failed to create geometry for predicate tests\n";
        return 1;
    }
    auto on_curve = kernel.predicates().point_on_curve({1.0, 0.0, 0.0}, *line.value, 1e-6);
    auto on_surface = kernel.predicates().point_on_surface({0.0, 0.0, 2.0}, *sphere.value, 1e-5);
    auto neg_tol = kernel.predicates().point_on_curve({1.0, 0.0, 0.0}, *line.value, -1.0);
    const std::vector<axiom::Point3> curve_points {{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};
    const std::vector<axiom::Point3> surface_points {{0.0, 0.0, 2.0}, {0.0, 0.0, 3.0}};
    auto on_curve_batch = kernel.predicates().point_on_curve_batch(curve_points, *line.value, 1e-6);
    auto on_surface_batch = kernel.predicates().point_on_surface_batch(surface_points, *sphere.value, 1e-5);
    const std::vector<axiom::Point3> body_points {{1.0, 1.0, 1.0}, {3.0, 3.0, 3.0}};
    auto in_body_batch = kernel.predicates().point_in_body_batch(body_points, *box.value, 1e-6);
    if (on_curve.status != axiom::StatusCode::Ok || on_surface.status != axiom::StatusCode::Ok ||
        neg_tol.status != axiom::StatusCode::InvalidInput || !on_curve.value.has_value() ||
        !on_surface.value.has_value() || !*on_curve.value || !*on_surface.value ||
        on_curve_batch.status != axiom::StatusCode::Ok || on_surface_batch.status != axiom::StatusCode::Ok ||
        !on_curve_batch.value.has_value() || !on_surface_batch.value.has_value() ||
        on_curve_batch.value->size() != 2 || on_surface_batch.value->size() != 2 ||
        !on_curve_batch.value->at(0) || on_curve_batch.value->at(1) ||
        !on_surface_batch.value->at(0) || on_surface_batch.value->at(1) ||
        in_body_batch.status != axiom::StatusCode::Ok || !in_body_batch.value.has_value() ||
        in_body_batch.value->size() != 2 || !in_body_batch.value->at(0) || in_body_batch.value->at(1)) {
        std::cerr << "unexpected predicate semantic behavior\n";
        return 1;
    }

    auto body_policy = kernel.tolerance().policy_for_body(*box.value);
    auto tiny_policy = kernel.tolerance().policy_for_body(*tiny_box.value);
    auto fallback_policy = kernel.tolerance().choose_body_or_global({99999999});
    auto body_policy_nl = kernel.tolerance().scale_policy_for_body_nonlinear(kernel.tolerance().global_policy(), *box.value);
    auto tiny_policy_nl = kernel.tolerance().scale_policy_for_body_nonlinear(kernel.tolerance().global_policy(), *tiny_box.value);
    if (!kernel.tolerance().is_valid_policy(body_policy) || !kernel.tolerance().is_valid_policy(tiny_policy) ||
        !kernel.tolerance().is_valid_policy(body_policy_nl) || !kernel.tolerance().is_valid_policy(tiny_policy_nl) ||
        !approx(fallback_policy.linear, kernel.tolerance().global_policy().linear) ||
        body_policy.linear < tiny_policy.linear || body_policy_nl.linear < tiny_policy_nl.linear) {
        std::cerr << "unexpected policy_for_body behavior\n";
        return 1;
    }

    return 0;
}
