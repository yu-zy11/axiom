# AxiomKernel 近期迭代与 Backlog

本文档承接《当前开发进度》中的“近期执行项”，把**当前迭代焦点、本阶段 backlog、下一未闭合批次、长期能力树**集中到一个更适合持续刷新的入口。事实状态仍以 `docs/plan/AxiomKernel_当前开发进度.md` 为准；总路线见 `docs/plan/AxiomKernel_主开发计划与阶段路线图.md`。

## 1. 当前阶段口径

当前阶段保持为：

`Stage 1.5 / Stage 2 过渡：核心公共层增强 + 基础几何/拓扑深化`

## 2. 当前迭代焦点

### 2.1 `diag + ops`

- 扩展 BOOL 阶段化诊断覆盖与 JSON `stage` 字段在更多失败分支中的一致性。
- 将布尔真求交子里程碑拆为“面级候选 -> 交线 -> imprint”可回归切片。

**DoD**

- 对应错误码/诊断码可检索。
- `axiom_boolean_workflow_test` 与 `axiom_boolean_prep_test` 不回归。

### 2.2 `math + geo`

- 收敛退化/大尺度下的谓词与容差行为定义。
- 推进样条导数/曲率或稳定最近点的最小增量。

**DoD**

- 失败具备稳定错误码。
- `axiom_math_services_test` 与 `axiom_geometry_test` 覆盖新增语义。

### 2.3 `topo`

- 继续加严 Strict 规则。
- 推进 trim bridge 的可物化子规则。

**DoD**

- `axiom_topology_test` 有新增失败类回归。
- 对应诊断码可稳定导出。

### 2.4 `heal + io`

- 在导入侧 mesh 工作流与验证项中继续闭合“自交/流形性/修复追溯”。

**DoD**

- `axiom_heal_test`、`axiom_io_workflow_test` 与 `axiom_ops_heal_test` 增量断言通过。
- `related_entities` 保持可追踪。

## 3. 本阶段 backlog 表

| 优先级 | 状态 | 模块 | 交付物（摘要） | 建议 `ctest` | 依赖 |
|--------|------|------|----------------|--------------|------|
| P0 | 已闭合（门禁） | core/io | 门面 IO 能力与 `IOService` 一致 | `axiom_smoke_test` | — |
| P0～P1 | 已闭合（首批） | diag/ops/io/heal | 工作流 `Issue.stage` + JSON 导出可聚合 | `axiom_diagnostics_test`、`axiom_boolean_workflow_test`、`axiom_heal_test`、`axiom_ops_heal_test` | core |
| P1 | 进行中 | math | 退化/尺度谓词与容差策略回归 | `axiom_math_services_test` | core |
| P1～P2 | 进行中 | geo/topo | trim / trim bridge / Strict 规则 | `axiom_geometry_test`、`axiom_topology_test` | math |
| P2 | 进行中 | ops | 布尔非 `bbox` 结果子里程碑 | `axiom_boolean_*` | geo/topo |
| P2～P3 | 进行中 | eval/rep | 重算指标、Rep 误差预算 | `axiom_query_eval_test`、`axiom_representation_io_test` | ops（部分） |

## 4. 下一未闭合批次

1. BOOL 全阶段失败路径诊断绑定与工业数据集雏形。
2. 特征建模（拉伸/旋转/扫掠）真实拓扑物化，替代过度依赖最小骨架。
3. HEAL 自交/流形性/容差冲突的验证与可回放修复。
4. IO：标准 IGES/STEP 实体交换或通用 3MF 读写的下一里程碑。
5. EvalGraph 与建模事件的命中率/成本指标门禁。
6. Plugin：OS 级隔离与动态加载安全。

## 5. 长期能力树

1. `core`：统一错误码绑定、配置策略中心、版本与兼容策略。
2. `diag`：诊断聚合检索、批量导出、跨模块追踪链路。
3. `eval`：节点生命周期管理、批量失效/重算、依赖图治理。
4. `math`：鲁棒谓词与尺度自适应公差策略深化。
5. `geo`：高质量样条/反求/曲面参数域与退化处理。
6. `topo`：拓扑一致性规则集、事务可观测性、索引完整性。
7. `rep`：表示转换质量控制、网格统计与检查报告。
8. `io`：多格式导入导出主链路、路径与批处理工程化。
9. `ops`：布尔真实求交/切分/分类/重建闭环。
10. `heal`：验证与修复工业化策略（小边小面/缝合/容差冲突）。
11. `plugin`：插件清单、能力发现、注册治理。
12. `sdk`：门面稳定性、调用一致性与向后兼容。
13. `tests`：单元/集成/回归/性能基线持续补齐。
14. `ci/release`：流水线门禁、发布产物与文档同步机制。

## 6. 使用约定

- 每次迭代开始时优先更新“当前迭代焦点”。
- 已纳入门禁的闭合项留在 backlog 表中，但应与“进行中”区分。
- 新增中长期能力时，优先追加到“长期能力树”，避免把一次性 Sprint 文档写成长篇历史记录。
