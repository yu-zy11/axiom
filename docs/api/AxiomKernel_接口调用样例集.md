# AxiomKernel 接口调用样例集

本文档提供 `AxiomKernel` 的典型接口调用样例，目标是帮助研发、测试、SDK 包装层和上层应用快速理解推荐调用方式、返回值语义和异常处理约定。本文档以伪代码和接近 `C++` 的代码风格为主，不要求与未来最终 API 命名完全一致，但要求流程和边界一致。

## 1. 文档目标

本文档回答以下问题：

- 上层系统如何创建和使用内核对象
- 每类核心操作如何调用
- 返回结果如何检查
- 失败时如何读取诊断
- 推荐的事务和验证调用方式是什么

## 2. 通用调用约定

### 2.1 推荐调用模式

所有调用建议遵循以下模式：

1. 构造输入
2. 调用内核服务
3. 检查 `status`
4. 若失败，读取 `diagnostic`
5. 若成功，再处理 `warnings`
6. 在关键操作后显式验证

### 2.2 推荐结果处理模板

```cpp
auto result = service.do_something(...);
if (result.status != StatusCode::Ok || !result.value.has_value()) {
  auto diag = kernel.diagnostics().get(result.diagnostic_id);
  log_error(result.status, diag);
  return;
}

if (!result.warnings.empty()) {
  log_warnings(result.warnings);
}

auto value = *result.value;
```

### 2.3 推荐诊断读取方式

```cpp
auto diag_result = kernel.diagnostics().get(result.diagnostic_id);
if (diag_result.status == StatusCode::Ok && diag_result.value.has_value()) {
  print(diag_result.value->summary);
  for (const auto& issue : diag_result.value->issues) {
    print(issue.code, issue.message);
  }
}
```

## 3. 初始化与基础对象

## 3.1 创建内核实例

```cpp
KernelConfig config;
config.tolerance.linear = 1e-6;
config.tolerance.angular = 1e-6;
config.precision_mode = PrecisionMode::AdaptiveCertified;
config.enable_diagnostics = true;
config.enable_cache = true;

Kernel kernel(config);
```

### 说明

- 生产环境建议默认启用诊断。
- `precision_mode` 推荐使用 `AdaptiveCertified`。
- 缓存建议默认开启，但测试基线场景可关闭。

## 3.2 创建基础几何对象

### 3.2.1 创建直线

```cpp
auto line = kernel.curves().make_line({0, 0, 0}, {1, 0, 0});
if (line.status != StatusCode::Ok) {
  handle_error(line);
  return;
}
```

### 3.2.2 创建圆

```cpp
auto circle = kernel.curves().make_circle(
  {0, 0, 0},
  {0, 0, 1},
  25.0
);
```

### 3.2.3 创建平面

```cpp
auto plane = kernel.surfaces().make_plane(
  {0, 0, 0},
  {0, 0, 1}
);
```

## 4. 几何求值样例

## 4.1 曲线求值

```cpp
auto line = kernel.curves().make_line({0,0,0}, {1,0,0});
auto eval = kernel.curve_service().eval(*line.value, 10.0, 2);

if (eval.status == StatusCode::Ok) {
  Point3 p = eval.value->point;
  Vec3 t = eval.value->tangent;
}
```

## 4.2 曲面求值

```cpp
auto plane = kernel.surfaces().make_plane({0,0,0}, {0,0,1});
auto eval = kernel.surface_service().eval(*plane.value, 10.0, 5.0, 2);

if (eval.status == StatusCode::Ok) {
  auto normal = eval.value->normal;
  auto k1 = eval.value->k1;
  auto k2 = eval.value->k2;
}
```

## 4.3 最近点查询

```cpp
Point3 query_p{12, 8, 3};
auto cp = kernel.surface_service().closest_point(surface_id, query_p);

if (cp.status != StatusCode::Ok) {
  handle_error(cp);
}
```

需要全域精度与终止证据时，使用曲线详细入口：

```cpp
CurveClosestPointOptions options;
options.distance_tolerance = 1e-10;
options.parameter_tolerance = 1e-10;
options.max_evaluations = 50000;

auto closest = kernel.curve_service().closest_point_detailed(curve_id, query_p, options);
if (!closest.value) {
  handle_error(closest);  // 预算耗尽等失败不返回部分结果，也不写求值缓存
  return;
}

print(closest.value->parameter,
      closest.value->distance,
      closest.value->distance_lower_bound,
      closest.value->parameter_uncertainty,
      closest.value->evaluations,
      closest.value->intervals_processed);
```

Line、LineSegment、Circle 与折线返回 `Analytic`；Ellipse、Parabola、Hyperbola、Bezier、BSpline、NURBS 和 CompositeChain 对完整有效域做分支限界。此结果类型只用于曲线；曲面使用对应的二维证据：

```cpp
SurfaceClosestPointOptions surface_options;
surface_options.distance_tolerance = 1e-8;
surface_options.parameter_tolerance = 1e-5;
surface_options.max_evaluations = 250000;

auto surface_closest = kernel.surface_service().closest_point_detailed(
    surface_id, query_p, surface_options);
if (!surface_closest.value) {
  handle_error(surface_closest);  // 失败无部分值，不写 surface eval 缓存
  return;
}

print(surface_closest.value->u, surface_closest.value->v,
      surface_closest.value->point, surface_closest.value->distance,
      surface_closest.value->distance_lower_bound,
      surface_closest.value->effective_domain,
      surface_closest.value->domain_was_finiteized,
      surface_closest.value->control_net_bound_patches,
      surface_closest.value->pruned_patches,
      surface_closest.value->convergence);
```

Plane、Cylinder、Cone、规则 Sphere/Torus 及嵌套 Offset 链返回 `Analytic`；若原始域含无界方向，`domain_was_finiteized` 为 true，`effective_domain` 给出本次解析证书使用的有限域，且不消耗数值求值预算。Bezier/BSpline/NURBS 的 `control_net_bound_patches` 与 `pruned_patches` 反映有理控制网凸包证书与剪枝工作量，Trimmed 和 Offset 包装保留保守性。当前 spindle/horn 环面仍走有界数值路径，通用无限派生面尚不自动有限化；偏置半径坍缩、负向完整锥面偏置自交或数值溢出都会结构化失败。

## 4.4 有界 3D 曲线-曲线求交

```cpp
auto arch = kernel.curves().make_bezier(
    std::array<Point3, 3>{{{0,0,0}, {1,2,0}, {2,0,0}}});
auto line = kernel.curves().make_line_segment({-1,.5,0}, {3,.5,0});

CurveCurveIntersectionOptions options;
options.position_tolerance = 1e-7;  // 模型长度单位
options.parameter_tolerance = 1e-7;
options.angular_tolerance = 1e-6;
options.max_evaluations = 200000;
options.max_subdivisions = 100000;

auto hits = kernel.geometry_intersection().intersect_curve_curve(
    *arch.value, *line.value, options);
if (!hits.value) { handle_error(hits); return; }
for (const auto& hit : hits.value->points) {
    print(hit.point, hit.first_parameter, hit.second_parameter,
          hit.residual_distance, hit.kind);
}
for (const auto& overlap : hits.value->overlaps) {
    print(overlap.first_interval, overlap.second_interval,
          overlap.same_direction, overlap.maximum_separation);
}
print(hits.value->evaluations,
      hits.value->parameter_rectangles_processed);  // 本次查询的工作量证据
```

空交集会成功返回两个空容器。无限 `Line` 必须在对应的 `first_interval` 或 `second_interval` 中显式给出有限参数窗口，例如 `options.second_interval = Range1D{-10.0, 10.0}`；不会自动从另一条曲线推断搜索域。离散交点分为 `Transverse/Tangent/Endpoint`；共线分段直线和可证明同参的高阶曲线返回连续 `overlaps`，一般高阶异参重合尚不作完备证明。非法句柄/区间/容差、无法建界或任一预算耗尽都不返回部分结果，也不写求值缓存、Intersection/拓扑存储或活动事务计数。

## 5. 基础体构造样例

## 5.1 创建盒体

```cpp
auto box = kernel.primitives().box({0,0,0}, 100.0, 80.0, 30.0);
if (box.status != StatusCode::Ok) {
  handle_error(box);
  return;
}

BodyId box_id = *box.value;
```

## 5.2 创建圆柱

```cpp
auto cyl = kernel.primitives().cylinder(
  {20, 20, 0},
  {0, 0, 1},
  10.0,
  30.0
);
```

## 5.3 构造后立即验证

```cpp
auto valid = kernel.validate().validate_all(box_id, ValidationMode::Standard);
if (valid.status != StatusCode::Ok) {
  auto diag = kernel.diagnostics().get(valid.diagnostic_id);
  report_validation_failure(diag);
}
```

## 6. 扫掠类操作样例

## 6.1 拉伸

```cpp
// 无孔 L 形轮廓；闭合边由末点到首点隐式补齐。
ProfileRef profile {"L", {{0, 0, 0}, {3, 0, 0}, {3, 1, 0},
                          {1, 1, 0}, {1, 3, 0}, {0, 3, 0}}};

auto solid = kernel.sweeps().extrude(
  profile,
  {0, 0, 1},
  50.0
);

if (solid.status != StatusCode::Ok) {
  handle_error(solid);
  return;
}
```

带孔截面可用同一入口；各环首尾隐式闭合，无需统一绕向：

```cpp
ProfileRef plate {"plate", {{0,0,0}, {8,0,0}, {8,6,0}, {0,6,0}},
                          {{{1,1,0}, {3,1,0}, {3,3,0}, {1,3,0}}}};
auto holed = kernel.sweeps().extrude(plate, {0,0,1}, 3);
if (!holed.value) { handle_error(holed); return; }
auto checked = kernel.validate().validate_all(*holed.value, ValidationMode::Strict);
auto faces = kernel.topology().query().faces_of_body(*holed.value); // 32 个真实三角面
auto edges = kernel.topology().query().edges_of_body(*holed.value); // 48 条边
auto mass = kernel.query().mass_properties(*holed.value); // 体积 (48−4)×3 = 132
// 同一 plate 可传给 sweep(plate, line_segment_id)，孔随截面一起平移。
// 截面方向不变的折线平移扫掠（第 62 包，已纳入统一门禁）。
// 每段 Z 位移同号；中间折点共享拓扑，孔贯通整条路径。
std::vector<axiom::Point3> rail_points {{50,60,70}, {54,58,71}, {47,61,73}, {51,60,76}};
auto rail = kernel.curves().make_composite_polyline(rail_points);
if (rail.value) {
    auto swept = kernel.sweeps().sweep(plate, *rail.value);
    if (swept.value) {
        auto validation = kernel.validate().validate_all(*swept.value, axiom::ValidationMode::Strict);
        auto faces = kernel.topology().query().faces_of_body(*swept.value);
        auto edges = kernel.topology().query().edges_of_body(*swept.value);
        auto mass = kernel.query().mass_properties(*swept.value);
    }
}

```

孔之间必须分离且不嵌套，不能接触外环；失败不留下部分实体。`revolve/revolve_between` 可接受与轴保持间隙的带孔截面，`loft` 可接受各站环拓扑和顶点数对应的带孔截面；带孔尖顶仍不支持。

### 等比变截面拉伸（第 65 包，已纳入统一门禁）

```cpp
// 孔与外环一起绕平面内的 (4,3,0) 缩放：顶部尺寸减半，法向高度 3。
auto tapered = kernel.sweeps().extrude_scaled(plate, {0,0,1}, 3, {4,3,0}, 0.5);
if (!tapered.value) { handle_error(tapered); return; }
auto valid = kernel.validate().validate_all(*tapered.value, ValidationMode::Strict);
auto faces = kernel.topology().query().faces_of_body(*tapered.value); // 32 个真实平面三角面
auto edges = kernel.topology().query().edges_of_body(*tapered.value); // 48 条边
auto mass = kernel.query().mass_properties(*tapered.value); // (48−4)×3×(1+0.5+0.25)/3 = 77
```

末端比例大于 1 表示扩张，等于 1 与普通显式轮廓拉伸一致；缩放中心可以在截面材料之外，但必须位于截面平面。反向通过方向向量表达，距离仍为正；不接受负比例、带孔尖顶、离面中心或数值退化。各截面保持等比相似，不等同于逐壁恒角拔模。

### 无孔尖顶拉伸（第 66 包，已纳入统一门禁）

```cpp
ProfileRef base {"pyramid", {{0,0,0}, {4,0,0}, {4,3,0}, {0,3,0}}};
// 顶点为 (2,1.5,6)，也支持凹轮廓、反向/斜向方向和倾斜平面上的轮廓。
auto pyramid = kernel.sweeps().extrude_scaled(base, {0,0,1}, 6, {2,1.5,0}, 0);
if (!pyramid.value) { handle_error(pyramid); return; }
auto apex_valid = kernel.validate().validate_all(*pyramid.value, ValidationMode::Strict);
auto apex_vertices = kernel.topology().query().vertices_of_body(*pyramid.value); // 5 个共享顶点
// 底面分成两个平面三角 Face，另有 4 个三角侧面；共 9 条边，体积为 24。
auto apex_mass = kernel.query().mass_properties(*pyramid.value);
auto apex_mesh = kernel.convert().brep_to_mesh(*pyramid.value, {});
```

