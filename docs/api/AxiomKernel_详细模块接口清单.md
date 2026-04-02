# AxiomKernel 详细模块接口清单

本文档定义 `AxiomKernel` 的模块级接口清单，目标是把技术架构文档进一步细化到“可拆研发任务、可定义代码边界、可编写接口文档”的粒度。本文档不约束最终命名细节，但约束模块职责、接口方向、输入输出和错误语义。

## 1. 文档目标

本文档用于明确以下内容：

- 模块边界
- 核心对象与句柄
- 关键服务接口
- 标准输入输出结构
- 错误码和诊断对象
- 插件扩展边界

## 2. 设计约定

### 2.1 命名约定

- 几何对象使用 `Curve`、`Surface`、`Body` 等名词。
- 句柄类型统一使用 `Id` 后缀，例如 `BodyId`。
- 服务接口使用 `Service` 或模块域命名，例如 `BooleanService`。
- 只读视图使用 `View` 后缀。
- 结果对象统一使用 `Result` 后缀。

### 2.2 API 约定

- 所有可能失败的操作必须返回结构化结果，而不是仅返回 `bool`。
- 所有修改型操作必须支持事务化提交。
- 所有重量级操作必须能输出诊断信息。
- 核心模块默认无 UI 依赖、无日志打印副作用。

### 2.3 错误处理约定

- 逻辑性失败通过 `Result` 返回。
- 编程错误通过断言或内部错误对象暴露。
- 不允许使用“失败但静默返回空对象”的模式。

## 3. 核心基础类型

### 3.1 标量与坐标

```cpp
using Scalar = double;
using Index = uint32_t;
using VersionId = uint64_t;
```

### 3.2 几何基础结构

```cpp
struct Point2 { Scalar x, y; };
struct Point3 { Scalar x, y, z; };
struct Vec2   { Scalar x, y; };
struct Vec3   { Scalar x, y, z; };

struct Range1D {
  Scalar min;
  Scalar max;
};

struct Range2D {
  Range1D u;
  Range1D v;
};

struct BoundingBox {
  Point3 min;
  Point3 max;
  bool is_valid;
};
```

### 3.3 稳定句柄

```cpp
struct CurveId   { uint64_t value; };
struct SurfaceId { uint64_t value; };
struct PCurveId  { uint64_t value; };

struct VertexId  { uint64_t value; };
struct EdgeId    { uint64_t value; };
struct CoedgeId  { uint64_t value; };
struct LoopId    { uint64_t value; };
struct FaceId    { uint64_t value; };
struct ShellId   { uint64_t value; };
struct BodyId    { uint64_t value; };

struct MeshId         { uint64_t value; };
struct IntersectionId { uint64_t value; };
struct DiagnosticId   { uint64_t value; };
```

### 3.4 通用结果对象

```cpp
enum class StatusCode {
  Ok,
  InvalidInput,
  InvalidTopology,
  DegenerateGeometry,
  NumericalInstability,
  ToleranceConflict,
  NotImplemented,
  OperationFailed,
  InternalError
};

struct Warning {
  std::string code;
  std::string message;
};

template <typename T>
struct Result {
  StatusCode status;
  std::optional<T> value;
  std::vector<Warning> warnings;
  DiagnosticId diagnostic_id;
};
```

### 3.5 配置与通用选项

```cpp
struct KernelConfig {
  TolerancePolicy tolerance;
  PrecisionMode precision_mode;
  bool enable_diagnostics;
  bool enable_cache;
};

struct ImportOptions {
  bool run_validation;
  bool auto_repair;
};

struct ExportOptions {
  bool compatibility_mode;
  bool embed_metadata;
  bool write_mesh_validation_report;
};
```

策略组合与网格导出门禁口径见 `docs/quality/AxiomKernel_IO_导出策略矩阵.md`。

## 4. `MathCore` 接口清单

### 4.1 `LinearAlgebraService`

职责：

- 基础向量矩阵运算
- 仿射变换
- 坐标系变换

核心接口：

```cpp
class LinearAlgebraService {
public:
  Scalar dot(const Vec3&, const Vec3&) const;
  Vec3 cross(const Vec3&, const Vec3&) const;
  Scalar norm(const Vec3&) const;
  Vec3 normalize(const Vec3&) const;
  Scalar distance_point_to_segment(const Point3&, const Point3& seg_a, const Point3& seg_b) const;
  Point3 transform(const Point3&, const Transform3&) const;
  Vec3 transform(const Vec3&, const Transform3&) const;
};
```

