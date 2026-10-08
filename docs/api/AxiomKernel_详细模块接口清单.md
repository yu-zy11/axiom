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
  bool run_validation{true};
  bool auto_repair{false};
  RepairMode repair_mode{RepairMode::Safe};
};

struct ExportOptions {
  bool compatibility_mode;
  bool embed_metadata;
  bool write_mesh_validation_report;
};
```

`run_validation=false` 显式跳过导入后验证和自动修复；`true` 时未修复的验证失败返回失败并回滚模型/缓存/Eval。`auto_repair` 的 ReportOnly/SuggestOnly 不升级为 Safe，具体闭环见 §9.2.1 与 §11.1。策略组合与网格导出门禁口径见 `docs/quality/AxiomKernel_IO_导出策略矩阵.md`。

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

`CurveFactory::make_bspline/make_nurbs` 的显式结点向量须有限、严格满足非减顺序（不使用几何容差吞掉逆序），且有效参数域 `knots[degree] < knots[poles.size()]`；每个结点的重数不得超过 `degree + 1`（精确数值比较），合法重复结点保留。内部重数为 `degree + 1` 的断点按右侧分段求值，上端点按左侧分段求值；返回的一、二阶导数是对应分段的单侧值，不表示断点处全局可微。非夹持结点向量在有效域端点出现重复结点时，跳过零长度分段，下端点使用第一个非空右侧分段、上端点使用最后一个非空左侧分段；点值、一二阶导数和曲率均遵循此约定，域外参数钳制后同样适用。常值分段允许求值，导数与曲率返回零。零长度有效域、结点逆序或重数超限返回 `InvalidInput` 和 `AXM-GEO-E-0001`，不创建几何对象、不改动已有求值缓存。省略结点时的默认生成规则不变。

`SurfaceFactory::make_bspline/make_nurbs` 对 u/v 两轴的显式结点采用相同的有限、非减、非零有效域及 `degree + 1` 重数上限；任一轴不满足时返回 `InvalidInput` 和 `AXM-GEO-E-0002`，且不创建曲面或改动求值缓存。合法满重数断点保留：内部断点使用右侧非空曲面片，上端点使用左侧非空曲面片；点值、一二阶偏导和曲率采用同一侧语义，不表示断点处全局可微。域外参数钳制到端点后遵循相同规则；常值退化片返回零偏导和零曲率。

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

struct CurveLengthOptions {
  Scalar absolute_tolerance{1e-9};
  Scalar relative_tolerance{1e-10};
  std::uint32_t max_evaluations{100000};
};

enum class CurveClosestPointConvergence : std::uint8_t {
  Analytic,
  DistanceTolerance,
  ParameterTolerance
};

struct CurveClosestPointOptions {
  Scalar distance_tolerance{1e-9};
  Scalar parameter_tolerance{1e-9};
  std::uint32_t max_evaluations{100000};
};

struct CurveClosestPointResult {
  Scalar parameter;
  Point3 point;
  Scalar distance;
  Scalar distance_lower_bound;
  Scalar parameter_uncertainty;
  std::uint32_t evaluations;
  std::uint32_t intervals_processed;
  CurveClosestPointConvergence convergence;
};

class CurveService {
public:
  Result<CurveEvalResult> eval(CurveId, Scalar t, int deriv_order) const;
  Result<Point3> point_at_parameter(CurveId, Scalar t) const;
  Result<CurveClosestPointResult> closest_point_detailed(
      CurveId, const Point3&, const CurveClosestPointOptions& = {}) const;
  Result<Scalar> closest_parameter(CurveId, const Point3&) const;
  Result<Point3> closest_point(CurveId, const Point3&) const;
  Result<Range1D> domain(CurveId) const;
  Result<Scalar> length(CurveId) const;
  Result<Scalar> length(CurveId, Scalar t0, Scalar t1) const;
  Result<Scalar> length(CurveId, const CurveLengthOptions&) const;
  Result<Scalar> length(CurveId, Scalar t0, Scalar t1, const CurveLengthOptions&) const;
  Result<BoundingBox> bbox(CurveId) const;
  Result<BoundingBox> bbox(CurveId, Scalar t0, Scalar t1) const;
};
```

`point_at_parameter` 与区间 `bbox` 是第 72 批为拓扑 trim bridge 提供的公开只读查询：两者都要求有限且位于定义域的参数，不写曲线求值缓存。区间 bbox 允许递减和零宽区间；解析圆锥曲线包含区间内坐标极值，Bezier/BSpline/NURBS 使用保守控制点凸包，复合链按涉及的子曲线保守合并。无效句柄、非有限参数和越域分别使用 `AXM-CORE-E-0001/0002` 与 `AXM-GEO-E-0004`。

`closest_point_detailed`（FR-GEO-001 第 69 批）对完整有效参数域给出最近参数、最近点、距离、保守距离下界、参数不确定度、求值/区间计数及终止原因。Line、LineSegment、Circle 与 CompositePolyline 走解析路径（`Analytic`，不消耗点值预算）；Ellipse、Parabola、Hyperbola、Bezier、BSpline、NURBS 与 CompositeChain 走确定性分支限界，样条的每个非空结点段独立覆盖，并用保守速度/加速度界剪枝。`DistanceTolerance` 保证 `distance - distance_lower_bound` 不超过请求的距离容差；`ParameterTolerance` 表示仍可能改进区间的最大宽度不超过请求的参数容差。非解析的旧 `closest_parameter/closest_point` 复用该主流程，并在全域证书收敛后用剩余预算做下降式参数精修。

选项要求 `distance_tolerance` 有限且非负、`parameter_tolerance` 有限且大于零、`max_evaluations >= 3`。非法选项或不可表示的有限输入返回结构化失败；预算耗尽返回 `OperationFailed / AXM-GEO-E-0006`，不返回部分结果。查询不写几何求值缓存。该类型是曲线查询的一维参数证据；曲面的二维参数域、预算和收敛证据由下文 `SurfaceClosestPointResult` 单独表达。

`CurveService::length`（FR-QUERY-001 第 63/67 功能包）以模型长度单位返回弧长。Line 有限区间、LineSegment、Circle 和 CompositePolyline 使用解析计算；Ellipse、Parabola、Hyperbola、Bezier、BSpline 和 NURBS 对真实一阶导数的速度做自适应 Simpson 积分，不使用 bbox、网格或采样弦长冒充弧长。差值、速度和累加使用 `long double` 中间量，最终舍入到 `Scalar`；数值路径的容差是误差估计目标，不是任意曲线的严格误差界。

`CurveLengthOptions` 的默认绝对容差为 `1e-9` 模型长度单位，相对容差为 `1e-10`，整次查询（含复合链）最多做 `100000` 次速度求值。两种容差可单独置零但不可同时为零，并且必须有限且非负；预算必须大于零。非法选项返回 `InvalidInput / AXM-CORE-E-0002`；预算耗尽、精度停滞或非有限速度返回 `OperationFailed / AXM-GEO-E-0011`。所有失败都不返回部分长度。完整域重载复用区间算法；反向端点返回相同非负长度，零区间和常值曲线返回 0。有界域外参数不钳制，返回 `InvalidInput / AXM-GEO-E-0004`；非有限端点、全域无限直线及结果溢出返回 `InvalidInput / AXM-CORE-E-0002`；无效句柄返回 `InvalidInput / AXM-CORE-E-0001`，退化直线方向返回 `DegenerateGeometry / AXM-GEO-E-0003`。

复合链严格沿用现有 `eval` 约定：父域 `[0,n]` 每一格映射到对应子曲线的局部 `[0,1]`，**不缩放到子曲线完整域**；例如圆子曲线只贡献一弧度，折线子曲线只贡献首段，嵌套链只遍历其第一格。只检查/累加查询区间实际覆盖的非零子区间；不连续连接处不补直线距离。BSpline/NURBS 按非空结点区间分开积分，满重数断点的几何跳跃不计入弧长。长度查询不写几何、拓扑、求值缓存或 Eval 失效状态，只新增诊断。

`CurveService::closest_parameter/closest_point` 对 BSpline/NURBS 会在全域粗采样之外逐个覆盖非空结点分段，再进行阻尼局部细化；因此合法的极窄分段（包括满重数断点隔开的分支与常值退化分段）不会仅因宽度小于全域采样步长而被跳过。返回值仍是数值搜索结果，不构成任意曲线全局最优或工业精度保证。非有限查询点返回 `InvalidInput` / `AXM-CORE-E-0002`，不修改几何或求值缓存。
线段最近参数使用解析投影与 `[0,1]` 钳制；投影的差值、点积和长度平方使用扩展精度中间量，使端点及查询点均有限但长度平方超出 `Scalar` 范围时仍可返回有限参数。无效句柄返回 `InvalidInput` / `AXM-GEO-E-0006`；退化线段在创建时返回 `InvalidInput` / `AXM-GEO-E-0001`，失败不修改几何或求值缓存。
椭圆不再直接把查询点的缩放极角当作最近参数：该极角仅作为初值，随后按三维欧氏距离进行阻尼细化，并将周期缝结果归一到 `[0, 2pi)`。该语义已覆盖解析可知最近点、周期缝、非有限输入与失败不污染；仍不宣称任意退化椭圆的全局最优保证。
`CurveFactory::make_ellipse` 在创建前检查轴向量长度与派生法向长度是否有限；即使输入坐标本身有限，轴长或叉积溢出仍返回 `InvalidInput` / `AXM-GEO-E-0001`，不写入几何对象或求值缓存。
`PCurveService::closest_parameter/closest_point` 对现有 UV 折线逐段投影并比较欧氏距离，零长度段使用其端点，多个等距最近点取参数最小者。投影和距离比较使用扩展精度中间量，支持有限坐标的距离平方超出 `Scalar` 范围的情况。非有限查询点返回 `InvalidInput` / `AXM-CORE-E-0002`，无效句柄返回 `InvalidInput` / `AXM-CORE-E-0001`；这些失败不修改几何或缓存。该合同仅覆盖当前的折线 PCurve。

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

enum class SurfaceClosestPointConvergence : std::uint8_t {
  DistanceTolerance = 0,
  ParameterTolerance = 1,
  Analytic = 2
};

struct SurfaceClosestPointOptions {
  Scalar distance_tolerance{1e-8};
  Scalar parameter_tolerance{1e-4};
  std::uint32_t max_evaluations{250000};
};

struct SurfaceClosestPointResult {
  Scalar u;
  Scalar v;
  Point3 point;
  Scalar distance;
  Scalar distance_lower_bound;
  Scalar u_uncertainty;
  Scalar v_uncertainty;
  std::uint32_t evaluations;
  std::uint32_t patches_processed;
  std::uint32_t control_net_bound_patches;
  std::uint32_t pruned_patches;
  SurfaceClosestPointConvergence convergence;
  Range2D effective_domain;
  bool domain_was_finiteized;
};

class SurfaceService {
public:
  Result<SurfaceEvalResult> eval(SurfaceId, Scalar u, Scalar v, int deriv_order) const;
  Result<SurfaceClosestPointResult> closest_point_detailed(
      SurfaceId, const Point3&, const SurfaceClosestPointOptions& = {}) const;
  Result<Point3> closest_point(SurfaceId, const Point3&) const;
  Result<std::pair<Scalar, Scalar>> closest_uv(SurfaceId, const Point3&) const;
  Result<Range2D> domain(SurfaceId) const;
  Result<BoundingBox> bbox(SurfaceId) const;
};
```

`closest_point_detailed` 返回最近 UV/点、距离与保守下界、参数不确定度、工作量和终止原因。Plane、Cylinder、Cone、规则 Sphere/Torus 及其嵌套 Offset 链可走 `Analytic`：无界方向自动构造包含至少一个全域极小点的 `effective_domain`，以 `domain_was_finiteized=true` 显式标识，且 `evaluations/patches_processed` 为 0。周期缝、柱轴、球心、锥顶和环管中心等非唯一参数位置返回确定性规范参数。

Bezier/BSpline/NURBS 对每个非空结点片及递归子片提取正权有理 Bezier 控制网，以欧氏凸包 AABB 建立随细分收紧的全域距离下界；Trimmed 保留该证书，嵌套 Offset 按各层绝对偏置保守扩张。`control_net_bound_patches` 和 `pruned_patches` 分别记录启用控制网界的参数片和被下界剪枝的参数片；它们是本次查询证据，不是续算游标。Revolved/Swept、通用派生面与其他有界路径继续使用保守变化界和确定性分支限界，修剪外环与孔边界参与搜索。

解析偏置链的有效半径坍缩、负向完整圆锥偏置自交返回 `DegenerateGeometry / AXM-GEO-E-0010`；解析有限化或结果超出表示范围返回 `NumericalInstability / AXM-GEO-E-0006`。非法选项、数值搜索预算耗尽或无法建立保守界同样结构化失败。所有失败都不返回部分值、不写 surface eval 缓存；旧 `closest_uv/closest_point` 复用同一主流程。当前仅规则环面使用解析证书，spindle/horn 环面仍走既有有界数值路径；通用无限派生曲面尚不自动有限化，极小齐次权重可在分母保护下回退速度界，旋转/扫掠面尚无独立大模型性能基线证书。

### 5.3 几何变换接口

```cpp
class GeometryTransformService {
public:
  Result<CurveId> transform_curve(CurveId, const Transform3&);
  Result<SurfaceId> transform_surface(SurfaceId, const Transform3&);
};
```

### 5.4 曲线-曲线求交接口

```cpp
enum class CurveCurveIntersectionKind : std::uint8_t {
  Transverse,
  Tangent,
  Endpoint
};

struct CurveCurveIntersectionPoint {
  Point3 point;
  Scalar first_parameter;
  Scalar second_parameter;
  Scalar residual_distance;
  CurveCurveIntersectionKind kind;
};

struct CurveCurveOverlap {
  Range1D first_interval;
  Range1D second_interval;
  bool same_direction;
  Scalar maximum_separation;
};

struct CurveCurveIntersectionOptions {
  Scalar position_tolerance{1e-8};
  Scalar parameter_tolerance{1e-8};
  Scalar angular_tolerance{1e-7};
  std::uint32_t max_evaluations{200000};
  std::uint32_t max_subdivisions{100000};
  std::optional<Range1D> first_interval;
  std::optional<Range1D> second_interval;
};

struct CurveCurveIntersectionResult {
  std::vector<CurveCurveIntersectionPoint> points;
  std::vector<CurveCurveOverlap> overlaps;
  std::uint32_t evaluations;
  std::uint32_t parameter_rectangles_processed;
};

class GeometryIntersectionService {
public:
  Result<CurveCurveIntersectionResult> intersect_curve_curve(
      CurveId first, CurveId second,
      const CurveCurveIntersectionOptions& options = {}) const;
};
```

`intersect_curve_curve` 只搜索有限参数域。未显式指定区间时使用曲线完整定义域；无限 `Line` 必须通过 `first_interval/second_interval` 提供有限窗口。区间可以为零宽点查询，输入的递减端点会按无向搜索域规范化。空交集是成功的空 `points/overlaps`，不是失败。

Line、LineSegment、CompositePolyline 及可证明为线性的样条/复合子段使用解析 3D 最近线段与共线覆盖路径；Bezier、BSpline、NURBS、圆锥曲线及 CompositeChain 按连续参数片建立保守包围，进行确定性参数矩形细分和阻尼 Gauss-Newton 精化；满重数结点两侧独立处理。离散交点返回双侧参数、两求值点中点、残差距离及 `Transverse/Tangent/Endpoint`（端点分类优先）；连续重合单独返回 `overlaps`，区间均按参数递增排列，`same_direction` 说明几何方向。一般高阶曲线仅在记录可证明同参时报告连续重合；异参同轨不做完备证明。

`evaluations` 与 `parameter_rectangles_processed` 分别给出本次查询实际消耗的无缓存点值求值数和候选参数矩形数，调用方可据此归档工作量；它们不是下一次调用的续算游标。相切邻域以曲线片相对端点弦的保守偏差决定是否继续非线性细分；非连续 CompositeChain 建界时分别使用子片起点右极限和终点左极限，避免接缝另一侧的公开求值语义遮蔽当前子片。

`position_tolerance` 和 `parameter_tolerance` 必须有限且大于零，`angular_tolerance` 必须位于 `[0,1]`，求值预算至少为 6，细分预算必须非零。无效句柄、非法/越域区间、无法建立有限数值界或预算耗尽均结构化失败且不返回部分结果。该查询不写曲线求值缓存、Intersection/拓扑存储或活动事务计数。位置容差内的近交按容差命中；无限曲线不自动推断搜索窗口。

## 6. `TopoCore` 接口清单

### 6.1 拓扑只读查询接口

```cpp
enum class FaceBoundaryConflictKind : std::uint8_t {
  ProperIntersection,
  EndpointTouch,
  CollinearOverlap,
  NearContact,
  CurveOverlap
};

struct FaceBoundaryConflict {
  FaceBoundaryConflictKind kind;
  LoopId first_loop;
  LoopId second_loop;
  EdgeId first_edge;
  EdgeId second_edge;
  Point3 first_point;
  Point3 second_point;
  Scalar distance;
  bool error_controlled;
  Scalar solver_tolerance;
  std::uint32_t curve_evaluations;
  std::uint32_t parameter_rectangles_processed;
};

enum class BodyShellRole : std::uint8_t { Material, Void };
struct BodyShellRegion {
  ShellId shell;
  BodyShellRole role;
  std::uint32_t nesting_depth;
  std::optional<ShellId> parent_shell;
};

enum class BodyPointLocation : std::uint8_t { Outside = 0, Inside = 1, Boundary = 2 };
struct BodySpatialQueryOptions {
  Scalar position_tolerance {};  // 模型长度单位；0 使用内核线性容差
  std::uint64_t max_triangle_tests {1000000};
};
struct BodyBoundaryPoint {
  Point3 point {};
  ShellId shell {};
  FaceId face {};
  Scalar distance {};  // 非负欧氏距离，模型长度单位
};
struct BodyPointQuery {
  BodyPointLocation location {BodyPointLocation::Outside};
  std::optional<BodyBoundaryPoint> nearest_boundary;
  Scalar position_tolerance {};
  std::uint64_t triangle_tests {};
};
struct BodySegmentInterval {
  Range1D parameters {};  // p(t)=start+t*(end-start)，0≤t≤1，无量纲
  BodyPointLocation location {BodyPointLocation::Inside};
};
struct BodySegmentQuery {
  std::vector<BodySegmentInterval> intervals;
  Scalar material_length {};  // 模型长度单位，仅累计 Inside 区间
  Scalar position_tolerance {};
  std::uint64_t triangle_tests {};
};

// cycle-0075：实际平面截面；世界坐标，area 为模型长度单位平方。
struct BodyPlaneSection {
  std::vector<Point3> vertices;
  std::vector<std::array<int, 3>> triangles;
  std::vector<std::array<Point3, 2>> boundary_segments;
  std::vector<Point3> contact_points;
  BoundingBox bbox {};
  Scalar area {};
  std::uint64_t triangle_tests {};
};
// 实体材料闭集距离；内部见证的 FaceId/ShellId 为零。
struct BodyDistanceQuery {
  Scalar distance {};  // 模型长度单位，等于两见证点的欧氏距离
  Point3 first_point {}, second_point {};
  FaceId first_face {}, second_face {};
  ShellId first_shell {}, second_shell {};
  std::uint64_t triangle_tests {};
};

