#include "axiom/ops/ops_services.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <unordered_map>

#include "axiom/heal/heal_services.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/eval_graph_invalidation.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/core/topology_materialization.h"
#include "axiom/internal/math/math_internal_utils.h"
#include "axiom/internal/ops/ops_service_internal.h"

namespace axiom {

using namespace ops_internal;

namespace {

constexpr Scalar kSweepPi = 3.1415926535897932384626433832795;

Result<BodyId> modeling_input_failure(detail::KernelState& state, std::string_view code,
                                      std::string message, std::string summary, std::string_view stage) {
    auto issue = detail::make_error_issue(code, std::move(message));
    issue.stage = std::string(stage);
    return error_result<BodyId>(StatusCode::InvalidInput,
        state.create_diagnostic(std::move(summary), {std::move(issue)}));
}

// Refine the already sampled rail for scale and optional roll, keeping its
// original corners and every law key. Arc-length keys are not curve parameters.
// Work stays in temporary vectors until the complete schedule has been checked.
bool refine_sweep_scale_law(detail::BodyRecord& record, std::span<const SweepScaleStation> keys,
                            std::span<const SweepLawStation> section_keys = {}) {
    constexpr std::size_t kMaxIntervals = 4096;
    const bool framed = !record.sweep_frame_origins.empty();
    const bool closed = framed && record.sweep_frame_closed;
    std::vector<Point3> origins;
    std::vector<Vec3> u, v;
    if (framed) {
        origins = record.sweep_frame_origins;
        u = record.sweep_frame_u;
        v = record.sweep_frame_v;
        if (origins.size() != u.size() || origins.size() != v.size()) return false;
        if (closed) {
            origins.push_back(origins.front());
            u.push_back(u.front());
            v.push_back(v.front());
        }
    } else {
        origins.reserve(record.sweep_station_offsets.size());
        for (const auto& offset : record.sweep_station_offsets)
            origins.push_back(detail::add_point_vec(record.extrude_scale_center, offset));
    }
    if (origins.size() < 2 || origins.size() > kMaxIntervals + 1 || keys.size() < 2 ||
        (closed && keys.back().scale != 1.0)) return false;
    if (!section_keys.empty() && (section_keys.size() != keys.size() ||
        (closed && std::min({std::abs(section_keys.back().twist_angle),
                            std::abs(section_keys.back().twist_angle-2*kSweepPi),
                            std::abs(section_keys.back().twist_angle+2*kSweepPi)}) > 1e-10))) return false;

    std::vector<long double> distances(origins.size(), 0.0L);
    for (std::size_t i = 1; i < origins.size(); ++i) {
        const long double dx = static_cast<long double>(origins[i].x) - origins[i-1].x;
        const long double dy = static_cast<long double>(origins[i].y) - origins[i-1].y;
        const long double dz = static_cast<long double>(origins[i].z) - origins[i-1].z;
        const long double step = std::hypot(dx, dy, dz);
        distances[i] = distances[i-1] + step;
        if (!std::isfinite(step) || !(step > 0) || !std::isfinite(distances[i]) ||
            !(distances[i] > distances[i-1])) return false;
    }
    const long double total_length = distances.back();
    if (!(total_length > 0) || total_length > std::numeric_limits<Scalar>::max()) return false;
    std::vector<Scalar> rail_fractions;
    rail_fractions.reserve(origins.size());
    for (const auto distance : distances) rail_fractions.push_back(static_cast<Scalar>(distance / total_length));
    rail_fractions.front() = 0.0;
    rail_fractions.back() = 1.0;
    for (std::size_t i = 1; i < rail_fractions.size(); ++i)
        if (!(rail_fractions[i] > rail_fractions[i-1])) return false;

    struct Knot { Scalar fraction; bool key; };
    std::vector<Knot> knots;
    knots.reserve(rail_fractions.size() + keys.size());
    for (const auto fraction : rail_fractions) knots.push_back({fraction, false});
    for (const auto& key : keys) knots.push_back({key.fraction, true});
    std::sort(knots.begin(), knots.end(), [](const Knot& a, const Knot& b) {
        return a.fraction < b.fraction || (a.fraction == b.fraction && a.key > b.key);
    });
    // A caller key near an existing sampled station owns the fraction. This
    // removes floating-point duplicates, never coalesces two distinct law keys,
    // and preserves the original rail position/frame at numerical coincidences.
    constexpr Scalar kFractionResolution = 64 * std::numeric_limits<Scalar>::epsilon();
    std::vector<Knot> merged;
    for (const auto& knot : knots) {
        if (!merged.empty() && knot.fraction - merged.back().fraction <= kFractionResolution) {
            if (knot.key && merged.back().key && knot.fraction != merged.back().fraction) return false;
            if (!knot.key && !merged.back().key && knot.fraction != merged.back().fraction) return false;
            if (knot.key) merged.back() = knot;
        } else {
            merged.push_back(knot);
        }
    }
    if (merged.size() < 2 || merged.size() > kMaxIntervals + 1) return false;
    const auto scale_at = [&](Scalar fraction) {
        const auto it = std::lower_bound(keys.begin(), keys.end(), fraction,
            [](const SweepScaleStation& key, Scalar value) { return key.fraction < value; });
        if (it == keys.begin()) return keys.front().scale;
        if (it == keys.end()) return keys.back().scale;
        if (it->fraction == fraction) return it->scale;
        const auto& a = *(it - 1);
        const long double t = (static_cast<long double>(fraction) - a.fraction) /
                              (static_cast<long double>(it->fraction) - a.fraction);
        return static_cast<Scalar>((1-t) * a.scale + t * it->scale);
    };
    const auto angle_at = [&](Scalar fraction) {
        if (section_keys.empty()) return Scalar(0);
        const auto it = std::lower_bound(section_keys.begin(),section_keys.end(),fraction,
            [](const SweepLawStation& key, Scalar value) { return key.fraction < value; });
        if (it == section_keys.begin()) return section_keys.front().twist_angle;
        if (it == section_keys.end()) return section_keys.back().twist_angle;
        if (it->fraction == fraction) return it->twist_angle;
        const auto& a = *(it-1);
        const long double t = (static_cast<long double>(fraction)-a.fraction) /
                              (static_cast<long double>(it->fraction)-a.fraction);
        return static_cast<Scalar>((1-t)*a.twist_angle+t*it->twist_angle);
    };
    std::vector<Scalar> fractions {merged.front().fraction};
    std::vector<Scalar> scales {keys.front().scale};
    std::vector<Scalar> angles {Scalar(0)};
    for (std::size_t i = 1; i < merged.size(); ++i) {
        const Scalar a = scale_at(merged[i-1].fraction), b = scale_at(merged[i].fraction);
        const Scalar angle_a = angle_at(merged[i-1].fraction), angle_b = angle_at(merged[i].fraction);
        const long double steps = std::max({1.0L, std::ceil(
            std::abs(static_cast<long double>(b) - a) / (0.25L * std::min(a,b))),
            static_cast<long double>(std::ceil(std::abs(angle_b-angle_a)/(kSweepPi/24)))});
        if (!std::isfinite(steps) || steps > static_cast<long double>(kMaxIntervals + 1 - fractions.size()))
            return false;
        const auto count = static_cast<std::size_t>(steps);
        for (std::size_t j = 1; j <= count; ++j) {
            const long double t = static_cast<long double>(j) / count;
            const Scalar fraction = j == count ? merged[i].fraction : static_cast<Scalar>(
                (1-t) * merged[i-1].fraction + t * merged[i].fraction);
            const Scalar section_scale = j == count ? b : static_cast<Scalar>((1-t) * a + t * b);
            if (!(fraction > fractions.back()) || !std::isfinite(section_scale) || !(section_scale > 0))
                return false;
            fractions.push_back(fraction);
            scales.push_back(section_scale);
            angles.push_back(j == count ? angle_b : static_cast<Scalar>((1-t)*angle_a+t*angle_b));
        }
    }

    std::vector<Point3> refined_origins;
    std::vector<Vec3> refined_u, refined_v;
    refined_origins.reserve(fractions.size());
    if (framed) { refined_u.reserve(fractions.size()); refined_v.reserve(fractions.size()); }
    std::size_t segment = 0;
    const auto rotate_vector = [](const Vec3& vector, const Vec3& axis, Scalar angle) -> Vec3 {
        const Scalar cosine = std::cos(angle), sine = std::sin(angle);
        const Vec3 first = detail::scale(vector, cosine);
        const Vec3 second = detail::scale(detail::cross(axis, vector), sine);
        const Vec3 third = detail::scale(axis, detail::dot(axis, vector) * (1-cosine));
        return {first.x+second.x+third.x, first.y+second.y+third.y, first.z+second.z+third.z};
    };
    for (const Scalar fraction : fractions) {
        while (segment + 2 < rail_fractions.size() && fraction > rail_fractions[segment+1]) ++segment;
        const Scalar begin = rail_fractions[segment], end = rail_fractions[segment+1];
        Scalar t = (fraction-begin) / (end-begin);
        if (std::abs(fraction-begin) <= kFractionResolution) t = 0;
        if (std::abs(fraction-end) <= kFractionResolution) t = 1;
        if (!std::isfinite(t) || t < 0 || t > 1) return false;
        const auto& a = origins[segment];
        const auto& b = origins[segment+1];
        const Point3 origin = t == 0 ? a : t == 1 ? b : Point3 {
            static_cast<Scalar>((1-static_cast<long double>(t))*a.x + static_cast<long double>(t)*b.x),
            static_cast<Scalar>((1-static_cast<long double>(t))*a.y + static_cast<long double>(t)*b.y),
            static_cast<Scalar>((1-static_cast<long double>(t))*a.z + static_cast<long double>(t)*b.z)};
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) return false;
        if (!refined_origins.empty() && detail::norm(detail::subtract(origin, refined_origins.back())) <= 1e-14)
            return false;
        refined_origins.push_back(origin);
        if (!framed) continue;
        if (t == 0 || t == 1) {
            const std::size_t index = segment + (t == 1 ? 1 : 0);
            refined_u.push_back(u[index]);
            refined_v.push_back(v[index]);
            continue;
        }
        // Interpolate the minimal rotation of the tangent, then the remaining
        // axial frame correction. This preserves orthonormality and both endpoint
        // frames, including the holonomy correction at a periodic seam.
        const Vec3 ta = detail::normalize(detail::cross(u[segment], v[segment]));
        const Vec3 tb = detail::normalize(detail::cross(u[segment+1], v[segment+1]));
        const Vec3 cross_t = detail::cross(ta,tb);
        const Scalar sine = detail::norm(cross_t);
        const Scalar cosine = std::clamp(detail::dot(ta,tb), Scalar(-1), Scalar(1));
        if (!std::isfinite(sine) || !std::isfinite(cosine) || cosine <= 0.5) return false;
        Vec3 tangent = ta, axis_u = u[segment], terminal_u = axis_u;
        if (sine > 1e-14) {
            const Vec3 axis = detail::scale(cross_t,1/sine);
            const Scalar angle = std::atan2(sine,cosine);
            tangent = rotate_vector(ta,axis,t*angle);
            axis_u = rotate_vector(axis_u,axis,t*angle);
            terminal_u = rotate_vector(terminal_u,axis,angle);
        }
        const Scalar residual = std::atan2(detail::dot(tb,detail::cross(terminal_u,u[segment+1])),
                                           detail::dot(terminal_u,u[segment+1]));
        axis_u = detail::normalize(rotate_vector(axis_u,tangent,t*residual));
        const Vec3 axis_v = detail::normalize(detail::cross(tangent,axis_u));
        if (!std::isfinite(residual) || detail::norm(axis_u) <= 1e-14 || detail::norm(axis_v) <= 1e-14)
            return false;
        refined_u.push_back(axis_u);
        refined_v.push_back(axis_v);
    }
    if (closed) {
        refined_origins.pop_back();
        refined_u.pop_back();
        refined_v.pop_back();
        scales.pop_back();
    }
    if (framed) {
        record.sweep_frame_origins = std::move(refined_origins);
        record.sweep_frame_u = std::move(refined_u);
        record.sweep_frame_v = std::move(refined_v);
        record.sweep_frame_scales = std::move(scales);
        if (!section_keys.empty()) record.sweep_frame_angles = std::move(angles);
    } else {
        record.sweep_station_offsets.clear();
        for (const auto& origin : refined_origins)
            record.sweep_station_offsets.push_back(detail::subtract(origin, record.extrude_scale_center));
        record.sweep_station_scales = std::move(scales);
        if (!section_keys.empty()) record.sweep_station_angles = std::move(angles);
    }
    record.b = static_cast<Scalar>(total_length);
    record.sweep_scale_law = true;
    return true;
}

Scalar sweep_segment_distance_squared(const Point3& p0, const Point3& p1,
                                      const Point3& q0, const Point3& q1) {
    // Closest points of two bounded 3-D segments. Keeping this local to sweep
    // validation avoids broadening MathCore with an Ops-specific conservative gate.
    const Vec3 u = detail::subtract(p1, p0);
    const Vec3 v = detail::subtract(q1, q0);
    const Vec3 w = detail::subtract(p0, q0);
    const Scalar a = detail::dot(u, u);
    const Scalar b = detail::dot(u, v);
    const Scalar c = detail::dot(v, v);
    const Scalar d = detail::dot(u, w);
    const Scalar e = detail::dot(v, w);
    if (!(a > 0.0) || !(c > 0.0) || !std::isfinite(a) || !std::isfinite(c)) {
        return std::numeric_limits<Scalar>::infinity();
    }
    const Scalar denominator = a * c - b * b;
    Scalar numerator_s = 0.0, denominator_s = denominator;
    Scalar numerator_t = 0.0, denominator_t = denominator;
    const Scalar parallel_tolerance = Scalar(64) * std::numeric_limits<Scalar>::epsilon() * a * c;
    if (denominator <= parallel_tolerance) {
        numerator_s = 0.0;
        denominator_s = 1.0;
        numerator_t = e;
        denominator_t = c;
    } else {
        numerator_s = b * e - c * d;
        numerator_t = a * e - b * d;
        if (numerator_s < 0.0) {
            numerator_s = 0.0;
            numerator_t = e;
            denominator_t = c;
        } else if (numerator_s > denominator_s) {
            numerator_s = denominator_s;
            numerator_t = e + b;
            denominator_t = c;
        }
    }
    if (numerator_t < 0.0) {
        numerator_t = 0.0;
        if (-d < 0.0) {
            numerator_s = 0.0;
        } else if (-d > a) {
            numerator_s = denominator_s;
        } else {
            numerator_s = -d;
            denominator_s = a;
        }
    } else if (numerator_t > denominator_t) {
        numerator_t = denominator_t;
        const Scalar projection = -d + b;
        if (projection < 0.0) {
            numerator_s = 0.0;
        } else if (projection > a) {
            numerator_s = denominator_s;
        } else {
            numerator_s = projection;
            denominator_s = a;
        }
    }
    const Scalar s = std::abs(numerator_s) <= parallel_tolerance ? 0.0 : numerator_s / denominator_s;
    const Scalar t = std::abs(numerator_t) <= parallel_tolerance ? 0.0 : numerator_t / denominator_t;
    const Vec3 delta {w.x + s * u.x - t * v.x,
                      w.y + s * u.y - t * v.y,
                      w.z + s * u.z - t * v.z};
    const Scalar result = detail::dot(delta, delta);
    return std::isfinite(result) && result >= 0.0 ? result : std::numeric_limits<Scalar>::infinity();
}

Result<OpReport> boolean_op_fail_staged(std::shared_ptr<detail::KernelState> state,
                                        bool diagnostics,
                                        BooleanOp op,
                                        StatusCode st,
                                        std::string_view code,
                                        std::string message,
                                        std::string summary,
                                        BodyId lhs,
                                        BodyId rhs,
                                        const BooleanPrepStats* prep) {
    const DiagnosticId diag = state->create_diagnostic(std::move(summary));
    if (diagnostics) {
        append_boolean_stage_issue(*state, diag, diag_codes::kBoolStageCandidates,
                                   "布尔早期退出：在候选/包围盒关系检查阶段已中止，未生成结果体",
                                   {lhs.value, rhs.value});
        if (prep != nullptr) {
            append_boolean_prep_candidate_issue(*state, diag, lhs, rhs, *prep);
        }
    }
    auto issue = detail::make_error_issue(code, std::move(message), {lhs.value, rhs.value});
    set_boolean_diagnostic_stage(issue, code);
    issue.numeric_evidence = {
        {"operation", static_cast<Scalar>(op), "enum"},
        {"lhs_exists", detail::has_body(*state, lhs) ? 1.0 : 0.0, "bool"},
        {"rhs_exists", detail::has_body(*state, rhs) ? 1.0 : 0.0, "bool"},
        {"diagnostics_enabled", diagnostics ? 1.0 : 0.0, "bool"},
    };
    if (prep != nullptr) {
        issue.numeric_evidence.push_back(
            {"lhs_regions", static_cast<Scalar>(prep->lhs_regions), "count"});
        issue.numeric_evidence.push_back(
            {"rhs_regions", static_cast<Scalar>(prep->rhs_regions), "count"});
        issue.numeric_evidence.push_back(
            {"overlap_candidates", static_cast<Scalar>(prep->overlap_candidates), "count"});
        issue.numeric_evidence.push_back(
            {"overlap_volume_sum", prep->overlap_volume_sum, "model_unit^3"});
    }
    state->append_diagnostic_issue(diag, std::move(issue));
    return error_result<OpReport>(st, diag);
}

Result<OpReport> op_report_error_with_stage(detail::KernelState& st, StatusCode code_status,
                                            std::string_view err_code, std::string message, std::string summary,
                                            std::string_view stage, std::span<const std::uint64_t> related) {
    Issue issue;
    if (related.empty()) {
        issue = detail::make_error_issue(err_code, std::move(message));
    } else {
        issue = detail::make_error_issue(err_code, std::move(message),
                                         std::vector<std::uint64_t>(related.begin(), related.end()));
    }
    issue.stage = std::string(stage);
    const DiagnosticId diag = st.create_diagnostic(std::move(summary), {std::move(issue)});
    return error_result<OpReport>(code_status, diag);
}

Result<void> boolean_prep_export_fail(detail::KernelState& state,
                                      StatusCode status,
                                      std::string_view code,
                                      std::string message,
                                      BodyId lhs,
                                      BodyId rhs,
                                      std::string_view path,
                                      std::string_view stage,
                                      const BooleanPrepStats* stats) {
    auto issue = detail::make_error_issue(code, std::move(message), {lhs.value, rhs.value});
    issue.stage = std::string(stage);
    issue.numeric_evidence = {
        {"lhs_exists", detail::has_body(state, lhs) ? 1.0 : 0.0, "bool"},
        {"rhs_exists", detail::has_body(state, rhs) ? 1.0 : 0.0, "bool"},
        {"path_empty", path.empty() ? 1.0 : 0.0, "bool"},
        {"path_length", static_cast<Scalar>(path.size()), "byte"},
    };
    if (stats != nullptr) {
        issue.numeric_evidence.push_back(
            {"lhs_regions", static_cast<Scalar>(stats->lhs_regions), "count"});
        issue.numeric_evidence.push_back(
            {"rhs_regions", static_cast<Scalar>(stats->rhs_regions), "count"});
        issue.numeric_evidence.push_back(
            {"overlap_candidates", static_cast<Scalar>(stats->overlap_candidates), "count"});
        issue.numeric_evidence.push_back(
            {"overlap_volume_sum", stats->overlap_volume_sum, "model_unit^3"});
    }
    const auto diagnostic = state.create_diagnostic(
        "布尔预处理统计导出失败", {std::move(issue)});
    return error_void(status, diagnostic);
}

}  // namespace

