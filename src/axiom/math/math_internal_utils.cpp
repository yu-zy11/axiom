#include "axiom/internal/math/math_internal_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "axiom/internal/core/kernel_state.h"

namespace axiom::detail {

Scalar safe_norm(const Vec3& value) {
    // std::hypot provides better overflow/underflow behavior than sqrt(x*x+y*y+z*z).
    const auto n = std::hypot(value.x, value.y, value.z);
    return std::isfinite(n) ? n : std::numeric_limits<Scalar>::infinity();
}

Scalar clamp_cosine(Scalar value) {
    if (!std::isfinite(value)) {
        // Treat non-finite cosine as "unknown"; return a safe in-range value.
        // 1.0 keeps acos() stable (returns 0) and avoids propagating NaN.
        return 1.0;
    }
    return std::clamp(value, -1.0, 1.0);
}

bool finite_vec3(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite_point3(const Point3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool valid_bbox(const BoundingBox& bbox) {
    return bbox.is_valid &&
           finite_point3(bbox.min) &&
           finite_point3(bbox.max) &&
           bbox.max.x >= bbox.min.x &&
           bbox.max.y >= bbox.min.y &&
           bbox.max.z >= bbox.min.z;
}

bool point_in_bbox_tol(const Point3& point, const BoundingBox& bbox, Scalar tolerance) {
    if (!finite_point3(point) || !valid_bbox(bbox) || !std::isfinite(tolerance)) {
        return false;
    }
    const auto tol = std::max(tolerance, 0.0);
    return point.x >= bbox.min.x - tol && point.x <= bbox.max.x + tol &&
           point.y >= bbox.min.y - tol && point.y <= bbox.max.y + tol &&
           point.z >= bbox.min.z - tol && point.z <= bbox.max.z + tol;
}

Transform3 identity_transform() {
    return Transform3 {};
}

Transform3 compose_transform(const Transform3& lhs, const Transform3& rhs) {
    Transform3 out;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            Scalar acc = 0.0;
            for (int k = 0; k < 4; ++k) {
                acc += lhs.m[r * 4 + k] * rhs.m[k * 4 + c];
            }
            out.m[r * 4 + c] = acc;
        }
    }
    return out;
}

Transform3 translation_transform(const Vec3& delta) {
    Transform3 out = identity_transform();
    out.m[3] = delta.x;
    out.m[7] = delta.y;
    out.m[11] = delta.z;
    return out;
}

Transform3 scale_transform(Scalar sx, Scalar sy, Scalar sz) {
    Transform3 out = identity_transform();
    out.m[0] = sx;
    out.m[5] = sy;
    out.m[10] = sz;
    return out;
}

Transform3 rotation_axis_angle_transform(const Vec3& axis, Scalar angle_rad) {
    const auto n = safe_norm(axis);
    if (n <= std::numeric_limits<Scalar>::epsilon()) {
        return identity_transform();
    }
    const auto u = scale(axis, 1.0 / n);
    const auto c = std::cos(angle_rad);
    const auto s = std::sin(angle_rad);
    const auto one_minus_c = 1.0 - c;

    Transform3 out = identity_transform();
    out.m[0] = c + u.x * u.x * one_minus_c;
    out.m[1] = u.x * u.y * one_minus_c - u.z * s;
    out.m[2] = u.x * u.z * one_minus_c + u.y * s;
    out.m[4] = u.y * u.x * one_minus_c + u.z * s;
    out.m[5] = c + u.y * u.y * one_minus_c;
    out.m[6] = u.y * u.z * one_minus_c - u.x * s;
    out.m[8] = u.z * u.x * one_minus_c - u.y * s;
    out.m[9] = u.z * u.y * one_minus_c + u.x * s;
    out.m[10] = c + u.z * u.z * one_minus_c;
    return out;
}

bool invert_affine_transform(const Transform3& in, Transform3& out, Scalar eps) {
    const auto tol = std::max(eps, 0.0);
    for (int k = 0; k < 12; ++k) {
        if (!std::isfinite(in.m[k])) {
            return false;
        }
    }
    const long double a = static_cast<long double>(in.m[0]);
    const long double b = static_cast<long double>(in.m[1]);
    const long double c = static_cast<long double>(in.m[2]);
    const long double d = static_cast<long double>(in.m[4]);
    const long double e = static_cast<long double>(in.m[5]);
    const long double f = static_cast<long double>(in.m[6]);
    const long double g = static_cast<long double>(in.m[8]);
    const long double h = static_cast<long double>(in.m[9]);
    const long double i = static_cast<long double>(in.m[10]);
    const long double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(static_cast<double>(det)) ||
        std::abs(det) <= static_cast<long double>(tol)) {
        return false;
    }
    const long double inv_det = 1.0L / det;
    out = identity_transform();
    out.m[0] = static_cast<Scalar>((e * i - f * h) * inv_det);
    out.m[1] = static_cast<Scalar>((c * h - b * i) * inv_det);
    out.m[2] = static_cast<Scalar>((b * f - c * e) * inv_det);
    out.m[4] = static_cast<Scalar>((f * g - d * i) * inv_det);
    out.m[5] = static_cast<Scalar>((a * i - c * g) * inv_det);
    out.m[6] = static_cast<Scalar>((c * d - a * f) * inv_det);
    out.m[8] = static_cast<Scalar>((d * h - e * g) * inv_det);
    out.m[9] = static_cast<Scalar>((b * g - a * h) * inv_det);
    out.m[10] = static_cast<Scalar>((a * e - b * d) * inv_det);