### 4.2 `PredicateService`

职责：

- 鲁棒几何判定
- inside/outside 分类
- 定向量计算

核心接口：

```cpp
enum class Sign {
  Negative,
  Zero,
  Positive,
  Uncertain
};

class PredicateService {
public:
  Sign orient2d(const Point2&, const Point2&, const Point2&) const;
  Sign orient3d(const Point3&, const Point3&, const Point3&, const Point3&) const;
  Sign orient2d_effective(const Point2&, const Point2&, const Point2&, Scalar tolerance_requested) const;
  Sign orient3d_effective(const Point3&, const Point3&, const Point3&, const Point3&, Scalar tolerance_requested) const;
  bool point_equal_effective(const Point3&, const Point3&, Scalar tolerance_requested) const;
  bool point_on_segment_tol(const Point3&, const Point3&, const Point3&, Scalar tolerance) const;
  bool point_on_segment_effective(const Point3&, const Point3&, const Point3&, Scalar tolerance_requested) const;
  bool vec_parallel_effective(const Vec3&, const Vec3&, Scalar angular_requested) const;
  bool vec_orthogonal_effective(const Vec3&, const Vec3&, Scalar angular_requested) const;
  Result<bool> point_on_curve(const Point3&, CurveId, Scalar tol) const;
  Result<bool> point_on_surface(const Point3&, SurfaceId, Scalar tol) const;
  Result<bool> point_in_body(const Point3&, BodyId, Scalar tol) const;
};
```

### 4.3 `ToleranceService`

职责：

- 公差策略配置
- 局部公差查询
- 操作级公差覆盖

核心接口：

```cpp
enum class PrecisionMode {
  FastFloat,
  AdaptiveCertified,
  ExactCritical
};

struct TolerancePolicy {
  Scalar linear;
  Scalar angular;
  Scalar min_local;
  Scalar max_local;
  PrecisionMode precision_mode;
};

class ToleranceService {
public:
  TolerancePolicy global_policy() const;
  TolerancePolicy policy_for_body(BodyId) const;
  TolerancePolicy override_policy(const TolerancePolicy&, Scalar linear) const;
  bool nearly_equal_linear(Scalar lhs, Scalar rhs, Scalar abs_requested, Scalar rel_requested) const;
  int compare_linear_rel_abs(Scalar lhs, Scalar rhs, Scalar abs_requested, Scalar rel_requested) const;
  bool nearly_equal_angular(Scalar lhs, Scalar rhs, Scalar abs_requested, Scalar rel_requested) const;
  int compare_angular_rel_abs(Scalar lhs, Scalar rhs, Scalar abs_requested, Scalar rel_requested) const;
};
```

## 5. `GeoCore` 接口清单

### 5.1 几何创建接口

#### `CurveFactory`

```cpp
class CurveFactory {
public:
  Result<CurveId> make_line(const Point3& origin, const Vec3& direction);
  Result<CurveId> make_circle(const Point3& center, const Vec3& normal, Scalar radius);
  Result<CurveId> make_ellipse(const Point3& center, const Vec3& axis_u, const Vec3& axis_v);
  Result<CurveId> make_bezier(std::span<const Point3> poles);
  Result<CurveId> make_bspline(const BSplineCurveDesc&);
  Result<CurveId> make_nurbs(const NURBSCurveDesc&);
};
```

#### `SurfaceFactory`

```cpp
class SurfaceFactory {
public:
  Result<SurfaceId> make_plane(const Point3& origin, const Vec3& normal);
  Result<SurfaceId> make_cylinder(const Point3& origin, const Vec3& axis, Scalar radius);
  Result<SurfaceId> make_cone(const Point3& apex, const Vec3& axis, Scalar semi_angle);
  Result<SurfaceId> make_sphere(const Point3& center, Scalar radius);
  Result<SurfaceId> make_torus(const Point3& center, const Vec3& axis, Scalar major_r, Scalar minor_r);
  Result<SurfaceId> make_bspline(const BSplineSurfaceDesc&);
  Result<SurfaceId> make_nurbs(const NURBSSurfaceDesc&);
};
```

### 5.2 求值接口

#### `CurveEvaluator`

