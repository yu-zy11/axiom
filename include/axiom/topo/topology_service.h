#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
struct TopologyCancellationState;
struct TopologyTransactionState;
}

/// 拓扑事务隔离/并发语义：当前内核实例强制单活动写事务 + 快照回滚，不等价于数据库 SERIALIZABLE，但可用于宿主侧协议对齐。
enum class TopologyIsolationLevel : std::uint8_t {
  Unspecified = 0,
  /// 单活动写事务、修改前快照、回滚恢复（工程上接近「串行化」使用方式，但无跨进程锁）。
  SnapshotSerializable = 1,
};

/// 可在宿主工作流与拓扑事务之间共享的协作式取消令牌。
/// 请求取消本身不访问内核存储；事务在显式轮询、下一次写入或提交边界观察并回滚。
class TopologyCancellationToken {
public:
    TopologyCancellationToken() = default;

    /// 默认构造的令牌不可取消；由 `TopologyCancellationSource::token()` 创建的令牌返回 true。
    bool can_be_cancelled() const noexcept;
    /// 线程安全地读取共享取消请求；不触发事务回滚。
    bool is_cancellation_requested() const noexcept;

private:
    explicit TopologyCancellationToken(
        std::shared_ptr<detail::TopologyCancellationState> state);

    std::shared_ptr<detail::TopologyCancellationState> state_;

    friend class TopologyCancellationSource;
    friend class TopologyTransaction;
};

/// 拓扑协作式取消源。副本共享同一信号；首次请求返回 true，后续幂等请求返回 false。
class TopologyCancellationSource {
public:
    TopologyCancellationSource();

    TopologyCancellationToken token() const noexcept;
    bool request_cancellation() noexcept;
    bool is_cancellation_requested() const noexcept;

private:
    std::shared_ptr<detail::TopologyCancellationState> state_;
};

/// 单个内核实例内协作式取消的累计审计；普通提交/回滚不计入。
struct TopologyCancellationMetrics {
    /// 在预取消构造、轮询、写入、提交或显式回滚边界首次观察到取消的事务数。
    std::uint64_t observed_transaction_count{};
    /// 已取得写者槽且因取消恢复快照并关闭的事务数（包含空事务）。
    std::uint64_t rolled_back_transaction_count{};
    /// 各次取消回滚前成功写操作数之和。
    std::uint64_t rolled_back_write_operations_total{};
    /// 最近一次取消回滚前的成功写操作数。
    std::uint64_t last_rolled_back_write_operations{};
};

/// 拓扑事务内保存点的不可伪造句柄；默认构造值无效，且只能交回创建它的活动事务。
class TopologySavepoint {
public:
    TopologySavepoint() = default;
    bool is_valid() const noexcept {
        return transaction_cookie_ != 0 && sequence_ != 0;
    }

private:
    TopologySavepoint(std::uint64_t transaction_cookie,
                      std::uint64_t sequence)
        : transaction_cookie_(transaction_cookie), sequence_(sequence) {}

    std::uint64_t transaction_cookie_{};
    std::uint64_t sequence_{};

    friend class TopologyTransaction;
};

/// 同一内核实例内保存点操作的累计审计。事务最终提交或回滚不会改写既有保存点审计。
struct TopologySavepointMetrics {
    std::uint64_t created_count{};
    std::uint64_t rollback_count{};
    std::uint64_t released_count{};
    /// 回滚到外层保存点时一并作废的更内层保存点数量。
    std::uint64_t discarded_nested_count{};
    /// 各次局部回滚丢弃的成功拓扑写操作数之和。
    std::uint64_t rolled_back_write_operations_total{};
    std::uint64_t last_rolled_back_write_operations{};
};

/// 同一面不同边界环之间的首个几何冲突类别。
/// 有限 Line/LineSegment、CompositePolyline 和纯线性 CompositeChain 使用解析
/// 分段判定；携带显式 trim 区间的圆锥曲线、Bezier、B-spline、NURBS 与混合
/// CompositeChain 使用误差受控求交。
enum class FaceBoundaryConflictKind : std::uint8_t {
    ProperIntersection = 0,
    EndpointTouch = 1,
    CollinearOverlap = 2,
    NearContact = 3,
    /// 非线性曲线存在连续重合参数区间。
    CurveOverlap = 4,
};

/// 跨环边界冲突的可查询证据。`first_point` / `second_point` 是两条有限边上的对应点；
/// 解析精确相交或重叠时 `distance` 为 0，误差受控曲线求交时为求解残差，
/// 容差邻近时为有限正值。
struct FaceBoundaryConflict {
    FaceBoundaryConflictKind kind {FaceBoundaryConflictKind::ProperIntersection};
    LoopId first_loop {};
    LoopId second_loop {};
    EdgeId first_edge {};
    EdgeId second_edge {};
    Point3 first_point {};
    Point3 second_point {};
    Scalar distance {};
    /// true 表示冲突由误差受控曲线求交器给出；false 表示解析线性谓词。
    bool error_controlled {false};
    /// 曲线求交使用的位置容差；解析线性谓词为 0。
    Scalar solver_tolerance {};
    /// 曲线求交的实际无缓存点值求值数与候选参数矩形数；解析路径均为 0。
    std::uint32_t curve_evaluations {};
    std::uint32_t parameter_rectangles_processed {};
};