PrimitiveService::PrimitiveService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<BodyId> PrimitiveService::box(const Point3& origin, Scalar dx, Scalar dy, Scalar dz) {
    if (dx <= 0.0 || dy <= 0.0 || dz <= 0.0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "盒体创建失败：边长必须大于 0", "盒体创建失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Box;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "box";
    record.origin = origin;
    record.a = dx;
    record.b = dy;
    record.c = dz;
    record.bbox = box_bbox(origin, dx, dy, dz);
    return ok_result(make_body(state_, record, "已创建盒体"), state_->create_diagnostic("已创建盒体"));
}

Result<BodyId> PrimitiveService::wedge(const Point3& origin, Scalar dx, Scalar dy, Scalar dz) {
    if (dx <= 0.0 || dy <= 0.0 || dz <= 0.0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "楔体创建失败：尺寸必须大于 0", "楔体创建失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Wedge;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "wedge";
    record.origin = origin;
    record.a = dx;
    record.b = dy;
    record.c = dz;
    record.bbox = box_bbox(origin, dx, dy, dz);
    return ok_result(make_body(state_, record, "已创建楔体"), state_->create_diagnostic("已创建楔体"));
}

Result<BodyId> PrimitiveService::sphere(const Point3& center, Scalar radius) {
    if (radius <= 0.0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "球体创建失败：半径必须大于 0", "球体创建失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sphere;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "sphere";
    record.origin = center;
    record.a = radius;
    record.bbox = detail::bbox_from_center_radius(center, radius, radius, radius);
    return ok_result(make_body(state_, record, "已创建球体"), state_->create_diagnostic("已创建球体"));
}

Result<BodyId> PrimitiveService::cylinder(const Point3& center, const Vec3& axis, Scalar radius, Scalar height) {
    if (radius <= 0.0 || height <= 0.0 || !valid_axis(axis)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "圆柱体创建失败：轴向量必须有效且半径、高度必须大于 0", "圆柱体创建失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Cylinder;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "cylinder";
    record.origin = center;
    record.axis = detail::normalize(axis);
    record.a = radius;
    record.b = height;
    record.bbox = cylinder_bbox(center, record.axis, radius, height);
    return ok_result(make_body(state_, record, "已创建圆柱体"), state_->create_diagnostic("已创建圆柱体"));
}

Result<BodyId> PrimitiveService::cone(const Point3& apex, const Vec3& axis, Scalar semi_angle, Scalar height) {
    if (height <= 0.0 || semi_angle <= 0.0 || !valid_axis(axis)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "圆锥体创建失败：轴向量必须有效，半角和高度必须大于 0", "圆锥体创建失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Cone;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "cone";
    record.origin = apex;
    record.axis = detail::normalize(axis);
    record.a = semi_angle;
    record.b = height;
    record.bbox = cone_bbox(apex, record.axis, semi_angle, height);
    return ok_result(make_body(state_, record, "已创建圆锥体"), state_->create_diagnostic("已创建圆锥体"));
}

Result<BodyId> PrimitiveService::torus(const Point3& center, const Vec3& axis, Scalar major_r, Scalar minor_r) {
    if (major_r <= 0.0 || minor_r <= 0.0 || major_r <= minor_r || !valid_axis(axis)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "环体创建失败：轴向量必须有效，且主半径必须大于副半径并同时大于 0", "环体创建失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Torus;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "torus";
    record.origin = center;
    record.axis = detail::normalize(axis);
    record.a = major_r;
    record.b = minor_r;
    record.bbox = detail::bbox_from_center_radius(center, major_r + minor_r, major_r + minor_r, minor_r);
    return ok_result(make_body(state_, record, "已创建环体"), state_->create_diagnostic("已创建环体"));
}

SweepService::SweepService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<BodyId> SweepService::extrude(const ProfileRef& profile, const Vec3& direction, Scalar distance) {
    if (profile.label.empty() || (profile.polygon_xyz.empty() && !profile.holes_xyz.empty()) ||
        !std::isfinite(distance) || distance <= 0.0 ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
        !valid_axis(direction)) {
        return modeling_input_failure(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "拉伸失败：轮廓不能为空，方向必须有效且距离必须大于 0", "拉伸失败", "extrude.input_gate");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "extrude:" + profile.label;
    const auto dir = detail::normalize(direction);
    record.axis = dir;
    record.b = distance;
    if (!profile.polygon_xyz.empty()) {
        if (profile.polygon_xyz.size() < 3) {
            return modeling_input_failure(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "拉伸失败：polygon 轮廓点数不足（至少 3 个点）", "拉伸失败", "extrude.input_gate");
        }
        const auto reject_profile = [&](const char* message) {
            return modeling_input_failure(
                *state_, diag_codes::kCoreParameterOutOfRange, message, "拉伸失败", "extrude.input_gate");
        };
        const auto& poly = profile.polygon_xyz;
        for (const auto& p : poly) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
                return reject_profile("拉伸失败：polygon 轮廓坐标必须为有限值");
            }
        }
        const auto raw_normal = detail::newell_normal_unnormalized_poly(poly);
        const auto normal_length = detail::norm(raw_normal);
        if (!std::isfinite(normal_length) || normal_length <= 1e-14) {
            return reject_profile("拉伸失败：polygon 轮廓面积退化");
        }
        const auto normal = detail::scale(raw_normal, 1.0 / normal_length);
        const auto plane_tol = std::max(Scalar(1e-7), state_->config.tolerance.linear * Scalar(100.0));
        for (const auto& p : poly) {
            const auto offset = detail::subtract(p, poly.front());
            if (std::abs(detail::dot(normal, offset)) > plane_tol) {
                return reject_profile("拉伸失败：polygon 轮廓不共面");
            }
        }
        const auto alignment = std::abs(detail::dot(normal, dir));
        const auto volume = normal_length * 0.5 * alignment * distance;
        if (!std::isfinite(volume) || alignment < 1e-6 || !(volume > 1e-18)) {
            return reject_profile("拉伸失败：polygon 轮廓与拉伸方向无法形成有效体积");
        }
        // Simplicity and cap triangulation are checked by the prism materializer
        // before any model IDs are allocated; concave profiles are supported.
        BoundingBox bbox {};
        auto extend = [&](const Point3& p) {
            if (!bbox.is_valid) {
                bbox.min = p;
                bbox.max = p;
                bbox.is_valid = true;
                return;
            }
            bbox.min.x = std::min(bbox.min.x, p.x);
            bbox.min.y = std::min(bbox.min.y, p.y);
            bbox.min.z = std::min(bbox.min.z, p.z);
            bbox.max.x = std::max(bbox.max.x, p.x);
            bbox.max.y = std::max(bbox.max.y, p.y);
            bbox.max.z = std::max(bbox.max.z, p.z);
        };
        const auto displacement = detail::scale(dir, distance);
        for (const auto& p : profile.polygon_xyz) {
            const auto top = detail::add_point_vec(p, displacement);
            if (!std::isfinite(top.x) || !std::isfinite(top.y) || !std::isfinite(top.z)) {
                return reject_profile("拉伸失败：polygon 拉伸坐标超出有限范围");
            }
            extend(p);
            extend(top);
        }
        if (!bbox.is_valid) {
            return modeling_input_failure(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "拉伸失败：polygon 轮廓点非法，无法形成有效包围盒", "拉伸失败", "extrude.input_gate");
        }
        record.bbox = bbox;
        // `BodyKind::Sweep` 的 `record.a`：多边形拉伸棱柱体积缓存（供 `mass_properties` 非纯 bbox 口径）。
        const auto& v0p = profile.polygon_xyz.front();
        Vec3 accn {0.0, 0.0, 0.0};
        const auto nv = profile.polygon_xyz.size();
        for (std::size_t i = 0; i < nv; ++i) {
            const auto& vi = profile.polygon_xyz[i];
            const auto& vj = profile.polygon_xyz[(i + 1) % nv];
            const auto ei = detail::subtract(vi, v0p);
            const auto ej = detail::subtract(vj, v0p);
            const auto cr = detail::cross(ei, ej);
            accn = Vec3 {accn.x + cr.x, accn.y + cr.y, accn.z + cr.z};
        }
        const auto acc_len = detail::norm(accn);
        const auto poly_area = 0.5 * acc_len;
        if (poly_area > 1e-18 && acc_len > 1e-18) {
            const auto n_unit = detail::scale(accn, 1.0 / acc_len);
            const auto h_eff = std::abs(detail::dot(n_unit, dir)) * distance;
            record.a = poly_area * h_eff;
            const Vec3 D = detail::scale(dir, distance);
            Scalar lateral = 0.0;
            for (std::size_t i = 0; i < nv; ++i) {
                const auto& vi = profile.polygon_xyz[i];
                const auto& vj = profile.polygon_xyz[(i + 1) % nv];
                const auto dv = detail::subtract(vj, vi);
                lateral += detail::norm(detail::cross(dv, D));
            }
            record.extrude_poly_cap_area = poly_area;
            record.extrude_lateral_area = lateral;
            Point3 csum {0.0, 0.0, 0.0};
            Scalar wsum = 0.0;
            for (std::size_t i = 1; i + 1 < nv; ++i) {
                const auto& vi = profile.polygon_xyz[i];
                const auto& vj = profile.polygon_xyz[i + 1];
                const auto e1 = detail::subtract(vi, v0p);
                const auto e2 = detail::subtract(vj, v0p);
                const auto cp = detail::cross(e1, e2);
                const auto ta = 0.5 * detail::dot(cp, n_unit);
                if (std::abs(ta) <= 1e-30) {
                    continue;
                }
                csum.x += (v0p.x + vi.x + vj.x) * ta / 3.0;
                csum.y += (v0p.y + vi.y + vj.y) * ta / 3.0;
                csum.z += (v0p.z + vi.z + vj.z) * ta / 3.0;
                wsum += ta;
            }
            if (wsum > 1e-30) {
                const Point3 c_base {csum.x / wsum, csum.y / wsum, csum.z / wsum};
                record.extrude_mass_centroid = detail::add_point_vec(c_base, detail::scale(D, 0.5));
            }
        }
        record.extrude_profile_xyz = profile.polygon_xyz;
        record.extrude_holes_xyz = profile.holes_xyz;
    } else {
        const auto p0 = Point3{0.0, 0.0, 0.0};
        const auto p1 = detail::add_point_vec(p0, detail::scale(dir, distance));
        // 占位轮廓约定：原点附近 1×1 单位正方形截面，沿 `dir` 拉伸 `distance`（与 bbox  padding 一致）。
        record.a = distance;
        record.extrude_poly_cap_area = 1.0;
        record.extrude_lateral_area = 4.0 * distance;
        record.extrude_mass_centroid = detail::add_point_vec(p0, detail::scale(dir, distance * 0.5));
        // minimal profile extent: unit square around origin
        const auto minx = std::min(p0.x, p1.x) - 0.5;
        const auto maxx = std::max(p0.x, p1.x) + 0.5;
        const auto miny = std::min(p0.y, p1.y) - 0.5;
        const auto maxy = std::max(p0.y, p1.y) + 0.5;
        const auto minz = std::min(p0.z, p1.z) - 0.5;
        const auto maxz = std::max(p0.z, p1.z) + 0.5;
        record.bbox = detail::make_bbox({minx, miny, minz}, {maxx, maxy, maxz});
    }
    const auto body = make_body(state_, record, "已完成拉伸");
    if (body.value == 0) {
        return modeling_input_failure(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "拉伸失败：polygon 轮廓无法物化为有效棱柱", "拉伸失败", "extrude.materialization");
    }
    return ok_result(body, state_->create_diagnostic("已完成拉伸"));
}

Result<BodyId> SweepService::extrude_scaled(const ProfileRef& profile, const Vec3& direction, Scalar distance,
                                          const Point3& center, Scalar end_scale) {
    const auto length = std::hypot(direction.x, direction.y, direction.z);
    if (profile.label.empty() || profile.polygon_xyz.size() < 3 ||
        !std::isfinite(length) || length <= 1e-14 || !std::isfinite(distance) || distance <= 0.0 ||
        !std::isfinite(end_scale) || end_scale < 0.0 ||
        (end_scale == 0.0 && !profile.holes_xyz.empty()) ||
        !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "变截面拉伸失败：须有显式轮廓、有限缩放中心、有效方向、有限正距离和非负比例；尖顶不支持带孔轮廓", "变截面拉伸失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "extrude:scaled:" + profile.label;
    record.axis = detail::scale(direction, 1.0 / length);
    record.b = distance;
    record.extrude_profile_xyz = profile.polygon_xyz;
    record.extrude_holes_xyz = profile.holes_xyz;
    record.extrude_end_scale = end_scale;
    record.extrude_scale_center = center;
    const auto normal = detail::newell_normal_unnormalized_poly(profile.polygon_xyz);
    const auto normal_length = detail::norm(normal);
    const auto plane_tol = std::max(Scalar(1e-7), state_->config.tolerance.linear * Scalar(100.0));
    const auto center_offset = detail::dot(detail::scale(normal, 1.0 / std::max(normal_length, Scalar(1e-14))),
                                           detail::subtract(center, profile.polygon_xyz.front()));
    if (!std::isfinite(normal_length) || normal_length <= 1e-14 ||
        !std::isfinite(center_offset) || std::abs(center_offset) > plane_tol) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "变截面拉伸失败：轮廓退化或缩放中心不在轮廓平面内", "变截面拉伸失败");
    }
    // The shared materializer checks both sections and integrates the actual closed
    // polyhedron before allocating any geometry, topology or model ID.
    record.bbox = detail::make_bbox(profile.polygon_xyz.front(), profile.polygon_xyz.front());
    const auto body = make_body(state_, std::move(record), "已完成等比变截面拉伸");
    if (body.value == 0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "变截面拉伸失败：轮廓、方向或缩放后的截面无法形成有效闭壳", "变截面拉伸失败");
    }
    return ok_result(body, state_->create_diagnostic("已完成等比变截面拉伸"));
}

Result<BodyId> SweepService::extrude_twisted(const ProfileRef& profile, const Vec3& direction, Scalar distance,
                                            const Point3& center, Scalar twist_angle) {
    constexpr Scalar kPi = 3.1415926535897932384626433832795;
    constexpr Scalar kTwoPi = 2.0 * kPi;
    constexpr Scalar kMaxStationAngle = kPi / 24.0;
    const Scalar direction_length = std::hypot(direction.x, direction.y, direction.z);
    if (profile.label.empty() || profile.polygon_xyz.size() < 3 ||
        !std::isfinite(direction_length) || direction_length <= 1e-14 ||
        !std::isfinite(distance) || distance <= 0.0 ||
        !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
        !std::isfinite(twist_angle) || std::abs(twist_angle) > kTwoPi + 1e-10) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "扭转拉伸失败：须有显式轮廓、有限平面内中心、有效方向、有限正距离，且扭角须位于 [-2π, 2π]",
            "扭转拉伸失败");
    }

    const Vec3 axis = detail::scale(direction, 1.0 / direction_length);
    const Vec3 raw_normal = detail::newell_normal_unnormalized_poly(profile.polygon_xyz);
    const Scalar normal_length = detail::norm(raw_normal);
    const Scalar plane_tol = std::max(Scalar(1e-7), state_->config.tolerance.linear * Scalar(100.0));
    if (!std::isfinite(normal_length) || normal_length <= 1e-14) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "扭转拉伸失败：轮廓面积退化", "扭转拉伸失败");
    }
    const Vec3 normal = detail::scale(raw_normal, 1.0 / normal_length);
    const Scalar center_offset = detail::dot(normal, detail::subtract(center, profile.polygon_xyz.front()));
    const Scalar axis_alignment = std::abs(detail::dot(normal, axis));
    if (!std::isfinite(center_offset) || std::abs(center_offset) > plane_tol ||
        !std::isfinite(axis_alignment) || axis_alignment < 1.0 - 1e-10) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "扭转拉伸失败：中心必须位于轮廓平面内，且方向必须垂直于轮廓平面", "扭转拉伸失败");
    }
    if (std::abs(twist_angle) <= 1e-14) {
        return extrude(profile, axis, distance);
    }

    const std::size_t segment_count = static_cast<std::size_t>(
        std::max(Scalar(1.0), std::ceil(std::abs(twist_angle) / kMaxStationAngle)));
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "extrude:twisted:" + profile.label;
    record.axis = axis;
    record.b = distance;
    record.extrude_profile_xyz = profile.polygon_xyz;
    record.extrude_holes_xyz = profile.holes_xyz;
    record.extrude_scale_center = center;
    record.sweep_station_offsets.reserve(segment_count + 1);
    record.sweep_station_angles.reserve(segment_count + 1);
    for (std::size_t i = 0; i <= segment_count; ++i) {
        const Scalar fraction = static_cast<Scalar>(i) / static_cast<Scalar>(segment_count);
        record.sweep_station_offsets.push_back(detail::scale(axis, distance * fraction));
        record.sweep_station_angles.push_back(twist_angle * fraction);
    }
    // The shared materializer validates every sampled section, triangle and mass
    // integral before allocating geometry/topology/model IDs.
    record.bbox = detail::make_bbox(profile.polygon_xyz.front(), profile.polygon_xyz.front());
    const auto body = make_body(state_, std::move(record), "已完成扭转拉伸");
    if (body.value == 0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "扭转拉伸失败：轮廓、孔洞或采样侧壁发生数值退化，无法形成有效闭壳", "扭转拉伸失败");
    }
    return ok_result(body, state_->create_diagnostic("已完成扭转拉伸"));
}

