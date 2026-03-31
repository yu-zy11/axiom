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
};
```

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
};
```

### 6.2 拓扑事务接口

```cpp
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
};
```

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
};

class RepresentationConversionService {
public:
  Result<MeshId> brep_to_mesh(BodyId, const TessellationOptions&);
  Result<BodyId> mesh_to_brep(MeshId);
  Result<MeshId> implicit_to_mesh(ImplicitFieldId, const TessellationOptions&);
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

class EvalGraphService {
public:
  Result<NodeId> register_node(NodeKind, std::string_view label);
  Result<void> add_dependency(NodeId from, NodeId to);
  Result<void> invalidate(NodeId);
  Result<void> invalidate_body(BodyId);
  Result<void> recompute(NodeId);
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

### 12.1 插件元信息

```cpp
struct PluginManifest {
  std::string name;
  std::string version;
  std::string vendor;
  std::vector<std::string> capabilities;
};
```

### 12.2 插件注册接口

```cpp
class PluginRegistry {
public:
  Result<void> register_curve_type(const PluginManifest&, std::unique_ptr<ICurvePlugin>);
  Result<void> register_surface_type(const PluginManifest&, std::unique_ptr<ISurfacePlugin>);
  Result<void> register_repair_plugin(const PluginManifest&, std::unique_ptr<IRepairPlugin>);
  Result<void> register_importer(const PluginManifest&, std::unique_ptr<IImporterPlugin>);
  Result<void> register_exporter(const PluginManifest&, std::unique_ptr<IExporterPlugin>);
};
```

### 12.3 插件基类

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