/// 拓扑边在其支撑三维曲线上的有向裁剪区间。
/// `start_parameter` 对应边的 `v0`，`end_parameter` 对应 `v1`；允许递减区间。
struct EdgeCurveInterval {
    Scalar start_parameter {};
    Scalar end_parameter {};
};

/// 实体中闭壳相对于实体材料区域的角色。角色由严格几何包含深度决定，与壳自身绕向无关。
enum class BodyShellRole : std::uint8_t {
    /// 偶数包含深度：外部材料边界，或位于空腔中的独立材料岛。
    Material = 0,
    /// 奇数包含深度：从最近材料区域扣除的封闭空腔边界。
    Void = 1,
};

/// `body_shell_regions` 返回的稳定壳空间关系。
struct BodyShellRegion {
    ShellId shell {};
    BodyShellRole role {BodyShellRole::Material};
    /// 严格包含该壳的祖先壳数量；0 表示最外层独立材料分量。
    std::uint32_t nesting_depth {};
    /// 最近的直接包含壳；最外层壳为空。
    std::optional<ShellId> parent_shell;
};

/// 相对于实体材料的点/线段位置；空腔内部属于 Outside。
enum class BodyPointLocation : std::uint8_t {
    Outside = 0,
    Inside = 1,
    Boundary = 2,
};

struct BodySpatialQueryOptions {
    /// 模型长度单位；0 使用当前内核线性容差，负数/非有限值失败。
    Scalar position_tolerance {};
    /// 完成拓扑/壳层级前置检查之后的三角形距离、求交和绕数计算总预算。
    /// 截面还计交段扫描，体间距离按三角形对计；triangle_tests 返回相同口径的实际工作量。
    /// 前置检查与已有质量属性查询相同，不计入此预算；0 非法，耗尽失败且无部分结果。
    std::uint64_t max_triangle_tests {1000000};
};

struct BodyBoundaryPoint {
    Point3 point {};
    ShellId shell {};
    FaceId face {};
    /// 非负欧氏距离，单位为模型长度单位；最近位置可能在面内、边或顶点。
    Scalar distance {};
};

struct BodyPointQuery {
    BodyPointLocation location {BodyPointLocation::Outside};
    /// 空实体为空；非空实体始终返回最近真实边界，包含空腔/材料岛边界。
    /// 等距时按 ShellId、FaceId 选择，和实体壳插入顺序无关。
    std::optional<BodyBoundaryPoint> nearest_boundary;
    Scalar position_tolerance {};
    std::uint64_t triangle_tests {};
};

struct BodySegmentInterval {
    /// p(t) = start + t * (end - start)，0 <= t <= 1；端点包含在区间内。
    Range1D parameters {};
    /// Inside 表示开区间内部为材料，端点可在边界；Boundary 表示共面边界段或孤立接触点。
    BodyPointLocation location {BodyPointLocation::Inside};
};

struct BodySegmentQuery {
    /// 有序、内部不重叠的材料/边界区间；相邻同类区间合并，Outside 区间省略。
    /// 相切点以 [t,t] 表示，已经被 Inside 或 Boundary 区间包含的接触点不重复返回。
    std::vector<BodySegmentInterval> intervals;
    /// 区间内部为材料的总长度；共面边界段和孤立接触点不计入。
    Scalar material_length {};
    Scalar position_tolerance {};
    std::uint64_t triangle_tests {};
};

/// 平面与实体材料闭集的交集；坐标为世界坐标，面积为模型长度单位的平方。
/// triangles 覆盖真实截面区域（含空腔/孔），接触线/点单独保留，不赋予虚构面积。
struct BodyPlaneSection {
    std::vector<Point3> vertices;
    std::vector<std::array<int, 3>> triangles;
    /// 真实边界面片与平面的交段；可重合，共面面片包含三角化内部边。
    std::vector<std::array<Point3, 2>> boundary_segments;
    std::vector<Point3> contact_points;
    BoundingBox bbox {};
    Scalar area {};
    std::uint64_t triangle_tests {};
};

/// 两个非空实体材料闭集的最近位置；相交/包含/相切时 distance=0。
/// FaceId/ShellId 为零表示见证点在该实体材料内部；正距离时两侧均属真实边界。
struct BodyDistanceQuery {
    Scalar distance {};
    Point3 first_point {}, second_point {};
    FaceId first_face {}, second_face {};
    ShellId first_shell {}, second_shell {};
    std::uint64_t triangle_tests {};
};

