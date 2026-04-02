# AxiomKernel 文档导航

本文档用于把 `docs/` 下的内容按“事实 / 规划 / 规格 / 质量 / 模板”分层，减少在单一长文档中同时查状态、查路线图、查接口的成本。

## 1. 推荐阅读顺序

### 1.1 想看“项目现在真实做到哪里了”

1. `docs/plan/AxiomKernel_当前开发进度.md`
2. `docs/architecture/AxiomKernel_项目结构与文档治理建议.md`
3. `AGENTS.md`

### 1.2 想看“后面准备怎么做”

1. `docs/plan/AxiomKernel_主开发计划与阶段路线图.md`
2. `docs/plan/AxiomKernel_MVP实施蓝图.md`
3. `docs/plan/AxiomKernel_几何引擎功能需求文档.md`
4. `docs/plan/AxiomKernel_近期迭代与Backlog.md`
5. `docs/plan/AxiomKernel_变更纪要.md`
6. `docs/plan/AxiomKernel_STEP_IGES_标准交换实施路线.md`（标准 STEP/IGES 与当前子集边界、里程碑）

### 1.3 想看“公开接口和调用方式”

1. `docs/api/AxiomKernel_详细模块接口清单.md`
2. `docs/api/AxiomKernel_接口调用样例集.md`
3. `docs/api/AxiomKernel_插件开发样例集.md`

### 1.4 想看“测试、质量和发布约束”

1. `docs/quality/AxiomKernel_测试与验收方案.md`
2. `docs/quality/AxiomKernel_IO_导出策略矩阵.md`（`ExportOptions` 与网格导出门禁口径）
3. `docs/quality/AxiomKernel_基准数据集与性能管理规范.md`
4. `docs/templates/AxiomKernel_合并请求模板.md`
5. `docs/templates/AxiomKernel_代码评审清单模板.md`

## 2. 单一事实来源

以下内容以代码和构建脚本为准，文档必须追随它们更新：

- 公开 API：`include/axiom/**`
- 实现落点：`src/axiom/**`
- 内部实现细节：`src/axiom/internal/**`
- 测试入口：`tests/**`
- 模块 target 与依赖方向：`cmake/AxiomKernelLibraries.cmake`
- `ctest` 注册清单：`CMakeLists.txt`

## 3. 文档分层约定

- `docs/plan/`：阶段状态、路线图、需求对照、backlog
- `docs/plan/` 中建议分工：`当前开发进度` 负责事实快照，`主开发计划与阶段路线图` 负责长期路线，`近期迭代与Backlog` 负责近期执行项，`变更纪要` 负责批次历史摘要
- `docs/architecture/`：模块边界、依赖、结构治理、架构决策
- `docs/api/`：对外接口、样例、集成方式
- `docs/quality/`：测试、性能、验收、数据集
- `docs/templates/`：MR、评审、发布、缺陷处理模板
- `docs/diagnostics/`：错误码、诊断码、用户可读文案

## 4. 当前已知治理重点

- 进度文档同时承载事实快照、需求差距、backlog 和 changelog，维护成本偏高。
- 测试文档强调“独立单元测试”，而仓库现状以聚合 `axiom_kernel` 门禁为主，需要明确阶段差异。
- `heal` 已拆出独立测试入口，但 `ops` 与 `heal` 的边界仍可继续细化。
- 插件样例与部分接口说明需要持续对齐真实 `Result<T>` / `KernelConfig` 结构。

## 5. 更新规则

- 变更公开接口时，同时检查 `docs/api/` 与 `docs/plan/`。
- 新增测试 target 时，同时更新 `AGENTS.md`、`docs/quality/` 与进度文档中的 `ctest` 清单。
- 清理结构漂移目录后，及时更新治理文档，避免把“已解决问题”继续留在已知风险列表中。
- 新增错误码或诊断阶段时，同时更新 `docs/diagnostics/` 与对应 workflow 文档。
- 发现“接口存在但实现仍占位”时，在文档中明确标注为“部分完成”，不要写成“已完成”。
