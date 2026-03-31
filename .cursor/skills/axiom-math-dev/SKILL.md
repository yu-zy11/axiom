---
name: axiom-math-dev
description: Develop and harden AxiomKernel MathCore (linear algebra, predicates, tolerance policy) aligned with AGENTS.md and docs/plan progress. Use when implementing MathCore features, robust numeric behavior, tolerance strategy changes, or when Math changes affect Geo/Topo/Ops/Rep.
---

# AxiomKernel MathCore 开发（Math/Predicate/Tolerance）

本 Skill 用于在 AxiomKernel 中推进 **MathCore** 的可用性与鲁棒性，严格遵守 `AGENTS.md` 的模块边界与测试/诊断要求。

## 适用场景（触发词）

- 需求涉及 `math/MathCore`、公差（tolerance）、谓词（predicate）、线代（linear algebra）、数值鲁棒性
- 需要为 `Geo/Topo/Ops/Rep/IO/Heal/Boolean` 的主链路补齐数学工具或修复数值不稳定

## 约束与原则（必须遵守）

- **依赖方向**：MathCore 不能依赖任何上层模块（见 `AGENTS.md` 禁止依赖）。
- **失败可诊断**：对外 API 的失败路径使用 `axiom::Result<T>` 与稳定错误码；Math 的“纯函数工具”避免引入上层诊断依赖。
- **公差必须一致**：同一概念的“有效容差”必须有唯一解析入口，避免各模块各算各的。
- **数值安全优先**：防 `NaN/Inf` 传播、溢出/下溢、灾难性消元；边界输入应返回可控结果（例如 `Sign::Uncertain/Zero`）。
- **改动最小化**：Math 改动优先只动 `include/axiom/math`、`src/math`、`include/axiom/internal/math`、对应测试；若必须改其它模块，拆分提交。

## 快速定位：本仓库的 MathCore 入口

- Public API：`include/axiom/math/math_services.h`
- 实现：`src/math/math_services.cpp`
- Internal utils：`include/axiom/internal/math/math_internal_utils.h` + `src/math/math_internal_utils.cpp`
- 回归测试：`tests/math_services_test.cpp`（必要时新增跨模块回归，但保持拆分）

## 标准工作流（按顺序执行）

### 1) 对齐文档与阶段目标

- 阅读并提炼“本阶段 Math 需要交付什么”：
  - `AGENTS.md`（模块边界、测试门禁、错误码/诊断要求）
  - `docs/plan/AxiomKernel_当前开发进度.md`（Stage 重点）
  - `docs/plan/AxiomKernel_几何引擎功能需求文档.md`（P0/MVP 相关）
- 输出一份 **Math 交付清单**（只列“本次要做的”，不要泛化成长期路线图）：
  - 线代/变换
  - 谓词（orient、bbox/range、并行/正交等）
  - 公差策略（effective/scale/clamp/merge、按体尺度策略）

### 2) 设计：明确“统一入口”

- **公差解析**：将 `requested + policy + scale` 的合成规则集中到 MathCore 一处（internal helper），并在调用点统一使用。
- **谓词鲁棒性**：定义以下策略并在测试中固化：
  - 非有限输入：返回 `Uncertain` 或结构化失败（视 API）
  - 近退化：`Zero` 的阈值应随尺度自适应（machine epsilon + scale）
  - 大尺度：避免 `x*x` 形式，优先 `hypot`/`long double`

### 3) 实现：只做必要改动

- 优先在 `src/math/*` 与 `include/axiom/internal/math/*` 落地；
- 仅在确需统一行为时才改调用点（例如 `PredicateService` 内部使用统一容差解析）。

### 4) 测试：先补回归再调实现

最少覆盖：

- **公差中心化**：`min_local/max_local` 对 `effective_*` 与 `resolve_*_for_scale` 生效（用 `KernelConfig` 构造不同策略测试）。
- **谓词**：
  - 大尺度输入不溢出且符号稳定（well-separated case）
  - `NaN/Inf` 输入返回 `Uncertain`（或明确失败）
  - 近共线/近共面落到 `Zero` 的阈值行为可重复

### 5) 验证门禁

- 全量构建与测试（推荐命令见 `AGENTS.md`）：
  - `cmake -S . -B build -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON`
  - `cmake --build build`
  - `ctest --test-dir build --output-on-failure`

### 6) 提交规范（重要）

- **只提交本次 Math 相关文件**；其它文件若被改动：
  - 先 `git restore` 回退，或拆分为单独提交再合入
- 提交前确保 `git status` 干净、`ctest` 全绿
- 提交信息建议：

```text
math: <动词> <目标>

<1-2 句解释：为何要改、统一了什么入口、解决了什么不一致/不稳定>
```

## 常见坑与处理

- **“容差到处算”导致行为漂移**：必须把解析入口统一（internal helper），并让所有调用点走同一条路。
- **测试不稳定**：避免用随机数据；对接近阈值的断言用“区间/相对误差/Sign 允许集合”而不是硬等值（除非定义明确）。
- **Math 误引入上层依赖**：任何 `#include` 指向 `geo/topo/ops/...` 都要重新审视（MathCore 不能反向依赖）。

