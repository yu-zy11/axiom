#include "axiom/ops/ops_services.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
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

Result<OpReport> boolean_op_fail_staged(std::shared_ptr<detail::KernelState> state,
                                        bool diagnostics,
                                        StatusCode st,
                                        std::string_view code,
                                        std::string message,
                                        std::string summary,
                                        BodyId lhs,
                                        BodyId rhs,
                                        const BooleanPrepStats* prep) {
    if (!diagnostics) {
        if (st == StatusCode::InvalidInput) {
            return detail::invalid_input_result<OpReport>(*state, code, std::move(message), std::move(summary));
        }
        return detail::failed_result<OpReport>(*state, st, code, std::move(message), std::move(summary));
    }
    const DiagnosticId diag = state->create_diagnostic(std::move(summary));
    append_boolean_stage_issue(*state, diag, diag_codes::kBoolStageCandidates,
                               "布尔早期退出：在候选/包围盒关系检查阶段已中止，未生成结果体",
                               {lhs.value, rhs.value});
    if (prep != nullptr) {
        append_boolean_prep_candidate_issue(*state, diag, lhs, rhs, *prep);
    }
    auto issue = detail::make_error_issue(code, std::move(message), {lhs.value, rhs.value});
    set_boolean_diagnostic_stage(issue, code);
    state->append_diagnostic_issue(diag, std::move(issue));
    return error_result<OpReport>(st, diag);
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
    if (profile.label.empty() || distance <= 0.0 || !valid_axis(direction)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "拉伸失败：轮廓不能为空，方向必须有效且距离必须大于 0", "拉伸失败");
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
            return detail::invalid_input_result<BodyId>(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "拉伸失败：polygon 轮廓点数不足（至少 3 个点）", "拉伸失败");
        }
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
        for (const auto& p : profile.polygon_xyz) {
            extend(p);
            extend(detail::add_point_vec(p, detail::scale(dir, distance)));
        }
        if (!bbox.is_valid) {
            return detail::invalid_input_result<BodyId>(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "拉伸失败：polygon 轮廓点非法，无法形成有效包围盒", "拉伸失败");
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
                const auto ta = 0.5 * detail::norm(cp);
                if (ta <= 1e-30) {
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
    return ok_result(make_body(state_, record, "已完成拉伸"), state_->create_diagnostic("已完成拉伸"));
}

Result<BodyId> SweepService::revolve(const ProfileRef& profile, const Axis3& axis, Scalar angle) {
    if (profile.label.empty() || angle <= 0.0 || !valid_axis(axis.direction)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "旋转失败：轮廓不能为空，旋转轴必须有效且角度必须大于 0", "旋转失败");
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
            return detail::invalid_input_result<BodyId>(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "旋转失败：polygon 轮廓点数不足（至少 3 个点）", "旋转失败");
        }
        record.revolve_profile_xyz = profile.polygon_xyz;
        const auto ct = std::cos(angle);
        const auto st = std::sin(angle);
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
            extend_point(p);
            extend_point(rotate_point_around_unit_axis(p, O, u, ct, st));
        }
        if (!bbox.is_valid) {
            return detail::invalid_input_result<BodyId>(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "旋转失败：polygon 轮廓无法形成有效包围盒", "旋转失败");
        }
        record.bbox = bbox;
        record.b = angle;
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
    return ok_result(make_body(state_, record, "已完成旋转"), state_->create_diagnostic("已完成旋转"));
}