仅恰好为零的末端比例进入尖顶路径；很小的正比例仍按独立末端截面处理，数值塌缩则拒绝。带孔轮廓的尖顶会形成非流形顶点，返回 `InvalidInput / AXM-CORE-E-0002`，不分配模型对象。

### 至平面拉伸

```cpp
// 每个轮廓顶点沿 +Z 射线命中斜目标面；距离可因顶点而异。
Plane target {{0, 0, 4}, {-0.2, 0.1, 1}};
auto to_plane = kernel.sweeps().extrude_to_plane(plate, {0, 0, 1}, target);
if (!to_plane.value) { handle_error(to_plane); return; }
auto to_plane_valid = kernel.validate().validate_all(*to_plane.value, ValidationMode::Strict);
auto to_plane_mass = kernel.query().mass_properties(*to_plane.value);
```

该入口支持凹多边形和孔，方向缩放及目标法向反号不改变几何。目标面必须在每条顶点射线的严格前方；切向、反向、接触/交叉平面或近退化输入返回 `InvalidInput / AXM-CORE-E-0002`，不留下部分实体。

### 扭转拉伸（cycle-0073，已通过完整门禁）

```cpp
ProfileRef twisted_profile;
twisted_profile.label = "twisted_plate";
twisted_profile.polygon_xyz = {{-2,-1,0}, {2,-1,0}, {2,1,0}, {-2,1,0}};
twisted_profile.holes_xyz = {{{-0.5,-0.4,0}, {0.5,-0.4,0},
                             {0.5,0.4,0}, {-0.5,0.4,0}}};
constexpr Scalar pi = 3.14159265358979323846;
auto twisted = kernel.sweeps().extrude_twisted(
    twisted_profile, Vec3{0,0,5}, 8.0, Point3{0,0,0}, -pi / 2);
if (twisted.value) {
  auto valid = kernel.validate().validate_all(*twisted.value, ValidationMode::Strict);
  use_if_valid(valid);
} else {
  auto report = kernel.diagnostics().get(twisted.diagnostic_id);
  handle_query_error(report);
}
```

方向按单位化使用，必须垂直于轮廓平面；中心须在该平面内，距离为有限正数。支持凸/凹外环、非嵌套分离孔洞和任意空间朝向，扭角以弧度表示，正负部分角及正负整周均可，限于一周。沿方向按右手规则扭转，零扭角在该法向合同下兼容 `extrude`。角站差不超过 7.5°，站间侧壁交替对角线剖分以避免系统性体积偏差。结果为有真实共享拓扑的采样多面体 BRep，质量属性来自该多面体；不是解析螺旋面，变比例联合扭转由 `extrude_with_law` 提供，至平面组合仍不支持。非法中心/方向/扭角或退化轮廓在模型分配前以 `InvalidInput / AXM-CORE-E-0002` 拒绝。

### 分段截面律拉伸与扫掠（cycle-0074，已通过完整门禁）

```cpp
axiom::ProfileRef law_profile;
law_profile.label = "section_law";
law_profile.polygon_xyz = {{-.2,-.1,0}, {.2,-.1,0}, {.2,.1,0}, {-.2,.1,0}};
constexpr axiom::Scalar law_pi = 3.14159265358979323846;
// fraction 是高度比例；中间扩张、扭转停顿，随后收缩并反向扭回。
std::vector<axiom::ExtrusionLawStation> extrusion_keys {
    {0,1,0}, {.4,1.5,law_pi/2}, {.6,1.5,law_pi/2}, {1,.75,0}};
auto law_body = kernel.sweeps().extrude_with_law(
    law_profile, {0,0,5}, 8, {0,0,0}, extrusion_keys);
if (law_body.value) {
    auto valid = kernel.validate().validate_all(*law_body.value, axiom::ValidationMode::Strict);
    auto mass = kernel.topology().query().body_mass_properties(*law_body.value);
    // mass 属于实际采样多面体；还须检查 valid/mass.value。
} else {
    auto report = kernel.diagnostics().get(law_body.diagnostic_id);
    handle_query_error(report);
}

auto law_rail = kernel.curves().make_line_segment({0,0,0}, {0,0,8});
if (law_rail.value) {
    // 扫掠 fraction 是归一化采样弦长，不是导轨参数或解析弧长。
    std::vector<axiom::SweepScaleStation> scale_keys {
        {0,1}, {.25,1.5}, {.75,.75}, {1,1}};
    auto scaled_law = kernel.sweeps().sweep_with_scale_law(
        law_profile, *law_rail.value, scale_keys);
    std::vector<axiom::SweepLawStation> combined_keys {
        {0,1,0}, {.4,1.5,law_pi/2}, {.6,1.5,law_pi/2}, {1,.75,0}};
    auto combined_law = kernel.sweeps().sweep_with_law(
        law_profile, *law_rail.value, combined_keys);
    for (const auto* result : {&scaled_law, &combined_law}) {
        if (result->value) {
            auto valid = kernel.validate().validate_all(*result->value, axiom::ValidationMode::Strict);
            use_if_valid(valid);
        } else {
            auto report = kernel.diagnostics().get(result->diagnostic_id);
            handle_query_error(report);
        }
    }
} else {
    auto report = kernel.diagnostics().get(law_rail.diagnostic_id);
    handle_query_error(report);
}
```

拉伸中心须共面，方向须法向，距离有限且正。直线/折线扫掠的起点须在截面平面内，每段严格同向穿过该平面；联合扭转绕按净推进方向定向的初始法向。曲线扫掠使用局部前向切向及传输标架，支持既有 Bezier/BSpline/NURBS 与非嵌套 G1 连续 CompositeChain；凹轮廓和分离非嵌套孔同样支持。

关键站从 `(0,1)` 或 `(0,1,0)` 到 fraction=1 严格递增，比例严格正；每步比例变化最多较小端的 25%，扭角最多 7.5°，累计绝对扭角最多一周，联合采样最多 4096 区间（含原导轨站和关键站）。原站与关键站数值重合时共享位置/标架，不可分辨的不同关键站失败。周期曲线允许中间变比例，但末比例必须为 1；联合律末角须为 0 或 ±2π（`1e-10 rad` 容差），首末环焊接、无端盖、不推断截面对称顶点置换。此能力不改变 `sweep_scaled` 仅接受周期单位终端比例的合同。

实际截面、盖片、推进/折叠、侧壁交叠/容差接触和质量积分在分配前检查；曲线保守曲率/间距门禁用最大比例，宽相候选上限 2000000。成功有真实拓扑、中间 bbox 和多面体质量；失败保留模型和活动事务，复用 `AXM-CORE-E-0002`（扫掠空标签/无效导轨为 `E-0001`）。扫掠律的采样/物化/周期扭角失败可查 `sweep_law_sampling/materialization/seam` 阶段和数值证据；输入预检沿用既有诊断。不支持零/负比例、尖顶、至平面组合、嵌套复合导轨、一般非线性解析律、解析扫掠/螺旋曲面或任意截面匹配。

## 6.2 旋转

```cpp
ProfileRef meridian {"ring", {{1,0,-1}, {3,0,-1}, {3,0,2}, {1,0,2}}};
Axis3 axis{.origin = {0, 0, 0}, .direction = {0, 0, 1}};
auto body = kernel.sweeps().revolve(meridian, axis, 2 * std::acos(-1.0)); // 弧度
if (!body.value) { handle_error(body); return; }
auto valid_revolution = kernel.validate().validate_all(*body.value, ValidationMode::Strict);
auto revolution_mesh = kernel.convert().brep_to_mesh(*body.value, {});

// 部分角使用同一真实闭壳路径，并物化首尾端盖。
auto quarter = kernel.sweeps().revolve(meridian, axis, std::acos(-1.0) / 2);
if (quarter.value) {
    auto quarter_valid = kernel.validate().validate_all(*quarter.value, ValidationMode::Strict);
    auto quarter_mass = kernel.topology().query().body_mass_properties(*quarter.value);
}

// 偏置起始角与负向区间；有符号跨度的绝对值不超过一周。
auto clockwise = kernel.sweeps().revolve_between(
    meridian, axis, std::acos(-1.0) / 3, -std::acos(-1.0) / 6);
auto symmetric = kernel.sweeps().revolve_between(
    meridian, axis, -std::acos(-1.0) / 4, std::acos(-1.0) / 4);
```

整周和部分角显式多边形旋转都支持与轴分离的外环及分离孔洞，也支持无孔且仅有一条连续边位于轴上的实心轮廓。`revolve_between` 的递减区间使用相反旋转方向，部分角首尾端盖绕向与之匹配；正负整周的几何与起始角无关，保持周期多壳语义。角分辨率为每周 48 段；部分角增加约束剖分的两个端盖。带孔区域触轴、跨轴、孤立轴点、近轴、自交或偏轴轮廓会在分配前被拒绝。结果是保守浮点分片的多面体 BRep，不是解析旋转曲面。

## 6.3 曲线导轨扫掠

```cpp
auto rail = kernel.curves().make_bezier({{0,0,0}, {4,0,0}, {8,1.5,0.5}, {12,3,1.5}});
// 起始截面位于导轨起点，法向与起始切向 +X 对齐。
ProfileRef section {"section", {{0,-.8,-.6}, {0,.8,-.6}, {0,.8,.6},
                                {0,.15,.6}, {0,.15,.15}, {0,-.8,.15}}};
if (rail.value) {
    auto swept = kernel.sweeps().sweep(section, *rail.value);
    if (!swept.value) { handle_error(swept); return; }
    auto strict = kernel.validate().validate_all(*swept.value, ValidationMode::Strict);
    auto mesh = kernel.convert().brep_to_mesh(*swept.value, {});

    // 截面比例按采样弧长从 1 线性过渡到 0.65。
    auto tapered = kernel.sweeps().sweep_scaled(section, *rail.value, 0.65);
    if (!tapered.value) { handle_error(tapered); return; }
    auto tapered_strict = kernel.validate().validate_all(
        *tapered.value, ValidationMode::Strict);
}
```

Bezier/BSpline/NURBS 开放导轨、显式端点重合且首尾切向连续的闭合样条，以及整圆/椭圆周期导轨使用旋转最小标架；空间闭环会做 holonomy 校正。`make_composite_chain(children)` 创建的复合导轨也可直接传入，相邻子段必须端点重合且 G1 切向连续；开放链有两个端盖，闭合链无端盖。周期带孔截面的外边界与各孔边界分别物化为独立闭壳，因此壳数和网格连通分量都为 `1 + holes_xyz.size()`；开放带孔导轨由端盖连成单壳。`sweep_scaled` 对直线、CompositePolyline 和上述开放曲线/复合导轨支持正比例收缩与扩张，`end_scale=1` 与 `sweep` 兼容。非单位比例不适用于周期导轨；零/负比例、一般非线性解析比例律、过小比例、伪闭合、切向断裂、嵌套复合链、抛物/双曲子段、尖点、过紧曲率和自靠近导轨保守拒绝；这是采样多面体 BRep，不是解析扫掠曲面。

## 6.4 放样

```cpp
std::vector<ProfileRef> profiles = {
  profile_1,
  profile_2,
  profile_3
};

auto loft = kernel.sweeps().loft(profiles);
if (loft.value) {
    auto strict = kernel.validate().validate_all(*loft.value, ValidationMode::Strict);
    auto regions = kernel.topology().query().body_shell_regions(*loft.value);
}
```

各截面必须提供显式共面多边形；外环及同索引孔环在所有站保持相同顶点数，顶点顺序定义直纹侧壁对应关系。允许凹外环、分离孔洞、独立环绕向和倾斜截面；不同环拓扑、自动顶点匹配、分支或坍塌截面会在分配前拒绝。

## 6.5 平面直边 Face 加厚（cycle-0077 / S3-MODELING）

以下构造独立矩形 Face，再沿其支撑 Plane 的单位法向单侧加厚；示例在使用 `axiom` 类型的上下文中，失败可用 `diagnostic_id` 查询。

