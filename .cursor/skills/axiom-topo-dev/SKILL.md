---
name: axiom-topo-dev
description: Develop and harden AxiomKernel TopoCore (topology transaction/query/validation, trim bridge, diagnostics) aligned with AGENTS.md and docs/plan progress. Use when implementing TopoCore rules, adding topology diagnostics codes, strengthening validation (loops/faces/shells/bodies), or updating topology tests and docs.
---

# AxiomKernel TopoCore 开发（Transaction/Query/Validation/Trim Bridge）

本 Skill 用于在 AxiomKernel 中推进 **TopoCore** 的可用性与一致性规则集，严格遵守 `AGENTS.md` 的模块边界、结构化结果与诊断码规范，并与 `docs/plan` 的阶段目标对齐。

## 适用场景（触发词）

- 需求涉及 `TopoCore/topo`、拓扑事务（transaction）、拓扑查询（query）、拓扑验证（validation）
- 需要补齐/增强拓扑一致性规则：loop/face/shell/body、反向索引、来源引用（source_*）一致性
- 需要推进 Stage 2 的修剪桥接（trim bridge）：`PCurve`、UV 闭合、`PCurve <-> 3D` 一致性
- 需要新增或细化 `AXM-TOPO-*` 错误码/诊断码，并补测试与文档字典

## 约束与原则（必须遵守）

- **依赖方向**：TopoCore 可以依赖 GeoCore/MathCore/Diagnostics；禁止反向依赖（见 `AGENTS.md`）。
- **失败可诊断**：对外接口返回 `axiom::Result<T>`；失败必须带稳定错误码（`AXM-TOPO-E-xxxx`）并尽量包含 `related_entities`。
- **事务一致性**：修改型操作必须可回滚；失败不污染原模型；索引必须随提交/回滚保持一致。
- **规则先测试**：新增/加严规则必须补回归测试；测试需隔离（常用手段：独立事务块 + `rollback()`）。
- **文档同步**：新增/变更拓扑错误码时，同步：
  - `include/axiom/diag/error_codes.h`
  - `docs/diagnostics/AxiomKernel_错误码与诊断码字典.md`
  - `docs/diagnostics/AxiomKernel_用户可读错误文案映射表.md`

## 快速定位：TopoCore 入口文件

- Public API：`include/axiom/topo/topology_service.h`
- 实现：`src/topo/topology_service.cpp`
- KernelState（拓扑容器/索引）：`include/axiom/internal/core/kernel_state.h`
- 回归测试：`tests/topology_test.cpp`（ctest 名称：`axiom_topology_test`）
- 阶段进度与缺口：`docs/plan/AxiomKernel_当前开发进度.md`（重点看 7.2）
- 功能需求对照：`docs/plan/AxiomKernel_几何引擎功能需求文档.md`（Topo 章节）

## 标准工作流（按顺序执行）

### 1) 对齐阶段目标与缺口（只做本次范围）

- 读取：
  - `AGENTS.md`（模块边界、诊断与测试门禁）
  - `docs/plan/AxiomKernel_当前开发进度.md`（Topo 7.2 的“未开始/缺失”）
- 输出一份 **本次要补的规则/接口清单**（3-7 条，避免泛化）。

### 2) 设计：明确“规则触发条件 + 错误码 + 相关实体”

对每条规则，写清楚：

- 触发条件（例如：同一环内重复 Edge、边未被任何 coedge 引用、内外环 UV 方向规则）
- 错误码（优先复用现有 `diag_codes::kTopo*`；确需新增则按字典编号）
- `related_entities` 最小集合（典型：`face/loop/coedge/edge` 的 id）

### 3) 实现：优先在 TopoCore 内聚落地

常见实现落点：

- 环/面/壳基本一致性：`validate_loop_record`、`validate_face*`、`validate_shell*`、`validate_indices_consistency`
- 事务创建门禁：`TopologyTransaction::create_*`
- Trim bridge（Stage 2）：
  - `TopologyValidationService::validate_loop_pcurve_closedness`
  - `TopologyValidationService::validate_face_trim_consistency`

### 4) 测试：用回归用例固化诊断码

最低要求：

- 失败时断言：
  - `status != Ok`
  - 诊断报告里包含预期 `AXM-TOPO-*` code（通过 `kernel.diagnostics().get(result.diagnostic_id)`）
- 测试隔离：
  - 新增测试尽量放在独立 `{ ... }` 事务块中，最后 `rollback()`，避免影响后续“事务可观测性”计数断言。

### 5) 构建与门禁

推荐命令（与 `AGENTS.md` 一致）：

```text
cmake -S . -B build -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON
cmake --build build
ctest --test-dir build -R axiom_topology_test --output-on-failure
ctest --test-dir build --output-on-failure
```

## 常见坑与处理

- **规则加严导致既有测试“隐式依赖”失败**：把新测试隔离成独立事务，并检查是否影响 shared state（例如 created_*_count）。
- **错误码只加在文档/头文件，未在失败路径返回**：必须在 `failed_result/failed_void` 使用对应 `diag_codes`，并在测试断言。
- **trim bridge 过度强制导致 Stage 2 不兼容**：best-effort 校验需在“可判定”时才硬失败；不可判定要么跳过要么返回更明确的“缺失绑定/不完整”错误码。