```cpp
struct CurveEvalResult {
  Point3 point;
  Vec3 tangent;
  std::vector<Vec3> derivatives;
};

class CurveService {
public:
  Result<CurveEvalResult> eval(CurveId, Scalar t, int deriv_order) const;
  Result<Scalar> closest_parameter(CurveId, const Point3&) const;
  Result<Point3> closest_point(CurveId, const Point3&) const;
  Result<Range1D> domain(CurveId) const;
  Result<BoundingBox> bbox(CurveId) const;
};
```

#### `SurfaceEvaluator`

```cpp
struct SurfaceEvalResult {
  Point3 point;
  Vec3 du;
  Vec3 dv;
  Vec3 normal;
  Scalar k1;
  Scalar k2;
};

class SurfaceService {
public:
  Result<SurfaceEvalResult> eval(SurfaceId, Scalar u, Scalar v, int deriv_order) const;
  Result<Point3> closest_point(SurfaceId, const Point3&) const;
  Result<std::pair<Scalar, Scalar>> closest_uv(SurfaceId, const Point3&) const;
  Result<Range2D> domain(SurfaceId) const;
  Result<BoundingBox> bbox(SurfaceId) const;
};
```

### 5.3 几何变换接口

```cpp
class GeometryTransformService {
public:
  Result<CurveId> transform_curve(CurveId, const Transform3&);
  Result<SurfaceId> transform_surface(SurfaceId, const Transform3&);
};
```

## 6. `TopoCore` 接口清单

### 6.1 拓扑只读查询接口

```cpp
class TopologyQueryService {
public:
  Result<std::array<VertexId, 2>> vertices_of_edge(EdgeId) const;
  Result<std::vector<EdgeId>> edges_of_loop(LoopId) const;
  Result<std::vector<LoopId>> loops_of_face(FaceId) const;
  Result<SurfaceId> surface_of_face(FaceId) const;
  Result<std::vector<FaceId>> faces_of_shell(ShellId) const;
  Result<std::vector<ShellId>> shells_of_body(BodyId) const;
  /// 累计只读查询次数（嵌套调用只计最外层一次）；与 `TopologyTransaction::write_operation_count` 互补。
  Result<std::uint64_t> query_operation_count() const;
};
```

### 6.2 拓扑事务接口

```cpp
enum class TopologyIsolationLevel : std::uint8_t {
  Unspecified = 0,
  SnapshotSerializable = 1,
};

class TopologyTransaction {
public:
  Result<VertexId> create_vertex(const Point3&);
  Result<EdgeId> create_edge(CurveId, VertexId, VertexId);
  Result<CoedgeId> create_coedge(EdgeId, bool reversed);
  Result<LoopId> create_loop(std::span<const CoedgeId>);
  Result<FaceId> create_face(SurfaceId, LoopId outer_loop, std::span<const LoopId> inner_loops);
  Result<ShellId> create_shell(std::span<const FaceId>);
  Result<BodyId> create_body(std::span<const ShellId>);

  Result<void> delete_face(FaceId);
  Result<void> replace_surface(FaceId, SurfaceId);

  Result<VersionId> commit();
  Result<void> rollback();

  Result<std::uint64_t> write_operation_count() const;
  Result<TopologyIsolationLevel> effective_isolation_level() const;
};
```

> 说明：`TopologyTransaction` 的完整签名见 `include/axiom/topo/topology_service.h`；另含 `set_coedge_pcurve`、删除壳/体、以及 trim 桥接审计读数 `coedge_pcurve_bind_count()` / `coedge_pcurve_clear_count()` 等。`write_operation_count()` 统计本事务内每次**成功**的写操作（创建/删除实体、`replace_surface`、每次 `set_coedge_pcurve` 含清除）；回滚或 `clear_tracking_records()` 归零。`effective_isolation_level()` 当前实现返回 `SnapshotSerializable`（单事务 + 快照回滚的工程占位，见头文件注释）。

### 6.3 拓扑验证接口

```cpp
class TopologyValidationService {
public:
  Result<void> validate_edge(EdgeId) const;
  Result<void> validate_face(FaceId) const;
  Result<void> validate_shell(ShellId) const;
  Result<void> validate_body(BodyId) const;
};
```

## 7. `RepCore` 接口清单

### 7.1 表示类型

```cpp
enum class RepKind {
  ExactBRep,
  MeshRep,
  ImplicitRep,
  HybridRep
};
```

### 7.2 表示查询接口

