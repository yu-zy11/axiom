#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
}

class CurveFactory {
public:
    explicit CurveFactory(std::shared_ptr<detail::KernelState> state);

    Result<CurveId> make_line(const Point3& origin, const Vec3& direction);
    Result<CurveId> make_line_segment(const Point3& a, const Point3& b);
    Result<CurveId> make_circle(const Point3& center, const Vec3& normal, Scalar radius);
    Result<CurveId> make_ellipse(const Point3& center, const Vec3& axis_u, const Vec3& axis_v);
    Result<CurveId> make_parabola(const Point3& origin, const Vec3& axis_u, const Vec3& axis_v, Scalar focal_param);
    Result<CurveId> make_hyperbola(const Point3& origin, const Vec3& axis_u, const Vec3& axis_v, Scalar a, Scalar b);
    Result<CurveId> make_bezier(std::span<const Point3> poles);
    Result<CurveId> make_bspline(const BSplineCurveDesc& desc);
    Result<CurveId> make_nurbs(const NURBSCurveDesc& desc);
    Result<CurveId> make_composite_polyline(std::span<const Point3> poles);
    // Composite curve chain (Stage 2 minimal): concatenate child curves into a single parameter domain [0, n].
    // Each child occupies one unit interval; evaluation maps t -> (child_index, local_t).
    Result<CurveId> make_composite_chain(std::span<const CurveId> children);

private:
    std::shared_ptr<detail::KernelState> state_;
};

class PCurveFactory {
public:
    explicit PCurveFactory(std::shared_ptr<detail::KernelState> state);

    // Minimal pcurve support for Stage 2: polyline in UV space.
    Result<PCurveId> make_polyline(std::span<const Point2> poles);

private:
    std::shared_ptr<detail::KernelState> state_;
};

class SurfaceFactory {
public:
    explicit SurfaceFactory(std::shared_ptr<detail::KernelState> state);

    Result<SurfaceId> make_plane(const Point3& origin, const Vec3& normal);
    Result<SurfaceId> make_cylinder(const Point3& origin, const Vec3& axis, Scalar radius);
    Result<SurfaceId> make_cone(const Point3& apex, const Vec3& axis, Scalar semi_angle);
    Result<SurfaceId> make_sphere(const Point3& center, Scalar radius);
    Result<SurfaceId> make_torus(const Point3& center, const Vec3& axis, Scalar major_r, Scalar minor_r);
    Result<SurfaceId> make_bezier(std::span<const Point3> poles);
    Result<SurfaceId> make_bspline(const BSplineSurfaceDesc& desc);
    Result<SurfaceId> make_nurbs(const NURBSSurfaceDesc& desc);
    // Stage 2 minimal: surfaces required by docs (7.1).
    Result<SurfaceId> make_revolved(CurveId generatrix, const Axis3& axis, Scalar sweep_angle_radians);
    Result<SurfaceId> make_swept_linear(CurveId profile, const Vec3& direction, Scalar sweep_length);
    Result<SurfaceId> make_trimmed(SurfaceId base_surface, Scalar u_min, Scalar u_max, Scalar v_min, Scalar v_max);
    /// 轴对齐盒 + UV 平面闭合折线环（≥3 点，有限）；用于 PCurve/外环驱动的真实修剪（Geo 侧不含 Topo 遍历）。
    Result<SurfaceId> make_trimmed_polygon(SurfaceId base_surface, Scalar u_min, Scalar u_max, Scalar v_min,
                                         Scalar v_max, std::span<const Point2> uv_boundary_loop);
    /// 外环 + 若干内环（孔）：有效 UV 为「外环内且不在任一内环内」；与 Topo PCurve 外/内环语义对齐的 Geo 最小实现。
    Result<SurfaceId> make_trimmed_polygon_with_holes(
        SurfaceId base_surface, Scalar u_min, Scalar u_max, Scalar v_min, Scalar v_max,
        std::span<const Point2> uv_outer_loop,
        const std::vector<std::vector<Point2>> &uv_holes);
    Result<SurfaceId> make_offset(SurfaceId base_surface, Scalar offset_distance);

private:
    std::shared_ptr<detail::KernelState> state_;
};

struct CurveLengthOptions {
    Scalar absolute_tolerance {1e-9};  // 模型长度单位，有限且非负。
    Scalar relative_tolerance {1e-10}; // 无量纲，有限且非负；两种容差不可同时为零。
    std::uint32_t max_evaluations {100000}; // 整次查询（含复合链）的速度求值预算，须 > 0。
};