Result<BodyId> SweepService::extrude_with_law(const ProfileRef& profile, const Vec3& direction, Scalar distance,
                                           const Point3& center, std::span<const ExtrusionLawStation> stations) {
    constexpr Scalar kTwoPi = 6.283185307179586476925286766559;
    constexpr Scalar kMaxStationAngle = kTwoPi / 48.0;
    constexpr std::size_t kMaxIntervals = 4096;
    const Scalar length = std::hypot(direction.x, direction.y, direction.z);
    if (profile.label.empty() || profile.polygon_xyz.size() < 3 ||
        !std::isfinite(length) || length <= 1e-14 || !std::isfinite(distance) || distance <= 0.0 ||
        !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
        stations.size() < 2 || stations.size() > kMaxIntervals + 1) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "截面律拉伸失败：须有显式轮廓、有限共面中心、有效方向、正距离和 2 至 4097 个截面律关键站",
            "截面律拉伸失败");
    }
    const Vec3 axis = detail::scale(direction, 1.0 / length);
    const Vec3 raw_normal = detail::newell_normal_unnormalized_poly(profile.polygon_xyz);
    const Scalar normal_length = detail::norm(raw_normal);
    if (!std::isfinite(normal_length) || normal_length <= 1e-14) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "截面律拉伸失败：轮廓面积退化", "截面律拉伸失败");
    }
    const Vec3 normal = detail::scale(raw_normal, 1.0 / normal_length);
    const Scalar plane_tol = std::max(Scalar(1e-7), state_->config.tolerance.linear * Scalar(100.0));
    const Scalar center_offset = detail::dot(normal, detail::subtract(center, profile.polygon_xyz.front()));
    const Scalar alignment = std::abs(detail::dot(normal, axis));
    if (!std::isfinite(center_offset) || std::abs(center_offset) > plane_tol ||
        !std::isfinite(alignment) || alignment < 1.0 - 1e-10) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "截面律拉伸失败：中心必须共面，方向必须垂直于轮廓平面", "截面律拉伸失败");
    }

    // Validate the entire law before creating stations. In particular, do not clamp
    // malformed endpoints or silently discard a key whose height rounds away.
    if (stations.front().fraction != 0.0 || stations.back().fraction != 1.0 ||
        stations.front().scale != 1.0 || stations.front().twist_angle != 0.0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "截面律拉伸失败：首站必须为 (0,1,0)，末站高度比例必须为 1", "截面律拉伸失败");
    }
    Scalar angular_travel = 0.0;
    std::size_t total_intervals = 0;
    std::vector<std::size_t> interval_counts;
    interval_counts.reserve(stations.size() - 1);
    for (std::size_t i = 0; i < stations.size(); ++i) {
        const auto& key = stations[i];
        if (!std::isfinite(key.fraction) || !std::isfinite(key.scale) || !(key.scale > 0.0) ||
            !std::isfinite(key.twist_angle) || key.fraction < 0.0 || key.fraction > 1.0 ||
            (i != 0 && key.fraction <= stations[i - 1].fraction)) {
            return detail::invalid_input_result<BodyId>(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "截面律拉伸失败：高度比例须严格递增且有限，比例须有限且为正，扭角须有限", "截面律拉伸失败");
        }
        if (i == 0) continue;
        const auto& previous = stations[i - 1];
        const Scalar angle_step = std::abs(key.twist_angle - previous.twist_angle);
        angular_travel += angle_step;
        const Scalar scale_step = std::abs(key.scale - previous.scale) / std::min(key.scale, previous.scale);
        const Scalar intervals = std::max({Scalar(1.0), std::ceil(angle_step / kMaxStationAngle),
                                           std::ceil(scale_step / 0.25)});
        if (!std::isfinite(angular_travel) || angular_travel > kTwoPi + 1e-10 ||
            !std::isfinite(intervals) || intervals > static_cast<Scalar>(kMaxIntervals - total_intervals)) {
            return detail::invalid_input_result<BodyId>(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "截面律拉伸失败：累计绝对扭角不得超过一周，采样区间总数不得超过 4096", "截面律拉伸失败");
        }
        const auto count = static_cast<std::size_t>(intervals);
        total_intervals += count;
        interval_counts.push_back(count);
    }

    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "extrude:law:" + profile.label;
    record.axis = axis;
    record.b = distance;
    record.extrude_profile_xyz = profile.polygon_xyz;
    record.extrude_holes_xyz = profile.holes_xyz;
    record.extrude_scale_center = center;
    record.sweep_station_offsets.reserve(total_intervals + 1);
    record.sweep_station_scales.reserve(total_intervals + 1);
    record.sweep_station_angles.reserve(total_intervals + 1);
    record.sweep_station_offsets.push_back({0,0,0});
    record.sweep_station_scales.push_back(1.0);
    record.sweep_station_angles.push_back(0.0);
    for (std::size_t i = 1; i < stations.size(); ++i) {
        const auto& a = stations[i - 1];
        const auto& b = stations[i];
        const auto count = interval_counts[i - 1];
        for (std::size_t j = 1; j <= count; ++j) {
            const Scalar t = static_cast<Scalar>(j) / static_cast<Scalar>(count);
            // Copy the exact key at each interval end; this preserves requested
            // intermediate sections and avoids accumulated interpolation error.
            const Scalar fraction = j == count ? b.fraction : a.fraction + t * (b.fraction - a.fraction);
            const Scalar section_scale = j == count ? b.scale : a.scale + t * (b.scale - a.scale);
            const Scalar angle = j == count ? b.twist_angle : a.twist_angle + t * (b.twist_angle - a.twist_angle);
            record.sweep_station_offsets.push_back(detail::scale(axis, distance * fraction));
            record.sweep_station_scales.push_back(section_scale);
            record.sweep_station_angles.push_back(angle);
        }
    }
    record.bbox = detail::make_bbox(profile.polygon_xyz.front(), profile.polygon_xyz.front());
    // The shared materializer validates rounded rings, slab walls and the complete
    // mass integral before allocating any geometry, topology or body IDs.
    const auto body = make_body(state_, std::move(record), "已完成分段截面律拉伸");
    if (body.value == 0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "截面律拉伸失败：轮廓或孔无效、采样截面数值坍塌、侧壁相交或闭壳退化", "截面律拉伸失败");
    }
    return ok_result(body, state_->create_diagnostic("已完成分段截面律拉伸"));
}

Result<BodyId> SweepService::extrude_to_plane(const ProfileRef& profile, const Vec3& direction,
                                            const Plane& end_plane) {
    const auto length = std::hypot(direction.x, direction.y, direction.z);
    const auto normal_length = std::hypot(end_plane.normal.x, end_plane.normal.y, end_plane.normal.z);
    if (profile.label.empty() || profile.polygon_xyz.size() < 3 ||
        !std::isfinite(length) || length <= 1e-14 ||
        !std::isfinite(normal_length) || normal_length <= 1e-14 ||
        !std::isfinite(end_plane.origin.x) || !std::isfinite(end_plane.origin.y) ||
        !std::isfinite(end_plane.origin.z)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "至平面拉伸失败：须有显式轮廓、有限目标平面及有效方向和法向", "至平面拉伸失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "extrude:to_plane:" + profile.label;
    record.axis = detail::scale(direction, 1.0 / length);
    record.b = 1.0;
    record.extrude_profile_xyz = profile.polygon_xyz;
    record.extrude_holes_xyz = profile.holes_xyz;
    record.extrude_end_plane = Plane {end_plane.origin, detail::scale(end_plane.normal, 1.0 / normal_length)};
    record.bbox = detail::make_bbox(profile.polygon_xyz.front(), profile.polygon_xyz.front());
    // Validate the complete projected region and closed shell before allocating IDs.
    const auto body = make_body(state_, std::move(record), "已完成至平面拉伸");
    if (body.value == 0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "至平面拉伸失败：轮廓无效、方向切向、目标平面未严格位于前方或闭壳数值退化", "至平面拉伸失败");
    }
    return ok_result(body, state_->create_diagnostic("已完成至平面拉伸"));
}

Result<BodyId> SweepService::revolve(const ProfileRef& profile, const Axis3& axis, Scalar angle) {
    constexpr Scalar kTwoPi = 6.283185307179586476925286766559;
    if (!std::isfinite(angle) || angle <= 0.0 || angle > kTwoPi + 1e-10) {
        return modeling_input_failure(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "旋转失败：旋转角须位于 (0, 2π]", "旋转失败", "revolve.input_gate");
    }
    return revolve_between(profile, axis, 0.0, angle);
}

