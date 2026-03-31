---
name: axiom-io-dev
description: Develop and harden AxiomKernel IO workflows (STEP/AXMJSON/glTF/STL import/export, format detection, validation/auto-repair integration, diagnostics) with regression tests. Use when adding formats, aligning kernel capability reports with IOService, or improving IO robustness.
---

# AxiomKernel IO 开发（Import/Export/Workflow/Diagnostics）

## 适用场景（触发词）

- STEP/AXMJSON/glTF/STL 导入导出、格式探测、路径与批处理
- 导入后验证/自动修复/诊断回流
- `Kernel` 的 format 能力报告与 `IOService` 实际支持不一致

## 代码入口

- Public API：`include/axiom/io/io_service.h`
- 实现：`src/io/io_service.cpp`
- 受影响模块：`rep`（gltf/stl 导出依赖 `brep_to_mesh`）、`heal`（导入后 validate/repair）
- 关键回归：
  - `tests/io_workflow_test.cpp`
  - `tests/representation_io_test.cpp`
  - `tests/diagnostics_test.cpp`（涉及诊断回流）

## 硬规则（必须遵守）

- **路径/格式鲁棒**：空路径、非法扩展名、不可写目录必须有明确失败语义与诊断码。
- **工作流闭环**：导入成功/失败/修复成功/修复失败都要能在诊断报告中体现。
- **能力报告一致性**：对外报告（如 `Kernel::io_supported_formats()`）必须与 `IOService` 的 `detect_format/export_auto/import_auto` 口径一致，或明确区分两者并测试固化。

## 默认工作流

### 1) 先明确“口径”

每次加/改格式先写清：

- **detect_format 支持哪些 ext**
- **import_auto/export_auto 允许哪些 format**
- **Kernel 能力报告对外宣称哪些 format**

并用测试固化一致性（避免“实现支持了但报告没更新”或相反）。

### 2) 诊断与 related_entities

- 导入/导出失败必须绑定稳定 code
- 可关联时填 `related_entities`（例如 body_id、导出路径、导入源文件）

### 3) 测试（至少）

- 正常路径：导出->导入或导出文件存在性/关键字段
- 非法路径：空路径/不可写/未知扩展名
- 工作流：导入后 validation/auto_repair 分支有断言

## DoD（完成定义）

- **实现**：format/路径/工作流一致；失败可诊断；不引入静默回退
- **测试**：覆盖正常+失败+诊断回流；必要时覆盖 kernel 报告一致性