/// 曲线最近点全域搜索的终止原因。成功结果始终覆盖完整有效参数域；
/// `DistanceTolerance` 给出距离上下界差，`ParameterTolerance` 给出仍可能改进区间的最大参数宽度。
enum class CurveClosestPointConvergence : std::uint8_t {
    Analytic,
    DistanceTolerance,
    ParameterTolerance
};

struct CurveClosestPointOptions {
    /// 模型长度单位下的全域最小距离上下界允许差；有限且非负。
    Scalar distance_tolerance {1e-9};
    /// 无法由距离界提前证明时，每个仍可能改进的参数区间须细分到此宽度；有限且大于零。
    Scalar parameter_tolerance {1e-9};
    /// 点值求值总预算（解析曲线不消耗该预算）；至少为 3。
    std::uint32_t max_evaluations {100000};
};

struct CurveClosestPointResult {
    Scalar parameter {};
    Point3 point {};
    Scalar distance {};
    /// 全域最小距离的保守下界；与 `distance` 的差不超过请求容差时以距离容差收敛。
    Scalar distance_lower_bound {};
    /// 以参数容差终止时，所有未由距离界排除区间的最大宽度；其他终止类型为 0。
    Scalar parameter_uncertainty {};
    std::uint32_t evaluations {};
    std::uint32_t intervals_processed {};
    CurveClosestPointConvergence convergence {CurveClosestPointConvergence::Analytic};
};

/// 有界曲面最近点全域搜索的终止原因。两种成功终止都覆盖完整有效参数域，
/// 包括多边形修剪的外环、孔边界以及样条的每个非空结点片。
enum class SurfaceClosestPointConvergence : std::uint8_t {
    DistanceTolerance,
    ParameterTolerance
};

struct SurfaceClosestPointOptions {
    /// 模型长度单位下的全域最小距离上下界允许差；有限且非负。
    Scalar distance_tolerance {1e-8};
    /// 距离界不能提前证明时，候选参数矩形的 u/v 边长须分别细分到此阈值。
    Scalar parameter_tolerance {1e-4};
    /// 无缓存点值求值总预算；须至少为 5。预算耗尽时失败且不返回部分结果。
    std::uint32_t max_evaluations {250000};
};

struct SurfaceClosestPointResult {
    Scalar u {};
    Scalar v {};
    Point3 point {};
    Scalar distance {};
    /// 完整有效域上最小距离的保守下界。
    Scalar distance_lower_bound {};
    /// 参数容差终止时仍未由距离界排除矩形的最大 u/v 边长；其他终止类型为 0。
    Scalar u_uncertainty {};
    Scalar v_uncertainty {};
    std::uint32_t evaluations {};
    std::uint32_t patches_processed {};
    SurfaceClosestPointConvergence convergence {SurfaceClosestPointConvergence::DistanceTolerance};
};

class CurveService {
public:
    explicit CurveService(std::shared_ptr<detail::KernelState> state);