struct EdgeCurveInterval {
  Scalar start_parameter;  // 对应 v0
  Scalar end_parameter;    // 对应 v1，可小于 start_parameter
};
```

```cpp
class TopologyQueryService {
public:
  Result<std::array<VertexId, 2>> vertices_of_edge(EdgeId) const;
  Result<std::vector<EdgeId>> edges_of_loop(LoopId) const;
  Result<std::vector<LoopId>> loops_of_face(FaceId) const;
  Result<SurfaceId> surface_of_face(FaceId) const;
  // 平面直线边面片面积；模型长度单位的平方，外环减内环。
  Result<Scalar> planar_face_area(FaceId) const;
  // 解析曲面的折线 PCurve 修剪面积；模型长度单位的平方，外环减内环。
  Result<Scalar> face_area(FaceId) const;
  // 当前真实拓扑的单壳/实体均匀密度质量属性；不使用 bbox、网格或缓存。
  Result<MassProperties> shell_mass_properties(ShellId) const;
  Result<std::vector<BodyShellRegion>> body_shell_regions(BodyId) const;
  Result<MassProperties> body_mass_properties(BodyId) const;
  Result<BodyPointQuery> locate_point(BodyId, const Point3&,
                                    const BodySpatialQueryOptions& options = {}) const;
  Result<BodySegmentQuery> clip_segment(BodyId, const Point3& start, const Point3& end,
                                       const BodySpatialQueryOptions& options = {}) const;
  Result<BodyPlaneSection> section(BodyId, const Plane&,
                                   const BodySpatialQueryOptions& options = {}) const;
  Result<BodyDistanceQuery> closest_points(BodyId, BodyId,
                                         const BodySpatialQueryOptions& options = {}) const;
  Result<std::optional<EdgeCurveInterval>> edge_curve_interval(EdgeId) const;
  Result<Scalar> edge_length(EdgeId) const;
  Result<Scalar> loop_length(LoopId) const;
  Result<Scalar> face_boundary_length(FaceId) const;
  Result<std::vector<FaceId>> faces_of_shell(ShellId) const;
  Result<std::vector<ShellId>> shells_of_body(BodyId) const;
  /// 累计只读查询次数（嵌套调用只计最外层一次）；与 `TopologyTransaction::write_operation_count` 互补。
  Result<std::uint64_t> query_operation_count() const;
};
```

`planar_face_area` 从当前拓扑顶点和曲面法向计算边界面积，不使用 bbox 或三角网格。顶点到平面的距离须不超过内核线性容差；曲面必须为 Plane，边曲线必须为 Line/LineSegment。曲面或曲边不支持时返回 `NotImplemented / AXM-CORE-E-0004`；拓扑不完整、非共面或面积超出数值范围时返回 `InvalidTopology / AXM-TOPO-E-0003`（内环为 `E-0004`）；无效或已删除面返回 `InvalidInput / AXM-CORE-E-0001`，这些失败均无面积值。无内环时仅计外环；每次查询从当前模型重算，事务修改即时可见，回滚后恢复原面积。

`face_area` 用曲面面积密度的 Green 边界积分计算完整折线 PCurve 修剪环的外环减内环面积，支持 Plane/Cylinder/Cone/Sphere/Torus 以及嵌套 Trimmed/Offset 包装。未包装 Plane 且所有定向边都没有 PCurve 时兼容回退 `planar_face_area`；其他曲面缺少 PCurve，或同一面上只绑定了部分 PCurve，返回 `InvalidTopology / AXM-TOPO-E-0008`。Bezier/BSpline/NURBS/Revolved/Swept 面目前返回 `NotImplemented / AXM-CORE-E-0004`。

PCurve 必须为至少两点的折线，按 coedge 方向连续闭合，各点位于当前包装后参数域，且 UV 端点在支撑曲面上与定向拓扑顶点的 3D 位置一致。退化、自交、断裂或越域的外/内环分别返回 `InvalidTopology / AXM-TOPO-E-0003/0004`；内环必须严格位于外环内且不得相交、重叠或嵌套。非有限面积返回 `NumericalInstability / AXM-QUERY-E-0003`，扣孔后非正或退化返回 `DegenerateGeometry / AXM-GEO-E-0003`，结果溢出返回 `InvalidInput / AXM-CORE-E-0002`；所有失败均无部分面积。周期参数缝须由调用方在同一展开区间内表达，查询不自动解包裹。每次查询从当前面/环/曲面重算，不写求值或网格缓存；事务内替换/删除即时可见，回滚后恢复。

`shell_mass_properties / body_shell_regions / body_mass_properties` 从当前真实拓扑重算单位密度的体积、表面积、质心及关于质心的世界坐标系 3×3 行主序惯性张量。体积、面积、质心和惯性的单位分别是模型长度单位的三次方、平方、一次方和五次方。支持平面、Line/LineSegment 边组成的双边流形闭壳，面可凹且可带孔。`body_shell_regions` 以严格包含深度给出 Material/Void 和直接父壳；`body_mass_properties` 用平行轴定理累加偶数深度材料壳、扣除奇数深度空腔壳，面积保留全部边界面积。多壳相交、重叠或在建模容差内接触返回 `InvalidTopology / AXM-QUERY-E-0006`；曲面、曲边、空/开/非流形壳、共享边同向、环绕向错误、非共面或零体积均结构化失败且无部分值。查询不发布网格、不写缓存、不改变 Eval 状态或事务写计数；事务内删除/替换即时可见，回滚后恢复。cycle-0076 起明确拒绝兼容代理面，包括重新组壳后的原生解析体代理面；原生解析质量资格仅供通用查询使用，完整范围与稳定失败阶段见 §6.1.3。

`edge_curve_interval / edge_length / loop_length / face_boundary_length` 组成拓扑边界长度接口族，单位为模型长度单位。`create_trimmed_edge` 创建的边保存有向参数区间，`edge_length` 调用同一支撑曲线的区间弧长实现，覆盖圆/椭圆/抛物线/双曲线、Bezier/BSpline/NURBS、折线和复合链；递减区间合法，区间起止必须分别对应 v0/v1。旧 `create_edge` 保持兼容：Line/LineSegment 继续按端点真实距离计算，未携带区间的曲边仍返回 `NotImplemented / AXM-CORE-E-0004`，绝不以弦长冒充弧长。显式曲边还会把解析区间极值或控制点凸包纳入面/壳/体拓扑包围盒，避免半圆等边界只取端点而低估范围。

裁剪参数越域在创建时返回 `InvalidInput / AXM-GEO-E-0004`；参数非有限/相同返回 `InvalidInput / AXM-CORE-E-0002`；参数求值与拓扑端点超出线性容差返回 `InvalidTopology / AXM-TOPO-E-0008`，诊断携带两端距离和容差。失败发生在 EdgeId 分配前，不增加事务写计数或几何缓存。零长度或存量不一致边同样返回 `AXM-TOPO-E-0008`，缺失边引用返回 `AXM-TOPO-E-0006`。环查询要求闭合且无重复成员；面查询返回**外环加全部内环**的长度，方向无关。所有失败均无部分数值；查询从当前拓扑重算，不修改模型、事务写计数、几何/网格缓存或 Eval 状态。

### 6.1.1 实体空间查询（cycle-0074，已通过完整门禁）

`locate_point` 对真实平面直边多面体材料返回 `Inside/Outside/Boundary`，空腔为 Outside，偶数包含深度的材料岛为 Inside。非空实体总是返回最近真实边界位置（可在面内、边或顶点）、非负距离及所属 `ShellId/FaceId`；等距时按 ShellId、FaceId 的稳定句柄顺序选择，与壳插入顺序无关。最近距离不大于有效 `position_tolerance` 时归为 Boundary。内部零壳分支约定 Outside 和空 `nearest_boundary`；公共 `create_body({})` 拒绝，cycle-0075 未构造或验收零壳查询分支。

`clip_segment` 返回按无量纲 `t` 递增、内部不重叠的材料/边界区间，省略 Outside 区间。Inside 指开区间内部为材料，端点包含且可位于边界；Boundary 指共面边界段或孤立相切点 `[t,t]`。相邻同类区间合并，已被区间包含的接触点不重复返回。`material_length` 仅累加 Inside 区间的实际长度，共面段/相切点不计入；完全无交集成功返回空集合和零长度。内部零壳分支也约定空集合和零长度，但不属于本批已验证范围。反向线段仍按自身从 start 到 end 的参数返回。

位置容差为模型长度单位，0 使用内核有效线性容差（至少 `1e-12`），负数或非有限值失败。线段长度不大于该容差时为退化失败；该容差不膨胀材料，不合并可分辨的薄层或窄空腔。求交/共面采用局部浮点舍入尺度；边界事件在归一化参数舍入尺度内无法可靠分离时返回 `NumericalInstability`，无部分区间。

两入口共用 `shell_mass_properties/body_shell_regions` 的闭壳质量、壳间接触和严格包含前置检查，支持凹面、孔、无序多壳、空腔与材料岛。cycle-0075 加严体类门禁：接受 `ExactBRep` 的 box/wedge、已物化真实多面体 Sweep、用户 Generic 闭壳及 cycle-0082 受认证 run_rebuilt BooleanResult（§8.2.3）；解析曲面体与旧 bbox 占位建模体即使拥有平面壳也拒绝，完整支持矩阵见 §6.1.2。仅支持无自交的嵌入平面直边双边流形闭壳；本批未新增壳自身全局自交证明。共享前置检查明确拒绝曲面/曲边（含显式 trim 曲边），不以端点弦替代曲边，并检查面边界位于支撑平面。壳相交/重叠/建模容差接触仍失败；调用方的位置容差不改变壳间建模容差。

`max_triangle_tests` 默认 1000000，须非零，仅限制前置检查之后的新三角形距离、求交和绕数计算；成功的 `triangle_tests` 是实际工作量，可用作精确预算重放，预算耗尽不返回部分结果。既有质量/壳关系前置检查不计入预算，尚无大规模空间加速。局部 `long double` 距离/平面裁剪和补偿立体角绕数用于计算，非有限坐标及溢出结构化失败；错误码见字典的 cycle-0074 空间查询条目。

查询从当前拓扑重算，事务替换/删除即时可见，回滚后恢复；仅增加诊断和一次顶层 Topo 查询审计，不创建几何或 MeshId，不写缓存、Eval 状态或事务写计数。`axiom_query_eval_test` 的最近面/边/角、容差带、穿透/反向/内部/端点/相切/共面/空集、薄层、多壳奇偶、姿态尺度、预算/数值失败、支撑面编辑回滚和不污染回归随 cycle-0074 完整 CTest 16/16 通过。

### 6.1.2 Stage 3 截面、最近点与距离支持矩阵（cycle-0075 / S3-QUERY）

当前退出统一矩阵及 cycle-0079 自动化证据见 [§6.1.4](#614-stage-3-统一退出支持矩阵cycle-0079--s3-exit)；本节保留专项合同与历史门禁。

`TopologyQueryService::section` 与 `QueryService::section_detailed` 返回同一 `BodyPlaneSection`；通用 `closest_point` 复用 `locate_point`，通用/专用 `closest_points` 返回同一 `BodyDistanceQuery`，`min_distance` 取其 distance 并保留诊断。以下“支持”均要求当前拓扑通过闭壳、面支撑、壳间接触与包含前置检查，且输入为无自交的嵌入双边流形闭壳。

| 当前体类 / 建模路径 | 截面、最近边界与实体距离 | 质量属性 / 表示合同 | 回归依据与限制 |
|---|---|---|---|
| ExactBRep box / wedge | 实际平面直边边界的浮点计算 | 通用质量与 `body_mass_properties` 共用当前拓扑 | `tests/eval/query_eval_test.cpp::stage3_section_distance_regression`；单位盒斜截面、凹孔、多壳、非等边平移楔体参考 |
| 真实物化的显式 polygon `extrude`、折线 `sweep`、拓扑兼容 `loft` | 支持真实多面体，凹/孔按材料奇偶填充 | ExactBRep 标签；真实拓扑 bbox、质量、面/壳归属及 provenance 一致 | `tests/ops/ops_heal_test.cpp::test_stage3_model_query_chain`；只覆盖各入口已声明可物化子域，不扩大自动环匹配或任意放样 |
| 真实物化的 `extrude_scaled/extrude_twisted/extrude_with_law`、`sweep_scaled/sweep_with_scale_law/sweep_with_law`、采样曲线 sweep、有向部分/整周 revolve | 查询实际采样与剖分多面体，非连续曲面的解析解 | 通用质量每次消费当前拓扑，不恢复旧 Sweep 质量缓存 | 同上及 `test_directed_interval_revolutions`、既有建模回归；新增跨模块截面断言覆盖其中选定夹具，不能视为所有采样变体穷举 |
| 平面直边 Face `thicken`（cycle-0077） | 支持当前真实棱柱的截面、最近边界与实体距离，共用真实 Sweep 门禁 | 独立 owned ExactBRep；通用/体/壳质量从当前拓扑积分 | `test_planar_face_thicken` 截面面积及最近边界距离=1；Query `stage3_section_distance_regression` 质量/最近点；曲面、曲边、代理输入不创建结果 |
| ExactBRep 用户 Generic 平面直边闭壳；独立壳、嵌套空腔与材料岛 | 支持；按严格包含深度奇偶计算材料 | 体积材料加/空腔减/岛加，面积计全部边界 | Query 多壳截面面积 `100−36+4=68`、空腔悬浮体距离 `0.5`；壳相交/重叠/建模容差接触拒绝，未证明壳自身全局嵌入 |
| run_rebuilt受认证BooleanResult（cycle-0082） | 当前owned真实边界进入共享空间查询门禁；人工节点截面可能数值拒绝 | 质量从当前拓扑积分，无bbox/来源fallback | workflow/ops_heal固定V/A/S及query_eval回滚链，范围见§8.2.3 |
| sphere / cylinder / cone / torus 等解析曲面体、曲面/曲边闭壳 | `NotImplemented / AXM-CORE-E-0004`，`query.*.support_gate` | 仅未编辑原生 sphere/cylinder/cone/torus 的通用质量保留解析路径；专用体/壳质量拒绝兼容代理面（cycle-0076） | sphere 拒绝回归与共享体类门禁；Geo 曲面最近点/单面面积的既有支持不等于实体查询支持 |
| 未真实物化 Sweep、历史占位记录、其他占位或非 ExactBRep 体 | 同上，不能用 bbox 壳代替实体 | 质量明确失败且无 value；无 bbox/来源/创建缓存恢复（cycle-0076） | cycle-0077 已将真实平面 Face thicken 纳入支持，旧代理记录仍拒绝 |
| 已删除体/面、支撑面错配、活动事务内改成曲面 | `preflight` 或 `support_gate` 失败，无部分结果 | 支持体类质量同步失败，回滚后恢复 | Query/Ops 编辑失败、缓存与 Eval 不污染、rollback/Strict 回归 |

截面三角形覆盖实际材料区域，扣除孔/空腔并保留材料岛。`vertices` 为世界坐标，`triangles` 是有效顶点索引，`area` 是实际三角面积之和；`boundary_segments` 是真实边界三角片与平面的交段，可重复，共面片含三角化内部边，因此不是已去重的闭合轮廓环。`contact_points` 保留点接触。完全无交集成功返回空容器、area=0、无效 bbox；共面面保留真实面积，纯线/点相切成功零面积并保留接触信息。

`vertices/triangles` 仅描述填充的面积区域，接触线/点由独立容器描述；顶点不保证焊接，接触记录不保证去重。`bbox` 包含交段与接触点，纯线/点接触也可有有效 bbox。因此 `area == 0` 或 `triangles.empty()` 只表示无面积，不能单独用来判定完全无交集；应同时检查接触容器。

兼容 `QueryService::section` 只在成功且有面积三角形时显式发布一个真实结果 MeshId，不填体/面三角化缓存；空交集与纯线/点接触成功返回有 value 的 `MeshId{}`，不发布网格。失败没有 value 且沿用详细入口诊断。调用方应分别检查 Result.value 与句柄是否为零，不能把零句柄当失败或传入网格消费入口。

点查询在材料内部也返回最近**边界**距离。体间查询则求两个材料闭集距离：相交、包含和相切为 0，bbox 重叠不能判作相交。正距离时双侧见证属于真实边界并有 FaceId/ShellId；零距离时内部见证的归属句柄可为零，等距按稳定壳/面 ID 选择。距离等于实际见证点欧氏距离，单位为模型长度单位。

位置容差不吸附近邻截面平面，不膨胀距离实体，也不借用线段裁剪舍入带抵消可表示正间隙。回归保留 `nextafter(1,2)` 外部平面空集与 `1e-18` 正间隙；这不表示任意尺度均有相同分辨率。平面符号、平面坐标事件或远隔小分量无法可靠分辨时 `AXM-QUERY-E-0002 / query.section.numeric` 失败，无部分面积。截面法向须有限且非零（不必单位长度）；坐标有限，容差须有限且非负，0 使用内核有效容差。

默认工作预算 1000000、须非零；仅计质量/壳层级前置检查之后工作。截面还计交段扫描、区域分类，体间距离按三角形对及包含绕数计数；`triangle_tests` 可用于同输入精确重放。预算耗尽为 `InvalidInput / AXM-CORE-E-0002 / query.*.budget`，不是部分成功。前置检查仍不纳入预算，无大规模加速结构。

详细查询每次消费当前拓扑，只增加诊断与 Topo 只读审计，不写模型/网格/交线存储、曲线/曲面求值缓存、体/面三角化缓存、Eval 失效/重算或事务写计数。编辑失败不得恢复旧拓扑质量或 bbox fallback；rollback 后截面与质量恢复。内部零壳分支约定截面成功空、距离 `DegenerateGeometry / AXM-QUERY-E-0001 / query.distance.empty_gate`，但公共 `create_body({})` 返回 `OperationFailed / AXM-TX-E-0001`，本批仅验证创建拒绝及写计数不变，未构造或验收零壳查询分支。

调度器修复后完整构建及 CTest **16/16 通过、148.44 s**；Query/Eval **0.90 s**、Ops/Heal **121.81 s**、representation/IO **9.87 s**。逐项独立参考与四条验收证据见 [测试与验收方案 §1.2](../quality/AxiomKernel_测试与验收方案.md#12-cycle-0075--s3-query-门禁与逐项证据)，稳定失败码见 [错误码字典](../diagnostics/AxiomKernel_错误码与诊断码字典.md)。该节保留 S3-QUERY 历史代码门禁与支持范围；Stage 3 历史退出任务为 cycle-0079 / S3-EXIT（§6.1.4）；当前唯一任务为 S5-TESSELLATION（§7.3.2），正式状态见当前进度 §5.2.1，不由历史日志推断文档检查/提交成功。Stage 3 / FR-QUERY-001 保持进行中。

本矩阵由 cycle-0078 / S3-CONSISTENCY 继续回归锁定：五类代表建模结果的截面/最近点/距离/质量、owned 表示、有效来源与 Eval 绑定闭环通过；成功提交、失败与回滚的一致性见 [§7.3.1](#731-stage-3-表示来源与-eval-一致性合同cycle-0078--s3-consistency) 和 [验收 §1.5](../quality/AxiomKernel_测试与验收方案.md#15-cycle-0078--s3-consistency-门禁与逐项证据)。全量 16/16、164.60 s；支持体类不扩大，详细查询只读，拓扑修改/恢复自身会传播 Eval dirty。

### 6.1.3 Stage 3 质量属性支持矩阵（cycle-0076 / S3-MASS）

当前退出统一矩阵及 cycle-0079 自动化证据见 [§6.1.4](#614-stage-3-统一退出支持矩阵cycle-0079--s3-exit)；本节保留专项合同与历史门禁。

`QueryService::mass_properties(BodyId)` 返回 `Result<MassProperties>`。密度固定为 1；`volume/area/centroid` 的单位为模型长度单位的三次方/平方/一次方，`inertia` 为关于质心的世界坐标系 3×3 行主序张量，单位为长度的五次方。张量非对角项为负积惯量，坐标旋转按 `R I Rᵀ` 变换。接口没有密度参数；均匀密度 ρ 的物理质量为 `ρ*volume`，物理惯性为 `ρ*inertia`，面积与重心不变。

| 当前模型 / 路径 | 质量来源与支持范围 | 边界与回归 |
|---|---|---|
| ExactBRep box / wedge | 委托 `TopologyQueryService::body_mass_properties`，从当前平面直边闭壳积分全部属性 | 盒体解析积分、楔体三角棱柱全张量参考；非等边/平移及当前支撑平面一致性 |
| 真实物化 polygon extrude、直线/折线 sweep、拓扑兼容 polygon loft | 同一当前拓扑积分；支持各入口已声明的凸/凹及孔子域 | `test_stage3_model_mass_references` 独立棱柱/方截面积分；逐面面积总和、通用/体/壳一致、Strict 与 owned_topo_welded 表示 |
| 平面直边 Face thicken（cycle-0077） | 当前真实棱柱拓扑积分，平面几何及质量在浮点容差内精确 | `test_planar_face_thicken` 有符号矩形区域积分独立核对三入口全部属性；V=区域面积×厚度，A=2×区域面积+全部环周长×厚度，含凹轮廓与分离非嵌套孔 |
| revolve/revolve_between、曲线 sweep、比例/扭转/联合律与其他已物化 Sweep | 返回实际采样剖分多面体的质量；创建时 Sweep 数值和来源记录不作为查询依据 | 有向整周/部分旋转 Green 边积分参考；连续光滑体只作采样误差对照，不能代替当前边界；一般曲线/律无统一解析误差保证 |
| ExactBRep 用户 Generic 平面直边嵌入闭壳、多壳材料/空腔/岛 | 严格包含层级：独立材料相加、奇数深度空腔相减、偶数深度岛再加；用平行轴定理汇总惯性 | 壳输入顺序不决定角色；面积包含外边界和全部空腔/岛边界。相交、重合、重叠或建模容差接触多壳拒绝；未新增壳自身全局嵌入证明 |
| 原生 PrimitiveFactory sphere / cylinder / cone / torus，ExactBRep 且未编辑 | 原生记录具有解析质量资格，体积/完整边界面积/重心/质心惯性按解析公式计算 | 兼容拓扑是代理壳而非物理边界；专用 `shell_mass_properties/body_mass_properties` 拒绝。此解析资格不扩展实体截面/距离支持 |
| 编辑后的原生解析体 | 成功替换面、改变 PCurve 绑定、删除面/壳等撤销解析资格，立即 support_gate 拒绝 | 失败编辑不撤销；保存点/整事务回滚恢复资格与数值，提交后持续拒绝；不能用其代理壳积分重新认证 |
| 旧 label-only Sweep/extrude、历史占位 thicken 记录、兼容run的占位Boolean/Modified、未知或不支持体类/表示 | 明确 `NotImplemented / AXM-CORE-E-0004 / query.mass_properties.support_gate`，无 value | 删除 bbox、Boolean/Modified 来源组合及 Sweep 创建缓存质量恢复。cycle-0077 平面直边 Face thicken 已有真实质量路径，历史代理记录仍拒绝 |
| metadata-only STEP 等恢复记录、mesh/implicit 派生 BRep、重新组壳的兼容代理面 | 不继承原生解析资格，不能以标签/来源/ExactBRep 名称证明实际边界 | `representation_io_test` 检查导入与派生体拒绝；代理面标记随克隆及布尔 imprint 切分保留，重新归属和删除原 owner 后仍拒绝 |

真实多面体要求无自交的嵌入平面直边双边流形闭壳。每次消费当前拓扑，合法编辑会改变结果；换曲面、支撑平面错配、开壳或删除目标则即时失败，即使已热缓存网格也不恢复旧数值。成功/失败质量查询不发布网格、不改模型、三角化/求值缓存、Eval invalid/recompute 或事务写计数；诊断与 Topo 只读审计可增加。回滚恢复当前拓扑、质量及来源记录，不以 provenance 冒充几何证明。

所有失败无 `value`，不返回部分体积/面积/重心/惯性。稳定阶段为 `query.mass_properties.support_gate/preflight/empty_gate/numeric`；原生解析溢出/惯性下溢、非有限结果或非正惯性对角项为 `NumericalInstability / AXM-QUERY-E-0003 / numeric`。无效/删除句柄为 `InvalidInput / AXM-CORE-E-0001 / preflight`；其余拓扑根因保留错误码。公共 API 无零壳体夹具，删除最后壳会删除 owner Body，回归核对的是 InvalidInput/preflight，内部 empty_gate 不声称已覆盖。

独立参考、采样误差和三条 `stage_evidence` 见 [测试与验收 §1.3](../quality/AxiomKernel_测试与验收方案.md#13-cycle-0076--s3-mass-门禁与逐项证据)。调度器完整构建成功，全量 CTest **16/16、0 失败、147.97 s**；此为 S3-MASS 历史代码门禁；历史任务 S3-EXIT 的证据见 §6.1.4 与测试与验收 §1.6；当前唯一任务S5-TESSELLATION见§7.3.2，正式状态见当前进度 §5.2.1。Stage 3 / FR-QUERY-001 继续进行中。

本矩阵由 cycle-0078 / S3-CONSISTENCY 继续回归锁定：五类代表建模结果的截面/最近点/距离/质量、owned 表示、有效来源与 Eval 绑定闭环通过；成功提交、失败与回滚的一致性见 [§7.3.1](#731-stage-3-表示来源与-eval-一致性合同cycle-0078--s3-consistency) 和 [验收 §1.5](../quality/AxiomKernel_测试与验收方案.md#15-cycle-0078--s3-consistency-门禁与逐项证据)。全量 16/16、164.60 s；支持体类不扩大，详细查询只读，拓扑修改/恢复自身会传播 Eval dirty。

### 6.1.4 Stage 3 统一退出支持矩阵（cycle-0079 / S3-EXIT）

此表保留 Stage 3 历史支持合同；当前唯一任务S5-TESSELLATION见§7.3.2。此表统一 §6.1.2 截面/最近点/距离、§6.1.3 质量、§8.1.1 五类建模与 §7.3.1 表示/来源/Eval 合同，复用既有接口及错误码，不扩展支持域。`stage_task_id=S3-EXIT`、`stage_outcome=ready_for_acceptance`；调度器独立完整构建成功，全量 CTest **16/16、0 失败、163.78 s**。三条验收证据与退出项映射见 [验收 §1.6](../quality/AxiomKernel_测试与验收方案.md#16-cycle-0079--s3-exit-门禁与逐项证据)，正式状态见 [当前进度 §5.2.1](../plan/AxiomKernel_当前开发进度.md#521-stage-5-当前退出任务)。

以下 17 行逐一对应 `tests/eval/query_eval_test.cpp::stage3_exit_support_matrix_regression` 中的 Row.label（main 已调用）。V/A/C 分别为体积/面积/质心，S 为水平截面积；单位及世界坐标质心惯性沿用 §6.1.3。距离列同时涵盖 `closest_points`、正反 `min_distance`，最近点涵盖 `closest_point/locate_point`。支持行先通过 Strict；矩阵不以 ExactBRep 标签、bbox 或来源作为物理边界证明。

| 体类 / 建模路径（自动化行） | 精度与范围 | 截面 S | 最近边界点 | 实体距离 | 通用质量 V / A / C | Topo 体质量 |
|---|---|---|---|---|---|---|
| box | 当前平面多面体，浮点容差内精确 | 6 | 支持 | 支持 | 24 / 52 / (1,1.5,2) | 同一当前边界 |
| wedge | 当前三角棱柱，浮点容差内精确 | 3 | 支持 | 支持 | 12 / (26+4√13) / (2/3,1,2) | 同一当前边界 |
| extrude/holed | 显式 polygon 凸/凹及分离非嵌套孔，代表夹具为贯通孔棱柱 | 12 | 支持 | 支持 | 36 / 96 / (2,2,1.5) | 同一当前边界 |
| sweep/line | 有限直线显式 polygon 平移棱柱；其他开放/周期曲线按采样合同 | 6 | 支持 | 支持 | 24 / 52 / (1,1.5,2) | 同一当前边界 |
| loft/compatible | 拓扑兼容对应顶点，代表夹具侧壁共面；非共面侧壁按实际三角剖分 | 6 | 支持 | 支持 | 24 / 52 / (1,1.5,2) | 同一当前边界 |
| thicken/planar | 当前平面直边 Face，有限正厚度沿支撑法向单侧；代表夹具在 z∈[-2,0] | 6 | 支持 | 支持 | 12 / 32 / (1,1.5,-1) | 同一当前边界 |
| revolve/chords | 有向 −π/2 弦面采样多面体；部分/整周沿用 §8.1 门禁 | a | 支持实际多面体 | 支持实际多面体 | 4a / (2a+4p) / (c,-c,2) | 同一采样边界 |
| generic/closed | 用户 Generic 平面直边嵌入双边流形闭壳 | 6 | 支持 | 支持 | 24 / 52 / (1,1.5,2) | 同一当前边界 |
| generic/cavity/island | 无序多壳严格包含，材料加/空腔减/岛加；面积计全部边界 | 68 | 支持 | 支持 | 792 / 840 / (5,5,5) | 同一当前边界 |
| sphere/native | 未编辑原生解析质量资格，r=2；兼容壳为代理 | 不支持 | 不支持 | 不支持 | 32π/3 / 16π / (0,0,0) | 不支持代理积分 |
| cylinder/native | 未编辑原生解析质量资格，r=2、h=6，夹具中心在原点 | 不支持 | 不支持 | 不支持 | 24π / 32π / (0,0,0) | 不支持代理积分 |
| cone/native | 未编辑原生解析质量资格，r=2、h=6、apex 在原点 | 不支持 | 不支持 | 不支持 | 8π / (4π+2π√40) / (0,0,4.5) | 不支持代理积分 |
| torus/native | 未编辑原生解析质量资格，R=5、r=2 | 不支持 | 不支持 | 不支持 | 40π² / 40π² / (0,0,0) | 不支持代理积分 |
| sweep/label_only | 历史无显式轮廓占位，不计真实主路径 | 不支持 | 不支持 | 不支持 | 不支持 | 不支持 |
| mesh/derived | mesh_to_brep 派生，不继承物理边界/解析质量资格 | 不支持 | 不支持 | 不支持 | 不支持 | 不支持 |
| boolean/placeholder | 兼容run占位 Boolean 结果，不按来源组合恢复质量；真实run_rebuilt另见§8.2.3 | 不支持 | 不支持 | 不支持 | 不支持 | 不支持 |
| invalid_handle | 无效句柄，所有入口 preflight 拒绝 | 拒绝 | 拒绝 | 拒绝 | 拒绝 | 拒绝 |

矩阵独立参考：旋转区间数 `n=vertex_count/4−1`、`δ=π/(2n)`，`a=2.5n sinδ`、`p=10n sin(δ/2)+2`、`c=19/(15n tan(δ/2))`。空腔/岛 V=`1000−216+8=792`、A=`600+216+24=840`、S=`100−36+4=68`，不能用外 bbox 答案取代。远方盒为 (50,50,50) 起始的单位盒；本体最近角点按上表前九行依次为 (2,3,4)、(0,3,4)、(4,4,3)、(2,3,4)、(2,3,4)、(2,3,0)、(3,0,4)、(2,3,4)、(10,10,10)，双侧见证距离取与 (50,50,50) 的欧氏长度；点查询为 (60,60,z_section)，参考边界点取该角点的 x/y 与 z_section。质量/点/距离/截面比较容差为 `2e-8*max(1,abs(reference))`。全九项惯性的独立参考由 `stage3_mass_authority_regression/topology_mass_properties_regression` 及 Ops `test_stage3_model_mass_references/test_planar_face_thicken` 保留，不将入口间一致代替独立参考。

所有不支持格要求无 value、可检索 diagnostic_id、`NotImplemented / AXM-CORE-E-0004` 与 `query.{section,closest_point,distance,mass_properties}.support_gate`；无效句柄为 `InvalidInput / AXM-CORE-E-0001` 与对应 `.preflight`。成功详细查询与矩阵拒绝不改变对象/几何数量、next_object_id、Mesh/体面三角化/曲线曲面求值/交线存储、Eval invalid/recompute 或事务写计数；诊断与 Topo 读审计可增长。Strict 可能先创建网格，因此只读快照在 Strict 完成后取得。兼容 section 的成功有面积分支仍会发布 MeshId（§6.1.2）；本矩阵仅在拒绝行检查该入口，不能据此声称所有兼容 section 都无写入。

原生解析资格的成功编辑撤销、失败编辑保留、保存点/回滚恢复、提交后持续拒绝，以及 metadata/mesh/implicit 不继承资格，沿用 §6.1.3。owned 表示缓存必须命中当前边界与正确 source_body，失败原子拒绝且无 bbox/创建参数 fallback；合法提交、保存点/显式/析构/取消回滚及删除清理沿用 §7.3.1。旧网格为快照，metadata 的 bbox_proxy 仅显示，Eval recompute 仅管理图，不承诺 ID 回收或算法自动重算。

“精确”仅指平面多面体浮点容差内的计算；曲线 sweep、旋转及比例/扭转律均测实际采样多面体，无通用连续曲面误差保证。通用曲面/曲边闭壳积分与实体空间查询、曲面/曲边 thicken、任意 loft 环匹配、相交/接触多壳、壳自身全局嵌入证明、空间加速、完整 trim/标准交换和工业 Boolean 均不作为 Stage 3 完成前提；内部 empty_gate 无公共夹具，不宣称已验收。工业需求 FR-OPS-001 / FR-QUERY-001 仍进行中，Stage 3 正式退出须调度器完成文档门禁及提交。

### 6.2 拓扑事务接口

```cpp
enum class TopologyIsolationLevel : std::uint8_t {
  Unspecified = 0,
  SnapshotSerializable = 1,
};

