# AxiomKernel 需求追踪矩阵

> 状态：已接受
> 责任域：Project
> 维护者：产品负责人、架构负责人、质量负责人
> 最后核验：2026-09-27
> 核验依据：[功能需求](AxiomKernel_几何引擎功能需求文档.md)、[当前进度](../plan/AxiomKernel_当前开发进度.md)、`CMakeLists.txt`、`.axiom-agent/logs/cycle-0068-gates.log`

## 1. 使用方式

本矩阵是需求到工程证据的索引，不替代需求正文或状态详情。实现 MR 应引用稳定 ID，并更新“证据”和“状态”；没有满足完整验收标准时不得标记“已满足”。状态采用[治理规范](../architecture/AxiomKernel_项目结构与文档治理建议.md#5-需求到交付闭环)中的统一定义。

## 2. 功能需求追踪

| ID | 需求域 | 责任模块 | 目标阶段 | 当前状态 | 自动化证据 | 主要缺口 / 下一验收点 |
|---|---|---|---|---|---|---|
| FR-GEO-001 | 几何对象、求值与变换 | Math, Geo | Stage 2 | 受限可用 | `axiom_math_services_test`, `axiom_geometry_test`, `axiom_query_eval_test`；第 69 批最终完整 CTest 16/16 通过 | 第 69 批新增曲线 `closest_point_detailed`：Line/LineSegment/Circle/折线解析求解，Ellipse/Parabola/Hyperbola/Bezier/BSpline/NURBS/CompositeChain 全有效域分支限界；公开距离下界、参数不确定度、预算与终止原因，旧非解析最近参数复用该流程，覆盖大坐标、非单位速度、窄结点段、常值退化及失败不写缓存。仍缺曲面的完整参数域（含修剪边界）全域精度、预算与收敛语义 |
| FR-TOPO-001 | 拓扑实体、关系与一致性 | Topo | Stage 2 | 受限可用 | `axiom_topology_test`, `axiom_kernel_runtime_invariant_test`；第 69 批最终完整 CTest 16/16 通过 | 第 69 批新增 `first_boundary_conflict` 及冲突类别/环边/最近点/距离证据；`create_face` 与 `validate_face` 统一识别有限 Line/LineSegment 的内部相交、端点相接、共线正长度重叠和容差内正距离邻近，新增 `AXM-TOPO-E-0028/0029`，拒绝不分配 FaceId、不改索引或事务计数。一般曲线仍因缺少显式边 trim 区间未覆盖，完整 trim bridge 与持久命名待闭合 |
| FR-OPS-001 | 基础体与特征构造 | Ops, Geo, Topo | Stage 3 | 进行中 | `axiom_smoke_test`, `axiom_ops_heal_test`（第 60/61/62/65/66 包回归保留；第 68 批新增 `extrude_to_plane` 240 组轮廓/空间/方向/目标面变体及三角楔体、整周 `revolve` 32 组离轴/轴边与空间变体、曲线导轨 `sweep` 40 组凹/带孔与绕向变体；覆盖真实面边点体、邻接/bbox、Strict、owned 网格、质量/惯性、退化失败不污染与回滚重试）；`cycle-0068-gates.log` 独立完整构建成功，CTest 16/16 通过（0 失败，136.35 s） | 仍限于保守浮点剖分/采样多面体 BRep；缺解析扫掠曲面/精确旋转、带孔尖顶/旋转、负比例、任意截面放样、逐壁恒角拔模、闭合样条/复合导轨及显式轮廓历史；部分角旋转与无显式轮廓路径仍受限/占位 |
| FR-BOOL-001 | 布尔并/交/差与阶段诊断 | Ops, Heal | Stage 4 | 进行中 | `axiom_boolean_prep_test`, `axiom_boolean_workflow_test` | 工业退化场景、精确切分/分类/重建成功率 |
| FR-MOD-001 | 偏置、抽壳与直接编辑 | Ops, Heal | Stage 6 | 未开始 | — | 先定义最小输入域、事务和验证门禁 |
| FR-BLEND-001 | 圆角与倒角 | Ops, Heal | Stage 6 | 未开始 | — | 常半径最小闭环及失败阶段诊断 |
| FR-QUERY-001 | 几何/拓扑查询与分析 | Geo, Topo, Eval | Stage 3 | 进行中 | `axiom_query_eval_test`（第 63/67 包长度查询回归保留；第 68 批新增 `face_area`，覆盖 Plane/Cylinder/Cone/Sphere/Torus、Trimmed+Offset、凹外环/孔/绕向、无 PCurve 平面兼容、不支持/越域/断裂/自交/删除、事务回滚及缓存/对象不污染）；`cycle-0068-gates.log` 独立完整构建成功，CTest 16/16 通过（0 失败，136.35 s） | `face_area` 仍仅支持折线 PCurve 修剪的解析面及 Trimmed/Offset，周期缝需同一展开区间；Bezier/BSpline/NURBS/Revolved/Swept 面积、拓扑曲边裁剪参数、通用体积/重心/惯性矩与稳定求交仍待闭合 |
| FR-HEAL-001 | 验证与修复 | Heal, Topo, Rep | Stage 5 | 进行中 | `axiom_heal_test`, `axiom_ops_heal_test` | 扩充规则集、修复前后不变量和语料库 |
| FR-IO-001 | 数据导入导出 | IO, Rep, Heal | Stage 5 | 受限可用 | `axiom_io_workflow_test`, `axiom_io_dataset_test` | 标准 STEP/IGES 深度、兼容矩阵和 round-trip 预算 |
| FR-REP-001 | 三角化、表示与转换 | Rep, Geo, Topo | Stage 5 | 受限可用 | `axiom_representation_io_test` | 误差预算、属性保真及局部增量更新 |
| FR-EVAL-001 | 版本、事务与增量失效 | Core, Topo, Eval | Stage 7 | 进行中 | `axiom_kernel_runtime_invariant_test`, `axiom_query_eval_test` | 公共事务合同、取消/重放、并发调度 |
| FR-PLUGIN-001 | 插件能力发现与隔离 | Plugin, SDK | Stage 8 | 受限可用 | `axiom_plugin_sdk_test` | ABI 生命周期、沙箱/资源策略及兼容测试 |
| FR-DIAG-001 | 结构化诊断与导出 | Diagnostics, all | 横向 | 受限可用 | `axiom_diagnostics_test`（`NumericEvidence` TXT/JSON、NaN→`null`、审计成功/缺口/无匹配/重复 ID/finding 截断/非法策略与 ID/导出失败及源报告不污染），`axiom_boolean_prep_test`（BOOL 失败数值证据与 `audit_evidence` 门禁）；第 69 批最终完整 CTest 16/16 通过 | 第 69 批公开 `Issue::numeric_evidence`、`DiagnosticEvidencePolicy/Audit/Finding`、`audit_evidence` 与 JSON 审计导出；单条/批量/全量导出保留数值证据，BOOL 主运行及预处理统计导出的受覆盖失败分支已纳入系统门禁。HEAL 验证/修复/回滚及 IO 导入导出/后验验证/批处理的全部重量级失败分支仍待迁移 |
| FR-HYBRID-001 | B-Rep/Mesh/Implicit 混合表示 | Rep, Ops | Stage 8 | 进行中 | `axiom_representation_io_test` | Implicit 表示、局部双向转换和误差合同 |

“受限可用”仅指仓库已测试的最小子集，不代表达到需求文档所述工业成熟度。详细限制以[当前开发进度](../plan/AxiomKernel_当前开发进度.md)为准。

## 3. 非功能需求追踪

| ID | 需求 | 当前状态 | 衡量方式 | 当前证据 | 发布门禁 |
|---|---|---|---|---|---|
| NFR-REL-001 | 失败不污染、事务一致性 | 受限可用 | 失败注入后句柄/存储/拓扑不变量 | `axiom_topology_test` 覆盖默认/共享/预取消、写/提交/显式回滚/析构边界取消、创建删除替换混合快照恢复、单写者隔离、移动所有权、稳定诊断和累计指标；`axiom_kernel_runtime_invariant_test` 覆盖取消审计自洽性；第 69 批最终完整 CTest 16/16 通过 | 协作式取消已闭合拓扑 API 边界与完整快照恢复；仍不会抢占单个正在执行的调用，长耗时 BOOL/HEAL/IO 内部阶段轮询、子事务/保存点及更细粒度隔离语义待实现 |
| NFR-DIA-001 | 可解释失败 | 受限可用 | 稳定错误码、`diagnostic_id`、阶段和问题实体 | diagnostics 及 workflow 测试；`axiom_boolean_prep_test` 覆盖诊断开关两种模式下的早期失败阶段、输入实体、检索/JSON 与失败不污染；第 10 切片补齐非法 BooleanOp 拒绝及合法枚举回归，拒绝不触发 Eval 失效传播；第 15 切片覆盖预处理统计导出输入/打开/写入失败阶段、实体、检索/JSON、参数失败文件及模型不污染、Linux `/dev/full` 拒绝假成功；第 20 切片为 `validate_geometry` 全部分支绑定 `heal.validate_geometry.*` 阶段及目标 Body/问题子实体；第 25 切片为体/壳/批量壳自交验证失败绑定 `heal.validate_self_intersection.*` 阶段及 Body/Shell；第 30 切片为 STEP 导入的空路径、缺失文件与非可读常规文件绑定 `io.import.step.input/path/open`；第 35 切片为 OBJ 物化前输入/路径/打开/解析/退化验证失败绑定 `io.import.obj.*`；第 40 切片为 STL 物化前输入/路径/打开/读取/解析/网格验证失败绑定 `io.import.stl.*`；第 45 切片为 glTF 物化前失败绑定 `io.import.gltf.input/path/open/read/parse/validation`；第 50 切片为 3MF 物化前失败绑定 `io.import.3mf.input/path/open/read/parse/validation`，`axiom_io_workflow_test` 覆盖阶段检索、JSON、非法数值/索引/退化网格、Body/Mesh 不污染及重试；第 64 功能包补齐四种网格导出的输入/路径/转换/网格/打开/写入/侧车失败阶段与 Body、关闭状态检查及失败时网格/ID/缓存恢复，新增策略组合、退化、检索/JSON、设备失败与重试回归（已修复编译调用和测试夹具错误；相关回归随第 68 批最终完整 CTest 16/16 通过） | 支持范围内无静默失败 |
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
