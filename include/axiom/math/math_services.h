#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <span>
#include <vector>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
}

/// 三维线代与度量；与 `PredicateService` 的有限性约定对齐：
/// - 点/向量坐标非有限时：`distance`/`squared_distance`/`manhattan_distance`/`norm`/`squared_norm` 与 `distance_point_to_line`/`distance_point_to_plane`/`distance_point_to_segment` 等度量返回 `+Inf`；
/// - `dot` 与 `cross`：操作数非有限或结果在浮点域内非有限时返回安静 `NaN`（向量返回三分量 `NaN`）；
/// - `centroid`/`average`：只要输入中有一项坐标非有限，则输出对应坐标为安静 `NaN`（空输入仍返回零向量/原点）；
/// - 标量体积/面积/混合积类返回安静 `NaN`（与 `tetrahedron_signed_volume` 一致）。
/// - `midpoint`/`lerp`：端点坐标或 `lerp` 的 `t` 非有限时，结果为安静 `NaN` 点。
/// - `angle_between`：任一向量坐标非有限时返回安静 `NaN`（与 `dot` 对齐）。
/// - `project`/`reject`：操作数非有限、或投影系数/结果非有限时返回安静 `NaN` 向量；`rhs` 近零（\(\|rhs\|^2\) 在机器 `epsilon` 以下）时 `project` 返回零向量以保持与历史退化语义一致。
/// - `is_near_zero`：`eps` 或向量坐标非有限时返回 `false`。
/// - `clamp_norm`：`value` 或 `max_norm` 非有限时返回安静 `NaN` 向量。
/// - `orthonormal_basis`：法向坐标非有限时返回 `std::nullopt`。
/// - `transform(Point3/Vec3)`：输入坐标或变换矩阵前 12 元素任一非有限，或 `long double` 累计结果非有限时，返回安静 `NaN` 坐标/向量（批量接口逐点沿用此规则）。
class LinearAlgebraService {
public:
    explicit LinearAlgebraService(std::shared_ptr<detail::KernelState> state);

