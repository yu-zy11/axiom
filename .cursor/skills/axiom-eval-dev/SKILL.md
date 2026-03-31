---
name: axiom-eval-dev
description: Develop and harden AxiomKernel EvalGraph (dependencies, invalidation, recompute, graph governance, observability) and cross-module integration. Use when changing invalidation rules, adding graph queries, preventing cycles, or wiring Ops/Topo changes to eval recomputation.
---

# AxiomKernel Eval 开发（EvalGraph）

## 适用场景（触发词）

- 依赖图：依赖/反向依赖、循环保护、缺失依赖防护
- 失效传播/重算计数、批量重算、图清理与可观测接口
- Ops/Topo/IO 改动后需要触发 eval invalidation 的联动

## 代码入口

- Public API：`include/axiom/eval/eval_services.h`
- 实现：`src/eval/eval_services.cpp`
- 内部工具：`include/axiom/internal/core/eval_graph_invalidation.h` 等
- 关键回归：`tests/query_eval_test.cpp`

## 硬规则（必须遵守）

- **可观测性**：至少能查询节点存在性/标签/依赖与反向依赖；重算计数可回归。
- **稳定失败语义**：循环依赖/缺失依赖等必须有可区分诊断码（DoD）。
- **跨模块一致性**：Topo/Ops 的变更若会影响结果，应有明确的 invalidation 合同，并在测试固化。

## 默认工作流

### 1) 定义“事件→失效”的合同

例：体被替换/修复/导入后，哪些节点必须失效？重算是否允许去重？

### 2) 先补测试，再改实现

在 `tests/query_eval_test.cpp` 固化：

- 循环依赖失败码
- 去重重算计数行为
- 缺失依赖防护行为
- 绑定/解绑体与节点的行为

## DoD（完成定义）

- **实现**：依赖治理稳定；跨模块联动可解释
- **诊断**：循环/缺失等关键失败路径可区分
- **测试**：至少覆盖 1 正常 + 1 循环失败 + 1 缺失依赖