class TopologyCancellationToken {
public:
  bool can_be_cancelled() const noexcept;
  bool is_cancellation_requested() const noexcept;
};

class TopologyCancellationSource {
public:
  TopologyCancellationToken token() const noexcept;
  bool request_cancellation() noexcept;
  bool is_cancellation_requested() const noexcept;
};

struct TopologyCancellationMetrics {
  std::uint64_t observed_transaction_count;
  std::uint64_t rolled_back_transaction_count;
  std::uint64_t rolled_back_write_operations_total;
  std::uint64_t last_rolled_back_write_operations;
};

class TopologySavepoint {
public:
  TopologySavepoint() = default;
  bool is_valid() const noexcept;
};

struct TopologySavepointMetrics {
  std::uint64_t created_count;
  std::uint64_t rollback_count;
  std::uint64_t released_count;
  std::uint64_t discarded_nested_count;
  std::uint64_t rolled_back_write_operations_total;
  std::uint64_t last_rolled_back_write_operations;
};

class TopologyTransaction {
public:
  TopologyTransaction(TopologyTransaction&&);
  TopologyTransaction& operator=(TopologyTransaction&&) = delete;
  TopologyTransaction(const TopologyTransaction&) = delete;
  TopologyTransaction& operator=(const TopologyTransaction&) = delete;
  ~TopologyTransaction() noexcept;
  // 坐标必须为有限值；NaN/±Inf 返回 InvalidInput / AXM-CORE-E-0002，拓扑与事务写计数不变。
  Result<VertexId> create_vertex(const Point3&);
  Result<EdgeId> create_edge(CurveId, VertexId, VertexId);
  Result<EdgeId> create_trimmed_edge(CurveId, Scalar start_parameter,
                                     Scalar end_parameter,
                                     VertexId v0, VertexId v1);
  // validate_edge 要求两个拓扑端点在引用的 3D Curve 上（采用内核线性容差）；不一致返回 InvalidTopology / AXM-TOPO-E-0008。
  Result<CoedgeId> create_coedge(EdgeId, bool reversed);
  // 按定向端点 ID 首尾闭合，单共边不豁免；未闭合返回 AXM-TOPO-E-0002，闭合前重复经过顶点返回 AXM-TOPO-E-0023，失败不写入环或事务计数。
  Result<LoopId> create_loop(std::span<const CoedgeId>);
  // 建面时外/内环至少三条共边（同曲线双弧环除外）；不足分别返回 AXM-TOPO-E-0003/0004。
  // 外环/内环及内环之间不得复用 EdgeId；返回 InvalidTopology / AXM-TOPO-E-0014，失败不分配面或改变索引、事务写计数。
  // 不同边界环不得共用 VertexId；返回 InvalidTopology / AXM-TOPO-E-0024，关联冲突环与顶点，失败不分配面。
  // 不同边界环的非平行直线边不得在三维空间端点相接；返回 InvalidTopology / AXM-TOPO-E-0027，失败不分配面。
  Result<FaceId> create_face(SurfaceId, LoopId outer_loop, std::span<const LoopId> inner_loops);
  // 成员面须引用存在的曲面，外环和所有内环须有效；受损引用返回 InvalidTopology / AXM-TOPO-E-0005，失败不分配壳 ID。
  Result<ShellId> create_shell(std::span<const FaceId>);
  Result<BodyId> create_body(std::span<const ShellId>);

  Result<void> delete_face(FaceId);
  Result<void> replace_surface(FaceId, SurfaceId);

  Result<VersionId> commit();
  Result<void> rollback();
  Result<TopologySavepoint> create_savepoint();
  Result<void> rollback_to_savepoint(TopologySavepoint);
  Result<void> release_savepoint(TopologySavepoint);
  Result<std::uint64_t> active_savepoint_count() const;
  Result<void> poll_cancellation();
  Result<bool> cancellation_requested() const;
  Result<bool> cancellation_observed() const;
  Result<std::uint64_t> cancelled_write_operation_count() const;

