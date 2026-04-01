# AxiomKernel 项目结构与文档治理建议

本文档基于当前仓库中的 `include/axiom/**`、`src/axiom/**`、`tests/**`、`cmake/**` 与 `docs/**` 做结构盘点，目标是把“项目不足、结构漂移、文档失真、下一步治理动作”集中到一个可执行入口。

## 1. 结论摘要

当前仓库已经具备可持续开发的模块化骨架、聚合门面和多条 `ctest` 门禁，但仍存在 4 类需要优先治理的问题：

1. **测试入口仍在继续细化**：`heal` 已有独立测试入口，但 `ops` 与 `heal` 的剩余交叉场景仍需要持续按边界收敛。
2. **文档角色混杂**：`docs/plan/AxiomKernel_当前开发进度.md` 同时承担事实快照、需求差距、backlog 与 changelog，维护成本偏高。
3. **测试口径与文档口径未完全对齐**：文档强调独立单元/组件测试，仓库现状仍以链接 `axiom_kernel` 的聚合门禁为主。
4. **部分 API 文档对外承诺大于当前实现深度**：尤其体现在 IO、Rep、Ops、Heal 等模块的“接口齐全但实现仍为占位/近似/回退”场景。

## 2. 已确认优势

- `include/axiom/**`、`src/axiom/**`、`tests/**`、`cmake/AxiomKernelLibraries.cmake` 已形成清晰的模块名对齐关系。
- `axiom_kernel` 作为聚合入口，使 smoke、workflow 与跨模块回归具备统一链接面。
- `diag`、`plugin/sdk`、`math` 已形成较清晰的“实现 + 回归”闭环。
- `BOOL/HEAL/IO` 已建立首批阶段化诊断与 JSON 导出链路，具备继续深化的工程基础。

## 3. 主要不足

### 3.1 结构层

- `core` 的公开头主要位于 `include/axiom/core/**`，而运行时主体长期落在 `src/axiom/internal/core/**`，对新贡献者的目录直觉不够友好。
- 大量业务逻辑分散在 `src/axiom/internal/**/*.inc`，模块实现路径清晰，但阅读路径不够清晰。

### 3.2 文档层

- 《当前开发进度》过长，且“事实、判断、风险、计划、历史”耦合在同一文件。
- 部分 API 样例与真实代码不一致，例如插件样例中的 `Result<T>` 返回方式。
- 质量文档对测试层级的表述偏理想化，需要显式说明“当前阶段以聚合门禁为主，逐步补充更细粒度测试”。

### 3.3 测试层

- 缺少 `tests/core/` 这类与模块同构的独立目录。
- `heal` 已拆到 `tests/heal/heal_test.cpp`，但仍有部分 ops/heal 交叉工作流保留在 `tests/ops/ops_heal_test.cpp`。
- `BOOL` 失败路径、`IO` 标准互操作失败链路、`core` 的 reset/clear 工程化不变量仍缺更集中的专项门禁。

### 3.4 模块完成度层

- `diag`、`plugin/sdk`、`math` 处于“基础可回归”状态。
- `geo`、`topo`、`rep`、`io`、`heal`、`eval` 处于“部分完成”，主要问题是工业级算法深度与规则完备性不足。
- `ops` 是当前最大缺口，尤其是布尔闭环、真实特征建模与圆角/倒角工业化。

## 4. 治理决策

### 4.1 目录真源决策

- **公开 API 真源**：`include/axiom/**`
- **实现真源**：`src/axiom/**`
- **内部私有实现真源**：`src/axiom/internal/**`
- **测试真源**：`tests/**`
- **构建真源**：`cmake/AxiomKernelLibraries.cmake` 与根 `CMakeLists.txt`

任何平行目录（例如 `src/<module>/`）若未纳入构建，均视为**结构漂移候选**，需要在工作区清理前先确认是否仍被人工使用。

### 4.2 文档分层决策

- **事实快照** 放在 `docs/plan/AxiomKernel_当前开发进度.md`
- **结构/治理问题** 放在本文档
- **路线图与阶段退出标准** 放在 `docs/plan/AxiomKernel_主开发计划与阶段路线图.md`
- **接口说明与样例** 放在 `docs/api/**`
- **测试口径与质量门禁** 放在 `docs/quality/**`

### 4.3 测试口径决策

- 当前阶段继续保留 `axiom_kernel` 聚合门禁，确保跨模块链路不退化。
- 后续逐步增加与模块同构的更细粒度专项测试，但在落地前，文档必须明确“当前现实门禁是什么”。

## 5. 优先级 backlog

| 优先级 | 类别 | 问题 | 动作 | 依赖 | 建议 `ctest` |
|--------|------|------|------|------|--------------|
| P0 | 文档 | 进度文档角色过多 | 以 `docs/README.md` + 本文档做分流，后续再拆 changelog/backlog | 无 | 无 |
| P0 | 文档/API | 插件样例失真 | 样例与 `examples/minimal_plugin.cpp`、`Result<T>` 真实用法对齐 | core/plugin/sdk | `axiom_plugin_sdk_test` |
| P1 | 质量 | 测试文档与实际门禁不完全一致 | 在质量文档中补充“当前阶段以聚合门禁为主”的说明 | 无 | 无 |
| P1 | 测试结构 | 继续收紧 `ops` / `heal` 边界 | 将更多纯验证/修复场景固化到 `tests/heal/`，保留 `ops` 侧工作流交叉回归 | heal/ops/io | `axiom_heal_test`、`axiom_ops_heal_test`、`axiom_io_workflow_test` |
| P1 | core | reset/clear 不变量回归不足 | 增加 `tests/core/` 或补充 smoke 子集断言 | core/eval/topo | `axiom_smoke_test` |
| P1 | BOOL | 失败路径 stage 覆盖不集中 | 强化失败链路与阶段诊断门禁 | diag/ops | `axiom_boolean_workflow_test`、`axiom_boolean_prep_test` |
| P2 | topo | trim bridge 与 Strict 规则说明分散 | 在进度文档与接口文档中统一“最小实现 vs 工业级差距”措辞 | geo/topo | `axiom_topology_test` |
| P2 | io/rep | API 深度承诺偏大 | 把“工业标准支持”与“Axiom 子集支持”写清楚 | io/rep | `axiom_io_workflow_test`、`axiom_representation_io_test` |

## 6. 完成定义（DoD）

### 6.1 结构治理项

- 目录真源唯一，构建脚本与文档表述一致。
- 不再存在“未参与构建但名称极易混淆”的平行源码目录。
- 新贡献者仅根据 `include/axiom/**`、`src/axiom/**`、`tests/**` 即可定位主要实现路径。

### 6.2 文档治理项

- 事实、路线图、接口、质量文档各有稳定归属。
- 示例代码可直接作为“接近可编译”的参考，不再误导使用者。
- 关键占位实现会在文档中明确标注为“部分完成”而非“已完成”。

### 6.3 测试治理项

- 高风险链路至少具备“成功 + 边界 + 失败 + 诊断”四类断言。
- 文档列出的 `ctest` 与 CMake 注册清单保持一致。
- 关键模块能说明自己的主回归入口，即使暂未拆成独立 target。

## 7. 本轮建议的安全边界

当前工作区已存在多处源码与文档修改，本轮优先做以下安全动作：

- 新增文档入口和治理文档
- 修正文档中已确认失真的样例
- 修正计划文档与质量文档中的明显口径偏差

以下动作建议留到专门的结构整理提交中处理：

- 大规模重排 `tests/**` 目录
- 拆分 `internal/**/*.inc` 为更多实体文件