```cpp
class RepresentationService {
public:
  Result<RepKind> kind_of_body(BodyId) const;
  Result<BoundingBox> bbox_of_body(BodyId) const;
  Result<bool> classify_point(BodyId, const Point3&) const;
  Result<Scalar> distance_to_body(BodyId, const Point3&) const;
};
```

### 7.3 表示转换接口

```cpp
struct TessellationOptions {
  Scalar chordal_error;
  Scalar angular_error;
  bool compute_normals;
  bool generate_texcoords;
  /// 默认 `180`：仅按位置/UV 焊接；小于 `180` 时在法向夹角过大处保留折边顶点。
  Scalar weld_shading_split_angle_deg;
};

/// 体级/面级三角化缓存观测；`clear_mesh_store` / `reset_runtime_stores` 时清零。
struct TessellationCacheStats {
  std::uint64_t body_cache_hits;
  std::uint64_t body_cache_misses;
  std::uint64_t body_cache_stale_evictions;
  std::uint64_t face_cache_hits;
  std::uint64_t face_cache_misses;
  std::uint64_t face_cache_stale_evictions;
};

struct MeshInspectionReport {
  std::uint64_t vertex_count;
  std::uint64_t triangle_count;
  std::uint64_t connected_components;
  std::string mesh_label;
  std::string tessellation_strategy;
  std::string tessellation_budget_digest;
  // ... 索引/退化/连通性等布尔字段见头文件
};

struct ConversionErrorBudget {
  Scalar bbox_abs_tol;
  Scalar max_point_abs_tol;
  Scalar normal_angle_deg_tol;
  Scalar chordal_error_basis;
  Scalar angular_error_basis_deg;
};

struct RoundTripReport {
  bool passed;
  ConversionErrorBudget budget;
  Scalar bbox_max_abs_delta;
  Scalar max_point_abs_delta;
  Scalar max_normal_angle_deg_delta;
  bool normal_deviation_measured;
  std::string tessellation_strategy;
  std::string tessellation_budget_digest;
  // ... 三角形计数等见头文件
};

class RepresentationConversionService {
public:
  Result<MeshId> brep_to_mesh(BodyId, const TessellationOptions&);
  /// 成功后将 `MeshRecord::source_body` 设为新建 `MeshRep` 体，便于 `brep_to_mesh` 嵌入返回同一网格。
  Result<BodyId> mesh_to_brep(MeshId);
  Result<MeshId> implicit_to_mesh(ImplicitFieldId, const TessellationOptions&);
  Result<TessellationCacheStats> tessellation_cache_stats() const;
  Result<void> export_tessellation_cache_stats_json(std::string_view path) const;
  Result<void> export_round_trip_report_json(const RoundTripReport& report, std::string_view path) const;
  Result<ConversionErrorBudget> conversion_error_budget_for_tessellation(const TessellationOptions&) const;
  Result<void> export_conversion_error_budget_json(const TessellationOptions&, std::string_view path) const;
};
```

## 8. `OpsCore` 接口清单

### 8.1 基础体与特征构造

```cpp
class PrimitiveService {
public:
  Result<BodyId> box(const Point3& origin, Scalar dx, Scalar dy, Scalar dz);
  Result<BodyId> sphere(const Point3& center, Scalar radius);
  Result<BodyId> cylinder(const Point3& center, const Vec3& axis, Scalar radius, Scalar height);
  Result<BodyId> cone(const Point3& apex, const Vec3& axis, Scalar semi_angle, Scalar height);
  Result<BodyId> torus(const Point3& center, const Vec3& axis, Scalar major_r, Scalar minor_r);
};
```

```cpp
class SweepService {
public:
  Result<BodyId> extrude(const ProfileRef&, const Vec3& direction, Scalar distance);
  Result<BodyId> revolve(const ProfileRef&, const Axis3&, Scalar angle);
  Result<BodyId> sweep(const ProfileRef&, CurveId rail);
  Result<BodyId> loft(std::span<const ProfileRef> profiles);
  Result<BodyId> thicken(FaceId, Scalar distance);
};
```

### 8.2 布尔操作接口

```cpp
enum class BooleanOp {
  Union,
  Subtract,
  Intersect,
  Split
};

struct BooleanOptions {
  TolerancePolicy tolerance;
  bool diagnostics;
  bool auto_repair;
};

struct OpReport {
  StatusCode status;
  BodyId output;
  DiagnosticId diagnostic_id;
  std::vector<Warning> warnings;
};

class BooleanService {
public:
  Result<OpReport> run(BooleanOp op, BodyId lhs, BodyId rhs, const BooleanOptions&);
};
```