    const Vec3 t {in.m[3], in.m[7], in.m[11]};
    const Vec3 it {
        -(out.m[0] * t.x + out.m[1] * t.y + out.m[2] * t.z),
        -(out.m[4] * t.x + out.m[5] * t.y + out.m[6] * t.z),
        -(out.m[8] * t.x + out.m[9] * t.y + out.m[10] * t.z)
    };
    out.m[3] = it.x;
    out.m[7] = it.y;
    out.m[11] = it.z;
    return true;
}

Scalar vec_angle_between(const Vec3& lhs, const Vec3& rhs) {
    const auto nl = safe_norm(lhs);
    const auto nr = safe_norm(rhs);
    if (!std::isfinite(nl) || !std::isfinite(nr) ||
        nl <= std::numeric_limits<Scalar>::epsilon() || nr <= std::numeric_limits<Scalar>::epsilon()) {
        return 0.0;
    }
    const long double num = static_cast<long double>(lhs.x) * static_cast<long double>(rhs.x) +
                            static_cast<long double>(lhs.y) * static_cast<long double>(rhs.y) +
                            static_cast<long double>(lhs.z) * static_cast<long double>(rhs.z);
    const long double den = static_cast<long double>(nl) * static_cast<long double>(nr);
    if (den <= 0.0L || !std::isfinite(static_cast<double>(num)) || !std::isfinite(static_cast<double>(den))) {
        return std::numeric_limits<Scalar>::quiet_NaN();
    }
    const long double c = num / den;
    if (!std::isfinite(static_cast<double>(c))) {
        return std::numeric_limits<Scalar>::quiet_NaN();
    }
    const auto cos_v = clamp_cosine(static_cast<Scalar>(c));
    return std::acos(cos_v);
}

Vec3 project_vec(const Vec3& lhs, const Vec3& rhs) {
    const auto denom = dot(rhs, rhs);
    if (denom <= std::numeric_limits<Scalar>::epsilon()) {
        return Vec3 {};
    }
    return scale(rhs, dot(lhs, rhs) / denom);
}

Vec3 reject_vec(const Vec3& lhs, const Vec3& rhs) {
    const auto projected = project_vec(lhs, rhs);
    return Vec3 {lhs.x - projected.x, lhs.y - projected.y, lhs.z - projected.z};
}