    Scalar dot(const Vec3& lhs, const Vec3& rhs) const;
    Vec3 cross(const Vec3& lhs, const Vec3& rhs) const;
    Vec3 add(const Vec3& lhs, const Vec3& rhs) const;
    Vec3 subtract(const Vec3& lhs, const Vec3& rhs) const;
    Vec3 scale(const Vec3& value, Scalar factor) const;
    Vec3 hadamard(const Vec3& lhs, const Vec3& rhs) const;
    Scalar norm(const Vec3& value) const;
    Scalar squared_norm(const Vec3& value) const;
    Vec3 normalize(const Vec3& value) const;
    Scalar distance(const Point3& lhs, const Point3& rhs) const;
    Scalar squared_distance(const Point3& lhs, const Point3& rhs) const;
    Scalar manhattan_distance(const Point3& lhs, const Point3& rhs) const;
    Point3 midpoint(const Point3& lhs, const Point3& rhs) const;
    Point3 lerp(const Point3& lhs, const Point3& rhs, Scalar t) const;
    Scalar angle_between(const Vec3& lhs, const Vec3& rhs) const;
    Scalar scalar_triple_product(const Vec3& a, const Vec3& b, const Vec3& c) const;
    Scalar distance_point_to_line(const Point3& point, const Point3& line_origin, const Vec3& line_direction) const;
    Scalar distance_point_to_plane(const Point3& point, const Point3& plane_origin, const Vec3& plane_normal) const;
    /// 点 `point` 到闭线段 \([seg_a, seg_b]\) 的欧氏距离（最近点参数 \(t\in[0,1]\) 在 `seg_a + t(seg_b-seg_a)` 上取）。
    /// 任一端点或 `point` 坐标非有限、或中间量非有限时返回 `+Inf`；`seg_a` 与 `seg_b` 重合时退化为 `distance(point, seg_a)`。
    Scalar distance_point_to_segment(const Point3& point, const Point3& seg_a, const Point3& seg_b) const;
    Scalar triangle_area(const Point3& a, const Point3& b, const Point3& c) const;
    Scalar tetrahedron_signed_volume(const Point3& a, const Point3& b, const Point3& c, const Point3& d) const;
    Vec3 project(const Vec3& lhs, const Vec3& rhs) const;
    Vec3 reject(const Vec3& lhs, const Vec3& rhs) const;
    Vec3 clamp_norm(const Vec3& value, Scalar max_norm) const;
    std::optional<std::pair<Vec3, Vec3>> orthonormal_basis(const Vec3& normal) const;
    bool is_near_zero(const Vec3& value, Scalar eps) const;
    bool is_finite(const Vec3& value) const;
    Transform3 identity_transform() const;
    Transform3 compose(const Transform3& lhs, const Transform3& rhs) const;
    Transform3 make_translation(const Vec3& delta) const;
    Transform3 make_scale(Scalar sx, Scalar sy, Scalar sz) const;
    Transform3 make_rotation_axis_angle(const Vec3& axis, Scalar angle_rad) const;
    bool invert_affine(const Transform3& in, Transform3& out, Scalar eps) const;
    std::vector<Point3> transform_points(std::span<const Point3> points, const Transform3& transform) const;
    std::vector<Vec3> transform_vectors(std::span<const Vec3> vectors, const Transform3& transform) const;
    Point3 transform(const Point3& point, const Transform3& transform) const;
    Vec3 transform(const Vec3& vector, const Transform3& transform) const;
    Point3 centroid(std::span<const Point3> points) const;
    Vec3 average(std::span<const Vec3> vectors) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

/// 几何/拓扑谓词（符号、包围盒、近似的点-体关系等）。
///
/// **定向谓词 `orient2d` / `orient3d`**（工业语义约定，供全链路一致实现与测试固化）：
/// - 任一输入坐标非有限（NaN/Inf）→ `Sign::Uncertain`。
/// - 行列式在浮点运算中非有限（上溢等）→ `Sign::Uncertain`。
/// - 否则在 `|det| <= tol` 时 → `Sign::Zero`；其中 `tol = max(user_eps, scale_adaptive_eps)`，
///   `user_eps` 对无后缀重载为内核 **`ToleranceService::effective_linear(0)`**（与 `resolve_linear_tolerance(0, policy)` 一致），
///   对 `*_tol` 重载为调用方传入的 `eps`（负值按 0 处理；**`eps` 非有限时返回 `Uncertain`**）；`scale_adaptive_eps` 随边长尺度放大，抑制大坐标下的舍入噪声。
/// - `orient*_effective`：与 `orient*_tol` 相同，但门限由 `resolve_linear_tolerance(tolerance_requested, policy)` 合成（与 `point_equal_effective` 对齐，受 `min_local`/`max_local` 钳制）。
/// - 否则按 `det` 符号返回 `Positive` / `Negative`。
///
/// **向量平行/正交**：任一向量非有限 → 视为不满足（返回 `false`）。`*_effective` 将角度门限经 `resolve_angular_tolerance` 与策略合成（与 `ToleranceService::effective_angular` 一致）；**`angular_requested` 非有限时返回 `false`**（与 `vec_parallel`/`vec_orthogonal` 对非有限角度门限一致）。
///
/// **点坐标类谓词**（如 `point_in_sphere`、经 `point_in_bbox` 的路径）：点非有限 → `false`。
///
/// **`point_on_segment_*`**：与 `LinearAlgebraService::distance_point_to_segment` 同一几何距离；显式容差非有限 → `false`；`*_effective` 在请求非有限时亦返回 `false`（与 `point_equal_effective` 一致）。
class PredicateService {
public:
    explicit PredicateService(std::shared_ptr<detail::KernelState> state);