### 8.3 修改操作接口

```cpp
class ModifyService {
public:
  Result<OpReport> offset_body(BodyId, Scalar distance, const TolerancePolicy&);
  Result<OpReport> shell_body(BodyId, std::span<const FaceId> removed_faces, Scalar thickness);
  Result<OpReport> draft_faces(BodyId, std::span<const FaceId>, const Vec3& pull_dir, Scalar angle);
  Result<OpReport> replace_face(BodyId, FaceId target, SurfaceId replacement);
  Result<OpReport> delete_face_and_heal(BodyId, FaceId target);
};
```

### 8.4 圆角与倒角接口

```cpp
class BlendService {
public:
  Result<OpReport> fillet_edges(BodyId, std::span<const EdgeId>, Scalar radius);
  Result<OpReport> chamfer_edges(BodyId, std::span<const EdgeId>, Scalar distance);
};
```

### 8.5 查询与分析接口

```cpp
struct MassProperties {
  Scalar volume;
  Scalar area;
  Point3 centroid;
  std::array<Scalar, 9> inertia;
};

class QueryService {
public:
  Result<IntersectionId> intersect(CurveId, SurfaceId) const;
  Result<IntersectionId> intersect(SurfaceId, SurfaceId) const;
  Result<MeshId> section(BodyId, const Plane&) const;
  Result<MassProperties> mass_properties(BodyId) const;
  Result<Scalar> min_distance(BodyId, BodyId) const;
};
```

## 9. `HealCore` 接口清单

### 9.1 验证接口

```cpp
enum class ValidationMode {
  Fast,
  Standard,
  Strict
};

class ValidationService {
public:
  Result<void> validate_geometry(BodyId, ValidationMode) const;
  Result<void> validate_topology(BodyId, ValidationMode) const;
  Result<void> validate_self_intersection(BodyId, ValidationMode) const;
  Result<void> validate_tolerance(BodyId, ValidationMode) const;
  Result<void> validate_all(BodyId, ValidationMode) const;
};
```

### 9.2 修复接口

```cpp
enum class RepairMode {
  ReportOnly,
  SuggestOnly,
  Safe,
  Aggressive
};

class RepairService {
public:
  Result<OpReport> sew_faces(std::span<const FaceId>, Scalar tolerance, RepairMode);
  Result<OpReport> remove_small_edges(BodyId, Scalar threshold, RepairMode);
  Result<OpReport> remove_small_faces(BodyId, Scalar threshold, RepairMode);
  Result<OpReport> merge_near_coplanar_faces(BodyId, Scalar angle_tol, RepairMode);
  Result<OpReport> auto_repair(BodyId, RepairMode);
};
```

## 10. `EvalGraph` 接口清单

### 10.1 依赖图接口

```cpp
struct NodeId { uint64_t value; };

enum class NodeKind {
  Geometry,
  Topology,
  Operation,
  Cache,
  Visualization,
  Analysis
};

/// 可观测性计数（节选）；含单次 `recompute` 传递闭包规模峰值 `recompute_single_root_max_finish_nodes` 与依赖 DFS 深度峰值 `recompute_single_root_max_stack_depth`。
struct EvalGraphTelemetry { /* 见 axiom/core/types.h */ };

class EvalGraphService {
public:
  Result<NodeId> register_node(NodeKind, std::string_view label);
  Result<void> add_dependency(NodeId from, NodeId to);
  Result<void> invalidate(NodeId);
  Result<void> invalidate_body(BodyId);
  Result<void> recompute(NodeId);
  Result<EvalGraphTelemetry> telemetry() const;
};
```

### 10.2 缓存接口

```cpp
class CacheService {
public:
  Result<void> store_curve_eval(CurveId, Scalar t, const CurveEvalResult&);
  Result<CurveEvalResult> load_curve_eval(CurveId, Scalar t) const;
  Result<void> store_mesh(BodyId, MeshId, VersionId);
  Result<MeshId> load_mesh(BodyId, VersionId) const;
  Result<void> clear_body_cache(BodyId);
};
```

## 11. `IO` 接口清单

### 11.1 导入导出接口

```cpp
class IOService {
public:
  Result<BodyId> import_step(std::string_view path, const ImportOptions&);
  Result<void> export_step(BodyId, std::string_view path, const ExportOptions&);
};
```

