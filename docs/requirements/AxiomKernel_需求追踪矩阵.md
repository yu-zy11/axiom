# AxiomKernel 需求追踪矩阵

> 状态：已接受
> 责任域：Project
> 维护者：产品负责人、架构负责人、质量负责人
> 最后核验：2026-09-30
> 核验依据：[功能需求](AxiomKernel_几何引擎功能需求文档.md)、[当前进度](../plan/AxiomKernel_当前开发进度.md)、`CMakeLists.txt`、`.axiom-agent/logs/cycle-0072-gates.log`

## 1. 使用方式

本矩阵是需求到工程证据的索引，不替代需求正文或状态详情。实现 MR 应引用稳定 ID，并更新“证据”和“状态”；没有满足完整验收标准时不得标记“已满足”。状态采用[治理规范](../architecture/AxiomKernel_项目结构与文档治理建议.md#5-需求到交付闭环)中的统一定义。

## 2. 功能需求追踪

| ID | 需求域 | 责任模块 | 目标阶段 | 当前状态 | 自动化证据 | 主要缺口 / 下一验收点 |
|---|---|---|---|---|---|---|
| FR-GEO-001 | 几何对象、求值与变换 | Math, Geo | Stage 2 | 受限可用 | `axiom_math_services_test`, `axiom_geometry_test`, `axiom_query_eval_test`；第 74 批最终独立完整构建成功、CTest 16/16 通过（0 失败，120.81 s；`axiom_geometry_test` 0.58 s） | 曲线与曲面均公开 `closest_point_detailed`。Plane/Cylinder/Cone/规则 Sphere/Torus 及嵌套 Offset 链支持解析全域求解与无界域自动有限化；Bezier/BSpline/NURBS 以正权有理控制网凸包建立随细分收紧的距离下界，穿透 Trimmed/Offset 并公开剪枝证据。仍缺通用无限派生面自动有限化、spindle/horn 环面解析证书、旋转/扫掠专用局部几何界和大模型独立性能基线 |
| FR-TOPO-001 | 拓扑实体、关系与一致性 | Topo | Stage 2 | 受限可用 | `axiom_topology_test`, `axiom_query_eval_test`, `axiom_kernel_runtime_invariant_test`；第 74 批最终完整 CTest 16/16 通过（0 失败；`axiom_topology_test` 0.13 s，运行时不变量 0.00 s） | 显式 trim 曲边长度/bbox 语义保留；跨环冲突现可复用无诊断 Geo 求交覆盖圆锥曲线、Bezier、BSpline、NURBS 和混合 CompositeChain，支持递减区间、端点接触、容差邻近与可证连续重合；缺 trim/预算/数值失败闭合拒绝。完整 3D trim↔PCurve/曲面参数域一致性、周期缝/奇点与持久命名仍待闭合 |
| FR-OPS-001 | 基础体与特征构造 | Ops, Geo, Topo | Stage 3 | 进行中 | `axiom_smoke_test`, `axiom_ops_heal_test`；第 74 批在不放宽覆盖的前提下优化体验证反向索引核对与闭合扫掠边复用，最终完整 CTest 16/16 通过，`axiom_ops_heal_test` 95.13 s（低于 120 s 限制） | `revolve/revolve_between` 支持轴分离孔洞、偏置/对称起始角、正负部分角与正负整周；`sweep_scaled` 支持开放直线/折线/样条/复合导轨上按弧长线性变化的有限正比例；`loft` 支持拓扑兼容凹/带孔显式截面。仍为保守浮点剖分/采样多面体 BRep，不是解析扫掠/精确旋转；缺带孔尖顶/触轴、零或负比例、非线性比例律、任意环拓扑匹配、分支/坍塌放样、逐壁恒角拔模、嵌套复合导轨及显式轮廓历史 |
| FR-BOOL-001 | 布尔并/交/差与阶段诊断 | Ops, Heal | Stage 4 | 进行中 | `axiom_boolean_prep_test`, `axiom_boolean_workflow_test` | 工业退化场景、精确切分/分类/重建成功率 |
| FR-MOD-001 | 偏置、抽壳与直接编辑 | Ops, Heal | Stage 6 | 未开始 | — | 先定义最小输入域、事务和验证门禁 |
| FR-BLEND-001 | 圆角与倒角 | Ops, Heal | Stage 6 | 未开始 | — | 常半径最小闭环及失败阶段诊断 |
| FR-QUERY-001 | 几何/拓扑查询与分析 | Geo, Topo, Eval | Stage 3 | 进行中 | `axiom_geometry_test`, `axiom_topology_test`, `axiom_query_eval_test`；第 74 批最终完整 CTest 16/16 通过（0 失败，120.81 s；三项分别 0.58/0.13/0.23 s） | `closest_point_detailed` 补齐解析无界面和高阶有理控制网证书；`intersect_curve_curve` 的有界曲线能力已被拓扑跨环预检/建面/验证复用，并公开求解路径、容差和工作量证据。仍缺曲面/曲边闭壳质量积分、一般高阶异参连续重合证明、通用无限派生面证书、独立内壳空腔/相交多壳扩展和大规模加速；`face_area` 的高阶/派生曲面限制不变 |
| FR-HEAL-001 | 验证与修复 | Heal, Topo, Rep | Stage 5 | 进行中 | `axiom_heal_test`, `axiom_ops_heal_test` | 扩充规则集、修复前后不变量和语料库 |
| FR-IO-001 | 数据导入导出 | IO, Rep, Heal | Stage 5 | 受限可用 | `axiom_io_workflow_test`, `axiom_io_dataset_test`；第 70 批 AXMJSON/Axiom IGES 元数据/Axiom BREP JSON 子集导入回归随完整 CTest 16/16 通过 | 仅严格支持内核自身导出的精确 B-Rep 文本子集；标准 IGES 实体仍为 NotImplemented，标准 STEP/IGES 深度、兼容矩阵和 round-trip 预算待闭合 |
| FR-REP-001 | 三角化、表示与转换 | Rep, Geo, Topo | Stage 5 | 受限可用 | `axiom_representation_io_test` | 误差预算、属性保真及局部增量更新 |
| FR-EVAL-001 | 版本、事务与增量失效 | Core, Topo, Eval | Stage 7 | 进行中 | `axiom_kernel_runtime_invariant_test`, `axiom_query_eval_test` | 公共事务合同、取消/重放、并发调度 |
| FR-PLUGIN-001 | 插件能力发现与隔离 | Plugin, SDK | Stage 8 | 受限可用 | `axiom_plugin_sdk_test` | ABI 生命周期、沙箱/资源策略及兼容测试 |
| FR-DIAG-001 | 结构化诊断与导出 | Diagnostics, all | 横向 | 受限可用 | `axiom_diagnostics_test`（`NumericEvidence` TXT/JSON、NaN→`null`、审计成功/缺口/无匹配/重复 ID/finding 截断/非法策略与 ID/导出失败及源报告不污染），`axiom_boolean_prep_test`（BOOL 失败数值证据与 `audit_evidence` 门禁）；第 69 批最终完整 CTest 16/16 通过 | 第 69 批公开 `Issue::numeric_evidence`、`DiagnosticEvidencePolicy/Audit/Finding`、`audit_evidence` 与 JSON 审计导出；单条/批量/全量导出保留数值证据，BOOL 主运行及预处理统计导出的受覆盖失败分支已纳入系统门禁。HEAL 验证/修复/回滚及 IO 导入导出/后验验证/批处理的全部重量级失败分支仍待迁移 |
| FR-HYBRID-001 | B-Rep/Mesh/Implicit 混合表示 | Rep, Ops | Stage 8 | 进行中 | `axiom_representation_io_test` | Implicit 表示、局部双向转换和误差合同 |

“受限可用”仅指仓库已测试的最小子集，不代表达到需求文档所述工业成熟度。详细限制以[当前开发进度](../plan/AxiomKernel_当前开发进度.md)为准。

## 3. 非功能需求追踪

| ID | 需求 | 当前状态 | 衡量方式 | 当前证据 | 发布门禁 |
|---|---|---|---|---|---|
| NFR-REL-001 | 失败不污染、事务一致性 | 受限可用 | 失败注入后句柄/存储/拓扑不变量 | `axiom_topology_test` 覆盖协作式取消、创建/删除/替换混合快照、嵌套/空/重复回滚保存点、LIFO 释放、失效/跨事务句柄、移动所有权和取消优先级；`axiom_kernel_runtime_invariant_test` 覆盖取消/保存点审计自洽性；第 74 批最终完整 CTest 16/16 通过（0 失败，120.81 s） | 嵌套保存点已闭合受限阶段性原子工作流，但采用内存全拓扑快照且仍为单活动写者；取消不抢占单个正在执行的调用，BOOL/HEAL/IO 内部阶段轮询、增量保存点与更细粒度隔离待实现 |
| NFR-DIA-001 | 可解释失败 | 受限可用 | 稳定错误码、`diagnostic_id`、阶段和问题实体 | diagnostics 及 workflow 测试；既有 BOOL/HEAL/STEP/OBJ/STL/glTF/3MF 及网格导出切片证据保留；第 70 批为 AXMJSON、Axiom IGES 元数据和 Axiom BREP JSON 子集补齐 `io.import.<format>.input/path/open/read/parse/validation`，覆盖普通文件、64 MiB/短读、严格字段/格式/BodyKind、有限数值/包围盒/轴退化、阶段与错误码检索、JSON、Body/Mesh/next_id 隔离和原位重试；`cycle-0070-gates.log` 最终 CTest 16/16 通过 | 支持范围内无静默失败；标准 IGES 实体仍按 NotImplemented 拒绝，HEAL/IO 全重量级分支的数值证据审计仍待闭合 |
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