Result<BodyId> SweepService::sweep(const ProfileRef& profile, CurveId rail) {
    if (profile.label.empty() || !detail::has_curve(*state_, rail)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreInvalidHandle,
            "扫描失败：轮廓为空或导轨曲线不存在", "扫描失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "sweep:" + profile.label;
    const auto& curve = state_->curves.at(rail.value);
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
    if (profiles.size() < 2 || std::any_of(profiles.begin(), profiles.end(), [](const ProfileRef& profile) { return profile.label.empty(); })) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "放样失败：至少需要两个有效截面", "放样失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "loft";
    // minimal loft bbox: size scales with profile count
    const auto extent = std::max<Scalar>(2.0, static_cast<Scalar>(profiles.size()));
    record.bbox = detail::make_bbox({0.0, 0.0, 0.0}, {extent, extent, extent});
    record.a = extent * extent * extent;
    return ok_result(make_body(state_, record, "已完成放样"), state_->create_diagnostic("已完成放样"));
}

Result<BodyId> SweepService::thicken(FaceId face_id, Scalar distance) {
    if (state_->faces.find(face_id.value) == state_->faces.end() || distance <= 0.0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kModShellFailure,
            "加厚失败：目标面不存在或厚度非法", "加厚失败");
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Sweep;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "thicken";
    append_unique_face(record.source_faces, face_id);
    append_shells_for_face(*state_, record.source_shells, face_id);
    auto bbox = face_bbox(*state_, face_id);
    if (!bbox.is_valid) {
        bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});
    }
    record.b = distance;
    const auto face_area_est = bbox_max_axis_rectangle_area(bbox);
    if (face_area_est > 1e-30) {
        record.extrude_poly_cap_area = face_area_est;
        record.a = face_area_est * distance;
    }
    record.bbox = offset_bbox(bbox, distance);
    return ok_result(make_body(state_, record, "已完成加厚"), state_->create_diagnostic("已完成加厚"));
}

BooleanService::BooleanService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<OpReport> BooleanService::run(BooleanOp op, BodyId lhs, BodyId rhs, const BooleanOptions& boolean_options) {
    if (!detail::has_body(*state_, lhs) || !detail::has_body(*state_, rhs)) {
        return boolean_op_fail_staged(state_, boolean_options.diagnostics, StatusCode::InvalidInput,
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
                return boolean_op_fail_staged(state_, boolean_options.diagnostics, StatusCode::OperationFailed,
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
                return boolean_op_fail_staged(state_, boolean_options.diagnostics, StatusCode::OperationFailed,
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

    BodyId output = make_body(state_, record, "已完成布尔操作");
    detail::invalidate_eval_for_bodies(*state_, {lhs, rhs});
    const auto diag = boolean_options.diagnostics ? state_->create_diagnostic("布尔操作完成") : DiagnosticId {};
    append_boolean_stage_issue(*state_, diag, diag_codes::kBoolStageCandidates,
                               "布尔候选构建阶段开始：已进入壳/区域级候选统计流程",
                               {lhs.value, rhs.value});
    const auto face_candidates = build_face_candidates_for_boolean(*state_, lhs, rhs);
    append_boolean_face_candidate_issue(*state_, diag, lhs, rhs, face_candidates.size());
    const auto intersection_curves = compute_intersection_curves_for_candidates(*state_, face_candidates);
    append_boolean_intersection_curve_issue(*state_, diag, lhs, rhs, intersection_curves);
    const auto intersection_segments = clip_intersection_lines_to_face_overlap(*state_, intersection_curves);
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
                               "布尔输出占位物化完成：最小 owned topology / 回退链路已执行",
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
            if (use_cylinder) {
                method = "cylinder_point_classification";
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
            state_->diagnostics[diag.value].issues.push_back(
                detail::make_warning_issue(warning.code, warning.message));
        }
    }

    return ok_result(make_report(StatusCode::Ok, output, diag, warnings), diag);
}

Result<void> BooleanService::export_boolean_prep_stats(BodyId lhs, BodyId rhs, std::string_view path) const {
    if (!detail::has_body(*state_, lhs) || !detail::has_body(*state_, rhs) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kBoolInvalidInput,
            "布尔预处理统计导出失败：输入实体无效或输出路径为空", "布尔预处理统计导出失败");
    }
    const auto stats = compute_boolean_prep_stats(*state_, lhs, rhs);
    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kBoolInvalidInput,
            "布尔预处理统计导出失败：无法打开输出文件", "布尔预处理统计导出失败");
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
    return ok_void(state_->create_diagnostic("已导出布尔预处理统计"));
}

ModifyService::ModifyService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<OpReport> ModifyService::offset_body(BodyId body_id, Scalar distance, const TolerancePolicy&) {
    if (!detail::has_body(*state_, body_id) || distance == 0.0) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kModOffsetInvalid,
            "偏置失败：目标实体不存在或偏置距离不能为 0", "偏置失败");
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    if (distance < 0.0 && std::abs(distance) * 2.0 >= bbox_min_extent(record.bbox)) {
        return detail::failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kModOffsetSelfIntersection,
            "偏置失败：负偏置过大，结果会产生自交或完全塌缩", "偏置失败");
    }
    record.kind = detail::BodyKind::Modified;
    record.label = "offset";
    append_unique_body(record.source_bodies, body_id);
    record.bbox = offset_bbox(record.bbox, distance);
    const auto output = make_body(state_, record, "已完成偏置");
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("偏置操作完成");
    return ok_result(make_report(StatusCode::Ok, output, diag), diag);
}