## 12. `Diagnostics` 接口清单

### 11.1 诊断对象

```cpp
enum class IssueSeverity {
  Info,
  Warning,
  Error,
  Fatal
};

struct Issue {
  std::string code;
  IssueSeverity severity;
  std::string message;
  std::vector<uint64_t> related_entities;
};

struct DiagnosticReport {
  DiagnosticId id;
  std::vector<Issue> issues;
  std::string summary;
};
```

### 11.2 日志接口

```cpp
class DiagnosticService {
public:
  Result<DiagnosticReport> get(DiagnosticId) const;
  Result<void> append_issue(DiagnosticId, const Issue&);
  Result<void> export_report(DiagnosticId, std::string_view path) const;
};
```

## 13. `TopoCore` 门面补充接口

为了与上层 `Kernel` 门面保持一致，建议提供一个轻量的拓扑入口服务，负责开启事务和聚合只读查询。

```cpp
class TopologyService {
public:
  TopologyTransaction begin_transaction();
  TopologyQueryService& query();
};
```

## 14. `PluginSDK` 接口清单

> **对齐说明**：以下以仓库当前 `include/axiom/plugin/plugin_registry.h`、`include/axiom/sdk/kernel.h` 为准。`PluginRegistry` 还提供大量清单/能力统计/导出查询方法，此处仅列核心注册与类型入口。

### 14.1 插件元信息与宿主策略

```cpp
struct PluginManifest {
  std::string name;
  std::string version;
  std::string vendor;
  std::vector<std::string> capabilities;
  std::string plugin_api_version;  // 建议与 axiom::kPluginSdkApiVersion 一致（见 plugin_sdk_version.h）
  std::string implementation_type_name;  // 与实现 type_name 绑定；带实现的 register 若为空则自动填充；按 type_name 注销实现时移除同字段匹配的清单
};

/// 进程内宿主策略（非 OS 级隔离）：注册前校验与容量门禁
struct PluginHostPolicy {
  std::uint32_t max_plugin_slots{};  // 0 表示不限制实例总数
  bool enforce_unique_implementation_type_name{true};
  bool require_non_empty_manifest_name{true};
  bool require_unique_manifest_name{false};
  bool require_non_empty_capabilities{false};
  bool require_non_empty_plugin_type_name{true};
  bool require_plugin_api_version_match{false};  // 为真时按 plugin_api_version_match_mode 校验清单字段
  PluginApiVersionMatchMode plugin_api_version_match_mode{PluginApiVersionMatchMode::Exact};
  PluginSandboxLevel sandbox_level{PluginSandboxLevel::None};  // 能力发现/审计占位，非 OS 沙箱
};
```

`PluginApiVersionMatchMode`：`Exact`（与 `kPluginSdkApiVersion` 字符串一致）、`SameMinor`（`major.minor` 一致，可带 patch）、`SameMajor`（仅 `major` 一致，更宽松）。

`PluginSandboxLevel`：`None`（默认）、`Annotated`（报告中标注更高安全期望，执行模型仍为进程内共享）。

`KernelConfig` 内含 `PluginHostPolicy plugin_host_policy`；`KernelState` 构造时会 `set_host_policy` 同步到 `PluginRegistry`。插件 SDK API 版本常量见 `include/axiom/plugin/plugin_sdk_version.h`（`kPluginSdkApiVersion`）。

注册门禁与 `Kernel::plugin_api_compatibility_report_lines()` 共用 **`plugin_api_version_declared_compatible(declared_trimmed, expected_sdk_api, mode)`**（声明于 `include/axiom/plugin/plugin_registry.h`）。

### 14.2 插件注册接口（`PluginRegistry`）

```cpp
class PluginRegistry {
public:
  void set_host_policy(const PluginHostPolicy& policy);
  Result<PluginHostPolicy> host_policy() const;
  Result<void> validate_manifest(const PluginManifest& manifest) const;

  Result<void> register_curve_type(const PluginManifest&, std::unique_ptr<ICurvePlugin>);
  Result<void> register_repair_plugin(const PluginManifest&, std::unique_ptr<IRepairPlugin>);
  Result<void> register_importer(const PluginManifest&, std::unique_ptr<IImporterPlugin>);
  Result<void> register_exporter(const PluginManifest&, std::unique_ptr<IExporterPlugin>);
  Result<void> register_manifest_only(const PluginManifest&);
  // … 清单查询、能力直方图、按厂商/能力筛选、导出 txt 等
};
```