class TopologyQueryService {
public:
    explicit TopologyQueryService(std::shared_ptr<detail::KernelState> state);

    Result<std::array<VertexId, 2>> vertices_of_edge(EdgeId edge_id) const;
    Result<std::vector<CoedgeId>> coedges_of_edge(EdgeId edge_id) const;
    Result<std::vector<LoopId>> loops_of_edge(EdgeId edge_id) const;
    Result<std::vector<FaceId>> faces_of_edge(EdgeId edge_id) const;
    Result<std::vector<ShellId>> shells_of_edge(EdgeId edge_id) const;
    Result<std::vector<EdgeId>> edges_of_loop(LoopId loop_id) const;
    /// 按环上 coedge 顺序返回每条边的起点顶点（闭合环长度为 coedge 数）。
    Result<std::vector<VertexId>> vertices_of_loop(LoopId loop_id) const;
    Result<std::vector<LoopId>> loops_of_face(FaceId face_id) const;
    Result<SurfaceId> surface_of_face(FaceId face_id) const;
    Result<std::vector<ShellId>> shells_of_face(FaceId face_id) const;
    Result<std::vector<BodyId>> bodies_of_face(FaceId face_id) const;
    Result<std::vector<FaceId>> source_faces_of_face(FaceId face_id) const;
    Result<std::vector<FaceId>> faces_of_shell(ShellId shell_id) const;
    Result<std::vector<BodyId>> bodies_of_shell(ShellId shell_id) const;
    Result<std::vector<ShellId>> source_shells_of_shell(ShellId shell_id) const;
    Result<std::vector<FaceId>> source_faces_of_shell(ShellId shell_id) const;
    Result<std::vector<ShellId>> shells_of_body(BodyId body_id) const;
    Result<std::vector<BodyId>> source_bodies_of_body(BodyId body_id) const;
    Result<std::vector<ShellId>> source_shells_of_body(BodyId body_id) const;
    Result<std::vector<FaceId>> source_faces_of_body(BodyId body_id) const;
    Result<TopologySummary> summary_of_shell(ShellId shell_id) const;
    Result<TopologySummary> summary_of_body(BodyId body_id) const;
    Result<std::uint64_t> edge_count_of_loop(LoopId loop_id) const;
    Result<std::uint64_t> loop_count_of_face(FaceId face_id) const;
    Result<std::uint64_t> face_count_of_shell(ShellId shell_id) const;
    Result<std::uint64_t> shell_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> coedge_count_of_edge(EdgeId edge_id) const;
    Result<std::uint64_t> owner_count_of_edge(EdgeId edge_id) const;
    Result<std::uint64_t> owner_count_of_face(FaceId face_id) const;
    Result<std::uint64_t> owner_count_of_shell(ShellId shell_id) const;
    Result<bool> has_vertex(VertexId vertex_id) const;
    Result<bool> has_edge(EdgeId edge_id) const;
    Result<bool> has_loop(LoopId loop_id) const;
    Result<bool> has_face(FaceId face_id) const;
    Result<bool> has_shell(ShellId shell_id) const;
    Result<bool> has_body(BodyId body_id) const;
    Result<bool> is_edge_boundary(EdgeId edge_id) const;
    Result<bool> is_edge_non_manifold(EdgeId edge_id) const;
    Result<bool> is_face_orphan(FaceId face_id) const;
    Result<bool> is_shell_orphan(ShellId shell_id) const;
    Result<bool> is_body_derived(BodyId body_id) const;
    /// 返回边的显式曲线裁剪区间；由旧 `create_edge` 创建、未携带区间的边成功返回空 optional。
    Result<std::optional<EdgeCurveInterval>> edge_curve_interval(EdgeId edge_id) const;
    Result<BoundingBox> bbox_of_face(FaceId face_id) const;
    /// 平面、直线边面片的真实边界面积（外环减内环），单位为模型长度单位的平方。
    /// 不支持曲边/非平面面；无效面返回失败且无数值。每次从当前拓扑重算，不缓存。
    Result<Scalar> planar_face_area(FaceId face_id) const;
    /// 由完整折线 PCurve 修剪环计算解析曲面面积（外环减内环），单位为模型长度单位的平方。
    /// 支持 Plane/Cylinder/Cone/Sphere/Torus 及其 Trimmed/Offset 包装；未包装 Plane 无 PCurve 时兼容回退 `planar_face_area`。
    /// UV 环必须连续闭合、位于当前参数域且内环严格位于外环内；退化/自交/重叠或缺失 PCurve 不返回部分面积。
    /// 每次从当前面、环与曲面记录重算，不写几何求值/网格缓存；事务内替换/删除即时可见，回滚后恢复。
    Result<Scalar> face_area(FaceId face_id) const;
    /// 从单个闭合多面体壳的当前真实拓扑计算均匀密度质量属性；仅支持平面、直线边面（可凹、可带孔）。
    /// `volume`/`area`/`centroid` 的单位分别为模型长度单位的三次方、平方和一次方；
    /// `inertia` 是关于质心、世界坐标系行主序的 3x3 张量，密度取 1，单位为模型长度单位的五次方。
    /// 壳须为双边流形闭壳，面边界绕向须与平面法向一致；曲面、曲边、开壳、非流形或退化壳失败且不返回部分值。
    /// 兼容占位面（含解析 primitive 的代理面）不能用于积分；重新组壳也不改变该限制。
    /// 失败阶段为 query.mass_properties.{support_gate,preflight,numeric}，不返回部分数值。
    /// 每次从当前拓扑重算，不创建网格或写缓存；事务内删除/替换即时可见，回滚后恢复。
    Result<MassProperties> shell_mass_properties(ShellId shell_id) const;
    /// 查询实体闭壳的严格空间包含层级。互不相交的最外层壳是独立材料分量；奇数深度壳为空腔，偶数深度壳为材料岛。
    /// 壳面相交、重叠或在容差内接触会返回 InvalidTopology，不给出部分层级；空实体成功返回空集合。
    /// 当前与质量属性相同，仅支持平面、直线边的双边流形闭壳。每次重算，不写缓存，事务修改与回滚即时可见。
    Result<std::vector<BodyShellRegion>> body_shell_regions(BodyId body_id) const;
    /// 汇总实体一个或多个闭合多面体壳的均匀密度质量属性。
    /// 独立最外层壳相加，奇数包含深度空腔相减，偶数深度材料岛再相加；壳接触/相交/重叠失败。
    /// `area` 为所有材料/空腔边界面积之和；其余单位及失败/只读语义同 `shell_mass_properties`。
    /// 受支持体类不拥有壳时返回 InvalidTopology/query.mass_properties.empty_gate；不从 bbox 或来源记录恢复质量。
    Result<MassProperties> body_mass_properties(BodyId body_id) const;
    /// 平面直边、多面体实体的真实材料定位与最近边界；支持凹面、孔、多壳、空腔、材料岛。
    /// 仅 ExactBRep 的 box/wedge、已物化真实多面体 Sweep 和用户建立的 Generic 闭壳；
    /// 解析曲面体及旧占位建模体返回 NotImplemented/query.closest_point.support_gate，不能使用 bbox 壳代替。
    /// 与 shell_mass_properties/body_shell_regions 共用闭壳/面/壳间接触前置检查；输入须为无自交的嵌入闭壳。
    /// 最近边界距离 <= position_tolerance 时为 Boundary，否则按闭壳包含奇偶判断 Inside/Outside。
    /// 非有限坐标、无效预算/容差、数值溢出失败；空实体成功返回 Outside 和空 nearest_boundary。
    /// 不创建几何/网格、不写求值缓存或 Eval 状态；当前事务修改即时可见，回滚后恢复。
    Result<BodyPointQuery> locate_point(
        BodyId body_id, const Point3& point, const BodySpatialQueryOptions& options = {}) const;
    /// 对有限线段执行真实面片裁剪，返回材料开区间、共面边界段和孤立相切点，参数为无量纲。
    /// 支持范围/前置条件/只读语义同 locate_point；不将 position_tolerance 膨胀为材料厚度。
    /// position_tolerance 用于短段退化判断：长度 <= 该容差的线段失败；壳间检查仍使用内核建模容差。
    /// 求交和共面判断使用局部浮点舍入尺度；可分辨的窄空腔不会按位置容差合并。
    /// 不同边界事件在归一化参数舍入尺度内无法可靠分离时返回 NumericalInstability，无部分区间。
    /// 空实体或完全在材料外且不接触边界的线段成功返回空集合和零长度。
    Result<BodySegmentQuery> clip_segment(
        BodyId body_id, const Point3& start, const Point3& end,
        const BodySpatialQueryOptions& options = {}) const;
    /// 当前平面直边嵌入闭壳的实际平面截面；支持凹面、孔、多壳、空腔和材料岛。
    /// 体类支持范围与 locate_point 相同；包含可验证的 box/wedge、真实 Sweep 及 Generic 闭壳。
    /// 对采样 revolve/sweep/loft 查询已物化的多面体，不宣称连续曲面的解析截面。
    /// 共面面包含在截面中；线/点相切成功返回零面积，空交集成功返回空结果。
    /// 位置容差不把近邻平面吸附到边界；距离符号或不同事件无法可靠分辨时 query.section.numeric 失败。
    /// 不用 bbox 替代解析曲面或占位建模体；不支持输入 NotImplemented，稳定 Issue.stage。
    /// 每次重算、无 MeshId/缓存/Eval/事务写入；预算耗尽或不可分辨事件失败，无部分结果。
    Result<BodyPlaneSection> section(
        BodyId body_id, const Plane& plane, const BodySpatialQueryOptions& options = {}) const;
    /// 对同一支持范围计算实际材料距离；包含需按壳奇偶判定，bbox 重叠不代表相交。
    /// 空实体无有限最近位置，DegenerateGeometry/query.distance.empty_gate；不按位置容差膨胀实体。
    /// 等距按壳/面 ID 稳定选择；只读和预算语义同 section，正距离单位为模型长度单位。
    /// 接触求交不使用线段裁剪的舍入带或调用者位置容差膨胀实体；可表示的正间隙保持正距离。
    Result<BodyDistanceQuery> closest_points(
        BodyId lhs, BodyId rhs, const BodySpatialQueryOptions& options = {}) const;
    /// 显式裁剪边按支撑曲线区间计算真实弧长；兼容旧 Line/LineSegment 边的端点距离。
    /// 曲边缺少裁剪区间返回 NotImplemented；区间、端点或曲线不一致返回 InvalidTopology。
    Result<Scalar> edge_length(EdgeId edge_id) const;
    /// 按闭合环的 coedge 累加边长，不受方向影响；空环/不闭合环失败且无值。
    Result<Scalar> loop_length(LoopId loop_id) const;
    /// 外环加全部内环的边界长度（不是外环减内环）；不要求支撑曲面为平面。
    /// 每次重算，不写几何/网格缓存；事务内修改即时可见，回滚后恢复。
    Result<Scalar> face_boundary_length(FaceId face_id) const;
    Result<BoundingBox> bbox_of_shell(ShellId shell_id) const;
    Result<BoundingBox> bbox_of_body_from_topology(BodyId body_id) const;
    Result<std::vector<FaceId>> faces_of_body(BodyId body_id) const;
    Result<std::vector<LoopId>> loops_of_body(BodyId body_id) const;
    Result<std::vector<EdgeId>> edges_of_body(BodyId body_id) const;
    Result<std::vector<VertexId>> vertices_of_body(BodyId body_id) const;
    Result<std::uint64_t> face_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> loop_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> edge_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> vertex_count_of_body(BodyId body_id) const;
    Result<bool> body_has_face(BodyId body_id, FaceId face_id) const;
    Result<bool> shell_has_face(ShellId shell_id, FaceId face_id) const;
    Result<bool> face_has_loop(FaceId face_id, LoopId loop_id) const;
    Result<bool> loop_has_edge(LoopId loop_id, EdgeId edge_id) const;
    Result<bool> edge_has_vertex(EdgeId edge_id, VertexId vertex_id) const;
    Result<std::uint64_t> shared_face_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> shared_edge_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> boundary_edge_count_of_body(BodyId body_id) const;
    Result<std::uint64_t> non_manifold_edge_count_of_body(BodyId body_id) const;
    Result<bool> is_body_topology_empty(BodyId body_id) const;
    Result<PCurveId> pcurve_of_coedge(CoedgeId coedge_id) const;
    /// Trim bridge：外环每条 coedge 均须绑定有效 PCurve；在 PCurve 控制点（折线顶点）上求 UV 轴对齐包围盒。
    /// 语义对齐 `validate_face_trim_consistency` 所用 UV（与 `SurfaceService::closest_uv(face_surface, …)` 同参空间）。
    Result<Range2D> face_outer_loop_uv_bounds(FaceId face_id) const;
    /// 外环 PCurve（折线）按 coedge 顺序串联的 UV 折线（闭合环，已去重相邻重复点）；供 `make_trimmed_polygon`。
    Result<std::vector<Point2>> face_outer_loop_uv_polyline(FaceId face_id) const;
    /// 指定外环或内环：按 coedge 顺序串联 UV 折线（须为该 `face_id` 的 `outer_loop` 或 `inner_loops` 之一）。
    Result<std::vector<Point2>> face_loop_uv_polyline(FaceId face_id, LoopId loop_id) const;
    /// 返回该面引用曲面的「修剪基曲面」：`Trimmed` 则沿 `base_surface_id` 解引用直至非 Trimmed。
    Result<SurfaceId> underlying_surface_for_face_trim(FaceId face_id) const;