bool vec_parallel_tol(const Vec3& lhs, const Vec3& rhs, Scalar angular_tolerance) {
    if (!finite_vec3(lhs) || !finite_vec3(rhs) || !std::isfinite(angular_tolerance)) {
        return false;
    }
    const auto angle = vec_angle_between(lhs, rhs);
    const auto tol = std::max(angular_tolerance, 0.0);
    return angle <= tol || std::abs(angle - std::acos(-1.0)) <= tol;
}

bool vec_orthogonal_tol(const Vec3& lhs, const Vec3& rhs, Scalar angular_tolerance) {
    if (!finite_vec3(lhs) || !finite_vec3(rhs) || !std::isfinite(angular_tolerance)) {
        return false;
    }
    const auto angle = vec_angle_between(lhs, rhs);
    const auto tol = std::max(angular_tolerance, 0.0);
    return std::abs(angle - (std::acos(-1.0) * 0.5)) <= tol;
}

bool bbox_intersects_tol(const BoundingBox& lhs, const BoundingBox& rhs, Scalar tolerance) {
    if (!valid_bbox(lhs) || !valid_bbox(rhs) || !std::isfinite(tolerance)) {
        return false;
    }
    const auto tol = std::max(tolerance, 0.0);
    return lhs.min.x <= rhs.max.x + tol && lhs.max.x + tol >= rhs.min.x &&
           lhs.min.y <= rhs.max.y + tol && lhs.max.y + tol >= rhs.min.y &&
           lhs.min.z <= rhs.max.z + tol && lhs.max.z + tol >= rhs.min.z;
}

bool bbox_contains_tol(const BoundingBox& outer, const BoundingBox& inner, Scalar tolerance) {
    if (!valid_bbox(outer) || !valid_bbox(inner) || !std::isfinite(tolerance)) {
        return false;
    }
    const auto tol = std::max(tolerance, 0.0);
    return inner.min.x >= outer.min.x - tol && inner.max.x <= outer.max.x + tol &&
           inner.min.y >= outer.min.y - tol && inner.max.y <= outer.max.y + tol &&
           inner.min.z >= outer.min.z - tol && inner.max.z <= outer.max.z + tol;
}

bool point_equal_tol(const Point3& lhs, const Point3& rhs, Scalar tolerance) {
    if (!finite_point3(lhs) || !finite_point3(rhs) || !std::isfinite(tolerance)) {
        return false;
    }
    const long double tol = static_cast<long double>(std::max(tolerance, 0.0));
    const long double dx =
        std::abs(static_cast<long double>(lhs.x) - static_cast<long double>(rhs.x));
    const long double dy =
        std::abs(static_cast<long double>(lhs.y) - static_cast<long double>(rhs.y));
    const long double dz =
        std::abs(static_cast<long double>(lhs.z) - static_cast<long double>(rhs.z));
    if (!std::isfinite(static_cast<double>(dx)) || !std::isfinite(static_cast<double>(dy)) ||
        !std::isfinite(static_cast<double>(dz))) {
        return false;
    }
    return dx <= tol && dy <= tol && dz <= tol;
}

