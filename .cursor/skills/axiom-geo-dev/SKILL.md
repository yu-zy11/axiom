---
name: axiom-geo-dev
description: Develop and harden AxiomKernel GeoCore (curves/surfaces creation, eval/domain/bbox/closest queries, Trimmed/Offset/Revolved/Swept, PCurve basics) aligned with AGENTS.md and docs/plan progress. Use when implementing geometry types, parameterization semantics, numeric validation (NaN/Inf), or when Geo changes affect Topo/Ops/Rep/IO tests.
---

# AxiomKernel GeoCore 开发（Curve/Surface/Eval/Domain/Closest/BBox）

本 Skill 用于在 AxiomKernel 中推进 **GeoCore** 的“最小可回归闭环”，遵守 `AGENTS.md`：模块边界、结构化结果、稳定错误码、以及全量 `ctest` 全绿。

## 适用场景（触发词）

- 需求涉及 `geo/GeoCore`、曲线/曲面类型补齐、`eval/domain/bbox/closest_*`
- 新增/修复 `Revolved/Swept/Trimmed/Offset` 等派生曲面语义
- 修复几何求值的 `NaN/Inf` 传播、退化输入（零向量/共线/半径<=0）与容差相关行为
- 需要把文档 7.1（`docs/plan/AxiomKernel_几何引擎功能需求文档.md`）的“必须支持类型”落成测试

## 约束与原则（必须遵守）

- **依赖方向**：GeoCore 可以依赖 MathCore/Diagnostics；**禁止** GeoCore 反向依赖 TopoCore（见 `AGENTS.md`）。
- **结构化返回**：对外 API 一律 `axiom::Result<T>`；失败必须用 `diag_codes::*`（`include/axiom/diag/error_codes.h`）。
- **输入有限性**：所有 `eval/closest` 路径必须拒绝 `NaN/Inf`（参数或点/向量）。
- **参数域一致性**：每种曲线/曲面必须定义可重复的 domain，并在 `closest_*` 中 clamp/归一化。
- **最小可回归**：每新增一种类型，至少提供：
  - factory 创建（非法输入保护）
  - `domain`
  - `eval`（点 + 一阶导/normal 的最小语义；高阶可占位）
  - `bbox`
  - `closest_parameter/closest_uv/closest_point`（可先网格采样 + 轻量 refine）
  - 对应 `tests/geo/geometry_test.cpp` 回归

## 快速定位：GeoCore 入口文件

- Public API：`include/axiom/geo/geometry_services.h`
- 实现：`src/geo/geometry_services.cpp`
- 记录结构：`include/axiom/internal/core/kernel_state.h`
- 回归测试：`tests/geo/geometry_test.cpp`（ctest：`axiom_geometry_test`）
- 阶段缺口：`docs/plan/AxiomKernel_当前开发进度.md`（重点看 7.1）

## 标准工作流（按顺序执行）

### 1) 对齐文档缺口与本次范围

- 从 `docs/plan/AxiomKernel_当前开发进度.md` 的 7.1 拉出“未开始/缺失”的几何类型或语义。
- 把目标压缩为“本次要补的 1-3 个类型/能力”，避免一次性铺太大。

### 2) 设计参数化（domain 与几何语义先定）

对每个类型写清：
- `domain`（有限/无限、角度是否 `[0,2π)`、派生曲面 `u/v` 含义）
- `eval(u,v,t)` 的几何定义（局部坐标框架、方向约定、normal 方向）
- `closest_*` 的最小可用策略（采样 + clamp + 可选少量 refine）
- `bbox` 的策略（解析式优先；不易解析则采样但要稳定、可重复）

### 3) 实现（优先在 GeoCore 内聚落地）

常见落点：
- record 扩展：`detail::CurveRecord / SurfaceRecord`
- domain：`curve_domain(...)` / `surface_domain(...)` 或 `SurfaceService::domain` 的“继承/委托”逻辑
- eval：`CurveService::eval` / `SurfaceService::eval`
- closest：`CurveService::closest_parameter` / `SurfaceService::closest_uv`（必要时用采样）
- bbox：`CurveService::bbox` / `SurfaceService::bbox`（派生曲面建议在 service 侧采样求 bbox）

### 4) 测试（每个类型至少 1 个正向 + 1 个非法输入）

在 `tests/geo/geometry_test.cpp` 中为每个新增类型断言：
- 创建成功
- domain 符合预期
- eval 在代表点上返回稳定值
- bbox `is_valid == true`
- closest_* 返回值落在 domain 内（或满足归一化约束）

### 5) 门禁与回归

按 `AGENTS.md` 推荐命令跑全量：

```text
cmake -S . -B build -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

若出现“编译通过但测试行为异常/不一致”，优先做一次 **干净重建**：

```text
rm -rf build
cmake -S . -B build -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 常见坑与处理

- **把 Trim/Offset 做成 Topo 语义**：GeoCore 的 `Trimmed` 只负责“参数域裁剪 + 委托基曲面求值”，不要依赖 Face/Loop/PCurve 规则（那属于 Topo 的 trim bridge）。
- **closest_* 不稳定**：采样网格要固定尺寸；断言用“落域 + 近似”而非硬编码极值。
- **新增失败路径未绑定错误码**：确保走 `invalid_input_result/failed_result` 且 code 来自 `diag_codes::*`。