Result<OpReport> ModifyService::shell_body(BodyId body_id, std::span<const FaceId> removed_faces, Scalar thickness) {
    if (!detail::has_body(*state_, body_id) || thickness <= 0.0) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kModShellFailure,
            "抽壳失败：目标实体不存在或壁厚必须大于 0", "抽壳失败");
    }
    if (!removed_faces.empty() && !valid_face_ids(*state_, removed_faces)) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kModShellFailure,
            "抽壳失败：待移除面集合包含无效面", "抽壳失败");
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    const auto min_extent = bbox_min_extent(record.bbox);
    if (thickness * 2.0 >= min_extent) {
        return detail::failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kModShellFailure,
            "抽壳失败：壁厚过大，内部空腔将塌缩", "抽壳失败");
    }
    const auto cavity_margin = min_extent - thickness * 2.0;
    if (cavity_margin <= detail::resolve_linear_tolerance(0.0, state_->config.tolerance)) {
        return detail::failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kModShellFailure,
            "抽壳失败：壁厚逼近容差阈值，内部空腔不稳定", "抽壳失败");
    }
    record.kind = detail::BodyKind::Modified;
    record.label = "shell";
    append_unique_body(record.source_bodies, body_id);
    for (const auto face_id : removed_faces) {
        append_unique_face(record.source_faces, face_id);
        append_shells_for_face(*state_, record.source_shells, face_id);
    }
    record.bbox = offset_bbox(record.bbox, -thickness * 0.5);
    const auto output = make_body(state_, record, "已完成抽壳");
    ValidationService validation {state_};
    const auto validation_result = validation.validate_all(output, ValidationMode::Standard);
    if (validation_result.status != StatusCode::Ok) {
        // Roll back just-created result body to keep modify failure side-effect free.
        state_->bodies.erase(output.value);
        detail::rebuild_topology_links(*state_);
        return detail::failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kModShellValidateFailed,
            "抽壳失败：结果校验未通过，已执行回滚", "抽壳失败");
    }
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("抽壳操作完成");
    return ok_result(make_report(StatusCode::Ok, output, diag), diag);
}

Result<OpReport> ModifyService::draft_faces(BodyId body_id, std::span<const FaceId> faces, const Vec3& pull_dir, Scalar angle) {
    if (!detail::has_body(*state_, body_id) || angle == 0.0 || !valid_axis(pull_dir) || !valid_face_ids(*state_, faces)) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "拔模失败：目标实体不存在、面集合无效、拉拔方向无效或角度不能为 0", "拔模失败");
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
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kModReplaceFaceIncompatible,
            "替换面失败：目标实体、目标面或替换曲面无效", "替换面失败");
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
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kModDeleteFaceHealFailure,
            "删除面补面失败：目标实体或目标面无效", "删除面补面失败");
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
    state_->append_diagnostic_issue(diag,
                                    detail::make_warning_issue(diag_codes::kHealFeatureRemovedWarning, "局部面删除后执行了补面简化"));
    return ok_result(report, diag);
}

