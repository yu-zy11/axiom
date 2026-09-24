# AxiomKernel 需求追踪矩阵

> 状态：已接受
> 责任域：Project
> 维护者：产品负责人、架构负责人、质量负责人
> 最后核验：2026-09-18
> 核验依据：[功能需求](AxiomKernel_几何引擎功能需求文档.md)、[当前进度](../plan/AxiomKernel_当前开发进度.md)、`CMakeLists.txt`

## 1. 使用方式

本矩阵是需求到工程证据的索引，不替代需求正文或状态详情。实现 MR 应引用稳定 ID，并更新“证据”和“状态”；没有满足完整验收标准时不得标记“已满足”。状态采用[治理规范](../architecture/AxiomKernel_项目结构与文档治理建议.md#5-需求到交付闭环)中的统一定义。

## 2. 功能需求追踪

| ID | 需求域 | 责任模块 | 目标阶段 | 当前状态 | 自动化证据 | 主要缺口 / 下一验收点 |
|---|---|---|---|---|---|---|
| FR-GEO-001 | 几何对象、求值与变换 | Math, Geo | Stage 2 | 受限可用 | `axiom_math_services_test`, `axiom_geometry_test` | 已补曲线及曲面双轴显式样条结点校验、高重数断点及非夹持重复端点单侧求值；第 16/26 切片分别使曲线非空结点分段、曲面非空张量积结点片参与最近点初值搜索；第 21 切片拒绝曲面零有效域与超限重数并保证失败不污染；第 31 切片固定曲面满重数断点的一二阶偏导与曲率单侧语义；第 36 切片使椭圆最近参数按欧氏距离阻尼细化、归一周期缝并拒绝创建时派生法向溢出；第 41 切片使 UV 折线 PCurve 最近参数逐段投影，覆盖短分段与重复点；仍需扩展曲线/曲面类型及全局精度语义 |
| FR-TOPO-001 | 拓扑实体、关系与一致性 | Topo | Stage 2 | 受限可用 | `axiom_topology_test`（含非有限顶点、单共边未闭合拒绝、第 12 切片建面跨环复用边拒绝、第 17 切片闭合壳共边反向配对、第 22 切片闭合壳单连通分量门禁、第 27 切片边端点与 3D Curve 一致性、第 32 切片环中间重复顶点拒绝、第 37 切片建面前拒绝边数不足的外/内环、第 42 切片建面前拒绝跨环共用顶点、退化输入、诊断 JSON、失败不污染与回滚）, `axiom_kernel_runtime_invariant_test` | 完整一致性规则、trim bridge、持久命名 |
| FR-OPS-001 | 基础体与特征构造 | Ops, Geo, Topo | Stage 3 | 进行中 | `axiom_smoke_test`, `axiom_ops_heal_test` | 真实特征拓扑及跨模块验收模型集 |
| FR-BOOL-001 | 布尔并/交/差与阶段诊断 | Ops, Heal | Stage 4 | 进行中 | `axiom_boolean_prep_test`, `axiom_boolean_workflow_test` | 工业退化场景、精确切分/分类/重建成功率 |
| FR-MOD-001 | 偏置、抽壳与直接编辑 | Ops, Heal | Stage 6 | 未开始 | — | 先定义最小输入域、事务和验证门禁 |
| FR-BLEND-001 | 圆角与倒角 | Ops, Heal | Stage 6 | 未开始 | — | 常半径最小闭环及失败阶段诊断 |
| FR-QUERY-001 | 几何/拓扑查询与分析 | Geo, Topo, Eval | Stage 3 | 进行中 | `axiom_query_eval_test` | 明确精度、单位、空结果和失效语义 |
| FR-HEAL-001 | 验证与修复 | Heal, Topo, Rep | Stage 5 | 进行中 | `axiom_heal_test`, `axiom_ops_heal_test` | 扩充规则集、修复前后不变量和语料库 |
| FR-IO-001 | 数据导入导出 | IO, Rep, Heal | Stage 5 | 受限可用 | `axiom_io_workflow_test`, `axiom_io_dataset_test` | 标准 STEP/IGES 深度、兼容矩阵和 round-trip 预算 |
| FR-REP-001 | 三角化、表示与转换 | Rep, Geo, Topo | Stage 5 | 受限可用 | `axiom_representation_io_test` | 误差预算、属性保真及局部增量更新 |
| FR-EVAL-001 | 版本、事务与增量失效 | Core, Topo, Eval | Stage 7 | 进行中 | `axiom_kernel_runtime_invariant_test`, `axiom_query_eval_test` | 公共事务合同、取消/重放、并发调度 |
| FR-PLUGIN-001 | 插件能力发现与隔离 | Plugin, SDK | Stage 8 | 受限可用 | `axiom_plugin_sdk_test` | ABI 生命周期、沙箱/资源策略及兼容测试 |
| FR-DIAG-001 | 结构化诊断与导出 | Diagnostics, all | 横向 | 受限可用 | `axiom_diagnostics_test`（批量 JSON 完整证据、转义；第 14 切片批量文本完整问题证据；第 24 切片阶段聚合导出的正常/空阶段、参数/打开/设备写入失败、文件/源报告不污染与重试；第 34 切片单报告 TXT/JSON 的成功证据、参数/打开/设备写入失败、源报告不污染与重试；第 39 切片阶段检索限额前排序、非法参数及源报告不污染）、`axiom_boolean_prep_test`（布尔预处理告警阶段/实体/JSON、退化与前置失败不污染）、`axiom_io_workflow_test`（严格网格导出 QA；第 29 切片 STEP 导出输入/路径/写入失败的阶段、Body、JSON、失败不污染与重试） | 全部重量级流程的阶段、实体和数值证据 |
| FR-HYBRID-001 | B-Rep/Mesh/Implicit 混合表示 | Rep, Ops | Stage 8 | 进行中 | `axiom_representation_io_test` | Implicit 表示、局部双向转换和误差合同 |

“受限可用”仅指仓库已测试的最小子集，不代表达到需求文档所述工业成熟度。详细限制以[当前开发进度](../plan/AxiomKernel_当前开发进度.md)为准。

## 3. 非功能需求追踪

| ID | 需求 | 当前状态 | 衡量方式 | 当前证据 | 发布门禁 |
|---|---|---|---|---|---|
| NFR-REL-001 | 失败不污染、事务一致性 | 受限可用 | 失败注入后句柄/存储/拓扑不变量 | runtime invariant、boolean/heal 测试；`axiom_topology_test` 覆盖已有共边 PCurve 绑定/清除的回滚、无效句柄拒绝与失败不污染；活动事务清理跟踪记录拒绝、创建/删除/PCurve 快照保留及关闭后幂等清理；第 13 切片覆盖新建面/壳/体修改与删除后回滚不复活；第 18 切片覆盖事务对象唯一所有权；第 23 切片覆盖活动事务析构自动回滚；第 28 切片覆盖仅 `replace_surface` 写入的回滚与提交审计；第 33 切片强制同一内核单活动写事务，重叠事务关闭且不能写入、提交或回滚所有者状态；第 38 切片以受损壳注入验证建体失败不消耗实体 ID，诊断、计数、回滚及重试均不污染 | S0/S1 回归全通过；继续闭合协作式取消及更细粒度隔离语义 |
| NFR-DIA-001 | 可解释失败 | 受限可用 | 稳定错误码、`diagnostic_id`、阶段和问题实体 | diagnostics 及 workflow 测试；`axiom_boolean_prep_test` 覆盖诊断开关两种模式下的早期失败阶段、输入实体、检索/JSON 与失败不污染；第 10 切片补齐非法 BooleanOp 拒绝及合法枚举回归，拒绝不触发 Eval 失效传播；第 15 切片覆盖预处理统计导出输入/打开/写入失败阶段、实体、检索/JSON、参数失败文件及模型不污染、Linux `/dev/full` 拒绝假成功；第 20 切片为 `validate_geometry` 全部分支绑定 `heal.validate_geometry.*` 阶段及目标 Body/问题子实体；第 25 切片为体/壳/批量壳自交验证失败绑定 `heal.validate_self_intersection.*` 阶段及 Body/Shell；第 30 切片为 STEP 导入的空路径、缺失文件与非可读常规文件绑定 `io.import.step.input/path/open`；第 35 切片为 OBJ 物化前输入/路径/打开/解析/退化验证失败绑定 `io.import.obj.*`；第 40 切片为 STL 物化前输入/路径/打开/读取/解析/网格验证失败绑定 `io.import.stl.*`，`axiom_io_workflow_test` 覆盖阶段检索、JSON、Body/Mesh 不污染及重试 | 支持范围内无静默失败 |
| NFR-PERF-001 | 可重复性能基线 | 进行中 | 总耗时、均值/P95、数据集/环境元数据 | `axiom_perf_baseline_test` | 不超过批准阈值；回退有批准记录 |
| NFR-COMP-001 | API/格式兼容性 | 进行中 | 版本策略、编译兼容、round-trip | plugin 与 IO 测试 | 破坏性变化有迁移说明和版本决策 |
| NFR-OBS-001 | 运行时可观测 | 受限可用 | 结构化运行时快照与关键计数 | runtime invariant 测试 | 发布制品可导出诊断快照 |
| NFR-SEC-001 | 不可信输入和插件边界 | 进行中 | 资源上限、解析失败、插件策略测试 | 部分 plugin/IO 测试 | 面向生产前完成威胁模型与恶意语料测试 |

## 4. 维护规则

1. 新需求先分配 ID，再进入路线图或 backlog。
2. 状态改变必须附可重复命令、测试名或制品链接。
3. 需求拆分后保留原 ID 的替代关系，避免历史 MR 失联。
4. 需求取消时标记“已废弃”并在 ADR 或计划中记录原因。
5. 每个里程碑由产品、架构、质量三方复核本矩阵。
