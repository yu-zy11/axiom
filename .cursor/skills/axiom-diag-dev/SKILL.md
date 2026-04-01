---
name: axiom-diag-dev
description: Develop and harden AxiomKernel Diagnostics (error/diagnostic codes, report aggregation/search/export, related_entities binding, workflow-stage diagnostics) with regression tests. Use when adding codes, binding diagnostics to failures, exporting JSON, or ensuring all critical paths are diagnosable.
---

# AxiomKernel Diagnostics 开发（Codes/Report/Search/Export）

## 适用场景（触发词）

- 新增/调整 `AXM-*-*` 错误码、诊断码、用户可读文案映射
- 给失败/告警路径补“可检索、可导出”的诊断绑定
- `DiagnosticService`：聚合、按相关实体检索、JSON 导出稳定性
- 布尔/修复/导入导出等“阶段化诊断”规范化

## 代码入口

- 公共码与服务：
  - `include/axiom/diag/error_codes.h`
  - `include/axiom/diag/diagnostic_service.h`
- 内部工具/实现：
  - `include/axiom/internal/diag/diagnostic_internal_utils.h`
  - `src/axiom/diag/diagnostic_internal_utils.cpp`
  - `src/axiom/diag/diagnostic_service.cpp`
- 文档字典（改码必同步）：`docs/diagnostics/**`
- 关键回归：`tests/diag/diagnostics_test.cpp`（以及受影响工作流测试）

## 硬规则（必须遵守）

- **诊断码是 DoD 的一部分**：关键失败路径必须能被定位，不允许“静默失败/只有 StatusCode”。
- **related_entities 必须可用**：能填则填（body/shell/face/loop/edge/vertex/curve/surface/mesh 等）。
- **稳定性**：JSON 字段向后兼容；新增字段可选但不能删旧字段。

## 默认工作流

### 1) 先选“复用还是新增”

- 优先复用现有码（保持字典紧凑）
- 只有在“语义明显不同、需要区分阶段/原因”时新增

### 2) 绑定到代码路径

- 所有入口统一走 `Result<T>` 返回
- 失败/告警路径确保有：
  - `StatusCode`
  - `diag_codes::k*`（或对应 code 常量）
  - `DiagnosticService` 可检索（必要时 `find_by_related_entity`）

### 3) 测试固化

- 在 `tests/diag/diagnostics_test.cpp` 断言：
  - 失败时 code 正确
  - JSON 导出包含关键字段
  - related_entities 可用于反查
- 若是工作流码（BOOL/IO/HEAL），同时在对应 workflow 测试里加断言

## DoD（完成定义）

- **实现**：码字典与绑定路径一致；可检索；可导出
- **文档**：字典与用户文案同步（若仓库已维护这些文档）
- **测试**：新增/更新回归覆盖“失败码/检索/导出”