BlendService::BlendService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<OpReport> BlendService::fillet_edges(BodyId body_id, std::span<const EdgeId> edges, Scalar radius) {
    if (!detail::has_body(*state_, body_id) || edges.empty() || radius <= 0.0 || !valid_edge_ids(*state_, edges)) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kBlendParameterTooLarge,
            "圆角失败：目标实体无效、边集合为空、边句柄无效或半径非法", "圆角失败");
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    if (radius * 2.0 >= bbox_min_extent(record.bbox)) {
        return detail::failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kBlendParameterTooLarge,
            "圆角失败：圆角半径过大，超过局部可容纳特征尺寸", "圆角失败");
    }
    record.kind = detail::BodyKind::BlendResult;
    record.label = "fillet";
    append_unique_body(record.source_bodies, body_id);
    for (const auto edge_id : edges) {
        append_faces_for_edge(*state_, record.source_faces, edge_id);
    }
    for (const auto face_id : record.source_faces) {
        append_shells_for_face(*state_, record.source_shells, face_id);
    }
    const auto output = make_body(state_, record, "已完成圆角");
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("圆角操作完成");
    std::vector<Warning> blend_warnings;
    blend_warnings.push_back(detail::make_warning(
        diag_codes::kBlendApproximatePlaceholder,
        "圆角：当前为拓扑占位与参数门禁，工业级滚球/角区/变半径未实现"));
    if (edges.size() > 1) {
        auto corner_issue = detail::make_warning_issue(
            diag_codes::kBlendMultiEdgeCornerPlaceholder,
            "圆角：一次处理多条边时角区/连续滚球/变半径未实现，仍为拓扑占位");
        corner_issue.related_entities = {body_id.value, output.value};
        state_->append_diagnostic_issue(diag, std::move(corner_issue));
        blend_warnings.push_back(detail::make_warning(
            diag_codes::kBlendMultiEdgeCornerPlaceholder,
            "圆角：多条边同时处理时角区与工业滚球未实现"));
    }
    return ok_result(make_report(StatusCode::Ok, output, diag, std::move(blend_warnings)), diag);
}

Result<OpReport> BlendService::chamfer_edges(BodyId body_id, std::span<const EdgeId> edges, Scalar distance) {
    if (!detail::has_body(*state_, body_id) || edges.empty() || distance <= 0.0 || !valid_edge_ids(*state_, edges)) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kBlendInvalidTarget,
            "倒角失败：目标实体无效、边集合为空、边句柄无效或距离非法", "倒角失败");
    }
    auto record = state_->bodies[body_id.value];
    detach_owned_topology(*state_, record);
    if (distance >= bbox_min_extent(record.bbox)) {
        return detail::failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kBlendParameterTooLarge,
            "倒角失败：倒角距离过大，超过局部可容纳特征尺寸", "倒角失败");
    }
    record.kind = detail::BodyKind::BlendResult;
    record.label = "chamfer";
    append_unique_body(record.source_bodies, body_id);
    for (const auto edge_id : edges) {
        append_faces_for_edge(*state_, record.source_faces, edge_id);
    }
    for (const auto face_id : record.source_faces) {
        append_shells_for_face(*state_, record.source_shells, face_id);
    }
    const auto output = make_body(state_, record, "已完成倒角");
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("倒角操作完成");
    std::vector<Warning> blend_warnings;
    blend_warnings.push_back(detail::make_warning(
        diag_codes::kBlendApproximatePlaceholder,
        "倒角：当前为拓扑占位与参数门禁，工业级角区/变距几何未实现"));
    if (edges.size() > 1) {
        auto corner_issue = detail::make_warning_issue(
            diag_codes::kBlendMultiEdgeCornerPlaceholder,
            "倒角：一次处理多条边时角区/连续倒角/变距未实现，仍为拓扑占位");
        corner_issue.related_entities = {body_id.value, output.value};
        state_->append_diagnostic_issue(diag, std::move(corner_issue));
        blend_warnings.push_back(detail::make_warning(
            diag_codes::kBlendMultiEdgeCornerPlaceholder,
            "倒角：多条边同时处理时角区与工业倒角未实现"));
    }
    return ok_result(make_report(StatusCode::Ok, output, diag, std::move(blend_warnings)), diag);
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

