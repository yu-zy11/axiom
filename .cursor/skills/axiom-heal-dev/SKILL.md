---
name: axiom-heal-dev
description: Develop and harden AxiomKernel Heal (Validation/Repair) including strict topology checks, auto-repair workflows, diagnostic traceability, and regression tests. Use when changing validation modes, adding repair strategies, or wiring IO/Ops to validation/repair.
---

# AxiomKernel Heal 开发（Validation/Repair）

## 适用场景（触发词）

- `ValidationService/RepairService` 规则集、ValidationMode（Safe/Strict）语义
- 自动修复（auto_repair）策略、修复前后诊断与 related_entities
- IO 导入后 validate/repair 的闭环与一致性

## 代码入口

- Public API：`include/axiom/heal/heal_services.h`
- 实现：`src/heal/heal_services.cpp`
- 关键回归：`tests/ops_heal_test.cpp`、`tests/io_workflow_test.cpp`、`tests/diagnostics_test.cpp`

## 硬规则（必须遵守）

- **失败不污染源体**：修复失败/验证失败不得破坏输入体可用性（必要时回滚/生成派生体）。
- **Strict 可解释**：Strict 验证失败必须提供可定位诊断码，不接受“只是 OperationFailed”。
- **修复可追踪**：修复报告必须能追踪 before/after（至少 related_entities 含输入体与输出体）。

## 默认工作流

### 1) 先定义本次“验证范围”

- 是 bbox 有效性？拓扑闭合？来源引用一致性？还是容差冲突？
- 明确失败时的 code 与 related_entities 最小集合

### 2) 修复策略必须可回归

- 修复输出要么是：
  - 新 body（派生体）且 provenance 可解释
  - 或明确返回失败 + 不污染输入

### 3) 测试优先

在相关测试中固化：

- 失败不污染输入体（bbox/provenance/拓扑摘要不变）
- 修复成功后能通过对应模式验证（Safe/Strict）
- 诊断报告包含关键字段与 related_entities

## DoD（完成定义）

- **实现**：验证/修复语义稳定；失败不污染；Strict 有清晰失败原因
- **诊断**：修复前后问题与结果可追踪
- **测试**：覆盖失败不污染 + 修复成功闭环