    Result<CurveEvalResult> eval(CurveId curve_id, Scalar t, int deriv_order) const;
    Result<std::vector<CurveEvalResult>> eval_batch(CurveId curve_id, std::span<const Scalar> ts, int deriv_order) const;
    /// 对完整有效域执行确定性分支限界搜索；样条逐个非空结点段覆盖，满重数断点两侧独立参与。
    /// 预算耗尽、选项非法或数值范围不可表示时失败且不返回部分结果，也不写求值缓存。
    Result<CurveClosestPointResult> closest_point_detailed(
        CurveId curve_id, const Point3& point,
        const CurveClosestPointOptions& options = {}) const;
    Result<Scalar> closest_parameter(CurveId curve_id, const Point3& point) const;
    Result<std::vector<Scalar>> closest_parameters_batch(CurveId curve_id, std::span<const Point3> points) const;
    Result<Point3> closest_point(CurveId curve_id, const Point3& point) const;
    Result<std::vector<Point3>> closest_points_batch(CurveId curve_id, std::span<const Point3> points) const;
    Result<Range1D> domain(CurveId curve_id) const;
    /// 弧长，单位为模型长度单位；直线/圆/折线使用解析计算，其他已支持曲线使用导数数值积分。
    /// 全域 Line 无有限长度；区间重载支持 Line，端点可反向，有限域外参数不钳制。
    /// Chain 与 eval 一致：每个子曲线使用局部参数 [0,1]，不计不连续连接处的跳跃距离。
    /// 支持类型的零区间/常值曲线返回 0；不支持类型、非法参数或溢出返回失败且无值。
    Result<Scalar> length(CurveId curve_id) const;
    Result<Scalar> length(CurveId curve_id, Scalar t0, Scalar t1) const;
    /// 椭圆/抛物线/双曲线/Bezier/BSpline/NURBS 共享自适应积分；容差为误差估计目标，非严格误差界。
    /// 按非空结点区间积分，不计不连续结点的跳跃距离；常值曲线返回 0。
    /// 预算耗尽、精度停滞或非有限速度返回 OperationFailed，无部分长度；查询不写求值缓存。
    Result<Scalar> length(CurveId curve_id, const CurveLengthOptions& options) const;
    Result<Scalar> length(CurveId curve_id, Scalar t0, Scalar t1, const CurveLengthOptions& options) const;
    Result<BoundingBox> bbox(CurveId curve_id) const;
    Result<std::vector<BoundingBox>> bbox_batch(std::span<const CurveId> curve_ids) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class PCurveService {
public:
    explicit PCurveService(std::shared_ptr<detail::KernelState> state);

    Result<PCurveEvalResult> eval(PCurveId pcurve_id, Scalar t, int deriv_order) const;
    /// UV 折线逐段投影求最近参数；重复控制点按零长度段处理，等距时取最早参数。
    Result<Scalar> closest_parameter(PCurveId pcurve_id, const Point2& point) const;
    Result<Point2> closest_point(PCurveId pcurve_id, const Point2& point) const;
    Result<Range1D> domain(PCurveId pcurve_id) const;
    Result<BoundingBox> bbox(PCurveId pcurve_id) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class SurfaceService {
public:
    explicit SurfaceService(std::shared_ptr<detail::KernelState> state);

    Result<SurfaceEvalResult> eval(SurfaceId surface_id, Scalar u, Scalar v, int deriv_order) const;
    Result<std::vector<SurfaceEvalResult>> eval_batch(
        SurfaceId surface_id, std::span<const std::pair<Scalar, Scalar>> uvs, int deriv_order) const;
    /// 对完整有界参数域执行确定性分支限界搜索。Bezier/BSpline/NURBS、旋转/线性扫掠、
    /// 偏置及修剪包装均受支持；样条按非空结点片覆盖，修剪多边形的外环和孔边界均参与搜索。
    /// 无限参数域须先修剪；预算耗尽、选项非法或数值界不可建立时失败，不写 surface eval 缓存。
    Result<SurfaceClosestPointResult> closest_point_detailed(
        SurfaceId surface_id, const Point3& point,
        const SurfaceClosestPointOptions& options = {}) const;
    Result<Point3> closest_point(SurfaceId surface_id, const Point3& point) const;
    Result<std::vector<Point3>> closest_points_batch(SurfaceId surface_id, std::span<const Point3> points) const;
    /// 有界复杂曲面复用 `closest_point_detailed` 的完整参数域搜索和默认预算。
    Result<std::pair<Scalar, Scalar>> closest_uv(SurfaceId surface_id, const Point3& point) const;
    Result<std::vector<std::pair<Scalar, Scalar>>> closest_uv_batch(
        SurfaceId surface_id, std::span<const Point3> points) const;
    Result<Range2D> domain(SurfaceId surface_id) const;
    Result<BoundingBox> bbox(SurfaceId surface_id) const;
    Result<std::vector<BoundingBox>> bbox_batch(std::span<const SurfaceId> surface_ids) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class GeometryTransformService {
public:
    explicit GeometryTransformService(std::shared_ptr<detail::KernelState> state);

    Result<CurveId> transform_curve(CurveId curve_id, const Transform3& transform);
    Result<SurfaceId> transform_surface(SurfaceId surface_id, const Transform3& transform);

private:
    std::shared_ptr<detail::KernelState> state_;
};

struct CurveSurfaceIntersection {
    Point3 point{};
    Scalar curve_t{0.0};
    Scalar surface_u{0.0};
    Scalar surface_v{0.0};
};

class GeometryIntersectionService {
public:
    explicit GeometryIntersectionService(std::shared_ptr<detail::KernelState> state);

    // Minimal intersection service for Stage 2/3: analytic pairs first (Line/Segment/Circle with Plane/Sphere/Cylinder).
    Result<std::vector<CurveSurfaceIntersection>> intersect_curve_surface(CurveId curve_id, SurfaceId surface_id) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

}  // namespace axiom