Result<MeshId> QueryService::section(BodyId body_id, const Plane& plane) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_result<MeshId>(
            *state_, StatusCode::InvalidInput, diag_codes::kQuerySectionFailure,
            "截面计算失败：目标实体不存在", "截面计算失败");
    }

    const auto& body = state_->bodies[body_id.value];
    if (!intersects_plane(body.bbox, plane)) {
        return detail::failed_result<MeshId>(
            *state_, StatusCode::OperationFailed, diag_codes::kQuerySectionFailure,
            "截面计算失败：截面平面与目标体包围盒不相交", "截面计算失败");
    }

    const auto mesh_id = MeshId {state_->allocate_id()};
    detail::MeshRecord mesh;
    mesh.source_body = body_id;
    mesh.label = "section";
    mesh.bbox = body.bbox;
    mesh.vertices = bbox_corners(body.bbox);
    mesh.indices = {0, 1, 2, 0, 2, 3};
    state_->meshes.emplace(mesh_id.value, std::move(mesh));
    return ok_result(mesh_id, state_->create_diagnostic("已完成截面计算"));
}

Result<MassProperties> QueryService::mass_properties(BodyId body_id) const {
    const auto it = state_->bodies.find(body_id.value);
    if (it == state_->bodies.end()) {
        return detail::failed_result<MassProperties>(
            *state_, StatusCode::InvalidInput, diag_codes::kQueryMassPropertiesFailure,
            "质量属性计算失败：目标实体不存在", "质量属性计算失败");
    }

    MassProperties props {};
    const auto& body = it->second;
    switch (body.kind) {
        case detail::BodyKind::Box:
        case detail::BodyKind::Wedge:
        case detail::BodyKind::Sphere:
        case detail::BodyKind::Cylinder:
        case detail::BodyKind::Cone:
        case detail::BodyKind::Torus:
            if (!try_primitive_analytic_mass_properties(body, props)) {
                props = bbox_mass_properties(body.bbox);
            }
            break;
        case detail::BodyKind::Sweep:
            (void)try_sweep_body_mass_properties(body, props);
            break;
        case detail::BodyKind::Modified:
            if (body.source_bodies.size() == 1 && body.label == "replace_face" && body.bbox.is_valid) {
                const auto src_it = state_->bodies.find(body.source_bodies[0].value);
                if (src_it != state_->bodies.end()) {
                    const Scalar rlin = detail::resolve_linear_tolerance(0.0, state_->config.tolerance);
                    const Scalar meps = std::max(Scalar {1e-7}, rlin * Scalar {128});
                    if (bbox_corners_almost_equal(body.bbox, src_it->second.bbox, meps)) {
                        return mass_properties(body.source_bodies[0]);
                    }
                }
            }
            props = bbox_mass_properties(body.bbox);
            break;
        case detail::BodyKind::BooleanResult:
            if (!try_boolean_two_operand_mass_properties(*state_, body, props)) {
                props = bbox_mass_properties(body.bbox);
            }
            break;
        default:
            props = bbox_mass_properties(body.bbox);
            break;
    }

    return ok_result(props, state_->create_diagnostic("已完成质量属性计算"));
}

Result<Scalar> QueryService::min_distance(BodyId lhs, BodyId rhs) const {
    if (!detail::has_body(*state_, lhs) || !detail::has_body(*state_, rhs)) {
        return detail::failed_result<Scalar>(
            *state_, StatusCode::InvalidInput, diag_codes::kQueryClosestPointFailure,
            "最短距离计算失败：输入实体不存在", "最短距离计算失败");
    }
    const auto& lb = state_->bodies[lhs.value].bbox;
    const auto& rb = state_->bodies[rhs.value].bbox;
    const auto dx = interval_gap(lb.min.x, lb.max.x, rb.min.x, rb.max.x);
    const auto dy = interval_gap(lb.min.y, lb.max.y, rb.min.y, rb.max.y);
    const auto dz = interval_gap(lb.min.z, lb.max.z, rb.min.z, rb.max.z);
    return ok_result<Scalar>(std::sqrt(dx * dx + dy * dy + dz * dz), state_->create_diagnostic("已完成最短距离计算"));
}

}  // namespace axiom
