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
    if (!std::isnan(kernel.linear_algebra().angle_between({NAN, 0.0, 0.0}, x))) {
        std::cerr << "expected angle_between NaN for non-finite operand\n";
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
    if (!std::isnan(kernel.linear_algebra().midpoint(p0, {NAN, 0.0, 0.0}).x) ||
        !std::isnan(kernel.linear_algebra().lerp(p0, p1, NAN).x) ||
        kernel.linear_algebra().is_near_zero({1e-20, 0.0, 0.0}, NAN) ||
        !std::isnan(kernel.linear_algebra().clamp_norm({10.0, 0.0, 0.0}, NAN).x) ||
        kernel.linear_algebra().orthonormal_basis({NAN, 0.0, 1.0}).has_value()) {
        std::cerr << "unexpected finite semantics for midpoint/lerp/is_near_zero/clamp_norm/orthonormal_basis\n";
        return 1;
    }

    auto proj = kernel.linear_algebra().project({2.0, 2.0, 0.0}, x);
    auto rej = kernel.linear_algebra().reject({2.0, 2.0, 0.0}, x);
    if (!approx(proj.x, 2.0) || !approx(proj.y, 0.0) || !approx(rej.y, 2.0) ||
        !kernel.linear_algebra().is_finite(proj) || !kernel.linear_algebra().is_near_zero({1e-10, 0.0, 0.0}, 1e-8)) {
        std::cerr << "unexpected projection/rejection behavior\n";
        return 1;
    }
    if (!std::isnan(kernel.linear_algebra().project({NAN, 0.0, 0.0}, x).x) ||
        !std::isnan(kernel.linear_algebra().reject({2.0, 0.0, 0.0}, {NAN, 1.0, 0.0}).x)) {
        std::cerr << "expected project/reject NaN for non-finite operands\n";
        return 1;
    }
    // 大尺度下投影系数仍可用 long double 路径保持稳定（a 与 b 共线 ⇒ 投影为 s * b̂ 方向上的 (s,s,0)）
    {
        const double s = 1e100;
        const axiom::Vec3 a {s, s, 0.0};
        const axiom::Vec3 b {1.0, 1.0, 0.0};
        const auto pr = kernel.linear_algebra().project(a, b);
        if (!std::isfinite(pr.x) || !std::isfinite(pr.y) || pr.z != 0.0 ||
            std::abs(pr.x - s) > 1e-6 * std::max(std::abs(s), 1.0) ||
            std::abs(pr.y - s) > 1e-6 * std::max(std::abs(s), 1.0)) {
            std::cerr << "unexpected large-scale project behavior\n";
            return 1;
        }
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
    {
        axiom::Transform3 bad = t;
        bad.m[3] = NAN;
        const auto q = kernel.linear_algebra().transform(axiom::Point3{1.0, 0.0, 0.0}, bad);
        const auto w = kernel.linear_algebra().transform(x, bad);
        if (!std::isnan(q.x) || !std::isnan(w.x) ||
            !std::isnan(kernel.linear_algebra().transform(axiom::Point3{NAN, 0.0, 0.0}, t).x)) {
            std::cerr << "expected transform NaN for non-finite matrix or point\n";
            return 1;
        }
    }
    if (!approx(kernel.linear_algebra().distance_point_to_line({1.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), 2.0) ||
        !approx(kernel.linear_algebra().distance_point_to_segment({2.0, 2.0, 0.0}, {0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}), 2.0) ||
        !approx(kernel.linear_algebra().distance_point_to_segment({-1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}), 1.0) ||
        !approx(kernel.linear_algebra().distance_point_to_segment({5.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}), 1.0) ||
        !approx(kernel.linear_algebra().distance_point_to_segment({1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), 0.0) ||
        !approx(kernel.linear_algebra().distance_point_to_plane({0.0, 0.0, 3.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}), 3.0) ||
        !approx(kernel.linear_algebra().triangle_area({0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 2.0, 0.0}), 2.0) ||
        !approx(kernel.linear_algebra().tetrahedron_signed_volume(
                    {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}),
                1.0 / 6.0) ||
        !std::isnan(kernel.linear_algebra().tetrahedron_signed_volume(
            {NAN, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0})) ||
        !std::isnan(kernel.linear_algebra().scalar_triple_product({NAN, 0.0, 0.0}, y, z)) ||
        !std::isnan(kernel.linear_algebra().triangle_area({0.0, 0.0, 0.0}, {1.0, 0.0, INFINITY}, {0.0, 1.0, 0.0})) ||
        !approx(kernel.linear_algebra().squared_norm({3.0, 4.0, 0.0}), 25.0)) {
        std::cerr << "unexpected geometry metric behavior\n";
        return 1;
    }
    if (!std::isinf(kernel.linear_algebra().manhattan_distance({NAN, 0.0, 0.0}, p0)) ||
        !std::isinf(kernel.linear_algebra().distance_point_to_line({NAN, 0.0, 0.0}, {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0})) ||
        !std::isinf(kernel.linear_algebra().distance_point_to_segment({0.0, 0.0, 0.0}, {NAN, 0.0, 0.0}, {1.0, 0.0, 0.0})) ||
        !std::isinf(
            kernel.linear_algebra().distance_point_to_plane({0.0, 0.0, 0.0}, {0.0, INFINITY, 0.0}, {0.0, 0.0, 1.0}))) {
        std::cerr << "expected distance metrics +Inf for non-finite operands\n";
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
    // Scale-adaptive robustness regression: very large coordinates should not overflow
    // and should still return a stable sign when the configuration is well-separated.
    {
        const double s = 1e150;
        const auto sign = kernel.predicates().orient2d({0.0, 0.0}, {s, 0.0}, {0.0, s});
        if (sign != axiom::Sign::Positive) {
            std::cerr << "unexpected orient2d behavior for large scale\n";
            return 1;
        }
        const auto sign3 = kernel.predicates().orient3d({0.0, 0.0, 0.0}, {s, 0.0, 0.0}, {0.0, s, 0.0}, {0.0, 0.0, s});
        if (sign3 != axiom::Sign::Positive) {
            std::cerr << "unexpected orient3d behavior for large scale\n";
            return 1;
        }
    }
    // Near-degenerate cases at large scale should be classified as Zero (scale-adaptive threshold).
    {
        const double s = 1e150;
        const auto sign = kernel.predicates().orient2d_tol({0.0, 0.0}, {s, 0.0}, {s, 1e-100}, 0.0);
        if (sign != axiom::Sign::Zero) {
            std::cerr << "expected orient2d_tol to be Zero for near-collinear large-scale input\n";
            return 1;
        }
        const auto sign3 = kernel.predicates().orient3d_tol({0.0, 0.0, 0.0}, {s, 0.0, 0.0}, {0.0, s, 0.0}, {0.0, 0.0, 1e-100}, 0.0);
        if (sign3 != axiom::Sign::Zero) {
            std::cerr << "expected orient3d_tol to be Zero for near-coplanar large-scale input\n";
            return 1;
        }
    }
    // Non-finite inputs should not silently claim a sign.
    {
        const auto sign = kernel.predicates().orient2d_tol({0.0, 0.0}, {NAN, 0.0}, {0.0, 1.0}, 1e-12);
        if (sign != axiom::Sign::Uncertain) {
            std::cerr << "expected orient2d_tol to be Uncertain for NaN input\n";
            return 1;
        }
        const auto sign3 = kernel.predicates().orient3d_tol({0.0, 0.0, 0.0}, {INFINITY, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, 1e-12);
        if (sign3 != axiom::Sign::Uncertain) {
            std::cerr << "expected orient3d_tol to be Uncertain for Inf input\n";
            return 1;
        }
        if (kernel.predicates().orient2d_tol({0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, NAN) != axiom::Sign::Uncertain ||
            kernel.predicates().orient3d_tol({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, NAN) !=
                axiom::Sign::Uncertain) {
            std::cerr << "expected orient*_tol Uncertain for non-finite eps\n";
            return 1;
        }
    }
    // Default orient2d/orient3d use kernel effective linear tolerance (not a hard-coded 1e-12).
    {
        axiom::KernelConfig cfg {};
        cfg.tolerance.linear = 1e-4;
        cfg.tolerance.min_local = 1e-9;
        cfg.tolerance.max_local = 1e-2;
        axiom::Kernel ko {cfg};
        if (!approx(ko.tolerance().effective_linear(0.0), 1e-4)) {
            std::cerr << "expected effective_linear(0) to match configured linear\n";
            return 1;
        }
        // det = 5e-5; user_tol floor 1e-4 => classified as degenerate (Zero).
        if (ko.predicates().orient2d({0.0, 0.0}, {1.0, 0.0}, {0.0, 5e-5}) != axiom::Sign::Zero) {
            std::cerr << "expected orient2d to respect kernel linear tolerance as user eps floor\n";
            return 1;
        }
        // Well-separated at unit scale remains Positive.
        if (ko.predicates().orient2d({0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}) != axiom::Sign::Positive) {
            std::cerr << "expected orient2d Positive for non-degenerate CCW triangle under widened tolerance\n";
            return 1;
        }
        if (ko.predicates().orient2d_effective({0.0, 0.0}, {1.0, 0.0}, {0.0, 5e-5}, 0.0) != axiom::Sign::Zero ||
            ko.predicates().orient3d_effective({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0},
                                             0.0) != ko.predicates().orient3d({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
                                                                            {0.0, 0.0, 1.0})) {
            std::cerr << "orient*_effective(0) should match default orient* for same kernel policy\n";
            return 1;
        }
    }
    // orient*_effective：请求容差经 min_local 钳制后与裸 orient*_tol(极小 eps) 行为可区分。
    {
        axiom::KernelConfig cfg {};
        cfg.tolerance.linear = 1e-8;
        cfg.tolerance.min_local = 1e-4;
        cfg.tolerance.max_local = 1.0;
        axiom::Kernel k {cfg};
        if (k.predicates().orient2d_tol({0.0, 0.0}, {1.0, 0.0}, {0.0, 5e-5}, 1e-12) != axiom::Sign::Positive ||
            k.predicates().orient2d_effective({0.0, 0.0}, {1.0, 0.0}, {0.0, 5e-5}, 1e-12) != axiom::Sign::Zero) {
            std::cerr << "expected orient2d_effective to apply clamped linear tolerance vs raw orient2d_tol\n";
            return 1;
        }
        // det(ab,ac,ad)=5e-5：裸 tol=1e-12 判为正，effective 经 min_local=1e-4 钳制后判为共面退化。
        if (k.predicates().orient3d_tol({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 5e-5, 0.0}, {0.0, 0.0, 1.0}, 1e-12) !=
                axiom::Sign::Positive ||
            k.predicates().orient3d_effective({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 5e-5, 0.0}, {0.0, 0.0, 1.0},
                                              1e-12) != axiom::Sign::Zero) {
            std::cerr << "expected orient3d_effective to apply clamped linear tolerance vs raw orient3d_tol\n";
            return 1;
        }
    }
    // *_effective：请求标量非有限时与裸 `*_tol` 门控一致，不得静默回退为策略默认容差。
    if (kernel.predicates().orient2d_effective({0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, NAN) != axiom::Sign::Uncertain ||
        kernel.predicates().orient3d_effective({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0},
                                            NAN) != axiom::Sign::Uncertain ||
        kernel.predicates().point_equal_effective({0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, NAN) ||
        kernel.predicates().vec_parallel_effective({1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, INFINITY) ||
        kernel.predicates().vec_orthogonal_effective({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, NAN)) {
        std::cerr << "expected *_effective to reject non-finite tolerance/angular request\n";
        return 1;
    }
    // P1 闭环：负向定向（与 CCW / 右手系正例对偶）。
    if (kernel.predicates().orient2d({0.0, 0.0}, {0.0, 1.0}, {1.0, 0.0}) != axiom::Sign::Negative ||
        kernel.predicates().orient3d({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}) !=
            axiom::Sign::Negative) {
        std::cerr << "expected orient2d/orient3d Negative for reversed winding vs canonical Positive\n";
        return 1;
    }
    // max_local 下调 `policy.linear`：`effective_linear(0)` 与 `*_effective` 门限一致，且可区分近退化与分离。
    {
        axiom::KernelConfig cfg {};
        cfg.tolerance.linear = 1e-2;
        cfg.tolerance.min_local = 1e-9;
        cfg.tolerance.max_local = 1e-5;
        axiom::Kernel k {cfg};
        if (!approx(k.tolerance().effective_linear(0.0), 1e-5)) {
            std::cerr << "expected effective_linear(0) clamped down by max_local\n";
            return 1;
        }
        const axiom::Point3 p {0.0, 0.0, 0.0};
        const axiom::Point3 q {2e-6, 0.0, 0.0};
        const axiom::Point3 r {2e-4, 0.0, 0.0};
        if (!k.predicates().point_equal_effective(p, q, 0.0) || k.predicates().point_equal_effective(p, r, 0.0)) {
            std::cerr << "unexpected point_equal_effective under max_local-clamped linear tolerance\n";
            return 1;
        }
        if (k.predicates().orient2d_effective({0.0, 0.0}, {1.0, 0.0}, {0.0, 3e-6}, 0.0) != axiom::Sign::Zero ||
            k.predicates().orient2d_effective({0.0, 0.0}, {1.0, 0.0}, {0.0, 3e-4}, 0.0) != axiom::Sign::Positive) {
            std::cerr << "unexpected orient2d_effective under max_local-clamped tolerance\n";
            return 1;
        }
    }

    const axiom::BoundingBox b0 {{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}, true};
    const axiom::BoundingBox b1 {{0.9, 0.9, 0.9}, {2.0, 2.0, 2.0}, true};
    // Vector / point predicates: non-finite coordinates must not return true spuriously.
    if (kernel.predicates().vec_parallel({NAN, 0.0, 0.0}, {1.0, 0.0, 0.0}, 1e-6) ||
        kernel.predicates().vec_parallel({1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, NAN) ||
        kernel.predicates().vec_orthogonal({1.0, 0.0, INFINITY}, {0.0, 1.0, 0.0}, 1e-6) ||
        kernel.predicates().point_equal_tol({0.0, 0.0, 0.0}, {NAN, 0.0, 0.0}, 1e-6) ||
        kernel.predicates().point_in_bbox({NAN, 0.5, 0.5}, b0, 0.02) ||
        kernel.predicates().point_in_sphere({0.0, 0.0, NAN}, {0.0, 0.0, 0.0}, 1.0, 1e-9)) {
        std::cerr << "expected non-finite predicate inputs to yield false (or Uncertain for orient)\n";
        return 1;
    }
    if (kernel.predicates().aabb_intersects(b0, b1, NAN) ||
        kernel.predicates().point_equal_tol({0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, NAN) ||
        !kernel.predicates().aabb_intersects(b0, b1, 0.0) ||
        !kernel.predicates().point_in_bbox({1.01, 0.5, 0.5}, b0, 0.02) ||
        !kernel.predicates().point_equal_tol({1.0, 1.0, 1.0}, {1.0 + 1e-7, 1.0, 1.0}, 1e-6) ||
        !kernel.predicates().bbox_contains({{0.0, 0.0, 0.0}, {3.0, 3.0, 3.0}, true}, b0, 0.0) ||
        !kernel.predicates().vec_parallel({1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, 1e-6) ||
        !kernel.predicates().vec_orthogonal({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1e-6) ||
        !kernel.predicates().vec_parallel_effective({1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, 0.0) ||
        !kernel.predicates().vec_orthogonal_effective({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 0.0)) {
        std::cerr << "unexpected bbox/vector predicate behavior\n";
        return 1;
    }
    if (kernel.predicates().vec_parallel_effective({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 0.0) !=
        kernel.predicates().vec_parallel({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, kernel.tolerance().effective_angular(0.0))) {
        std::cerr << "vec_parallel_effective should match vec_parallel with resolved angular tolerance\n";
        return 1;
    }
    auto inter = kernel.predicates().bbox_intersection(b0, b1, 0.0);
    if (kernel.predicates().bbox_intersection(b0, b1, NAN).has_value() ||
        !kernel.predicates().bbox_valid(b0) || !inter.has_value() ||
        kernel.predicates().bbox_overlap_ratio(b0, b1, NAN) != 0.0 ||
        !approx(kernel.predicates().bbox_overlap_ratio(b0, b1, 0.0), 0.0004291845, 1e-6) ||
        kernel.predicates().bbox_center_in(b0, {{-1.0, -1.0, -1.0}, {3.0, 3.0, 3.0}, true}, NAN) ||
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
    // Centralized tolerance resolution should respect min_local/max_local clamps (via KernelConfig).
    {
        axiom::KernelConfig cfg {};
        cfg.tolerance.linear = 1e-6;
        cfg.tolerance.angular = 1e-6;
        cfg.tolerance.min_local = 1e-4;
        cfg.tolerance.max_local = 2e-4;
        axiom::Kernel k2 {cfg};
        // Too small request clamps up to min_local.
        if (!approx(k2.tolerance().effective_linear(1e-12), 1e-4) ||
            !approx(k2.tolerance().effective_angular(1e-12), 1e-4)) {
            std::cerr << "unexpected effective_* clamp-up behavior\n";
            return 1;
        }
        // Too large request clamps down to max_local.
        if (!approx(k2.tolerance().effective_linear(1.0), 2e-4) ||
            !approx(k2.tolerance().effective_angular(1.0), 2e-4)) {
            std::cerr << "unexpected effective_* clamp-down behavior\n";
            return 1;
        }
        // Scale-aware resolution should still respect local clamps.
        if (!approx(k2.tolerance().resolve_linear_for_scale(1e-6, 10.0), 2e-4) ||
            !approx(k2.tolerance().resolve_angular_for_scale(1e-6, 10.0), 2e-4)) {
            std::cerr << "unexpected resolve_*_for_scale clamp behavior\n";
            return 1;
        }
        // Non-finite scale should not escape the local clamp window.
        if (!approx(k2.tolerance().resolve_linear_for_scale(1e-6, INFINITY), 2e-4) ||
            !approx(k2.tolerance().resolve_angular_for_scale(1e-6, INFINITY), 2e-4)) {
            std::cerr << "unexpected resolve_*_for_scale behavior for infinite scale\n";
            return 1;
        }
        // NaN model_scale must not trigger std::max(NaN, …) UB;钳制结果与无穷尺度一致。
        if (!approx(k2.tolerance().resolve_linear_for_scale(1e-6, NAN), 2e-4) ||
            !approx(k2.tolerance().resolve_angular_for_scale(1e-6, NAN), 2e-4)) {
            std::cerr << "unexpected resolve_*_for_scale behavior for NaN scale\n";
            return 1;
        }
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
    // 聚合：任一输入非有限则输出 NaN；dot/cross 与 normalize 对齐标量/向量有限性语义。
    {
        const std::vector<axiom::Point3> bad_pts {{1.0, 0.0, 0.0}, {NAN, 0.0, 0.0}};
        const std::vector<axiom::Vec3> bad_vecs {{1.0, 0.0, 0.0}, {0.0, INFINITY, 0.0}};
        const auto c_bad = kernel.linear_algebra().centroid(bad_pts);
        const auto a_bad = kernel.linear_algebra().average(bad_vecs);
        if (!std::isnan(c_bad.x) || !std::isnan(a_bad.x) ||
            !std::isnan(kernel.linear_algebra().dot({NAN, 0.0, 0.0}, x)) ||
            !std::isnan(kernel.linear_algebra().cross({1.0, 0.0, INFINITY}, y).x) ||
            !std::isnan(kernel.linear_algebra().normalize({NAN, 0.0, 0.0}).x)) {
            std::cerr << "expected NaN for non-finite dot/cross/normalize/centroid/average inputs\n";
            return 1;
        }
        const auto cr = kernel.linear_algebra().cross(x, y);
        if (!approx(cr.x, 0.0) || !approx(cr.y, 0.0) || !approx(cr.z, 1.0)) {
            std::cerr << "unexpected cross after numeric path change\n";
            return 1;
        }
        const double s = 1e100;
        if (kernel.linear_algebra().dot({s, 0.0, 0.0}, {s, 0.0, 0.0}) <= 0.0 ||
            !std::isfinite(kernel.linear_algebra().dot({s, 0.0, 0.0}, {s, 0.0, 0.0}))) {
            std::cerr << "expected large-scale dot to stay finite and positive\n";
            return 1;
        }
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

    // Rep classify_point 与 Predicate point_in_body 对 bbox 门控使用同一套 resolve_linear_tolerance(0, policy)。
    {
        axiom::KernelConfig cfg {};
        cfg.tolerance.linear = 0.02;
        cfg.tolerance.min_local = 1e-9;
        cfg.tolerance.max_local = 1.0;
        axiom::Kernel k3 {cfg};
        const axiom::Scalar tlin = k3.tolerance().effective_linear(0.0);
        if (!approx(tlin, 0.02)) {
            std::cerr << "cross-module tolerance: effective_linear mismatch\n";
            return 1;
        }
        auto b = k3.primitives().box({0.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
        if (b.status != axiom::StatusCode::Ok || !b.value.has_value()) {
            std::cerr << "cross-module tolerance: box creation failed\n";
            return 1;
        }
        const axiom::Point3 near_out {1.0 + tlin * 0.5, 0.5, 0.5};
        auto rep_cl = k3.representation().classify_point(*b.value, near_out);
        auto pred_in = k3.predicates().point_in_body(near_out, *b.value, 0.0);
        if (rep_cl.status != axiom::StatusCode::Ok || !rep_cl.value.has_value() || !*rep_cl.value ||
            pred_in.status != axiom::StatusCode::Ok || !pred_in.value.has_value() || !*pred_in.value) {
            std::cerr << "cross-module tolerance: Rep vs Predicate bbox gate mismatch\n";
            return 1;
        }
    }

    // valid_bbox 含角点有限性：非有限包围盒不得参与相交/包含谓词。
    {
        const axiom::BoundingBox bad {axiom::Point3{NAN, 0.0, 0.0}, axiom::Point3{1.0, 1.0, 1.0}, true};
        const axiom::BoundingBox ok {axiom::Point3{0.0, 0.0, 0.0}, axiom::Point3{1.0, 1.0, 1.0}, true};
        if (kernel.predicates().bbox_valid(bad) || kernel.predicates().aabb_intersects(bad, ok, 0.0) ||
            kernel.predicates().aabb_intersects(ok, bad, 0.0)) {
            std::cerr << "expected invalid bbox / no intersection for non-finite corners\n";
            return 1;
        }
    }
    // Range1D 非有限端点不得给出“相交”真值。
    const axiom::Range2D r2_ok {axiom::Range1D{0.0, 1.0}, axiom::Range1D{0.0, 1.0}};
    const axiom::Range2D r2_bad {axiom::Range1D{NAN, 1.0}, axiom::Range1D{0.0, 1.0}};
    if (kernel.predicates().range1d_overlap({0.0, 1.0}, {NAN, 2.0}, 0.0) ||
        kernel.predicates().range2d_overlap(r2_ok, r2_bad, 0.0)) {
        std::cerr << "expected range overlap false for non-finite bounds\n";
        return 1;
    }
    // compare_*：非有限标量不强行排序，返回 0。
    if (kernel.tolerance().compare_linear(NAN, 1.0, 1e-3) != 0 ||
        kernel.tolerance().compare_angular(INFINITY, 0.1, 1e-3) != 0) {
        std::cerr << "expected compare_linear/angular to return 0 for non-finite operands\n";
        return 1;
    }
    // within_* / compare_*：传入的容差实参非有限时不得给出真值或有序结果。
    if (kernel.tolerance().within_linear(1.0, 1.0, NAN) ||
        kernel.tolerance().within_angular(0.1, 0.1, INFINITY) ||
        kernel.tolerance().compare_linear(1.0, 2.0, NAN) != 0) {
        std::cerr << "expected within_* false and compare_* 0 for non-finite tolerance argument\n";
        return 1;
    }
    // 混合绝对/相对比较：绝对项走 effective_linear，供全链路统一标量比较。
    if (!kernel.tolerance().nearly_equal_linear(1.0, 1.0 + 5e-7, 0.0, 1e-6) ||
        kernel.tolerance().nearly_equal_linear(NAN, 1.0, 0.0, 0.0) ||
        kernel.tolerance().compare_linear_rel_abs(1.0, 2.0, 0.0, 0.0) >= 0) {
        std::cerr << "unexpected nearly_equal_linear / compare_linear_rel_abs behavior\n";
        return 1;
    }
    if (!kernel.tolerance().nearly_equal_angular(0.1, 0.1 + 5e-7, 0.0, 1e-6) ||
        kernel.tolerance().nearly_equal_angular(NAN, 0.1, 0.0, 0.0) ||
        kernel.tolerance().compare_angular_rel_abs(0.1, 0.5, 0.0, 0.0) >= 0) {
        std::cerr << "unexpected nearly_equal_angular / compare_angular_rel_abs behavior\n";
        return 1;
    }
    // point_equal_effective 与 resolve_linear_tolerance(0) + point_equal_tol 一致。
    if (!kernel.predicates().point_equal_effective({1.0, 1.0, 1.0}, {1.0 + 5e-7, 1.0, 1.0}, 0.0) ||
        !kernel.predicates().point_equal_tol({1.0, 1.0, 1.0}, {1.0 + 5e-7, 1.0, 1.0},
                                            kernel.tolerance().effective_linear(0.0))) {
        std::cerr << "unexpected point_equal_effective vs resolved point_equal_tol\n";
        return 1;
    }
    // point_equal_tol / vec_parallel：坐标差与 dot/(|a||b|) 用 long double，与 squared_distance 同量级平移场景一致。
    {
        const double s = 1e50;
        const double delta = 1e40;
        const axiom::Point3 pa {s, 0.0, 0.0};
        const axiom::Point3 pb {s + delta, 0.0, 0.0};
        if (!kernel.predicates().point_equal_tol(pa, pb, 2.0 * delta) ||
            kernel.predicates().point_equal_tol(pa, pb, 0.5 * delta)) {
            std::cerr << "unexpected point_equal_tol at large translation\n";
            return 1;
        }
        if (!kernel.predicates().vec_parallel({s, 0.0, 0.0}, {2.0 * s, 0.0, 0.0}, 1e-9) ||
            !kernel.predicates().vec_parallel({0.0, s, 0.0}, {0.0, -s, 0.0}, 1e-9)) {
            std::cerr << "unexpected vec_parallel at large scale\n";
            return 1;
        }
    }
    // point_on_segment_*：与 distance_point_to_segment 同一欧氏度量；effective 与 resolve_linear_tolerance 对齐。
    if (!kernel.predicates().point_on_segment_tol({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, 1e-9) ||
        kernel.predicates().point_on_segment_tol({1.0, 0.01, 0.0}, {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, 1e-9) ||
        !kernel.predicates().point_on_segment_tol({1.0, 0.01, 0.0}, {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, 0.02) ||
        !kernel.predicates().point_on_segment_tol({1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 1e-9) ||
        kernel.predicates().point_on_segment_tol({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, NAN) ||
        kernel.predicates().point_on_segment_effective({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, NAN)) {
        std::cerr << "unexpected point_on_segment_tol / point_on_segment_effective\n";
        return 1;
    }
    if (!kernel.predicates().point_on_segment_effective({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, 0.0) ||
        !kernel.predicates().point_on_segment_tol({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0},
                                                  kernel.tolerance().effective_linear(0.0))) {
        std::cerr << "unexpected point_on_segment_effective vs resolved linear tolerance\n";
        return 1;
    }
    // squared_norm：大尺度下避免裸 double 累加过早 Inf（与 safe_norm 策略一致）。
    {
        const double s = 1e200;
        const axiom::Vec3 v {s, 0.0, 0.0};
        const auto sn = kernel.linear_algebra().squared_norm(v);
        if (std::isnan(sn) || !(sn > 0.0)) {
            std::cerr << "expected positive non-NaN squared_norm for large axis-aligned vector\n";
            return 1;
        }
    }
    // squared_distance：与 squared_norm 相同的大坐标安全策略；非有限坐标返回 +Inf。
    {
        const double s = 1e50;
        // 间隔需大于 |s| 附近的 ULP，且平方需在 double 范围内（避免 long double→double 溢出）。
        const double delta = 1e40;
        const axiom::Point3 a {s, 0.0, 0.0};
        const axiom::Point3 b {s + delta, 0.0, 0.0};
        const auto d2 = kernel.linear_algebra().squared_distance(a, b);
        // `b.x` 为 double 上 `s + delta` 的舍入结果，与数学上的 `delta` 可有 ~1e-6 量级相对误差。
        const double expect = delta * delta;
        if (std::isnan(d2) || !std::isfinite(d2) ||
            std::abs(d2 - expect) > 1e-5 * std::max(expect, 1.0)) {
            std::cerr << "unexpected squared_distance for large-separated points\n";
            return 1;
        }
        if (!std::isinf(kernel.linear_algebra().squared_distance({NAN, 0.0, 0.0}, {0.0, 0.0, 0.0}))) {
            std::cerr << "expected squared_distance +Inf for non-finite point\n";
            return 1;
        }
    }
    // tetrahedron_signed_volume：大坐标下仍保持有符号体积比例（与 orient3d 一致的右手系）。
    {
        const double s = 1e40;
        const auto v = kernel.linear_algebra().tetrahedron_signed_volume(
            {0.0, 0.0, 0.0}, {s, 0.0, 0.0}, {0.0, s, 0.0}, {0.0, 0.0, s});
        const double expect = (s * s * s) / 6.0;
        if (std::isnan(v) || !std::isfinite(v) ||
            std::abs(v - expect) > 1e-10 * std::max(std::abs(expect), 1.0)) {
            std::cerr << "unexpected tetrahedron_signed_volume at large scale\n";
            return 1;
        }
    }
    // distance_point_to_segment：大坐标平移后仍得到与单位段相同的几何距离（平移量需在 double 下与段长可辨）。
    {
        const double s = 1e10;
        const axiom::Point3 a {s, 0.0, 0.0};
        const axiom::Point3 b {s + 4.0, 0.0, 0.0};
        const axiom::Point3 p {s + 2.0, 2.0, 0.0};
        const auto d = kernel.linear_algebra().distance_point_to_segment(p, a, b);
        if (!approx(d, 2.0, 1e-5)) {
            std::cerr << "unexpected distance_point_to_segment at large translation\n";
            return 1;
        }
    }

    return 0;
}