Result<BodyId> SweepService::revolve_between(const ProfileRef& profile, const Axis3& axis,
                                             Scalar start_angle, Scalar end_angle) {
    constexpr Scalar kTwoPi = 6.283185307179586476925286766559;
    const auto axis_length = std::hypot(axis.direction.x, axis.direction.y, axis.direction.z);
    const Scalar signed_angle = end_angle - start_angle;
    const Scalar angle = std::abs(signed_angle);
    if (profile.label.empty() || (profile.polygon_xyz.empty() && !profile.holes_xyz.empty()) ||
        !std::isfinite(start_angle) || !std::isfinite(end_angle) || !std::isfinite(signed_angle) || angle <= 0.0 ||
        angle > kTwoPi + 1e-10 || !std::isfinite(axis.origin.x) ||
        !std::isfinite(axis.origin.y) || !std::isfinite(axis.origin.z) ||
        !std::isfinite(axis_length) || axis_length <= 1e-14) {
        return modeling_input_failure(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "区间旋转失败：轮廓与旋转轴必须有限有效，起止角须定义绝对值位于 (0, 2π] 的有向区间",
            "区间旋转失败", "revolve.input_gate");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "revolve:" + profile.label;
    const auto u = detail::normalize(axis.direction);
    record.axis = u;
    const auto& O = axis.origin;
    record.origin = O;

    if (!profile.polygon_xyz.empty()) {
        if (profile.polygon_xyz.size() < 3) {
            return modeling_input_failure(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "旋转失败：polygon 轮廓点数不足（至少 3 个点）", "旋转失败", "revolve.input_gate");
        }
        record.revolve_profile_xyz = profile.polygon_xyz;
        record.revolve_holes_xyz = profile.holes_xyz;
        record.revolve_full_turn = std::abs(angle - kTwoPi) <= 1e-10;
        record.revolve_start_angle = record.revolve_full_turn ? 0.0 : std::remainder(start_angle, kTwoPi);
        record.revolve_signed_angle = std::copysign(record.revolve_full_turn ? kTwoPi : angle, signed_angle);
        const auto start_cos = std::cos(record.revolve_start_angle);
        const auto start_sin = std::sin(record.revolve_start_angle);
        const auto end = record.revolve_start_angle + record.revolve_signed_angle;
        const auto end_cos = std::cos(end);
        const auto end_sin = std::sin(end);
        BoundingBox bbox {};
        auto extend_point = [&](const Point3& p) {
            if (!bbox.is_valid) {
                bbox.min = p;
                bbox.max = p;
                bbox.is_valid = true;
                return;
            }
            bbox.min.x = std::min(bbox.min.x, p.x);
            bbox.min.y = std::min(bbox.min.y, p.y);
            bbox.min.z = std::min(bbox.min.z, p.z);
            bbox.max.x = std::max(bbox.max.x, p.x);
            bbox.max.y = std::max(bbox.max.y, p.y);
            bbox.max.z = std::max(bbox.max.z, p.z);
        };
        for (const auto& p : profile.polygon_xyz) {
            extend_point(rotate_point_around_unit_axis(p, O, u, start_cos, start_sin));
            extend_point(rotate_point_around_unit_axis(p, O, u, end_cos, end_sin));
        }
        if (!bbox.is_valid) {
            return modeling_input_failure(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "旋转失败：polygon 轮廓无法形成有效包围盒", "旋转失败", "revolve.input_gate");
        }
        record.bbox = bbox;
        record.b = record.revolve_full_turn ? kTwoPi : angle;
        // Pappus：体积 = 轮廓面积 × 质心到轴距离 × 转角（弧度）。
        if (const auto pm = try_pappus_revolve_mass(std::span<const Point3>(profile.polygon_xyz.data(),
                                                                            profile.polygon_xyz.size()),
                                                    O, u, angle)) {
            record.a = pm->volume;
            record.extrude_poly_cap_area = pm->profile_area;
        }
    } else {
        // 无 polygon：沿用单位尺度占位包围盒
        record.bbox = detail::bbox_from_center_radius(axis.origin, 1.0, 1.0, 1.0);
    }
    const auto body = make_body(state_, std::move(record), "已完成旋转");
    if (body.value == 0) {
        return modeling_input_failure(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "区间旋转失败：轮廓须为有效共面多边形区域、轴须位于轮廓平面且区域位于轴的一侧；带孔区域须与轴保持间隙，无孔轮廓也可仅以一条连续边接触轴",
            "区间旋转失败", "revolve.materialization");
    }
    return ok_result(body, state_->create_diagnostic("已完成区间旋转"));
}

Result<BodyId> SweepService::sweep(const ProfileRef& profile, CurveId rail) {
    return sweep_scaled(profile, rail, 1.0);
}

Result<BodyId> SweepService::sweep_scaled(const ProfileRef& profile, CurveId rail, Scalar end_scale) {
    return sweep_impl(profile, rail, end_scale, {});
}

Result<BodyId> SweepService::sweep_with_scale_law(const ProfileRef& profile, CurveId rail,
                                                std::span<const SweepScaleStation> stations) {
    if (stations.size() < 2 || stations.size() > 4097 || profile.polygon_xyz.size() < 3 ||
        stations.front().fraction != 0 || stations.back().fraction != 1 || stations.front().scale != 1) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "截面律扫掠失败：需要显式轮廓及 2 至 4097 个关键站，首站为 (0,1)，末站弧长比例为 1",
            "截面律扫掠失败");
    }
    long double minimum_intervals = 0;
    for (std::size_t i = 0; i < stations.size(); ++i) {
        const auto& key = stations[i];
        if (!std::isfinite(key.fraction) || key.fraction < 0 || key.fraction > 1 ||
            !std::isfinite(key.scale) || !(key.scale > 0) ||
            (i != 0 && !(key.fraction > stations[i-1].fraction))) {
            return detail::invalid_input_result<BodyId>(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "截面律扫掠失败：弧长比例必须有限且严格递增，所有截面比例必须有限且严格为正",
                "截面律扫掠失败");
        }
        if (i == 0) continue;
        minimum_intervals += std::max(1.0L, std::ceil(
            std::abs(static_cast<long double>(key.scale) - stations[i-1].scale) /
            (0.25L * std::min(key.scale, stations[i-1].scale))));
        if (!std::isfinite(minimum_intervals) || minimum_intervals > 4096) {
            return detail::invalid_input_result<BodyId>(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "截面律扫掠失败：比例变化要求的采样区间超过 4096", "截面律扫掠失败");
        }
    }
    return sweep_impl(profile, rail, stations.back().scale, stations);
}

Result<BodyId> SweepService::sweep_with_law(const ProfileRef& profile, CurveId rail,
                                          std::span<const SweepLawStation> stations) {
    if (stations.size() < 2 || stations.size() > 4097 || profile.polygon_xyz.size() < 3 ||
        stations.front().fraction != 0 || stations.back().fraction != 1 ||
        stations.front().scale != 1 || stations.front().twist_angle != 0) {
        return detail::invalid_input_result<BodyId>(
            *state_,diag_codes::kCoreParameterOutOfRange,
            "联合截面律扫掠失败：需要显式轮廓和 2 至 4097 个关键站，首站为 (0,1,0)，末站弧长比例为 1",
            "联合截面律扫掠失败");
    }
    std::vector<SweepScaleStation> scales;
    scales.reserve(stations.size());
    long double travel = 0, minimum_intervals = 0;
    for (std::size_t i = 0; i < stations.size(); ++i) {
        const auto& key = stations[i];
        if (!std::isfinite(key.fraction) || key.fraction < 0 || key.fraction > 1 ||
            !std::isfinite(key.scale) || !(key.scale > 0) || !std::isfinite(key.twist_angle) ||
            (i != 0 && !(key.fraction > stations[i-1].fraction))) {
            return detail::invalid_input_result<BodyId>(
                *state_,diag_codes::kCoreParameterOutOfRange,
                "联合截面律扫掠失败：弧长比例须有限且严格递增，比例须有限且为正，扭角须有限",
                "联合截面律扫掠失败");
        }
        scales.push_back({key.fraction,key.scale});
        if (i == 0) continue;
        const auto& previous = stations[i-1];
        const long double angle_step = std::abs(static_cast<long double>(key.twist_angle)-previous.twist_angle);
        travel += angle_step;
        minimum_intervals += std::max({1.0L,static_cast<long double>(std::ceil(
            static_cast<Scalar>(angle_step)/(kSweepPi/24))),
            std::ceil(std::abs(static_cast<long double>(key.scale)-previous.scale) /
                      (0.25L*std::min(key.scale,previous.scale)))});
        if (!std::isfinite(travel) || travel > 2*kSweepPi+1e-10L ||
            !std::isfinite(minimum_intervals) || minimum_intervals > 4096) {
            return detail::invalid_input_result<BodyId>(
                *state_,diag_codes::kCoreParameterOutOfRange,
                "联合截面律扫掠失败：累计绝对扭角超过一周或联合采样要求超过 4096 区间",
                "联合截面律扫掠失败");
        }
    }
    return sweep_impl(profile,rail,stations.back().scale,scales,stations);
}