    Sign orient2d(const Point2& a, const Point2& b, const Point2& c) const;
    Sign orient3d(const Point3& a, const Point3& b, const Point3& c, const Point3& d) const;
    Sign orient2d_tol(const Point2& a, const Point2& b, const Point2& c, Scalar eps) const;
    Sign orient3d_tol(const Point3& a, const Point3& b, const Point3& c, const Point3& d, Scalar eps) const;
    /// `tolerance_requested` 经 `resolve_linear_tolerance` 与全局策略合成后作为 `orient2d_tol` 的 `eps`。
    /// **`tolerance_requested` 非有限时返回 `Sign::Uncertain`**（与 `orient*_tol` 对非有限 `eps` 一致；区别于裸 `resolve` 对 NaN 的回退语义）。
    Sign orient2d_effective(const Point2& a, const Point2& b, const Point2& c, Scalar tolerance_requested) const;
    /// 同上，供 3D 定向与 `point_equal_effective` 共用同一容差解析入口；**非有限请求 → `Uncertain`**。
    Sign orient3d_effective(const Point3& a, const Point3& b, const Point3& c, const Point3& d,
                            Scalar tolerance_requested) const;
    bool aabb_intersects(const BoundingBox& lhs, const BoundingBox& rhs, Scalar tolerance) const;
    bool point_in_bbox(const Point3& point, const BoundingBox& bbox, Scalar tolerance) const;
    /// 三轴绝对差与 `tolerance` 比较在实现中用 `long double`，大坐标下与 `squared_distance` 路径对齐。
    bool point_equal_tol(const Point3& lhs, const Point3& rhs, Scalar tolerance) const;
    /// 与 `point_equal_tol` 相同几何语义，但 `tolerance_requested` 经 `resolve_linear_tolerance` 与全局/策略合成（`<=0` 回退 `policy.linear` 并受 `min_local`/`max_local` 钳制）。
    /// **`tolerance_requested` 非有限时返回 `false`**（与 `point_equal_tol` 对非有限容差一致）。
    bool point_equal_effective(const Point3& lhs, const Point3& rhs, Scalar tolerance_requested) const;
    /// 点 `point` 到闭线段 \([seg_a, seg_b]\) 的欧氏距离是否不超过 `tolerance`（`tolerance` 非有限 → `false`）。
    bool point_on_segment_tol(const Point3& point, const Point3& seg_a, const Point3& seg_b,
                              Scalar tolerance) const;
    /// 同上，`tolerance_requested` 经 `resolve_linear_tolerance` 合成。
    bool point_on_segment_effective(const Point3& point, const Point3& seg_a, const Point3& seg_b,
                                    Scalar tolerance_requested) const;
    bool bbox_contains(const BoundingBox& outer, const BoundingBox& inner, Scalar tolerance) const;
    bool bbox_valid(const BoundingBox& bbox) const;
    std::optional<BoundingBox> bbox_intersection(const BoundingBox& lhs, const BoundingBox& rhs, Scalar tolerance) const;
    Scalar bbox_overlap_ratio(const BoundingBox& lhs, const BoundingBox& rhs, Scalar tolerance) const;
    bool bbox_center_in(const BoundingBox& inner, const BoundingBox& outer, Scalar tolerance) const;
    bool range1d_overlap(const Range1D& lhs, const Range1D& rhs, Scalar tolerance) const;
    bool range2d_overlap(const Range2D& lhs, const Range2D& rhs, Scalar tolerance) const;
    bool point_in_sphere(const Point3& point, const Point3& center, Scalar radius, Scalar tolerance) const;
    bool point_in_cylinder_approx(
        const Point3& point, const Point3& origin, const Vec3& axis, Scalar radius, Scalar height, Scalar tolerance) const;
    bool vec_parallel(const Vec3& lhs, const Vec3& rhs, Scalar angular_tolerance) const;
    bool vec_orthogonal(const Vec3& lhs, const Vec3& rhs, Scalar angular_tolerance) const;
    bool vec_parallel_effective(const Vec3& lhs, const Vec3& rhs, Scalar angular_requested) const;
    bool vec_orthogonal_effective(const Vec3& lhs, const Vec3& rhs, Scalar angular_requested) const;
    Result<bool> point_on_curve(const Point3& point, CurveId curve_id, Scalar tolerance) const;
    Result<std::vector<bool>> point_on_curve_batch(std::span<const Point3> points, CurveId curve_id, Scalar tolerance) const;
    Result<bool> point_on_surface(const Point3& point, SurfaceId surface_id, Scalar tolerance) const;
    Result<std::vector<bool>> point_on_surface_batch(std::span<const Point3> points, SurfaceId surface_id, Scalar tolerance) const;
    Result<bool> point_in_body(const Point3& point, BodyId body_id, Scalar tolerance) const;
    Result<std::vector<bool>> point_in_body_batch(std::span<const Point3> points, BodyId body_id, Scalar tolerance) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

/// 容差策略与解析入口。距离/角度类门控应通过 `effective_*` / `resolve_*_for_scale`（或内核内等价的
/// `resolve_*_tolerance` 规则）合成，以便 `min_local`/`max_local` 钳制在全链路一致生效。
class ToleranceService {
public:
    explicit ToleranceService(std::shared_ptr<detail::KernelState> state);