    /// 累计拓扑只读查询次数（自 `KernelState` 创建起；嵌套调用只计最外层一次）。与 `TopologyTransaction::write_operation_count` 互补，用于审计/可观测性。
    Result<std::uint64_t> query_operation_count() const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class TopologyTransaction {
public:
    /// 同一内核已有活动事务时，新事务以关闭状态返回；其写入/提交/回滚均失败且不污染模型。
    explicit TopologyTransaction(std::shared_ptr<detail::KernelState> state);
    /// 绑定协作式取消令牌。若令牌已取消，事务不占用写者槽并以已取消关闭状态返回。
    TopologyTransaction(std::shared_ptr<detail::KernelState> state,
                        TopologyCancellationToken cancellation_token);
    /// 事务为唯一所有权对象：可移动构造，但不可复制或移动赋值。
    /// 移动后源对象保持可析构、可查询的关闭状态，不能再提交或回滚。
    TopologyTransaction(TopologyTransaction&& other);
    TopologyTransaction& operator=(TopologyTransaction&&) = delete;
    TopologyTransaction(const TopologyTransaction&) = delete;
    TopologyTransaction& operator=(const TopologyTransaction&) = delete;
    /// 活动事务离开作用域时自动回滚；已提交、已回滚或移动后的源对象不改变模型。
    ~TopologyTransaction() noexcept;

    /// 坐标必须为有限值；否则返回 InvalidInput / AXM-CORE-E-0002，且不修改拓扑或事务写计数。
    Result<VertexId> create_vertex(const Point3& point);
    Result<EdgeId> create_edge(CurveId curve_id, VertexId v0, VertexId v1);
    /// 创建带显式曲线裁剪区间的边。参数必须位于曲线定义域，且区间两端求值分别与 v0/v1 在容差内一致。
    /// 失败发生在 EdgeId 分配前，不增加事务写计数，也不写几何求值缓存。
    Result<EdgeId> create_trimmed_edge(CurveId curve_id,
                                       Scalar start_parameter,
                                       Scalar end_parameter,
                                       VertexId v0, VertexId v1);
    Result<CoedgeId> create_coedge(EdgeId edge_id, bool reversed);
    Result<void> set_coedge_pcurve(CoedgeId coedge_id, PCurveId pcurve_id);
    /// 按定向端点 ID 首尾闭合；单共边不豁免。未闭合返回 InvalidTopology / AXM-TOPO-E-0002，不写入环或事务计数。
    Result<LoopId> create_loop(std::span<const CoedgeId> coedges);
    /// 已绑定面的外/内环至少三条共边；同曲线双弧环除外。边数不足分别返回 AXM-TOPO-E-0003/0004。
    /// 同一面各环不得复用 EdgeId；失败返回 InvalidTopology / AXM-TOPO-E-0014，不写入模型。
    /// 不同边界环不得共用 VertexId；失败返回 InvalidTopology / AXM-TOPO-E-0024，不分配 FaceId。
    Result<FaceId> create_face(SurfaceId surface_id, LoopId outer_loop, std::span<const LoopId> inner_loops);
    /// 成员面须引用存在的曲面，外环及所有内环须有效；受损引用返回 InvalidTopology / AXM-TOPO-E-0005，不分配 ShellId 或修改事务写计数。
    Result<ShellId> create_shell(std::span<const FaceId> faces);
    Result<BodyId> create_body(std::span<const ShellId> shells);
    Result<void> delete_face(FaceId face_id);
    Result<void> delete_shell(ShellId shell_id);
    Result<void> delete_body(BodyId body_id);
    Result<void> replace_surface(FaceId face_id, SurfaceId replacement);
    Result<VersionId> commit();
    Result<void> rollback();
    /// 捕获当前事务的拓扑与审计状态，建立可重复回滚的嵌套保存点。
    /// 当前实现为内存全拓扑快照，适合阶段性原子工作流；不会回收已分配对象 ID。
    Result<TopologySavepoint> create_savepoint();
    /// 恢复到指定保存点并保留该保存点供再次回滚；其后创建的内层保存点全部失效。
    /// 外部事务或已释放/失效句柄返回 OperationFailed / AXM-TX-E-0003，模型不变。
    Result<void> rollback_to_savepoint(TopologySavepoint savepoint);
    /// 按 LIFO 顺序释放最内层保存点并保留其后的模型修改；非最内层句柄失败且不修改模型。
    Result<void> release_savepoint(TopologySavepoint savepoint);
    Result<std::uint64_t> active_savepoint_count() const;
    /// 主动观察取消边界。已请求时，原子式恢复事务前模型、关闭事务并释放写者槽，
    /// 返回 OperationFailed / AXM-TX-E-0007；未请求时成功且不改变事务。
    Result<void> poll_cancellation();
    Result<bool> is_active() const;
    /// 只读查询共享令牌，不触发回滚；未绑定取消源时恒为 false。
    Result<bool> cancellation_requested() const;
    /// 是否已由预取消、轮询、写入口、提交或显式回滚观察到取消。
    Result<bool> cancellation_observed() const;
    /// 自动/显式取消回滚前的成功写次数；正常关闭或未观察取消时为 0。
    Result<std::uint64_t> cancelled_write_operation_count() const;
    Result<std::uint64_t> created_vertex_count() const;
    Result<std::uint64_t> created_edge_count() const;
    Result<std::uint64_t> created_coedge_count() const;
    Result<std::uint64_t> created_loop_count() const;
    Result<std::uint64_t> created_face_count() const;
    Result<std::uint64_t> created_shell_count() const;
    Result<std::uint64_t> created_body_count() const;
    Result<std::uint64_t> created_entity_count_total() const;
    Result<std::uint64_t> touched_face_count() const;
    Result<std::uint64_t> touched_shell_count() const;
    Result<std::uint64_t> touched_body_count() const;
    // Destructive ops in this transaction (for audit/metrics). Reset on rollback and clear_tracking_records.
    Result<std::uint64_t> deleted_face_count() const;
    Result<std::uint64_t> deleted_shell_count() const;
    Result<std::uint64_t> deleted_body_count() const;
    /// Successful `replace_surface` calls in this transaction (audit/metrics). Reset on rollback / clear_tracking_records.
    Result<std::uint64_t> replaced_surface_count() const;
    /// Successful `set_coedge_pcurve` with non-zero `PCurveId` (trim bridge bind). Reset on rollback / clear_tracking_records.
    Result<std::uint64_t> coedge_pcurve_bind_count() const;
    /// Successful `set_coedge_pcurve` to `PCurveId{}` after a non-zero binding was present (trim bridge clear). Reset on rollback / clear_tracking_records.
    Result<std::uint64_t> coedge_pcurve_clear_count() const;
    /// 本事务内每次**成功**的拓扑写操作各计 1（创建/删除实体、`replace_surface`、每次 `set_coedge_pcurve` 含清除）。提交后仍可查询；回滚或 `clear_tracking_records` 归零。
    Result<std::uint64_t> write_operation_count() const;
    /// 报告当前实现的有效隔离级别（见 `TopologyIsolationLevel` 注释）。
    Result<TopologyIsolationLevel> effective_isolation_level() const;
    Result<bool> has_created_vertex(VertexId vertex_id) const;
    Result<bool> has_created_edge(EdgeId edge_id) const;
    Result<bool> has_created_coedge(CoedgeId coedge_id) const;
    Result<bool> has_created_loop(LoopId loop_id) const;
    Result<bool> has_created_face(FaceId face_id) const;
    Result<bool> has_created_shell(ShellId shell_id) const;
    Result<bool> has_created_body(BodyId body_id) const;
    Result<bool> can_commit() const;
    Result<VersionId> preview_commit_version() const;
    Result<bool> has_snapshot_face(FaceId face_id) const;
    Result<bool> has_snapshot_shell(ShellId shell_id) const;
    Result<bool> has_snapshot_body(BodyId body_id) const;
    /// 仅在提交或回滚后清理审计/撤销记录，可重复调用且不改变模型。
    /// 活动事务（含空事务）返回 OperationFailed / AXM-TX-E-0006，保留全部跟踪记录。
    Result<void> clear_tracking_records();
    Result<std::vector<VertexId>> created_vertices() const;
    Result<std::vector<EdgeId>> created_edges() const;
    Result<std::vector<CoedgeId>> created_coedges() const;
    Result<std::vector<LoopId>> created_loops() const;
    Result<std::vector<FaceId>> created_faces() const;
    Result<std::vector<ShellId>> created_shells() const;
    Result<std::vector<BodyId>> created_bodies() const;

private:
    bool token_cancellation_requested() const noexcept;
    void record_cancellation_observed(bool rolled_back,
                                      std::uint64_t write_operations);
    std::optional<DiagnosticId> observe_cancellation(std::string_view operation);
    void restore_model_and_close();

    std::shared_ptr<detail::KernelState> state_;
    std::shared_ptr<detail::TopologyTransactionState> transaction_state_;
    TopologyCancellationToken cancellation_token_;
    std::vector<std::uint64_t> created_vertices_;
    std::vector<std::uint64_t> created_edges_;
    std::vector<std::uint64_t> created_coedges_;
    std::vector<std::uint64_t> created_loops_;
    std::vector<std::uint64_t> created_faces_;
    std::vector<std::uint64_t> created_shells_;
    std::vector<std::uint64_t> created_bodies_;
    std::uint64_t txn_deleted_faces_{0};
    std::uint64_t txn_deleted_shells_{0};
    std::uint64_t txn_deleted_bodies_{0};
    std::uint64_t txn_replaced_surfaces_{0};
    std::uint64_t txn_coedge_pcurve_binds_{0};
    std::uint64_t txn_coedge_pcurve_clears_{0};
    std::uint64_t txn_write_ops_{0};
    std::uint64_t cancelled_write_ops_{0};
    bool cancellation_observed_{false};
    bool active_ {true};
};

class TopologyValidationService {
public:
    explicit TopologyValidationService(std::shared_ptr<detail::KernelState> state);

