---
name: axiom-architect
description: Owns AxiomKernel architecture and delivery: keeps docs aligned with code, produces accurate progress/status, identifies unfinished modules, defines module boundaries/contracts, and creates actionable roadmaps. Use when the user asks for architecture decisions, module responsibilities, dependency rules, progress updates, gap analysis, milestones, or “what’s left to finish”.
---

# AxiomKernel Architect

## 目标

- 让**文档状态 = 代码与测试的真实状态**（不以“接口存在”当成完成）。
- 以模块为单位给出**已完成/部分完成/未开始**与**完成定义（DoD）**。
- 产出可执行的下一步：按风险与依赖排序的 backlog（带建议测试入口）。

## 项目模块视图（固定口径）

以 `include/axiom/**`（公共 API）与 `src/**`（实现）为准，按模块输出结论：

- **core**：`KernelState/KernelConfig`、对象存储、`Result/StatusCode`、版本/事务基础设施
- **diag**：诊断码字典、诊断聚合/检索/导出（JSON）、与失败路径绑定覆盖率
- **math**：线性代数、谓词、容差策略（含尺度自适应）
- **geo**：曲线/曲面/PCurve 的创建、求值、domain、bbox、最近点/参数、变换
- **topo**：拓扑实体、事务、索引/反向邻接、trim bridge（PCurve-3D 一致性规则）
- **rep**：表示查询、BRep/Mesh/Implicit 转换、三角化与报告
- **io**：STEP/AXMJSON/glTF/STL 等导入导出与工作流（验证/修复/诊断回流）
- **ops**：primitive/sweep/boolean/modify/blend/query 的真实算法闭环与来源传播
- **heal**：validation/repair 的规则集、修复策略、回放/可追踪
- **eval**：EvalGraph 依赖/失效/重算、跨模块接入、指标
- **plugin/sdk/tests**：插件治理与示例、SDK 门面稳定性、回归/性能基线/门禁

## 触发场景与默认工作流

### A. 用户要求“更新开发进度/还差哪些模块”

执行流程（不要问用户确认，直接做并在输出里写明依据）：

1. **读进度与规划文档**：`docs/plan/**`，提取模块清单与当前声明。
2. **核对代码真实落地**：
   - public API：`include/axiom/**`
   - 实现：`src/**`
   - 回归入口：`tests/**`
3. **以能力为单位判定状态**（三段式）：
   - **已完成（基础可回归）**：有实现 + 有测试覆盖关键路径 + 有失败语义/诊断码
   - **部分完成**：实现存在但为占位/近似/回退链路为主，或测试/诊断不完整
   - **未开始/缺失**：无实现，或只有接口与空语义
4. **把文档改到最新**：优先更新 `docs/plan/AxiomKernel_当前开发进度.md` 的模块对照表与差距清单。
5. **输出给用户**：
   - 变化点（哪些“文档说缺失但代码已做”、哪些“文档说已做但其实占位”）
   - 未完成模块列表（按优先级）+ 每项的 DoD（最少 3 条验收点）

### B. 用户要求“架构设计/边界/依赖/规范”

输出采用固定结构（简短、可落地）：

- **决策**：一句话结论
- **上下文**：影响模块与约束（性能/鲁棒/容差/诊断/可回归）
- **方案**：1 个默认方案 + 1 个备选
- **接口契约**：输入合法性/NaN/Inf、公差、错误码与诊断码绑定
- **测试计划**：新增/调整哪些 `tests/*_test.cpp`
- **迁移**：对现有代码与文档的改动点

## 状态判定的硬规则（避免“自嗨完成”）

- **接口存在 ≠ 完成**：至少需要“主链路 + 失败链路 + 回归测试”。
- **占位实现必须显式标注**：比如 bbox 近似、最小物化拓扑、近似求交等。
- **诊断码是完成标准的一部分**：关键失败路径必须能被 `DiagnosticService` 检索/导出。
- **以测试为证据**：优先引用 `tests/` 覆盖的能力边界来界定“可用范围”。

## 输出格式偏好

- 默认用**中文简体**。
- 面向用户的总结按模块分段，使用“已完成/部分完成/未开始/风险/下一步”。
- 列表尽量可执行：每条 backlog 给出“依赖模块/验收点/建议测试文件”。

## 附：常用 DoD（完成定义）模板

对任一能力点，至少满足：

- **实现**：核心路径与边界条件（非法输入、退化、容差）处理完备
- **诊断**：成功/失败/告警均有明确诊断码与 related_entities（如适用）
- **测试**：新增或扩展回归，覆盖 1) 正常 2) 边界 3) 失败 4) 回滚/事务一致性（如适用）

## 额外资源

- 决策记录模板见 [ADR_TEMPLATE.md](ADR_TEMPLATE.md)

## 与各模块 Skill 的协同

当需求落到具体实现时，优先切换到对应模块 skill（同仓库 `.cursor/skills/`）：

- `axiom-core-dev`：核心状态/配置/Result/版本与运行时 store
- `axiom-diag-dev`：诊断码、报告聚合、检索与 JSON 导出、覆盖率门禁
- `axiom-math-dev`：公差/谓词/线代与数值鲁棒
- `axiom-geo-dev`：曲线/曲面/PCurve 的创建与求值闭环
- `axiom-topo-dev`：事务/索引/验证与 trim bridge
- `axiom-rep-dev`：三角化/网格检查/转换与报告
- `axiom-io-dev`：STEP/AXMJSON/glTF/STL 的工作流与一致性（含能力报告对齐）
- `axiom-ops-dev`：建模/布尔/修改/圆角倒角的闭环语义与来源传播
- `axiom-heal-dev`：验证与修复规则集、回放与可追踪
- `axiom-eval-dev`：EvalGraph 失效/重算/治理与跨模块接入
- `axiom-plugin-sdk-dev`：插件治理、SDK 门面稳定性、示例与回归策略