Result<BodyId> SweepService::sweep_impl(const ProfileRef& profile, CurveId rail, Scalar end_scale,
                                      std::span<const SweepScaleStation> stations,
                                      std::span<const SweepLawStation> section_stations) {
    const auto law_failure = [&](std::string_view stage, std::string message, std::size_t intervals) {
        auto issue = detail::make_error_issue(diag_codes::kCoreParameterOutOfRange,std::move(message),{rail.value});
        issue.stage = std::string(stage);
        Scalar minimum_scale = 1, maximum_scale = 1;
        for (const auto& key : stations) {
            minimum_scale = std::min(minimum_scale,key.scale);
            maximum_scale = std::max(maximum_scale,key.scale);
        }
        issue.numeric_evidence = {
            {"law_key_count",static_cast<Scalar>(stations.size()),"count"},
            {"sampled_intervals",static_cast<Scalar>(intervals),"count"},
            {"maximum_intervals",4096,"count"},
            {"minimum_scale",minimum_scale,"ratio"},
            {"maximum_scale",maximum_scale,"ratio"},
            {"maximum_scale_step_ratio",0.25,"ratio"},
            {"maximum_contact_candidates",2000000,"count"},
        };
        if (!section_stations.empty()) {
            Scalar travel = 0;
            for (std::size_t i = 1; i < section_stations.size(); ++i)
                travel += std::abs(section_stations[i].twist_angle-section_stations[i-1].twist_angle);
            issue.numeric_evidence.push_back({"absolute_twist_travel",travel,"rad"});
            issue.numeric_evidence.push_back({"maximum_twist_step",kSweepPi/24,"rad"});
            issue.numeric_evidence.push_back({"terminal_twist",section_stations.back().twist_angle,"rad"});
        }
        const DiagnosticId diagnostic = state_->create_diagnostic("截面律扫掠失败",{std::move(issue)});
        return error_result<BodyId>(StatusCode::InvalidInput,diagnostic);
    };
    if (!std::isfinite(end_scale) || !(end_scale > 0.0)) {
        return modeling_input_failure(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "变截面扫描失败：终端比例必须有限且严格为正", "变截面扫描失败", "sweep.input_gate");
    }
    if (profile.label.empty() || !detail::has_curve(*state_, rail)) {
        return modeling_input_failure(
            *state_, diag_codes::kCoreInvalidHandle,
            "扫描失败：轮廓为空或导轨曲线不存在", "扫描失败", "sweep.input_gate");
    }
    if (end_scale != 1.0 && profile.polygon_xyz.empty()) {
        return modeling_input_failure(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "变截面扫描失败：非单位比例要求显式 polygon 轮廓", "变截面扫描失败", "sweep.input_gate");
    }
    const auto& curve = state_->curves.at(rail.value);
    if (!profile.polygon_xyz.empty() || !profile.holes_xyz.empty()) {
        const bool linear = curve.kind == detail::CurveKind::LineSegment ||
                            curve.kind == detail::CurveKind::CompositePolyline;
        const bool curved = curve.kind == detail::CurveKind::Circle || curve.kind == detail::CurveKind::Ellipse ||
                            curve.kind == detail::CurveKind::Bezier ||
                            curve.kind == detail::CurveKind::BSpline || curve.kind == detail::CurveKind::Nurbs ||
                            curve.kind == detail::CurveKind::CompositeChain;
        if ((!linear && !curved) || profile.polygon_xyz.size() < 3 || (linear && curve.poles.size() < 2)) {
            return modeling_input_failure(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "扫描失败：显式 polygon 轮廓须有至少三个点，导轨须为线段、折线、整圆或受支持的样条", "扫描失败", "sweep.input_gate");
        }
        if (curved) {
            std::vector<Point3> origins;
            std::vector<Vec3> tangents;
            bool closed = false;
            if (!sample_curved_sweep_rail(*state_, curve, origins, tangents, closed)) {
                return modeling_input_failure(
                    *state_, diag_codes::kCoreParameterOutOfRange,
                    "曲线扫掠失败：导轨求值退化、具有尖点，或复合导轨接缝的位置/切向不连续", "曲线扫掠失败", "sweep.input_gate");
            }
            if (closed && end_scale != 1.0) {
                return modeling_input_failure(
                    *state_, diag_codes::kCoreParameterOutOfRange,
                    "变截面扫描失败：周期导轨的首尾截面比例必须一致", "变截面扫描失败", "sweep.input_gate");
            }
            if (closed && !section_stations.empty() &&
                std::min({std::abs(section_stations.back().twist_angle),
                          std::abs(section_stations.back().twist_angle-2*kSweepPi),
                          std::abs(section_stations.back().twist_angle+2*kSweepPi)}) > 1e-10) {
                return law_failure("sweep_law_seam",
                    "联合截面律扫掠失败：周期导轨末端扭角须为零或正负一周，不推断截面对称顶点置换",0);
            }
            const auto raw_normal = detail::newell_normal_unnormalized_poly(profile.polygon_xyz);
            const Scalar normal_length = detail::norm(raw_normal);
            const Scalar plane_tol = std::max(Scalar(1e-7), state_->config.tolerance.linear * Scalar(100.0));
            if (!std::isfinite(normal_length) || normal_length <= 1e-14) {
                return modeling_input_failure(
                    *state_, diag_codes::kCoreParameterOutOfRange,
                    "曲线扫掠失败：截面面积退化", "曲线扫掠失败", "sweep.input_gate");
            }
            const Vec3 profile_normal = detail::scale(raw_normal, 1.0 / normal_length);
            const Vec3 start_tangent = detail::normalize(tangents.front());
            const Scalar normal_alignment = std::abs(detail::dot(profile_normal, start_tangent));
            Scalar profile_radius = 0.0;
            const auto inspect_ring = [&](const std::vector<Point3>& ring) {
                if (ring.size() < 3) return false;
                for (const auto& p : ring) {
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
                        std::abs(detail::dot(start_tangent, detail::subtract(p, origins.front()))) > plane_tol) {
                        return false;
                    }
                    profile_radius = std::max(profile_radius, detail::norm(detail::subtract(p, origins.front())));
                }
                return true;
            };
            if (normal_alignment < 1.0 - 1e-6 || !inspect_ring(profile.polygon_xyz) ||
                !std::all_of(profile.holes_xyz.begin(), profile.holes_xyz.end(), inspect_ring) ||
                !std::isfinite(profile_radius) || profile_radius <= plane_tol) {
                return modeling_input_failure(
                    *state_, diag_codes::kCoreParameterOutOfRange,
                    "曲线扫掠失败：起始截面须有限、非退化、经过导轨起点且垂直于起始切向", "曲线扫掠失败", "sweep.input_gate");
            }
            Scalar maximum_scale = std::max(Scalar(1.0), end_scale);
            for (const auto& key : stations) maximum_scale = std::max(maximum_scale, key.scale);
            profile_radius *= maximum_scale;
            if (!std::isfinite(profile_radius)) {
                return modeling_input_failure(
                    *state_, diag_codes::kCoreParameterOutOfRange,
                    "变截面扫描失败：比例后的截面尺度溢出", "变截面扫描失败", "sweep.input_gate");
            }

            // Conservative tube gates: every sampled turn must have a radius of
            // curvature comfortably larger than the complete outer/hole section.
            // Distant stations must also stay separated, preventing obvious
            // self-contact without widening the global mesh SAT policy.
            Scalar rail_length = 0.0;
            const std::size_t station_count = origins.size();
            std::vector<Scalar> accumulated_length(station_count, 0.0);
            for (std::size_t i = 0; i < station_count; ++i) {
                const std::size_t next = (i + 1) % station_count;
                if (!closed && next == 0) break;
                const Scalar step = detail::norm(detail::subtract(origins[next], origins[i]));
                if (!std::isfinite(step) || step <= plane_tol) {
                    return modeling_input_failure(
                        *state_, diag_codes::kCoreParameterOutOfRange,
                        "曲线扫掠失败：采样导轨含重复或近退化站点", "曲线扫掠失败", "sweep.input_gate");
                }
                rail_length += step;
                if (next != 0) accumulated_length[next] = rail_length;
                const Scalar cosine = std::clamp(detail::dot(detail::normalize(tangents[i]),
                                                              detail::normalize(tangents[next])), -1.0, 1.0);
                if (cosine <= 0.5) {
                    return modeling_input_failure(
                        *state_, diag_codes::kCoreParameterOutOfRange,
                        "曲线扫掠失败：导轨转折过急或切向反转", "曲线扫掠失败", "sweep.input_gate");
                }
                const Scalar sine_half = std::sqrt(std::max(Scalar(0.0), (1.0 - cosine) * 0.5));
                if (sine_half > 1e-10) {
                    const std::size_t previous = i == 0 ? (closed ? station_count - 1 : 0) : i - 1;
                    const Scalar previous_step = i == 0 && !closed ? step :
                        detail::norm(detail::subtract(origins[i], origins[previous]));
                    const Scalar curvature_radius = std::min(step, previous_step) / (2.0 * sine_half);
                    if (!std::isfinite(curvature_radius) || profile_radius >= 0.35 * curvature_radius) {
                        return modeling_input_failure(
                            *state_, diag_codes::kCoreParameterOutOfRange,
                            "曲线扫掠失败：截面相对导轨曲率过大，可能折叠", "曲线扫掠失败", "sweep.input_gate");
                    }
                }
            }
            for (std::size_t i = 0; i < station_count; ++i) {
                for (std::size_t j = i + 1; j < station_count; ++j) {
                    Scalar along = accumulated_length[j] - accumulated_length[i];
                    if (closed) along = std::min(along, rail_length - along);
                    // Nearby samples on the same local tube are expected to be
                    // close in space. Compare only stations separated by enough
                    // arc length to represent distinct tube neighborhoods.
                    if (along <= 3.0 * profile_radius) continue;
                    if (detail::norm(detail::subtract(origins[i], origins[j])) <= 2.25 * profile_radius) {
                        return modeling_input_failure(
                            *state_, diag_codes::kCoreParameterOutOfRange,
                            "曲线扫掠失败：非相邻导轨区段过近，截面可能自相交", "曲线扫掠失败", "sweep.input_gate");
                    }
                }
            }
            // Point-to-point separation can miss two long sampled chords that cross
            // between their endpoints. Check nonlocal rail segments as well, using
            // arc-midpoint separation to avoid rejecting neighboring pieces of the
            // same tube. This is especially important for mixed CompositeChain rails.
            const std::size_t rail_segments = closed ? station_count : station_count - 1;
            std::vector<Scalar> segment_mid_lengths(rail_segments, 0.0);
            std::vector<Scalar> segment_lengths(rail_segments, 0.0);
            for (std::size_t i = 0; i < rail_segments; ++i) {
                const std::size_t next = (i + 1) % station_count;
                const Scalar begin = accumulated_length[i];
                const Scalar step = detail::norm(detail::subtract(origins[next], origins[i]));
                segment_mid_lengths[i] = begin + 0.5 * step;
                segment_lengths[i] = step;
            }
            const Scalar clearance_squared = 2.25 * profile_radius * 2.25 * profile_radius;
            for (std::size_t i = 0; i < rail_segments; ++i) {
                const std::size_t i_next = (i + 1) % station_count;
                for (std::size_t j = i + 1; j < rail_segments; ++j) {
                    const std::size_t j_next = (j + 1) % station_count;
                    if (j == i + 1 || (closed && i == 0 && j + 1 == rail_segments)) continue;
                    Scalar along = std::abs(segment_mid_lengths[j] - segment_mid_lengths[i]);
                    if (closed) along = std::min(along, rail_length - along);
                    // Use the arc gap between the segment intervals, not their
                    // midpoint separation. Two locally adjacent chords can have
                    // distant midpoints yet still share the same valid tube
                    // neighborhood (notably on sampled circles and ellipses).
                    const Scalar interval_gap = std::max(
                        Scalar(0.0), along - 0.5 * (segment_lengths[i] + segment_lengths[j]));
                    if (interval_gap <= 3.0 * profile_radius) continue;
                    const Scalar distance_squared = sweep_segment_distance_squared(
                        origins[i], origins[i_next], origins[j], origins[j_next]);
                    if (distance_squared <= clearance_squared) {
                        return modeling_input_failure(
                            *state_, diag_codes::kCoreParameterOutOfRange,
                            "曲线扫掠失败：非相邻导轨弦段相交或过近，截面可能自相交", "曲线扫掠失败", "sweep.input_gate");
                    }
                }
            }

            Vec3 initial_u = detail::subtract(profile.polygon_xyz.front(), origins.front());
            initial_u = detail::subtract(Point3 {initial_u.x, initial_u.y, initial_u.z},
                                         Point3 {start_tangent.x * detail::dot(initial_u, start_tangent),
                                                 start_tangent.y * detail::dot(initial_u, start_tangent),
                                                 start_tangent.z * detail::dot(initial_u, start_tangent)});
            if (detail::norm(initial_u) <= plane_tol) {
                const Vec3 reference = std::abs(start_tangent.x) < 0.8 ? Vec3 {1,0,0} : Vec3 {0,1,0};
                initial_u = detail::cross(start_tangent, reference);
            }
            initial_u = detail::normalize(initial_u);
            std::vector<Vec3> frame_u {initial_u};
            std::vector<Vec3> frame_v {detail::normalize(detail::cross(start_tangent, initial_u))};
            frame_u.reserve(station_count);
            frame_v.reserve(station_count);
            for (std::size_t i = 1; i < station_count; ++i) {
                const Vec3 previous_tangent = detail::normalize(tangents[i - 1]);
                const Vec3 tangent = detail::normalize(tangents[i]);
                const Vec3 rotation_axis = detail::cross(previous_tangent, tangent);
                const Scalar sine = detail::norm(rotation_axis);
                const Scalar cosine = std::clamp(detail::dot(previous_tangent, tangent), -1.0, 1.0);
                Vec3 transported = frame_u.back();
                if (sine > 1e-14) {
                    const Vec3 axis = detail::scale(rotation_axis, 1.0 / sine);
                    const auto first = detail::scale(transported, cosine);
                    const auto second = detail::scale(detail::cross(axis, transported), sine);
                    const auto third = detail::scale(axis, detail::dot(axis, transported) * (1.0 - cosine));
                    transported = {first.x + second.x + third.x,
                                   first.y + second.y + third.y,
                                   first.z + second.z + third.z};
                }
                const auto correction = detail::scale(tangent, -detail::dot(transported, tangent));
                transported = {transported.x + correction.x, transported.y + correction.y,
                               transported.z + correction.z};
                if (detail::norm(transported) <= 1e-12) {
                    return modeling_input_failure(
                        *state_, diag_codes::kCoreParameterOutOfRange,
                        "曲线扫掠失败：无法构造稳定的平行移动截面标架", "曲线扫掠失败", "sweep.input_gate");
                }
                transported = detail::normalize(transported);
                frame_u.push_back(transported);
                frame_v.push_back(detail::normalize(detail::cross(tangent, transported)));
            }
            if (closed) {
                // Parallel transport around a spatial loop may return with a finite
                // holonomy angle.  Transport the last frame across the seam, measure
                // that residual about the initial tangent, then distribute its inverse
                // over the sampled arc.  Station zero remains the caller's section
                // orientation while the final wall becomes twist-continuous.
                const Vec3 last_tangent = detail::normalize(tangents.back());
                const Vec3 seam_axis_raw = detail::cross(last_tangent, start_tangent);
                const Scalar seam_sine = detail::norm(seam_axis_raw);
                const Scalar seam_cosine = std::clamp(detail::dot(last_tangent, start_tangent), -1.0, 1.0);
                Vec3 seam_u = frame_u.back();
                if (seam_sine > 1e-14) {
                    const Vec3 seam_axis = detail::scale(seam_axis_raw, 1.0 / seam_sine);
                    const auto first = detail::scale(seam_u, seam_cosine);
                    const auto second = detail::scale(detail::cross(seam_axis, seam_u), seam_sine);
                    const auto third = detail::scale(
                        seam_axis, detail::dot(seam_axis, seam_u) * (1.0 - seam_cosine));
                    seam_u = detail::normalize({first.x + second.x + third.x,
                                                first.y + second.y + third.y,
                                                first.z + second.z + third.z});
                }
                const Scalar residual = std::atan2(
                    detail::dot(start_tangent, detail::cross(seam_u, frame_u.front())),
                    std::clamp(detail::dot(seam_u, frame_u.front()), -1.0, 1.0));
                const Scalar correction_length = accumulated_length.back();
                if (!std::isfinite(residual) || !(correction_length > plane_tol)) {
                    return modeling_input_failure(
                        *state_, diag_codes::kCoreParameterOutOfRange,
                        "闭合曲线扫掠失败：无法构造连续的周期截面标架", "闭合曲线扫掠失败", "sweep.input_gate");
                }
                for (std::size_t i = 1; i < station_count; ++i) {
                    const Scalar angle = residual * accumulated_length[i] / correction_length;
                    const Vec3 tangent = detail::normalize(tangents[i]);
                    const Vec3 u = frame_u[i];
                    const Scalar cosine = std::cos(angle), sine = std::sin(angle);
                    const auto first = detail::scale(u, cosine);
                    const auto second = detail::scale(detail::cross(tangent, u), sine);
                    const auto third = detail::scale(tangent, detail::dot(tangent, u) * (1.0 - cosine));
                    frame_u[i] = detail::normalize({first.x + second.x + third.x,
                                                    first.y + second.y + third.y,
                                                    first.z + second.z + third.z});
                    frame_v[i] = detail::normalize(detail::cross(tangent, frame_u[i]));
                }
            }
            detail::BodyRecord record;
            record.kind = detail::BodyKind::Sweep;
            record.rep_kind = RepKind::ExactBRep;
            record.label = closed ? "sweep_curve:periodic:" + profile.label :
                curve.kind == detail::CurveKind::CompositeChain ? "sweep_curve:composite:" + profile.label :
                                                                  "sweep_curve:spline:" + profile.label;
            record.axis = start_tangent;
            record.b = rail_length;
            record.extrude_profile_xyz = profile.polygon_xyz;
            record.extrude_holes_xyz = profile.holes_xyz;
            record.sweep_frame_origins = std::move(origins);
            record.sweep_frame_u = std::move(frame_u);
            record.sweep_frame_v = std::move(frame_v);
            if (end_scale != 1.0) {
                record.sweep_frame_scales.reserve(station_count);
                for (const Scalar distance : accumulated_length) {
                    record.sweep_frame_scales.push_back(
                        1.0 + (end_scale - 1.0) * distance / rail_length);
                }
            }
            record.sweep_frame_closed = closed;
            if (!stations.empty() && !refine_sweep_scale_law(record, stations, section_stations)) {
                return law_failure("sweep_law_sampling",
                    "截面律扫掠失败：关键站或导轨无法分辨、比例采样超过 4096 或插值标架退化",
                    0);
            }
            record.bbox = detail::make_bbox(profile.polygon_xyz.front(), profile.polygon_xyz.front());
            const std::size_t sampled_intervals = closed ? record.sweep_frame_origins.size() :
                                                           record.sweep_frame_origins.size()-1;
            const auto body = make_body(state_, std::move(record), "已完成随导轨标架曲线扫掠");
            if (body.value == 0) {
                if (!stations.empty()) return law_failure("sweep_law_materialization",
                    "截面律扫掠失败：实际截面退化、侧壁折叠/接触、接触候选超限或闭壳质量积分失败",
                    sampled_intervals);
                return modeling_input_failure(
                    *state_, diag_codes::kCoreParameterOutOfRange,
                    "曲线扫掠失败：截面、孔、导轨或采样闭壳发生数值退化", "曲线扫掠失败", "sweep.materialization");
            }
            return ok_result(body, state_->create_diagnostic("已完成随导轨标架曲线扫掠"));
        }
        // Linear rails keep the profile in world space; the rail supplies translations only.
        const auto displacement = detail::subtract(curve.poles.back(), curve.poles.front());
        const auto length = std::hypot(displacement.x, displacement.y, displacement.z);
        if (!std::isfinite(length) || length <= 0.0) {
            return modeling_input_failure(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "扫描失败：导轨位移必须有限且非退化", "扫描失败", "sweep.input_gate");
        }
        if (curve.kind == detail::CurveKind::LineSegment && end_scale == 1.0 && stations.empty()) {
            auto result = extrude(profile, detail::scale(displacement, 1.0 / length), length);
            if (!result.value) {
                const auto diagnostic = state_->diagnostics.find(result.diagnostic_id.value);
                if (diagnostic != state_->diagnostics.end()) {
                    auto issues = diagnostic->second.issues;
                    for (auto& issue : issues)
                        if (issue.stage.starts_with("extrude.")) issue.stage.replace(0, 7, "sweep");
                    result.diagnostic_id = state_->create_diagnostic("扫描失败", std::move(issues));
                }
            }
            return result;
        }
        detail::BodyRecord record;
        record.kind = detail::BodyKind::Sweep;
        record.rep_kind = RepKind::ExactBRep;
        record.label = end_scale == 1.0 ? "sweep_polyline:" + profile.label :
                                          "sweep_polyline_scaled:" + profile.label;
        record.axis = detail::scale(displacement, 1.0 / length);
        record.b = length;
        record.extrude_profile_xyz = profile.polygon_xyz;
        record.extrude_holes_xyz = profile.holes_xyz;
        for (const auto& p : curve.poles) {
            record.sweep_station_offsets.push_back(detail::subtract(p, curve.poles.front()));
        }
        if (end_scale != 1.0 || !stations.empty()) {
            const auto raw_normal = detail::newell_normal_unnormalized_poly(profile.polygon_xyz);
            const Scalar normal_length = detail::norm(raw_normal);
            const Scalar plane_tol = std::max(Scalar(1e-7), state_->config.tolerance.linear * Scalar(100.0));
            if (!std::isfinite(normal_length) || normal_length <= 1e-14 ||
                std::abs(detail::dot(detail::scale(raw_normal, 1.0 / normal_length),
                                     detail::subtract(curve.poles.front(), profile.polygon_xyz.front()))) > plane_tol) {
                return modeling_input_failure(
                    *state_, diag_codes::kCoreParameterOutOfRange,
                    "变截面扫描失败：直线或折线导轨起点必须位于轮廓平面", "变截面扫描失败", "sweep.input_gate");
            }
            if (!section_stations.empty()) {
                // Fixed-plane polyline sections roll about their oriented normal,
                // even when the translating rail has an in-plane component.
                const Vec3 normal = detail::scale(raw_normal,1/normal_length);
                record.axis = detail::scale(normal,detail::dot(normal,displacement) < 0 ? -1.0 : 1.0);
            }
            Scalar path_length = 0.0;
            std::vector<Scalar> station_distances(curve.poles.size(), 0.0);
            for (std::size_t i = 1; i < curve.poles.size(); ++i) {
                const Scalar step = detail::norm(detail::subtract(curve.poles[i], curve.poles[i - 1]));
                if (!std::isfinite(step) || !(step > 0.0)) {
                    return modeling_input_failure(
                        *state_, diag_codes::kCoreParameterOutOfRange,
                        "变截面扫描失败：导轨含重复或非有限站点", "变截面扫描失败", "sweep.input_gate");
                }
                path_length += step;
                station_distances[i] = path_length;
            }
            if (!std::isfinite(path_length) || !(path_length > 0.0)) {
                return modeling_input_failure(
                    *state_, diag_codes::kCoreParameterOutOfRange,
                    "变截面扫描失败：导轨弧长退化", "变截面扫描失败", "sweep.input_gate");
            }
            record.extrude_scale_center = curve.poles.front();
            record.sweep_station_scales.reserve(station_distances.size());
            for (const Scalar distance : station_distances) {
                record.sweep_station_scales.push_back(
                    1.0 + (end_scale - 1.0) * distance / path_length);
            }
        }
        if (!stations.empty() && !refine_sweep_scale_law(record, stations, section_stations)) {
            return law_failure("sweep_law_sampling",
                "截面律扫掠失败：关键站或折线无法分辨、比例采样超过 4096 或导轨退化",
                0);
        }
        // The materializer computes the full bounds and all geometric gates before
        // allocating any entity. make_body must not fall back to a bounding box.
        record.bbox = detail::make_bbox(profile.polygon_xyz.front(), profile.polygon_xyz.front());
        const std::size_t sampled_intervals = record.sweep_station_offsets.size()-1;
        const auto body = make_body(state_, std::move(record),
                                    end_scale == 1.0 ? "已完成折线平移扫掠" : "已完成折线变截面扫掠");
        if (body.value == 0) {
            if (!stations.empty()) return law_failure("sweep_law_materialization",
                "截面律扫掠失败：实际截面退化、轮廓或孔无效、侧壁接触、导轨非单调或闭壳质量积分失败",
                sampled_intervals);
            return modeling_input_failure(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "扫描失败：轮廓无效或导轨未沿轮廓法向严格单调推进，无法形成有效闭壳", "扫描失败", "sweep.materialization");
        }
        return ok_result(body, state_->create_diagnostic(
            end_scale == 1.0 ? "已完成折线平移扫掠" : "已完成折线变截面扫掠"));
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "sweep:" + profile.label;
    auto bbox = curve_bbox_for_query(curve);
    if (bbox.is_valid) {
        bbox = offset_bbox(bbox, 0.5);
    }
    record.bbox = bbox.is_valid ? bbox : detail::make_bbox({0.0, 0.0, 0.0}, {2.0, 2.0, 2.0});
    if (record.bbox.is_valid) {
        record.a = bbox_mass_properties(record.bbox).volume;
    }
    return ok_result(make_body(state_, record, "已完成扫描"), state_->create_diagnostic("已完成扫描"));
}