    Result<void> validate_edge(EdgeId edge_id) const;
    Result<void> validate_vertex(VertexId vertex_id) const;
    Result<void> validate_coedge(CoedgeId coedge_id) const;
    Result<void> validate_loop(LoopId loop_id) const;
    /// 建面前返回候选外/内环间的首个边界冲突；无冲突为成功的空 optional。
    /// 线性分段使用解析谓词；显式裁剪圆锥曲线、样条与混合 CompositeChain
    /// 使用无缓存、受预算和误差约束的曲线求交。缺少必要 trim、数值界无法建立
    /// 或预算耗尽时失败且不返回部分结果。
    /// `linear_tolerance == 0` 使用内核容差策略，正值会按策略上下限钳制；负值或非有限值失败。
    /// 只读查询不会修改拓扑、反向索引或活动事务计数。
    Result<std::optional<FaceBoundaryConflict>> first_boundary_conflict(
        LoopId outer_loop, std::span<const LoopId> inner_loops,
        Scalar linear_tolerance = 0.0) const;
    // Trim bridge (Stage 2): validate that coedge pcurves in a loop are continuous and closed in UV space.
    // Requires every coedge in the loop to have a valid non-zero PCurveId.
    Result<void> validate_loop_pcurve_closedness(LoopId loop_id) const;
    // Trim bridge (Stage 2): face-level trim consistency.
    // Requires outer/inner loops to have complete coedge pcurves and each loop is UV-closed.
    Result<void> validate_face_trim_consistency(FaceId face_id) const;
    // Trim bridge (batch): validate trim for every face in a shell / body's owned shells.
    Result<void> validate_shell_trim_consistency(ShellId shell_id) const;
    Result<void> validate_body_trim_consistency(BodyId body_id) const;
    Result<void> validate_face(FaceId face_id) const;
    Result<void> validate_face_sources(FaceId face_id) const;
    Result<void> validate_shell(ShellId shell_id) const;
    // Strict closedness validation:
    // - kTopoOpenBoundary when any edge is used < 2 times within the shell
    // - kTopoNonManifoldEdge when any edge is used > 2 times within the shell
    // - kTopoLoopOrientationMismatch when the two coedges of a shared edge have the same direction
    // - kTopoShellDisconnected when paired faces form more than one connected component
    Result<void> validate_shell_closedness(ShellId shell_id) const;
    Result<void> validate_shell_sources(ShellId shell_id) const;
    Result<void> validate_body(BodyId body_id) const;
    Result<void> validate_body_closedness(BodyId body_id) const;
    Result<void> validate_body_sources(BodyId body_id) const;
    Result<void> validate_body_bbox(BodyId body_id) const;
    /// Per-body index checks (coedge_to_loop / loop_to_faces / edge_to_coedges / face_to_shells) for topology reachable from the body's shells only.
    Result<void> validate_body_topology_indices(BodyId body_id) const;
    Result<void> validate_indices_consistency() const;
    Result<void> validate_face_many(std::span<const FaceId> face_ids) const;
    Result<void> validate_shell_many(std::span<const ShellId> shell_ids) const;
    Result<void> validate_body_many(std::span<const BodyId> body_ids) const;
    Result<bool> is_face_valid(FaceId face_id) const;
    Result<bool> is_shell_valid(ShellId shell_id) const;
    Result<bool> is_body_valid(BodyId body_id) const;
    Result<std::uint64_t> count_invalid_faces(std::span<const FaceId> face_ids) const;
    Result<std::uint64_t> count_invalid_shells(std::span<const ShellId> shell_ids) const;
    Result<std::uint64_t> count_invalid_bodies(std::span<const BodyId> body_ids) const;
    Result<FaceId> first_invalid_face(std::span<const FaceId> face_ids) const;
    Result<ShellId> first_invalid_shell(std::span<const ShellId> shell_ids) const;
    Result<BodyId> first_invalid_body(std::span<const BodyId> body_ids) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class TopologyService {
public:
    explicit TopologyService(std::shared_ptr<detail::KernelState> state);

    TopologyTransaction begin_transaction();
    /// 创建绑定协作式取消信号的写事务；取消只在事务边界被观察，不异步访问模型。
    TopologyTransaction begin_transaction(
        const TopologyCancellationToken& cancellation_token);
    /// 是否有事务持有当前内核实例的唯一拓扑写者槽；只读且不观察取消。
    Result<bool> has_active_write_transaction() const;
    /// 返回自内核创建起的协作式取消累计审计；读取本身不创建或关闭事务。
    Result<TopologyCancellationMetrics> cancellation_metrics() const;
    /// 返回自内核创建起的保存点创建、局部回滚、释放和丢弃写入累计审计。
    Result<TopologySavepointMetrics> savepoint_metrics() const;
    TopologyQueryService& query();
    TopologyValidationService& validate();

private:
    std::shared_ptr<detail::KernelState> state_;
    TopologyQueryService query_service_;
    TopologyValidationService validation_service_;
};

}  // namespace axiom
