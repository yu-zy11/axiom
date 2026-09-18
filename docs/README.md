# AxiomKernel 文档中心

本目录是项目文档的统一入口。文档按“为什么做 → 做什么 → 如何设计 → 如何实现与验证 → 如何发布”的链路组织，避免需求、计划、实现状态混写。

## 1. 按角色开始

| 角色 / 任务 | 首读 | 然后阅读 |
|---|---|---|
| 新开发者 | [开发者工作流](guides/AxiomKernel_开发者工作流.md) | [架构总览](architecture/AxiomKernel_几何引擎技术架构文档.md)、根目录 `AGENTS.md` |
| 自动开发维护者 | [Agent 自动开发指南](guides/AxiomKernel_Agent自动开发指南.md) | [需求追踪矩阵](requirements/AxiomKernel_需求追踪矩阵.md)、[近期 Backlog](plan/AxiomKernel_近期迭代与Backlog.md) |
| 产品与项目负责人 | [功能需求](requirements/AxiomKernel_几何引擎功能需求文档.md) | [需求追踪矩阵](requirements/AxiomKernel_需求追踪矩阵.md)、[路线图](plan/AxiomKernel_主开发计划与阶段路线图.md) |
| 模块开发者 | [模块依赖](architecture/AxiomKernel_模块依赖图与时序图.md) | [接口清单](api/AxiomKernel_详细模块接口清单.md)、[测试与验收](quality/AxiomKernel_测试与验收方案.md) |
| 评审者 | [合并请求模板](templates/AxiomKernel_合并请求模板.md) | [代码评审清单](templates/AxiomKernel_代码评审清单模板.md)、[决策记录](decisions/README.md) |
| 发布负责人 | [发布与回滚手册](operations/AxiomKernel_发布与回滚手册.md) | [发布说明模板](templates/AxiomKernel_发布说明模板.md)、[当前进度](plan/AxiomKernel_当前开发进度.md) |
| 集成方 | [接口调用样例](api/AxiomKernel_接口调用样例集.md) | [插件样例](api/AxiomKernel_插件开发样例集.md)、[诊断字典](diagnostics/AxiomKernel_错误码与诊断码字典.md) |

## 2. 信息架构与单一事实来源

| 层 | 回答的问题 | 目录 / 真源 | 更新触发条件 |
|---|---|---|---|
| 需求 | 为什么做、必须具备什么、如何验收 | `docs/requirements/` | 范围、优先级或验收标准变化 |
| 计划 | 何时做、当前做到哪里、下一步是什么 | `docs/plan/` | 迭代规划、里程碑或状态变化 |
| 架构 | 系统如何分层、依赖为何如此 | `docs/architecture/` | 模块边界、数据流或重要约束变化 |
| 决策 | 某个关键选择为何被接受 | `docs/decisions/` | 难以逆转或跨模块的技术决策 |
| API | 对外契约及调用方式 | **代码真源** `include/axiom/**`；说明位于 `docs/api/` | Public API 或示例变化 |
| 开发指南 | 如何构建、修改和交付 | `AGENTS.md` 与 `docs/guides/` | 工具链或日常流程变化 |
| 质量 | 如何测试、衡量和验收 | **注册真源** `CMakeLists.txt`；策略位于 `docs/quality/` | 测试入口、门禁或数据集变化 |
| 诊断 | 失败如何编码和呈现 | **代码真源** `include/axiom/diag/error_codes.h`；字典位于 `docs/diagnostics/` | 诊断码、语义或文案变化 |
| 运维发布 | 如何发布、验证和回滚 | `docs/operations/` | 制品、兼容策略或发布流程变化 |
| 模板 | 如何一致地提交交付物 | `docs/templates/` | 团队流程变化 |

冲突时按以下优先级判断：**可执行代码/构建配置 > 已接受 ADR > 需求与架构规范 > 计划和进度快照 > 样例与说明**。发现冲突不能只修改低优先级文档掩盖问题，应在同一 MR 中修复或明确记录差异。

## 3. 核心文档地图

### 3.1 需求与交付追踪

- [功能需求文档](requirements/AxiomKernel_几何引擎功能需求文档.md)：产品边界、能力需求、非功能需求和验收目标。
- [需求追踪矩阵](requirements/AxiomKernel_需求追踪矩阵.md)：把 FR/NFR 连接到模块、里程碑、测试证据与当前状态。
- [当前开发进度](plan/AxiomKernel_当前开发进度.md)：当前事实快照；不得把目标能力写成已交付。
- [近期迭代与 Backlog](plan/AxiomKernel_近期迭代与Backlog.md)：近期任务唯一入口。
- [主路线图](plan/AxiomKernel_主开发计划与阶段路线图.md)：长期阶段与退出标准。

### 3.2 架构、接口与决策

- [技术架构](architecture/AxiomKernel_几何引擎技术架构文档.md)与[模块依赖图](architecture/AxiomKernel_模块依赖图与时序图.md)。
- [项目结构与文档治理](architecture/AxiomKernel_项目结构与文档治理建议.md)：文档生命周期、维护责任和完成定义。
- [ADR 索引](decisions/README.md)：架构决策状态与模板。
- [详细模块接口清单](api/AxiomKernel_详细模块接口清单.md)：便于阅读的接口说明；签名仍以头文件为准。

### 3.3 质量、诊断与发布

- [测试与验收方案](quality/AxiomKernel_测试与验收方案.md)、[性能管理规范](quality/AxiomKernel_基准数据集与性能管理规范.md)。
- [错误码与诊断码字典](diagnostics/AxiomKernel_错误码与诊断码字典.md)。
- [发布与回滚手册](operations/AxiomKernel_发布与回滚手册.md)。

## 4. 文档状态与维护约定

核心规范文档顶部应逐步采用以下元数据；新文档必须提供：

```yaml
状态: 草案 | 评审中 | 已接受 | 已废弃
责任域: Core | Math | Geo | Topo | Rep | Ops | Heal | Eval | IO | Plugin | SDK | Project
维护者: 对应模块负责人或项目负责人
最后核验: YYYY-MM-DD
核验依据: 代码、测试、ADR 或会议决议的路径/编号
```

- **目标**、**当前事实**、**提案**必须使用明确措辞，不能用“支持”同时表示三种状态。
- 状态只在[当前开发进度](plan/AxiomKernel_当前开发进度.md)维护；需求文档只定义目标，backlog 只管理待办，变更纪要只记录历史。
- 同一事实只设一个维护入口，其他文档使用链接，不复制大段清单。
- 每个需求使用稳定 ID；实现 MR 应在[追踪矩阵](requirements/AxiomKernel_需求追踪矩阵.md)补充测试证据。
- 文档改动也要通过链接检查和评审；命令见[开发者工作流](guides/AxiomKernel_开发者工作流.md)。

## 5. 变更影响速查

| 代码变更 | 必查文档 |
|---|---|
| Public API | `docs/api/`、需求追踪矩阵、调用样例、发布兼容性说明 |
| 模块/依赖 | 架构图、ADR、根 `AGENTS.md`、构建说明 |
| 错误码/诊断阶段 | 诊断字典、用户文案、相关测试、追踪矩阵 |
| 新测试或门禁 | 测试方案、开发工作流、当前进度 |
| IO 格式/策略 | IO 策略矩阵、兼容性说明、数据集与发布手册 |
| 里程碑状态 | 当前进度、近期 backlog、追踪矩阵；不要回写需求目标 |
