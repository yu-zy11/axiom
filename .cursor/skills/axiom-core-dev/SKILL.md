---
name: axiom-core-dev
description: Develop and harden AxiomKernel Core (KernelState/KernelConfig, Result/StatusCode, ids, runtime stores reset, capability reporting) with stable invariants and tests. Use when changing core types, config/tolerance wiring, stores/caches, version/transaction semantics, or when modules disagree on capability/format reporting.
---

# AxiomKernel Core 开发（KernelState/Config/Result/Stores）

## 适用场景（触发词）

- `KernelState/KernelConfig`、对象 store、id 分配、runtime reset/clear
- `Result<T>/StatusCode` 语义调整、错误码与诊断的 core 级约束
- 能力报告（capability/module/services/formats）与实际模块实现不一致
- 版本/事务基础设施与“失败不污染状态”类问题定位

## 硬约束（必须遵守）

- **Core 不做业务算法**：只提供跨模块一致的状态/配置/结果与生命周期工具。
- **不引入上层依赖**：Core 不能依赖 geo/topo/ops/rep/io/heal/eval 具体实现逻辑。
- **状态一致性优先**：任何失败路径都不能让 store/caches/indices 进入“半更新”。
- **能力报告必须可解释**：对外报告的 formats/services 必须与实际可用入口一致。

## 代码入口（先读这些）

- `KernelState`：`include/axiom/internal/core/kernel_state.h`
- 门面：`include/axiom/sdk/kernel.h` + `src/sdk/kernel.cpp`
- `Result/Types`：`include/axiom/core/result.h`、`include/axiom/core/types.h`

## 默认工作流

### 1) 先定义不变量（Invariant）

对本次改动，写清至少 3 条不变量并用测试固化，例如：

- `reset_runtime_stores()` 后 eval/mesh/intersection/diagnostics 等 runtime store 都被清空
- `Kernel::io_supported_formats()` 与 `IOService::detect_format/export_auto` 支持集合一致（或明确区分“报告格式 vs 内部支持格式”）
- 任何 `set_*_tolerance` 类 config 写入都满足：非法输入失败 + 不改变旧值

### 2) 失败语义必须可回归

- 失败返回 `Result<T>`/`Result<void>`
- 绑定稳定 `StatusCode`
- 必要时要求诊断可检索（通过 `DiagnosticService`）

### 3) 测试入口（建议）

- “门面一致性/能力报告”：`tests/smoke_test.cpp` 或新增 core 专项测试
- “容差/配置写入”：`tests/math_services_test.cpp`（若仅 math）或新增 core 测试
- “runtime reset/clear”：按影响面选择 `tests/query_eval_test.cpp`、`tests/representation_io_test.cpp` 等回归入口

## DoD（完成定义）

- **实现**：不变量完整落实；失败不污染状态；报告/统计无自相矛盾
- **诊断**：关键失败路径能定位（至少 status + 诊断 id 可追踪）
- **测试**：新增/更新回归覆盖“正常/非法输入/失败不污染状态”

