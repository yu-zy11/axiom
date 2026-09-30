#pragma once

#include <cstdint>
#include <memory>
#include <optional>
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

/// 曲面最近点全域搜索的终止原因。所有成功终止都覆盖完整有效参数域，
/// 包括多边形修剪的外环、孔边界、样条的每个非空结点片以及可解析证明的无界域。
enum class SurfaceClosestPointConvergence : std::uint8_t {
    DistanceTolerance = 0,
    ParameterTolerance = 1,
    /// 由解析投影及全域几何下界直接证明，不消耗数值求值预算。
    Analytic = 2
};

struct SurfaceClosestPointOptions {
    /// 模型长度单位下的全域最小距离上下界允许差；有限且非负。
    Scalar distance_tolerance {1e-8};
    /// 距离界不能提前证明时，候选参数矩形的 u/v 边长须分别细分到此阈值。
    Scalar parameter_tolerance {1e-4};
    /// 无缓存数值点值求值总预算；须至少为 5。解析终止不消耗此预算；预算耗尽时失败且不返回部分结果。
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
    /// 以 Bezier/BSpline/NURBS（含 Trimmed/Offset 包装）局部有理控制网凸包建立空间下界的参数片数量。
    /// 该计数可用于确认高阶曲面查询实际启用了随细分收紧的全域证书；解析路径为 0。
    std::uint32_t control_net_bound_patches {};
    /// 已由保守下界证明不可能改进当前最优值、因而未继续细分的参数片数量。
    std::uint32_t pruned_patches {};
    SurfaceClosestPointConvergence convergence {SurfaceClosestPointConvergence::DistanceTolerance};
    /// 数值搜索覆盖的原有有界域，或解析证明包含至少一个全域极小点的有限化参数域。
    Range2D effective_domain {};
    /// true 表示原始曲面含无界参数方向，`effective_domain` 由本次查询自动构造。
    bool domain_was_finiteized {false};
};

class CurveService {
public:
    explicit CurveService(std::shared_ptr<detail::KernelState> state);

    Result<CurveEvalResult> eval(CurveId curve_id, Scalar t, int deriv_order) const;
    Result<std::vector<CurveEvalResult>> eval_batch(CurveId curve_id, std::span<const Scalar> ts, int deriv_order) const;
    /// 无导数、无求值缓存的参数点查询；参数须位于曲线定义域。
    Result<Point3> point_at_parameter(CurveId curve_id, Scalar t) const;
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
    /// 有限参数区间的保守包围盒；允许递减或零宽区间，不写求值缓存。
    Result<BoundingBox> bbox(CurveId curve_id, Scalar t0, Scalar t1) const;
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
    /// 对完整参数域求全域最近点。Plane/Cylinder/Cone 等无界解析面及可解析的嵌套
    /// Offset 自动构造包含全域极小点的有限参数域并给出解析证书；它们不消耗数值预算。
    /// Bezier/BSpline/NURBS 使用随子片收紧的（有理）控制网凸包空间界；旋转/线性扫掠、
    /// 通用偏置及修剪包装执行确定性分支限界。样条按非空结点片覆盖，修剪多边形的外环和
    /// 孔边界均参与搜索；结果公开控制网界与剪枝计数作为预算/性能证据。预算耗尽、选项非法、
    /// 偏置退化/自交或数值界不可建立时失败，不返回部分结果且不写 surface eval 缓存。
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

/// 两条有界曲线的离散交点类型。重合区间不伪装成若干离散点，见
/// `CurveCurveIntersectionResult::overlaps`。
enum class CurveCurveIntersectionKind : std::uint8_t {
    /// 两条曲线在交点处具有非平行切向。
    Transverse = 0,
    /// 两条曲线在交点处切向平行或至少一侧速度退化。
    Tangent = 1,
    /// 交点位于任一请求参数区间的端点；优先于切向分类。
    Endpoint = 2,
};

struct CurveCurveIntersectionPoint {
    /// 两侧求值点的中点，单位为模型长度单位。
    Point3 point {};
    Scalar first_parameter {};
    Scalar second_parameter {};
    /// 两侧求值点间距；不大于请求的 `position_tolerance`。
    Scalar residual_distance {};
    CurveCurveIntersectionKind kind {CurveCurveIntersectionKind::Transverse};
};

/// 连续重合曲线段。两个区间均按参数递增顺序返回；`same_direction`
/// 描述参数递增时几何方向是否一致。
struct CurveCurveOverlap {
    Range1D first_interval {};
    Range1D second_interval {};
    bool same_direction {true};
    /// 已验证对应点的最大分离距离；解析分段直线路径通常为 0。
    Scalar maximum_separation {};
};

struct CurveCurveIntersectionOptions {
    /// 模型长度单位下的相交/重合判定容差；必须有限且大于零。
    Scalar position_tolerance {1e-8};
    /// 每条曲线自身参数单位下的细分终止宽度；必须有限且大于零。
    Scalar parameter_tolerance {1e-8};
    /// 归一化切向叉积阈值，用于区分横交和相切；范围为 [0, 1]。
    Scalar angular_tolerance {1e-7};
    /// 无缓存点值求值总预算；预算耗尽时失败且不返回部分结果。
    std::uint32_t max_evaluations {200000};
    /// 候选参数矩形处理预算；用于限制相切、近重合等困难输入。
    std::uint32_t max_subdivisions {100000};
    /// 未指定时使用曲线完整定义域；无限定义域（Line）必须显式指定有限区间。
    std::optional<Range1D> first_interval;
    std::optional<Range1D> second_interval;
};

struct CurveCurveIntersectionResult {
    std::vector<CurveCurveIntersectionPoint> points;
    std::vector<CurveCurveOverlap> overlaps;
    std::uint32_t evaluations {};
    std::uint32_t parameter_rectangles_processed {};
};

class GeometryIntersectionService {
public:
    explicit GeometryIntersectionService(std::shared_ptr<detail::KernelState> state);

    /// 查询两条曲线在有限参数区间内的全部离散交点与连续重合段。
    /// Line/LineSegment/CompositePolyline 的各线性参数段使用解析 3D 求交；
    /// Bezier、B-spline、NURBS、圆锥曲线和 CompositeChain 按连续参数片进行
    /// 保守包围、确定性细分及阻尼 Gauss-Newton 精化。样条满重数断点两侧独立处理。
    /// 空交集是成功的空结果；非法句柄/区间、退化数值界或预算耗尽返回结构化失败，
    /// 不返回部分结果，也不写曲线求值缓存、拓扑、Intersection 存储或事务状态。
    /// 同一曲线同参区间及分段直线共线覆盖返回 `overlaps`；一般高阶曲线的连续
    /// 重合仅在记录可证明相同且参数对应一致时返回，避免把密集离散命中误报为重合。
    Result<CurveCurveIntersectionResult> intersect_curve_curve(
        CurveId first_curve, CurveId second_curve,
        const CurveCurveIntersectionOptions& options = {}) const;

    // Minimal intersection service for Stage 2/3: analytic pairs first (Line/Segment/Circle with Plane/Sphere/Cylinder).
    Result<std::vector<CurveSurfaceIntersection>> intersect_curve_surface(CurveId curve_id, SurfaceId surface_id) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

}  // namespace axiom
