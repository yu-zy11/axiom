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
ProfileRef profile = build_closed_profile(...);

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

## 6.2 旋转

```cpp
Axis3 axis{
  .origin = {0, 0, 0},
  .direction = {0, 1, 0}
};

auto body = kernel.sweeps().revolve(profile, axis, 360.0);
```

## 6.3 放样

```cpp
std::vector<ProfileRef> profiles = {
  profile_1,
  profile_2,
  profile_3
};

auto loft = kernel.sweeps().loft(profiles);
```

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
