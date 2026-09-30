# AxiomKernel 需求追踪矩阵

> 状态：已接受
> 责任域：Project
> 维护者：产品负责人、架构负责人、质量负责人
> 最后核验：2026-09-30
> 核验依据：[功能需求](AxiomKernel_几何引擎功能需求文档.md)、[当前进度](../plan/AxiomKernel_当前开发进度.md)、`CMakeLists.txt`、[cycle-0074 门禁日志](../../.axiom-agent/logs/cycle-0074-gates.log)（此前验收沿用 cycle-0072/0073 证据）

## 1. 使用方式

本矩阵是需求到工程证据的索引，不替代需求正文或状态详情。实现 MR 应引用稳定 ID，并更新“证据”和“状态”；没有满足完整验收标准时不得标记“已满足”。状态采用[治理规范](../architecture/AxiomKernel_项目结构与文档治理建议.md#5-需求到交付闭环)中的统一定义。

## 2. 功能需求追踪

| ID | 需求域 | 责任模块 | 目标阶段 | 当前状态 | 自动化证据 | 主要缺口 / 下一验收点 |
|---|---|---|---|---|---|---|
| FR-GEO-001 | 几何对象、求值与变换 | Math, Geo | Stage 2 | 受限可用 | `axiom_math_services_test`, `axiom_geometry_test`, `axiom_query_eval_test`；cycle-0072 最终独立完整构建成功、CTest 16/16 通过（0 失败，120.81 s；`axiom_geometry_test` 0.58 s） | 曲线与曲面均公开 `closest_point_detailed`。Plane/Cylinder/Cone/规则 Sphere/Torus 及嵌套 Offset 链支持解析全域求解与无界域自动有限化；Bezier/BSpline/NURBS 以正权有理控制网凸包建立随细分收紧的距离下界，穿透 Trimmed/Offset 并公开剪枝证据。仍缺通用无限派生面自动有限化、spindle/horn 环面解析证书、旋转/扫掠专用局部几何界和大模型独立性能基线 |
| FR-TOPO-001 | 拓扑实体、关系与一致性 | Topo | Stage 2 | 受限可用 | `axiom_topology_test`, `axiom_query_eval_test`, `axiom_kernel_runtime_invariant_test`；cycle-0072 最终完整 CTest 16/16 通过（0 失败；`axiom_topology_test` 0.13 s，运行时不变量 0.00 s） | 显式 trim 曲边长度/bbox 语义保留；跨环冲突现可复用无诊断 Geo 求交覆盖圆锥曲线、Bezier、BSpline、NURBS 和混合 CompositeChain，支持递减区间、端点接触、容差邻近与可证连续重合；缺 trim/预算/数值失败闭合拒绝。完整 3D trim↔PCurve/曲面参数域一致性、周期缝/奇点与持久命名仍待闭合 |
| FR-OPS-001 | 基础体与特征构造 | Ops, Geo, Topo | Stage 3 | 进行中 | `axiom_smoke_test`, `axiom_ops_heal_test`；cycle-0074 独立完整构建（并发 4）成功，CTest 16/16、0 失败、134.05 s（Ops 106.39 s）；独立积分体积/质心/完整惯性、真实拓扑/Strict/网格、中间关键站、凹/孔/姿态/反向/周期、旧接口兼容、数值/预算/阶段诊断、活动事务原子性和编辑回滚重试通过 | 新增 `ExtrusionLawStation/extrude_with_law` 高度分段比例/扭转、`SweepScaleStation/sweep_with_scale_law` 采样弦长分段比例、`SweepLawStation/sweep_with_law` 联合律；首站 (0,1[,0])、正比例、严格递增至 fraction=1。保留关键站和原导轨站，步长 ≤7.5°/较小比例 25%、联合 ≤4096 区间、累计绝对扭角 ≤一周；周期末比例 1、末角 0/±2π（1e-10 rad），焊接无端盖不推断置换；曲线全律最大比例曲率/间距及 2000000 接触候选门禁。`revolve_between/sweep_scaled/extrude_twisted` 合同兼容；结果为采样多面体，采样/物化/周期扭角提供阶段证据，失败不污染，未新增错误码。带孔尖顶/触轴、零负扫掠比例、一般非线性解析比例/扭转律、至平面组合、任意截面匹配/分支坍塌、逐壁恒角拔模、嵌套复合导轨、解析扫掠/精确旋转/螺旋面及轮廓历史仍缺失 |
| FR-BOOL-001 | 布尔并/交/差与阶段诊断 | Ops, Heal | Stage 4 | 进行中 | `axiom_boolean_prep_test`, `axiom_boolean_workflow_test` | 工业退化场景、精确切分/分类/重建成功率 |
| FR-MOD-001 | 偏置、抽壳与直接编辑 | Ops, Heal | Stage 6 | 未开始 | — | 先定义最小输入域、事务和验证门禁 |
| FR-BLEND-001 | 圆角与倒角 | Ops, Heal | Stage 6 | 未开始 | — | 常半径最小闭环及失败阶段诊断 |
| FR-QUERY-001 | 几何/拓扑查询与分析 | Geo, Topo, Eval | Stage 3 | 进行中 | `axiom_geometry_test`, `axiom_topology_test`, `axiom_query_eval_test`；cycle-0074 完整 CTest 16/16、0 失败、134.05 s（三项 0.55/0.14/0.37 s）；最近面/边/角、容差带、穿透/反向/内部/端点/相切/共面/空集、凹面孔、多壳空腔岛、薄层/姿态尺度、精确预算重放/耗尽/数值失败、曲面曲边拒绝、支撑面编辑回滚及只读不污染通过 | 新增 `locate_point/clip_segment` 与 Body 空间查询公开类型；真实最近位置/距离与 FaceId/ShellId，等距稳定选择，按闭壳奇偶定位空腔/材料岛。无量纲有序线段区间、共面段/孤立相切，Inside 材料长度为模型长度单位，无交集成功空集，位置容差不膨胀材料或合并可分辨薄层。仅无自交嵌入平面直边双边流形闭壳，共用质量/壳接触/包含检查，拒绝曲边弦替代并核对支撑平面；默认 1000000 三角形预算只计前置之后的新计算，失败无部分值。既有 `closest_point_detailed/intersect_curve_curve` 保留；曲面曲边闭壳积分与定位、一般高阶异参重合、通用无限派生面证书、相交多壳、自身全局嵌入证明、大规模加速及完整拓扑曲边裁剪参数模型仍待后续 |
| FR-HEAL-001 | 验证与修复 | Heal, Topo, Rep | Stage 5 | 进行中 | `axiom_heal_test`, `axiom_ops_heal_test`；cycle-0073 完整 CTest 16/16 通过，单项后验、批量后项及 auto_repair 失败回滚/诊断回归通过 | 失败阶段/实体/有限数值证据与派生对象回收、trim 原 PCurve 绑定恢复已闭合；Plane/Cylinder/Sphere trim 支持范围不变，修复规则集、不变量语料与工业容差策略仍需扩充 |
| FR-IO-001 | 数据导入导出 | IO, Rep, Heal | Stage 5 | 受限可用 | `axiom_io_workflow_test`, `axiom_io_dataset_test`；cycle-0073 主格式证据、共享后验管线、批量精确体/网格晚失败回滚与重试随完整 CTest 16/16 通过 | 精确文本仍为 Axiom 子集，标准 STEP/IGES 实体未实现；STEP/AXMJSON/auto 批量导入失败恢复对象/缓存/Eval/next_id，后验问题不必使导入返回失败。标准格式深度、兼容矩阵、round-trip 预算及文件系统事务待闭合 |
| FR-REP-001 | 三角化、表示与转换 | Rep, Geo, Topo | Stage 5 | 受限可用 | `axiom_representation_io_test` | 误差预算、属性保真及局部增量更新 |
| FR-EVAL-001 | 版本、事务与增量失效 | Core, Topo, Eval | Stage 7 | 进行中 | `axiom_kernel_runtime_invariant_test`, `axiom_query_eval_test` | 公共事务合同、取消/重放、并发调度 |
| FR-PLUGIN-001 | 插件能力发现与隔离 | Plugin, SDK | Stage 8 | 受限可用 | `axiom_plugin_sdk_test` | ABI 生命周期、沙箱/资源策略及兼容测试 |
| FR-DIAG-001 | 结构化诊断与导出 | Diagnostics, all | 横向 | 受限可用 | `axiom_diagnostics_test`、`axiom_boolean_prep_test` 保留既有 NumericEvidence/审计回归；cycle-0073 `axiom_heal_test`、`axiom_io_workflow_test` 新增模块级 `audit_evidence`、JSON 有限数值证据、源报告不污染、单项/批量/自动修复失败回滚；修复后完整 CTest 16/16 通过（126.44 s） | `Issue::numeric_evidence`、证据策略/审计/JSON 导出已具备；HEAL 验证/修复/后验验证/trim/批量及 IO 主格式导入导出/auto/后验验证/批处理已迁移阶段、实体与有限数值证据并通过模块门禁。普通文本/目录等非主格式辅助接口及更广泛失败注入语料仍待扩充，不宣称全 API 或工业格式覆盖 |
| FR-HYBRID-001 | B-Rep/Mesh/Implicit 混合表示 | Rep, Ops | Stage 8 | 进行中 | `axiom_representation_io_test` | Implicit 表示、局部双向转换和误差合同 |

当前主线为 Stage 3，cycle-0074 是升级前原范围验收检查点，下一批开始阶段任务调度；本批四包和第 60/61/62/65 包现有回归已纳入完整门禁，FR-OPS-001 / FR-QUERY-001 均不标记需求完成。构建仍有成员初始化/未使用参数告警，日志无文档检查结果；本轮未重跑构建测试。

“受限可用”仅指仓库已测试的最小子集，不代表达到需求文档所述工业成熟度。详细限制以[当前开发进度](../plan/AxiomKernel_当前开发进度.md)为准。

## 3. 非功能需求追踪

| ID | 需求 | 当前状态 | 衡量方式 | 当前证据 | 发布门禁 |
|---|---|---|---|---|---|
| NFR-REL-001 | 失败不污染、事务一致性 | 受限可用 | 失败注入后句柄/存储/拓扑不变量 | `axiom_topology_test` 覆盖协作式取消、创建/删除/替换混合快照、嵌套/空/重复回滚保存点、LIFO 释放、失效/跨事务句柄、移动所有权和取消优先级；`axiom_kernel_runtime_invariant_test` 覆盖取消/保存点审计自洽性；cycle-0072 最终完整 CTest 16/16 通过（0 失败，120.81 s） | 嵌套保存点已闭合受限阶段性原子工作流，但采用内存全拓扑快照且仍为单活动写者；取消不抢占单个正在执行的调用，BOOL/HEAL/IO 内部阶段轮询、增量保存点与更细粒度隔离待实现 |
| NFR-DIA-001 | 可解释失败 | 受限可用 | 稳定错误码、`diagnostic_id`、阶段、问题实体和有限数值证据 | cycle-0073 HEAL/IO 模块审计、JSON 数值证据与源报告不污染通过；IO 覆盖 STEP/AXMJSON/IGES/BREP/OBJ/STL/glTF/3MF 与 auto、`io.post_import.*`、批量上下文、真实失败传播、精确体/网格晚失败回滚及 `next_id` 原位重试；[门禁日志](../../.axiom-agent/logs/cycle-0073-gates.log) 最终 CTest 16/16 通过（126.44 s；IO 13.45 s、HEAL 0.06 s） | 支持的重量级范围已通过阶段/实体/有限数值审计；预物化无对象用零令牌，导入后验证/修复 issue 可随 Ok 返回。标准 STEP/IGES 实体交换和普通文本/目录辅助接口仍受限；批量导出不保证文件事务 |
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
