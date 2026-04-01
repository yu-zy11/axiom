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
           bbox.max.x >= bbox.min.x &&
           bbox.max.y >= bbox.min.y &&
           bbox.max.z >= bbox.min.z;
}

bool point_in_bbox_tol(const Point3& point, const BoundingBox& bbox, Scalar tolerance) {
    if (!finite_point3(point) || !valid_bbox(bbox)) {
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
    const Scalar a = in.m[0], b = in.m[1], c = in.m[2];
    const Scalar d = in.m[4], e = in.m[5], f = in.m[6];
    const Scalar g = in.m[8], h = in.m[9], i = in.m[10];
    const Scalar det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(det) <= tol) {
        return false;
    }
    const Scalar inv_det = 1.0 / det;
    out = identity_transform();
    out.m[0] = (e * i - f * h) * inv_det;
    out.m[1] = (c * h - b * i) * inv_det;
    out.m[2] = (b * f - c * e) * inv_det;
    out.m[4] = (f * g - d * i) * inv_det;
    out.m[5] = (a * i - c * g) * inv_det;
    out.m[6] = (c * d - a * f) * inv_det;
    out.m[8] = (d * h - e * g) * inv_det;
    out.m[9] = (b * g - a * h) * inv_det;
    out.m[10] = (a * e - b * d) * inv_det;

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
    if (nl <= std::numeric_limits<Scalar>::epsilon() || nr <= std::numeric_limits<Scalar>::epsilon()) {
        return 0.0;
    }
    const auto cos_v = clamp_cosine(dot(lhs, rhs) / (nl * nr));
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
    if (!finite_vec3(lhs) || !finite_vec3(rhs)) {
        return false;
    }
    const auto angle = vec_angle_between(lhs, rhs);
    const auto tol = std::max(angular_tolerance, 0.0);
    return angle <= tol || std::abs(angle - std::acos(-1.0)) <= tol;
}

bool vec_orthogonal_tol(const Vec3& lhs, const Vec3& rhs, Scalar angular_tolerance) {
    if (!finite_vec3(lhs) || !finite_vec3(rhs)) {
        return false;
    }
    const auto angle = vec_angle_between(lhs, rhs);
    const auto tol = std::max(angular_tolerance, 0.0);
    return std::abs(angle - (std::acos(-1.0) * 0.5)) <= tol;
}

bool bbox_intersects_tol(const BoundingBox& lhs, const BoundingBox& rhs, Scalar tolerance) {
    if (!valid_bbox(lhs) || !valid_bbox(rhs)) {
        return false;
    }
    const auto tol = std::max(tolerance, 0.0);
    return lhs.min.x <= rhs.max.x + tol && lhs.max.x + tol >= rhs.min.x &&
           lhs.min.y <= rhs.max.y + tol && lhs.max.y + tol >= rhs.min.y &&
           lhs.min.z <= rhs.max.z + tol && lhs.max.z + tol >= rhs.min.z;
}

bool bbox_contains_tol(const BoundingBox& outer, const BoundingBox& inner, Scalar tolerance) {
    if (!valid_bbox(outer) || !valid_bbox(inner)) {
        return false;
    }
    const auto tol = std::max(tolerance, 0.0);
    return inner.min.x >= outer.min.x - tol && inner.max.x <= outer.max.x + tol &&
           inner.min.y >= outer.min.y - tol && inner.max.y <= outer.max.y + tol &&
           inner.min.z >= outer.min.z - tol && inner.max.z <= outer.max.z + tol;
}

bool point_equal_tol(const Point3& lhs, const Point3& rhs, Scalar tolerance) {
    if (!finite_point3(lhs) || !finite_point3(rhs)) {
        return false;
    }
    const auto tol = std::max(tolerance, 0.0);
    return std::abs(lhs.x - rhs.x) <= tol &&
           std::abs(lhs.y - rhs.y) <= tol &&
           std::abs(lhs.z - rhs.z) <= tol;
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
    const auto scale = std::max(model_scale, 1e-12);
    return clamp_local_tolerance(resolve_linear_tolerance(requested, policy) * scale, policy);
}

Scalar resolve_angular_tolerance_for_scale(Scalar requested, const TolerancePolicy& policy, Scalar model_scale) {
    const auto ratio = std::clamp(std::max(model_scale, 1e-12), 0.1, 10.0);
    return clamp_local_tolerance(resolve_angular_tolerance(requested, policy) * ratio, policy);
}

bool within_tolerance(Scalar lhs, Scalar rhs, Scalar tolerance) {
    return std::abs(lhs - rhs) <= std::max(tolerance, 0.0);
}

bool nearly_equal_rel_abs(Scalar lhs, Scalar rhs, Scalar abs_tol, Scalar rel_tol) {
    if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
        return false;
    }
    const auto a = std::max(abs_tol, 0.0);
    const auto r = std::max(rel_tol, 0.0);
    const auto diff = std::abs(lhs - rhs);
    const auto scale = std::max(std::abs(lhs), std::abs(rhs));
    return diff <= std::max(a, r * scale);
}

int compare_rel_abs(Scalar lhs, Scalar rhs, Scalar abs_tol, Scalar rel_tol) {
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