    TolerancePolicy global_policy() const;
    TolerancePolicy policy_for_body(BodyId body_id) const;
    TolerancePolicy override_policy(const TolerancePolicy& base, Scalar linear) const;
    TolerancePolicy clamp_policy(const TolerancePolicy& base) const;
    TolerancePolicy scale_policy(const TolerancePolicy& base, Scalar factor) const;
    TolerancePolicy scale_policy_for_body_nonlinear(const TolerancePolicy& base, BodyId body_id) const;
    TolerancePolicy merge_policy(const TolerancePolicy& primary, const TolerancePolicy& fallback) const;
    TolerancePolicy with_angular(const TolerancePolicy& base, Scalar angular) const;
    TolerancePolicy loosen_policy(const TolerancePolicy& base, Scalar factor) const;
    TolerancePolicy tighten_policy(const TolerancePolicy& base, Scalar factor) const;
    TolerancePolicy choose_body_or_global(BodyId body_id) const;
    Scalar effective_linear(Scalar requested) const;
    Scalar effective_angular(Scalar requested) const;
    Scalar normalize_linear_request(Scalar requested) const;
    Scalar normalize_angular_request(Scalar requested) const;
    /// `model_scale` 非有限时按极大尺度处理并由策略 `min_local`/`max_local` 钳制（避免内部 `std::max(NaN, …)` 未定义行为）。
    Scalar resolve_linear_for_scale(Scalar requested, Scalar model_scale) const;
    Scalar resolve_angular_for_scale(Scalar requested, Scalar model_scale) const;
    /// 三值比较：小于为 -1，相等为 0，大于为 +1。若 lhs、rhs 或传入的 tolerance 实参非有限，返回 0（与 `within_linear` 门控一致）。
    int compare_linear(Scalar lhs, Scalar rhs, Scalar tolerance) const;
    int compare_angular(Scalar lhs, Scalar rhs, Scalar tolerance) const;
    /// lhs 与 rhs 在 effective 线性容差内视为相等。lhs/rhs 非有限、或传入的 tolerance 实参非有限、或 effective 结果非有限，返回 false。
    bool within_linear(Scalar lhs, Scalar rhs, Scalar tolerance) const;
    bool within_angular(Scalar lhs, Scalar rhs, Scalar tolerance) const;
    /// 混合绝对/相对门控：`abs_requested` 经 `effective_linear` 解析；`rel_requested` 为显式相对系数（负值按 0）。任一数非有限则返回 false。
    bool nearly_equal_linear(Scalar lhs, Scalar rhs, Scalar abs_requested, Scalar rel_requested) const;
    /// 在 `nearly_equal_linear` 为真时返回 0；否则按数值比较返回 -1/+1。lhs/rhs 非有限返回 0。
    int compare_linear_rel_abs(Scalar lhs, Scalar rhs, Scalar abs_requested, Scalar rel_requested) const;
    /// 与 `nearly_equal_linear` 对称，绝对项经 `effective_angular` 解析。
    bool nearly_equal_angular(Scalar lhs, Scalar rhs, Scalar abs_requested, Scalar rel_requested) const;
    int compare_angular_rel_abs(Scalar lhs, Scalar rhs, Scalar abs_requested, Scalar rel_requested) const;
    bool is_valid_policy(const TolerancePolicy& policy) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

}  // namespace axiom