```cpp
Kernel kernel;
const std::array<Point3, 4> points {{{0,0,0}, {6,0,0}, {6,4,0}, {0,4,0}}};
auto plane = kernel.surfaces().make_plane({0,0,0}, {0,0,1});
if (!plane.value) return 1;

auto setup = kernel.topology().begin_transaction();
std::vector<VertexId> vertices;
for (const auto& point : points) {
  auto vertex = setup.create_vertex(point);
  if (!vertex.value) return 1;
  vertices.push_back(*vertex.value);
}
std::vector<CoedgeId> coedges;
for (std::size_t i = 0; i < points.size(); ++i) {
  const auto next = (i + 1) % points.size();
  auto curve = kernel.curves().make_line_segment(points[i], points[next]);
  if (!curve.value) return 1;
  auto edge = setup.create_edge(*curve.value, vertices[i], vertices[next]);
  if (!edge.value) return 1;
  auto coedge = setup.create_coedge(*edge.value, false);
  if (!coedge.value) return 1;
  coedges.push_back(*coedge.value);
}
auto loop = setup.create_loop(coedges);
if (!loop.value) return 1;
auto face = setup.create_face(*plane.value, *loop.value, {});
if (!face.value || setup.commit().status != StatusCode::Ok) return 1;

auto thickened = kernel.sweeps().thicken(*face.value, 2.0);
if (!thickened.value) {
  auto diagnostic = kernel.diagnostics().get(thickened.diagnostic_id);
  return 1;  // 读取 Issue.stage：input_gate/topology_gate/support_gate/materialization
}
auto strict = kernel.validate().validate_all(*thickened.value, ValidationMode::Strict);
auto mass = kernel.query().mass_properties(*thickened.value);
auto section = kernel.query().section_detailed(*thickened.value, {{0,0,1}, {0,0,1}});
if (strict.status != StatusCode::Ok || !mass.value || !section.value) return 1;
// 单位密度：V=48，A=88，C=(3,2,1)，截面面积=24。
// 独立 Face 无源壳/体；结果仍记录源 Face，拥有独立闭壳拓扑。
```