> **注**：曲面类型等扩展接口可作为后续版本增补；当前仓库未提供 `register_surface_type`。

### 14.3 插件基类

```cpp
class ICurvePlugin {
public:
  virtual ~ICurvePlugin() = default;
  virtual std::string type_name() const = 0;
  virtual Result<CurveId> create(const PluginCurveDesc&) = 0;
};

class IRepairPlugin {
public:
  virtual ~IRepairPlugin() = default;
  virtual std::string type_name() const = 0;
  virtual Result<OpReport> run(BodyId, RepairMode) = 0;
};
```

### 14.4 `Kernel` 插件相关门面（摘要）

除 `PluginRegistry& plugins()` 外，`Kernel` 还提供：

- **能力发现**：`plugin_manifest_names`、`plugin_total_count`、`has_any_plugins`、`plugin_vendors`、`plugin_capabilities`、`plugin_capabilities_histogram_lines`、`plugin_sdk_api_version`、`plugin_discovery_report_lines`、`plugin_discovery_report_json`（含 `manifests` 数组：`name`、`implementation_type_name`）、`plugin_api_compatibility_report_lines`。
- **策略**：`plugin_host_policy`、`set_plugin_host_policy`、`has_service_plugin_registry`、`has_service_plugin_discovery`、`has_service_plugin_import`、`has_service_plugin_export`、`has_service_plugin_repair`、`has_service_plugin_curve`；`PluginHostPolicy::auto_validate_body_after_plugin_importer`、`auto_validate_body_before_plugin_exporter`、`auto_validate_body_after_plugin_repair`、`auto_verify_curve_after_plugin_curve`（能力报告行/JSON 与 `plugin_discovery_report_lines` 对齐）。
- **宿主导入封装**：`plugin_import_file(implementation_type_name, path, ValidationMode)`（可选在成功后按策略自动 `validate_all`）；注册表侧 `PluginRegistry::invoke_registered_importer` 仅调用插件、不做验证。
- **宿主导出封装**：`plugin_export_file(implementation_type_name, body_id, path, ValidationMode)`（可选在调用插件前按策略先 `validate_all`；`Body` 不存在时失败）；注册表侧 `PluginRegistry::invoke_registered_exporter` 仅调用插件、不做验证。
- **宿主修复封装**：`plugin_run_repair(implementation_type_name, body_id, RepairMode, ValidationMode)`（可选在插件返回 Ok 后按策略对结果体自动 `validate_all`：`OpReport::output` 若仍注册则优先，否则验证输入体；`Body` 不存在时失败）；注册表侧 `PluginRegistry::invoke_registered_repair` 仅调用插件、不做验证。
- **宿主曲线封装**：`plugin_create_curve(implementation_type_name, PluginCurveDesc)`（可选在插件返回 Ok 后按策略校验 `CurveId` 已注册且 `CurveService::domain` 为合法开区间；`PluginCurveDesc::type_name` 若为空则视为与 `implementation_type_name` 一致，若非空则须与之一致）；注册表侧 `PluginRegistry::invoke_registered_curve` 仅调用插件、不做宿主校验；若需与 `plugin_create_curve`（在开启 `auto_verify_curve_after_plugin_curve` 时）相同的校验语义，可在注册表调用成功后显式调用 **`verify_after_plugin_curve(CurveId)`**。
- **可诊断注册**（失败且 `enable_diagnostics` 时写入诊断存储，`diagnostic_id` 非零）：`register_plugin_curve`、`register_plugin_repair`、`register_plugin_importer`、`register_plugin_exporter`、`register_plugin_manifest_only`。  
  若仅需 `Result::warnings`、不写全局诊断，可直接使用 `plugins().register_*`。
