---
name: axiom-ops-dev
description: Develop and harden AxiomKernel OpsCore (primitives/sweep/boolean/modify/blend/query interactions, provenance propagation, minimal topology materialization, diagnostics, tests) aligned with AGENTS.md and docs/plan progress. Use when implementing ops services, changing materialization/provenance rules, adding BOOL/MOD/BLEND/QUERY diagnostics codes, or updating ops/heal/boolean workflow tests.
---

# AxiomKernel OpsCore 开发（Primitive/Sweep/Boolean/Modify/Blend/Query）

本 Skill 用于在 AxiomKernel 中推进 **OpsCore** 的“可回归最小闭环”能力，严格遵守 `AGENTS.md` 的模块边界、结构化结果与诊断码规范，并与 `docs/plan/AxiomKernel_当前开发进度.md` 的阶段目标对齐。

## 适用场景（触发词）

- 需求涉及 `ops/OpsCore`：`PrimitiveService/SweepService/BooleanService/ModifyService/BlendService/QueryService`
- 需要调整/修复 **来源追踪（source_*)**、派生体 provenance、owned/source topology 分离语义
- 需要改/扩 **最小拓扑物化**（materialization），确保派生结果体通过 `ValidationMode::Strict`
- 需要新增或细化 `AXM-BOOL-*` / `AXM-MOD-*` / `AXM-QUERY-*` / `AXM-HEAL-*` 诊断码绑定，并补回归测试

## 约束与原则（必须遵守）

- **依赖方向**：OpsCore 可依赖 `Math/Geo/Topo/Rep/Diagnostics`；禁止把 Ops 逻辑下沉到 `Math` 或反向依赖（见 `AGENTS.md`）。
- **统一返回**：所有对外失败路径返回 `axiom::Result<T>`（或 `OpReport`），并绑定稳定错误码（`include/axiom/diag/error_codes.h`）。
- **失败可诊断**：对 BOOL/HEAL/IO 等高风险链路，失败诊断需可区分阶段；必要时补 `related_entities` 便于回溯。
- **事务一致性**：修改型操作失败必须 **不污染源体**（回滚/复制语义要可回归验证）。
- **provenance 语义不可破坏**：
  - **primitive**：允许拥有 `owned shell/face` 作为工作流/验证闭环支撑，但不得把 owned topology 伪装成 `source_*`。
  - **derived result（Boolean/Modified/Blend/Sweep 等）**：必须保留可解释的 `source_bodies/source_shells/source_faces` 语义；避免无意扩大来源集合。
- **Strict 门禁**：任何“应当可物化闭合壳”的结果体，必须能 `validate_topology(..., Strict)` 通过；若暂不能，需返回可诊断失败而不是静默产出坏拓扑。

## 快速定位：OpsCore 关键入口

- Public API：`include/axiom/ops/ops_services.h`
- 实现：`src/ops/ops_services.cpp`
- 最小物化：`include/axiom/internal/core/topology_materialization.h`
- 验证/修复：`include/axiom/heal/heal_services.h` + `src/heal/heal_services.cpp`
- IO 元数据（kind/origin/axis/params）：`src/io/io_service.cpp`
- 关键回归测试（ctest 名称）：
  - `tests/ops_heal_test.cpp`（`axiom_ops_heal_test`）
  - `tests/boolean_workflow_test.cpp`（`axiom_boolean_workflow_test`）
  - `tests/boolean_prep_test.cpp`（`axiom_boolean_prep_test`）
  - `tests/query_eval_test.cpp`（`axiom_query_eval_test`）
  - `tests/smoke_test.cpp`（`axiom_smoke_test`）

## 标准工作流（按顺序执行）

### 1) 对齐“进度文档缺口”并限定本次范围

- 阅读：
  - `AGENTS.md`
  - `docs/plan/AxiomKernel_当前开发进度.md`（重点：7.3/7.4/7.5/7.6/7.7）
- 只选择 **1-3 个**可闭环增量（实现 + 测试 + ctest），避免泛化成“完成全部工业建模”。

### 2) 设计：先定义 provenance / 物化 / 验证的契约

对每个增量，写清：

- 结果体 `BodyKind` 与 `label`
- `source_bodies/source_shells/source_faces` 的规则（来自谁、何时清空、何时继承）
- owned topology 的生成路径（来源局部重建 / 来源壳克隆 / bbox 或其他回退）
- `Strict` 期望（应通过/应失败）以及失败应绑定的 `AXM-*` code

### 3) 实现：优先在 OpsCore 内聚，物化逻辑集中在 materialization

- OpsCore 负责：输入校验、来源集合拼装（必须可解释）、bbox/参数更新、事务一致性
- 物化层负责：owned topology 构建/克隆/回退链路（避免在各个 ops 分散写“造壳”逻辑）

### 4) 测试：用“工作流测试”固化语义

最低要求（每个变更都要命中至少一类）：

- **一致性**：失败不污染源体（bbox/provenance/拓扑索引保持）
- **Strict**：应通过的体必须 `validate_topology(..., Strict)` 通过
- **诊断码**：失败时诊断报告包含预期 code（`kernel.diagnostics().get(...)`）
- **provenance**：`source_*` 查询返回的集合大小/内容符合契约（尤其 BOOL subtract/union/intersect 的差异）

### 5) 构建与门禁

推荐命令（与 `AGENTS.md` 对齐）：

```text
cmake -S . -B build -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 常见坑与处理

- **Subtract 传播来源壳导致结果体变成多壳**：如果物化优先走 `source_shells` 克隆，减法通常只应继承 `lhs` 的壳/面来源；否则会产出 2 个 shell 破坏回归。
- **primitive detach 时把 owned 写成 source**：`detach_owned_topology` 这类“继承 owned→source”逻辑必须对 primitive 关闭，避免破坏 provenance 语义。
- **物化函数默认要求 bbox 有效**：构造/导入链路需保证 `bbox.is_valid` 与 min/max 合法；否则严格验证与查询可能失败，应返回明确错误码。