Result<BodyId> SweepService::loft(std::span<const ProfileRef> profiles) {
    if (profiles.size() < 2 ||
        std::any_of(profiles.begin(), profiles.end(), [](const ProfileRef& profile) {
            return profile.label.empty() || profile.polygon_xyz.size() < 3;
        })) {
        return modeling_input_failure(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "放样失败：至少需要两个带显式平面多边形的有效截面", "放样失败", "loft.input_gate");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "loft:polygon";
    record.loft_profiles_xyz.reserve(profiles.size());
    record.loft_holes_xyz.reserve(profiles.size());
    for (const auto& profile : profiles) {
        record.loft_profiles_xyz.push_back(profile.polygon_xyz);
        record.loft_holes_xyz.push_back(profile.holes_xyz);
    }
    // A valid sentinel is required by the generic body construction gate. The
    // loft materializer replaces it with the bounds of every actual section.
    record.bbox = detail::make_bbox(profiles.front().polygon_xyz.front(), profiles.front().polygon_xyz.front());
    const auto body = make_body(state_, std::move(record), "已完成兼容多边形截面放样");
    if (body.value == 0) {
        return modeling_input_failure(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "放样失败：截面须简单共面、拓扑兼容、严格有序，且截面间插值不得退化或折叠", "放样失败", "loft.materialization");
    }
    return ok_result(body, state_->create_diagnostic("已完成兼容多边形截面放样"));
}

Result<BodyId> SweepService::thicken(FaceId face_id, Scalar distance) {
    const auto failure = [&](StatusCode status, std::string_view code,
                             std::string_view stage, std::string message) {
        auto issue = detail::make_error_issue(code, std::move(message), {face_id.value});
        issue.stage = std::string(stage);
        issue.numeric_evidence = {{"distance", distance, "model_unit"}};
        return error_result<BodyId>(status, state_->create_diagnostic("加厚失败", {std::move(issue)}));
    };
    const auto face = state_->faces.find(face_id.value);
    if (face == state_->faces.end()) {
        return failure(StatusCode::InvalidInput, diag_codes::kCoreInvalidHandle,
                       "thicken.input_gate", "加厚失败：目标面不存在");
    }
    if (!std::isfinite(distance) || !(distance > 0)) {
        return failure(StatusCode::InvalidInput, diag_codes::kModShellFailure,
                       "thicken.input_gate", "加厚失败：厚度必须有限且为正");
    }
    const auto surface = state_->surfaces.find(face->second.surface_id.value);
    if (surface == state_->surfaces.end()) {
        return failure(StatusCode::InvalidTopology, diag_codes::kModShellFailure,
                       "thicken.topology_gate", "加厚失败：支撑曲面引用无效");
    }
    if (face->second.mass_boundary_proxy || surface->second.kind != detail::SurfaceKind::Plane) {
        return failure(StatusCode::NotImplemented, diag_codes::kCoreOperationUnsupported,
                       "thicken.support_gate", "加厚仅支持真实平面直边面，不支持代理面或曲面");
    }
    const Scalar normal_length = detail::norm(surface->second.normal);
    if (!std::isfinite(normal_length) || !(normal_length > 1e-14)) {
        return failure(StatusCode::DegenerateGeometry, diag_codes::kModShellFailure,
                       "thicken.topology_gate", "加厚失败：支撑平面法向退化");
    }
    const Vec3 normal = detail::scale(surface->second.normal, 1 / normal_length);
    const Scalar tolerance = std::max(Scalar(1e-12), state_->config.tolerance.linear);
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "thicken:planar";
    record.axis = normal;
    record.b = distance;
    // Read actual oriented boundary chains, never the face bounds. Preflight
    // remains in temporary vectors and does not populate evaluation caches.
    std::vector<LoopId> loops {face->second.outer_loop};
    loops.insert(loops.end(), face->second.inner_loops.begin(), face->second.inner_loops.end());
    for (std::size_t ring_index = 0; ring_index < loops.size(); ++ring_index) {
        const auto loop = state_->loops.find(loops[ring_index].value);
        if (loop == state_->loops.end() || loop->second.coedges.size() < 3) {
            return failure(StatusCode::InvalidTopology, diag_codes::kModShellFailure,
                           "thicken.topology_gate", "加厚失败：面边界环不完整");
        }
        std::vector<Point3> ring;
        VertexId first {}, previous {};
        for (const auto coedge_id : loop->second.coedges) {
            const auto coedge = state_->coedges.find(coedge_id.value);
            if (coedge == state_->coedges.end()) {
                return failure(StatusCode::InvalidTopology, diag_codes::kModShellFailure,
                               "thicken.topology_gate", "加厚失败：定向边引用无效");
            }
            const auto edge = state_->edges.find(coedge->second.edge_id.value);
            if (edge == state_->edges.end()) {
                return failure(StatusCode::InvalidTopology, diag_codes::kModShellFailure,
                               "thicken.topology_gate", "加厚失败：边引用无效");
            }
            const auto curve = state_->curves.find(edge->second.curve_id.value);
            const auto v0 = state_->vertices.find(edge->second.v0.value);
            const auto v1 = state_->vertices.find(edge->second.v1.value);
            if (curve == state_->curves.end() || v0 == state_->vertices.end() || v1 == state_->vertices.end()) {
                return failure(StatusCode::InvalidTopology, diag_codes::kModShellFailure,
                               "thicken.topology_gate", "加厚失败：边支撑曲线或端点无效");
            }
            const auto& c = curve->second;
            if (c.kind != detail::CurveKind::Line && c.kind != detail::CurveKind::LineSegment) {
                return failure(StatusCode::NotImplemented, diag_codes::kCoreOperationUnsupported,
                               "thicken.support_gate", "加厚失败：曲边不能用端点弦替代");
            }
            const VertexId start = coedge->second.reversed ? edge->second.v1 : edge->second.v0;
            const VertexId end = coedge->second.reversed ? edge->second.v0 : edge->second.v1;
            if ((!ring.empty() && previous.value != start.value) || start.value == end.value) {
                return failure(StatusCode::InvalidTopology, diag_codes::kModShellFailure,
                               "thicken.topology_gate", "加厚失败：边界链不连续或端点重复");
            }
            if (ring.empty()) first = start;
            previous = end;
            Point3 origin = c.origin;
            Vec3 direction = c.direction;
            if (c.kind == detail::CurveKind::LineSegment) {
                if (c.poles.size() != 2) {
                    return failure(StatusCode::InvalidTopology, diag_codes::kModShellFailure,
                                   "thicken.topology_gate", "加厚失败：线段支撑退化");
                }
                origin = c.poles.front();
                direction = detail::subtract(c.poles.back(), origin);
            }
            const Scalar length = detail::norm(direction);
            if (!std::isfinite(length) || !(length > 0)) {
                return failure(StatusCode::InvalidTopology, diag_codes::kModShellFailure,
                               "thicken.topology_gate", "加厚失败：边支撑方向退化");
            }
            const Vec3 unit = detail::scale(direction, 1 / length);
            for (const auto& point : {v0->second.point, v1->second.point}) {
                const Vec3 offset = detail::subtract(point, origin);
                const Scalar along = detail::dot(offset, unit);
                const Scalar residual = detail::norm(detail::cross(offset, unit));
                const Scalar plane_distance = detail::dot(detail::subtract(point, surface->second.origin), normal);
                if (!std::isfinite(along) || !std::isfinite(residual) || residual > tolerance ||
                    !std::isfinite(plane_distance) || std::abs(plane_distance) > tolerance ||
                    (c.kind == detail::CurveKind::LineSegment && (along < -tolerance || along > length+tolerance))) {
                    return failure(StatusCode::InvalidTopology, diag_codes::kModShellFailure,
                                   "thicken.topology_gate", "加厚失败：端点不在支撑曲线或平面上");
                }
            }
            if (edge->second.has_parameter_interval) {
                const Scalar a = edge->second.start_parameter, b = edge->second.end_parameter;
                if (!std::isfinite(a) || !std::isfinite(b) || a == b ||
                    (c.kind == detail::CurveKind::LineSegment && (a < 0 || a > 1 || b < 0 || b > 1)) ||
                    detail::norm(detail::subtract(detail::add_point_vec(origin, detail::scale(direction,a)),
                                                   v0->second.point)) > tolerance ||
                    detail::norm(detail::subtract(detail::add_point_vec(origin, detail::scale(direction,b)),
                                                   v1->second.point)) > tolerance) {
                    return failure(StatusCode::InvalidTopology, diag_codes::kModShellFailure,
                                   "thicken.topology_gate", "加厚失败：边裁剪区间与端点不一致");
                }
            }
            ring.push_back(coedge->second.reversed ? v1->second.point : v0->second.point);
        }
        if (previous.value != first.value) {
            return failure(StatusCode::InvalidTopology, diag_codes::kModShellFailure,
                           "thicken.topology_gate", "加厚失败：边界链未闭合");
        }
        if (ring_index == 0) record.extrude_profile_xyz = std::move(ring);
        else record.extrude_holes_xyz.push_back(std::move(ring));
    }
    record.source_faces = {face_id};
    append_shells_for_face(*state_, record.source_shells, face_id);
    for (const auto shell : record.source_shells) {
        const auto owners = state_->shell_to_bodies.find(shell.value);
        if (owners != state_->shell_to_bodies.end())
            for (const auto owner : owners->second) append_unique_body(record.source_bodies, BodyId {owner});
    }
    record.bbox = detail::make_bbox(record.extrude_profile_xyz.front(), record.extrude_profile_xyz.front());
    const auto body = make_body(state_, std::move(record), "已完成平面面片加厚");
    if (body.value == 0) {
        return failure(StatusCode::InvalidInput, diag_codes::kModShellFailure,
                       "thicken.materialization", "加厚失败：面区域或厚度退化，无法形成真实闭壳");
    }
    return ok_result(body, state_->create_diagnostic("已完成平面面片加厚"));
}

BooleanService::BooleanService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<BooleanIntersectionPreparation> BooleanService::prepare_intersections(
    BodyId lhs, BodyId rhs, const BooleanIntersectionOptions& options) const {
    return prepare_planar_boolean_intersections(*state_, lhs, rhs, options);
}

Result<OpReport> BooleanService::run(BooleanOp op, BodyId lhs, BodyId rhs, const BooleanOptions& boolean_options) {
    if (op != BooleanOp::Union && op != BooleanOp::Subtract &&
        op != BooleanOp::Intersect && op != BooleanOp::Split) {
        return boolean_op_fail_staged(state_, boolean_options.diagnostics, op, StatusCode::InvalidInput,
                                      diag_codes::kBoolInvalidInput,
                                      "布尔运算失败：运算类型无效", "布尔运算失败", lhs, rhs, nullptr);
    }
    if (!detail::has_body(*state_, lhs) || !detail::has_body(*state_, rhs)) {
        return boolean_op_fail_staged(state_, boolean_options.diagnostics, op, StatusCode::InvalidInput,
                                      diag_codes::kBoolInvalidInput,
                                      "布尔运算失败：输入实体不存在或无效", "布尔运算失败", lhs, rhs, nullptr);
    }

    detail::BodyRecord record;
    record.kind = detail::BodyKind::BooleanResult;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "boolean";
    append_unique_body(record.source_bodies, lhs);
    append_unique_body(record.source_bodies, rhs);
    if (op == BooleanOp::Subtract) {
        append_shell_provenance_for_body(*state_, record.source_shells, lhs);
        append_face_provenance_for_body(*state_, record.source_faces, lhs);
    } else {
        append_shell_provenance_for_body(*state_, record.source_shells, lhs);
        append_shell_provenance_for_body(*state_, record.source_shells, rhs);
        append_face_provenance_for_body(*state_, record.source_faces, lhs);
        append_face_provenance_for_body(*state_, record.source_faces, rhs);
    }
    const auto& lhs_bbox = state_->bodies[lhs.value].bbox;
    const auto& rhs_bbox = state_->bodies[rhs.value].bbox;
    std::vector<Warning> warnings;
    const auto relation = classify_bbox_relation(lhs_bbox, rhs_bbox);
    const auto prep = compute_boolean_prep_stats(*state_, lhs, rhs);

    switch (op) {
        case BooleanOp::Union:
            record.bbox = union_bbox(lhs_bbox, rhs_bbox);
            if (relation == BBoxRelation::Disjoint) {
                warnings.push_back(detail::make_warning(diag_codes::kBoolNearDegenerateWarning,
                                                        "并运算输入互不重叠，结果当前仍以单体包围盒语义表示"));
            }
            break;
        case BooleanOp::Intersect:
            if (relation == BBoxRelation::Disjoint) {
                return boolean_op_fail_staged(state_, boolean_options.diagnostics, op, StatusCode::OperationFailed,
                                              diag_codes::kBoolIntersectionFailure,
                                              "布尔交集失败：两个输入体的包围盒不相交", "布尔交集失败", lhs, rhs,
                                              &prep);
            }
            record.bbox = prep.local_overlap_bbox.is_valid
                              ? prep.local_overlap_bbox
                              : relation == BBoxRelation::LhsContainsRhs ? rhs_bbox
                              : relation == BBoxRelation::RhsContainsLhs ? lhs_bbox
                                                                         : intersect_bbox(lhs_bbox, rhs_bbox);
            if (!prep.local_overlap_bbox.is_valid && relation != BBoxRelation::Disjoint) {
                warnings.push_back(detail::make_warning(
                    diag_codes::kBoolPrepNoCandidateWarning,
                    "布尔交集未构建到局部候选重叠区域，已回退为全局 bbox 交叠语义"));
            }
            if (relation == BBoxRelation::Touching) {
                warnings.push_back(detail::make_warning(diag_codes::kBoolNearDegenerateWarning,
                                                        "交集结果仅在包围盒层面接触，可能退化为低维结果"));
            } else {
                warnings.push_back(detail::make_warning(diag_codes::kBoolNearDegenerateWarning,
                                                        "交集结果可能接近退化边界"));
            }
            break;
        case BooleanOp::Subtract:
            record.bbox = lhs_bbox;
            if (relation == BBoxRelation::Disjoint) {
                warnings.push_back(detail::make_warning(diag_codes::kBoolNearDegenerateWarning, "减运算输入未发生空间重叠，结果与左体近似一致"));
            } else if (prep.overlap_candidates == 0) {
                warnings.push_back(detail::make_warning(
                    diag_codes::kBoolPrepNoCandidateWarning,
                    "减运算未发现局部重叠候选，当前仅按全局语义保留左体"));
            } else if (relation == BBoxRelation::RhsContainsLhs) {
                return boolean_op_fail_staged(state_, boolean_options.diagnostics, op, StatusCode::OperationFailed,
                                              diag_codes::kBoolClassificationFailure,
                                              "布尔减运算失败：右体近似完全包含左体，当前阶段无法稳定表达空结果",
                                              "布尔减运算失败", lhs, rhs, &prep);
            }
            break;
        case BooleanOp::Split:
            record.bbox = union_bbox(lhs_bbox, rhs_bbox);
            warnings.push_back(detail::make_warning(diag_codes::kBoolNearDegenerateWarning, "Split 目前返回合并包围盒语义，尚未生成真实分割片段"));
            break;
    }

    record.has_boolean_op = true;
    record.boolean_op = op;

    // Check all real face boundaries before materializing an output or touching
    // Eval. A rejected trim must leave the caller's model and writer intact.
    const auto face_candidates = build_face_candidates_for_boolean(*state_, lhs, rhs);
    auto intersection_curves = compute_intersection_curves_for_candidates(*state_, face_candidates);
    const auto trimmed = clip_intersection_lines_to_face_boundaries(*state_, intersection_curves, lhs, rhs);
    if (!trimmed.value) return error_result<OpReport>(trimmed.status, trimmed.diagnostic_id);
    const auto& intersection_segments = *trimmed.value;

    BodyId output = make_body(state_, record, "已完成布尔操作");
    detail::invalidate_eval_for_bodies(*state_, {lhs, rhs});
    const auto diag = boolean_options.diagnostics ? state_->create_diagnostic("布尔操作完成") : DiagnosticId {};
    append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageCandidates,
                               "布尔候选构建阶段开始：已进入壳/区域级候选统计流程",
                               {lhs.value, rhs.value});
    append_boolean_face_candidate_issue(*state_, diag, lhs, rhs, face_candidates.size());
    append_boolean_intersection_curve_issue(*state_, diag, lhs, rhs, intersection_curves);
    append_boolean_intersection_segment_issue(*state_, diag, lhs, rhs, intersection_segments);
    if (diag.value != 0 && !intersection_segments.empty()) {
        append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageSplit,
                                  "布尔切分阶段开始：已准备执行 imprint/split 占位路径",
                                  {lhs.value, rhs.value, output.value});
        std::vector<CurveId> curves;
        curves.reserve(intersection_segments.size());
        for (const auto& seg : intersection_segments) {
            curves.push_back(seg.curve);
        }
        // Stage 2: store intersection wires for later imprint/split/classify.
        const auto iid = store_intersection(state_, "boolean_intersection_wires", record.bbox, std::move(curves), {});
        append_boolean_intersection_stored_issue(*state_, diag, lhs, rhs, iid, intersection_segments.size());

        // Stage 2 minimal imprint: mutate output owned topology (split one rectangular face) to enter real split/imprint development.
        const auto out_it = state_->bodies.find(output.value);
        if (out_it != state_->bodies.end() && !out_it->second.shells.empty()) {
            const auto shell_id = out_it->second.shells.front();
            const auto shell_it = state_->shells.find(shell_id.value);
            if (shell_it != state_->shells.end()) {
                FaceId target {};
                for (const auto fid : shell_it->second.faces) {
                    const auto fit = state_->faces.find(fid.value);
                    if (fit == state_->faces.end()) {
                        continue;
                    }
                    const auto lit = state_->loops.find(fit->second.outer_loop.value);
                    if (lit == state_->loops.end()) {
                        continue;
                    }
                    if (lit->second.coedges.size() == 4) {
                        target = fid;
                        break;
                    }
                }
                if (target.value != 0) {
                    auto seg_curve = longest_intersection_segment_curve(*state_, intersection_segments);
                    if (seg_curve.value == 0) {
                        seg_curve = intersection_segments.front().curve;
                    }
                    bool applied = false;
                    if (imprint_split_rect_face_by_segment(*state_, shell_id, target, seg_curve)) {
                        applied = true;
                        auto issue = detail::make_info_issue(diag_codes::kBoolImprintSegmentApplied,
                                                             "布尔切分/imprint 已应用：输出壳的矩形面已按交线段切分为两四边形");
                        issue.related_entities = {lhs.value, rhs.value, output.value, shell_id.value, target.value};
                        set_boolean_diagnostic_stage(issue, diag_codes::kBoolImprintSegmentApplied);
                        state_->append_diagnostic_issue(diag, std::move(issue));
                    } else {
                        bool prefer_diag_02 = true;
                        const auto curve_it = state_->curves.find(seg_curve.value);
                        if (curve_it != state_->curves.end() && curve_it->second.kind == detail::CurveKind::LineSegment &&
                            curve_it->second.poles.size() >= 2) {
                            const auto seg_dir = detail::normalize(detail::subtract(curve_it->second.poles.back(),
                                                                                   curve_it->second.poles.front()));
                            const auto face_it = state_->faces.find(target.value);
                            if (face_it != state_->faces.end()) {
                                const auto loop_it = state_->loops.find(face_it->second.outer_loop.value);
                                if (loop_it != state_->loops.end() && loop_it->second.coedges.size() == 4) {
                                    std::array<VertexId, 4> verts {};
                                    for (std::size_t i = 0; i < 4; ++i) {
                                        const auto oriented = oriented_vertices_for_coedge_local(*state_, loop_it->second.coedges[i]);
                                        if (!oriented.has_value()) {
                                            break;
                                        }
                                        verts[i] = (*oriented)[0];
                                    }
                                    const auto v0_it = state_->vertices.find(verts[0].value);
                                    const auto v1_it = state_->vertices.find(verts[1].value);
                                    const auto v2_it = state_->vertices.find(verts[2].value);
                                    const auto v3_it = state_->vertices.find(verts[3].value);
                                    if (v0_it != state_->vertices.end() && v1_it != state_->vertices.end() &&
                                        v2_it != state_->vertices.end() && v3_it != state_->vertices.end()) {
                                        const auto d02 = detail::normalize(detail::subtract(v2_it->second.point, v0_it->second.point));
                                        const auto d13 = detail::normalize(detail::subtract(v3_it->second.point, v1_it->second.point));
                                        const auto s02 = std::abs(detail::dot(d02, seg_dir));
                                        const auto s13 = std::abs(detail::dot(d13, seg_dir));
                                        prefer_diag_02 = s02 >= s13;
                                    }
                                }
                            }
                        }
                        if (imprint_split_rect_face_diagonal(*state_, shell_id, target, prefer_diag_02)) {
                            applied = true;
                            auto issue = detail::make_info_issue(diag_codes::kBoolImprintApplied,
                                                                 "布尔切分/imprint 已应用：输出壳的矩形面已沿对角线切分");
                            issue.related_entities = {lhs.value, rhs.value, output.value, shell_id.value, target.value};
                            set_boolean_diagnostic_stage(issue, diag_codes::kBoolImprintApplied);
                            state_->append_diagnostic_issue(diag, std::move(issue));
                        }
                    }

                    if (applied) {
                        detail::rebuild_topology_links(*state_);
                    }
                }
            }
        }
    }
    append_boolean_prep_candidate_issue(*state_, diag, lhs, rhs, prep);
    append_boolean_run_stage_issue(*state_, diag, op, relation, prep, lhs, rhs, output);
    append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageOutputMaterialized,
                               "布尔输出物化完成：owned topology 已建立（来源面/壳重建、单壳 bbox 回退及后续 imprint 等路径的组合）",
                               {lhs.value, rhs.value, output.value});

    if (diag.value != 0) {
        append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageClassify,
                                  "布尔分类阶段开始：将对输出面执行最小可解释分类统计",
                                  {lhs.value, rhs.value, output.value});
        // Stage 2 classification v1: point classification against analytic RHS primitive when available.
        auto point_in_cylinder = [&](const detail::BodyRecord& cyl, const Point3& p, Scalar eps) -> int {
            // returns: 1 inside, 0 on, -1 outside
            const auto C = cyl.origin;
            const auto a = cyl.axis;
            const auto r = cyl.a;
            const auto h = cyl.b;
            const Vec3 cp {p.x - C.x, p.y - C.y, p.z - C.z};
            const auto t = detail::dot(cp, a);
            const auto half = h * 0.5;
            if (t < -half - eps || t > half + eps) {
                return -1;
            }
            const Vec3 proj {a.x * t, a.y * t, a.z * t};
            const Vec3 radial {cp.x - proj.x, cp.y - proj.y, cp.z - proj.z};
            const auto rr = detail::dot(radial, radial);
            const auto r2 = r * r;
            if (rr > r2 + eps) {
                return -1;
            }
            if (std::abs(rr - r2) <= eps || std::abs(t - half) <= eps || std::abs(t + half) <= eps) {
                return 0;
            }
            return 1;
        };

        auto point_in_sphere = [&](const detail::BodyRecord& sph, const Point3& p, Scalar eps) -> int {
            const auto C = sph.origin;
            const auto r = sph.a;
            const Vec3 d {p.x - C.x, p.y - C.y, p.z - C.z};
            const auto rr = detail::dot(d, d);
            const auto r2 = r * r;
            if (rr > r2 + eps) {
                return -1;
            }
            if (std::abs(rr - r2) <= eps) {
                return 0;
            }
            return 1;
        };

        auto face_representative_point = [&](FaceId face_id, bool& ok) -> Point3 {
            ok = false;
            const auto face_it = state_->faces.find(face_id.value);
            if (face_it == state_->faces.end()) {
                return {};
            }
            const auto loop_it = state_->loops.find(face_it->second.outer_loop.value);
            if (loop_it == state_->loops.end() || loop_it->second.coedges.empty()) {
                return {};
            }
            Point3 sum {0.0, 0.0, 0.0};
            std::size_t count = 0;
            for (const auto coedge_id : loop_it->second.coedges) {
                const auto oriented = oriented_vertices_for_coedge_local(*state_, coedge_id);
                if (!oriented.has_value()) {
                    continue;
                }
                const auto v_it = state_->vertices.find((*oriented)[0].value);
                if (v_it == state_->vertices.end()) {
                    continue;
                }
                sum.x += v_it->second.point.x;
                sum.y += v_it->second.point.y;
                sum.z += v_it->second.point.z;
                ++count;
            }
            if (count == 0) {
                return {};
            }
            ok = true;
            return Point3 {sum.x / static_cast<Scalar>(count),
                           sum.y / static_cast<Scalar>(count),
                           sum.z / static_cast<Scalar>(count)};
        };

        std::size_t face_total = 0;
        std::size_t classified_inside = 0;
        std::size_t classified_on = 0;
        std::size_t classified_outside = 0;
        std::size_t classified_unknown = 0;
        std::string method = "bbox_fallback";
        std::unordered_map<std::uint64_t, int> face_cls;

        const auto rhs_it = state_->bodies.find(rhs.value);
        const auto out_it = state_->bodies.find(output.value);
        if (rhs_it != state_->bodies.end() && out_it != state_->bodies.end() && !out_it->second.shells.empty()) {
            const auto& rhs_body = rhs_it->second;
            const auto& out_body = out_it->second;
            const Scalar eps = detail::resolve_linear_tolerance(0.0, state_->config.tolerance);
            const bool use_cylinder = rhs_body.kind == detail::BodyKind::Cylinder && rhs_body.rep_kind == RepKind::ExactBRep &&
                                      rhs_body.a > 0.0 && rhs_body.b > 0.0;
            const bool use_sphere =
                rhs_body.kind == detail::BodyKind::Sphere && rhs_body.rep_kind == RepKind::ExactBRep && rhs_body.a > 0.0;
            if (use_cylinder) {
                method = "cylinder_point_classification";
            } else if (use_sphere) {
                method = "sphere_point_classification";
            }
            for (const auto shell_id : out_body.shells) {
                const auto shell_it = state_->shells.find(shell_id.value);
                if (shell_it == state_->shells.end()) {
                    continue;
                }
                for (const auto face_id : shell_it->second.faces) {
                    ++face_total;
                    bool ok = false;
                    const auto p = face_representative_point(face_id, ok);
                    if (!ok) {
                        face_cls[face_id.value] = -2;
                        ++classified_unknown;
                        continue;
                    }
                    int cls = -2;
                    if (use_cylinder) {
                        cls = point_in_cylinder(rhs_body, p, eps);
                    } else if (use_sphere) {
                        cls = point_in_sphere(rhs_body, p, eps);
                    } else {
                        // Fallback: bbox-based point inclusion.
                        if (!rhs_bbox.is_valid) {
                            cls = -2;
                        } else if (p.x >= rhs_bbox.min.x - eps && p.x <= rhs_bbox.max.x + eps &&
                                   p.y >= rhs_bbox.min.y - eps && p.y <= rhs_bbox.max.y + eps &&
                                   p.z >= rhs_bbox.min.z - eps && p.z <= rhs_bbox.max.z + eps) {
                            cls = 1;
                        } else {
                            cls = -1;
                        }
                    }
                    face_cls[face_id.value] = cls;
                    if (cls == 1) {
                        ++classified_inside;
                    } else if (cls == 0) {
                        ++classified_on;
                    } else if (cls == -1) {
                        ++classified_outside;
                    } else {
                        ++classified_unknown;
                    }
                }
            }
        }

        {
            std::ostringstream msg;
            msg << "布尔分类阶段完成: method=" << method
                << " face_total=" << face_total
                << " inside=" << classified_inside
                << " on=" << classified_on
                << " outside=" << classified_outside
                << " unknown=" << classified_unknown;
            auto issue = detail::make_info_issue(diag_codes::kBoolClassificationCompleted, msg.str());
            issue.related_entities = {lhs.value, rhs.value, output.value};
            set_boolean_diagnostic_stage(issue, diag_codes::kBoolClassificationCompleted);
            state_->append_diagnostic_issue(diag, std::move(issue));
        }

        bool subtract_shell_rebuild_applied = false;
        bool subtract_shell_rebuild_rolled_back = false;
        if (op == BooleanOp::Subtract && prep.overlap_candidates > 0 && out_it != state_->bodies.end() &&
            !out_it->second.shells.empty() && !face_cls.empty()) {
            append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageRebuild,
                                      "布尔重建阶段开始：将尝试按分类结果裁剪/重建输出壳（占位策略）",
                                      {lhs.value, rhs.value, output.value});
            const auto shell_id = out_it->second.shells.front();
            const auto shell_it = state_->shells.find(shell_id.value);
            if (shell_it != state_->shells.end()) {
                std::vector<FaceId> seed;
                seed.reserve(shell_it->second.faces.size());
                for (const auto face_id : shell_it->second.faces) {
                    const auto it = face_cls.find(face_id.value);
                    const int c = (it == face_cls.end()) ? -2 : it->second;
                    if (c != 1) {
                        seed.push_back(face_id);
                    }
                }
                auto closed = detail::build_closed_face_region_from_source_faces(*state_, shell_id, seed);
                auto& shell_faces = shell_it->second.faces;
                if (!closed.empty() && closed.size() >= 6 && !same_unordered_face_ids(closed, shell_faces)) {
                    auto backup_faces = shell_faces;
                    shell_faces = std::move(closed);
                    detail::rebuild_topology_links(*state_);
                    ValidationService validation_after_trim {state_};
                    const auto trim_strict = validation_after_trim.validate_topology(output, ValidationMode::Strict);
                    if (trim_strict.status != StatusCode::Ok) {
                        shell_faces = std::move(backup_faces);
                        detail::rebuild_topology_links(*state_);
                        subtract_shell_rebuild_rolled_back = true;
                    } else {
                        subtract_shell_rebuild_applied = true;
                    }
                }
            }
        }

        ValidationService validation {state_};
        append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageValidate,
                                  "布尔验证阶段开始：将对输出执行 Strict 拓扑验证",
                                  {lhs.value, rhs.value, output.value});
        auto strict_result = validation.validate_topology(output, ValidationMode::Strict);
        bool auto_repair_used = false;
        if (strict_result.status != StatusCode::Ok && boolean_options.auto_repair) {
            append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageRepair,
                                      "布尔修复阶段开始：Strict 未通过，将尝试 auto_repair(Safe)",
                                      {lhs.value, rhs.value, output.value});
            RepairService repair {state_};
            const auto repaired = repair.auto_repair(output, RepairMode::Safe);
            if (repaired.status == StatusCode::Ok && repaired.value.has_value()) {
                output = repaired.value->output;
                strict_result = validation.validate_topology(output, ValidationMode::Strict);
                auto_repair_used = true;
            }
        }
        {
            std::ostringstream msg;
            msg << "布尔重建阶段完成: strict_ok=" << (strict_result.status == StatusCode::Ok ? "true" : "false")
                << " subtract_shell_rebuild_applied=" << (subtract_shell_rebuild_applied ? "true" : "false")
                << " subtract_shell_rebuild_rollback=" << (subtract_shell_rebuild_rolled_back ? "true" : "false")
                << " auto_repair=" << (auto_repair_used ? "true" : "false");
            auto issue = detail::make_info_issue(diag_codes::kBoolRebuildCompleted, msg.str());
            issue.related_entities = {lhs.value, rhs.value, output.value};
            set_boolean_diagnostic_stage(issue, diag_codes::kBoolRebuildCompleted);
            state_->append_diagnostic_issue(diag, std::move(issue));
        }

        if (strict_result.status != StatusCode::Ok) {
            auto residual = detail::make_warning_issue(
                diag_codes::kBoolStrictValidationResidual,
                "布尔结果 Strict 拓扑验证仍未通过（含已尝试 auto_repair 时）：精确切分/分类/重建/修复工业闭环未完整时可出现；"
                "建议对输出体执行 Heal 或检查输入几何。");
            residual.related_entities = {lhs.value, rhs.value, output.value};
            set_boolean_diagnostic_stage(residual, diag_codes::kBoolStrictValidationResidual);
            state_->append_diagnostic_issue(diag, std::move(residual));
        }
    }
    if (diag.value != 0) {
        for (const auto& warning : warnings) {
            auto issue = detail::make_warning_issue(warning.code, warning.message);
            issue.related_entities = {lhs.value, rhs.value, output.value};
            set_boolean_diagnostic_stage(issue, warning.code);
            state_->append_diagnostic_issue(diag, std::move(issue));
        }
    }

    return ok_result(make_report(StatusCode::Ok, output, diag, warnings), diag);
}