  Result<std::uint64_t> write_operation_count() const;
  Result<TopologyIsolationLevel> effective_isolation_level() const;
};
```

同一内核实例仅允许一个活动拓扑写事务；已有所有者时，`begin_transaction()` 返回可查询但已关闭的事务对象，其写入、提交与回滚均失败且不修改模型。所有者提交、回滚或离开作用域后释放写槽。该约束不等价于跨进程数据库 SERIALIZABLE。

> 说明：`TopologyTransaction` 的完整签名见 `include/axiom/topo/topology_service.h`；事务具有唯一所有权，只能移动构造，不能复制或移动赋值。移动后的源对象处于可安全查询的关闭状态，写入、提交和回滚均被拒绝，事务权限仅由目标对象持有。活动事务若未显式提交或回滚便离开作用域，`noexcept` 析构会自动回滚成功写入；空事务析构是纯 no-op，已关闭事务与移动后的源对象析构不改变模型。另含 `set_coedge_pcurve`、删除壳/体、以及 trim 桥接审计读数 `coedge_pcurve_bind_count()` / `coedge_pcurve_clear_count()` 等。`write_operation_count()` 统计本事务内每次**成功**的写操作（创建/删除实体、`replace_surface`、每次 `set_coedge_pcurve` 含清除）；回滚或 `clear_tracking_records()` 归零。`clear_tracking_records()` 仅在提交或回滚后允许调用，可重复清理且不改变模型；活动事务（含空事务）返回 `OperationFailed` / `AXM-TX-E-0006`，保留创建记录、修改快照与计数。`effective_isolation_level()` 当前实现返回 `SnapshotSerializable`（单事务 + 快照回滚的工程占位，见头文件注释）。

第 69 批支持把 `TopologyCancellationSource::token()` 传给 `begin_transaction(token)`。默认令牌不可取消；源及其副本共享幂等信号。预取消事务不取得写者槽；活动事务在显式 `poll_cancellation`、全部拓扑写入口、`commit`、显式 `rollback` 或作用域退出边界观察取消，随后用完整快照恢复创建、删除、曲面替换与反向索引，释放写者槽，不推进版本或成功提交审计，并返回 `OperationFailed / AXM-TX-E-0007`。重叠而被拒绝的事务不能通过取消影响实际所有者，移动事务唯一转移取消权限。`TopologyService::has_active_write_transaction()` 与 `cancellation_metrics()` 提供只读状态和累计审计。

第 74 批新增嵌套保存点。`create_savepoint()` 捕获拓扑主存储、完整撤销基线和事务写审计；`rollback_to_savepoint()` 保留目标供重复回滚，回滚到外层时使所有更内层句柄失效；`release_savepoint()` 只允许 LIFO 释放最内层点，并保留其后写入供最终提交。默认、跨事务、已失效和非栈顶句柄以 `OperationFailed / AXM-TX-E-0003` 失败且不改模型；对已请求取消的事务，完整事务恢复优先于局部保存点回滚，返回 `AXM-TX-E-0007` 并清空保存点。移动构造会转移保存点所有权。`TopologyService::savepoint_metrics()` 返回创建、回滚、释放、失效内层点与丢弃写次数的累计审计；`core_runtime_invariants_hold()` 同时检查取消与保存点审计自洽性。当前保存点是内存全拓扑快照，不回收已分配对象 ID；仍为单活动写者，取消不抢占正在执行的单次调用，BOOL/HEAL/IO 内部阶段轮询和更细粒度隔离仍待实现。

### 6.3 拓扑验证接口

```cpp
class TopologyValidationService {
public:
  Result<void> validate_edge(EdgeId) const;
  Result<std::optional<FaceBoundaryConflict>> first_boundary_conflict(
      LoopId outer_loop, std::span<const LoopId> inner_loops,
      Scalar linear_tolerance = 0.0) const;
  Result<void> validate_face(FaceId) const;
  Result<void> validate_shell(ShellId) const;
  // Strict 闭合性：每条边须由两个不同面反向配对，且全部面只能形成一个连通分量。
  // 多个独立闭合分量返回 InvalidTopology / AXM-TOPO-E-0018。
  Result<void> validate_shell_closedness(ShellId) const;
  Result<void> validate_body(BodyId) const;
};
```

`first_boundary_conflict` 返回不同边界环间按稳定遍历顺序遇到的首个冲突；无冲突是成功的空 `optional`。Line/LineSegment、CompositePolyline 和纯线性 CompositeChain 保持解析分段谓词；带显式 trim 区间的圆锥曲线、Bezier、BSpline、NURBS 及含真曲线子项的 CompositeChain 复用 Geo 层无诊断/无缓存的有限区间求交。除 `ProperIntersection/EndpointTouch/CollinearOverlap/NearContact` 外，连续曲线重合返回 `CurveOverlap`。`error_controlled`、`solver_tolerance`、`curve_evaluations` 与 `parameter_rectangles_processed` 区分求解路径并给出容差/工作量证据；解析线性路径的这些字段为 false/0。递减区间可用，复合曲线内部分段点不冒充拓扑边端点。

默认 `linear_tolerance == 0` 使用内核线性容差；有限正值按策略上下限钳制，负值或非有限值返回 `InvalidInput`。`create_face` 与 `validate_face` 复用同一流程；真曲线内部交、端点接触和连续重合分别使用 `AXM-TOPO-E-0032/0033/0031`，缺少必要 trim、曲线损坏或预算/数值失败使用 `AXM-TOPO-E-0030` 闭合失败。失败不返回部分冲突；建面拒绝不分配 `FaceId`、不改反向索引或事务写计数。一般高阶曲线连续重合仍只在 Geo 求交器可证明相同参数化或解析分段重合时报告，近接能力受统一求交预算约束。

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
  bool use_principal_curvature_refinement; // 默认 true；受支持双线性片可直接满足误差界
  int refine_patch_chordal_max_passes; // 默认 3，合法 0..12；0 不关闭边界/必需误差检查
  bool uv_parametric_seam; // 默认 false；与 generate_texcoords 联用
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
  Result<MeshId> brep_to_mesh_local(BodyId, std::span<const FaceId> dirty_faces, const TessellationOptions&);
  Result<MeshId> brep_to_mesh_shell(BodyId, ShellId, const TessellationOptions&);
  /// 首次创建 MeshRep 并绑定网格；重复转换已由本入口建立的关联返回同一 BodyId。
  Result<BodyId> mesh_to_brep(MeshId);
  Result<MeshId> implicit_to_mesh(ImplicitFieldId, const TessellationOptions&);
  Result<std::vector<MeshId>> brep_to_mesh_batch(std::span<const BodyId>, const TessellationOptions&);
  Result<std::vector<BodyId>> mesh_to_brep_batch(std::span<const MeshId>);
  Result<RoundTripReport> verify_brep_mesh_round_trip(BodyId, const TessellationOptions&);
  Result<RoundTripReport> verify_mesh_brep_round_trip(MeshId, const TessellationOptions&);
  Result<TessellationCacheStats> tessellation_cache_stats() const;
  Result<void> export_tessellation_cache_stats_json(std::string_view path) const;
  Result<void> export_round_trip_report_json(const RoundTripReport& report, std::string_view path) const;
  Result<ConversionErrorBudget> conversion_error_budget_for_tessellation(const TessellationOptions&) const;
  Result<void> export_conversion_error_budget_json(const TessellationOptions&, std::string_view path) const;
};
```

### 7.3.1 Stage 3 表示、来源与 Eval 一致性合同（cycle-0078 / S3-CONSISTENCY）