Scalar squared_distance_point_to_segment(const Point3& point, const Point3& seg_a, const Point3& seg_b) {
    if (!finite_point3(point) || !finite_point3(seg_a) || !finite_point3(seg_b)) {
        return std::numeric_limits<Scalar>::infinity();
    }
    const long double ax = static_cast<long double>(seg_a.x);
    const long double ay = static_cast<long double>(seg_a.y);
    const long double az = static_cast<long double>(seg_a.z);
    const long double bx = static_cast<long double>(seg_b.x);
    const long double by = static_cast<long double>(seg_b.y);
    const long double bz = static_cast<long double>(seg_b.z);
    const long double abx = bx - ax;
    const long double aby = by - ay;
    const long double abz = bz - az;
    const long double ab2 = abx * abx + aby * aby + abz * abz;
    if (!std::isfinite(static_cast<double>(ab2))) {
        return std::numeric_limits<Scalar>::infinity();
    }
    const long double px = static_cast<long double>(point.x);
    const long double py = static_cast<long double>(point.y);
    const long double pz = static_cast<long double>(point.z);
    if (ab2 <= 0.0L) {
        const long double apx = px - ax;
        const long double apy = py - ay;
        const long double apz = pz - az;
        const long double d2 = apx * apx + apy * apy + apz * apz;
        if (!std::isfinite(static_cast<double>(d2))) {
            return std::numeric_limits<Scalar>::infinity();
        }
        return static_cast<Scalar>(d2);
    }
    const long double apx = px - ax;
    const long double apy = py - ay;
    const long double apz = pz - az;
    const long double tnum = apx * abx + apy * aby + apz * abz;
    if (!std::isfinite(static_cast<double>(tnum))) {
        return std::numeric_limits<Scalar>::infinity();
    }
    long double t = tnum / ab2;
    if (t < 0.0L) {
        t = 0.0L;
    } else if (t > 1.0L) {
        t = 1.0L;
    }
    const long double cx = ax + t * abx;
    const long double cy = ay + t * aby;
    const long double cz = az + t * abz;
    const long double dx = px - cx;
    const long double dy = py - cy;
    const long double dz = pz - cz;
    const long double d2 = dx * dx + dy * dy + dz * dz;
    if (!std::isfinite(static_cast<double>(d2))) {
        return std::numeric_limits<Scalar>::infinity();
    }
    return static_cast<Scalar>(d2);
}

bool point_on_segment_tol(const Point3& point, const Point3& seg_a, const Point3& seg_b, Scalar tolerance) {
    if (!std::isfinite(tolerance)) {
        return false;
    }
    const auto tol = std::max(tolerance, 0.0);
    const auto s2 = squared_distance_point_to_segment(point, seg_a, seg_b);
    if (!std::isfinite(s2)) {
        return false;
    }
    const long double t = static_cast<long double>(tol);
    return static_cast<long double>(s2) <= t * t;
}

Scalar bbox_characteristic_length(const BoundingBox& bbox) {
    if (!valid_bbox(bbox) || !finite_point3(bbox.min) || !finite_point3(bbox.max)) {
        return 1.0;
    }
    const auto ex = std::max<Scalar>(0.0, bbox.max.x - bbox.min.x);
    const auto ey = std::max<Scalar>(0.0, bbox.max.y - bbox.min.y);
    const auto ez = std::max<Scalar>(0.0, bbox.max.z - bbox.min.z);
    const auto diag = std::hypot(ex, ey, ez);
    if (!std::isfinite(diag) || diag <= 0.0) {
        return 1.0;
    }
    return diag;
}

Scalar effective_tolerance(Scalar requested, Scalar fallback) {
    if (requested > 0.0) {
        return requested;
    }
    return std::max(fallback, 0.0);
}

Scalar clamp_local_tolerance(Scalar value, const TolerancePolicy& policy) {
    const auto min_t = std::max(policy.min_local, 0.0);
    const auto max_t = std::max(policy.max_local, min_t);
    if (!std::isfinite(value)) {
        return max_t;
    }
    return std::clamp(std::max(value, 0.0), min_t, max_t);
}

Scalar resolve_linear_tolerance(Scalar requested, const TolerancePolicy& policy) {
    return clamp_local_tolerance(effective_tolerance(requested, policy.linear), policy);
}

Scalar resolve_angular_tolerance(Scalar requested, const TolerancePolicy& policy) {
    return clamp_local_tolerance(effective_tolerance(requested, policy.angular), policy);
}

Scalar resolve_linear_tolerance_for_scale(Scalar requested, const TolerancePolicy& policy, Scalar model_scale) {
    // `std::max(NaN, x)` 对浮点为未定义行为；非有限尺度按“极大尺度”处理，由 clamp 落到 max_local。
    const Scalar scale = std::isfinite(model_scale) ? std::max(model_scale, static_cast<Scalar>(1e-12))
                                                    : std::numeric_limits<Scalar>::infinity();
    return clamp_local_tolerance(resolve_linear_tolerance(requested, policy) * scale, policy);
}