Result<void> BooleanService::export_boolean_prep_stats(BodyId lhs, BodyId rhs, std::string_view path) const {
    if (!detail::has_body(*state_, lhs) || !detail::has_body(*state_, rhs) || path.empty()) {
        return boolean_prep_export_fail(
            *state_, StatusCode::InvalidInput, diag_codes::kBoolInvalidInput,
            "布尔预处理统计导出失败：输入实体无效或输出路径为空",
            lhs, rhs, path, "bool.prep.export.input", nullptr);
    }
    const auto stats = compute_boolean_prep_stats(*state_, lhs, rhs);
    std::ofstream out {std::string(path)};
    if (!out) {
        return boolean_prep_export_fail(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "布尔预处理统计导出失败：无法打开输出文件",
            lhs, rhs, path, "bool.prep.export.open", &stats);
    }
    out << "{";
    out << "\"lhs_regions\":" << stats.lhs_regions << ",";
    out << "\"rhs_regions\":" << stats.rhs_regions << ",";
    out << "\"overlap_candidates\":" << stats.overlap_candidates << ",";
    out << "\"overlap_volume_sum\":" << stats.overlap_volume_sum << ",";
    out << "\"local_clip_applied\":" << (stats.local_clip_applied ? "true" : "false");
    if (stats.local_overlap_bbox.is_valid) {
        out << ",\"local_overlap_bbox\":{"
            << "\"min_x\":" << stats.local_overlap_bbox.min.x << ","
            << "\"min_y\":" << stats.local_overlap_bbox.min.y << ","
            << "\"min_z\":" << stats.local_overlap_bbox.min.z << ","
            << "\"max_x\":" << stats.local_overlap_bbox.max.x << ","
            << "\"max_y\":" << stats.local_overlap_bbox.max.y << ","
            << "\"max_z\":" << stats.local_overlap_bbox.max.z << "}";
    }
    out << "}";
    out.close();
    if (!out) {
        return boolean_prep_export_fail(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "布尔预处理统计导出失败：文件写入失败",
            lhs, rhs, path, "bool.prep.export.write", &stats);
    }
    return ok_void(state_->create_diagnostic("已导出布尔预处理统计"));
}