当前退出统一矩阵及 cycle-0079 自动化证据见 [§6.1.4](#614-stage-3-统一退出支持矩阵cycle-0079--s3-exit)；本节保留专项合同与历史门禁。

无公开签名或错误码常量新增；`brep_to_mesh` 和事务 `rollback` 的公开合同注释已补齐。本批统一的是 Stage 3 已声明支持主链的当前实体语义，质量/查询范围沿用 §6.1.2～6.1.3，五类建模边界沿用 §8.1.1。

| 路径 / 状态 | 当前合同 | 回归依据 |
|---|---|---|
| 体级 / 面级缓存 | 包含 BodyId、owned 壳/面/环/共边/边/顶点、支撑及 PCurve/裁剪关联与 options；同时核对缓存网格存在且 source_body 等于目标体。相同 bbox/创建参数或重复几何不共享身份；mesh_to_brep 重绑定后原体必须重新转换 | `stage3_representation_consistency_regression` 不同几何同 bbox 与重复体 MeshId 区分、OBJ 独立质心、重绑定缓存拒绝 |
| full / local / shell owned 转换 | 全部面片三角化、索引检查、组装/焊接完成后才发布面网格和结果网格。任一面失败无 value，不分配 MeshId，不遗留部分网格/两级缓存，不回退 bbox 或创建参数；局部入口强制重算 dirty_faces，其余仅复用符合当前边界且 owner 正确的缓存 | Rep warm/cold/full/local/shell 位移支撑失败，计数/next_id/事务写数保持；Ops 五类三入口网格有效 |
| 平面直边 owned 面 | 检查当前完整环、Line/LineSegment 边和支撑平面一致性，以凹多边形/带孔区域三角化替代凸面 fan 假设；输出 bbox 来自实际网格顶点 | Rep 凹 L V=10/A=34、带孔 V=24/A=72 的 OBJ 三角积分；不扩展曲边/曲面实体查询 |
| 原生 primitive 编辑资格 | 创建参数三角化只适用于未编辑原生 primitive；成功面替换、PCurve 绑定及面/壳删除撤销资格，保存点/回滚恢复。编辑 box 使用当前有效平面边界；编辑 sphere/cylinder/cone/torus 的兼容代理壳不能重建原生曲面网格 | Rep native box/sphere 位移支撑拒绝，回滚恢复原 MeshId；其余解析体遵循同一资格门禁，不宣称本批逐体穷举 |
| provenance / 实体关联 | 新 owned 面/壳与来源实体区分；面 owner、支撑、source_bodies/source_shells/source_faces 和逐面来源可公开核对；thicken 追溯输入 Face 及其已有 owner。来源不能替代物理边界或继承解析资格 | `test_stage3_consistency_chain` 五类来源有效、thicken 精确来源、距离 FaceId/ShellId 属于对应体；SDK 共享源壳派生体清理不改变源 owned 面 |
| 编辑 / 恢复 / Eval | 成功换面、PCurve 改绑及面/壳/体删除使绑定节点与下游 dirty；非法修改不写入且不脏化。保存点、显式、析构或取消恢复会再次使受影响消费者 dirty，即使事务内已经 recompute；成功提交等价支撑保留质量/来源并使用新当前网格，后续失败回滚恢复该已提交状态 | `stage3_eval_rollback_consistency_regression` bound/downstream dirty、unrelated clean，提交/失败/恢复和独立查询参考 |
| 移除体的运行时清理 | 回滚/保存点/取消恢复后及提交时，移除已不存在体关联的运行时网格、体/面缓存和 Eval 体绑定，传播消费者失效；保留共享源壳及源体记录 | `stage3_discarded_body_runtime_regression` 网格/缓存回基线、移除体无绑定、源网格不变、Core/Eval 映射不变量 |

失败复用 `AXM-TES-E-0001`：`rep.tessellation.face`（当前面不可三角化）、`.support`（NotImplemented，不支持当前边界）、`.topology`（owned 壳/面缺失）、`.assembly`（无效索引/规模或空组装）；实体关联包含 BodyId，并按路径附 ShellId/FaceId。无效句柄/options 保留既有 CORE 前置错误，不能要求所有失败都使用 TES；具体对照见错误码字典。

旧 MeshId 是不可变边界快照，不代表编辑后的当前实体；存活体可保留历史边界缓存，恢复原边界且 source_body 未重绑定时可重新命中原 MeshId。移除体对应网格则不可继续使用。成功转换会发布网格、填缓存并更新缓存统计，但不自动 invalidate/recompute Eval；详细查询仍只读。Eval recompute 管理图状态与计数，不执行建模、质量或表示算法；不承诺事务对象 ID 回收。无新写入的重复保存点回滚不要求再次 dirty，非法修改也不脏化消费者；这两项与实际恢复已编辑边界时的重新失效分开断言。

旧 metadata-only 体仍可输出显式 `bbox_proxy` 显示网格，此兼容显示路径不证明 owned BRep、物理质量或实体空间查询。`RepresentationService::classify_point/distance_to_body` 仍为 bbox 辅助语义，实体查询使用 QueryService / TopologyQueryService；本批一致性不能扩大到这些旧辅助入口、通用解析体曲面壳或 metadata 派生体。

完整构建及全量 CTest **16/16、0 失败、164.60 s**，四个必需回归通过。按三条要求排列的独立参考与限制见 [验收 §1.5](../quality/AxiomKernel_测试与验收方案.md#15-cycle-0078--s3-consistency-门禁与逐项证据)；`stage_outcome=ready_for_acceptance`，本节为 cycle-0078 历史证据，当前 S3-EXIT 门禁及残余告警见验收 §1.6，正式验收条件见 [当前进度 §5.2.1](../plan/AxiomKernel_当前开发进度.md#521-stage-5-当前退出任务)。

### 7.3.2 Stage 5 真实边界三角化与转换一致性（cycle-0086 / S5-TESSELLATION）

公开签名及数据布局不变，补充已有接口的合同；复用既有错误码。`stage_task_id=S5-TESSELLATION`、`stage_outcome=ready_for_acceptance`。调度器 repair 后完整构建成功，最终 CTest **16/16、0 失败、200.26 s**；必需 representation_io/geometry/query_eval **15.40/0.66/1.82 s**。按两项退出要求排列的独立参考、断言及限制见 [验收 §1.13](../quality/AxiomKernel_测试与验收方案.md#113-cycle-0086--s5-tessellation-门禁与逐项证据)。最终文档门禁及调度器提交尚未记录，FR-REP-001 受限可用，Stage 5 进行中。

| 输入 / 路径 | 支持合同 | 拒绝与限制 |
|---|---|---|
| owned 平面直边面 | 读取完整外环与孔环，核对 Line/LineSegment 支撑、端点、显式参数区间、可选 PCurve 和面支撑；保留凹口与孔洞，三角绕序及法向遵循实际外环 | 不以凸 fan 或 UV bbox 填充真实边界；一般曲边不支持 |
| 四极点 Bezier；BSpline/NURBS | 仅四控制点双线性 patch；样条限 u/v degree=1、等权、工厂归一化后的 `{0,0,1,1}` 夹持节点；须认证真实四边矩形及等参数边 | 高阶、不等权、真实非夹持节点拒绝；`{2,2,5,5}` 经既有工厂仿射归一化后合法，不能误写成拒绝所有非单位输入节点 |
| LineSegment Swept、矩形 Trimmed | 线段扫掠及上述受支持基曲面的矩形裁剪；四角世界位置与曲线/参数须一致，参数 patch 的 PCurve 可全无或四边全有；Trimmed 外 UV 环为四矩形角且无孔 | 混合缺失 PCurve、错误映射、曲面孔洞/非矩形裁剪、非矩形 Trimmed、一般 Swept、Offset/Revolved 拒绝；UV bbox 本身不是边界认证 |
| 未编辑 native box/sphere/cylinder/cone/torus | 创建参数路径保留资格门禁；box 精确焊接，球/锥极点不输出退化三角形，真实三角绕序、内部偏差及法向有独立参考 | 编辑撤销原生资格；不能通过创建参数恢复已编辑代理壳；固定参考不证明工业全局误差 |
| MeshRep | 已附着网格直接返回同一 MeshId；`mesh_to_brep` 从实际有限顶点计算 bbox，并关联 MeshRep | 嵌入网格丢失返回 `NotImplemented / AXM-TES-E-0001 / rep.tessellation.support`，不生成 bbox 替代；转换不生成 owned ExactBRep，不证明闭壳或开放曲面的物理质量 |
| metadata / implicit | 保留显式显示代理 | 代理不认证物理边界、面积/体积或实体空间查询 |

`chordal_error` 为模型长度单位的绝对偏差，须有限正值且不超过 Scalar 最大值的一半；`angular_error` 为度、有限正值，认证使用 `min(90°, angular_error)`。位置匹配阈值为 `min(max(1e-12, abs(model linear tolerance)), chordal_error/4)`；它用于匹配支撑而不是最小边长，1e-3 模型容差下合法 2e-4 窄边仍可接受。双线性三角内部界 `|mixed|/(4*nu*nv)` 使用 3/4 弦高预算；仿射法向的四角锥覆盖全单元，以 atan2 核对有向法向角，避免只测顶点/中点。平面倾斜薄环同样接受法向角门禁。owned 与 box 仅焊接精确位置，不移动邻近点；法向折边拆分及 UV seam 可保留重复顶点，不认证流形连通性。

patch 每方向最多 256 段，原生圆周最多 4096 段、原生网格最多 1000000 顶点；达不到必需误差或发布前发现非有限坐标/法向/面积及退化则失败。小尺度 polygon 基础绝对门槛保留。`compute_normals` 输出受焊接/折边策略影响；OBJ 不导出 vertex normals，本轮独立核对的是三角 cross 与解析/Geo 法向。

full/local/shell 先完成所有面片与组装再发布；local 的 dirty_faces 强制重算，其余仅复用当前身份匹配缓存。缓存键覆盖 BodyId、当前拓扑、支撑/base、曲线/PCurve、节点、trim 和 options，旧 MeshId 是快照。任一 batch 成员失败恢复本次 mesh/body、源网格绑定、体/面缓存、Geo 曲线/曲面缓存、六项统计及 next_id，失败诊断保留，无部分结果。重复转换仅在源网格已关联 `MeshRep` 且 label 为 `brep_from_mesh` 时返回同一 BodyId；导入 MeshRep 首次转换仍可创建新关联。两种 verify 的临时转换状态在成功及失败时均恢复；报告不证明任意 BRep 属性保真。既有嵌入网格的严格/兼容 QA 仍由 IO 执行，不以新结果发布检查抢占 `io.export.mesh_strict_qa` 或 glTF float32 范围诊断。

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
struct ExtrusionLawStation {
  Scalar fraction {};  // 归一化高度
  Scalar scale {1.0};
  Scalar twist_angle {};  // 弧度
};
struct SweepScaleStation {
  Scalar fraction {};  // 归一化采样导轨弦长
  Scalar scale {1.0};
};
struct SweepLawStation {
  Scalar fraction {};  // 归一化采样导轨弦长
  Scalar scale {1.0};
  Scalar twist_angle {};  // 弧度
};

class SweepService {
public:
  Result<BodyId> extrude(const ProfileRef&, const Vec3& direction, Scalar distance);
  Result<BodyId> extrude_scaled(const ProfileRef&, const Vec3& direction, Scalar distance,
                                const Point3& center, Scalar end_scale);
  Result<BodyId> extrude_twisted(const ProfileRef&, const Vec3& direction, Scalar distance,
                                 const Point3& center, Scalar twist_angle);
  Result<BodyId> extrude_with_law(const ProfileRef&, const Vec3& direction, Scalar distance,
                                  const Point3& center, std::span<const ExtrusionLawStation> stations);
  Result<BodyId> extrude_to_plane(const ProfileRef&, const Vec3& direction, const Plane& end_plane);
  Result<BodyId> revolve(const ProfileRef&, const Axis3&, Scalar angle);
  Result<BodyId> revolve_between(const ProfileRef&, const Axis3&,
                                 Scalar start_angle, Scalar end_angle);
  Result<BodyId> sweep(const ProfileRef&, CurveId rail);
  Result<BodyId> sweep_scaled(const ProfileRef&, CurveId rail, Scalar end_scale);
  Result<BodyId> sweep_with_scale_law(const ProfileRef&, CurveId rail,
                                     std::span<const SweepScaleStation> stations);
  Result<BodyId> sweep_with_law(const ProfileRef&, CurveId rail,
                               std::span<const SweepLawStation> stations);
  Result<BodyId> loft(std::span<const ProfileRef> profiles);
  Result<BodyId> thicken(FaceId, Scalar distance);
};
```

显式 `ProfileRef::polygon_xyz` 拉伸支持有限坐标、共面且简单的多边形，包括凸轮廓和 L/U 形等凹轮廓（首尾隐式闭合，不重复首点）。方向按单位化后乘正距离使用，须不平行于轮廓平面并形成非退化体积。端盖采用耳切剖分，侧壁为平面三角片；无孔时 n 个轮廓顶点生成 2n 个顶点、6n−6 条边和 4n−4 个面，保留真实凹口，支持双绕向、不同起点、反向和斜向拉伸。自交或非相邻边接触、共线转角/重复顶点、非平面、数值退化或非有限参数在创建实体前返回 `InvalidInput` / `AXM-CORE-E-0002`，不退回 bbox 壳。投影边界使用随轮廓尺度增长的浮点容差，接近退化的轮廓保守拒绝。公开拓扑查询、质量属性、Strict 验证及 `brep_to_mesh` 的 `owned_topo_welded` 路径共同构成验收链路。无显式轮廓的历史占位路径仍存在。

`extrude_to_plane` 将显式平面多边形（可凹、可带孔）沿射线方向逐顶点投影到目标 Plane，物化真实三角端盖、侧壁、共享边/顶点的单一闭壳 `ExactBRep`。方向与目标法向的长度无关，目标法向反号不改变结果；每个边界点必须在严格正向且超过平面容差的射线参数处命中目标面。方向切向、反向、起止面相交/接触、近退化平面、非有限参数或大坐标舍入塌缩均在分配对象前返回 `InvalidInput / AXM-CORE-E-0002`。体积、面积、质心与惯性张量由闭合多面体积分缓存。该入口仅支持显式平面多边形轮廓，不是圆弧/样条扫掠或显式轮廓历史。

第 65 包新增 `extrude_scaled`（已纳入统一门禁）：显式凸/凹多边形及分离孔洞的等比变截面直线拉伸。截面点按 `p(t)=center+[1+t(end_scale−1)]·(p−center)+t·unit(direction)·distance`（`0≤t≤1`）变化；缩放中心须有限且在轮廓平面内（采用现有共面容差），可在材料区域外。距离须有限且为正，末端比例须有限且非负，方向须横穿轮廓平面。支持收缩、扩张、等截面、独立环绕向/起点/孔序、反向斜拉和倾斜平面；`end_scale=1` 与显式 `extrude` 几何一致。外环与孔采用同一缩放中心和比例，不能独立指定孔壁斜度；这不是一般恒角拔模或任意截面放样。

正末端比例时，末端边仍与起始边平行，因此侧壁为真实平面，三角 Face 是平面分割，不是曲面的网格近似；两端盖及内外侧壁组成 `ExactBRep` 闭壳。n 个总环顶点、h 个孔生成 `V=2n`、`F=4n+4h−4`、`E=3F/2`；体积为 `A·H·(1+s+s²)/3`（H 为法向高度），面积、质心和完整惯性来自实际闭壳积分。公开拓扑/邻接、面面积和边长查询、Strict 验证及焊接网格转换均有回归。负比例、带孔尖顶、切向、非法轮廓、缩放中心离面以及非预期的舍入塌缩/退化均在分配前以 `InvalidInput / AXM-CORE-E-0002` 拒绝；无显式轮廓也拒绝。失败允许新增诊断，但不写模型/几何/模型 ID/活动事务计数/几何与网格缓存。

第 66 包扩展 `end_scale=0` 为无孔尖顶拉伸（已纳入统一门禁）：凸/凹简单轮廓收敛到唯一顶点 `center+unit(direction)·distance`，底面耳切剖分，每条边界边生成一个真实三角侧面；不创建塌缩端盖或重合顶点。n 个轮廓点生成 `V=n+1`、`F=2n−2`、`E=3n−3`，体积 `A·H/3`，质心为底面面积质心的 3/4 加尖顶的 1/4，面积和完整惯性由闭壳积分给出。支持三角/凸/凹轮廓、双绕向/起点、平面内任意中心（可在边界或区域外）、正反向斜拉及倾斜平面。带孔轮廓收敛到同一点会形成非流形顶点，明确拒绝；不会把很小的正比例自动吸附为零。72 组矩形/L 形变体及 4 组四面体回归覆盖解析质量、共享尖顶/真实面边体与邻接、Strict/网格、失败不污染和编辑回滚重试，保留第 65 包 360 组正比例回归。

防自交依赖内部截面的正比例等比变换及严格法向推进；无孔尖顶是简单边界的锥顶，实际顶点须严格位于整个底面之外的法向半空间。正比例末端另复查末端环合法性、复用端盖三角形方向和实际截面不交叠；未扩大既有 Sweep Strict 网格 SAT 范围。沿用保守浮点剖分，近退化或极端尺度可拒绝，不承诺工业容差或大轮廓性能。

第 61 包新增 `ProfileRef::holes_xyz`（末尾可选 `std::vector<std::vector<Point3>>` 字段，已有聚合初始化仍有效）。外环与各孔可独立选择绕向、起点；孔必须与外环共面、严格位于外环内且彼此分离，不允许接触、相交、重合或嵌套。外环和孔均可为凹多边形。带孔端盖通过边界约束的非交叉平面图剖分，内外侧壁均物化为平面三角面；总计 n 个环顶点、h 个孔生成 2n 个顶点、6n+6h−6 条边、4n+4h−4 个面，欧拉特征为 2−2h。剖分不增加几何顶点，保留真实贯通孔，不是曲面或网格近似；端盖可能由多个共面 Face 组成。质量属性（含质心惯性张量）由闭壳三角面积分给出。边界验证、端盖数量/面积核对、非有限/坍塌三角片及质量积分检查均在实体分配前完成；复用 `InvalidInput / AXM-CORE-E-0002`。无外环却提供孔也拒绝。线段扫掠、轴分离旋转和拓扑兼容放样继承带孔语义；带孔尖顶仍拒绝。带孔平面图构造为保守浮点算法，近退化输入可拒绝，最坏时间为三次量级，不承诺任意大轮廓的性能或工业容差完备性。

`sweep` 的显式多边形路径支持 `make_line_segment(a, b)` 和 `make_composite_polyline(points)`：保持轮廓世界坐标与方向，导轨第 k 个点给出相对起点的位移 `points[k]-points[0]`。不自动将截面移到导轨起点，也不旋转截面。第 62 包新增折线平移扫掠：每一段沿轮廓法向须严格同向推进，允许斜向、反向及共线的中间段；切向段、回退、闭合、重复点、近退化段及坐标溢出拒绝。这里的折线是实际分段直线导轨，不将圆弧或样条离散后冒充精确曲线扫掠。

折线扫掠复用上述凸/凹及带孔轮廓约束，折点共享一组截面顶点/边，仅在整条路径两端封盖，不产生内部端盖。每段侧壁为真实平面（可拆为三角 Face），形成一个可查询的闭壳实体；n 个环顶点、h 个孔、m 个导轨点生成 `V=nm`、`F=2(n+2h−2)+2n(m−1)`、`E=3F/2`，欧拉特征为 `2−2h`。包围盒包含所有中间截面，质量属性及质心惯性来自完整闭壳积分。各段投影单调和实际舍入后截面不交叠检查、轮廓剖分、三角片退化及积分检查均在实体分配前完成，失败不创建模型实体/几何、不推进模型 ID、不改变活动拓扑事务写计数和几何缓存；错误沿用 `InvalidInput / AXM-CORE-E-0002`，空标签/无效导轨句柄沿用 `AXM-CORE-E-0001`。

第 62 包的公开拓扑/邻接、分段独立解析体积/面积/质心/惯性、`validate_all(Strict)`、`brep_to_mesh` 与回滚重试回归已纳入统一门禁。本轮未扩大既有 Sweep Strict 验证器的网格 SAT 范围，防交叠依靠上述严格单调限制；近退化轮廓与路径保守拒绝，不承诺工业容差或大轮廓性能。

`revolve` 的角度单位为弧度，必须位于 `(0, 2π]`。显式多边形路径以每周 48 段的角分辨率物化真实三角面、共享边/顶点和闭壳；部分角旋转增加约束剖分的首尾端盖，整周路径无端盖缝。支持与轴严格分离的凸/凹外环及分离孔洞；无孔轮廓还可仅通过一条唯一连续轴边闭合。轮廓/孔绕向、起点、孔序、轴方向反号/缩放和任意空间子午面保持等价；带孔区域触轴、跨轴、孤立轴点、近轴、自交、非共面或偏轴轮廓在分配前返回 `InvalidInput / AXM-CORE-E-0002`。结果体积、面积、质心和惯性来自闭合多面体积分。整周与部分角结果均是保守分片多面体 BRep，不是解析圆柱/圆锥/圆环面。

`revolve_between` 以弧度解释有向区间 `[start_angle,end_angle]`，有符号跨度必须有限、非零且绝对值不超过 `2π`。递增/递减区间分别按轴方向的正向/反向旋转，支持偏置起始角和对称区间；部分角结果的首尾端盖绕向与方向匹配。正负整周保留周期多壳语义且几何与起始角无关。`revolve(profile,axis,angle)` 的正角合同不变，等价委托 `[0,angle]`。轮廓、轴分离和数值退化限制与 `revolve` 相同。

第 74 批（cycle-0074）新增三个截面律入口，均已通过完整门禁，复用既有错误码：

| 入口 / 关键站 | 自变量与插值 | 方向与周期合同 |
|---|---|---|
| `extrude_with_law / ExtrusionLawStation` | `fraction` 为归一化高度，scale 和有向 twist_angle 分段线性插值；每个关键站原值保留 | 方向须垂直于截面平面，按单位方向右手规则扭转；中心有限且共面，距离有限且正，方向反转同时反转旋转轴 |
| `sweep_with_scale_law / SweepScaleStation` | `fraction` 为既有导轨采样弦长的归一化累计值，正比例分段线性插值 | 直线/折线以共面的导轨起点为中心，截面保持世界方向；曲线沿旋转最小标架；周期曲线要求末比例恰为 1 |
| `sweep_with_law / SweepLawStation` | 同一采样弦长上的正比例和有向扭角联合分段线性插值 | 直线/折线绕按净推进方向定向的初始截面法向旋转；曲线相对传输标架绕局部前向切向旋转；周期末比例为 1、末角为 0 或 ±2π（`1e-10 rad` 容差） |

关键站须有 2 至 4097 个，fraction 有限、从 0 到 1 严格递增；首站为 `(0,1)` 或 `(0,1,0)`。所有比例须有限且严格正，扭角须有限。支持中间扩张/收缩/比例停顿、扭转停顿及反向，凸/凹简单轮廓与分离非嵌套孔、任意空间朝向和方向反转；外环与孔共享比例/扭转，不自动匹配截面。每步比例变化不超过较小端比例的 25%；带扭角时每步不超过 7.5°，累计绝对扭角 `Σ|Δangle|` 最多一周（数值检查余量 `1e-10 rad`），反向后终角较小也不能绕过累计门禁。联合采样最多 4096 区间，不按两个律分别给预算。

两个扫掠律入口保留全部原导轨站和关键站；数值重合的导轨站/关键站共享位置和标架，不可分辨的不同关键站拒绝。支持直线、每段严格同向穿过初始截面平面的折线、既有 Bezier/BSpline/NURBS 和非嵌套 G1 连续 CompositeChain；周期圆/椭圆及既有闭合样条/复合链也支持。采样弧长不是曲线参数或解析弧长。插入曲线标架通过切向最小旋转和轴向残差插值保持正交及端点标架。周期末环与首环焊接且无端盖；联合律保留末端角检查接缝采样，不根据截面对称性推断顶点置换。周期带孔沿用外环与各孔分别物化闭壳的语义。

共享物化器在分配前检查所有实际舍入截面、复用盖片对应、推进/折叠、非相邻侧壁（曲线为无共享顶点三角形）的交叠或容差接触及质量积分。曲线接触检查按最大空间跨度排序宽相并做局部坐标 SAT，最多 2000000 宽相候选；保守曲率/导轨间距门禁采用全律最大比例。扭转侧壁使用交替对角线。成功生成真实三角 Face、共享 Edge/Vertex 和闭壳，bbox 包含实际中间截面，质量属性属于实际采样多面体，连续律独立 Gauss 积分仅作近似对照。失败不分配模型/拓扑/几何/ID，不改变活动事务、求值/网格缓存或 Eval；允许新增诊断。扫掠律采样/物化/周期扭角失败分别提供 `sweep_law_sampling/materialization/seam` 阶段及比例、采样/候选上限、累计/末端扭角等有限数值证据；前置输入失败仍复用既有诊断路径。

`extrude_twisted`、`sweep_scaled` 和 `sweep` 的旧合同保留，零扭角比例律及两关键站兼容回归通过。输出仍为采样多面体 BRep；新律不支持零/负比例、尖顶、至平面组合、嵌套复合导轨、一般非线性解析律、解析扫掠/螺旋曲面或任意截面匹配。三个入口的独立体积/质心/完整惯性、真实拓扑/边长/面面积/Strict/索引/网格、中间关键站、周期正负整周、无效/数值退化/采样预算、阶段诊断、活动事务失败原子性和编辑回滚重试回归随 `axiom_ops_heal_test` 通过（106.39 s）；整批 CTest 16/16、0 失败、134.05 s，见 [cycle-0074 门禁日志](../../.axiom-agent/logs/cycle-0074-gates.log)。FR-OPS-001 保持进行中。

`loft` 要求至少两个显式平面多边形截面。各站外环以及同索引孔环必须保持相同顶点数，以顶点顺序定义直纹侧壁对应关系；环绕向可独立变化。截面须沿共同横向严格有序，站间插值区域不得退化、翻折或相交。成功时生成真实共享面边点的单闭壳；公开质量每次从当前拓扑重算，创建时积分数据不作查询回退；不同环拓扑、自动顶点匹配、分支、尖顶/坍塌截面和无显式轮廓路径拒绝，不再返回 bbox 占位体。

`sweep` 还支持显式凹多边形或带孔截面沿 Bezier/BSpline/NURBS 开放导轨、显式端点重合且首尾切向连续的闭合样条、整圆/椭圆周期导轨，以及 `CompositeChain` 复合导轨。复合链子段可为有界直线/线段、圆/椭圆弧、Bezier、BSpline、NURBS 或 polyline 首段，按公开链语义的子曲线局部参数 `[0,1]` 取值；接缝必须位置连续且 G1 切向连续。截面必须位于导轨起点且其平面法向与起始切向对齐；开放导轨生成两端盖，闭合样条与闭合复合链生成无端盖周期闭壳。空间闭环使用沿采样弧长分布的旋转最小标架 holonomy 校正，避免首尾截面隐藏扭转缝。周期带孔截面的外边界与每个孔边界是互不连通的闭壳，因此一个 Body 含 `1 + holes_xyz.size()` 个 Shell，`owned_topo_welded` 网格也报告同数量的连通分量；开放带孔导轨由端盖连成单壳，周期无孔也为单壳。

`sweep_scaled` 在上述路径上把截面统一缩放比从起点的 1 按采样弧长线性插值到有限且严格为正的 `end_scale`。直线与 CompositePolyline 以导轨起点为截面平面内缩放中心；曲线导轨在旋转最小标架中逐站缩放。凹轮廓和带孔轮廓均生成真实共享面边点闭壳；物化前检查比例、截面平面、前向非折叠、端盖、流形边和质量属性。非单位比例仅支持开放导轨；周期导轨仅接受 1，因为缝两侧截面必须一致。`sweep(profile,rail)` 与 `sweep_scaled(profile,rail,1)` 兼容。

`sweep_scaled` 保留正终端比例与线性采样弧长插值合同；分段比例/扭转律由上述新入口提供。零比例尖顶、负比例/反射、一般非线性解析律和嵌套复合导轨仍不支持。过小比例或相对曲率过大的截面可因数值退化/自交风险被保守拒绝。结果仍是采样多面体 BRep，不是解析扫掠曲面。

曲线扫掠的端点伪闭合、首尾切向断裂、接缝错位/折角/尖点、嵌套复合链、抛物线/双曲线复合子段、过紧曲率、非局部弦段自靠近、局部不前进及退化物化都以 `InvalidInput / AXM-CORE-E-0002` 拒绝，不产生模型、拓扑、ID、事务写或求值/网格缓存污染。当前仅支持显式平面多边形截面，不保留显式轮廓历史。结果是保守采样多面体 BRep，不是解析扫掠曲面；无显式轮廓仍为历史 bbox 占位路径。

第 73 批（cycle-0073）新增 `extrude_twisted`，已通过修复后的完整门禁。仅接受有标签的显式平面简单多边形，可凹且可含严格分离、非嵌套孔洞；轮廓可任意空间朝向、独立环绕向和起点。`center` 须有限且位于轮廓平面内，`direction` 须有限、非零且垂直于该平面，按单位方向使用，`distance` 须有限且为正。`twist_angle` 以弧度表示，沿单位方向按右手规则旋转，允许正负部分角及正负整周，限于 `[-2π,2π]`；反转方向也反转旋转轴。满足上述法向合同后，零扭角兼容 `extrude`。

截面按高度均匀旋转，站间角差不超过 `π/24`（7.5°）；非零扭转路径的分段数为 `max(1,ceil(abs(twist_angle)/(π/24)))`，`abs(twist_angle)≤1e-14` 时委托 `extrude`。实际三角端盖和侧壁物化可查询的 Body/Shell/Face/Edge/Vertex 闭壳、bbox 与多面体质量属性；共面中心、法向、站点严格推进、端盖、三角退化及有限积分检查在对象分配前完成。非法/数值退化输入返回 `InvalidInput / AXM-CORE-E-0002`，不分配模型或拓扑对象。repair 仅在扭转站间交替侧壁四边形的剖分对角线，消除固定对角线累积的一阶有向体积偏差，保持站点数及非扭转物化路径。结果虽使用 `ExactBRep` 表示标签，仍是保守采样多面体，不是解析螺旋面；质量属性描述实际剖分体，不承诺解析螺旋体精度。暂不支持斜方向、多于一周或与变比例/至平面拉伸组合，极端尺度与近退化输入可保守拒绝。

### 8.1.1 Stage 3 五类建模主路径（cycle-0077 / S3-MODELING）

当前退出统一矩阵及 cycle-0079 自动化证据见 [§6.1.4](#614-stage-3-统一退出支持矩阵cycle-0079--s3-exit)；本节保留专项合同与历史门禁。

此节保留 cycle-0077 历史五类路径证据；Stage 3 历史退出任务为 cycle-0079 / S3-EXIT（§6.1.4）；当前唯一任务为 S5-TESSELLATION（§7.3.2）。公开签名不变，未新增错误码。cycle-0077 核验既有四类路径，并以真实平面直边 Face 加厚补齐第五类；下表只声明可证明子集。

| 入口 / 主路径 | 实际拓扑与质量语义 | 本批回归及支持边界 |
|---|---|---|
| `extrude` 显式平面 polygon | 三角端盖/侧壁、共享边/顶点的 owned 闭壳；直棱柱质量在浮点容差内精确 | `test_stage3_model_mass_references` 平移/倾斜矩形 V=16、A=40；凸/凹/分离非嵌套孔沿用 §8.1 支持门禁，label-only 占位不计主路径 |
| `revolve/revolve_between` 有向部分/整周 | 角站弦面采样多面体，质量对应实际物化边界 | 同上正负部分/整周 Green 边积分；仅轴分离或既有受限轴接触子域，非精确旋转曲面，固定夹具采样误差见验收 §1.3 |
| `sweep` 有限直线 rail | 显式 polygon 平移棱柱，直线主路径无曲率采样误差 | 同上 V=16、A=40；其他开放/周期曲线路径继续按既有采样合同，不扩展导轨种类 |
| `loft` 拓扑兼容 polygon 截面 | 对应顶点连接的真实三角闭壳；方锥台共面侧壁夹具在浮点容差内精确 | 同上独立截面积分 V=112/3、A=20+12√17、Cz=17/7；不声明任意截面匹配、分支或坍塌路径，非共面站间侧壁按实际剖分计算 |
| `thicken` 真实平面直边 Face | 当前定向外/内环沿单位支撑法向生成独立 owned 棱柱；平面几何及质量在浮点容差内精确 | `test_planar_face_thicken` 矩形/凹形/孔、平移倾斜、独立环绕向/正反支撑法向、反向裁剪共边；曲面/曲边/代理面明确拒绝 |

`thicken(face, distance)` 要求有限正厚度，沿支撑 Plane 的单位法向单侧加厚，方向独立于环绕向；法向反号使实体移到另一侧，厚度不是对称偏置。读取当前 Face 的外环及分离、非嵌套孔环，按 coedge 定向顺序使用实际顶点，检查 Line/LineSegment 支撑与裁剪区间、连续闭合、平面归属及区域有效性。凹口和孔保留，不使用 bbox 面积或占位回退；无法分辨的微小厚度、重复/共线/自交/接触边界及无效孔在分配前拒绝。

本批 thicken 数值参考具体覆盖矩形、L 形凹轮廓、单矩形孔三种区域，各组合平移后的轴对齐/倾斜姿态与正反支撑法向，共 12 个成功夹具；倾斜夹具反转环绕向，反法向夹具使用反向共边及延长支撑线段上的实际裁剪区间。分离非嵌套孔是支持合同，多孔组合并未在本批逐一穷举，不能将这 12 个夹具视为任意边界的完备证明。

结果拥有独立顶点/边/三角面/单一闭壳，不共享源面拓扑。总边界顶点数 n、孔数 h 时，顶点=2n、面=4n+4h−4、边=6n+6h−6。保留源 Face、已有源 Shell/Body provenance；独立 Face 没有 owner 时不虚构源壳/体。`faces_of_body/edges_of_body/vertices_of_body/shells_of_body`、面唯一 owner、每边两共边/两邻面及正长度、逐面面积总和、`ExactBRep` 与 `owned_topo_welded` 网格共同锁定实际表示。通用、体、壳三入口逐项核对 V/A/C 及九项惯性，单元密度/单位约定沿用 §6.1.3；ExactBRep 标签不将采样 revolve 变成平滑解析结果。

五条主路径均有 `validate_all(Strict)==Ok` 回归。输入与物化失败阶段为 `extrude/revolve/sweep/loft.input_gate` 或 `.materialization`，直线 sweep 委托 extrude 的诊断仍映射为 `sweep.*`。thicken 分为 `input_gate/topology_gate/support_gate/materialization`，status/code 对照见错误码字典。本批失败不改变模型/几何数、next_id、索引、缓存、Eval 或活动事务写计数，诊断可增加；回滚删除临时对象后可重试。结果被换成曲面后质量与空间查询立即拒绝且无部分值，不恢复创建质量；回滚恢复质量/截面/最近边界与 provenance。

调度器完整构建及全量 CTest **16/16、0 失败、153.89 s**；Ops/Heal **126.52 s**、Topology **0.13 s**、Query/Eval **0.95 s**。逐项证据见 [测试与验收 §1.4](../quality/AxiomKernel_测试与验收方案.md#14-cycle-0077--s3-modeling-门禁与逐项证据)。日志未记录独立 Strict warnings 门禁或文档检查/提交成功；Strict 拓扑断言通过不等于无编译告警。`stage_outcome=ready_for_acceptance`，正式验收以调度器完整门禁、文档检查及提交成功为条件；Stage 3 / FR-OPS-001 / FR-QUERY-001 保持进行中。

cycle-0078 / S3-CONSISTENCY 在上述五类支持范围内补齐集成闭环：`test_stage3_consistency_chain` 逐体核对 Strict、当前拓扑质量、独立截面/质心/最近点/距离及双侧见证、有效来源/Eval 绑定和 full/local/shell owned 网格；`stage3_representation_consistency_regression` 独立积分 OBJ 的实际三角形。完整 CTest **16/16、0 失败、164.60 s**，三条逐项证据见 [验收 §1.5](../quality/AxiomKernel_测试与验收方案.md#15-cycle-0078--s3-consistency-门禁与逐项证据)，编辑/失败/回滚合同见 [§7.3.1](#731-stage-3-表示来源与-eval-一致性合同cycle-0078--s3-consistency)。支持矩阵不扩大，历史 S3-MODELING 不再列为当前唯一任务。

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
  Result<BooleanRebuildReport> run_rebuilt(
      BooleanOp op, BodyId lhs, BodyId rhs, const BooleanRebuildOptions& options = {});
  Result<BooleanIntersectionPreparation> prepare_intersections(
      BodyId lhs, BodyId rhs, const BooleanIntersectionOptions& options = {}) const;
  Result<BooleanSplitClassificationPreparation> prepare_split_classification(
      BodyId lhs, BodyId rhs, const BooleanSplitClassificationOptions& options = {}) const;
  Result<std::vector<BooleanPointClassification>> classify_points(
      BodyId body, std::span<const Point3> points,
      const BooleanIntersectionOptions& options = {}) const;
  Result<void> export_boolean_prep_stats(BodyId lhs, BodyId rhs, std::string_view path) const;
};
```

`export_boolean_prep_stats` 导出当前壳/区域级候选统计，不表示精确布尔能力。成功在输出流关闭且检查通过后返回；失败报告保留输入体 ID，并用 `bool.prep.export.input/open/write` 区分参数、打开文件及写入失败。参数失败不改写目标文件；设备写入失败不保证文件恢复。

### 8.2.1 Stage 4 第一代求交准备支持矩阵（cycle-0080 / S4-INTERSECTION）

公开入口 `BooleanService::prepare_intersections` 返回 `Result<BooleanIntersectionPreparation>`；类型定义在 `include/axiom/core/types.h`，合同在 `include/axiom/ops/ops_services.h`。本批冻结的是候选对与几何求交准备范围，FR-BOOL-001 仍进行中。调度器 repair 后完整构建和全量 CTest **16/16 通过**，三条阶段证据见 [验收 §1.7](../quality/AxiomKernel_测试与验收方案.md#17-cycle-0080--s4-intersection-门禁与逐项证据)。

```cpp
struct BooleanIntersectionOptions {
  TolerancePolicy tolerance{};
  std::size_t max_face_pairs{2000000};
  std::size_t max_segments{100000};
  std::size_t max_edges_per_face{256};
};
struct BooleanFaceCandidate {
  FaceId lhs_face{}, rhs_face{};
  std::vector<EdgeId> lhs_edges, rhs_edges;
};
struct BooleanBoundaryHit {
  FaceId face{};
  EdgeId edge{};
  Scalar edge_fraction{};
};
struct BooleanIntersectionSegment {
  FaceId lhs_face{}, rhs_face{};
  Point3 begin{}, end{};
  std::vector<BooleanBoundaryHit> begin_hits, end_hits;
  bool point_contact{false};
};
struct BooleanIntersectionPreparation {
  std::vector<BooleanFaceCandidate> candidates;
  std::vector<BooleanIntersectionSegment> segments;
};
```

| 输入 / 场景 | 求交准备结果 | 精确、采样与拒绝边界 |
|---|---|---|
| 真实 ExactBRep box、wedge、旋转 polygon prism、凹形及带孔棱柱（含显式内环） | 枚举真实 face，候选包含两侧全部外环/内环 edge；解析平面交线按真实区域裁剪 | 要求平面支撑、Line/LineSegment 直边、连续 coedge、闭合一致定向壳；所有面/边均检查，bbox 仅筛候选 |
| 凹区域 / 孔环 | 在所有真实边界交点处分区，保留凹区间、排除孔洞 | 浮点容差内平面直边解析计算，不用凸包填凹口或孔洞 |
| 分离 / 严格包含 | 成功，可返回空 segments | 这是边界求交结果，不是体积交集或材料分类结论 |
| 横向相切 | 返回真实交段及 `point_contact` 点接触关联 | 点接触由标志区分，不保证共面接触支持 |
| 共面候选 / 面接触 / 相同体 | `NotImplemented / AXM-BOOL-E-0014 / bool.intersect` | 需要二维区域求交；即使仅在包围范围内接近也保守拒绝 |
| 曲面、曲边、代理面、不支持表示、无真实壳、ExactCritical | `NotImplemented / AXM-BOOL-E-0011 / bool.prep.candidates` | 无连续曲面求交、无采样 fallback、无精确谓词认证；ExactBRep 标签本身不授予支持资格 |
| 开壳、损坏关联、退化边或非法参数 | 结构化失败 `AXM-BOOL-E-0001` | 状态区分 InvalidInput、InvalidTopology、DegenerateGeometry，无部分 value |
| 预算不足 / 浮点不可分辨 | `AXM-BOOL-E-0012 / E-0013`，候选或求交阶段可定位 | 不将未解析交线当空集；稳定码与阶段详见 [字典 §7.5](../diagnostics/AxiomKernel_错误码与诊断码字典.md#75-bool-布尔模块错误码) |

`options.tolerance` 默认 linear/angular 为 **1e-6**（线容差按模型单位、角容差按弧度），min_local/max_local 为 **1e-9 / 1e-3**，precision_mode 为 AdaptiveCertified。线/角及局部界须有限、策略有效且为正，linear 位于局部界内，angular < 1。FastFloat 与 AdaptiveCertified 都使用带残差检查的 double 解析运算，不等于自适应精确谓词。读面检查坐标舍入尺度；求解后、裁剪前检查交线残差及其到所有面顶点距离的舍入可分辨性，因此远处未解析交线即使最终可能为空也必须失败。

预算合法范围：max_face_pairs **1..2000000**（两体面数笛卡尔积比较上限，不只是 bbox 筛选后候选数）、max_segments **1..100000**（含点接触输出）、max_edges_per_face **3..256**（每面全部外/内环边数总和）。选项越界为 E-0001，合法预算耗尽为 E-0012。成功结果拥有坐标及源 face/edge ID，不新建 CurveId、拓扑对象或交线集合；`edge_fraction` 是来源 edge 的 **v0→v1 弦比例 [0,1]**，不随 coedge 反向，允许端点关联多条真实边。结果可供后续切分，来源 ID 的有效性依赖输入模型仍存在。

成功与失败均不改 body/geometry/topology/intersection/eval/cache、Eval bridge 或调用者活动事务写计数；失败允许新增诊断记录，并始终返回可查询 diagnostic_id 与 `bool.prep.candidates` 或 `bool.intersect`。局部闭合与边界检查不证明全局壳无自交或多壳材料层级，嵌入壳是输入前提。

兼容 `run` 的交线现按真实外环/内环裁剪；所有局部面读取/裁剪完成后才统一物化辅助线和交段，在创建结果体及 Eval 失效之前传播读取失败。真实短边、257 边预算和大坐标数值失稳分别以 E-0001/E-0012/E-0013、`bool.intersect.trim` 拒绝，带 diagnostic_id，输入和活动事务不污染。该入口仍保留代理多面体及后续占位语义，不继承 prepare 的全部支持/共面拒绝合同；交线数量和 Strict 拓扑通过不证明重建实体物理正确。cycle-0081 的有限平面只读切分/分类合同见 §8.2.2；cycle-0082 的受限真实实体重建、内部共面区域与Safe见 §8.2.3；本只读入口不提供这些能力。精确谓词认证、曲面/曲边求交与全局嵌入证明仍属后续独立工作。

#### 固定参考模型集与独立结果

夹具固定在 `tests/ops/boolean_prep_test.cpp::check_planar_intersection_references` 和 `make_reference_holed_prism`，失败夹具在 `check_planar_preparation_failure_isolation`；期望来自半空间与多边形区间，不调用生产求交器或 bbox 作为参考。

| 模型 | 独立参考 / 已执行断言 |
|---|---|
| 重叠盒 [0,2]³ 与 [1,3]³，含交换输入 | 6 候选 / 6 交段；一坐标=2、另一坐标=1、第三坐标∈[1,2] 的六条单位边，完整区间覆盖、无重复长度；公共 loops_of_face/edges_of_loop 核对完整真实边集合 |
| RxRz 非轴对齐立方体 | R 转置还原上述六边参考，避免用旋转 bbox 充当交段 |
| 斜楔与 bbox 重叠但材料分离的盒；斜楔与真实切盒 | 楔体半空间 x≥0、y≥0、x+y≤2、z∈[0,2]；盒完全在 x+y>2，候选非空但零交段；真实 z=.5 斜交段总长 √2 |
| 凹 U、挤出方孔、显式内环方孔棱柱与带状切盒 | 带内 x∈[0,1]∪[3,4]；全部 **16 条**参考：z=0、y∈{1.5,2.5} 的四条 x 区间，z=.5、x∈{0,1,3,4} 的四条 y∈[1.5,2.5] 区间，以及这四个 x 与两个 y 上的八条 z∈[0,.5] 区间。逐条完整覆盖且无重复；显式内环盖面在两个截面各恰为 2 段，源 hit 用创建时顶点表与公共 vertices_of_edge 独立插值，误差≤1e-8，内环 hit 非零 |
| 分离 / 严格包含 | 边界交集为空，成功返回零交段 |
| 斜楔横向相切 | 所有接触位置 x=.75、y=1.25、z∈[.25,.75]，存在 point_contact |
| 共面面接触 / 相同体，曲面/代理与退化/数值/预算 | 结构化拒绝，诊断阶段、输入摘要和活动事务隔离；不是成功布尔模型 |

完整性与无 bbox 伪交/漏交结论限于上述固定平面参考集，不声明通用工业曲面成功率。兼容工作流 `check_geometric_preparation_workflow` 同时核对真实重叠入库 1 集合/6 段，bbox 伪交入库 0 集合/0 段及 `bool.intersect.trim`。

### 8.2.2 Stage 4 第一代切分与实体分类支持矩阵（cycle-0081 / S4-SPLIT-CLASSIFY）

本批新增 `BooleanService::prepare_split_classification` 与 `classify_points` 两个只读公开入口，类型在 `include/axiom/core/types.h`，实现为 `src/axiom/ops/boolean_split_classify.cpp`。真实交线接入实际面/边切分，并从实体裁剪边界判定内外；不分配拓扑 ID 或重建实体。`stage_outcome=ready_for_acceptance`，调度器最终完整构建成功、CTest **16/16、0 失败、173.96 s**；按三项要求排列的证据见 [验收 §1.8](../quality/AxiomKernel_测试与验收方案.md#18-cycle-0081--s4-split-classify-门禁与逐项证据)。本节为历史只读准备证据，当前任务 S5-TESSELLATION 见 §7.3.2；Stage 4 / FR-BOOL-001 仍进行中，正式状态见当前进度 §5.2.1。

```cpp
enum class BooleanPointLocation { Outside, Inside, Boundary };
struct BooleanPointClassification {
  BooleanPointLocation location{BooleanPointLocation::Outside};
  std::vector<FaceId> boundary_faces;
};
struct BooleanSplitClassificationOptions {
  BooleanIntersectionOptions intersection{};
  std::size_t max_fragments{100000};
};
struct BooleanFaceFragment {
  BodyId source_body{};
  FaceId source_face{};
  std::array<Point3, 3> vertices{};
  std::array<EdgeId, 3> source_edges{};
  std::array<Scalar, 3> source_edge_begin{}, source_edge_end{};
  std::vector<std::size_t> intersection_segments, adjacent_fragments;
  BooleanPointClassification classification{};
};
struct BooleanEdgeFragment {
  BodyId source_body{};
  EdgeId source_edge{};
  Point3 begin{}, end{};
  Scalar begin_fraction{}, end_fraction{1.0};
  std::vector<FaceId> adjacent_faces;
};
struct BooleanSplitClassificationPreparation {
  BooleanIntersectionPreparation intersection;
  std::vector<BooleanFaceFragment> fragments;
  std::vector<BooleanEdgeFragment> edge_fragments;
};
```

| 输入 / 场景 | 切分 / 分类行为 | 支持与拒绝边界 |
|---|---|---|
| 真实嵌入平面直边 ExactBRep 闭壳；非轴向、凹形及显式孔环 | 从实际外/内环三角化，按真实有限交段支撑线及端点横截线切分，同步相邻面源边切点 | 沿用 §8.2.1 的输入/容差门禁；单环绕向须符合支撑法线。允许额外三角划分，不生成 bbox 替代面 |
| 来源与方向 / 邻接 | 返回 source BodyId/FaceId/EdgeId、原边参数、交段索引及同源体整边邻接 | 三角绕向沿 source outer loop；每条三角边有唯一反向整边邻接，可跨相邻源面；两来源面上的有限交段连续覆盖，point_contact 保留真实顶点 |
| 内外 / 严格包含 / 可解析小间隙 | 分片中心相对另一体分类；classify_points 按输入顺序返回实体点分类 | 真实 trimmed boundary 射线奇偶材料语义，至少两条有效射线一致；包含可无交段，不能把边界空交当材料为空 |
| 真实面 / 角点边界 | 返回 Boundary 与实际 boundary_faces | 仅舍入尺度可解析的实际裁剪边界；整个线容差带不自动视为 Boundary |
| 非共面相切 | 保留有限交段/point_contact 语义，开放面片可为 Outside | 固定 wedge/box 相切回归成功；不由此推断所有退化接触或共面区域支持 |
| 共面候选、面接触 / 相同体 | NotImplemented / E-0014 / bool.intersect | 先行求交保守拒绝，未提供二维共面区域解算；独立 classify_points 不执行双体共面候选判定 |
| 曲面/曲边、代理、无真实壳、ExactCritical | NotImplemented / E-0011 | split 先行输入门禁为 bool.prep.candidates；独立点分类为 bool.classify；无采样 fallback 或精确谓词认证 |
| 未解析近边界、射线分歧或不足两条有效射线 | NumericalInstability / E-0013 / bool.classify | 明确拒绝，不猜测内外；有限数值证据与 diagnostic_id 可检索 |
| 切分不一致 | E-0004 / bool.split | InvalidTopology 或 NumericalInstability，无部分结果 |
| 工作或输出预算耗尽 | E-0012 / bool.split 或 bool.classify | OperationFailed，无部分结果；具体 status/传播阶段见 [错误码字典 §7.5](../diagnostics/AxiomKernel_错误码与诊断码字典.md#75-bool-布尔模块错误码) |

`source_edges[i]=0` 表示三角内部新增划分边；非零时 `source_edge_begin/end[i]` 均沿原 edge 的 v0→v1，可随三角绕向递减。`intersection_segments` 指向结果 `intersection.segments`，`adjacent_fragments` 指向同一结果 `fragments` 中同来源体的分片。独立 `edge_fragments` 的始末分数递增，覆盖原边 [0,1]，保留同步后的额外 knot；`adjacent_faces` 是原 incident source faces。坐标由结果自有，来源 ID 有效性依赖输入仍存在。

预算复用 `BooleanIntersectionOptions`：`prepare_intersections` 的 max_face_pairs 仍限制两体面数笛卡尔积；求交返回后 split 另起累计工作计数，以同一上限约束边界读取、细分、约束覆盖、邻接及分类，不是求交加切分的全程单一计数。独立 classify_points 累计边界读取与点/面射线工作，max_segments 另限制输入点数。新增 max_fragments 合法范围 **1..100000**，默认 **100000**，限制返回面片与边片合计；非法选项 E-0001，合法预算耗尽 E-0012。默认 double/容差与 §8.2.1 一致。

#### 切分与分类固定参考集

| 回归参考 | 独立结果 / 断言 |
|---|---|
| [0,2]³ / [1,3]³，及两次刚性旋转逆映射 | 两体总面积各 **24**，落在对方内部的面积各 **3**；逐 source face 面积守恒、绕向、整边反向对称邻接、每条交段在两源面上覆盖参数 [0,1] |
| 凹 U / 显式孔棱柱及 cutter | 总面积 **64 / 72**，对方内部面积各 **4**，cutter 总面积 **26**；独立材料公式核对三角中心及朝各顶点内部样本；孔源边按原端点 v0+fraction×(v1−v0) 独立插值 |
| 严格包含 / 1e-4 正间隙 / 实际面和角点 | 包含无交段，外体面积 **24**、内部小体面积 **1.5**；间隙可解析，边界有真实 FaceId |
| 非共面 wedge/box 相切 / bbox 内而 wedge 材料外 | 所有开放分片 Outside；point_contact 成为真实顶点、有限竖交段在两源面完整覆盖；材料外点 Outside；5e-7 近边界 E-0013/bool.classify 拒绝 |
| 无 writer / 活动 writer 的成功、失败及 rollback | 拓扑/来源/表示/公开几何摘要、存储、Eval bridge 五指标、缓存六指标与 writer 写次数保持；哨兵后继续写入及 rollback、原体 Strict 通过 |

证据限固定嵌入平面参考集及 double 舍入尺度，不是通用工业布尔成功率。多壳采用奇偶材料约定，壳自身及壳间无相交为前置，未提供全局嵌入证明。隔离摘要包括环顺序、原边 v0/v1、来源、表示、face/body bbox、真实面积/边长、incident PCurveId（合法 0）及完整绑定时 UV；不是逐顶点直接坐标或全部几何状态编码，不认证部分 PCurve 绑定面。新入口无模型/表示/来源/Eval/cache/事务写入，失败仅允许新增诊断。兼容 `run()` 仍含 bbox 实体语义，其后续物化、重建与修复事务未因本包获得认证；受限真实实体重建与内部共面区域支持另见 §8.2.3，公开只读prepare的共面拒绝不变；精确谓词、曲面/曲边和全局嵌入仍未认证。

### 8.2.3 Stage 4 真实实体重建支持矩阵（cycle-0082 / S4-REBUILD）

`BooleanService::run_rebuilt` 是真实重建入口，仅接受 Union/Subtract/Intersect；Split 返回 InvalidInput。公开只读 `prepare_intersections/prepare_split_classification` 继续按 §8.2.1/§8.2.2 拒绝共面（E-0014），重建内部另启共面区域切分与两侧材料分类。兼容 `run` 保留历史 bbox/proxy 语义，不继承本节认证。实现 `src/axiom/ops/boolean_rebuild.cpp` 已随调度器最终全量 **16/16、0失败、184.89 s** 通过，逐项证据见 [验收 §1.9](../quality/AxiomKernel_测试与验收方案.md#19-cycle-0082--s4-rebuild-门禁与逐项证据)。`stage_outcome=ready_for_acceptance` 为该历史批次报告；当前唯一退出任务 cycle-0086 / S5-TESSELLATION 见 §7.3.2，Stage 4 / FR-BOOL-001 仍进行中；正式状态见当前进度 §5.2.1。

```cpp
struct BooleanRebuildOptions {
  BooleanSplitClassificationOptions preparation{};
  bool auto_repair{false};
};
struct BooleanRebuildReport {
  std::optional<BodyId> output;
  std::size_t selected_fragments{};
  std::size_t output_faces{};
  bool repaired{false};
};
```

诊断位于外层 `Result<BooleanRebuildReport>::diagnostic_id`，报告没有独立诊断字段。空交集、完全减除及纯低维接触交集返回 Ok、`output=nullopt`，计数为零、repaired=false，不分配空壳、bbox 或 mesh 替代体，也不对空结果宣称执行非空 Strict 验证。

| 场景 / 能力 | 真实行为与认证范围 |
|---|---|
| 输入与选片 | 嵌入平面直边 ExactBRep 闭壳、double/奇偶材料；每个源壳须连通且朝向材料外部，薄壳/相邻壳方向探针未解析时拒绝。按真实切分与分类选片，差集反转右体内部片 |
| 非空重建 | 跨来源同步真实有限端点、舍入级焊接、共享整边两侧反向、逐顶点 incident-face 唯一面扇，再物化独立 owned 面/边/连通壳；必须通过 `validate_all(Strict)` 与 `body_shell_regions` 后发布 |
| 共面、同形与面接触 | 实际外/孔环支撑线裁剪共面区域，Boundary 用受最近真实边界限制的两侧材料探针判定真值；共享区域只由左体物化一次且保留双方来源。同 BodyId 只生成一侧并去重来源；完整面相切 Union 合并材料、Subtract 保留左体、Intersect 空 |
| 分离、包含、孔洞与凹形 | 支持固定双壳并集、包含空腔、真孔及凹U参考与连续布尔；材料/空腔层级从真实壳关系查询 |
| 边/点接触与夹点 | 边/点相切 Union、四侧边、跨壳点触与同连通壳夹点保守拒绝 E-0006/bool.rebuild；固定边/点接触 Subtract 保留左材料，Intersect 空 |
| 公共查询与来源 | `faces_of_body/edges_of_body/shells_of_body`、outer/inner loops、PCurve、面/壳/体来源可查询；共面共享区域保留双方 source_faces/source_shells，体来源去重。边没有直接来源字段，经 `faces_of_edge` → `source_faces_of_face` 间接追溯 |
| 质量与空间查询 | 受认证重建 BooleanResult 独立进入当前拓扑质量、材料定位、截面等真实查询门禁，不按来源/bbox 恢复质量；人工节点截面可能明确数值拒绝、无部分 value，固定常规参考平面仍须成功 |
| 受限 Safe | auto_repair=true 且首次 Strict 有明确可认证分片几何缺陷时，固定种子合并同向舍入级共面片、抵消已认证反向内边并聚合来源；所有 incident 环邻点对一致、严格位于端点之间且舍入级共线时同步删除人工节点。保持真实外/孔环、角点和面积向量，不移动端点或删除容差大小真实特征 |
| Safe 发布与拒绝 | 撤销首次失败物化及缓存/Eval后，仅物化实际使用节点，再经共享边/面扇、Strict与壳关系认证，成功才 repaired=true。直接成功仍 false。真短折角、真实薄交集、自交、普通trim/domain或未解析材料关系不获修复；trim仅有非零且低于阈值UV环面积证据可纳入几何缺陷资格。不调用通用bbox代理修复，不放宽Strict |
| 预算与数值 | 延续 preparation 合法范围；累计共面扫描/裁剪/去重/探针、节点和面扇/Safe工作均有预算，跨来源节点同步至多 **20000000** 次比较，环节点至多 **max_fragments×12**。近共面间隙/短非零交段 E-0013/bool.intersect；无法解析的材料探针按 bool.rebuild 定位 |

固定参考以 **V / A / S** 表示体积 / 全边界面积 / 指定截面面积；数值是独立断言期望，日志未逐项打印测量值。U/D/I 分别为并/差/交。

| 固定模型 | U | D | I |
|---|---|---|---|
| 偏移盒及刚性旋转 | 15 / 42 / 7 | 7 / 24 / 3 | 1 / 6 / 1 |
| 同形异ID或同ID | 8 / 24 / 4 | 空 | 8 / 24 / 4 |
| 共面部分重叠 | 12 / 32 / 6 | 4 / 16 / 2 | 4 / 16 / 2 |
| 完整面相切 | 16 / 40 / 8 | 8 / 24 / 4 | 空 |
| 孔洞跨高度 | 29 / 90 / 16 | 23 / 72 / 10 | 1 / 8 / 2 |
| 凹U跨高度 | 25 / 82 / 14 | 19 / 64 / 8 | 1 / 8 / 2 |
| 孔洞同高度 | 32 / 92 / 16 | 20 / 68 / 10 | 4 / 20 / 2 |
| 凹U同高度 | 28 / 84 / 14 | 16 / 60 / 8 | 4 / 20 / 2 |
| 两个2盒、x重叠d=0.0002 | 15.9992 / 39.9984 / 7.9996（Safe成功） | 7.9992 / 23.9984 / 3.9996（直接Strict成功，repaired=false） | 真实薄交集拒绝 |

分离 Union V=11/A=37、Subtract V=8/A=24、Intersect 空；包含差集空腔 V=7.875/A=25.5/S=3.75。孔/凹参考独立按 V=hS、A=2S+hP及材料定位校核；Safe 真孔 V/A/S=39.9984/95.9976/19.9992，两个cap保持inner loops。Safe夹具准备容差1e-6、服务Strict线容差 **1e-3**；Union auto=false明确bool.validate失败，auto=true成功且面数减少。连续通孔链参考11.5/35.5/5.75、7.5/27.5/3.75，Safe后空腔差集15.8742/41.4984/7.7496。旋转局部z=1.375及通孔z=0.875截面强制成功；原人工节点z=1.5/z=1只允许正确面积或 NumericalInstability/kQuerySectionFailure/query.section.numeric、无value，不承诺任意截面成功。

失败保留原 cause issues 和稳定 bool.rebuild/validate/repair 根因、输入ID、有限数值证据及JSON，先行候选/求交/切分/分类阶段原样传播。服务撤销新增拓扑、几何/网格、曲线/曲面求值与三角化缓存及统计，恢复Eval状态；诊断与递增ID保留。非空成功仅在最终认证后登记活动writer的服务分配区间，不增加显式write_operation_count，不失效输入Eval。保存点回滚保留该点前输出并清除后续分配；完整rollback防止成功输出delete后由旧快照复活，清理支撑几何和cache，允许重试。合法累计遥测不回退；固定回归的bridge.for_body_entries逐体+2、两体+4，其余字段保持。

限制：固定嵌入闭壳与double不等于精确谓词或全局嵌入证明；逐顶点流形认证只锁局部拓扑。保留现有Strict每壳至少六面与近似网格自交门禁，曲面/曲边、ExactCritical、未解析薄层/容差带不认证，Safe只修人工分片拓扑。输入隔离包含公开拓扑/支撑面/UV世界坐标/曲线端点/边长摘要，不是完整几何序列化。prepare分片分类预建真实面BVH，数值安全上界无法证明时退回全脸扫描；公开classify_points与重建两侧探针预算契约不变，未提高预算或性能阈值。

### 8.2.4 Stage 4 退出支持矩阵（cycle-0083 / S4-EXIT）

此节保留 cycle-0083 / Stage 4 历史退出证据；当前 Stage 5 / S5-TESSELLATION 见 §7.3.2。本批公开 API、生产实现和既有错误码冻结；沿用 §8.2.1～§8.2.3 合同，补固定模型稳定性、独立表示参考与全链路诊断验收。`stage_task_id=S4-EXIT`、`stage_outcome=ready_for_acceptance`；调度器完整配置/并发4构建成功，CTest **16/16、0失败、194.58 s**。五项必需 workflow/prep/query_eval/representation_io/runtime 各 **4.25/5.63/2.01/10.17/0.04 s**，性能基线 **2.03 s**。按退出要求排列的测试/断言/参考/限制见 [验收 §1.10](../quality/AxiomKernel_测试与验收方案.md#110-cycle-0083--s4-exit-门禁与逐项证据)。正式文档门禁与调度器提交尚未记录，Stage 4 / FR-BOOL-001 保持进行中。

| 体类 / 能力 | 固定支持与独立证据 | 限制 / 拒绝 |
|---|---|---|
| 平面直边 ExactBRep 闭壳 | 偏移盒 U/D/I 各总计两轮，V/A/S=15/42/7、7/24/3、1/6/1；owned面/共享边/连通壳、来源、Strict、当前质量/截面均检查 | 仅嵌入双边流形、double/奇偶材料与可解析外向源壳；不证明全局嵌入或精确谓词 |
| 固定分离、包含、同形/同ID、旋转、共面/面接触 | §8.2.3 固定参考保留；分离并 11/37/4、包含空腔 7.875/25.5/3.75 排除 bbox 代理；内部有限共面重建保留双来源 | 公开只读 prep 共面仍 E-0014；边/点 Union 拒绝，边/点差保留/交为空仅限固定参考 |
| 固定孔/凹U多面体及连续布尔 | prep::check_rebuild_holed_references 独立 V=hS、A=2S+hP 与材料定位；heal/ops_heal/query_eval 的真孔、空腔、Safe与连续布尔回归 | 不把这些 helper 的结果算作本批新增 OBJ 积分；人工节点截面可能 query.section.numeric 明确拒绝、无部分 value |
| 非空 BooleanResult→owned 网格→OBJ | workflow::check_real_rebuild_references 全部非空固定结果核标签 mesh_from_brep_owned_faces、策略 owned_topo_welded、无越界/退化三角及计数；独立读取 OBJ 三角形，以 long double 累计有向 V/A，误差 ≤1e-7；再次转换命中同 MeshId | 网格为当前平面边界三角化；ExactBRep 标签不表示精确算术，不认证通用曲面/曲边采样；不以 bbox 网格替代真实材料 |
| 空交/完全减除及低维接触交 | Ok/output=nullopt，不分配空壳或替代体，既有回归同次通过 | 空结果不宣称执行非空 Strict、查询或表示转换 |
| Safe及writer/保存点/rollback | 仅人工共面分片/一致共线节点修复，保留真边界与来源；最终 Strict/壳关系成功才发布，失败撤销新增物化/cache/Eval；既有提交、局部/完整回滚及重试闭环同次通过 | 不修真实薄层/短特征/未解析容差带；保留 Strict 至少六面及近似网格自交；合法累计遥测不回退 |
| 候选/求交准备暖缓存隔离 | 固定 CurveId 域、四点无缓存/缓存曲线对照、支撑面采样/边长/归属摘要，10 store/6 tessellation/5 bridge及Eval依赖/有效性/重算不变；writer成功/失败及rollback后无writer重试端点1e-12一致 | 摘要非完整顶点/几何序列化；未绑定open_body删除合法for_body_entries+1，与rebuilt服务体回滚+2/两体+4分别核对 |
| 曲面/曲边、ExactCritical、精确谓词、全局嵌入与工业性能 | 保留结构化拒绝及诊断；没有新增支持域 | run_rebuilt 不支持通用曲面/曲边或未解析薄层；perf默认150次/4000ms、30s超时只覆盖兼容run/查询，未认证run_rebuilt工业性能 |

七阶段 `bool.prep.candidates / bool.intersect / bool.split / bool.classify / bool.rebuild / bool.validate / bool.repair` 的完整名称、稳定码、检索与 JSON 证据见 [字典 §7.5](../diagnostics/AxiomKernel_错误码与诊断码字典.md#75-bool-布尔模块错误码)。切分/分类及重建/验证/修复固定失败按阶段和码索引检索；prep固定失败按阶段检索且在报告/JSON核码。兼容 `run` 仍保留 bbox/proxy 后续实体语义，原 trim 读取失败传播不代表其全流程获本矩阵认证。

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
  Result<BodyPlaneSection> section_detailed(
      BodyId, const Plane&, const BodySpatialQueryOptions& options = {}) const;
  Result<MeshId> section(BodyId, const Plane&) const;
  Result<BodyPointQuery> closest_point(
      BodyId, const Point3&, const BodySpatialQueryOptions& options = {}) const;
  Result<BodyDistanceQuery> closest_points(
      BodyId, BodyId, const BodySpatialQueryOptions& options = {}) const;
  Result<MassProperties> mass_properties(BodyId) const;
  Result<Scalar> min_distance(BodyId, BodyId) const;
};
```

cycle-0075 起，上述实体截面与距离入口遵循 §6.1.2 的真实拓扑支持矩阵及空结果合同。cycle-0076 / S3-MASS 的质量合同见 §6.1.3：真实多面体只消费当前拓扑，未编辑原生球/柱/锥/环保留解析资格；编辑撤销资格，回滚恢复。兼容run的旧占位布尔/Modified、metadata导入和兼容代理壳明确拒绝；cycle-0082受认证run_rebuilt BooleanResult进入真实查询门禁（§8.2.3），无 bbox、来源记录或创建缓存回退。公开签名未变，本批补齐注释与失败语义，复用既有错误码。

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
  // 失败报告使用 heal.* 阶段，关联目标及问题子实体，并携带有限数值证据。
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
  Result<void> repair_face_trim_pcurves(FaceId, RepairMode);
  Result<OpReport> sew_faces(std::span<const FaceId>, Scalar tolerance, RepairMode);
  Result<OpReport> remove_small_edges(BodyId, Scalar threshold, RepairMode);
  Result<OpReport> remove_small_faces(BodyId, Scalar threshold, RepairMode);
  Result<OpReport> merge_near_coplanar_faces(BodyId, Scalar angle_tol, RepairMode);
  Result<OpReport> auto_repair(BodyId, RepairMode);
  Result<std::vector<OpReport>> repair_many_auto(std::span<const BodyId>, RepairMode);
  Result<std::vector<OpReport>> repair_many_remove_small_edges(std::span<const BodyId>, Scalar threshold, RepairMode);
  Result<std::vector<OpReport>> repair_many_remove_small_faces(std::span<const BodyId>, Scalar threshold, RepairMode);
  Result<std::vector<OpReport>> repair_many_merge_near_coplanar_faces(std::span<const BodyId>, Scalar angle_tol, RepairMode);
};
```

第 73 批为 HEAL 验证、修复、后验验证、trim 重建和批量修复的失败报告统一补齐 `heal.*` 阶段、关联实体和有限 `numeric_evidence`，并通过 `axiom_heal_test` 的模块级 `audit_evidence` 门禁。无效/空目标用显式零令牌标识无可关联实体；证据至少含状态与实体数量，并按分支附验证模式、阈值、计数或几何量，非有限测量值被过滤并记录遗漏数量。该诊断合同不扩大验证或修复算法的几何支持范围。

单项修改型修复在物化后验证，后验失败回收本次派生体及拓扑/几何对象并保留失败诊断；`auto_repair` 失败也回收派生结果。`repair_face_trim_pcurves` 支持 Plane/Cylinder/Sphere，投影重建或后验验证失败恢复全部原 coedge PCurve 绑定并删除本次新建 PCurve。四个 `repair_many_*` 入口任一子项失败时回滚此前子项全部派生对象及 Eval 失效状态，不返回半成功结果；批量失败报告关联失败子项目标，并附 `heal.repair_many_*.rollback` 与 `completed_item_count/requested_item_count/rollback_applied`，其中 `repair_many_auto` 另附 `allocated_object_count`（以分配 ID 增量计）；`repair_many_auto` 将失败子项 issue 复制到批量报告并保留非空子阶段，同时附批量 rollback 证据；其他三个批量入口仍将子项根因保留在原诊断，不合并全部 issue。HEAL 回滚不恢复 `next_id`，被回收对象占用的 ID 可留下空档，不承诺重试复用原 ID。诊断记录作为失败证据保留。

### 9.2.1 Stage 5 第一代导入修复闭环（cycle-0084 / S5-HEAL）

本节为 S5-HEAL 历史支持合同；当前唯一退出任务 cycle-0086 / S5-TESSELLATION 的支持与证据见 §7.3.2。

公开签名与错误码常量保持不变；本批修改 `auto_repair`、导入闭环语义及 MeshRep 验证。`stage_task_id=S5-HEAL`、`stage_outcome=ready_for_acceptance`；调度器最终完整 CTest **16/16、0 失败、214.19 s**。逐项断言、独立参考及限制见 [验收 §1.11](../quality/AxiomKernel_测试与验收方案.md#111-cycle-0084--s5-heal-门禁与逐项证据)，正式状态见 [当前进度 §5.2.1](../plan/AxiomKernel_当前开发进度.md#521-stage-5-当前退出任务)。

| 条件 / 策略 | 当前行为与支持边界 |
|---|---|
| `auto_repair` 的 ReportOnly/SuggestOnly | 仅 Standard 预检，返回原体；外层 Result 可 Ok，须读取 `OpReport::status` 了解预检结果。模型、Eval、缓存/统计不变，允许新增诊断。此约定不泛化到其他旧修复入口 |
| Standard 预检失败的 owned ExactBRep，Safe/Aggressive | 新真实规则仅限单壳、至少六个唯一面、真实平面和直线外环，无内环或代理面；仅修配置容差内端点间隙、方向、重复 FaceId 引用与连续零长节点。不扩到曲面/曲边、孔洞或多壳 |
| 容差 | 使用配置 `linear`，要求有限正数且在 `min_local/max_local` 内；焊接距离 ≤ linear，拒绝多候选和链式漂移，不自动放宽本规则阈值。`angular` 仍影响 Strict 后验资格；固定边界 linear=1/1024，gap=0、linear/2、linear 成功，1.01linear 拒绝 |
| 真实派生结果 | 重建共享顶点/边及相反共边方向，按底层边 canonical 端点绑定有限 PCurve；以既有 Generic + ExactBRep 注册，公开 UV 环、质量/截面可消费真实边界。Strict 成功才保留派生输出；源体缺陷不变 |
| 失败与重试 | 单项及批量自动修复恢复模型、反向索引、几何求值/三角化缓存与统计、Eval；后项失败撤销此前派生输出。保留子阶段/数值证据和诊断，Heal 不承诺回收已分配 ID；修正条件后同一源体可重试 |
| 无 owned 拓扑的非法 bbox | Safe 稳定拒绝；显式 Aggressive 保留历史单位盒恢复，必须读 `heal.auto_repair.metadata_bbox` / `synthetic_bbox=1`，不计为真实几何修复 |
| MeshRep 验证 | 按实际所属网格的有限顶点、合法索引和非退化三角形验证，合法二维平面网格允许；本项不证明闭合或自交 |

固定 2×3×4 参考修复后为 6 面/12 边/8 顶点、V=24/A=52，IO 截面面积 6；闭合按每边两张不同邻接面和两次相反定向使用证明。现有 `boundary_edge_count_of_body` 按单壳归属仍为 **12**，不能以该 API=0 作为水密验收。STEP 固定文件仅传递 Axiom 元数据，重复/退化缺陷由测试显式物化/注入；不代表标准 STEP 文件缺陷实体交换或工业通用修复。

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

cycle-0078 明确拓扑成功编辑、删除及回滚恢复对体绑定和下游节点传播失效；事务内重算后恢复也再次 dirty，移除体清理绑定而不删除消费者节点。只读查询/表示转换不改变 dirty/recompute_count；recompute 只管理图，不自动执行质量/表示算法。完整回归见 §7.3.1 与验收 §1.5。

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
  Result<BodyId> import_axmjson(std::string_view path, const ImportOptions&);
  Result<BodyId> import_iges(std::string_view path, const ImportOptions&);
  Result<BodyId> import_brep(std::string_view path, const ImportOptions&);
  Result<void> export_step(BodyId, std::string_view path, const ExportOptions&);
  Result<void> export_axmjson(BodyId, std::string_view path, const ExportOptions&);
  Result<void> export_iges(BodyId, std::string_view path, const ExportOptions&);
  Result<void> export_brep(BodyId, std::string_view path, const ExportOptions&);
  Result<BodyId> import_auto(std::string_view path, const ImportOptions&);
  Result<void> export_auto(BodyId, std::string_view path, const ExportOptions&);
  Result<std::vector<BodyId>> import_many_step(std::span<const std::string> paths, const ImportOptions&);
  Result<std::vector<BodyId>> import_many_axmjson(std::span<const std::string> paths, const ImportOptions&);
  Result<std::vector<BodyId>> import_many_auto(std::span<const std::string> paths, const ImportOptions&);
  Result<void> export_many_auto(std::span<const BodyId>, std::span<const std::string> paths, const ExportOptions&);
};
```

`import_axmjson`、`import_iges` 与 `import_brep`（NFR-DIA-001 第 70 批）在分配 `BodyId` 前完成普通文件检查、64 MiB 上限与短读检查、严格结构解析、格式/`BodyKind` 校验，以及有限数值、包围盒顺序和非零轴校验。三者的物化前失败分别绑定 `io.import.<format>.input/path/open/read/parse/validation`；输入/路径/打开/读取失败复用 `AXM-IO-E-0004`，结构/格式失败复用 `AXM-IO-E-0003`，非有限几何复用 `AXM-VAL-E-0010`，包围盒无效或轴退化复用 `AXM-VAL-E-0004`。失败返回可检索 `diagnostic_id`，不写 Body/Mesh store、不推进模型 ID，修复原文件后可用同一路径重试。AXMJSON 兼容既有仅含身份与 bbox 的早期文件；一旦出现扩展几何字段就要求整组完整。BREP 仅接受带 `AXIOM_BREP_INTERCHANGE` 文件头的 `AXIOM_BREP` JSON 子集；IGES 仅物化 Axiom 元数据子集，典型标准 IGES 卡片/DE 实体仍返回既有 `NotImplemented / AXM-IO-E-0011`，并可附 `AXM-IO-D-0017` 扫描摘要。

第 73 批统一 STEP/AXMJSON/IGES/BREP/OBJ/STL/glTF/3MF 主格式导入、导出及 auto 路由失败的 `io.*` 阶段、问题实体令牌与有限数值证据，已通过 `axiom_io_workflow_test` 模块级 `audit_evidence` 门禁。预物化文件失败无内核对象时使用 `related_entities=[0]`；导出通常关联输入 Body。AXMJSON/IGES/BREP 导出补齐 `io.export.<format>.input/path/open/write` 及最终写入/关闭检查，复用既有 IO/VAL 错误码。

八个具体入口 STEP/AXMJSON/IGES/BREP/OBJ/STL/glTF/3MF 使用共享导入后闭环，auto 路由与批量入口传播其结果。`run_validation=true` 先做 Standard；成功则返回原导入体。失败且 `auto_repair=false` 或策略为 ReportOnly/SuggestOnly 时，在 `io.post_import.validation` 返回失败，不把观察策略升级为 Safe。Safe/Aggressive 修复失败在 `io.post_import.repair` 返回失败；修复成功后再做 Standard，失败在 `io.post_import.post_validate` 返回失败，成功返回修复输出。新真实平面修复另在 Heal 内部通过 Strict 才能成功。所有未解决的失败均无 Body value，回收本次导入和派生对象，恢复模型、链接、缓存/统计、Eval 与 `next_id`；诊断保留。`run_validation=false` 跳过该闭环，不承诺验证通过。

IO 外层复制根因码、实体和有限数值证据，Error/Fatal 阶段映射为对应 `io.post_import.*`；原 Heal 诊断及细分子阶段保留，不修改源报告。默认 STEP/IGES/BREP 仍为 Axiom 元数据子集，标准全实体交换未获本批认证。

`import_many_step/import_many_axmjson/import_many_auto` 任一子项实际返回失败时回收本批（含自动修复）新分配的 Body/Mesh/拓扑/几何，恢复链接、缓存、Eval 失效状态及 `next_id`，可原位重试；保留诊断记录。合并报告保留根因并附 `io.batch_import/io.batch_export`、从零开始的 `failed_item_index`、`completed_item_count`、`path_length`。完成数量表示失败前已完成项，并不表示回滚后仍保留这些导入体。候选导入、严格现有文件导入、目录导出与条件导出传播真实失败；`export_auto_existing_only` 仅跳过父目录不存在的目标。批量导出不承诺文件系统事务，已成功前项及侧车可能保留；cycle-0085 起失败项主文件受单文件发布保护；普通文本/目录等辅助接口未纳入本重量级证据包。标准 STEP/IGES 实体及既有格式子集限制不变。

### 11.1.1 Stage 5 受限 IO 主链路（cycle-0085 / S5-IO）

本节保留 cycle-0085 历史 IO 合同；当前唯一任务见 §7.3.2。公开签名、ImportOptions/ExportOptions 不变，仅补充公开合同注释；既有码复用。`stage_task_id=S5-IO`、`stage_outcome=ready_for_acceptance`，调度器完整构建成功、CTest **16/16、0 失败、227.56 s**，三项必需 workflow/dataset/representation_io **14.77/0.70/12.23 s**；[三条逐项证据](../quality/AxiomKernel_测试与验收方案.md#112-cycle-0085--s5-io-门禁与逐项证据)覆盖本包退出要求，正式条件见当前进度 §5.2.1。

| 格式/路径 | 当前实际支持与固定参考 | 保留限制 |
|---|---|---|
| STEP / IGES / BREP | Axiom 参数、origin/axis/bbox 元数据；固定高精度 double 文本及导出再导入精确比较；ExactBRep 标签但零 owned shells | STEP 不是 EXPRESS 标准实体；IGES 不是 DE/PD 交换；BREP 是带 AXIOM_BREP_INTERCHANGE 头的 AXIOM_BREP JSON，非通用外部 BRep；容器/schema/entity 标记不赋予实体资格 |
| STL | ASCII / binary 三角网格；固定四面体导入→验证→导出→再导入的实际 4 三角形/12 顶点，独立 V=4、A=13+sqrt(244)/2、质心参考误差≤1e-12 | MeshRep，非 owned BRep；ASCII 精确 double 往返参考不承诺 binary float32 无损；不证明一般网格闭合/自交 |
| 标准 STEP / IGES | 典型实体及混有 Axiom 标记的标准物理文件仍优先 NotImplemented，稳定错误码与扫描摘要 | 启发式扫描，未实现标准单位转换/AP 全实体；当前 CMake 未定义或启用桥接，里程碑 1～4 为可选后续路线 |

STEP/IGES/BREP/STL 在分配模型对象前施加 **64 MiB（67108864 字节）**读取预算，超限归入 `io.import.<format>.read`。STEP 新增严格容器、字段完整性和有限数值门禁；STL 拒绝不完整容器、闭合后垃圾、非有限坐标与面积计算溢出。坐标保留模型单位，无 SI_UNIT 或 IGES 单位换算认证；AXMJSON 原有 64 MiB 合同保留。导入后的验证/修复和失败回滚沿用上文共享管线。

STEP/AXMJSON/IGES/BREP/OBJ/STL/glTF/3MF 导出统一使用 classic locale / max_digits10（不扩大格式固有数值能力），同目录独占临时 payload 写完并检查关闭；四网格格式如请求侧车，侧车成功后再 rename 发布主文件。`.open/.write/.sidecar/.publish` 任一步失败均不宣称主文件成功；保护已有主文件、清理临时 payload，恢复本次三角化 mesh/cache/统计/next_id，保留诊断；成功仍保留转换缓存。publish 失败携带 `AXM-IO-E-0005 / io.export.<format>.publish`。侧车仍走 REP 出口，不承诺侧车与主文件、全批跨文件事务、掉电持久性或并发目录修改安全；辅助文本/诊断导出不自动继承此合同。

## 12. `Diagnostics` 接口清单

### 11.1 诊断对象

```cpp
enum class IssueSeverity {
  Info,
  Warning,
  Error,
  Fatal
};

struct NumericEvidence {
  std::string name;
  Scalar value;
  std::string unit;
};

struct Issue {
  std::string code;
  IssueSeverity severity;
  std::string message;
  std::vector<uint64_t> related_entities;
  std::string stage;
  std::vector<NumericEvidence> numeric_evidence;
};

struct DiagnosticReport {
  DiagnosticId id;
  std::vector<Issue> issues;
  std::string summary;
};

struct DiagnosticEvidencePolicy {
  std::string issue_code_prefix;
  std::string stage_prefix;
  IssueSeverity minimum_severity{IssueSeverity::Error};
  bool require_stage{true};
  bool require_related_entities{true};
  bool require_numeric_evidence{true};
  std::uint64_t max_findings{256};
};

struct DiagnosticEvidenceFinding {
  DiagnosticId diagnostic_id;
  std::uint64_t issue_index;
  std::string issue_code;
  bool matching_issue_missing;
  bool stage_missing_or_mismatched;
  bool related_entities_missing;
  bool numeric_evidence_missing_or_invalid;
};

struct DiagnosticEvidenceAudit {
  std::uint64_t reports_inspected;
  std::uint64_t matching_issues;
  std::uint64_t complete_issues;
  std::uint64_t reports_without_matching_issue;
  std::uint64_t issues_missing_stage;
  std::uint64_t issues_missing_related_entities;
  std::uint64_t issues_missing_numeric_evidence;
  std::uint64_t omitted_findings;
  std::vector<DiagnosticEvidenceFinding> findings;
  bool passed() const;
};
```

### 11.2 日志接口

```cpp
class DiagnosticService {
public:
  Result<DiagnosticReport> get(DiagnosticId) const;
  Result<void> append_issue(DiagnosticId, const Issue&);
  Result<void> export_report(DiagnosticId, std::string_view path) const;
  Result<void> export_report_json(DiagnosticId, std::string_view path) const;
  Result<void> export_reports_txt(std::span<const DiagnosticId>, std::string_view path) const;
  Result<void> export_reports_json(std::span<const DiagnosticId>, std::string_view path) const;
  Result<void> export_grouped_by_stage_txt(std::string_view path) const;
  Result<void> export_grouped_by_stage_json(std::string_view path) const;
  Result<std::vector<DiagnosticId>> find_by_issue_code_prefix(std::string_view prefix, std::uint64_t max_results) const;
  Result<std::vector<DiagnosticId>> find_by_related_entity(std::uint64_t entity_id, std::uint64_t max_results) const;
  Result<std::vector<DiagnosticId>> find_by_issue_stage(std::string_view stage, std::uint64_t max_results) const;
  Result<std::vector<DiagnosticId>> find_by_issue_stage_prefix(std::string_view prefix, std::uint64_t max_results) const;
  Result<DiagnosticEvidenceAudit> audit_evidence(
      std::span<const DiagnosticId>, const DiagnosticEvidencePolicy&) const;
  Result<void> export_evidence_audit_json(
      std::span<const DiagnosticId>, const DiagnosticEvidencePolicy&,
      std::string_view path) const;
};
```

`NumericEvidence{name, value, unit}` 为可机器读取的计数、阈值、距离或容差证据；名称在单个 issue 内稳定，单位可为空。单条、指定 ID 批量和全量 TXT/JSON 导出均保留该字段；JSON 遇到非有限数值写 `null`，审计则把空名称或非有限值视为证据无效。

`audit_evidence` 按问题码前缀与最低严重级别筛选 issue，并可要求阶段前缀、关联实体及有效数值证据。重复 `DiagnosticId` 只审计一次；没有匹配 issue 的报告计入 `reports_without_matching_issue`；`max_findings` 仅限制明细，额外缺口计入 `omitted_findings`，完整统计不截断。空 ID 集、空问题码前缀、必需但为空的阶段前缀、零明细上限、非法严重级别或无效 ID 均结构化失败，且不修改源报告。`export_evidence_audit_json` 先完成审计再打开文件；空路径、打开或最终写入失败复用 `AXM-IO-E-0005`。BOOL 受覆盖失败分支及第 73 批 HEAL 验证/修复/回滚、IO 主格式导入导出/后验验证/批处理已接入模块门禁；普通文件文本/目录辅助接口及更广泛的失败注入语料仍待扩充，不能据此宣称所有公开接口全覆盖。

问题码前缀、阶段精确与阶段前缀检索均按 `DiagnosticId` 升序返回最早的前 `max_results` 个匹配报告，单报告的多个匹配 issue 只返回一次。空问题码前缀、空阶段/阶段前缀或零上限返回 `InvalidInput` / `AXM-CORE-E-0002`，源报告保持不变。

相关实体检索同样先按 `DiagnosticId` 升序排序，再返回前 `max_results` 个匹配报告；同一报告内多个 issue 关联实体时只返回一次。实体 ID 为零或上限为零返回 `InvalidInput` / `AXM-CORE-E-0002`，源报告保持不变。`report_ids_by_entity` 使用相同的检索语义。

`export_reports_json` 顶层为 `{"diagnostics":[...]}`，每项与单报告 JSON 一致，包含 `id/summary/issues` 及问题的 `code/severity/message/stage/related_entities`；字符串控制字节转义后保留。按输入顺序导出，重复 ID 重复输出，无问题报告输出空 `issues`。

`export_reports_txt` 按输入顺序拼接单报告文本，保留摘要、每个问题的严重级别、错误码、消息及非空阶段与关联实体；重复 ID 重复输出，空报告仍包含 ID 和摘要。文本中的特殊字节原样保留。

上述两个指定 ID 批量导出接口：空 ID 列表或空路径返回 `InvalidInput` / `AXM-IO-E-0005`；任一 ID 不存在返回 `InvalidInput` / `AXM-CORE-E-0001`。上述参数失败均在打开文件前返回，不创建或截断目标，不修改源报告（仍生成失败诊断）。打开或写入失败返回 `OperationFailed` / `AXM-IO-E-0005`；写入期间的设备错误不保证恢复原文件。

阶段聚合导出把空 `Issue.stage` 归入 `(unset)`。空路径在打开文件前返回 `InvalidInput` / `AXM-IO-E-0005`，文件打开、写入或关闭失败返回 `OperationFailed` / `AXM-IO-E-0005`；导出不修改参与聚合的源报告，设备写入失败不保证恢复目标文件。

## 13. `TopoCore` 门面补充接口

为了与上层 `Kernel` 门面保持一致，建议提供一个轻量的拓扑入口服务，负责开启事务和聚合只读查询。

```cpp
class TopologyService {
public:
  TopologyTransaction begin_transaction();
  TopologyTransaction begin_transaction(const TopologyCancellationToken&);
  Result<bool> has_active_write_transaction() const;
  Result<TopologyCancellationMetrics> cancellation_metrics() const;
  Result<TopologySavepointMetrics> savepoint_metrics() const;
  TopologyQueryService& query();
  TopologyValidationService& validate();
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