Scalar resolve_angular_tolerance_for_scale(Scalar requested, const TolerancePolicy& policy, Scalar model_scale) {
    const Scalar ratio =
        std::isfinite(model_scale)
            ? std::clamp(std::max(model_scale, static_cast<Scalar>(1e-12)), static_cast<Scalar>(0.1),
                         static_cast<Scalar>(10.0))
            : static_cast<Scalar>(10.0);
    return clamp_local_tolerance(resolve_angular_tolerance(requested, policy) * ratio, policy);
}

bool within_tolerance(Scalar lhs, Scalar rhs, Scalar tolerance) {
    if (!std::isfinite(lhs) || !std::isfinite(rhs) || !std::isfinite(tolerance)) {
        return false;
    }
    const long double diff =
        std::abs(static_cast<long double>(lhs) - static_cast<long double>(rhs));
    if (!std::isfinite(static_cast<double>(diff))) {
        return false;
    }
    return diff <= static_cast<long double>(std::max(tolerance, 0.0));
}

bool nearly_equal_rel_abs(Scalar lhs, Scalar rhs, Scalar abs_tol, Scalar rel_tol) {
    if (!std::isfinite(lhs) || !std::isfinite(rhs) || !std::isfinite(abs_tol) ||
        !std::isfinite(rel_tol)) {
        return false;
    }
    const auto a = std::max(abs_tol, 0.0);
    const auto r = std::max(rel_tol, 0.0);
    const auto diff = std::abs(lhs - rhs);
    const auto scale = std::max(std::abs(lhs), std::abs(rhs));
    return diff <= std::max(a, r * scale);
}

int compare_rel_abs(Scalar lhs, Scalar rhs, Scalar abs_tol, Scalar rel_tol) {
    if (!std::isfinite(lhs) || !std::isfinite(rhs) || !std::isfinite(abs_tol) ||
        !std::isfinite(rel_tol)) {
        return 0;
    }
    if (nearly_equal_rel_abs(lhs, rhs, abs_tol, rel_tol)) {
        return 0;
    }
    return lhs < rhs ? -1 : 1;
}

TolerancePolicy clamp_tolerance_policy(const TolerancePolicy& base) {
    TolerancePolicy out = base;
    out.linear = std::clamp(out.linear, 0.0, 1.0);
    out.angular = std::clamp(out.angular, 0.0, 1.0);
    out.min_local = std::clamp(out.min_local, 0.0, 1.0);
    out.max_local = std::clamp(out.max_local, out.min_local, 1.0);
    return out;
}

TolerancePolicy scale_tolerance_policy(const TolerancePolicy& base, Scalar factor) {
    const auto f = std::max(factor, 0.0);
    TolerancePolicy out = base;
    out.linear *= f;
    out.angular *= f;
    out.min_local *= f;
    out.max_local *= f;
    return clamp_tolerance_policy(out);
}

TolerancePolicy merge_tolerance_policy(const TolerancePolicy& primary, const TolerancePolicy& fallback) {
    TolerancePolicy out = primary;
    if (out.linear <= 0.0) out.linear = fallback.linear;
    if (out.angular <= 0.0) out.angular = fallback.angular;
    if (out.min_local <= 0.0) out.min_local = fallback.min_local;
    if (out.max_local <= 0.0) out.max_local = fallback.max_local;
    if (out.max_local < out.min_local) out.max_local = out.min_local;
    return clamp_tolerance_policy(out);
}

TolerancePolicy tolerance_with_angular(const TolerancePolicy& base, Scalar angular) {
    auto out = base;
    out.angular = angular;
    return clamp_tolerance_policy(out);
}

bool valid_tolerance_policy(const TolerancePolicy& policy) {
    return policy.linear >= 0.0 &&
           policy.angular >= 0.0 &&
           policy.min_local >= 0.0 &&
           policy.max_local >= policy.min_local;
}

}  // namespace axiom::detail
