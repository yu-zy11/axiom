---
name: axiom-plugin-sdk-dev
description: Develop and harden AxiomKernel Plugin/SDK surface (Kernel facade stability, plugin registry, manifests/capabilities, examples, and regression strategy). Use when adding plugin APIs, changing Kernel public surface, introducing compatibility policies, or creating end-to-end examples/tests.
---

# AxiomKernel Plugin/SDK 开发（Kernel/Registry/Compatibility）

## 适用场景（触发词）

- `Kernel` 门面 API 调整、向后兼容策略、能力报告（modules/services/formats）
- 插件注册/清单/能力统计、错误处理与诊断
- 示例与回归：用最小 E2E 工作流验证“可用服务”组合不回归

## 代码入口

- SDK：`include/axiom/sdk/kernel.h` + `src/sdk/kernel.cpp`
- Plugin：`include/axiom/plugin/plugin_registry.h` + `src/plugin/plugin_registry.cpp`
- 关键回归：`tests/sdk/smoke_test.cpp`、`tests/diag/diagnostics_test.cpp`（以及任何新增的 E2E 测试）

## 硬规则（必须遵守）

- **门面稳定**：能不破坏现有 public API 就不要破坏；确需变更要提供迁移路径并补回归。
- **能力报告真实**：对外宣称“支持/可用”的服务与格式必须与实现一致（否则修正或显式标注为“实验性”并测试固化）。
- **插件失败可诊断**：注册/加载/查询失败必须可定位（status + 诊断）。

## 默认工作流

### 1) 变更分类

- **兼容变更**：新增字段/新增接口/默认行为不变
- **不兼容变更**：删除/改签名/改变默认语义（必须补迁移说明与回归）

### 2) 测试优先：最小 E2E

至少覆盖：

- `Kernel` 能列出 modules/services/formats（且与实际一致）
- 插件清单/能力统计查询不崩溃、结果稳定

## DoD（完成定义）

- **实现**：门面与 registry 行为稳定可解释
- **诊断**：插件与门面关键失败路径可定位
- **测试**：最小 E2E 回归覆盖能力报告与插件查询