ModifyService::ModifyService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<OpReport> ModifyService::draft_faces(BodyId body_id, std::span<const FaceId> faces, const Vec3& pull_dir, Scalar angle) {
    if (!detail::has_body(*state_, body_id) || angle == 0.0 || !valid_axis(pull_dir) || !valid_face_ids(*state_, faces)) {
        return op_report_error_with_stage(
            *state_, StatusCode::InvalidInput, diag_codes::kCoreParameterOutOfRange,
            "拔模失败：目标实体不存在、面集合无效、拉拔方向无效或角度不能为 0", "拔模失败",
            "modify.draft.input_gate", std::span<const std::uint64_t> {});
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    record.kind = detail::BodyKind::Modified;
    record.label = "draft";
    append_unique_body(record.source_bodies, body_id);
    for (const auto face_id : faces) {
        append_unique_face(record.source_faces, face_id);
        append_shells_for_face(*state_, record.source_shells, face_id);
    }
    record.bbox = offset_bbox(record.bbox, std::abs(angle) * 0.01);
    const auto output = make_body(state_, record, "已完成拔模");
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("拔模操作完成");
    return ok_result(make_report(StatusCode::Ok, output, diag), diag);
}

Result<OpReport> ModifyService::replace_face(BodyId body_id, FaceId target, SurfaceId replacement) {
    if (!detail::has_body(*state_, body_id) || state_->faces.find(target.value) == state_->faces.end() || !detail::has_surface(*state_, replacement)) {
        return op_report_error_with_stage(
            *state_, StatusCode::InvalidInput, diag_codes::kModReplaceFaceIncompatible,
            "替换面失败：目标实体、目标面或替换曲面无效", "替换面失败", "modify.replace_face.input_gate",
            std::span<const std::uint64_t> {});
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    record.kind = detail::BodyKind::Modified;
    record.label = "replace_face";
    append_unique_body(record.source_bodies, body_id);
    append_unique_face(record.source_faces, target);
    append_shells_for_face_owned_by_body(*state_, record.source_shells, target, body_id);
    const auto output = make_body(state_, record, "已完成替换面");
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("替换面操作完成");
    return ok_result(make_report(StatusCode::Ok, output, diag), diag);
}

Result<OpReport> ModifyService::delete_face_and_heal(BodyId body_id, FaceId target) {
    if (!detail::has_body(*state_, body_id) || state_->faces.find(target.value) == state_->faces.end()) {
        return op_report_error_with_stage(
            *state_, StatusCode::InvalidInput, diag_codes::kModDeleteFaceHealFailure,
            "删除面补面失败：目标实体或目标面无效", "删除面补面失败", "modify.delete_face.input_gate",
            std::span<const std::uint64_t> {});
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    record.kind = detail::BodyKind::Modified;
    record.label = "delete_face_and_heal";
    append_unique_body(record.source_bodies, body_id);
    append_unique_face(record.source_faces, target);
    append_shells_for_face(*state_, record.source_shells, target);
    const auto output = make_body(state_, record, "已完成删除面补面");
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("删除面补面操作完成");
    auto report = make_report(StatusCode::Ok, output, diag,
                              {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "局部面删除后已执行简化补面")});
    {
        auto heal_warn = detail::make_warning_issue(diag_codes::kHealFeatureRemovedWarning, "局部面删除后执行了补面简化");
        heal_warn.related_entities = {body_id.value, output.value, target.value};
        heal_warn.stage = "modify.delete_face.heal";
        state_->append_diagnostic_issue(diag, std::move(heal_warn));
    }
    return ok_result(report, diag);
}

QueryService::QueryService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<IntersectionId> QueryService::intersect(CurveId curve_id, SurfaceId surface_id) const {
    if (!detail::has_curve(*state_, curve_id) || !detail::has_surface(*state_, surface_id)) {
        return detail::failed_result<IntersectionId>(
            *state_, StatusCode::InvalidInput, diag_codes::kQueryClosestPointFailure,
            "曲线曲面求交失败：输入曲线或曲面不存在", "曲线曲面求交失败");
    }
    const auto& curve = state_->curves.at(curve_id.value);
    const auto& surface = state_->surfaces.at(surface_id.value);

    if (curve.kind == detail::CurveKind::Line && surface.kind == detail::SurfaceKind::Plane) {
        const auto denom = detail::dot(curve.direction, surface.normal);
        const auto delta = detail::subtract(surface.origin, curve.origin);
        if (std::abs(denom) <= 1e-12) {
            if (std::abs(detail::dot(delta, surface.normal)) > 1e-9) {
                return detail::failed_result<IntersectionId>(
                    *state_, StatusCode::OperationFailed, diag_codes::kQueryClosestPointFailure,
                    "曲线曲面求交失败：直线与平面平行且不共面", "曲线曲面求交失败");
            }
            const auto id = store_intersection(state_, "line_plane_coincident", curve_bbox_for_query(curve), {curve_id}, {surface_id});
            return ok_result(id, state_->create_diagnostic("已完成曲线曲面求交"));
        }

        const auto t = detail::dot(delta, surface.normal) / denom;
        const auto point = detail::add_point_vec(curve.origin, detail::scale(curve.direction, t));
        const auto id = store_intersection(state_, "line_plane_point", detail::make_bbox(point, point), {curve_id}, {surface_id});
        return ok_result(id, state_->create_diagnostic("已完成曲线曲面求交"));
    }

    if (curve.kind == detail::CurveKind::Line && surface.kind == detail::SurfaceKind::Sphere) {
        const auto oc = detail::subtract(curve.origin, surface.origin);
        const auto a = detail::dot(curve.direction, curve.direction);
        const auto b = 2.0 * detail::dot(oc, curve.direction);
        const auto c = detail::dot(oc, oc) - surface.radius_a * surface.radius_a;
        const auto discriminant = b * b - 4.0 * a * c;
        if (discriminant < 0.0) {
            return detail::failed_result<IntersectionId>(
                *state_, StatusCode::OperationFailed, diag_codes::kQueryClosestPointFailure,
                "曲线曲面求交失败：直线与球面不相交", "曲线曲面求交失败");
        }
        const auto id = store_intersection(state_, "line_sphere", surface_bbox_for_query(surface), {curve_id}, {surface_id});
        return ok_result(id, state_->create_diagnostic("已完成曲线曲面求交"));
    }

    const auto bbox = intersect_bbox(curve_bbox_for_query(curve), surface_bbox_for_query(surface));
    if (!bbox.is_valid) {
        return detail::failed_result<IntersectionId>(
            *state_, StatusCode::OperationFailed, diag_codes::kQueryClosestPointFailure,
            "曲线曲面求交失败：输入对象包围盒不相交", "曲线曲面求交失败");
    }
    const auto id = store_intersection(state_, "curve_surface_bbox_overlap", bbox, {curve_id}, {surface_id});
    return ok_result(id, state_->create_diagnostic("已完成曲线曲面求交"));
}

Result<IntersectionId> QueryService::intersect(SurfaceId lhs, SurfaceId rhs) const {
    if (!detail::has_surface(*state_, lhs) || !detail::has_surface(*state_, rhs)) {
        return detail::failed_result<IntersectionId>(
            *state_, StatusCode::InvalidInput, diag_codes::kQueryClosestPointFailure,
            "曲面曲面求交失败：输入曲面不存在", "曲面曲面求交失败");
    }
    const auto& lhs_surface = state_->surfaces.at(lhs.value);
    const auto& rhs_surface = state_->surfaces.at(rhs.value);

    if (lhs_surface.kind == detail::SurfaceKind::Plane && rhs_surface.kind == detail::SurfaceKind::Plane) {
        const auto cross = detail::cross(lhs_surface.normal, rhs_surface.normal);
        const auto cross_norm = detail::norm(cross);
        const auto offset = detail::dot(detail::subtract(rhs_surface.origin, lhs_surface.origin), lhs_surface.normal);
        if (cross_norm <= 1e-12) {
            if (std::abs(offset) > 1e-9) {
                return detail::failed_result<IntersectionId>(
                    *state_, StatusCode::OperationFailed, diag_codes::kQueryClosestPointFailure,
                    "曲面曲面求交失败：两个平面平行且不重合", "曲面曲面求交失败");
            }
            const auto id = store_intersection(state_, "plane_plane_coincident", surface_bbox_for_query(lhs_surface), {}, {lhs, rhs});
            return ok_result(id, state_->create_diagnostic("已完成曲面曲面求交"));
        }
        const auto bbox = union_bbox(surface_bbox_for_query(lhs_surface), surface_bbox_for_query(rhs_surface));
        const auto id = store_intersection(state_, "plane_plane_line", bbox, {}, {lhs, rhs});
        return ok_result(id, state_->create_diagnostic("已完成曲面曲面求交"));
    }

    if (lhs_surface.kind == detail::SurfaceKind::Sphere && rhs_surface.kind == detail::SurfaceKind::Sphere) {
        const auto center_delta = detail::subtract(lhs_surface.origin, rhs_surface.origin);
        const auto center_distance = detail::norm(center_delta);
        const auto radius_sum = lhs_surface.radius_a + rhs_surface.radius_a;
        const auto radius_diff = std::abs(lhs_surface.radius_a - rhs_surface.radius_a);
        if (center_distance > radius_sum || center_distance < radius_diff) {
            return detail::failed_result<IntersectionId>(
                *state_, StatusCode::OperationFailed, diag_codes::kQueryClosestPointFailure,
                "曲面曲面求交失败：两个球面不存在稳定交线", "曲面曲面求交失败");
        }
        const auto bbox = intersect_bbox(surface_bbox_for_query(lhs_surface), surface_bbox_for_query(rhs_surface));
        const auto id = store_intersection(state_, "sphere_sphere_circle", bbox, {}, {lhs, rhs});
        return ok_result(id, state_->create_diagnostic("已完成曲面曲面求交"));
    }

    const auto bbox = intersect_bbox(surface_bbox_for_query(lhs_surface), surface_bbox_for_query(rhs_surface));
    if (!bbox.is_valid) {
        return detail::failed_result<IntersectionId>(
            *state_, StatusCode::OperationFailed, diag_codes::kQueryClosestPointFailure,
            "曲面曲面求交失败：输入曲面包围盒不相交", "曲面曲面求交失败");
    }
    const auto id = store_intersection(state_, "surface_surface_bbox_overlap", bbox, {}, {lhs, rhs});
    return ok_result(id, state_->create_diagnostic("已完成曲面曲面求交"));
}

Result<BodyPlaneSection> QueryService::section_detailed(
    BodyId body_id, const Plane& plane, const BodySpatialQueryOptions& options) const {
    return TopologyQueryService(state_).section(body_id, plane, options);
}

Result<MeshId> QueryService::section(BodyId body_id, const Plane& plane) const {
    const auto section = section_detailed(body_id, plane);
    if (!section.value) return error_result<MeshId>(section.status, section.diagnostic_id);
    if (section.value->triangles.empty()) return ok_result(MeshId {}, section.diagnostic_id);
    detail::MeshRecord mesh;
    mesh.source_body = body_id;
    mesh.label = "section_polyhedron";
    mesh.bbox = section.value->bbox;
    mesh.vertices = section.value->vertices;
    for (const auto& triangle : section.value->triangles)
        for (const auto index : triangle) mesh.indices.push_back(static_cast<Index>(index));
    const auto mesh_id = MeshId {state_->allocate_id()};
    state_->meshes.emplace(mesh_id.value, std::move(mesh));
    return ok_result(mesh_id, section.diagnostic_id);
}

Result<BodyPointQuery> QueryService::closest_point(
    BodyId body_id, const Point3& point, const BodySpatialQueryOptions& options) const {
    return TopologyQueryService(state_).locate_point(body_id, point, options);
}

Result<BodyDistanceQuery> QueryService::closest_points(
    BodyId lhs, BodyId rhs, const BodySpatialQueryOptions& options) const {
    return TopologyQueryService(state_).closest_points(lhs, rhs, options);
}

Result<MassProperties> QueryService::mass_properties(BodyId body_id) const {
    const auto it = state_->bodies.find(body_id.value);
    if (it == state_->bodies.end()) {
        return TopologyQueryService(state_).body_mass_properties(body_id);
    }

    const auto& body = it->second;
    const bool analytic = body.kind == detail::BodyKind::Sphere ||
        body.kind == detail::BodyKind::Cylinder || body.kind == detail::BodyKind::Cone ||
        body.kind == detail::BodyKind::Torus;
    if (!analytic) {
        // Current topology is the sole authority for real polyhedra. Provenance,
        // sweep creation caches and bounding boxes cannot recover a failed query.
        return TopologyQueryService(state_).body_mass_properties(body_id);
    }
    const auto failure = [&](StatusCode status, std::string_view code,
                              std::string_view stage, const char* message) {
        auto issue = detail::make_error_issue(code, message, {body_id.value});
        issue.stage = std::string(stage);
        return error_result<MassProperties>(status,
            state_->create_diagnostic("质量属性查询失败", {std::move(issue)}));
    };
    // Analytic primitive records describe the solid; their compatibility shells
    // are proxies. A topology edit revokes this certificate transactionally.
    if (body.rep_kind != RepKind::ExactBRep || !body.analytic_mass_valid) {
        return failure(StatusCode::NotImplemented, diag_codes::kCoreOperationUnsupported,
            "query.mass_properties.support_gate", "质量属性查询失败：解析实体已被编辑或表示不受支持");
    }
    MassProperties props {};
    if (!try_primitive_analytic_mass_properties(body, props) ||
        !(props.volume > 0.0) || !(props.area > 0.0) ||
        !std::isfinite(props.volume) || !std::isfinite(props.area) ||
        !std::isfinite(props.centroid.x) || !std::isfinite(props.centroid.y) ||
        !std::isfinite(props.centroid.z) ||
        !std::all_of(props.inertia.begin(), props.inertia.end(),
                     [](Scalar value) { return std::isfinite(value); }) ||
        !(props.inertia[0] > 0.0) || !(props.inertia[4] > 0.0) || !(props.inertia[8] > 0.0)) {
        return failure(StatusCode::NumericalInstability, diag_codes::kQueryMassPropertiesFailure,
            "query.mass_properties.numeric", "质量属性查询失败：解析积分退化或超出数值范围");
    }
    return ok_result(props, state_->create_diagnostic("已从未编辑解析实体计算质量属性"));
}

Result<Scalar> QueryService::min_distance(BodyId lhs, BodyId rhs) const {
    const auto closest = closest_points(lhs, rhs);
    if (!closest.value) return error_result<Scalar>(closest.status, closest.diagnostic_id);
    return ok_result(closest.value->distance, closest.diagnostic_id);
}

}  // namespace axiom