- **可诊断注销**：`unregister_plugin_curve`、`unregister_plugin_repair`、`unregister_plugin_importer`、`unregister_plugin_exporter`（按实现 `type_name`）、`unregister_plugin_manifest`（按清单 `name`）。`PluginRegistry::unregister_*` 在 `type_name` 为空或未命中实现时返回 **`AXM-PLUGIN-E-0003` / `AXM-PLUGIN-E-0008`**（见 `error_codes.h`）。
- **验证**：`validate_after_plugin_mutation(BodyId, ValidationMode)`（语义同 `validate().validate_all`，非 `const` 成员因需访问 `ValidationService`）。开启 `auto_validate_body_after_plugin_importer` 时，`plugin_import_file` 成功返回前会先做同一套全量验证；开启 `auto_validate_body_before_plugin_exporter` 时，`plugin_export_file` 在调用导出插件前先验证；开启 `auto_validate_body_after_plugin_repair` 时，`plugin_run_repair` 在插件返回 Ok 后对结果体做全量验证；开启 `auto_verify_curve_after_plugin_curve` 时，`plugin_create_curve` 在插件返回 Ok 后做曲线句柄与参数域一致性校验；**`verify_after_plugin_curve(CurveId)`** 提供与上述曲线自动校验相同的显式入口（适用于直接调用 `invoke_registered_curve` 等路径）。验证失败时返回非 Ok（`AXM-PLUGIN-E-0005` 等），导入场景下 Body 可能仍保留；修复场景下模型可能已被插件修改；曲线场景下无效 `CurveId` 仍可能已被插件登记或未登记（视插件实现而定）。

## 15. Kernel 门面接口

为了降低上层使用复杂度，需要提供聚合式门面。

```cpp
class Kernel {
public:
  explicit Kernel(const KernelConfig&);

  CurveFactory& curves();
  SurfaceFactory& surfaces();
  CurveService& curve_service();
  SurfaceService& surface_service();
  ToleranceService& tolerance();
  TopologyService& topology();
  PrimitiveService& primitives();
  SweepService& sweeps();
  BooleanService& booleans();
  ModifyService& modify();
  BlendService& blends();
  QueryService& query();
  ValidationService& validate();
  RepairService& repair();
  RepresentationConversionService& convert();
  IOService& io();
  DiagnosticService& diagnostics();
  EvalGraphService& eval_graph();
  PluginRegistry& plugins();
  Result<TessellationCacheStats> tessellation_cache_stats() const;
  Result<void> export_tessellation_cache_stats_json(std::string_view path) const;
  Result<void> export_round_trip_report_json(const RoundTripReport& report, std::string_view path) const;
  Result<ConversionErrorBudget> conversion_error_budget_for_tessellation(const TessellationOptions&) const;
  Result<void> export_conversion_error_budget_json(const TessellationOptions&, std::string_view path) const;
  /// `KernelEvalGraphMetrics`：含 `max_per_node_recompute_count`、`nodes_with_recompute_nonzero`、`mean_recompute_events_per_node`、`mean_recompute_events_per_touched_node` 等与重算门禁相关的派生字段。
  Result<KernelEvalGraphMetrics> eval_graph_metrics() const;
  /// 合并拓扑审计、Eval 指标、运行时 store 与 **`rep_stage_snapshot`**（默认 `TessellationOptions` 的 digest + `ConversionErrorBudget`，`derivation=tessellation_options_v1`）。
  Result<void> export_runtime_observability_json(std::string_view path) const;
  // 另见 §14.4：配置/能力报告/插件发现与 register_plugin_* 等
};
```

## 16. 版本 1 的最小接口集

为控制首版复杂度，建议 `v1` 只冻结以下接口：

- `CurveFactory`
- `SurfaceFactory`
- `CurveService`
- `SurfaceService`
- `TopologyTransaction`
- `TopologyQueryService`
- `PrimitiveService`
- `SweepService`
- `BooleanService`
- `QueryService`
- `ValidationService`
- `RepairService`
- `RepresentationConversionService`
- `IOService`
- `DiagnosticService`

以下接口可在 `v1.1` 或 `v2` 稳定：

- `BlendService` 高级部分
- `ModifyService` 高级部分
- `EvalGraphService` 完整接口
- `PluginRegistry` 完整能力

## 17. 研发拆分建议

按实现优先级，建议拆分为以下包：

- `axiom.math`
- `axiom.geo`
- `axiom.topo`
- `axiom.rep`
- `axiom.ops`
- `axiom.heal`
- `axiom.eval`
- `axiom.diag`
- `axiom.plugin`
- `axiom.io`

## 18. 结论

这份接口清单的核心目的不是把名字一次定死，而是先把边界定稳：

- 哪些能力属于几何层
- 哪些能力属于拓扑层
- 哪些能力必须事务化
- 哪些能力必须返回诊断对象
- 哪些能力可以在首版先收敛

后续如果继续推进，下一步最适合补的是：

1. `接口调用时序图`
2. `模块依赖图`
3. `错误码与诊断码字典`
