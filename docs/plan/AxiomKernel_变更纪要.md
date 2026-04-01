# AxiomKernel 变更纪要

本文档用于把《当前开发进度》中的长历史条目压缩成“按批次阅读”的结构化纪要，降低单文档持续膨胀的维护成本。详细事实与完整模块状态仍以 `docs/plan/AxiomKernel_当前开发进度.md` 为准。

## 1. 使用原则

- 这里记录的是**批次摘要**，不是逐提交日志。
- 只记录会影响模块完成度、测试门禁、文档口径或结构治理的重要变化。
- 更细粒度的实现演进仍可暂时保留在进度文档中，但后续应逐步把新增历史优先沉淀到这里。

## 2. 最近关键批次

### 2.1 基线与门面稳定化

- 工程从“文档骨架”推进到“可持续编译与回归”的代码基线。
- `Kernel` 门面、`Result`/诊断/错误码主链路打通。
- smoke、示例与多专项 `ctest` 形成持续门禁。

### 2.2 Geometry / Topology 最小闭环

- 基础曲线/曲面求值、包围盒、最近点/参数等语义逐步从纯占位推进到可回归最小实现。
- `TopoCore` 增加事务、回滚、反向邻接与 Strict 校验的首批闭环。
- 结果体开始区分 `owned topology` 与 `source topology`，为后续真实重建留出语义空间。

### 2.3 Ops / Heal / Validation 工作流增强

- 布尔前置、近似求交、来源传播、最小物化拓扑骨架与 Strict 回归形成首批闭环。
- 修复与验证链路增加自动修复、修复前后问题回流、`related_entities` 追踪。
- `BOOL/HEAL/IO` 开始形成阶段化诊断与工作流级回归。

### 2.4 IO / Rep / Eval 工程化增强

- `STEP/AXMJSON` 主链路可回归，`STL/glTF` 与 `IGES/BREP/OBJ/3MF` 的 Axiom 子集互操作已接入。
- 表示层增加网格检查、报告导出、BRep/Mesh 基础 round-trip 语义。
- `EvalGraph` 增加循环保护、依赖治理、批量失效与重算能力。

### 2.5 2026-04 结构收敛

- 代码实现路径按 `src/axiom/<module>/` 与 `include/axiom/<module>/` 对齐。
- `cmake/AxiomKernelLibraries.cmake` 与文档路径引用已同步到新结构。
- 当前仍观察到 `src/math/` 漂移迹象，后续需要做单独清理。

### 2.6 2026-04 文档治理批次

- 新增 `docs/README.md` 作为文档导航入口。
- 新增 `docs/architecture/AxiomKernel_项目结构与文档治理建议.md` 作为结构治理与 backlog 汇总入口。
- 插件样例、质量文档、阶段路线图与进度文档的口径做了一轮对齐。
- 将“近期迭代与 backlog”单独拆到 `docs/plan/AxiomKernel_近期迭代与Backlog.md`。

### 2.7 2026-04 结构清理补充

- 删除未参与构建的 `src/math/math_internal_utils.cpp`，统一 `MathCore` 源码真源为 `src/axiom/math/**`。
- 将纯验证/修复回归从 `tests/ops/ops_heal_test.cpp` 拆到 `tests/heal/heal_test.cpp`。
- `axiom_ops_heal_test` 继续保留跨模块 ops/heal 工作流语义，`axiom_heal_test` 承载独立 HealCore 回归。

## 3. 仍需继续拆分的历史负担

- 《当前开发进度》中的长条目 changelog 仍偏长，后续新增历史应优先写入本文件。
- 一些“能力已实现但仍属过渡语义”的历史项需要继续按“基础可回归 / 部分完成 / 未开始”重新归类。
- 若后续引入结构级源码整理（例如清理漂移目录、拆测试 target），应单独记录成新的治理批次。
