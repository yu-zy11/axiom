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

方向按单位化使用，必须垂直于轮廓平面；中心须在该平面内，距离为有限正数。支持凸/凹外环、非嵌套分离孔洞和任意空间朝向，扭角以弧度表示，正负部分角及正负整周均可，限于一周。沿方向按右手规则扭转，零扭角在该法向合同下兼容 `extrude`。角站差不超过 7.5°，站间侧壁交替对角线剖分以避免系统性体积偏差。结果为有真实共享拓扑的采样多面体 BRep，质量属性来自该多面体；不是解析螺旋面，未与变比例或至平面拉伸组合。非法中心/方向/扭角或退化轮廓在模型分配前以 `InvalidInput / AXM-CORE-E-0002` 拒绝。

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

Bezier/BSpline/NURBS 开放导轨、显式端点重合且首尾切向连续的闭合样条，以及整圆/椭圆周期导轨使用旋转最小标架；空间闭环会做 holonomy 校正。`make_composite_chain(children)` 创建的复合导轨也可直接传入，相邻子段必须端点重合且 G1 切向连续；开放链有两个端盖，闭合链无端盖。周期带孔截面的外边界与各孔边界分别物化为独立闭壳，因此壳数和网格连通分量都为 `1 + holes_xyz.size()`；开放带孔导轨由端盖连成单壳。`sweep_scaled` 对直线、CompositePolyline 和上述开放曲线/复合导轨支持正比例收缩与扩张，`end_scale=1` 与 `sweep` 兼容。非单位比例不适用于周期导轨；零/负比例、非线性比例律、过小比例、伪闭合、切向断裂、嵌套复合链、抛物/双曲子段、尖点、过紧曲率和自靠近导轨保守拒绝；这是采样多面体 BRep，不是解析扫掠曲面。

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

## 7. 布尔操作样例

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

## 8. 修改操作样例

## 8.1 偏置

```cpp
auto offset = kernel.modify().offset_body(
  body_id,
  2.0,
  kernel.tolerance().global_policy()
);
```

## 8.2 抽壳

```cpp
std::vector<FaceId> removed_faces = {face_1};

auto shelled = kernel.modify().shell_body(
  body_id,
  removed_faces,
  2.5
);
```

## 8.3 删除面补面

```cpp
auto healed = kernel.modify().delete_face_and_heal(body_id, target_face);
```

## 9. 圆角与倒角样例

## 9.1 常半径圆角

```cpp
std::vector<EdgeId> edges = {edge_1, edge_2, edge_3};
auto fillet = kernel.blends().fillet_edges(body_id, edges, 3.0);
```

## 9.2 倒角

```cpp
auto chamfer = kernel.blends().chamfer_edges(body_id, edges, 2.0);
```

## 9.3 圆角失败处理建议

```cpp
if (fillet.status != StatusCode::Ok) {
  auto diag = kernel.diagnostics().get(fillet.diagnostic_id);
  if (contains_issue(diag, "AXM-BLEND-E-0002")) {
    suggest_user("请减小圆角半径");
  }
}
```

## 10. 查询与分析样例

## 10.1 质量属性

```cpp
auto mp = kernel.query().mass_properties(body_id);
if (mp.status == StatusCode::Ok) {
  print("volume", mp.value->volume);
  print("area", mp.value->area);
  print("centroid", mp.value->centroid);
}
```

## 10.2 最短距离

```cpp
auto dist = kernel.query().min_distance(body_a, body_b);
```

## 10.3 截面

```cpp
Plane section_plane{
  .origin = {0,0,10},
  .normal = {0,0,1}
};

auto sec = kernel.query().section(body_id, section_plane);
```

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

AXMJSON、Axiom IGES 元数据子集与 Axiom BREP JSON 子集均限 64 MiB，且在分配 `BodyId` 前完成文件、结构、格式、有限数值、包围盒和轴校验。失败不写 Body/Mesh store，修复原文件后可原位重试。AXMJSON 兼容早期仅身份与 bbox 字段的文件，但扩展几何字段一旦出现就必须成组完整。标准 IGES DE 实体仍返回 `NotImplemented`，不会被当成 Axiom 子集物化。

## 11.3 导入后修复

```cpp
auto imported = kernel.io().import_step(path, opts);
if (imported.status == StatusCode::Ok) {
  auto valid = kernel.validate().validate_all(*imported.value, ValidationMode::Standard);
  if (valid.status != StatusCode::Ok) {
    auto repaired = kernel.repair().auto_repair(*imported.value, RepairMode::Safe);
    use_if_valid(repaired);
  }
}
```

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

`import_many_step` 和 `import_many_auto` 具有相同模型存储原子性；单项实际失败触发整批回滚，诊断证据仍保留。STEP/AXMJSON 的后验验证、自动修复及修复后复验问题复制为 `io.post_import.validation/repair/post_validate`，保留有限数值证据且不改源 HEAL 报告。导入后验证或修复问题可能随成功导入报告返回，`Ok` 本身不保证有效体，须读取诊断或显式验证。批量导出不回滚已写文件，普通文本/目录辅助接口尚未纳入本批证据门禁。

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

单项修改型修复后验失败会回收本次物化对象；`repair_many_auto/remove_small_edges/remove_small_faces/merge_near_coplanar_faces` 均不泄漏半成功结果。`repair_face_trim_pcurves(face_id, RepairMode::Safe)` 支持 Plane/Cylinder/Sphere，重建或后验失败恢复原 coedge PCurve 绑定并回收新增 PCurve。失败报告使用 `heal.*` 阶段、实体与有限数值证据，不扩大现有修复规则或曲面支持范围。HEAL 回滚不恢复 `next_id`，重试不保证复用被回收对象的 ID；批量子项根因保留在原诊断，返回的批量报告记录失败目标与回滚上下文。

## 12. 三角化样例

## 12.1 实体转网格

```cpp
TessellationOptions tess;
tess.chordal_error = 0.05;
tess.angular_error = 5.0;
tess.compute_normals = true;

auto mesh = kernel.convert().brep_to_mesh(body_id, tess);
```

## 12.2 局部修改后重新取网格

```cpp
auto updated = kernel.modify().offset_body(body_id, 1.0, tol);
auto mesh = kernel.convert().brep_to_mesh(updated.value->output, tess);
```

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

这两个入口仅支持平面、Line/LineSegment 边组成的双边流形闭壳，面可凹且可带孔。实体含多个互不重叠的独立实体壳时使用平行轴定理汇总；独立内壳不解释为空腔，相交/重叠多壳也不在支持范围。曲面、曲边、开壳、非流形或零体积壳失败且无部分值。查询每次从当前拓扑重算，不分配网格、不写缓存或改变 Eval 状态。