支撑法向反号会使结果移到 z∈[-2,0]，反转环绕向不改变加厚方向；不是双侧或对称偏置。支持简单凹轮廓、分离非嵌套孔与有效 Line/LineSegment 裁剪边；全部环周长计入侧壁面积。当前实际拓扑用于通用/体/壳质量与截面/最近边界查询，平面棱柱在浮点容差内精确，曲面/曲边/代理输入拒绝。活动事务内失败不污染，回滚后可重试；结果编辑为曲面即拒绝旧质量，回滚恢复。完整五类范围和回归见 [API §8.1.1](AxiomKernel_详细模块接口清单.md#811-stage-3-五类建模主路径cycle-0077--s3-modeling) 与 [验收 §1.4](../quality/AxiomKernel_测试与验收方案.md#14-cycle-0077--s3-modeling-门禁与逐项证据)。本样例未单独编译，实际门禁证据来自对应测试。

## 7. 布尔操作样例

第一代求交边界见 [§7.5](#75-只读平面几何求交准备cycle-0080--s4-intersection)，只读切分与实体分类见 [§7.6](#76-只读真实切分与实体分类cycle-0081--s4-split-classify)。§7.1～7.4 展示兼容 run 调用形态；圆柱代理与自动修复不构成连续曲面布尔或重建实体正确性认证。

## 7.1 差集

```cpp
auto a = kernel.primitives().box({0,0,0}, 100, 80, 30);
auto b = kernel.primitives().cylinder({20,20,0}, {0,0,1}, 10, 30);

BooleanOptions opts;
opts.tolerance = kernel.tolerance().global_policy();
opts.diagnostics = true;
opts.auto_repair = true;

auto result = kernel.booleans().run(
  BooleanOp::Subtract,
  *a.value,
  *b.value,
  opts
);

if (result.status != StatusCode::Ok || !result.value.has_value()) {
  auto diag = kernel.diagnostics().get(result.diagnostic_id);
  show_boolean_failure(diag);
  return;
}

BodyId output = result.value->output;
```

## 7.2 并集

```cpp
auto result = kernel.booleans().run(
  BooleanOp::Union,
  body_a,
  body_b,
  opts
);
```

## 7.3 交集

```cpp
auto result = kernel.booleans().run(
  BooleanOp::Intersect,
  body_a,
  body_b,
  opts
);
```

## 7.4 布尔后验证

```cpp
auto check = kernel.validate().validate_all(output, ValidationMode::Strict);
if (check.status != StatusCode::Ok) {
  auto repaired = kernel.repair().auto_repair(output, RepairMode::Safe);
  handle_optional_repair(repaired);
}
```

## 7.5 只读平面几何求交准备（cycle-0080 / S4-INTERSECTION）

```cpp
using namespace axiom;
auto a = kernel.primitives().box({0,0,0}, 2, 2, 2);
auto b = kernel.primitives().box({1,1,1}, 2, 2, 2);
if (!a.value || !b.value) return;

BooleanIntersectionOptions options;  // 默认线/角容差 1e-6，独立于全局策略
options.tolerance = kernel.tolerance().global_policy();  // 沿用全局策略时显式复制
// 预算限制全部面比较及输出；此夹具 36 次面比较，6 个候选，6 条交段。
options.max_face_pairs = 36;
options.max_segments = 6;
options.max_edges_per_face = 256;
auto prepared = kernel.booleans().prepare_intersections(*a.value, *b.value, options);
if (prepared.status != StatusCode::Ok || !prepared.value) {
  auto report = kernel.diagnostics().get(prepared.diagnostic_id);
  if (report.value) {
    for (const auto& issue : report.value->issues) {
      // issue.code / issue.stage / related_entities / numeric_evidence
      // bool.prep.candidates 或 bool.intersect；没有可用的部分交段。
      show_issue(issue);
    }
  }
  return;
}
for (const auto& segment : prepared.value->segments) {
  // segment.begin/end 为坐标；lhs_face/rhs_face 是输入真实面 ID。
  // point_contact 表示点接触。begin_hits/end_hits 关联真实边：
  // hit.edge_fraction 沿 edge 的 v0 -> v1，不随 coedge 反转。
  consume_intersection_for_later_split(segment);
}
```

本样例片段未单独编译；实际执行证据来自对应回归与调度器门禁日志。`show_issue` 与 `consume_intersection_for_later_split` 是应用侧示意函数。prepare 不分配模型对象或写事务，成功后也不在内核交线存储中新增集合。该重叠盒的六条单位边来自一坐标=2、另一坐标=1、第三坐标∈[1,2] 的解析参考；固定回归逐条检查覆盖、源边集合和比例。分离/包含的边界空交成功，不能由此推断布尔实体的体积或材料分类。

共面面接触/相同体返回 NotImplemented/E-0014/bool.intersect；曲面/曲边/代理及 ExactCritical 返回 E-0011/bool.prep.candidates；预算耗尽 E-0012，数值不可分辨 E-0013。所有失败可查 diagnostic_id。候选 bbox 只是筛选；斜楔与位于 x+y>2 的盒即使候选非空也必须零交段。凹形/孔洞的完整 16 区间、RxRz 旋转及相切参考见 [固定参考模型集](AxiomKernel_详细模块接口清单.md#固定参考模型集与独立结果)；三条证据和真实门禁见 [验收 §1.7](../quality/AxiomKernel_测试与验收方案.md#17-cycle-0080--s4-intersection-门禁与逐项证据)。

兼容 run 已按真实面边界裁剪；读取短边、超 256 边或大坐标失败在 bool.intersect.trim 结构化返回，所有局部读取完成后才物化交线与结果体。Generic 单面壳的 run 失败夹具不属于 prepare 的闭壳支持认证。有限平面只读切分/分类见 §7.6；精确谓词认证、布尔实体重建、二维共面区域求交、连续曲面/曲边求交和全局壳嵌入证明仍未认证。

## 7.6 只读真实切分与实体分类（cycle-0081 / S4-SPLIT-CLASSIFY）

```cpp
using namespace axiom;
auto a = kernel.primitives().box({0,0,0}, 2, 2, 2);
auto b = kernel.primitives().box({1,1,1}, 2, 2, 2);
if (!a.value || !b.value) return;

BooleanSplitClassificationOptions options;  // 使用默认累计工作与片数预算
options.intersection.tolerance = kernel.tolerance().global_policy();
auto prepared = kernel.booleans().prepare_split_classification(*a.value, *b.value, options);
if (prepared.status != StatusCode::Ok || !prepared.value) {
  auto report = kernel.diagnostics().get(prepared.diagnostic_id);
  if (report.value) for (const auto& issue : report.value->issues) show_issue(issue);
  return;  // 失败没有部分切分/分类结果
}
for (const auto& fragment : prepared.value->fragments) {
  // source_body/source_face 是输入实体；vertices 为真实三角分片。
  // source_edges=0 为内部细分边；非零边参数沿原 v0->v1，可递减。
  // intersection_segments / adjacent_fragments 为本结果内的索引。
  consume_face_fragment(fragment);
}
for (const auto& edge : prepared.value->edge_fragments) {
  // begin_fraction < end_fraction，沿源边 v0->v1，保留原 incident faces。
  consume_edge_fragment(edge);
}
std::array<Point3, 3> points{{{0.5,0.5,0.5}, {3,0.5,0.5}, {0,1,1}}};
auto classified = kernel.booleans().classify_points(*a.value, points, options.intersection);
if (classified.status != StatusCode::Ok || !classified.value) {
  auto report = kernel.diagnostics().get(classified.diagnostic_id);
  if (report.value) for (const auto& issue : report.value->issues) show_issue(issue);
  return;
}
// 与 points 顺序一致：Inside、Outside、Boundary；最后一项含真实 boundary_faces。
consume_point_classifications(*classified.value);
```

`show_issue` 与 `consume_*` 是应用侧示意函数，本样例未单独编译；执行证据来自 [回归与最终门禁 §1.8](../quality/AxiomKernel_测试与验收方案.md#18-cycle-0081--s4-split-classify-门禁与逐项证据)。默认容差独立于全局设置，上例显式复制；参考预期基于默认 1e-6 策略。不要照搬 §7.5 的 36 次面比较预算，split 后续累计边界读取/细分/邻接/分类另需工作预算。max_fragments 限 face+edge 总片数，max_segments 也限独立分类输入点数。

盒总面积各24、对方内部面积各3；结果供后续实体重建使用，不新建模型、交线集合或写事务。分类使用实际裁剪边界和至少两条一致有效射线。真实舍入尺度边界返回 Boundary；5e-7 近边界不确定点返回 E-0013/bool.classify。非共面相切已回归；共面候选/面接触/相同体仍由 bool.intersect/E-0014 保守拒绝。稳定切分失败为 bool.split/E-0004，预算为 E-0012；先行求交阶段原样传播。全部支持范围、来源和邻接语义见 [API §8.2.2](AxiomKernel_详细模块接口清单.md#822-stage-4-第一代切分与实体分类支持矩阵cycle-0081--s4-split-classify)。兼容run仍含bbox实体语义，本只读prepare不提供实体重建或二维共面；受限真实重建另见§7.7，曲面/曲边及全局嵌入仍未认证。

## 7.7 真实并/差/交、空材料与可选Safe（cycle-0082 / S4-REBUILD）

```cpp
using namespace axiom;
auto a = kernel.primitives().box({0,0,0}, 2, 2, 2);
auto b = kernel.primitives().box({1,1,1}, 2, 2, 2);
if (a.status != StatusCode::Ok || !a.value ||
    b.status != StatusCode::Ok || !b.value) return;
BooleanRebuildOptions options;
options.preparation.intersection.tolerance = kernel.tolerance().global_policy();
options.auto_repair = true;  // 仅对明确人工分片几何缺陷尝试受限Safe

auto result = kernel.booleans().run_rebuilt(BooleanOp::Union, *a.value, *b.value, options);
if (result.status != StatusCode::Ok || !result.value) {
  auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
  if (diagnostic.value)
    for (const auto& issue : diagnostic.value->issues) show_issue(issue);
  return;
}
if (!result.value->output) {
  consume_empty_material();  // 空交/完全减除为成功，无BodyId可查询
  return;
}
const BodyId output = *result.value->output;
auto faces = kernel.topology().query().faces_of_body(output);
auto edges = kernel.topology().query().edges_of_body(output);
auto shells = kernel.topology().query().shells_of_body(output);
auto sources = kernel.topology().query().source_bodies_of_body(output);
auto mass = kernel.topology().query().body_mass_properties(output);
auto strict = kernel.validate().validate_all(output, ValidationMode::Strict);
if (!faces.value || !edges.value || !shells.value || !sources.value || !mass.value ||
    strict.status != StatusCode::Ok) return;
// 本固定并集V=15、A=42；auto_repair不代表repaired必为true。
consume_rebuilt(output, result.value->repaired, *mass.value);

// 完全减除：同ID只生成一侧并去重来源，成功空材料。
auto empty = kernel.booleans().run_rebuilt(BooleanOp::Subtract, *a.value, *a.value);
if (empty.status == StatusCode::Ok && empty.value && !empty.value->output)
  consume_empty_material();
```

`show_issue/consume_*`为应用示意函数，片段未单独编译；执行证据来自workflow/heal/ops_heal/query_eval/prep实际回归及 [最终门禁§1.9](../quality/AxiomKernel_测试与验收方案.md#19-cycle-0082--s4-rebuild-门禁与逐项证据)。诊断在外层Result，必须分别检查Result.value与报告output。只接受U/D/I，Split拒绝。公开面/壳/体来源可查，边经faces_of_edge→source_faces_of_face间接追溯；同形/同ID/共面共享区域来源去重且保留双方信息。

真实内部共面及面相切支持不改变§7.5/§7.6公开prep共面拒绝，兼容run保留历史bbox代理语义。成功非空输出已经Strict及真实壳材料关系认证；受限Safe保持真外/孔环和角点，不调用bbox代理修复或放宽Strict。固定Safe夹具准备1e-6、服务Strict1e-3：Union auto=false以bool.validate失败，auto=true成功repaired=true；Subtract直接Strict成功repaired=false。若设置服务容差，使用现有ToleranceService API，准备选项本身不改变服务Strict容差。

在已打开的拓扑writer内调用成功非空重建会登记服务分配，不增加显式write_operation_count，输出参与保存点/完整rollback；失败恢复新增对象、geometry/cache/Eval而保留diagnostic/递增ID，输入Eval保持有效。保存点只清其后输出，完整rollback防delete后的旧快照复活，回滚后可重试；合法累计遥测不回退。[支持矩阵及V/A/S参考](AxiomKernel_详细模块接口清单.md#823-stage-4-真实实体重建支持矩阵cycle-0082--s4-rebuild)保留边点Union拒绝、真实薄层/Safe失败、曲面曲边/ExactCritical不认证、Strict至少六面/近似网格自交及人工节点截面数值拒绝限制。

cycle-0083 / S4-EXIT 沿用上述公开签名和调用片段，新增执行证据见 [验收 §1.10](../quality/AxiomKernel_测试与验收方案.md#110-cycle-0083--s4-exit-门禁与逐项证据) 与 [退出矩阵 §8.2.4](AxiomKernel_详细模块接口清单.md#824-stage-4-退出支持矩阵cycle-0083--s4-exit)。workflow 中先 `brep_to_mesh(output,{})`，再 `inspect_mesh` 核对 owned 标签/策略、索引/退化与计数，随后 `export_obj(output,path,{})` 并独立解析三角形积分 V/A（long double 累计，误差 ≤1e-7），再次转换应命中同 MeshId。偏移盒 U/D/I 各总计两轮，V/A/S 为 15/42/7、7/24/3、1/6/1；分离并 11/37/4、包含空腔 7.875/25.5/3.75 也经过该表示核对。孔/凹U独立公式在prep既有回归，未新增同样OBJ覆盖。片段仍为应用示意，未单独编译。

失败可用外层 diagnostic_id 查询、按 `Issue.stage` / code 检索并 JSON 导出，七阶段证据见字典 §7.5；本批新检索覆盖重建/验证/修复。暖缓存/Eval/writer/rollback隔离为公开摘要，非完整几何序列化；合法累计遥测保留。平面边界三角化不认证通用曲面采样，兼容run与run_rebuilt范围应分开使用；现有性能基线只测兼容run/查询，run_rebuilt工业性能未认证。调度器本批CTest 16/16、194.58 s通过，最终文档门禁及提交尚未记录，Stage4/FR-BOOL-001仍进行中。

## 8. 修改操作样例

## 8.1 偏置

以下片段使用已有 `kernel`，标准头包括 `<vector>`、`<cmath>`、`<cassert>`；创建4×5×6完整轴对齐盒。仅认证当前 owned 六平面矩形闭壳；正距离外扩、负距离内缩，位移须大于有效容差且可在当前坐标精度下表达。

```cpp
auto stock = kernel.primitives().box({1,2,3}, 4,5,6);
if (!stock.value) { handle_error(stock); return; }
auto offset = kernel.modify().offset_body(
  *stock.value,
  0.5,
  kernel.tolerance().global_policy()
);
if (!offset.value) { handle_error(offset); return; }
auto offset_strict = kernel.validate().validate_all(
    offset.value->output, ValidationMode::Strict);
if (offset_strict.status != StatusCode::Ok) { handle_error(offset_strict); return; }
// 真实外边界为 [0.5,5.5] × [1.5,7.5] × [2.5,9.5]，尺寸5×6×7。
```

## 8.2 抽壳

```cpp
auto& topo = kernel.topology().query();
auto faces = topo.faces_of_body(offset.value->output);
if (!faces.value) { handle_error(faces); return; }
FaceId top_face {};
for (auto face : *faces.value) {
  auto bounds = topo.bbox_of_face(face);
  if (!bounds.value) { handle_error(bounds); return; }
  if (std::abs(bounds.value->min.z-9.5) < 1e-9 &&
      std::abs(bounds.value->max.z-9.5) < 1e-9) top_face = face;
}
assert(top_face.value != 0);
std::vector<FaceId> removed_faces {top_face};
auto shelled = kernel.modify().shell_body(
  offset.value->output,
  removed_faces,
  0.5
);
if (!shelled.value) { handle_error(shelled); return; }
auto strict = kernel.validate().validate_all(shelled.value->output, ValidationMode::Strict);
if (strict.status != StatusCode::Ok) { handle_error(strict); return; }
auto mass = kernel.query().mass_properties(shelled.value->output);
if (!mass.value) { handle_error(mass); return; }
assert(std::abs(mass.value->volume-80.0) < 1e-7);
assert(std::abs(mass.value->area-331.0) < 1e-7);

// 无移除面：原4×5×6盒保持外边界，生成反向内腔双闭壳。
std::vector<FaceId> no_opening;
auto cavity = kernel.modify().shell_body(*stock.value, no_opening, 0.5);
if (!cavity.value) { handle_error(cavity); return; }
auto cavity_mass = kernel.query().mass_properties(cavity.value->output);
if (!cavity_mass.value) { handle_error(cavity_mass); return; }
assert(std::abs(cavity_mass.value->volume-60.0) < 1e-7);
assert(std::abs(cavity_mass.value->area-242.0) < 1e-7);
```

单面开口支持六个方向；实际外5面、内5面与四片口沿构成闭合材料壳，保留壁厚为真实平面间距0.5。偏置后的上开口内腔为4×5×6.5，故 V=210−130=80，A=331；原盒上开口参考为 V=54/A=225。空移除集合生成外6面与反向内6面的双闭壳，内腔为3×4×5，V=120−60=60，A=148+94=242。一般曲面、多开口、异属/重复/不存在移除面、塌缩、容差接触或不可表达厚度拒绝。

源和结果在私有暂存中 Strict 检查；失败无部分结果、live ID/源拓扑来源索引/Eval/暖缓存保持。成功追加独立 `Generic/ExactBRep`，通知输入 Eval 及下游失效；活动事务回滚清理派生几何/拓扑/表示/缓存，保留源暖网格，成功分配 ID 允许空档。可从 diagnostic_id 查看 `modify.offset.* / modify.shell.*` 阶段。ReportOnly 保持厚度已有回归，修改式 Safe 修复保形未认证。

片段未独立编译，本轮未运行测试；执行证据来自已通过调度器完整门禁的 [Ops/Heal/Rep 回归与两条验收证据](../quality/AxiomKernel_测试与验收方案.md#116-cycle-0089--s6-offset-shell-门禁与逐项证据)。独立参考为解析公式/公开 OBJ 积分，无外部工业内核认证；完整支持与限制见 [API §8.3.1](AxiomKernel_详细模块接口清单.md#831-stage-6-真实偏置与抽壳支持矩阵cycle-0089--s6-offset-shell)。

## 8.3 删除面补面

```cpp
auto healed = kernel.modify().delete_face_and_heal(body_id, target_face);
```

## 9. 圆角与倒角样例

### 9.1 从当前公开边界选平行边（cycle-0088 / S6-BLEND）

以下片段延续已创建的 `kernel`；使用 4×5×6 轴对齐盒的四条 Z 向凸边，半径/退让距离均为 0.4，退让区互不干涉。X/Y 轴同样支持。选择和核验使用公开查询；所需标准头包括 `<vector>`、`<cmath>`、`<cassert>`。

```cpp
auto source = kernel.primitives().box({0,0,0}, 4,5,6);
if (!source.value) { handle_error(source); return; }
auto& topo = kernel.topology().query();
auto source_edges = topo.edges_of_body(*source.value);
if (!source_edges.value) { handle_error(source_edges); return; }
std::vector<EdgeId> edges;
for (auto edge : *source_edges.value) {
  auto vertices = topo.vertices_of_edge(edge);
  if (!vertices.value) { handle_error(vertices); return; }
  auto a = topo.point_of_vertex((*vertices.value)[0]);
  auto b = topo.point_of_vertex((*vertices.value)[1]);
  if (!a.value || !b.value) { return; }
  if (std::abs(a.value->x-b.value->x) < 1e-12 &&
      std::abs(a.value->y-b.value->y) < 1e-12 &&
      std::abs(a.value->z-b.value->z) > 5.0) edges.push_back(edge);
}
assert(edges.size() == 4);
// 可选 1–4 条同轴平行边；每次从同一 source 生成独立结果。
auto fillet = kernel.blends().fillet_edges(*source.value, edges, 0.4);
if (!fillet.value) { handle_error(fillet); return; }
auto fillet_strict = kernel.validate().validate_all(
    fillet.value->output, ValidationMode::Strict);
if (fillet_strict.status != StatusCode::Ok) { handle_error(fillet_strict); return; }
```

### 9.2 公开核对真实圆弧及倒角

```cpp
auto rounded_edges = topo.edges_of_body(fillet.value->output);
if (!rounded_edges.value) { handle_error(rounded_edges); return; }
std::size_t arc_count = 0;
for (auto edge : *rounded_edges.value) {
  auto curve = topo.curve_of_edge(edge);
  auto interval = topo.edge_curve_interval(edge);
  if (!curve.value || !interval.value) { return; }
  if (!*interval.value) continue;  // 兼容直边可无显式区间
  const auto range = **interval.value;
  auto mid = kernel.curve_service().eval(
      *curve.value, (range.start_parameter+range.end_parameter)/2, 2);
  if (!mid.value) { handle_error(mid); return; }
  if (std::abs(mid.value->curvature) < 1e-12) continue;
  ++arc_count;
  auto length = topo.edge_length(edge);
  assert(length.value);
  assert(std::abs(mid.value->curvature-1/0.4) < 1e-8);
  assert(std::abs(*length.value-std::acos(-1.0)*0.4/2) < 1e-8);
}
assert(arc_count == 8);  // 每条圆角棱边两端各有一个四分之一圆弧

// 倒角仍调用原 source；distance 是邻接平面的退让距离。
auto chamfer = kernel.blends().chamfer_edges(*source.value, edges, 0.4);
if (!chamfer.value) { handle_error(chamfer); return; }
auto chamfer_strict = kernel.validate().validate_all(
    chamfer.value->output, ValidationMode::Strict);
if (chamfer_strict.status != StatusCode::Ok) { handle_error(chamfer_strict); return; }
auto mass = kernel.query().mass_properties(chamfer.value->output);
if (!mass.value) { handle_error(mass); return; }
const double section = 4*5-4*0.4*0.4/2;
const double perimeter = 2*(4+5)+4*(std::sqrt(2.0)-2)*0.4;
assert(std::abs(mass.value->volume-6*section) < 1e-8);
assert(std::abs(mass.value->area-(2*section+6*perimeter)) < 1e-8);
```

斜面边宽为 `sqrt(2)*0.4`。`surface_of_face` 可取得真实 Cylinder/Plane 支撑，独立曲率与端点切触检查见 [验收 §1.15](../quality/AxiomKernel_测试与验收方案.md#115-cycle-0088--s6-blend-门禁与逐项证据)。getter 只读不分配模型对象或写几何缓存，诊断与读审计可增长。圆角体通用质量/实体空间查询及无 PCurve 面面积不支持；圆角质量明确返回 NotImplemented、无 value 与 `query.mass_properties.support_gate`，不能照用上述倒角积分参考。

### 9.3 失败与回滚

失败使用 `diagnostic_id` 查询 `Issue.code/stage`，前缀为 `blend.fillet.` 或 `blend.chamfer.`：input_gate/E-0001 检查输入，support_gate/E-0003 检查当前边界，intersection_gate/E-0004 拒绝非平行边与角区，radius_gate 或 distance_gate/E-0002 表示退让接触/重叠，geometry_gate/E-0005 表示容差或浮点退化，validation/E-0006 表示 Strict 失败。成功为 complete/I-0001；具体文案见 [字典 §7.6](../diagnostics/AxiomKernel_错误码与诊断码字典.md#76-blend-圆角倒角错误码)。

失败无输出，不改变源模型、ID、索引、Eval 或暖缓存。成功输出由活动事务登记，保存点及完整 writer 回滚清理派生几何/缓存并保留源暖缓存；已成功分配的 ID 不承诺复用。仅支持当前完整轴对齐矩形闭壳及互不干涉平行凸边；一般曲面、连续二次圆角、变半径/变距未支持，不能对 fillet 输出继续倒角并期待成功。调度器最终完整 CTest 16/16、200.33 s 通过，本片段为文档样例，本轮未编译运行样例。

## 10. 查询与分析样例

## 10.1 质量属性（cycle-0076 / S3-MASS）

```cpp
auto box = kernel.primitives().box({10,20,30}, 2,3,4);
if (!box.value) { handle_error(box); return; }
auto mp = kernel.query().mass_properties(*box.value);
if (!mp.value) {
  handle_error(mp);  // diagnostic_id → Issue.code / query.mass_properties.*
  return;           // 失败无部分数值，不用 bbox 或来源记录代算
}
print("volume", mp.value->volume);      // 24
print("area", mp.value->area);          // 52
print("centroid", mp.value->centroid);  // (11,21.5,32)
print("inertia", mp.value->inertia);    // diag(50,40,26)，质心世界张量
```

密度为 1；体积/面积/重心/惯性单位分别为模型长度的 3/2/1/5 次方。惯性是关于质心的世界坐标系行主序张量，非对角项是负积惯量；若另有均匀物理密度 ρ，质量为 `ρ*volume`、物理惯性为 `ρ*inertia`。

真实 ExactBRep box/wedge、已物化 Sweep 与用户 Generic 平面直边闭壳每次从当前拓扑积分，通用/专用体质量一致。独立材料壳相加、奇数深度空腔相减、偶数深度岛相加，面积含所有内外边界；壳顺序不决定材料角色，相交/重合/建模容差接触多壳拒绝。采样 revolve/曲线 sweep/截面律返回实际多面体属性，光滑解析极限只作误差对照。

```cpp
auto sphere = kernel.primitives().sphere({0,0,0}, 2);
if (!sphere.value) { handle_error(sphere); return; }
auto native_mass = kernel.query().mass_properties(*sphere.value); // V=32π/3，A=16π
// 这是未编辑原生记录的解析质量；其兼容壳不是球的物理边界。
auto faces = kernel.topology().query().faces_of_body(*sphere.value);
if (!faces.value || faces.value->empty()) { handle_error(faces); return; }
auto replacement = kernel.surfaces().make_plane({0,0,0}, {0,0,1});
if (!replacement.value) { handle_error(replacement); return; }
auto txn = kernel.topology().begin_transaction();
auto changed = txn.replace_surface(faces.value->front(), *replacement.value);
if (changed.status != StatusCode::Ok) { handle_error(changed); return; }
auto edited_mass = kernel.query().mass_properties(*sphere.value);
// NotImplemented / AXM-CORE-E-0004 / query.mass_properties.support_gate；无 value。
auto rolled_back = txn.rollback();
if (rolled_back.status != StatusCode::Ok) { handle_error(rolled_back); return; }
auto restored_mass = kernel.query().mass_properties(*sphere.value); // 恢复原解析质量
```

原生球/柱/锥/环仅 ExactBRep 且未编辑时有解析资格；专用体/壳质量拒绝代理边界。成功面替换或改变 PCurve 绑定/删除会撤销资格，失败编辑保留，保存点/回滚恢复，提交后仍拒绝。圆锥横向质心惯性为 `V*(3r²/20+3h²/80)`，重心距 apex 为 `3h/4`。

metadata-only STEP 恢复与 mesh/implicit 派生 BRep 不继承解析资格；Boolean/Modified、旧 label-only extrude 与历史占位 thicken 记录、未知体类/表示或兼容代理壳均 support_gate 拒绝。真实 box 改为曲面 support_gate 拒绝，支撑平面错配/开壳 preflight 拒绝，回滚恢复；热网格、来源或创建缓存不能恢复旧值。数值溢出/惯性下溢为 `AXM-QUERY-E-0003 / numeric`，无部分属性。查询不创建网格或改变缓存/Eval/事务写计数。完整范围、采样误差和独立参考见 [质量支持矩阵](AxiomKernel_详细模块接口清单.md#613-stage-3-质量属性支持矩阵cycle-0076--s3-mass) 与 [三条验收证据](../quality/AxiomKernel_测试与验收方案.md#13-cycle-0076--s3-mass-门禁与逐项证据)；cycle-0077 的真实平面 Face thicken 已支持，见 §6.5。

## 10.2 最短距离

```cpp
// body_a/body_b 必须为支持的真实 ExactBRep 多面体。
auto nearest = kernel.query().closest_points(body_a, body_b);
if (!nearest.value) {
  handle_error(nearest);  // 诊断含 query.distance.support_gate/preflight/budget/numeric
  return;
}
print(nearest.value->distance, nearest.value->first_point, nearest.value->second_point);
// distance 为模型长度单位，等于见证点欧氏距离；相交/包含/相切为 0。
// 正距离时两侧均有真实 FaceId/ShellId；内部见证归属句柄可为零。
auto dist = kernel.query().min_distance(body_a, body_b);  // 同一主链，使用默认预算
```

点到体的最近位置采用最近边界语义，返回 `BodyPointQuery`：

```cpp
auto wedge = kernel.primitives().wedge({0,0,0}, 1,1,1);
if (!wedge.value) {
  handle_error(wedge);
  return;
}
auto point_result = kernel.query().closest_point(*wedge.value, {1,1,0.5});
if (!point_result.value) {
  handle_error(point_result);
  return;
}
if (point_result.value->nearest_boundary) {
  const auto& boundary = *point_result.value->nearest_boundary;
  print(boundary.point, boundary.distance, boundary.face, boundary.shell);
  // 实际边界见证 (0.5,0.5,0.5)，距离 1/sqrt(2)，单位为模型长度单位。
}
```

## 10.3 截面

```cpp
auto unit_box = kernel.primitives().box({0,0,0}, 1,1,1);
if (!unit_box.value) {
  handle_error(unit_box);
  return;
}
Plane section_plane{.origin = {1,0,0}, .normal = {1,1,1}};  // x+y+z=1
BodySpatialQueryOptions options;
options.max_triangle_tests = 1000000;  // 前置检查之后工作；0 非法
// 详细查询只读，不发布 MeshId，不写缓存/Eval/事务。
auto detailed = kernel.query().section_detailed(*unit_box.value, section_plane, options);
if (!detailed.value) {
  handle_error(detailed);
  return;
}
print(detailed.value->area);  // sqrt(3)/2，模型长度单位平方
// vertices 世界坐标；triangles 为索引；boundary_segments/contact_points 保留接触。
auto dedicated = kernel.topology().query().section(*unit_box.value, section_plane, options);

// 兼容入口：成功有面积时显式发布一个真实结果网格，不填三角化缓存。
auto sec = kernel.query().section(*unit_box.value, section_plane);
if (!sec.value) {
  handle_error(sec);
} else if (sec.value->value != 0) {
  auto count = kernel.convert().mesh_triangle_count(*sec.value);
}
auto empty = kernel.query().section(*unit_box.value, {{0,0,2}, {0,0,1}});
// empty 成功且有 value，但 *empty.value == MeshId{}；不创建/消费网格。
auto edge_contact = kernel.query().section_detailed(*unit_box.value, {{0,0,0}, {1,1,0}});
// 成功时 area=0、无面积三角形，boundary_segments 保留真实线接触。
// 此时 bbox 可有效；area=0 或兼容 MeshId{} 不能区分无交集与接触。
```

通用 `closest_point(body, point, options)` 返回与 `TopologyQueryService::locate_point` 相同的 `BodyPointQuery`，即使点在材料内部也返回最近边界距离。只支持 ExactBRep box/wedge、已物化真实 Sweep（含 cycle-0077 平面 Face thicken）与用户 Generic 平面直边嵌入闭壳（凹/孔/空腔/材料岛）；旋转/曲线扫掠返回采样多面体结果。解析曲面体和旧占位 thicken 为 `NotImplemented / AXM-CORE-E-0004 / query.*.support_gate`，不生成 bbox 伪截面/距离。无交集、纯线/点相切是成功，兼容入口返回零网格句柄；接触细节使用详细入口。

真实多面体 `mass_properties` 与专用体质量共用当前拓扑；编辑支撑面或删面失败不会恢复旧 Sweep 质量。预算耗尽及数值不可分辨失败无部分值，位置容差不吸附近邻平面、不抹去可表示正间隙。完整合同、限制与本批门禁见 [Stage 3 支持矩阵](AxiomKernel_详细模块接口清单.md#612-stage-3-截面最近点与距离支持矩阵cycle-0075--s3-query)。

## 11. 导入导出样例

## 11.1 导入 `STEP`

```cpp
ImportOptions opts;
opts.run_validation = true;
opts.auto_repair = false;

auto imported = kernel.io().import_step("/data/part.step", opts);
if (imported.status != StatusCode::Ok) {
  handle_error(imported);
  return;
}
```

## 11.2 导出 `STEP`

```cpp
ExportOptions opts;
opts.compatibility_mode = false;
opts.embed_metadata = true;

auto exported = kernel.io().export_step(body_id, "/data/out.step", opts);
```

### 11.2.1 S5-IO 四格式受限往返与失败处理

`part.step` 必须是 Axiom 元数据子集；标准 STEP 实体仍返回 NotImplemented。IGES/BREP 同样只支持各自 Axiom 子集，STL 为实际三角网格；本例坐标保留模型单位，不执行标准单位转换。以下为调用片段，未单独编译；验证依据是调度器运行的固定数据及 workflow/representation 回归，见 [验收 §1.12](../quality/AxiomKernel_测试与验收方案.md#112-cycle-0085--s5-io-门禁与逐项证据)。

```cpp
ImportOptions import_opts;
import_opts.run_validation = true;
import_opts.auto_repair = true;
import_opts.repair_mode = RepairMode::Safe;
ExportOptions export_opts;
export_opts.embed_metadata = true;
export_opts.compatibility_mode = false;
// 对 step/iges/brep/stl 分别提供对应输入与输出路径。
auto imported = kernel.io().import_auto(input_path, import_opts);
if (imported.status != StatusCode::Ok || !imported.value) {
  handle_error(imported); // io.import.* / io.post_import.*；无本次模型残留。
  return;
}
auto exported = kernel.io().export_auto(*imported.value, output_path, export_opts);
if (exported.status != StatusCode::Ok) {
  auto report = kernel.diagnostics().get(exported.diagnostic_id);
  handle_query_error(report); // open/write/sidecar/publish；原主文件受保护。
  return;
}
auto reimported = kernel.io().import_auto(output_path, import_opts);
if (reimported.status != StatusCode::Ok || !reimported.value) {
  handle_error(reimported);
  return;
}
auto valid = kernel.validate().validate_all(*reimported.value, ValidationMode::Standard);
use_if_valid(valid);
```

四格式读取预算为 64 MiB，超限在 `.read` 失败，STEP 严格容器/字段与 STL 闭合/非有限/溢出拒绝均可由 diagnostic_id 检索。固定三元数据文件证明参数/坐标精确 double 往返及零 owned shells；固定 STL 实际积分 V=4（非 bbox 的24）、面积/质心误差≤1e-12，详见 API §11.1.1。导出文本使用 classic locale/max_digits10，但 glTF float32 等格式能力不因此扩大。四网格格式开启 `write_mesh_validation_report` 时先成功写侧车再发布主文件；侧车可能保留，侧车/主文件及全批不承诺跨文件事务、掉电持久性或并发目录修改安全。发布失败有 `.publish` 合同，但该失败分支未单独注入回归。

### 11.2.2 S5-EXIT 显式 STL 修复、三角化与往返

以下调用片段未单独编译；固定 `s5_io_precision_tetra.stl` 的实际验收来自[本批§1.14](../quality/AxiomKernel_测试与验收方案.md#114-cycle-0087--s5-exit-门禁与逐项证据)，四必需回归随完整16/16、196.58 s通过。须分别检查外层Result和OpReport状态。

```cpp
ImportOptions opts;
opts.run_validation = true;
auto source = kernel.io().import_stl(input_path, opts);
if (source.status != StatusCode::Ok || !source.value) {
  handle_error(source);
  return;
}
auto before = kernel.validate().validate_all(*source.value, ValidationMode::Strict);
if (before.status != StatusCode::Ok) {
  handle_error(before);
  return;
}
auto repaired = kernel.repair().auto_repair(*source.value, RepairMode::Safe);
if (repaired.status != StatusCode::Ok || !repaired.value ||
    repaired.value->status != StatusCode::Ok) {
  auto report = kernel.diagnostics().get(repaired.diagnostic_id);
  handle_query_error(report);
  return;
}
BodyId output = repaired.value->output;
// MeshRep 内部后验是 Standard；这里额外显式检查 Strict。
auto strict = kernel.validate().validate_all(output, ValidationMode::Strict);
if (strict.status != StatusCode::Ok) {
  handle_error(strict);
  return;
}
auto mesh = kernel.convert().brep_to_mesh(output, {});
if (mesh.status != StatusCode::Ok || !mesh.value) {
  handle_error(mesh);
  return;
}
auto exported = kernel.io().export_stl(output, output_path, {});
if (exported.status != StatusCode::Ok) {
  handle_error(exported);
  return;
}
auto reimported = kernel.io().import_stl(output_path, opts);
if (reimported.status != StatusCode::Ok || !reimported.value) {
  handle_error(reimported);
  return;
}
auto final_valid = kernel.validate().validate_all(*reimported.value, ValidationMode::Strict);
use_if_valid(final_valid);
```

Safe派生新BodyId/新MeshId完整实际网格快照，源MeshId及坐标保留；该合同仅适用auto_repair，不泛化所有修复入口。固定源/派生/再导入均4三角、零owned shells；独立ASCII解析积分V=4、A=13+sqrt(244)/2、C=固定原点+(0.5,0.75,1)，误差≤1e-12，bbox体积24不是分析参考。Strict及固定积分不证明任意mesh流形/自交或实体质量服务资格。

STEP/IGES/BREP须按[API§11.1.2](AxiomKernel_详细模块接口清单.md#1112-stage-5-集成退出支持矩阵cycle-0087--s5-exit)区分：原Box元数据Standard/ReportOnly可通过但零owned shells，直接三角化 `rep.tessellation.topology` 拒绝、质量 `query.mass_properties.empty_gate` 拒绝；显式Safe合成Modified bbox边界可owned_topo_welded显示，导出再导入仍零壳/bbox_proxy，两者质量support_gate拒绝。不能把上述STL片段的成功预期直接套用到原metadata或标准实体。

缺mesh与复制后angular=0修复失败无value，`heal.auto_repair.post_validate` 保留HEAL-E-0006及VAL-E-0004/0003根因；回收派生体/mesh、恢复缓存统计/Eval、源快照保持。独立Heal允许ID空档，IO导入外层另恢复next_id。模型单位/64 MiB、float32限制、单主文件发布/侧车及批量非事务、无直接publish注入合同沿用§11.2.1；本例不是标准BRep交换或通用分析认证。

### 精确 B-Rep 文本子集的失败诊断

```cpp
ImportOptions exact_opts;
exact_opts.run_validation = false;

auto imported = kernel.io().import_axmjson("/data/part.axmjson", exact_opts);
// import_iges 和 import_brep 具有相同的物化前失败合同。
if (!imported.value) {
    auto report = kernel.diagnostics().get(imported.diagnostic_id);
    // 可按 io.import.axmjson.input/path/open/read/parse/validation 检索根因。
    auto same_stage = kernel.diagnostics().find_by_issue_stage_prefix(
        "io.import.axmjson.", 32);
    handle_query_error(report);
}
```

STEP/STL（cycle-0085）及 AXMJSON、Axiom IGES 元数据子集与 Axiom BREP JSON 子集均限 64 MiB，且在分配 `BodyId` 前完成文件、结构、格式、有限数值、包围盒和轴校验。失败不写 Body/Mesh store，修复原文件后可原位重试。AXMJSON 兼容早期仅身份与 bbox 字段的文件，但扩展几何字段一旦出现就必须成组完整。标准 IGES DE 实体仍返回 `NotImplemented`，不会被当成 Axiom 子集物化。

## 11.3 导入后修复

```cpp
ImportOptions repair_opts;
repair_opts.run_validation = true;
repair_opts.auto_repair = true;
repair_opts.repair_mode = RepairMode::Safe;
auto imported = kernel.io().import_step(path, repair_opts);
if (imported.status != StatusCode::Ok || !imported.value) {
  // io.post_import.validation/repair/post_validate；本次模型/cache/Eval/next_id 已回滚。
  handle_error(imported);
  return;
}
// 返回原验证合格体或已修复且再验证合格的输出。
auto strict = kernel.validate().validate_all(*imported.value, ValidationMode::Strict);
use_if_valid(strict);
```

若需保留缺陷体作人工预检，应显式 `run_validation=false` 后调用 `auto_repair(body, ReportOnly/SuggestOnly)`。观察返回原体，外层 Result 可 Ok，但 `OpReport::status` 是实际 Standard 预检状态；模型/Eval/cache 不改，诊断可增加。修改型 `auto_repair` 的新真实规则只在 Standard 失败时进入，限至少六唯一面的平面直边单壳外环，linear 必须位于配置 min_local/max_local；孔洞/曲面/多壳/代理面明确拒绝。Strict 后验成功输出 Generic + ExactBRep，有限 PCurve 支持公开 UV/质量/截面。默认 STEP/IGES/BREP 仍是 Axiom 元数据子集，样例不代表标准全实体交换。

### 批量导入失败、后验诊断与原位重试

```cpp
ImportOptions batch_opts;
batch_opts.run_validation = true;
batch_opts.auto_repair = true;
std::vector<std::string> paths = {"/data/part.axmjson", "/data/missing.axmjson"};
auto imported_batch = kernel.io().import_many_axmjson(paths, batch_opts);
if (!imported_batch.value) {
  auto report = kernel.diagnostics().get(imported_batch.diagnostic_id);
  // io.batch_import 记录从零开始的失败项索引、失败前完成数量和路径字节长度。
  // 本批新分配的模型/网格/拓扑/几何及 next_id 已恢复，可修复输入后原位重试。
  handle_query_error(report);
} else {
  for (BodyId body : *imported_batch.value) {
    auto valid = kernel.validate().validate_all(body, ValidationMode::Standard);
    use_if_valid(valid);
  }
}
```

`import_many_step` 和 `import_many_auto` 具有相同模型存储原子性；单项实际失败触发整批回滚，诊断证据仍保留。八个具体格式入口的后验验证、自动修复及修复后复验 Error/Fatal 阶段映射为 `io.post_import.validation/repair/post_validate`，保留有限数值证据且不改源 HEAL 报告。`run_validation=true` 的未修复验证失败或修复/再验证失败返回失败且无 value，单项也回滚本次模型/缓存统计/Eval/next_id；ReportOnly/SuggestOnly 不升级为修改策略。`false` 显式跳过闭环，不承诺有效。批量导出不回滚已成功前项或已写侧车；cycle-0085 八主格式的失败项主文件受单文件发布保护，普通文本/目录辅助接口尚未纳入本批证据门禁。

### HEAL 修复失败与批量原子性

```cpp
std::vector<BodyId> inputs = {body_id, BodyId{0}};
auto repaired_batch = kernel.repair().repair_many_remove_small_faces(
    inputs, 0.01, RepairMode::Aggressive);
if (!repaired_batch.value) {
  auto report = kernel.diagnostics().get(repaired_batch.diagnostic_id);
  // 后项失败时，前项派生对象和 Eval 失效状态也回滚。
  handle_query_error(report);
}
```

单项修改型修复后验失败会回收本次物化对象；`repair_many_auto/remove_small_edges/remove_small_faces/merge_near_coplanar_faces` 均不泄漏半成功结果。`repair_face_trim_pcurves(face_id, RepairMode::Safe)` 支持 Plane/Cylinder/Sphere，重建或后验失败恢复原 coedge PCurve 绑定并回收新增 PCurve。失败报告使用 `heal.*` 阶段、实体与有限数值证据，不扩大现有修复规则或曲面支持范围。HEAL 回滚不恢复 `next_id`，重试不保证复用被回收对象的 ID；`repair_many_auto` 返回报告复制失败子项 issue 并保留其非空阶段及批量 rollback 上下文；其余三个批量入口仍在原诊断保留子项根因。

## 12. 三角化样例

## 12.1 实体转网格

```cpp
TessellationOptions tess;
tess.chordal_error = 0.05;
tess.angular_error = 5.0;
tess.compute_normals = true;

auto mesh = kernel.convert().brep_to_mesh(body_id, tess);
```

## 12.2 当前 owned 边界、提交与失败回滚（cycle-0078 / S3-CONSISTENCY）

以下片段接续一个已成功建模的平面直边实体 `body_id` 和 §12.1 的 `tess`；完整独立闭环夹具见 §12.3。所有调用先检查 Result 再使用句柄。等价 Plane 替换仍改变支撑身份，提交后必须使用新当前网格；之后失败回滚恢复的是已提交支撑和网格。

```cpp
auto& topo = kernel.topology().query();
auto& eval = kernel.eval_graph();
auto faces = topo.faces_of_body(body_id);
auto baseline = kernel.convert().brep_to_mesh(body_id, tess);
if (!faces.value || faces.value->empty() || !baseline.value) return;
const auto face = faces.value->front();
auto support = topo.surface_of_face(face);
if (!support.value) return;
auto sample = kernel.surface_service().eval(*support.value, 0, 0, 1);
if (!sample.value) return;
auto equivalent = kernel.surfaces().make_plane(sample.value->point, sample.value->normal);
auto bound = eval.register_node(NodeKind::Analysis, "body:" + std::to_string(body_id.value));
auto consumer = eval.register_node(NodeKind::Analysis, "consistency:query_rep");
if (!equivalent.value || !bound.value || !consumer.value) return;
if (eval.add_dependency(*consumer.value, *bound.value).status != StatusCode::Ok) return;

MeshId committed_mesh{};
{
    auto txn = kernel.topology().begin_transaction();
    if (txn.replace_surface(face, *equivalent.value).status != StatusCode::Ok) return;
    const std::array dirty_faces{face};
    auto local = kernel.convert().brep_to_mesh_local(body_id, dirty_faces, tess);
    auto full = kernel.convert().brep_to_mesh(body_id, tess);
    if (!local.value || !full.value || *full.value == *baseline.value) return;
    if (kernel.validate().validate_all(body_id, ValidationMode::Strict).status != StatusCode::Ok) return;
    if (txn.commit().status != StatusCode::Ok) return;
    committed_mesh = *full.value;
}
if (eval.recompute(*consumer.value).status != StatusCode::Ok) return;

auto displaced = kernel.surfaces().make_plane({0,0,100}, {0,0,1});
if (!displaced.value) return;
auto txn = kernel.topology().begin_transaction();
if (txn.replace_surface(face, *displaced.value).status != StatusCode::Ok) return;
auto rejected = kernel.convert().brep_to_mesh(body_id, tess);
if (rejected.status == StatusCode::Ok || rejected.value) return;
auto diagnostic = kernel.diagnostics().get(rejected.diagnostic_id);
// 当前支撑错配：AXM-TES-E-0001 / rep.tessellation.face；
// 日志或 UI 读取 diagnostic.value->issues 中的 stage、related_entities。
if (!diagnostic.value) return;
if (eval.recompute(*consumer.value).status != StatusCode::Ok) return;
if (txn.rollback().status != StatusCode::Ok) return;
auto restored = kernel.convert().brep_to_mesh(body_id, tess);
auto restored_support = topo.surface_of_face(face);
auto dirty_after_restore = eval.is_invalid(*consumer.value);
if (!restored.value || *restored.value != committed_mesh ||
    restored_support.value != equivalent.value ||
    !dirty_after_restore.value || !*dirty_after_restore.value) return;
```

旧 MeshId 是不可变快照；存活体可以保留历史缓存，只有当前边界键且 `source_body` 正确才命中。`mesh_to_brep` 首次把网格 source_body 绑定到新 MeshRep 体，原体须重新生成网格；cycle-0086 起对本入口已建立的 `MeshRep + brep_from_mesh` 关联重复转换返回同一 BodyId。两向 round-trip 的临时转换在成功及失败后均恢复原绑定/缓存/统计/分配状态。owned 失败不发布部分网格/缓存，也不回退 bbox/创建参数；编辑 native primitive 撤销创建参数资格，回滚恢复。Eval recompute 仅管理图状态，实际查询/表示仍需显式调用；恢复后消费者再次 dirty，不保证自动重算。移除体在恢复或提交后清理关联网格/缓存/体绑定，源体及共享源壳保留。

### 12.2.1 Stage 5 批量、往返及幂等转换（cycle-0086）

以下接续成功建模的 `body_id` 与 `tess`；参数曲面须满足 [API §7.3.2](AxiomKernel_详细模块接口清单.md#732-stage-5-真实边界三角化与转换一致性cycle-0086--s5-tessellation) 的真实矩形边界、四极点双线性/一阶等权归一化夹持节点或 LineSegment Swept 合同。一般曲边、高阶、非矩形/孔曲面裁剪及 Offset/Revolved 拒绝；关闭可选细化不关闭边界与必需误差检查。

```cpp
auto mesh = kernel.convert().brep_to_mesh(body_id, tess);
if (!mesh.value) return;
auto forward = kernel.convert().verify_brep_mesh_round_trip(body_id, tess);
auto reverse = kernel.convert().verify_mesh_brep_round_trip(*mesh.value, tess);
if (!forward.value || !reverse.value || !forward.value->passed || !reverse.value->passed) return;
// verify 的临时状态已恢复，此处仍持有原网格及原绑定。
auto converted = kernel.convert().mesh_to_brep(*mesh.value);
auto repeated = kernel.convert().mesh_to_brep(*mesh.value);
if (!converted.value || converted.value != repeated.value) return;
const std::array meshes{*mesh.value, *mesh.value};
auto bodies = kernel.convert().mesh_to_brep_batch(meshes);
if (!bodies.value || bodies.value->size() != 2 ||
    (*bodies.value)[0] != *converted.value || (*bodies.value)[1] != *converted.value) return;
const std::array inputs{body_id, BodyId{}};
auto rejected_batch = kernel.convert().brep_to_mesh_batch(inputs, tess);
if (rejected_batch.status == StatusCode::Ok || rejected_batch.value) return;
auto diagnostic = kernel.diagnostics().get(rejected_batch.diagnostic_id);
if (!diagnostic.value) return;
// 后项失败恢复前项产生的 mesh/body/cache/统计/next_id，诊断可继续查询。
```

缺嵌入网格的 MeshRep 返回 `AXM-TES-E-0001 / rep.tessellation.support`，不生成 bbox 替代；`mesh_to_brep` 的 bbox 来自实际顶点，但不生成 owned ExactBRep，也不使开放曲面获得质量查询资格。metadata/implicit 显示代理不认证物理量；round-trip 报告还须结合独立边界/面积/体积参考。OBJ 不导出 vertex normals，本批核对三角 cross 与解析/Geo法向。此文档片段未单独编译；固定回归随调度器最终16/16（200.26 s）通过，两条证据见 [验收 §1.13](../quality/AxiomKernel_测试与验收方案.md#113-cycle-0086--s5-tessellation-门禁与逐项证据)，正式文档门禁及提交尚未记录。

## 12.3 基础零件建模到查询与表示闭环

```cpp
ProfileRef profile{"consistency_rectangle",
    {{0,0,0}, {2,0,0}, {2,3,0}, {0,3,0}}};
auto body = kernel.sweeps().extrude(profile, {0,0,1}, 4);
auto remote = kernel.primitives().box({50,50,50}, 1, 1, 1);
if (!body.value || !remote.value) return;
const auto body_id = *body.value;
if (kernel.validate().validate_all(body_id, ValidationMode::Strict).status != StatusCode::Ok) return;
auto mass = kernel.query().mass_properties(body_id);
auto boundary_mass = kernel.topology().query().body_mass_properties(body_id);
auto section = kernel.query().section_detailed(body_id, {{0,0,2}, {0,0,1}});
auto nearest = kernel.query().closest_point(body_id, {60,60,2});
auto pair = kernel.topology().query().closest_points(body_id, *remote.value);
auto sources = kernel.topology().query().source_faces_of_body(body_id);
auto mesh = kernel.convert().brep_to_mesh(body_id, TessellationOptions{});
if (!mass.value || !boundary_mass.value || !section.value || !nearest.value ||
    !nearest.value->nearest_boundary || !pair.value || !sources.value || !mesh.value) return;
// 独立参考：V=24，A=52，C=(1,1.5,2)，水平截面积=6；
// 最近边界=(2,3,2)，远端盒双侧见证=(2,3,4)/(50,50,50)，
// 体间距离=sqrt(48²+47²+46²)，外点到最近边界距离=sqrt(58²+57²)。
// 来源句柄可通过 has_face 核对，见证 FaceId/ShellId 应属于各自实体；
// inspect_mesh 可核对三角形/连通分量及非法索引，不能只比较 bbox。
```

五类路径及凹形/孔 OBJ 三角形独立积分均已随调度器全量 **16/16、164.60 s** 执行通过；上述片段是同一公开调用合同的示例，本轮未单独编译片段。revolve 查询对应采样弦面、thicken 为真实平面 Face 单侧正厚度；原生解析体实体截面/距离仍拒绝，metadata 显示代理和 Rep bbox 辅助分类/距离不能替代此闭环。逐项文件/断言和参考见 [验收 §1.5](../quality/AxiomKernel_测试与验收方案.md#15-cycle-0078--s3-consistency-门禁与逐项证据)，API 合同见 [§7.3.1](AxiomKernel_详细模块接口清单.md#731-stage-3-表示来源与-eval-一致性合同cycle-0078--s3-consistency)。

## 13. 事务与版本样例

## 13.1 显式事务

```cpp
auto txn = kernel.topology().begin_transaction();

auto v0 = txn.create_vertex({0,0,0});
auto v1 = txn.create_vertex({10,0,0});

if (v0.status != StatusCode::Ok || v1.status != StatusCode::Ok) {
  txn.rollback();
  return;
}

auto version = txn.commit();
```

## 13.2 失败回滚

```cpp
auto txn = kernel.topology().begin_transaction();
auto r = txn.create_face(surface_id, bad_loop, {});

if (r.status != StatusCode::Ok) {
  txn.rollback();
  log("rollback due to invalid topology");
}
```

## 13.3 协作式取消

```cpp
TopologyCancellationSource cancellation;
auto txn = kernel.topology().begin_transaction(cancellation.token());

auto v0 = txn.create_vertex({0, 0, 0});
cancellation.request_cancellation();

// 也可不显式轮询：下一次写入、commit、rollback 或作用域退出会观察取消。
auto cancelled = txn.poll_cancellation();
if (cancelled.status == StatusCode::OperationFailed) {
  auto observed = txn.cancellation_observed();
  auto reverted_writes = txn.cancelled_write_operation_count();
  auto metrics = kernel.topology().cancellation_metrics();
  // 已有写入已从完整快照恢复，写者槽已释放，版本与成功提交审计不推进。
}
```

取消只在 API 边界协作式观察，不抢占单个正在执行的拓扑调用。预取消事务不会取得写者槽；移动事务唯一转移取消权限；被单写者规则拒绝的重叠事务不能借取消影响所有者。

## 13.4 嵌套保存点

```cpp
auto txn = kernel.topology().begin_transaction();
auto outer = txn.create_savepoint();
if (!outer.value) { handle_error(outer); return; }

auto tentative_face = txn.create_face(surface_id, candidate_outer, candidate_holes);
if (!tentative_face.value) {
  auto restored = txn.rollback_to_savepoint(*outer.value);
  if (restored.status != StatusCode::Ok) { handle_error(restored); return; }
  // outer 仍有效，可修正输入后重试，或再次回滚到同一点。
}

auto inner = txn.create_savepoint();
if (!inner.value) { handle_error(inner); return; }
// ... 执行另一个受限阶段 ...
auto released = txn.release_savepoint(*inner.value); // 只能 LIFO 释放最内层点
if (released.status != StatusCode::Ok) { handle_error(released); return; }

auto version = txn.commit();
auto audit = kernel.topology().savepoint_metrics();
```

`rollback_to_savepoint` 恢复拓扑主存储、完整撤销基线和写审计，保留目标供重复回滚，但会使目标之后的所有内层保存点失效。无效、跨事务、已失效或非栈顶释放句柄返回 `OperationFailed / AXM-TX-E-0003` 且不改模型。保存点随移动事务转移所有权；取消已请求时，整事务恢复和 `AXM-TX-E-0007` 优先，不只回滚局部保存点。当前实现使用内存全拓扑快照，大模型需评估内存成本。

## 13.5 创建带显式参数区间的曲边

```cpp
const double pi = std::acos(-1.0);
auto circle = kernel.curves().make_circle({0, 0, 0}, {0, 0, 1}, 5.0);
auto p0 = kernel.curve_service().point_at_parameter(*circle.value, 0.0);
auto p1 = kernel.curve_service().point_at_parameter(*circle.value, pi / 2.0);

auto txn = kernel.topology().begin_transaction();
auto v0 = txn.create_vertex(*p0.value);
auto v1 = txn.create_vertex(*p1.value);
auto arc = txn.create_trimmed_edge(*circle.value, 0.0, pi / 2.0,
                                   *v0.value, *v1.value);

auto interval = kernel.topology().query().edge_curve_interval(*arc.value);
auto length = kernel.topology().query().edge_length(*arc.value); // 5*pi/2
```

参数端点与拓扑顶点须在内核线性容差内一致，且参数位于曲线定义域；创建失败不会分配 EdgeId 或增加事务写计数。旧 `create_edge` 创建的曲边没有显式区间，长度查询仍结构化拒绝，不使用弦长近似。

## 13.6 建面前检查跨环边界冲突

```cpp
auto conflict = kernel.topology().validate().first_boundary_conflict(
    outer_loop, inner_loops);  // tolerance=0 使用内核线性容差

if (!conflict.value) {
  handle_error(conflict);
  return;
}
if (conflict.value->has_value()) {
  const auto& evidence = conflict.value->value();
  print(evidence.first_loop.value, evidence.second_loop.value,
        evidence.first_edge.value, evidence.second_edge.value,
        evidence.distance, evidence.error_controlled,
        evidence.solver_tolerance, evidence.curve_evaluations,
        evidence.parameter_rectangles_processed);
  return;  // 不进入 create_face
}
```

Line/LineSegment、CompositePolyline 和线性 CompositeChain 使用解析分段谓词；带显式 trim 区间的圆锥曲线、Bezier、BSpline、NURBS 与混合 CompositeChain 使用无缓存、受预算和误差约束的 Geo 求交流程。返回可区分内部相交、真实拓扑边端点接触、线性共线重叠、曲线连续重合和容差邻近；真曲线证据的 `error_controlled` 为 true。缺失必要 trim、曲线损坏或预算/数值失败会以 `AXM-TOPO-E-0030` 失败且不返回部分冲突。一般高阶异参连续重合仍不作完备证明，近接能力受统一求交预算约束。

## 14. 诊断与错误处理样例

## 14.1 打印主错误和警告

```cpp
void handle_error(const GenericResult& result) {
  print("status", result.status);

  if (result.diagnostic_id.value != 0) {
    auto diag = kernel.diagnostics().get(result.diagnostic_id);
    if (diag.status == StatusCode::Ok && diag.value.has_value()) {
      print(diag.value->summary);
      for (const auto& issue : diag.value->issues) {
        print(issue.code, issue.message);
      }
    }
  }

  for (const auto& w : result.warnings) {
    print("warning", w.code, w.message);
  }
}
```

## 14.2 用户友好错误转换

```cpp
std::string to_user_message(const Issue& issue) {
  if (issue.code == "AXM-BOOL-E-0005") {
    return "布尔计算失败：局部区域无法稳定分类，请尝试简化模型或启用修复模式。";
  }
  if (issue.code == "AXM-BLEND-E-0002") {
    return "圆角失败：当前半径过大，请减小参数。";
  }
  return "操作失败，请查看详细诊断。";
}
```

## 14.3 审计重量级失败的结构化证据

```cpp
DiagnosticEvidencePolicy policy;
policy.issue_code_prefix = "AXM-BOOL-E-";
policy.stage_prefix = "bool.";
policy.minimum_severity = IssueSeverity::Error;
policy.require_stage = true;
policy.require_related_entities = true;
policy.require_numeric_evidence = true;
policy.max_findings = 256;

auto audit = kernel.diagnostics().audit_evidence(diagnostic_ids, policy);
if (!audit.value || !audit.value->passed()) {
  kernel.diagnostics().export_evidence_audit_json(
      diagnostic_ids, policy, "bool-evidence-audit.json");
  return;
}
```

重复 `DiagnosticId` 只审计一次；未匹配到目标 issue 的报告也会使门禁失败。`max_findings` 只截断明细，遗漏数由 `omitted_findings` 记录，统计总数保持完整。BOOL 受支持失败分支和 cycle-0073 的 HEAL/IO 重量级包均已接入模块门禁。对 HEAL 与 IO 应使用 `issue_code_prefix="AXM-"`，因为根因也会复用 CORE/VAL/TOPO 等码，并分别设置 `stage_prefix="heal."` 或 `"io."`：

```cpp
policy.issue_code_prefix = "AXM-";
policy.stage_prefix = "heal.";
auto heal_audit = kernel.diagnostics().audit_evidence(heal_failure_ids, policy);
policy.stage_prefix = "io.";
auto io_audit = kernel.diagnostics().audit_evidence(io_failure_ids, policy);
```

`heal_failure_ids/io_failure_ids` 分别收集目标工作流的失败诊断 ID。IO 预物化失败用 `[0]` 明确表示尚无内核实体，不能把令牌当成有效句柄。证据至少含有限的 `status_code/related_entity_count`，按分支另附阈值、模式、计数或路径长度；非有限测量值过滤后以 `non_finite_evidence_omitted` 计数。审计检查结构完整性，不证明算法正确或格式工业完备；普通文本/目录辅助接口仍在本批范围外。

## 15. Python绑定样例

以下示例展示建议的脚本接口风格。

## 15.1 创建并导出模型

```python
from axiom import Kernel, BooleanOp

k = Kernel()

box = k.primitives.box((0, 0, 0), 100, 80, 30)
cyl = k.primitives.cylinder((20, 20, 0), (0, 0, 1), 10, 30)

res = k.booleans.run(BooleanOp.Subtract, box, cyl, diagnostics=True, auto_repair=True)
if not res.ok:
    print(res.primary_error)
    print(k.diagnostics.get(res.diagnostic_id))
    raise SystemExit(1)

k.io.export_step(res.output, "part.step")
```

## 15.2 导入并修复

```python
body = k.io.import_step("dirty.step", run_validation=True)
report = k.validate.validate_all(body)
if not report.ok:
    repaired = k.repair.auto_repair(body, mode="safe")
    body = repaired.output
```

## 16. 测试调用样例

## 16.1 断言布尔结果合法

```cpp
TEST(Boolean, subtract_box_cylinder_should_be_valid) {
  Kernel kernel = make_test_kernel();

  auto box = must(kernel.primitives().box({0,0,0}, 100, 80, 30));
  auto cyl = must(kernel.primitives().cylinder({20,20,0}, {0,0,1}, 10, 30));

  auto res = kernel.booleans().run(BooleanOp::Subtract, box, cyl, default_bool_options());
  ASSERT_EQ(res.status, StatusCode::Ok);
  ASSERT_TRUE(res.value.has_value());

  auto valid = kernel.validate().validate_all(res.value->output, ValidationMode::Strict);
  ASSERT_EQ(valid.status, StatusCode::Ok);
}
```

## 16.2 断言错误码稳定

```cpp
TEST(Blend, too_large_radius_should_return_stable_error_code) {
  auto res = kernel.blends().fillet_edges(body_id, edges, 10000.0);
  ASSERT_EQ(res.status, StatusCode::OperationFailed);

  auto diag = must(kernel.diagnostics().get(res.diagnostic_id));
  ASSERT_TRUE(has_issue_code(diag, "AXM-BLEND-E-0002"));
}
```

## 17. 推荐封装模式

### 17.1 上层 CAD 封装

建议封装一层应用服务：

- `CreateBodyUseCase`
- `BooleanFeatureUseCase`
- `RepairImportedModelUseCase`
- `ExportUseCase`

原因：

- 隔离 UI 与内核
- 集中处理诊断与日志
- 更利于事务和撤销重做集成

### 17.2 不推荐的调用方式

- UI 直接深入调用底层事务对象
- 多处手写错误码映射
- 直接绕过验证器使用输出结果
- 导入后不做任何验证就进入后续建模链路

## 18. 结论

这份样例集的目的不是替代正式 API 文档，而是给团队一个统一的“调用姿势参考”。只要上层调用遵循这里的模式，后续即使接口名略有调整，整体使用方式也不会偏离太多。

后续如果继续补充，建议增加：

1. `docs/api/AxiomKernel_REST与远程调用样例集.md`
2. `docs/api/AxiomKernel_插件开发样例集.md`


### 曲线与边界长度查询（FR-QUERY-001，第 63/67 功能包）

```cpp
auto segment = kernel.curves().make_line_segment({0, 0, 0}, {3, 4, 0});
if (segment.value) {
    auto full = kernel.curve_service().length(*segment.value);       // 5 模型长度单位
    auto part = kernel.curve_service().length(*segment.value, .8, .2); // 3，方向无关
}
auto ellipse = kernel.curves().make_ellipse({0, 0, 0}, {3, 0, 0}, {0, 2, 0});
if (ellipse.value) {
    axiom::CurveLengthOptions options;
    options.absolute_tolerance = 1e-10; // 模型长度单位
    options.relative_tolerance = 1e-11;
    options.max_evaluations = 200000;
    auto quarter = kernel.curve_service().length(*ellipse.value, 0, std::acos(-1.0) / 2, options);
    // 数值路径成功时返回弧长；预算或精度失败时无 value，并返回 AXM-GEO-E-0011。
}
auto box = kernel.primitives().box({0, 0, 0}, 2, 3, 4);
if (box.value) {
    auto faces = kernel.topology().query().faces_of_body(*box.value);
    if (faces.value) {
        for (auto face : *faces.value) {
            auto boundary = kernel.topology().query().face_boundary_length(face);
            // 成功值为 10、12、14 各两次；含孔面会加上每个孔的周长。
            // 失败时无 value，可用 diagnostic_id 检索原因。
        }
    }
}
```

曲线长度对直线有限区间、线段、圆和折线使用解析计算，对椭圆、抛物线、双曲线、Bezier、BSpline 和 NURBS 使用可配置自适应积分。复合链沿用 eval 的子曲线局部 `[0,1]`，不自动取子曲线全域。显式通过 `create_trimmed_edge` 保存区间的曲边使用真实区间弧长；旧 `create_edge` 创建且未携带区间的曲边仍返回不支持，不以弦长冒充曲边弧长。

### 解析曲面 PCurve 修剪面积

```cpp
// analytic_face 的每条 coedge 已绑定连续闭合的折线 PCurve。
auto area = kernel.topology().query().face_area(analytic_face);
if (!area.value) {
    auto diagnostic = kernel.diagnostics().get(area.diagnostic_id);
    handle_query_error(diagnostic);
}
```

`face_area` 返回模型长度单位的平方，支持 Plane/Cylinder/Cone/Sphere/Torus 及嵌套 Trimmed/Offset，外环减去全部内环。未包装 Plane 没有 PCurve 时兼容回退 `planar_face_area`。所有其他支持曲面都要求完整折线 PCurve；UV 端点必须与定向拓扑顶点的 3D 位置一致。周期缝须显式用同一展开区间表达；Bezier/BSpline/NURBS/Revolved/Swept 面暂不支持。查询从当前拓扑和曲面重算，不写求值或网格缓存。

### 闭合多面体拓扑质量属性

```cpp
auto box = kernel.primitives().box({10, 20, 30}, 2, 3, 4);
if (box.value) {
    auto shells = kernel.topology().query().shells_of_body(*box.value);
    auto body_mass = kernel.topology().query().body_mass_properties(*box.value);
    if (shells.value && !shells.value->empty()) {
        auto shell_mass = kernel.topology().query().shell_mass_properties(shells.value->front());
        // 单位密度：volume=24，centroid=(11,21.5,32)。
        // inertia 是关于质心、世界坐标系行主序的 3x3 张量。
    }
}
```

这两个入口仅支持实际物理边界的平面、Line/LineSegment 边双边流形闭壳；原生解析体兼容代理面即使重新组壳也拒绝，面可凹且可带孔。实体壳按严格包含深度判定材料、空腔及材料岛，并以平行轴定理汇总；`body_shell_regions` 可查询深度、角色与直接父壳。相交/重叠/建模容差接触多壳不在支持范围。曲面、曲边（含显式 trim）、开壳、非流形或零体积壳失败且无部分值，面边界必须位于支撑平面。查询每次从当前拓扑重算，不分配网格、不写缓存或改变 Eval 状态。

### 实体点定位与有限线段裁剪（cycle-0074，已通过完整门禁）

```cpp
auto spatial_box = kernel.primitives().box({0,0,0}, 2,3,4);
if (spatial_box.value) {
    auto& topology_query = kernel.topology().query();
    axiom::BodySpatialQueryOptions options;
    options.position_tolerance = 1e-6;  // 模型长度单位；0 使用内核有效容差
    options.max_triangle_tests = 1000000;  // 不包含质量/壳关系前置检查
    auto point = topology_query.locate_point(*spatial_box.value, {-1,1,2}, options);
    if (point.value && point.value->nearest_boundary) {
        // Outside；最近点 (0,1,2)，距离 1，返回真实 face/shell 归属。
        auto nearest_face = point.value->nearest_boundary->face;
        auto nearest_shell = point.value->nearest_boundary->shell;
        auto actual_work = point.value->triangle_tests;
    } else if (!point.value) {
        auto report = kernel.diagnostics().get(point.diagnostic_id);
        handle_query_error(report);
    }
    auto clipped = topology_query.clip_segment(
        *spatial_box.value, {-1,1,2}, {3,1,2}, options);
    if (clipped.value) {
        // 一个 Inside 区间 [0.25,0.75]（无量纲），material_length=2 模型长度单位。
        auto intervals = clipped.value->intervals;
        auto material_length = clipped.value->material_length;
    } else {
        auto report = kernel.diagnostics().get(clipped.diagnostic_id);
        handle_query_error(report);
    }
    auto coplanar = topology_query.clip_segment(*spatial_box.value, {-1,0,2}, {3,0,2}, options);
    // 成功时 Boundary [0.25,0.75]，材料长度 0；须检查 coplanar.value。
    auto tangent = topology_query.clip_segment(*spatial_box.value, {-1,1,2}, {1,-1,2}, options);
    // 成功时孤立接触 Boundary [0.5,0.5]，材料长度 0。
    auto empty = topology_query.clip_segment(*spatial_box.value, {-2,1,2}, {-1,1,2}, options);
    // 无交集是成功空 intervals，材料长度 0。
}
```

Inside/Outside 按闭壳包含奇偶判断，支持空腔和材料岛；非空实体总给最近面内/边/顶点边界，等距按稳定 ShellId、FaceId 选择，内部零壳实体合同为 Outside 和空最近边界；公共 `create_body({})` 拒绝，本批未构造/验证该零壳查询分支。Boundary 点定位是距离容差带；线段裁剪不按该容差膨胀材料或合并可分辨薄层，长度不大于容差的线段失败。裁剪区间按参数排序、内部不重叠，省略 Outside，孤立相切点不与已覆盖区间重复，Boundary 段不计材料长度。

仅支持无自交的嵌入平面直边双边流形闭壳，曲面/显式裁剪曲边拒绝，面边界须在支撑平面上；壳间相交/重叠/建模容差接触失败。本批未新增壳自身全局自交证明或大规模空间加速。预算仅计前置检查之后的新三角形计算；耗尽或事件数值分辨率不足时无部分值。查询增加诊断和只读审计，不发布 MeshId，不写缓存、Eval 或事务；当前编辑即时可见，回滚后恢复。门禁和完整失败码见 [接口合同](AxiomKernel_详细模块接口清单.md#611-实体空间查询cycle-0074已通过完整门禁) 与 [错误码字典](../diagnostics/AxiomKernel_错误码与诊断码字典.md)。

## 16. Stage 3 退出主链样例（cycle-0079 / S3-EXIT）

以下沿用 `tests/sdk/smoke_test.cpp::main` 的公开门面夹具：三角形底面积 6、高 3、终端比例 0.5，中点比例 0.75，结果为当前真实多面体。文档片段未单独编译；实际 smoke 已随调度器完整 CTest **16/16、163.78 s** 通过（smoke 0.02 s）。

```cpp
axiom::Kernel kernel;
axiom::ProfileRef profile{"exit_scaled_triangle", {{0,0,0},{4,0,0},{0,3,0}}};
auto solid = kernel.sweeps().extrude_scaled(profile, {0,0,2}, 3.0, {0,0,0}, 0.5);
if (!solid.value) return 1;  // 用 diagnostic_id 检索诊断

auto mass = kernel.query().mass_properties(*solid.value);              // V=10.5
// mass: 单位密度，面积 L²、体积 L³、质心 L、世界坐标质心惯性 L⁵。
auto section = kernel.query().section_detailed(
    *solid.value, {{0,0,1.5},{0,0,1}});                                // S=3.375
// 材料内部的 closest_point 仍返回最近边界；本例为外点，距离=1。
auto nearest = kernel.query().closest_point(*solid.value, {0,0,4});
auto remote = kernel.primitives().box({0,0,4}, 1, 1, 1);
if (!remote.value) return 1;
auto distance = kernel.query().min_distance(*solid.value, *remote.value); // 1
if (!mass.value || !section.value || !nearest.value || !distance.value)
    return 1;  // 失败无部分值；读取 query.*.support_gate/preflight/budget/numeric

auto mesh = kernel.convert().brep_to_mesh(*solid.value, {});
if (!mesh.value) return 1;
auto report = kernel.convert().inspect_mesh(*mesh.value);
if (!report.value || report.value->triangle_count == 0 ||
    report.value->tessellation_strategy != "owned_topo_welded") return 1;
auto strict = kernel.validate().validate_all(*solid.value, axiom::ValidationMode::Strict);
if (strict.status != axiom::StatusCode::Ok) return 1;
```

此链不使用 bbox 截面或来源/创建缓存质量。详细 section 不发布 MeshId，兼容 `query().section` 的有面积成功分支会发布结果网格；空集/纯线点相切成功返回零句柄，不能与无 value 失败混淆。表示转换成功会发布网格并填缓存；owned 失败原子拒绝，不回退 bbox/创建参数。来源及 Face/Shell 见证、缓存 BodyId/当前边界/source_body、primitive 编辑资格和事务/Eval 合同沿用 §12.2；保存点/显式/析构/取消回滚会恢复当前边界并传播 dirty，合法提交之后的失败恢复已提交状态。旧 MeshId 是快照，Eval recompute 不自动运行质量/表示算法；metadata bbox_proxy 仅显示，不取得物理查询资格。

五类真实建模及 box/wedge/Generic/原生解析质量资格/占位和派生拒绝的完整范围、独立参考与 17 行自动化映射见 [API §6.1.4](AxiomKernel_详细模块接口清单.md#614-stage-3-统一退出支持矩阵cycle-0079--s3-exit)，本批三条证据及五项退出映射见 [验收 §1.6](../quality/AxiomKernel_测试与验收方案.md#16-cycle-0079--s3-exit-门禁与逐项证据)。平面多面体仅在浮点容差内精确，旋转/曲线扫掠/截面律查询实际采样多面体；四类未编辑原生解析体仅通用质量成功，实体截面/最近点/距离与代理壳积分拒绝。通用曲面/曲边积分及实体查询、曲面 thicken、任意 loft 匹配、相交多壳/全局嵌入证明、空间加速、完整 trim/标准交换、工业 Boolean 和 Eval 自动算法重算仍有限制，不作为 Stage 3 完成前提；最终文档门禁及调度器提交成功前保持 ready_for_acceptance，不提前宣布阶段退出。
