# AxiomKernel 当前开发进度

> **cycle-0087 / S5-EXIT（当前唯一退出任务）**：当前唯一退出任务为 **cycle-0087 / S5-EXIT**，`stage_task_id=S5-EXIT`、`stage_outcome=ready_for_acceptance`。[两条逐项证据](../quality/AxiomKernel_测试与验收方案.md#114-cycle-0087--s5-exit-门禁与逐项证据)与[集成支持矩阵](../api/AxiomKernel_详细模块接口清单.md#1112-stage-5-集成退出支持矩阵cycle-0087--s5-exit)覆盖固定导入→验证→修复→三角化→导出/再导入、独立几何参考、稳定拒绝与失败隔离。调度器一轮完整构建成功，CTest **16/16、0 失败、196.58 s**；必需 io_workflow/io_dataset/heal/representation_io **13.05/0.55/0.78/14.32 s**，性能 **1.92 s**，无 repair。develop 的“尚未运行/待统一门禁”已由本批代码门禁取代；文档本轮同步，最终文档门禁及调度器提交成功未记录，不能记正式已验收。FR-IO-001 / FR-REP-001 保持受限可用，Stage 5 进行中，不追认历史阶段或 S5-HEAL/S5-IO/S5-TESSELLATION 的提交/验收。本包已收口，不扩展功能；阶段任务优先于需求权重、历史 remaining 和新增变体，基础层仅限直接阻断项。
>
> **支持范围与原子性**：本批集成边界：原Box元数据零owned shells直接三角化/质量拒绝；Safe合成Modified bbox边界仅显示，派生再导入零壳/bbox_proxy且质量仍拒绝。STL显式Safe派生完整新体/新mesh快照并保源网格，内部Standard后验、固定回归额外Strict；独立ASCII积分V4/A=13+sqrt(244)/2/C误差≤1e-12，非bbox V24。缺mesh与实际复制后angular=0后验失败回滚有固定证据；Heal允许ID空档、IO外层恢复next_id。默认无标准交换桥接，沿用真实边界三角化子集/预算/UV/开放面和发布限制。

> **本批真实门禁**：[日志](../../.axiom-agent/logs/cycle-0087-gates.log)一轮测试/示例开启配置、并发4完整构建成功；CTest16/16、0失败、196.58 s，四项必需13.05/0.55/0.78/14.32 s，性能1.92 s。SDK实发1条Issue::numeric_evidence初始化告警，不宣称告警清零；构建总耗时、最终文档门禁及提交未记录。本轮只同步文档，未重跑构建测试。
>
> **支持限制**：仅支持平面直边凹面/孔洞及经过真实矩形边界认证的四极点双线性、一阶等权且工厂归一化后单位夹持样条、LineSegment Swept 和矩形 Trimmed。一般曲边、高阶、不等权、曲面孔洞/非矩形、Offset/Revolved 拒绝；patch每向256、圆周4096、native百万顶点上限，不可达失败。小尺度polygon绝对门槛保留，UV seam/法向拆分不认证流形；metadata/implicit仅显示代理，开放曲面无实体质量资格，round-trip与固定积分参考不证明任意BRep保真或工业全局误差。OBJ不导出vertex normals，本批核对三角cross与解析/Geo法向。

> **cycle-0083 / S4-EXIT（Stage 4 历史代码门禁）**：`stage_task_id=S4-EXIT`、`stage_outcome=ready_for_acceptance`。cycle-0083 / S4-EXIT冻结生产代码/公开API/既有码，以回归收口固定第一代工业模型集：真实并/差/交owned重建→Strict/来源/查询→owned网格/OBJ独立V/A，偏移盒U/D/I各总计两轮；暖缓存下七阶段诊断、Eval依赖/输入摘要与writer/保存点/rollback重试均有[逐项证据](../quality/AxiomKernel_测试与验收方案.md#110-cycle-0083--s4-exit-门禁与逐项证据)及[退出支持矩阵](../api/AxiomKernel_详细模块接口清单.md#824-stage-4-退出支持矩阵cycle-0083--s4-exit)。本批完整CTest **16/16、0失败、194.58 s**；现有性能基线 **2.03 s**只测兼容run/查询，run_rebuilt工业性能未认证。

> **cycle-0083 历史门禁**：[日志](../../.axiom-agent/logs/cycle-0083-gates.log)一轮测试/示例开启配置、完整并发4构建成功，完整CTest16/16、0失败、194.58s。必需workflow/prep/query_eval/representation_io/runtime 4.25/5.63/2.01/10.17/0.04s，性能2.03s；关联ops_heal/heal153.97/0.67s。无repair；日志未记录构建总时长、最终文档门禁或提交成功，增量无告警不能证明全仓清零。

> **正式状态与限制**：代码全量门禁通过，API/诊断/样例/矩阵/进度/Backlog已同步，调度器最终文档门禁及提交成功后才记已验收，Stage4/FR-BOOL-001保持进行中。支持限嵌入平面直边ExactBRep闭壳、double/奇偶材料及可解析外向源壳；内部共面、同形/同ID和面相切有真实参考，公开只读prep仍拒绝共面。边/点Union、未解析薄层/容差带、曲面曲边/ExactCritical拒绝，Safe只修人工共面分片及一致共线节点。保留Strict至少六面及近似网格自交门禁，不证明全局嵌入或精确谓词；人工节点截面可能明确数值拒绝。兼容run仍含bbox代理实体语义，输入隔离是公开几何/拓扑摘要而非完整序列化。 本轮只改docs Markdown，未构建测试、写自动台账/.axiom-agent/result.json、提交或推送。

> **cycle-0082 / S4-REBUILD（历史重建代码门禁）**：新增run_rebuilt真实owned重建、内部共面/面相切、空材料与受限Safe；首轮11/16、28.43s，repair后完整构建/CTest16/16、184.89s。该批[三条证据](../quality/AxiomKernel_测试与验收方案.md#19-cycle-0082--s4-rebuild-门禁与逐项证据)及[支持范围](../api/AxiomKernel_详细模块接口清单.md#823-stage-4-真实实体重建支持矩阵cycle-0082--s4-rebuild)保留，不追认历史提交或验收；当前唯一任务见§5.2.1。

> **cycle-0081 / S4-SPLIT-CLASSIFY（历史只读准备代码门禁）**：`stage_outcome=ready_for_acceptance`。新增 prepare_split_classification / classify_points，实际外/孔环三角化并接入真实有限交段切分面与源边，同步源边切点，保留 source BodyId/FaceId/EdgeId、v0→v1 分数、交段与反向整边邻接；实体分类从真实裁剪边界使用至少两条一致有效射线，未解析近边界明确拒绝。支持 [API §8.2.2](../api/AxiomKernel_详细模块接口清单.md#822-stage-4-第一代切分与实体分类支持矩阵cycle-0081--s4-split-classify)。

> **cycle-0081 实际门禁**：[日志](../../.axiom-agent/logs/cycle-0081-gates.log) 首次完整构建因 Math 声明缺失失败，没有首轮 CTest；repair 后第二轮配置/并发4完整构建成功，最终串行 CTest **16/16、0失败、173.96 s**；必需 prep/workflow/topology **0.45/0.61/0.18 s**，性能 **2.26 s**。repair 的定向三项通过 **0.83 s** 单独记录，不替代最终门禁；构建总耗时未记录，增量日志无告警打印不证明全仓清零。本轮未重建测试。

> **cycle-0081历史状态与限制**：三条 [stage_evidence](../quality/AxiomKernel_测试与验收方案.md#18-cycle-0081--s4-split-classify-门禁与逐项证据) 按真实切分、实体分类、稳定诊断与失败/回滚隔离排列。代码全量门禁已通过，API/错误码/样例/矩阵/进度/Backlog 已同步；该批静态文档检查见验收§1.8，未追认调度器提交或验收；当前 Stage 5 任务见§5.2.1；此处仅保留 Stage 4 历史代码事实。支持限嵌入平面直边闭壳、double 与奇偶材料约定，允许额外三角细分；该只读包不认证实体重建或二维共面；cycle-0082支持另见API§8.2.3，精确谓词、曲面/曲边或全局嵌入仍未认证。兼容 run 仍含 bbox 实体语义；输入隔离为公开摘要，不是逐顶点坐标/全部几何编码，部分 PCurve 绑定面未获固定快照认证。本轮只改 docs Markdown，未提交/推送或写自动台账/.axiom-agent/。

> **cycle-0080 / S4-INTERSECTION（历史代码门禁）**：真实平面直边闭壳候选/解析交线、凹形/孔环裁剪、源边参数与兼容 run 读取拒绝已通过固定参考；最终全量 **16/16、0失败、172.43 s**，prep/workflow **0.17/0.12 s**。保留 [验收 §1.7](../quality/AxiomKernel_测试与验收方案.md#17-cycle-0080--s4-intersection-门禁与逐项证据) 的历史事实；不由本轮追认其正式提交或验收。

> **cycle-0079 / S3-EXIT（Stage 3 历史记录，ready_for_acceptance）**：17 行统一支持矩阵映射体类、五类真实建模、截面/最近点/距离/质量与精确/采样/拒绝范围；门面 smoke 补齐缩放拉伸→质量→截面→最近边界→距离→owned 网格→Strict。复用既有接口/错误码，不新增工业功能。矩阵成功/拒绝查询核对对象/几何数量、next_object_id、缓存/Eval/事务只读；历史编辑/来源/表示/回滚清理回归保留。

> **cycle-0079 实际门禁（历史）**：[日志](../../.axiom-agent/logs/cycle-0079-gates.log) 一轮独立配置及并发 4 完整构建成功，串行全量 CTest **16/16、0 失败、163.78 s**；必需 Ops **137.10 s**、Query **1.41 s**、Rep **9.01 s**、Runtime **0.02 s**、smoke **0.02 s**；保留 Topology **0.17 s**、Boolean workflow **0.03 s**、Boolean prep **0.04 s**、性能 **1.71 s**，原阈值/迭代不变。Strict warnings 缓存配置 ON，Topo/Rep 所涉 3 条未再出现；日志实发残余 SDK 初始化告警 **1 条**，历史 helpers 17 条未重编译，不能声称已清零。构建总耗时未记录，无 repair。

> **cycle-0079 退出证据与状态（历史）**：`stage_task_id=S3-EXIT`、`stage_outcome=ready_for_acceptance`；[三条 stage_evidence 与五项退出映射](../quality/AxiomKernel_测试与验收方案.md#16-cycle-0079--s3-exit-门禁与逐项证据)、[统一矩阵](../api/AxiomKernel_详细模块接口清单.md#614-stage-3-统一退出支持矩阵cycle-0079--s3-exit)、专项合同/诊断/样例、需求矩阵及 Backlog 已同步。文档检查结果见验收 §1.6；调度器提交成功尚未记录，Stage 3 与 FR-OPS-001 / FR-QUERY-001 保持进行中。通用曲面/曲边积分与查询、曲面 thicken、任意 loft 匹配、相交多壳/全局嵌入证明、空间加速、完整 trim/标准交换、工业 Boolean、Eval 自动算法重算不作为本阶段完成前提；不自动开启后续阶段。本轮仅修改 docs Markdown，未重建测试、提交/推送或修改自动台账/`.axiom-agent/`。

> **cycle-0078 / S3-CONSISTENCY（历史代码全量门禁）**：历史交付为基础零件建模→验证→截面/距离/质量→表示/来源/Eval 一致性。缓存包含体身份与当前 owned 边界并核对 source_body；full/local/shell 面片完整三角化和组装后原子发布，owned 失败不分配网格 ID、不遗留部分缓存，不回退 bbox/创建参数。平面凹形/孔面按真实区域三角化；native primitive 编辑撤销创建参数资格，回滚恢复。面/PCurve/面壳体编辑与恢复传播 Eval dirty，移除体清理网格/缓存/体绑定，保留源体及共享壳。无公开签名或错误码常量新增，仅补 API 合同注释。

> **cycle-0078 实际门禁（历史）**：[日志](../../.axiom-agent/logs/cycle-0078-gates.log) 记录一轮独立配置（测试/示例开启）、并发 4 完整构建成功，CTest **16/16、0 失败、164.60 s**；四个必需回归 Ops/Heal **137.40 s**、Query/Eval **0.95 s**、representation/IO **9.44 s**、runtime invariant **0.02 s**，保留 Topology **0.23 s**、性能 **1.74 s**。开发报告“未执行”已由实际结果取代，无 repair；仍有初始化/未使用参数告警，未记录独立 Strict warnings、调度器文档检查或提交成功。本轮仅编辑 docs Markdown，未重建测试、提交/推送或写自动台账/`.axiom-agent/`。

> **cycle-0078 状态与边界（历史）**：`stage_task_id=S3-CONSISTENCY`、`stage_outcome=ready_for_acceptance`，三条 [stage_evidence](../quality/AxiomKernel_测试与验收方案.md#15-cycle-0078--s3-consistency-门禁与逐项证据) 按验收条目排列，文件/断言、独立参考和限制均可核对；[一致性合同](../api/AxiomKernel_详细模块接口清单.md#731-stage-3-表示来源与-eval-一致性合同cycle-0078--s3-consistency)、查询/质量/五类建模矩阵及样例已同步。revolve 是采样多面体，thicken 为平面直边单侧正厚度；Eval recompute 只管理图，不自动执行质量/表示算法；旧 MeshId 为快照，历史边界缓存可保留，不承诺 ID 回收。旧 metadata 显示 bbox 代理/Rep bbox 辅助查询仍保留，不构成 owned 或物理查询资格。正式已验收须调度器完整门禁、文档检查及提交成功；Stage 3 / FR-OPS-001 / FR-QUERY-001 仍进行中，不扩展后续阶段。

> **cycle-0077 / S3-MODELING（历史代码全量门禁）**：核验既有 extrude/revolve/sweep/loft 主路径，补齐真实平面直边 Face thicken，读取当前定向外/内环与裁剪边、沿单位支撑法向单侧生成独立 owned 三角闭壳。支持矩形/凹形/分离非嵌套孔、平移倾斜、独立环绕向/正反法向及反向裁剪共边；曲面/曲边/代理面拒绝，失败无 bbox 或占位回退。公开签名不变、未新增错误码。五类公开拓扑/表示/全质量/Strict、诊断四阶段、活动事务不污染、编辑拒绝旧质量与回滚重试回归已通过调度器实际 CTest。

> **cycle-0077 实际门禁（历史）**：[cycle-0077-gates.log](../../.axiom-agent/logs/cycle-0077-gates.log) 记录一轮独立配置/完整构建（测试/示例开启、并发 4）成功，全量 CTest **16/16、0 失败、153.89 s**；必需 Ops/Heal **126.52 s**、Topology **0.13 s**，关联 Query/Eval **0.95 s**、representation/IO **9.79 s**、性能基线 **1.60 s**。无 repair 报告；开发阶段“尚未执行”已由实际结果取代。仍有 BoundaryEdge/Issue 初始化与未使用参数告警；日志未记录独立 Strict warnings 门禁、文档检查或提交成功。Strict 拓扑断言通过不等于无编译告警。本轮只同步 docs Markdown，未重建测试、提交/推送或写自动台账/`.axiom-agent/`。

> **cycle-0077 证据边界（历史）**：历史报告 `stage_task_id=S3-MODELING`、`stage_outcome=ready_for_acceptance`，按三条验收要求排列的 [stage_evidence](../quality/AxiomKernel_测试与验收方案.md#14-cycle-0077--s3-modeling-门禁与逐项证据)、[五类主路径矩阵](../api/AxiomKernel_详细模块接口清单.md#811-stage-3-五类建模主路径cycle-0077--s3-modeling) 及质量/查询矩阵已同步。真实平面 thicken 已补齐主路径缺口，历史代理记录仍拒绝；棱柱及共面侧壁 loft 夹具在浮点容差内精确，revolve 为采样弦面多面体。正式已验收仅在调度器完整门禁、文档检查及提交成功后记录；FR-OPS-001 / FR-QUERY-001 与 Stage 3 仍进行中。通用解析/曲边闭壳积分、相交多壳、全局嵌入证明与后续阶段未扩展，内部零壳查询未声称验收。

> **cycle-0076 / S3-MASS（历史代码门禁）**：统一当前拓扑与未编辑原生 sphere/cylinder/cone/torus 解析质量资格，删除 bbox、Boolean/Modified 来源与 Sweep 创建缓存质量恢复；成功拓扑/PCurve 编辑撤销解析资格，保存点/回滚恢复，metadata/mesh 派生不继承，代理面重组仍拒绝。独立全属性参考、旋转采样误差、编辑/多壳材料空腔和拒绝合同通过完整 CTest **16/16、0 失败、147.97 s**（Query **0.95 s**、Ops **120.75 s**、Rep **9.68 s**）；历史证据见验收 §1.3。该批当时的 thicken 拒绝夹具已在 cycle-0077 改验真实质量/最近点，当前任务以 §5.2.1 为准，不推断历史文档检查或提交成功。

> **cycle-0075 / S3-QUERY（历史代码门禁）**：repair 后调度器完整构建成功，全量 **16/16 通过、0 失败、148.44 s**，支持矩阵与四条证据见 [测试与验收 §1.2](../quality/AxiomKernel_测试与验收方案.md#12-cycle-0075--s3-query-门禁与逐项证据)。首次 14/16（141.18 s）后修复负向旋转侧壁绕向、非等边楔体支撑和空列表建体拒绝夹具。当前唯一退出任务为 S5-EXIT（§5.2.1）；历史门禁不用于推断未记录的文档检查/提交成功或阶段完成。

> **cycle-0074 升级前检查点（FR-OPS-001 / FR-QUERY-001，已通过完整门禁）**：本批四个功能包为 `ExtrusionLawStation/extrude_with_law`、多面体 `locate_point/clip_segment`、`SweepScaleStation/sweep_with_scale_law`、`SweepLawStation/sweep_with_law`。实际 diff 增补公开类型、共享物化/质量前置检查与 Ops/Query 回归，未新增错误码。调度器以测试、示例开启配置 `build-agent`，`cmake --build /workspaces/axiom/build-agent --parallel 4` 完整构建成功；`ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` **16/16 通过、0 失败、总耗时 134.05 s**。其中 Ops/Heal **106.39 s**（低于现有 120 s 限制）、Query/Eval **0.37 s**、IO workflow **13.95 s**、representation/IO **9.97 s**、Geo **0.55 s**、Topo **0.14 s**、HEAL **0.06 s**、性能基线 **1.75 s**。见 [cycle-0074 门禁日志](../../.axiom-agent/logs/cycle-0074-gates.log)。日志记录一轮完整构建/CTest，无本批 repair 或文档检查结果；仍有 `BoundaryEdge/Issue` 缺失成员初始化及未使用参数告警，不是无告警或严格告警门禁通过的声明。本轮仅同步文档，未重新构建测试、未提交或推送，未修改自动开发进度台账或 `.axiom-agent/`。

> **cycle-0074 支持范围与状态（历史检查点）**：三个截面律入口支持凸/凹简单轮廓及分离非嵌套孔的分段正比例、扭转停顿/反向，保留关键站和真实中间 bbox/多面体质量；每步比例变化 ≤ 较小端的 25%、扭角 ≤ 7.5°、联合采样 ≤ 4096 区间、累计绝对扭角 ≤ 一周。扫掠以既有采样弦长为自变量；周期末比例为 1，联合律末角为 0 或 ±2π（1e-10 rad 容差），首末焊接无端盖；曲线接触最多 2000000 宽相候选。实体查询支持无自交的嵌入平面直边双边流形闭壳、空腔和材料岛，提供真实最近边界与有限线段材料/共面/相切区间；不按位置容差膨胀材料，预算只覆盖质量/壳关系前置检查之后的新三角形计算。采样多面体、曲面/曲边闭壳拒绝、无壳自身全局自交证明和无大规模加速等限制保留，详见 §3.3 与接口清单。四包及第 60/61/62/65 包的既有回归均已通过本次完整门禁，功能报告中的“未编译/待验收/继续缓冲再验收”为开发阶段旧状态。FR-OPS-001 / FR-QUERY-001 均保持进行中；该批当时主线为 Stage 3，按原范围闭合；cycle-0075 S3-QUERY 为历史代码门禁；当前唯一任务 cycle-0087 / S5-EXIT，状态见本文顶部。

> **cycle-0073 批次（FR-DIAG-001 / NFR-DIA-001 / FR-OPS-001，已通过完整门禁）**：HEAL 验证、修复、后验验证、trim 重建及批量修复失败统一携带 `heal.*` 阶段、实体与有限数值证据；单项后验失败回收派生体/物化对象，trim 失败恢复原 PCurve 绑定，批量后项失败回滚此前全部派生对象及 Eval 失效状态。IO 的 STEP/AXMJSON/IGES/BREP/OBJ/STL/glTF/3MF 与 auto 主格式失败补齐 `io.*` 与有限数值证据，STEP/AXMJSON 接入共享后验验证/修复管线，复制 `io.post_import.*` 证据且不改源诊断；STEP/AXMJSON/auto 批量实际失败恢复 Body/Mesh/拓扑/几何、链接、缓存、Eval 失效及 `next_id`，支持原位重试。候选/严格现有文件导入和目录/条件导出传播真实失败，AXMJSON/IGES/BREP 导出补齐最终流检查。`SweepService::extrude_twisted` 支持显式平面凸/凹及非嵌套分离孔、任意朝向、方向缩放/反向、正负部分角/整周及零角，物化真实三角闭壳与质量属性。首次全量 CTest **15/16 通过、1 失败、106.79 s**，Ops 在 `twisted_convex` 的拓扑/质量/Strict 检查失败；repair 仅交替扭转侧壁站间剖分对角线，消除固定对角线累积的一阶有向体积偏差，未放宽断言/性能阈值或增加角站。调度器独立完整构建（`build-agent --parallel 4`）后，`ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` 最终 **16/16 通过、0 失败、126.44 s**（Ops 98.84 s、IO workflow 13.45 s、HEAL 0.06 s、representation/IO 10.46 s、性能基线 1.68 s）。证据见 [cycle-0073 门禁日志](../../.axiom-agent/logs/cycle-0073-gates.log)；既有第 60/61/62/65 包随本次全量回归通过，无待验收项。首次构建仍有 BoundaryEdge/Issue 聚合成员初始化与未使用参数告警，不是无告警构建；日志未记录本批文档检查结果。本轮仅同步文档，未重跑构建或测试。

> **支持边界**：HEAL/IO 重量级包已通过模块 `audit_evidence` 门禁，普通文本/目录等非主格式辅助接口及更广泛失败注入语料仍待补齐。cycle-0084 起开启验证时未修复的验证/修复失败返回失败并回滚本次模型/cache/Eval/next_id；批量导入回滚由子项实际失败触发，批量导出不承诺文件事务。标准 STEP/IGES 实体限制不变。扭转方向须垂直轮廓平面、中心共面、正距离，扭角最多一周、角站差不超过 7.5°；仍为保守采样多面体 BRep，该旧入口未组合变比例/至平面；分段比例与扭转组合已由 cycle-0074 新入口提供，至平面组合仍不支持，不是解析螺旋面。FR-OPS-001 继续进行中；FR-DIAG-001/NFR-DIA-001 继续受限可用。

> **第 74 批（cycle-0072，FR-GEO-001 / FR-TOPO-001 / NFR-REL-001，已通过完整门禁）**：曲面 `closest_point_detailed` 新增 Plane/Cylinder/Cone/规则 Sphere/Torus 及嵌套 Offset 的无缓存解析全域求解，公开 `Analytic`、`effective_domain` 和自动有限化证据；Bezier/BSpline/NURBS 对非空结点片和递归子片使用正权有理 Bezier 控制网凸包 AABB 下界，并公开控制网/剪枝计数。跨环冲突复用 Geo 有限区间求交，覆盖显式 trim 的圆锥曲线、Bezier、BSpline、NURBS 与混合 CompositeChain，新增 `AXM-TOPO-E-0030..0033` 和求解工作量证据。拓扑事务新增嵌套保存点、LIFO 释放、可重复局部回滚、移动所有权和累计审计，无效句柄复用 `AXM-TX-E-0003`，取消仍以 `AXM-TX-E-0007` 优先恢复整事务。首次全量 CTest 为 15/16，保存点移动所有权夹具误提交孤立顶点，触发既有 `AXM-TOPO-E-0013` 不变量并使取消断言误报；repair 改为提交对既有面的合法曲面替换。第二次全量为 15/16，`axiom_ops_heal_test` 在闭合样条扫掠阶段 120.03 s 超时；repair 把体验证的重复全量反向索引扫描改为一次收集线性核对，复用已完成的闭壳/流形/Strict 结果，并用无序边键物化闭合扫掠，定向回归为 95.88 s、通过 120 s 限制。调度器最终独立完整构建成功，`ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` **16/16 通过、0 失败、总耗时 120.81 s**（`axiom_ops_heal_test` 95.13 s，IO workflow 12.78 s，representation/IO 9.54 s，Geo 0.58 s，Topo 0.13 s，性能基线 1.57 s）。构建日志仍有一处本批 `BoundaryEdge` 缺失成员初始化告警，以及既有聚合初始化/未使用参数告警，未导致构建或测试失败。本批无“待验收/待统一门禁”状态。

> **第 73 切片（FR-TOPO-001，专项门禁闭合）**：跨环边界冲突从单段 Line/LineSegment 扩展到带显式裁剪区间的 CompositePolyline 与仅含线性子曲线的 CompositeChain；按真实参数方向精确拆分线性片段，支持递增/递减区间，并保留“复合曲线内部折点 ≠ 拓扑边端点”语义。`first_boundary_conflict` / `create_face` / `validate_face` 继续共用同一精确线段谓词；回归覆盖内部相交、共线重叠、容差邻近、只读预检、建面零污染与存量面验证。完整构建、Geo/Topo/Query/Core 定向回归 4/4 及除既有超长 `axiom_ops_heal_test` 外的 CTest **15/15 通过、0 失败、总耗时 25.57 s**，文档检查 35/35 通过；当时未闭合的圆锥曲线/样条误差受控求交已由第 74 批在显式 trim 子域内闭合。

> **第 72 批（FR-TOPO-001 / FR-QUERY-001，专项门禁闭合）**：公开 `create_trimmed_edge` 与 `edge_curve_interval`，在 EdgeId 分配前校验参数有限非零、曲线定义域和参数端点与 v0/v1 的容差一致性；显式裁剪曲边的长度查询复用真实曲线区间，解析区间极值或样条控制点凸包进入拓扑 bbox。旧 `create_edge` 兼容，未携带区间的曲边继续拒绝弦长冒充弧长；失败不写事务计数或几何求值缓存。完整构建与文档检查通过；除既有超长 `axiom_ops_heal_test` 外的 CTest **15/15 通过、0 失败、总耗时 29.61 s**，该 Ops 长矩阵未因本批重跑。

> **第 71 批（FR-GEO-001 / FR-OPS-001 / FR-QUERY-001，已通过完整门禁）**：在已公开的有界曲面 `closest_point_detailed`、带孔 `revolve`、拓扑兼容凹/带孔 `loft` 及 `body_shell_regions` 基础上，新增 `SweepService::revolve_between` 的偏置/对称起始角、顺逆时针不超过一周区间和正负整周语义；新增 `sweep_scaled`，支持开放直线、CompositePolyline、Bezier、BSpline、NURBS 与 CompositeChain 导轨上从 1 按采样弧长线性变化到有限正终端比例；新增 `GeometryIntersectionService::intersect_curve_curve`，返回离散横交/相切/端点、连续重合区间、残差和工作量证据。首次全量 CTest 为 15/16，曲线求交回归失败；repair 以相对端点弦的保守偏差终止 Bezier 相切邻域重复细分，并修正非连续 CompositeChain 子片首尾的右/左极限包围。随后 Geo/Query 定向复验通过，调度器独立完整构建成功；同一命令连续两次完整 CTest 均为 **16/16 通过、0 失败**：总耗时分别为 **1865.97 s**（`axiom_ops_heal_test` 1838.47 s，`axiom_query_eval_test` 0.32 s，`axiom_geometry_test` 0.75 s，性能基线 2.40 s）和 **1863.95 s**（对应 1823.22 s、0.20 s、1.04 s、1.74 s）。门禁日志中的两次文档检查也均为 35 个 Markdown、0 错误、0 警告。构建日志仍有既有聚合初始化、未使用参数和测试窄化转换告警，未导致构建或测试失败。本批及第 60/61/62/65 等既有包均无“待验收/待统一门禁”状态。

本文档用于记录 `AxiomKernel` 当前阶段的实际开发状态、已完成内容、当前风险和下一阶段执行重点。

> **第 70 批（NFR-DIA-001 / FR-OPS-001 / FR-QUERY-001）**：AXMJSON、Axiom IGES 元数据与 Axiom BREP JSON 子集在物化前完成普通文件、64 MiB/短读、严格结构和几何验证，失败绑定 `io.import.<format>.input/path/open/read/parse/validation` 且不污染 Body/Mesh/ID；`SweepService` 新增首尾 G1 连续闭合样条、开放/闭合 `CompositeChain` 导轨和部分角多边形旋转的真实多面体闭壳；`TopologyQueryService` 新增 `shell_mass_properties/body_mass_properties`，从平面直边双边流形闭壳的当前拓扑重算单位密度体积、面积、质心和惯性。repair 修正了测试入口误用、早期 AXMJSON 兼容、扫掠弦段局部邻域判定、闭合复合导轨夹具和旋转闭壳绕向翻转后重算。调度器独立完整构建成功，最终 `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` **16/16 通过、0 失败、总耗时 1539.20 s**（`axiom_ops_heal_test` 1510.78 s，`axiom_io_workflow_test` 14.96 s，`axiom_query_eval_test` 0.17 s，性能基线 1.90 s）。本批门禁已闭合，第 60/61/62/65 等既有包不再有“待验收”状态。当批尚缺的带孔旋转、空腔多壳与有向区间/变截面扫掠已由第 71 批部分闭合；当前仍为保守采样多面体 BRep，并且嵌套复合导轨、显式轮廓历史、曲面/曲边质量积分、相交多壳和标准 IGES 实体仍不支持。

> **第 69 批（FR-GEO-001 / FR-TOPO-001 / NFR-REL-001 / FR-DIAG-001）**：公开曲线全域最近点详细查询与精度/预算/收敛证书；公开跨环有限直线边界冲突预检并补齐共线重叠、容差邻近错误码；拓扑事务新增协作式取消、写者状态和累计取消审计；诊断新增结构化数值证据、证据覆盖审计与 JSON 导出，BOOL 受覆盖失败分支已接入门禁。调度器首次完整 CTest 为 14/16 通过（`axiom_geometry_test`、`axiom_query_eval_test` 失败），repair 以二阶保守距离下界和证书后的剩余预算精修修复两项回归；随后独立完整构建成功，最终 `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` **16/16 通过、0 失败、总耗时 126.00 s**（性能基线 1.63 s）。构建日志仍有 `Issue::numeric_evidence` 聚合初始化缺失及一处既有未使用参数告警，但未导致构建或测试失败。本批门禁已闭合。当时缺少的解析无界面/高阶有理曲面最近点证书、显式 trim 真曲线跨环求交和受限事务保存点已由第 74 批闭合；HEAL/IO 重量级数值证据已由 cycle-0073 闭合；抢占式/长流程内部取消、通用无限派生面证书及非主格式辅助接口证据仍待推进。

> **第 68 批（FR-OPS-001 / FR-QUERY-001）**：公开 `SweepService` 新增 `extrude_to_plane`，并完成整周显式多边形 `revolve` 与旋转最小化标架曲线 `sweep` 的真实多面体 BRep 物化；公开 `TopologyQueryService::face_area` 支持 Plane/Cylinder/Cone/Sphere/Torus 及 Trimmed/Offset 的折线 PCurve 修剪面积。故障复验修复了整周旋转的编译错误，并把周期带孔扫掠的互不连通边界正确物化为多个独立闭壳。调度器独立 `build-agent` 完整构建成功，`ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` **16/16 通过、0 失败、总耗时 136.35 s**（`axiom_ops_heal_test` 111.16 s，`axiom_query_eval_test` 0.15 s，性能基线 1.70 s）。第 60/61/62/65/66 包和第 63/67 包的现有回归也在该次全量门禁中通过，不再标记为“待统一验收”。需求仍为进行中，因为解析扫掠/旋转曲面、通用样条面积、曲边裁剪参数与通用质量属性等尚未闭合。

> **第 58 切片（FR-DIAG-001）**：严重级别检索先收集匹配报告、按 `DiagnosticId` 升序排序，再应用 `max_results`；`report_ids_by_severity` 继承相同语义。`axiom_diagnostics_test` 覆盖多报告限额、重复 issue、空报告/空结果、非法上限、源报告不污染及失败后重试。需求保持受限可用；重量级流程的阶段、实体和数值证据尚未全覆盖。

> **第 57 切片（NFR-REL-001）**：`create_body` 在写入前拒绝壳成员面空或悬空曲面引用，避免有效包围盒掩盖受损拓扑；复用 `AXM-TOPO-E-0005` 并关联壳、面、曲面 ID。`axiom_topology_test` 覆盖故障注入、诊断 JSON、ID/存储/事务计数不污染、修复后重试、回滚和提交。该切片当时尚无协作式取消；拓扑 API 边界取消已由第 69 批闭合，更广泛 S0/S1 失败注入仍待推进。

> **第 50 切片（NFR-DIA-001）**：3MF 导入的物化前输入、路径、打开、读取、解析和网格验证失败绑定 `io.import.3mf.*` 根因阶段；目录输入在读取前结构化拒绝，非法数值与超范围索引不再抛出常规解析异常或被截断。`axiom_io_workflow_test` 覆盖阶段检索、JSON、退化网格、Body/Mesh 不污染及成功重试，`axiom_io_dataset_test` 验证往返；需求保持受限可用。

> **第 49 切片（FR-DIAG-001）**：按相关实体检索先收集命中报告，按 `DiagnosticId` 升序排序，再应用 `max_results`，避免无序存储遍历导致限额结果不稳定。`axiom_diagnostics_test` 覆盖成功、空报告/空结果、重复 issue、限额、非法参数、源报告不污染与失败后重试；`report_ids_by_entity` 继承相同语义。需求保持受限可用；重量级流程的阶段、实体和数值证据尚未全覆盖。

> **第 48 切片（NFR-REL-001）**：`create_shell` 在分配壳 ID 前拒绝成员面悬空或空曲面引用，复用 `AXM-TOPO-E-0005` 并关联面、曲面 ID。`axiom_topology_test` 覆盖受损输入、诊断 JSON、ID/存储/事务计数不污染，以及修复输入后的重试、回滚和提交。该切片当时尚无协作式取消；拓扑 API 边界取消已由第 69 批闭合，更广泛 S0/S1 失败注入仍待推进。

> **第 45 切片（NFR-DIA-001）**：glTF 导入的物化前输入、路径、打开、读取、解析及网格验证失败绑定 `io.import.gltf.*` 根因阶段；非普通文件在读取前结构化拒绝。`axiom_io_workflow_test` 覆盖阶段检索、JSON、越界索引/退化三角形、Body/Mesh 不污染及成功重试；需求保持受限可用。

> **第 44 切片（FR-DIAG-001）**：问题码前缀检索在应用结果上限前按 `DiagnosticId` 升序排序，稳定返回最早的匹配报告。`axiom_diagnostics_test` 覆盖成功、空报告/空结果、多个匹配 issue、限额、非法参数、源报告不污染及失败后重试。需求保持受限可用；重量级流程的阶段、实体和数值证据尚未全覆盖。

> **第 40 切片（NFR-DIA-001）**：STL 导入的物化前输入、路径、打开、读取、解析及网格验证失败绑定 `io.import.stl.*` 根因阶段；非普通文件在读取前结构化拒绝。`axiom_io_workflow_test` 覆盖阶段检索、JSON、Body/Mesh 不污染及成功重试；需求保持受限可用。

> **第 39 切片（FR-DIAG-001）**：阶段精确与前缀检索在应用结果上限前按 `DiagnosticId` 升序排序；`axiom_diagnostics_test` 覆盖限额、空结果、非法参数、源报告不污染及重试。需求保持受限可用。

> **第 35 切片（NFR-DIA-001）**：OBJ 导入的五类物化前失败现在携带 `io.import.obj.input/path/open/parse/validation` 根因阶段；非普通文件在读取前结构化拒绝。`axiom_io_workflow_test` 覆盖检索、JSON、Body/Mesh 不污染和成功重试；需求保持受限可用。

> **第 25 切片（NFR-DIA-001）**：体级、壳级及批量壳级自交验证的现有失败出口现在携带 `heal.validate_self_intersection.*` 细分阶段，并关联目标 Body/Shell；`axiom_heal_test` 覆盖正常 Strict、非法/异属句柄、空批量、阶段检索、JSON 与失败不污染。需求保持受限可用，网格 SAT 仍为近似验证能力。

> **第 19 切片（FR-DIAG-001）**：严格网格导出 QA 失败 `AXM-IO-E-0006` 现在携带 `io.export.mesh_strict_qa` 阶段和输入 Body；`axiom_io_workflow_test` 覆盖合法网格、越界索引、退化三角形、阶段检索、JSON 导出及失败不污染。需求保持受限可用。

**相关文档（计划层）**：[主开发计划与阶段路线图](AxiomKernel_主开发计划与阶段路线图.md)（总阶段与退出标准）、[MVP 实施蓝图](AxiomKernel_MVP实施蓝图.md)（交付拆解）、[几何引擎功能需求文档](../requirements/AxiomKernel_几何引擎功能需求文档.md)（需求对照见本文 §3.3）、[近期迭代与 Backlog](AxiomKernel_近期迭代与Backlog.md)（近期执行项）、[变更纪要](AxiomKernel_变更纪要.md)（批次历史摘要）；工程约定见仓库根目录 `AGENTS.md`；文档导航见 [`docs/README.md`](../README.md)；结构与文档治理问题见 [`docs/architecture/AxiomKernel_项目结构与文档治理建议.md`](../architecture/AxiomKernel_项目结构与文档治理建议.md)。

## 1. 当前阶段

当前阶段口径为：

`Stage 1 已达成；Stage 2 可测基线已达成；当前主线为 Stage 5：修复、导入导出、三角化（进行中）`

当前唯一退出任务为 **cycle-0087 / S5-EXIT**，`stage_task_id=S5-EXIT`、`stage_outcome=ready_for_acceptance`。[两条逐项证据](../quality/AxiomKernel_测试与验收方案.md#114-cycle-0087--s5-exit-门禁与逐项证据)与[集成支持矩阵](../api/AxiomKernel_详细模块接口清单.md#1112-stage-5-集成退出支持矩阵cycle-0087--s5-exit)覆盖固定导入→验证→修复→三角化→导出/再导入、独立几何参考、稳定拒绝与失败隔离。调度器一轮完整构建成功，CTest **16/16、0 失败、196.58 s**；必需 io_workflow/io_dataset/heal/representation_io **13.05/0.55/0.78/14.32 s**，性能 **1.92 s**，无 repair。develop 的“尚未运行/待统一门禁”已由本批代码门禁取代；文档本轮同步，最终文档门禁及调度器提交成功未记录，不能记正式已验收。FR-IO-001 / FR-REP-001 保持受限可用，Stage 5 进行中，不追认历史阶段或 S5-HEAL/S5-IO/S5-TESSELLATION 的提交/验收。本包已收口，不扩展功能；阶段任务优先于需求权重、历史 remaining 和新增变体，基础层仅限直接阻断项。

### 1.1 仓库事实入口（本轮与代码/构建对齐）

以下条目为**可机械核对**的单一事实来源，避免“文档叙事”与仓库脱节：

- **`ctest` 清单**：以根目录 `CMakeLists.txt` 中 `add_test(NAME ...)` 为准；当前共 **16** 条测试目标，枚举见 **§2.3**（与 `AGENTS.md`「当前仓库的测试目标」一致）。
- **实现路径**：`src/axiom/<module>/` 与 `include/axiom/<module>/` 同构；内部实现分片在 `src/axiom/internal/**`。
- **`MathCore` 真源**：仅 `src/axiom/math/**` 参与 `cmake/AxiomKernelLibraries.cmake` 中的 `axiom_math`；**不应**再出现未纳入构建的 `src/math/**` 平行副本。
- **测试布局**：按 `tests/<模块>/` 为主；**`heal`** 有独立 `axiom_heal_test`（`tests/heal/heal_test.cpp`），**`ops` 与 heal 交叉工作流**仍由 `axiom_ops_heal_test` 承载；**`core` 运行时不变量**由 `axiom_kernel_runtime_invariant_test` 承载；**`io` 固定数据集**由 `axiom_io_dataset_test` + `tests/data/io` 承载。

### 1.2 模块完成度一句话（相对工业几何引擎）

| 模块 | 一句话 |
|------|--------|
| **core / sdk** | 门面、运行时 store/遥测、不变量门禁与拓扑取消审计已显著强于纯骨架，**更细隔离策略与事件级 Eval 接入**仍偏薄。 |
| **diag** | 检索、JSON、阶段/实体/数值证据、批量归档与证据审计已具备，HEAL/IO 重量级包已纳入模块门禁，非主格式辅助接口及更广泛语料仍待补齐。 |
| **math** | 本阶段退化/尺度/容差谓词已在 `axiom_math_services_test` 固化，**全链路热点统一入口**仍待持续收敛。 |
| **geo / topo** | 曲线全域最近点证书、有限直线跨环冲突、Strict/trim 与可取消事务持续加严，**曲面证书、一般曲线 trim 和完备规则集**仍差。 |
| **rep / io** | cycle-0085 固定四格式闭环/精确 double 参考、实际 STL 积分、读取预算和单主文件发布保护已通过完整门禁；**标准实体级互操作、单位转换与误差预算工业闭环**仍缺失。 |
| **ops** | 显式轮廓拉伸、旋转、开放/周期扫掠与分段截面律已有真实多面体回归，解析建模、任意截面匹配与工业布尔仍有显著缺口。 |
| **heal** | 第一代平面单壳验证→真实修复→Strict、独立参考及失败/回滚重试已通过 cycle-0084 完整门禁，**BRep 全场景与工业修复**仍受限。 |
| **eval** | 图治理与遥测已接入门面与不变量测试，**与真实重建链路的指标门禁**仍弱。 |
| **plugin** | 进程内注册与策略较完整，**OS 级隔离与供应链安全**未具备。 |

## 2. 已完成项

### 2.1 文档体系

已完成：

- 架构文档
- 功能需求文档
- 详细模块接口清单
- MVP 实施蓝图
- 测试与验收方案
- 错误码与诊断码字典
- 模块依赖图与时序图
- 接口调用样例集
- 用户可读错误文案映射表
- 术语表与命名约定
- 代码目录与编码规范建议
- CI 与发布流水线建议
- 代码评审清单模板
- 合并请求模板
- 缺陷分类与处理流程
- 发布说明模板
- 构建系统与编译选项建议
- 插件开发样例集
- REST 与远程调用样例集
- 基准数据集与性能管理规范
- 主开发计划与阶段路线图

### 2.2 代码工程

已完成：

- `CMake` 工程初始化
- 目录结构：`include/axiom/`（公开 API）、`src/axiom/<module>/`（实现，与公开头同构）、`src/axiom/internal/`（私有头与分片 `.inc`）、`tests/`、`examples/`、`cmake/`（约定见 `AGENTS.md`）
- **实现真源约定**：`MathCore` 与其它模块一致，以 `src/axiom/math/**` 为准；**不再保留**未纳入 `cmake/AxiomKernelLibraries.cmake` 的平行 `src/math/**` 副本，避免“改了未编译源”的漂移
- `Kernel` 门面骨架
- `core/math/geo/topo/rep/ops/heal/eval/diag/io/plugin` 模块头文件与实现骨架
- **测试目录**：`tests/heal/`（`axiom_heal_test`，纯验证/修复与批量 API 回归）、`tests/sdk/kernel_runtime_invariant_test.cpp`（`axiom_kernel_runtime_invariant_test`）、`tests/data/io` + `axiom_io_dataset_test`（小型固定数据集与导出策略烟测）

### 2.3 构建与执行

已完成：

- 工程配置通过
- 工程完整编译通过
- 示例程序运行通过
- **多专项回归测试**接入 `ctest`（维持可重复门禁）

当前测试包括：

- `axiom_smoke_test`
- `axiom_kernel_runtime_invariant_test`（`tests/sdk/kernel_runtime_invariant_test.cpp`，运行时 store reset/可观测与配置失败不污染）
- `axiom_plugin_sdk_test`（对应 `tests/sdk/plugin_sdk_test.cpp`，插件注册/能力发现/门面 JSON）
- `axiom_boolean_workflow_test`
- `axiom_io_workflow_test`
- `axiom_io_dataset_test`（`tests/data/io` 固定数据集 + `ExportOptions` 组合烟测）
- `axiom_diagnostics_test`
- `axiom_geometry_test`
- `axiom_topology_test`
- `axiom_representation_io_test`
- `axiom_ops_heal_test`
- `axiom_heal_test`
- `axiom_query_eval_test`
- `axiom_boolean_prep_test`
- `axiom_math_services_test`
- `axiom_perf_baseline_test`（默认 30s 超时，可用环境变量调节负载）

## 3. 当前状态判断

### 3.1 已稳定内容

- 项目结构
- **结构与测试入口治理**：`Heal` 已具备独立 `axiom_heal_test`；`Math` 源码真源与 CMake 一致；`IO` 具备 `axiom_io_dataset_test` 数据集门禁；`Core` 运行时重置/缓存/Eval 遥测等与 `axiom_kernel_runtime_invariant_test` 对齐
- 模块边界
- 公开 API 骨架
- 诊断结果和结果对象模式
- 错误码常量与诊断辅助构造层
- 基础构建、测试、示例主链路
- 基础几何求值能力已不再完全占位
- 拓扑事务具备更严格的输入校验
- 拓扑查询已具备基础反向邻接追溯能力
- 拓扑桥接查询已具备基础索引化支撑
- 表示层、查询层和 IO 主链路具备了更多基础语义
- 操作层、验证层和修复层开始具备更清晰的失败/告警/修复语义
- 查询层与评估图已具备基础失效传播和重算语义
- 布尔前置判定与近似求交语义进一步增强
- 结果体来源追踪已具备基础查询能力
- 拓扑体参与操作后的来源壳/来源面传播进一步增强
- 几何层已具备基于局部坐标框架的基础曲面求值与参数反求能力
- 曲线层已开始具备与法向/主轴一致的定向求值和包围盒语义
- 派生结果体已开始区分“实际拥有拓扑”和“来源拓扑引用”语义
- 样条曲线已开始具备区分 `Bezier/BSpline/NURBS` 的基础求值与参数域语义
- 样条曲面已开始具备基础控制网格求值、定义域与近似反求语义
- 部分派生结果体已开始具备最小可物化的 owned shell/face 骨架
- 部分派生结果体的最小物化骨架已从单占位面推进到闭合多面壳骨架
- `Strict` 拓扑验证已开始检查闭合壳边使用次数与来源拓扑引用一致性
- 结果体物化已开始优先复用来源闭合壳拓扑，而不再总是退回统一 `bbox` 占位壳
- 结果体物化已开始支持 `source_faces -> source_shells` 自动推导，减少操作层显式来源壳拼装
- 结果体物化已开始优先尝试 `source_faces` 驱动的局部闭合面域重建，再回退到整壳克隆或 `bbox` 壳
- 结果体物化已开始在多壳场景按 `source_bodies` 做来源壳亲和筛选，降低跨壳误扩展概率
- 结果体物化已补来源引用净化与一致性过滤，避免已失效来源引用在多代派生链路中累积
- 结果体物化已补齐面级与壳级 `source_faces` 净化，避免来源面失效后壳/体校验被悬空引用击穿
- STEP 导入导出已开始保基础体类型与参数元数据，不再只保 `bbox/label`
- 修复链路中的缝合结果已开始具备最小可物化的 owned topology 骨架
- STEP 导入主链路已开始默认触发自动验证，并把验证结果折回导入诊断
- 验证层已开始识别派生结果体最小闭合壳骨架被破坏的场景
- STEP 导入主链路已开始支持“验证失败后自动修复并返回派生体”语义
- 自动修复诊断已开始记录修复前问题与修复后验证通过结果
- 自动修复与导入修复诊断已开始结构化记录受影响实体 `related_entities`

### 3.2 仍处于占位或简化实现的内容

- 曲线曲面求值仍偏示意性
- **布尔**：cycle-0083 / S4-EXIT冻结生产代码/公开API/既有码，以回归收口固定第一代工业模型集：真实并/差/交owned重建→Strict/来源/查询→owned网格/OBJ独立V/A，偏移盒U/D/I各总计两轮；暖缓存下七阶段诊断、Eval依赖/输入摘要与writer/保存点/rollback重试均有[逐项证据](../quality/AxiomKernel_测试与验收方案.md#110-cycle-0083--s4-exit-门禁与逐项证据)及[退出支持矩阵](../api/AxiomKernel_详细模块接口清单.md#824-stage-4-退出支持矩阵cycle-0083--s4-exit)。本批完整CTest **16/16、0失败、194.58 s**；现有性能基线 **2.03 s**只测兼容run/查询，run_rebuilt工业性能未认证。 支持限嵌入平面直边ExactBRep闭壳、double/奇偶材料及可解析外向源壳；内部共面、同形/同ID和面相切有真实参考，公开只读prep仍拒绝共面。边/点Union、未解析薄层/容差带、曲面曲边/ExactCritical拒绝，Safe只修人工共面分片及一致共线节点。保留Strict至少六面及近似网格自交门禁，不证明全局嵌入或精确谓词；人工节点截面可能明确数值拒绝。兼容run仍含bbox代理实体语义，输入隔离是公开几何/拓扑摘要而非完整序列化。
- 修复器和验证器已有基础行为语义，但仍远未达到工业级
- 三角化和表示转换已有基础语义，但距离工业级仍有明显差距
- 插件仍是骨架实现，求值图已具备基础状态能力但仍远未达到参数化求解级
- 拓扑-BRep 桥接仍较弱；布尔输出已可出现切分后面数变化等原型拓扑，但稳定工业级重建与全几何类型覆盖仍不足

### 证据口径说明（文档=代码真实状态）

本节及后续“完成度判断”以 **代码与测试为证据**，并遵循硬规则：**接口存在 ≠ 完成**。判定口径：

- **已完成（基础可回归）**：主链路实现存在 + 关键失败链路有明确状态/诊断码 + `tests/<模块>/*_test.cpp` 有回归覆盖
- **部分完成**：实现存在但仍以占位/近似/回退链路为主，或测试/诊断覆盖不完整
- **未开始/缺失**：无实现，或仅有接口/空语义

为便于追溯，本仓库按 **`tests/<模块>/`** 组织；部分模块具备**多个** `ctest` 入口（如 `heal`、`io`、`sdk`），核心证据路径如下（随代码演进可补充）：

- **core/sdk**：`include/axiom/sdk/kernel.h`、`src/axiom/sdk/kernel.cpp`、`tests/sdk/smoke_test.cpp`、`tests/sdk/kernel_runtime_invariant_test.cpp`、`tests/sdk/plugin_sdk_test.cpp`；示例 `examples/minimal_plugin.cpp`
- **diag**：`include/axiom/diag/diagnostic_service.h`、`src/axiom/diag/diagnostic_service.cpp`、`tests/diag/diagnostics_test.cpp`
- **io**：`include/axiom/io/io_service.h`、`src/axiom/io/io_service.cpp`、`tests/io/io_workflow_test.cpp`、`tests/io/io_dataset_test.cpp`（`tests/data/io`）
- **topo**：`include/axiom/topo/topology_service.h`、`src/axiom/topo/topology_service.cpp`、`tests/topo/topology_test.cpp`
- **rep**：`include/axiom/rep/representation_conversion_service.h`、`src/axiom/rep/representation_conversion_service.cpp`、`tests/rep/representation_io_test.cpp`
- **eval**：`include/axiom/eval/eval_services.h`、`src/axiom/eval/eval_services.cpp`、`tests/eval/query_eval_test.cpp`
- **ops**：`include/axiom/ops/ops_services.h`、`src/axiom/ops/ops_services.cpp`、`tests/ops/boolean_workflow_test.cpp`、`tests/ops/boolean_prep_test.cpp`（修改/查询等亦在本实现中）
- **heal**：`include/axiom/heal/heal_services.h`、`src/axiom/heal/heal_services.cpp`、`tests/heal/heal_test.cpp`；交叉工作流回归仍见 `tests/ops/ops_heal_test.cpp`
- **math**：`include/axiom/math/math_services.h`、`src/axiom/math/math_services.cpp`、`tests/math/math_services_test.cpp`

### 各模块完成度一览（架构快照，相对「工业几何引擎」）

说明：下表 **不等于接口是否齐全**，而是指在工业场景下可用的**算法深度、容差与可诊断闭环**是否到位。`Stage2 内可用度` 表示 Stage 2 可测基线及其持续深化对后续开发的支撑程度。

| 模块 | Stage2 内可用度 | 工业化差距 | 主要回归/证据（ctest） | 最突出不足 |
|------|----------------|-----------|------------------------|------------|
| **core** | 中 | 高 | `axiom_smoke_test`、`axiom_kernel_runtime_invariant_test` | **工程化不变量**：**拓扑/Eval 可观测 JSON** + **Bridge 下游步数** + **`core_runtime_invariants_hold` 等组合门禁** + 写分项/累计；更深**隔离级策略矩阵**仍弱 |
| **math** | 中 | 高 | `axiom_math_services_test` | 工业级鲁棒谓词、尺度自适应容差在全链路的一致解释仍未闭合 |
| **geo** | 中 | 高 | `axiom_geometry_test` | 高质量 B-Spline/NURBS、曲率与鲁棒最近点、真实 trim 语义 |
| **topo** | 中 | 高 | `axiom_topology_test` | 规则集仍不完整；但 **trim bridge、Strict 闭合性、平面/球/柱/锥/环面无 PCurve 外环绕向、`coedge_to_loop` 环校验**已进入回归 |
| **rep** | 中 | 高 | `axiom_representation_io_test` | 全几何类型的误差预算三角化、工业 round-trip、显示/UV seam |
| **io** | 中 | 高 | `axiom_io_workflow_test`、`axiom_io_dataset_test`、`axiom_smoke_test` | **工业级全格式**（标准 IGES/STEP 实体、通用 3MF 等）仍缺；**严格导出与侧车报告**已有首批闭环（见 §3.2.2 io）；批处理/失败路径诊断覆盖仍可加强 |
| **ops** | 低 | **极高** | `axiom_boolean_workflow_test`、`axiom_boolean_prep_test`、`axiom_ops_heal_test` | 平面受限布尔闭环已有固定参考，通用工业布尔、特征建模真实拓扑及圆角倒角仍缺 |
| **heal** | 中偏低 | 高 | `axiom_heal_test`、`axiom_ops_heal_test`、`axiom_io_workflow_test` | 自交、流形性完备、容差冲突与小特征工业修复与回放 |
| **eval** | 中 | 高 | `axiom_query_eval_test` | 与真实建模/重建深度耦合、缓存与指标门禁 |
| **diag** | 中偏高 | 中偏高 | `axiom_diagnostics_test` | BOOL 受覆盖分支及 cycle-0073 HEAL/IO 重量级证据门禁已通过；非主格式辅助接口与更广泛失败注入语料仍待扩充 |
| **plugin & sdk** | 低～中偏低 | 高 | `axiom_smoke_test`、`axiom_plugin_sdk_test`、`axiom_diagnostics_test`（插件注册失败诊断） | **OS 级隔离/安全**仍未具备；动态加载与签名校验；长期兼容性策略需产品化 |

### 模块完成度与不足（详细版，可转 backlog）

说明：本节把“工业化差距”展开成**可执行缺口**。每条缺口建议具备 DoD：实现 + 诊断 + 回归（必要时含性能门禁）。

- **core（Kernel/State/Result/Stores）**
  - **已具备**：`Kernel` 门面、基础配置写入/查询、运行时 store 清理/重置、对象计数与能力报告框架；**`io_supported_formats` / `io_can_import_format` / `io_can_export_format` 已与 `IOService::detect_format`、`import_auto`、`export_auto` 对齐**，并由 `axiom_smoke_test` 回归；**`runtime_store_counts`**（含 **`tessellation_metrics` 快照**）/ **`reset_runtime_stores`** 与 **`topology_version_next` / `topology_commit_audit`（成功提交次数、写操作累计、**`TopologyCommitWriteBreakdown` 上次/累计分项**，由 `TopologyTransaction::commit` 写回 `KernelState`；**`topology_commit_breakdown_created_entities_total`** 与能力报告 **`Core.Topology.Audit.Cumulative.CreatedEntities`** 同源）/ `eval_graph_metrics`（节点/失效/重算、**依赖出边节点数/依赖边总数/体绑定记录数与引用总数** + 内嵌 `EvalGraphTelemetry` + **`EvalInvalidationBridgeMetrics`**（`detail::invalidate_eval_for_*` 引擎入口计数 + **`downstream_invalidation_steps`**：`invalidate_eval_downstream` 每次入口，与 SDK `invalidate` / `invalidate_many` 共用，与 Heal 等非 SDK 路径对齐）；能力报告含 **`Core.EvalGraph.Telemetry.*`**（含 **`InvalidateManyBatches` / `InvalidateManyNodeTotal`**）、**`Core.EvalGraph.Bridge.*`**（含 **`DownstreamInvalidationSteps=`**）、**`Core.EvalGraph.DependencyEdges` / `BodyBindingRecords` / `BodyBindingRefs`**、**`Core.Topology.Audit.Cumulative.CreatedEntities`**）** / **`export_topology_commit_audit_json` / `export_eval_graph_metrics_json` / `export_runtime_observability_json`**（合并快照与分项导出字段一致；**`Core.Export.RuntimeObservabilityJson=1`**）** / **`kernel_config_numeric_wellformed` / `topology_version_audit_consistent` / `eval_graph_store_maps_consistent` / `core_runtime_invariants_hold`**（数值容差良定义、`next_version` 与提交审计字段自洽、**EvalGraph 各表键与依赖边节点引用对齐，且 `eval_dependencies`↔`eval_reverse_dependencies` 逐边互指**、三角化缓存一致；**`Core.EvalGraph.StoreMapsCheckedByCoreInvariants=1`**；供宿主/CI 一键门禁）** / **`runtime_tessellation_caches_consistent` / `tessellation_cache_stats` / `export_tessellation_cache_stats_json` / `prune_stale_tessellation_cache_entries`**（悬挂三角化缓存剔除 + stale 统计累计）、**`set_enable_cache` / `set_precision_mode`**、容差 **`isfinite` 校验**、能力报告 **`Core.*` 快照行**（含 **`Core.Topology.Isolation.Effective`**、**`Core.Topology.Audit.*`**、**`Core.EvalGraph.*`**）；**`reset_runtime_stores` / `EvalGraphService::clear_graph` 同步清零 `EvalGraphTelemetry`、`EvalInvalidationBridgeMetrics` 与 `TessellationCacheStats`**；由 **`axiom_kernel_runtime_invariant_test`** 回归
  - **主要不足**：
    - **工程化不变量门禁**：多隔离级/写集细粒度**策略**与「建模事件→Eval」**事件级**全链路仍待加强（当前为门面快照 + **提交写分项/累计** + **Eval 依赖边/体绑定/下游步数** + 遥测 + **组合不变量门面**）
  - **建议测试入口**：`axiom_smoke_test`、`axiom_kernel_runtime_invariant_test`

- **diag（Diagnostics）**
  - **已具备**：错误码/诊断码常量、诊断报告、JSON 导出（含 `Issue.stage` 与 `Issue.numeric_evidence`）、按 related entity 检索；BOOL/HEAL/IO 关键路径已绑定阶段标签并有回归断言；**按 `Issue.stage` 聚合**：`issue_stage_histogram`、`export_grouped_by_stage_txt/json`（空 `stage` 计入 `(unset)`；第 24 切片补齐空路径预校验及写入/关闭失败检测）；**全量归档**：`export_all_reports_json` / `export_all_reports_txt`；**结构化证据门禁**：`audit_evidence` / `export_evidence_audit_json` 按问题码、阶段前缀和最低严重级别审计阶段、实体与有限数值证据，重复报告去重并统计截断明细，BOOL 受覆盖失败分支与 cycle-0073 HEAL/IO 重量级包已接入
  - **主要不足**：
    - **证据门禁范围扩展**：HEAL 验证/修复/回滚及 IO 主格式导入导出/后验验证/批处理已通过 cycle-0073 模块门禁；非主格式辅助接口与更广泛失败注入语料仍待补齐
  - **建议测试入口**：`axiom_diagnostics_test` + 对应 workflow 测试中的失败分支断言

- **math（Tolerance/Predicate/Linear Algebra）**
  - **已具备**：容差解析与尺度策略（`effective_*`、`resolve_*_for_scale`、`scale_policy_for_body_nonlinear`）、线代/谓词；**本阶段已在 `axiom_math_services_test` 固化**：退化/大尺度定向、非有限输入与门限、`orient2d/3d_effective` 与 `max_local`/`min_local` 钳制、负向定向对偶、`point_on_segment_*` 与 Rep/点在体内容差对齐等回归矩阵
  - **主要不足（长期）**：
    - **工业级全链路一致解释**：Geo/Topo/Ops/Heal/Rep 热点仍须持续收敛到同一容差解析入口并补跨模块回归
    - **更强谓词**：Shewchuk 级精确算术或扩展符号判定等仍不在本阶段范围
  - **建议测试入口**：`axiom_math_services_test`（本阶段 P1 条目已闭合）；跨模块对齐见各 workflow 测试

- **geo（Curves/Surfaces/PCurve/Eval/Closest）**
  - **已具备**：曲线/曲面/PCurve 的创建与 eval/domain/bbox/closest，含批量接口；曲线与曲面均公开 `closest_point_detailed`，返回距离下界、参数不确定度、预算计数与终止原因；Plane/Cylinder/Cone/规则 Sphere/Torus 及其嵌套 Offset 链支持无界域解析自动有限化，高阶 Bezier/BSpline/NURBS 使用随细分收紧的正权有理控制网凸包界，并穿透 Trimmed/嵌套 Offset；Revolved/Swept、修剪孔边界及旧复杂曲面最近参数复用同一主流程且不写 eval 缓存
  - **主要不足**：
    - **曲面全域精度合同后续**：通用无限派生面尚未自动有限化；spindle/horn 环面仍无解析证书；旋转/扫掠面尚缺专用局部几何界，通用退化曲面、极端尺度与大模型性能仍未工业化
    - **真实 Trim 语义**：Trimmed 目前偏“参数域裁剪占位”，缺基于 loop/coedge/PCurve 的修剪边界
  - **建议测试入口**：`axiom_geometry_test`（增加曲率/导数/退化场景后再逐步收紧）

- **topo（Transaction/Query/Validation/Trim Bridge）**
  - **第 74 批一致性与可靠性增量（已通过完整门禁）**：`first_boundary_conflict/create_face/validate_face` 对显式 trim 的圆锥曲线、Bezier、BSpline、NURBS 和混合 CompositeChain 复用 Geo 层受预算/误差约束的有限区间求交，分类内部相交、端点接触、容差邻近与可证连续重合；缺 trim、损坏曲线或预算/数值失败闭合拒绝。`TopologyTransaction` 新增嵌套保存点，支持保留目标的重复回滚、回滚外层时使内层失效、LIFO 释放、移动所有权和取消优先整事务恢复；保存点累计审计纳入 core runtime invariant。当前保存点是内存全拓扑快照。
  - **第 69 批一致性与可靠性增量**：`first_boundary_conflict`、`create_face` 与 `validate_face` 共享有限 Line/LineSegment 最近段流程，统一分类内部相交、端点相接、共线正长度重叠及容差内正距离邻近；新增 `AXM-TOPO-E-0028/0029` 与可查询最近点/距离证据，拒绝不分配 FaceId、不改索引或事务计数。`TopologyCancellationSource/Token`、带令牌事务、显式轮询、状态/写次数查询及累计指标覆盖预取消、写/提交/显式回滚/析构边界，取消恢复完整快照、释放写者槽且不推进版本或成功提交审计。当时尚缺的一般曲线 trim 求交与事务保存点已由第 74 批闭合受支持子域；BOOL/HEAL/IO 长耗时内部轮询仍待实现。
  - **第 56 切片一致性增量**：`create_face` 与 `validate_face` 拒绝不同边界环的非平行直线边在端点相接，`AXM-TOPO-E-0027` 关联两环与两边；`axiom_topology_test` 覆盖外/内及内/内相接、合法面、诊断 JSON、失败不污染和回滚。曲线求交、共线重叠、容差邻近相接、完整 trim bridge 与持久命名仍待覆盖，FR-TOPO-001 保持受限可用。
  - **第 52 切片一致性增量**：`create_face` 与 `validate_face` 检查不同边界环的直线/线段边在三维内部相交，`AXM-TOPO-E-0026` 关联两环与两边；`axiom_topology_test` 覆盖外/内及内/内相交、合法面、诊断 JSON、失败不污染和回滚。当时缺少的端点触碰、共线重叠、容差邻近及显式 trim 真曲线冲突已由第 56/69/74 批次逐步闭合；FR-TOPO-001 仍因完整 trim bridge 等限制保持受限可用。
  - **第 47 切片一致性增量**：`create_face` 与 `validate_face` 拒绝跨环独立顶点的有限三维坐标精确重合，`AXM-TOPO-E-0025` 关联两环与两顶点；`axiom_topology_test` 覆盖外/内、内/内、成功、诊断 JSON、失败不污染及回滚。FR-TOPO-001 仍为受限可用，边段相交、容差邻近相接和完整 trim bridge 尚未闭合。
  - **第 42 切片一致性增量**：`create_face` 在分配面 ID 前拒绝不同边界环共用 VertexId，`validate_face` 对存量面执行同一规则；`AXM-TOPO-E-0024` 关联两环与冲突顶点。`axiom_topology_test` 覆盖外/内及内/内环相接、合法双孔面、诊断 JSON、失败不污染和回滚；FR-TOPO-001 仍为受限可用，几何自交及完整 trim bridge 未闭合。
  - **第 37 切片一致性增量**：`create_face` 在分配面 ID 前检查外/内环的面绑定边数规则，拒绝非同曲线的单/双边环，复用 `AXM-TOPO-E-0003/0004` 并关联问题环；同曲线双弧例外保持。`axiom_topology_test` 覆盖外/内环拒绝、诊断 JSON、状态与写计数不污染、合法重试及回滚；FR-TOPO-001 仍为受限可用。
  - **第 32 切片一致性增量**：`create_loop` 在分配环 ID 与更新索引前拒绝闭合终点之外重复经过同一顶点的自接触环，返回 `InvalidTopology` / `AXM-TOPO-E-0023` 并关联重复顶点及两条冲突定向边。`axiom_topology_test` 覆盖失败、JSON、存储/索引/事务计数不污染、合法重试与回滚；未扩展为几何自交或周期 seam 支持，FR-TOPO-001 仍为受限可用。
  - **第 27 切片一致性增量**：`validate_edge` 以当前线性容差校验两个拓扑端点位于引用的 3D Curve 上；偏离或最近点求解失败返回 `InvalidTopology` / `AXM-TOPO-E-0008`，关联边、曲线与问题顶点。`axiom_topology_test` 覆盖成功、容差边界、失败、JSON、验证不污染与回滚；FR-TOPO-001 仍为受限可用。
  - **第 17 切片一致性增量**：`validate_shell_closedness` 除共享计数外校验同一 Edge 两侧 coedge 必须反向；同向时返回 `InvalidTopology` / `AXM-TOPO-E-0015` 并关联壳、边、两面与两 coedge。`axiom_topology_test` 覆盖同向失败、反向成功、零厚度退化壳、JSON、失败不污染计数及回滚。FR-TOPO-001 仍为受限可用。
  - **第 22 切片一致性增量**：`validate_shell_closedness` 在边双面反向配对通过后继续验证面邻接图只有一个连通分量，拒绝把多个独立闭合实体拼成一个壳；失败返回 `InvalidTopology` / `AXM-TOPO-E-0018`，关联壳及各分量代表面。`axiom_topology_test` 覆盖单个闭合壳成功、两个闭合分量失败、诊断 JSON、验证失败不污染模型/事务写计数及回滚。FR-TOPO-001 仍为受限可用。
  - **本轮一致性增量**：`create_vertex` 在写入前拒绝任一非有限坐标（`InvalidInput` / `AXM-CORE-E-0002`），有限极值与次正规值仍可创建；`axiom_topology_test` 覆盖三个轴的 NaN/±Inf、诊断 JSON、失败不污染计数、后续创建与回滚。FR-TOPO-001 仍为受限可用。
  - **已具备**：事务 begin/commit/rollback、反向索引查询、Strict 的闭合性/来源一致性一部分校验、trim bridge 的最小校验入口；**`validate_loop` 校验 `coedge_to_loop` 与环成员一致**；**若 `loop_to_faces` 已有本环条目则校验与面记录互指、单面归属**（无条目时不判错，兼容「仅有环」的事务中间态）；**`validate_coedge` 校验 `edge_to_coedges` 列出本定向边；若已存在 `coedge_to_loop` 则校验环与成员列表互指**（尚未入环的事务中间态不强制环索引，与 `set_coedge_pcurve` 等兼容；悬挂 coedge 仍由 **`validate_indices_consistency`** 兜底）；**`validate_face` 校验外/内环在 `loop_to_faces` 中恰指向本面**；**若 `face_to_shells` 已有本面条目则校验各壳的 `faces` 列出本面**（孤立面无条目时不强制）；**`validate_shell` 对开放边界/非流形边的告警 Issue 附带 `related_entities`（壳+边）**；无 PCurve 时 **平面/球/圆柱/圆锥/环面** 外环绕向与几何外向的一致性规则已进入 `validate_face` 并由 `axiom_topology_test` 回归
  - **主要不足**：
    - **拓扑规则集仍不完整**：更系统的非流形/悬挂规则与诊断覆盖仍不足（解析面外环绕向已覆盖平面/球/柱/锥/环面管向）
    - **PCurve↔3D 一致性闭环**：`validate_face_trim_consistency` 已加强**全量 trim** 下 PCurve 定义域硬门禁与**平面基曲面**上 UV→3D Newell 与法向对齐；工业级全类型解析面 + 修复策略仍不足
    - **事务可观测性/审计**：读写集、隔离级别、审计统计未落地
  - **建议测试入口**：`axiom_topology_test`（按规则逐条加严并固化诊断码）

- **rep（Tessellation/Conversion/Inspection/Reports）**
  - **已具备**：cycle-0086真实边界支持见[API §7.3.2](../api/AxiomKernel_详细模块接口清单.md#732-stage-5-真实边界三角化与转换一致性cycle-0086--s5-tessellation)：五类未编辑native、owned平面直边凹/孔、认证矩形双线性/一阶等权单位夹持样条与LineSegment Swept/矩形trim；三角内部偏差界及全单元法向检查、精确焊接和发布前有限/退化检查。体面缓存覆盖当前边界与支撑/曲线/PCurve/节点/trim/options；full/local/shell原子发布、batch失败回退、双向round-trip临时状态恢复、mesh_to_brep已建立关联的重复幂等。缓存统计/JSON与runtime_store_counts.tessellation_metrics对齐；三个必需回归随最终16/16、200.26 s通过。
  - **主要不足**：
    - **全类型误差预算**：认证仅覆盖上述子集；通用高阶、一般曲边/非矩形/孔曲面裁剪、Offset/Revolved仍拒绝，固定参考不构成工业全局认证。
    - **显示管线能力**：受限local重算和cache身份/失败门禁已有回归；通用自适应patch、UV seam/unwrap、连通性认证、工业增量及大模型性能仍不足。
  - **建议测试入口**：`axiom_representation_io_test`（报告字段与密度单调性）+ `axiom_io_workflow_test`（导出链路不回归）

- **io（STEP/AXMJSON/glTF/STL + workflow）**
  - **已具备**：STEP/AXMJSON 导入导出主链路；**STL/glTF 导入**（Axiom 导出子集/嵌入数据可解析）；**IGES/BREP/OBJ/3MF** 的 **Axiom 元数据或自描述 ZIP 子集**互操作（非完整工业标准文件）；网格导出 **`ExportOptions::compatibility_mode` 严格门控** + 可选 **`write_mesh_validation_report` JSON 侧车**（`merge` 回流导出诊断）；**`Kernel` 格式能力报告与 `detect_format` / `import_auto` / `export_auto` 已对齐**（`axiom_smoke_test`）；`io_workflow` 含往返与**空路径/未知扩展名**失败诊断断言；**STEP/IGES 子集** HEADER 写入/解析 **`AXIOM_STEP_SCHEMA` / `AXIOM_STEP_ENTITY` / `AXIOM_IGES_ENTITY`** 及 ISO-10303-21 风格 `FILE_SCHEMA` 注释；**OBJ** 跳过 `vn`/`vt`/`o`/`g` 等常见非网格行；**3MF** 模型路径匹配更宽松、XML 顶点/三角形属性顺序容错及 **1-based 三角形索引**自动纠偏；**导出策略矩阵**文档与 **`axiom_io_dataset_test` + `tests/data/io`** 小型 CI 数据集；导出目录**只读探测**与批量失败 **`io.batch_*` 诊断**（含 **`detect_formats_with_paths` / `count_by_format` / `paths_of_format` `AXM-IO-D-0011`**、**批量读取 `AXM-IO-D-0012`**、**`compare_file_text_many_equal` `AXM-IO-D-0013`**、**批量路径写操作 `AXM-IO-D-0014`**（`append_text_many` / `touch_empty_files` / `remove_files` / `ensure_parent_directories`）、**路径变换批量 `AXM-IO-D-0015`**（`normalize_paths` / `compose_paths` / `change_extensions` / **`validate_import_paths`** / **`validate_export_paths`**）、**`export_body_summaries_many` 合并 `D-0010`**）；**`validate_import_path`** 区分空路径 / **不存在（`AXM-IO-E-0001`）** / 非常规文件；**`validate_export_path`/`validate_export_paths`**（父目录可写，批量项 `Issue.stage=io.batch_validate_export`）；**`export_auto_to_directory`** 前置**扩展名白名单**（`AXM-IO-E-0002`）与目录可写探测；**标准 STEP/IGES 物理文件**在 **`AXM-IO-E-0010`/`E-0011`** 同诊断内附 **Info `AXM-IO-D-0016`/`D-0017`**（EXPRESS 类型名 / IGES DE 实体号频度扫描，**非**几何物化）；全实体路线见 **`docs/plan/AxiomKernel_STEP_IGES_标准交换实施路线.md`**
  - **主要不足**：
    - **标准实体级互操作**：标准 IGES/STEP **全实体**交换、通用 3MF/OBJ **全工业阅读器**仍不足（当前为子集 + 渐进兼容 + 标准文件**扫描摘要**；**BRep 物化**仍依赖外部内核集成）
    - **工业交付深化**：侧车字段产品化矩阵、大规模数据集与性能门禁、批处理失败聚合策略仍可加强
  - **建议测试入口**：`axiom_io_workflow_test` + `axiom_io_dataset_test` + `axiom_diagnostics_test`

- **ops（Primitive/Sweep/Boolean/Modify/Blend/Query）**
  - **已具备**：cycle-0082平面直边真实并/差/交owned重建、共面/面相切、空材料nullopt、Strict及受限Safe、来源/查询与事务回滚闭环；兼容run近似路径及部分修改/修复工作流语义；**Modify/Blend 失败与关键告警**在启用诊断时写入 **`Issue.stage`**（如 `modify.offset.self_intersection`、`modify.shell.*`、`blend.fillet.placeholder` / `blend.fillet.multi_edge` 等），`axiom_ops_heal_test` 对典型分支做断言
  - **主要不足（最大缺口）**：
    - **布尔工业范围**：受限平面真实闭环已通过；精确谓词、通用曲面曲边、边点Union/薄层与全局嵌入证明仍缺失
    - **特征建模完整性**：显式多边形的多类 extrude/revolve/sweep 子域已生成真实多面体闭壳，但解析扫掠/旋转曲面、任意放样、曲面/曲边 thicken 及显式轮廓历史仍缺失；平面直边 Face thicken 已由 cycle-0077 补齐
    - **圆角/倒角工业化**：含角区/变半径/失败分类与回归数据集缺失
  - **建议测试入口**：`axiom_boolean_workflow_test`、`axiom_boolean_prep_test`、`axiom_ops_heal_test`

- **heal（Validation/Repair）**
  - **cycle-0084 已通过完整门禁**：受限平面单壳的间隙焊接、方向传播、重复 FaceId/连续零长节点清理→共享拓扑/有限 PCurve→Strict，固定独立 V/A/位移参考、观察不修改、后验/批量失败回滚和重试；八具体 IO 入口传播未修复失败且单项恢复 next_id。
  - **已具备**：Strict 拓扑与来源一致性校验、导入后验证与可选自动修复语义、部分 repair 策略与 related_entities 回流；**网格自交分析**（`mesh_self_intersection` 等内部实现）与 **`validate_self_intersection` / 批量路径**已接入工作流，并由 `axiom_heal_test` + `axiom_ops_heal_test` 共同覆盖（仍为可扩展原型，非工业级完备）
  - **主要不足**：
    - **工业验证项仍不完整**：体/壳级自交与 BRep 全场景、流形性完备规则、容差冲突、参数域异常等仍需系统化
    - **工业修复策略缺失**：小特征/近重复/法向与退化处理策略与回放机制不足
  - **建议测试入口**：`axiom_heal_test`、`axiom_ops_heal_test`、`axiom_io_workflow_test`

- **eval（EvalGraph）**
  - **已具备**：循环保护、失效传播、重算计数与治理接口，已有专项回归；**可观测性计数**（`EvalGraphTelemetry`：`invalidate_*` 调用量、批量失效规模、`recompute` 传递去重跳过次数、重算完成事件数）及 **`telemetry` / `reset_telemetry`**，**`clear_graph` 会清零遥测与 `EvalInvalidationBridgeMetrics`**；**`Kernel::eval_graph_metrics()` 内嵌同一份遥测与 bridge 计数**，**`reset_runtime_stores` 亦清零**（与清空求值图存储一致），由 `axiom_query_eval_test`、`axiom_kernel_runtime_invariant_test` 覆盖
  - **主要不足**：与真实建模/重建深度耦合、缓存命中率等更高层指标门禁仍不足
  - **建议测试入口**：`axiom_query_eval_test`

- **plugin & sdk**
  - **已具备**：`PluginRegistry` 注册与清单、宿主策略与 API 版本门禁、能力发现 JSON、`minimal_plugin` 示例；**`invoke_registered_importer` 在开启导入后自动校验时与 `plugin_import_file` 同语义**（`axiom_plugin_sdk_test`）；smoke 与其它插件诊断回归同前
  - **主要不足**：**进程外/OS 级隔离**、动态库加载与供应链安全；长期 **SemVer/多 ABI** 与兼容性策略仍需产品化
  - **建议测试入口**：`axiom_smoke_test`、`axiom_plugin_sdk_test`、`axiom_diagnostics_test`（插件注册失败诊断）

## 3.3 对照《几何引擎功能需求文档》的差距清单（按模块）

说明：本节用“已完成 / 部分完成 / 未开始”对齐 `docs/requirements/AxiomKernel_几何引擎功能需求文档.md` 的一级需求模块，便于下一批迭代拆解。这里的“已完成”指**具备可回归的最小可用链路**，不等于工业级。为避免与本文自身章节号冲突，以下标题统一写作“需求 7.x”。

### 需求 7.1 几何基础对象管理（GeoCore）

- **FR-GEO-001 第 69 批（已通过完整门禁）**：新增 `CurveService::closest_point_detailed`、精度/预算选项和解析/距离容差/参数容差终止结果；解析覆盖 Line/LineSegment/Circle/CompositePolyline，分支限界覆盖 Ellipse/Parabola/Hyperbola/Bezier/BSpline/NURBS/CompositeChain，样条按全部非空结点段独立覆盖并以保守速度/加速度界剪枝。旧非解析 `closest_parameter/closest_point` 复用该流程；预算、非法选项和数值不可表示均结构化失败且不写求值缓存。首次全量测试的椭圆与 NURBS 参数回归经 repair 修复，最终完整 CTest 16/16 通过。当时尚缺的曲面详细入口已由后续批次公开，第 74 批又补齐了受支持解析无界面和高阶有理控制网证书。

- **FR-GEO-001 第 55 切片**：UV 折线 PCurve 最近参数的逐段投影和距离比较使用扩展精度中间量，有限大坐标的距离平方超出 `Scalar` 范围时仍能选中正确分段。`axiom_geometry_test` 覆盖段内最近点、端点、重复点、非法查询/句柄、稳定错误码、失败不污染与重试。该切片未改变公开签名；3D 曲线全域精度合同已由第 69 批闭合，曲面合同已由第 71/74 批扩展到有界派生面、受支持解析面和高阶有理控制网证书，PCurve 详细证书仍待推进。
- **FR-GEO-001 第 51 切片**：线段最近参数的解析投影使用扩展精度中间量，有限大尺度端点和查询点不再因 `Scalar` 长度平方溢出产生非有限参数；保持端点钳制。`axiom_geometry_test` 覆盖段内最近点、端点、退化创建、非法查询/句柄、稳定错误码、几何与缓存不污染及重试。该切片未改变公开签名；其余 3D 曲线全域精度合同已由第 69 批闭合，曲面合同已由第 71/74 批扩展，PCurve 详细证书及通用无限派生面仍待推进。
- **FR-GEO-001 第 46 切片**：3D 复合折线最近参数逐段解析投影并按三维距离比较，有限大坐标用扩展精度中间量防止平方溢出；等距取最早参数，重复控制点按零长度段处理。`axiom_geometry_test` 覆盖窄分支、段内投影、退化段、大坐标、非法查询/句柄、错误码、失败不污染与重试。未改变公开签名或错误码，其他曲线/曲面全局最近点精度仍待定义，需求保持受限可用。
- **FR-GEO-001 第 41 切片**：UV 折线 PCurve 最近参数逐段投影并比较距离，避免固定全域采样漏掉短分段；重复控制点形成的零长度段可安全参与比较，等距取最早参数。`axiom_geometry_test` 覆盖短分段、段内投影、重复点、非法查询与句柄、错误码、失败不污染及重试。未改变公开签名或错误码，不扩展至任意曲线的全局精度保证，需求保持受限可用。
- **FR-GEO-001 第 36 切片**：椭圆最近参数以粗采样为初值按三维欧氏距离阻尼细化，周期缝结果归一到 `[0, 2pi)`；创建时拒绝轴长或派生法向长度溢出。回归覆盖解析可知最近点、周期缝、非有限查询、有限但溢出的轴向量及失败不污染。未改变公开签名或错误码，不宣称任意退化椭圆全局最优，需求保持受限可用。
- **FR-GEO-001 第 31 切片**：BSpline/NURBS 曲面满重数断点、上端点及域外钳制的点值、一二阶偏导和曲率统一使用同一非空单侧片；回归覆盖双轴断点、非单位权重、常值退化、非法结点失败不污染和既有曲面继续求值。未改变公开签名或错误码，不宣称断点全局可微，需求保持受限可用。
- **FR-GEO-001 第 26 切片**：BSpline/NURBS 曲面最近点初值逐个覆盖非空张量积结点片，修复固定全域网格漏掉极窄、双轴满重数隔离片的问题；回归覆盖多项式与非单位权重有理曲面、非法查询及几何/缓存不污染。未改变公开签名或错误码，不宣称任意曲面全局最优，需求保持受限可用。
- **FR-GEO-001 第 21 切片**：BSpline/NURBS 曲面 u/v 显式结点现在拒绝零长度有效域和超过 `degree + 1` 的结点重数，复用 `AXM-GEO-E-0002`；回归覆盖两轴、两类曲面、合法满重数断点、失败诊断以及几何/缓存不污染。未改变公开签名或错误码，需求保持受限可用。
- **FR-GEO-001 第 16 切片**：BSpline/NURBS 最近参数初值搜索逐个覆盖非空结点分段，修复固定全域网格漏掉极窄、满重数断开分支的问题；回归覆盖显式/推断次数、非单位权重、常值退化、非法查询点诊断和失败不污染。该切片当时仅为初值改进；第 69 批已补曲线全域预算与收敛证书，第 71/74 批已补曲面详细入口及高阶有理控制网证书，剩余缺口是通用无限派生面、退化曲面与大模型性能证书。

- **FR-GEO-001 第 11 切片**：非夹持 BSpline/NURBS 有效域重复端点按非空单侧分段求值，修复端点及导数结点向量选中零长度分段造成的错误点值/导数/曲率；`axiom_geometry_test` 覆盖二次解析参考（含非单位权重）、显式/推断次数、域外钳制、常值退化和失败不污染。未改变公开签名或错误码，需求保持受限可用。

- **曲线类型**
  - **已完成（基础可用）**：直线、线段、圆、椭圆、抛物线、双曲线、Bezier、BSpline（近似）、NURBS（近似）、复合曲线 polyline（占位）
  - **部分完成（Stage 2 minimal）**：复合曲线 chain（子曲线拼接到参数域 `[0,n]` 的最小实现）、`PCurve`（UV polyline 的最小实现，含 eval/domain/bbox/closest）
  - **未开始/缺失（按需求文档口径）**：高质量复合曲线（连续性/弧长参数化/拼接光顺）、完整 `PCurve`（与面参数域/修剪边界的强一致性与桥接规则）
- **曲面类型**
  - **已完成（基础可用）**：平面、圆柱、圆锥、球面、环面、Bezier 曲面（控制网格占位）、BSpline（控制网格）、NURBS（控制网格）
  - **部分完成（Stage 2 minimal）**：旋转面、扫掠面（线性扫掠）、修剪面（参数域裁剪占位）、偏置面（offset record 占位）
  - **未开始/缺失（按需求文档口径）**：真实修剪面（基于 `PCurve` 的 trim loops + 与 3D 曲线一致性）、高质量偏置面（几何意义上的 surface offset + 自交/退化处理）、高质量扫掠/放样/加厚等特征曲面
- **求值/查询能力**
  - **已完成（基础可用）**：eval / bbox / domain（含 analytic 的 ±inf 语义）、closest_point / closest_parameter / closest_uv；曲线 `closest_point_detailed` 已提供完整有效域、预算、距离下界/参数分辨率与终止原因合同
  - **未开始/缺失**：曲面曲率高质量实现；Bezier/BSpline/NURBS 及派生/修剪曲面的完整参数域（含 trim 边界）全域最近点精度、预算与收敛合同

### 需求 7.2 拓扑结构管理（TopoCore）

- **FR-TOPO-001 第 73 切片线性复合边跨环冲突（专项门禁闭合）**：显式裁剪 CompositePolyline 与线性 CompositeChain 按参数折点展开为精确有限线段，共用既有跨环冲突分类、错误码与事务隔离。内部折点不冒充拓扑边端点，递减区间与线性嵌套链可用；当时保守跳过的真曲线子段已由第 74 批接入误差受控 Geo 求交器。

- **FR-TOPO-001 第 72 批显式边裁剪区间（专项门禁闭合）**：`EdgeRecord` 保存有向起止参数；`create_trimmed_edge` 以稳定错误码拒绝非有限/零区间、越域或端点不一致，拒绝发生在 ID 分配前并携带数值证据。`edge_curve_interval` 区分显式区间与兼容旧边；校验器同步检查存量区间。圆、椭圆、抛物线、双曲线、Bezier/BSpline/NURBS、折线及复合链可据真实区间查询长度，拓扑 bbox 纳入解析极值或保守控制凸包。其真曲线跨环求交消费路径已由第 74 批闭合；完整 PCurve trim bridge、周期缝/奇点与持久命名仍待推进。

- **FR-TOPO-001 第 69 批（已通过完整门禁）**：新增只读 `first_boundary_conflict` 及冲突类别、两环/两边、最近点与距离证据；`create_face` 和 `validate_face` 复用同一有限 Line/LineSegment 最近段流程，除既有内部相交和端点相接外，新增 `AXM-TOPO-E-0028` 共线正长度重叠与 `AXM-TOPO-E-0029` 容差内正距离邻近。失败不分配 FaceId、不改反向索引或事务写计数。一般曲线因边尚无显式 trim 区间仍未覆盖，需求保持受限可用。

- FR-TOPO-001 第 12 切片：`create_face` 在写入前拒绝同一面外环/内环及内环之间复用 EdgeId，复用 `AXM-TOPO-E-0014` 并关联两个冲突环与边。`axiom_topology_test` 覆盖单边共享、完全重合边界、正反共边、内环顺序、诊断 JSON、计数/索引不污染、拒绝后合法双孔面及回滚后重新提交；不同面通过独立共边共享 Edge 仍允许。未增加公开签名或错误码，未扩展 seam/周期修剪支持，需求保持受限可用。 验证：`ctest --test-dir build-agent --output-on-failure` 16/16 通过，总耗时 25.86 秒，性能项 2.64 秒；文档检查通过。

- **FR-TOPO-001 第 7 切片**：移除 `validate_loop_record` 对单共边的特殊放行，创建入口与环验证共享首尾顶点 ID 闭合规则；未闭合返回 `AXM-TOPO-E-0002`，创建失败不分配环 ID、不改存储、索引或事务写计数。`axiom_topology_test` 覆盖正反方向、坐标重合但 ID 不同、失败后复用共边构造闭合三角环、回滚保留已有顶点及诊断 JSON。`axiom_ops_heal_test` 的面修改夹具已由单条开放边改为闭合三角环，保留来源追踪等原有断言。当前 `create_edge` 仍拒绝同一端点 ID，故本切片不宣称支持单边周期环；FR-TOPO-001 保持受限可用。验证：`ctest --test-dir build-agent --output-on-failure` 16/16 通过，总耗时 24.91 秒，性能项 2.89 秒；文档检查通过。

- **拓扑元素与关系**
  - **已完成（基础可用）**：Vertex/Edge/Coedge/Loop/Face/Shell/Body；外环与内环；基础反向邻接索引；来源追踪的基础查询
  - **部分完成**：壳闭合/非流形检测（既有告警 + Strict hard；**`validate_shell` 开放边界/非流形告警带壳+边 `related_entities`**）；来源引用一致性 Strict 校验；**面环方向**（无 PCurve：平面/球/圆柱/圆锥侧壁、**环面**管向截面径向）；**环级 `coedge_to_loop` 一致性**；**`validate_loop` 在 `loop_to_faces` 有条目时与面互指**；**定向边级 `validate_coedge`：`edge_to_coedges` 必含本 coedge；若已有 `coedge_to_loop` 则与环成员互指**（未入环中间态不强制）；**面级 `loop_to_faces` 与面外/内环互指**；**`face_to_shells` 有条目时与壳 `faces` 互指**（`validate_face`）；**trim bridge**（UV 闭合、内外环 UV 方向、PCurve↔3D 采样/端点一致性，`validate_face_trim_consistency` 等）
  - **未开始/缺失**：更复杂解析组合面/退化参数域上的绕向规则；悬挂/重复/非流形在「全规则集」意义上的完备覆盖；工业级 **PCurve↔3D** 修复策略与全分支诊断
- **事务/一致性**
  - NFR-REL-001 第 43 切片：`create_shell` 在分配 ShellId 前验证成员面所有内环，拒绝缺失及存在但损坏的内环，复用 `AXM-TOPO-E-0005` 并关联面/环。`axiom_topology_test` 覆盖诊断 JSON、失败后 ID/壳存储/事务计数不污染、修复输入后重试、回滚索引恢复与后续提交。该切片未改公开签名；拓扑 API 边界取消已由第 69 批闭合，更广泛 S0/S1 失败注入仍待推进。验证：`ctest --test-dir build-agent -R '^axiom_(topology|kernel_runtime_invariant)_test$' --output-on-failure` 2/2 通过；`python3 scripts/check_docs.py` 通过。
  - NFR-REL-001 第 38 切片：`create_body` 在包围盒有效性通过后才分配 BodyId，避免受损壳导致的失败消耗全局实体 ID。`axiom_topology_test` 通过受损空壳注入覆盖稳定错误码/JSON、失败后 ID/存储/事务计数不污染、非法句柄退化输入、回滚与后续提交。该切片未改公开 API；拓扑 API 边界取消已由第 69 批闭合，更广泛 S0/S1 失败注入仍待推进。验证：`ctest --test-dir build-agent -R '^axiom_(topology|kernel_runtime_invariant)_test$' --output-on-failure` 2/2 通过；`python3 scripts/check_docs.py` 通过。
  - NFR-REL-001 第 33 切片：同一内核实例强制单活动拓扑写事务，落实 `SnapshotSerializable` 的单写前提；重叠事务保持关闭，写入、提交和回滚失败且不改变所有者模型，所有者关闭或空事务析构后释放写槽。`axiom_topology_test` 覆盖拒绝、提交、回滚重试和空事务释放；第 69 批已在该隔离语义上补齐拓扑 API 边界协作式取消，跨进程隔离仍未提供。验证：`ctest --test-dir build-agent -R '^axiom_(topology|kernel_runtime_invariant)_test$' --output-on-failure` 2/2 通过。
  - NFR-REL-001 第 28 切片：修复 `replace_surface` 成功后未递增 `write_operation_count` 的 S0 可靠性缺口；仅替换曲面的活动事务现在会在析构时正确回滚，提交后总写入数与 `replaced_surfaces` 分项一致。`axiom_topology_test` 覆盖非法替换失败不污染、仅替换作用域回滚、显式回滚归零和提交审计；第 69 批已补拓扑 API 边界协作式取消，细粒度隔离和全量 S0/S1 门禁仍待闭合。
  - NFR-REL-001 第 23 切片：活动 `TopologyTransaction` 离开作用域时由 `noexcept` 析构自动回滚成功写入，避免未提交的创建、删除和替换污染共享 store；空事务析构不生成额外回滚诊断，已关闭事务与移动后的源对象析构保持 inert。`axiom_topology_test` 覆盖创建、既有面替换、级联删除、空事务、移动所有权、显式提交持久性及索引不变量；第 69 批已补拓扑 API 边界协作式取消，细粒度隔离和全量 S0/S1 门禁仍待推进。
  - NFR-REL-001 第 18 切片：`TopologyTransaction` 改为显式唯一所有权；允许移动构造，禁止复制和移动赋值。移动后源对象安全关闭并失去写入、提交和回滚权限，避免默认移动留下 `active_` 而空内部状态导致崩溃，或双重事务权限误撤销已提交模型。`axiom_topology_test` 覆盖编译期所有权约束、移动后失败路径、目标提交/回滚及失败不污染；第 69 批已补拓扑 API 边界协作式取消，细粒度隔离仍待交付。
  - NFR-REL-001 第 13 切片：修复同一事务新建面/壳/体被修改或删除后，回滚从快照复活新建实体的问题（S0：回滚污染）。先恢复快照再清理本事务创建的高层拓扑，保留已有快照/触达计数语义。`axiom_topology_test` 覆盖新建面替换曲面、面/壳/体删除、空壳/空体级联删除、已有面与新建共享壳混合快照、重复删除失败不污染、全部新建拓扑句柄失效、原模型/反向索引恢复、空回滚及后续提交。第 69 批已补拓扑 API 边界协作式取消；细粒度隔离仍待交付。验证：`ctest --test-dir build-agent -R '^axiom_(topology|kernel_runtime_invariant)_test$' --output-on-failure` 2/2 通过；`python3 scripts/check_docs.py` 通过。
  - **NFR-REL-001 第 8 切片**：修复活动事务调用 `clear_tracking_records` 丢失撤销记录的问题（S0：回滚后模型污染）。入口现在返回 `OperationFailed` / `AXM-TX-E-0006`，包括空事务；提交或回滚后仍允许重复清理且不改变模型。`axiom_topology_test` 覆盖创建记录、删除体快照及 PCurve 修改快照在拒绝后保留、后续提交/回滚、计数与句柄不变量、诊断 JSON 和关闭后幂等清理。需求保持受限可用，完整事务隔离与取消仍未交付。验证：`ctest --test-dir build-agent -R '^axiom_(topology|kernel_runtime_invariant)_test$' --output-on-failure` 2/2 通过；`python3 scripts/check_docs.py` 通过。
  - **NFR-REL-001 可靠性增量**：`set_coedge_pcurve` 保存首次修改前的绑定，回滚恢复已有共边的原 PCurve（含未绑定状态）；重复绑定/清除不覆盖原快照，无效句柄拒绝不改变绑定、存储数量或事务写计数。`axiom_topology_test` 覆盖提交保留、重复绑定、清除、失败诊断与回滚后共边验证；需求仍为受限可用，尚不代表完整事务隔离或取消能力。
  - **已完成（基础可用）**：begin/commit/rollback；删除面/壳/体支持；回滚后索引恢复回归覆盖
  - **未开始/缺失**：增量保存点与结构共享、更细粒度写集/读集与隔离级别、完整子事务和跨进程并发语义；BOOL/HEAL/IO 长耗时内部阶段取消轮询

### 需求 7.3 基础建模能力（OpsCore/TopoCore/GeoCore）

- **基础体构造**
  - **部分完成**：`PrimitiveService` 已提供 `box/wedge/sphere/cylinder/cone/torus` 等；物化层对部分 primitive 可走 **解析壳路径**（如 `Wedge` 专用壳、`Cylinder` 棱柱壳等），其余仍常退回 **bbox 壳** 以满足 Strict/闭环回归；派生结果体仍大量依赖最小物化骨架、`source_*` 推导与来源壳克隆
  - **未开始/缺失（工业化）**：与工业内核一致的 **全 primitive 精确拓扑面环 + 与曲面参数域严格一致** 的 BRep；楔体/盒体等若仍部分依赖 bbox 占位壳，需升级为完整解析拓扑与几何
- **特征构造**
  - **部分完成（接口/占位）**：`SweepService` 等接口存在，部分路径产出派生体与工作流语义
  - **cycle-0074 分段截面律（已通过完整门禁）**：`ExtrusionLawStation/extrude_with_law` 以归一化高度对正比例和有向扭角分段线性插值，方向须法向、中心共面、距离有限且正；`SweepScaleStation/sweep_with_scale_law` 以归一化采样导轨弦长分段变比例，`SweepLawStation/sweep_with_law` 联合比例与扭转。首站为 `(0,1)` 或 `(0,1,0)`，末 fraction=1，2 至 4097 站严格递增；保留关键站与原导轨站，数值重合的原站/关键站共享位置与标架，不可分辨的不同关键站失败。支持扩张/收缩/停顿、扭转反向、凸/凹及分离非嵌套孔、空间朝向和方向反转。
  - **采样与物化合同**：每步比例变化不超过较小端 25%，扭角不超过 7.5°，联合采样最多 4096 区间，累计绝对扭角最多一周。扫掠支持线段、严格同向穿过初始截面平面的折线、既有 Bezier/BSpline/NURBS 与非嵌套 G1 CompositeChain；线性导轨绕按净推进定向的初始法向扭转，曲线绕传输标架局部前向切向。周期圆/椭圆及既有闭合样条/复合导轨末比例为 1，联合律末角为 0 或 ±2π（1e-10 rad 容差），焊接无端盖且不推断对称顶点置换。插入曲线标架保持正交与原站端点；最大比例用于保守曲率/间距检查，接触宽相最多 2000000 候选。分配前检查实际舍入截面、盖片、推进/折叠、壁片交叠/容差接触与质量，成功生成真实共享面边点闭壳、中间 bbox 和多面体质量。
  - **验收与限制**：独立积分体积/质心/完整惯性、真实拓扑/Strict/索引/网格、孔洞/凹轮廓/姿态/反向、周期正负整周、中间站边界、旧接口兼容、无效/数值退化/预算与阶段诊断、活动事务失败原子性和编辑回滚重试随 `axiom_ops_heal_test` 通过（106.39 s）。采样/物化/周期扭角失败携 `sweep_law_sampling/materialization/seam` 和有限数值证据，前置输入与拉伸律仍沿用既有诊断。复用 `kCoreParameterOutOfRange/kCoreInvalidHandle`；结果为采样多面体，独立连续律 Gauss 积分是近似对照。零/负比例、尖顶、至平面组合、一般非线性解析律、嵌套复合导轨、任意截面匹配、解析扫掠/螺旋面仍不支持。
  - **cycle-0073 扭转拉伸（已通过完整门禁）**：`extrude_twisted` 的显式凸/凹及分离孔轮廓、空间朝向、方向反转/缩放、正负部分角/整周和零角均已回归；方向须法向、中心共面、正距离、扭角不超过一周。角站差不超过 7.5°，真实三角闭壳/反向索引/bbox/多面体质量/Strict/网格与失败原子性、事务编辑回滚重试通过。repair 交替站间侧壁对角线消除系统性体积偏差，站点数不变；仍是采样多面体，该旧入口未组合变比例或至平面拉伸；cycle-0074 新入口已提供分段比例/扭转组合，至平面组合仍不支持，不是解析螺旋面。
  - **第 71 批 SweepService 增量（已通过完整门禁）**：`revolve` 的整周/部分角显式子午面支持轴分离凹外环及多个分离孔洞；`revolve_between` 增加偏置/对称起始角、顺逆时针不超过一周的有向区间及正负整周，部分角首尾端盖绕向与方向匹配；`sweep_scaled` 将开放线/折线/样条/复合导轨上的截面比例按采样弧长从 1 线性变到有限正终端比例，支持凹外环与孔；`loft` 以相同外环/孔环顶点数定义站间对应，支持倾斜、凹/带孔多截面真实单闭壳。回归覆盖拓扑/质量/角向位置/邻接/bbox/owned mesh/Strict、非法输入零污染、活动事务、回滚和重试；最终完整 CTest 16/16 通过（1865.97 s）。当前仍为每周 48 段/导轨采样的保守浮点多面体 BRep；带孔触轴、零/负比例、一般非线性解析比例律、不同环拓扑自动匹配、分支/尖顶/坍塌截面、嵌套复合导轨及显式轮廓历史仍不支持。
  - **第 70 批 SweepService 扩展（已通过完整门禁）**：端点重合且首尾切向连续的 Bezier/BSpline/NURBS 导轨以 holonomy 校正旋转最小标架，生成无端盖周期闭壳；`CompositeChain` 可连接有界直线/线段、圆/椭圆弧、Bezier/BSpline/NURBS 和 polyline 首段，强制接缝位置与 G1 连续，开放链有端盖、闭合链无端盖；`revolve` 对 `(0,2π)` 显式平面轮廓以每周 48 段的分辨率生成侧壁和约束剖分首尾端盖。三组回归分别覆盖 48、32、48 组成功变体，并覆盖真实拓扑、邻接/bbox、Strict、owned 网格、质量/惯性、失败零污染、活动事务零写入、回滚和重试。完整 CTest 16/16 通过（1539.20 s）。仍不支持解析扫掠/精确旋转、带孔旋转、嵌套复合链、抛物/双曲子段、急弯/自靠近输入和显式轮廓历史。
  - **第 68 批 SweepService 扩展（已通过完整门禁）**：`extrude_to_plane` 沿射线把显式凹/带孔平面轮廓投影到斜目标面；整周 `revolve` 以 48 站周期分片支持离轴环形体及唯一连续轴边闭合的实心轮廓；曲线 `sweep` 以旋转最小化标架支持 Bezier/BSpline/NURBS 开放导轨和整圆/椭圆周期导轨。三条路径均物化实际三角面、共享边/顶点闭壳并在创建时检查闭合多面体积分；cycle-0076 起公开质量每次从当前拓扑重算，不使用创建缓存回退。周期带孔扫掠经故障复验改为外边界与各孔边界分别物化独立闭壳，壳数及 owned 网格连通分量均为 `1 + holes_xyz.size()`；开放带孔导轨仍由端盖连成单壳。回归分别覆盖 240、32、40 组主要变体，以及公开拓扑、bbox、Strict、owned 网格、质量/惯性、退化失败不污染与回滚重试。最终 `build-agent` 完整构建成功，CTest 16/16 通过（136.35 s；`axiom_ops_heal_test` 111.16 s，性能基线 1.70 s）。当前仍是保守浮点剖分/采样多面体 BRep，不是解析扫掠或旋转曲面；带孔旋转、闭合样条/复合导轨、尖点、过紧曲率和自靠近导轨拒绝，部分角旋转仍沿用受限路径。
  - **等比变截面与尖顶拉伸（第 65/66 包，已纳入第 68 批全量门禁）**：`extrude_scaled` 支持凸/凹及单孔/多孔轮廓的正比例收缩、扩张和等截面；`end_scale=0` 支持无孔三角/凸/凹轮廓收敛到共享尖顶。既有 360 组正比例、72 组矩形/L 形尖顶及 4 组四面体回归随本批 16/16 CTest 通过。带孔尖顶、负比例、任意截面放样和逐壁恒角拔模仍不支持；近退化或极端尺度输入可保守拒绝。
  - **带孔拉伸与折线平移扫掠（第 61/62 包，已纳入第 68 批全量门禁）**：显式带孔凸/凹多边形拉伸及线段/折线固定方向平移扫掠已由真实闭壳、公开邻接、解析质量属性、Strict、网格、失败不污染和回滚回归覆盖。折线每段仍须沿截面法向严格同向推进，不支持回退、切向或闭合折线；无显式轮廓路径仍为历史占位。第 60 包无孔凹轮廓及 Rep UV/焊接回归同次通过。
  - **显式多边形线段扫掠受限可用（第 59 切片）**：`sweep` 对有界线段导轨复用真实棱柱拉伸链路，保持轮廓世界坐标并按终点减起点平移；双绕向、反向/斜向、不同轮廓平面均由公开面/边/顶点查询、面面积、质量属性及 Strict 验证回归验收。不支持的轮廓/导轨及退化位移在物化前拒绝，失败不污染；曲线导轨与无显式轮廓路径不属于本轮真实能力范围
  - **未开始/缺失（工业化）**：拉伸/旋转/扫掠/放样/加厚等 **真实几何求交 + 拓扑构造 + 失败可诊断** 的完整实现

### 需求 7.4 布尔运算能力（OpsCore）

- **部分完成**：cycle-0083 / S4-EXIT冻结生产代码/公开API/既有码，以回归收口固定第一代工业模型集：真实并/差/交owned重建→Strict/来源/查询→owned网格/OBJ独立V/A，偏移盒U/D/I各总计两轮；暖缓存下七阶段诊断、Eval依赖/输入摘要与writer/保存点/rollback重试均有[逐项证据](../quality/AxiomKernel_测试与验收方案.md#110-cycle-0083--s4-exit-门禁与逐项证据)及[退出支持矩阵](../api/AxiomKernel_详细模块接口清单.md#824-stage-4-退出支持矩阵cycle-0083--s4-exit)。本批完整CTest **16/16、0失败、194.58 s**；现有性能基线 **2.03 s**只测兼容run/查询，run_rebuilt工业性能未认证。 支持限嵌入平面直边ExactBRep闭壳、double/奇偶材料及可解析外向源壳；内部共面、同形/同ID和面相切有真实参考，公开只读prep仍拒绝共面。边/点Union、未解析薄层/容差带、曲面曲边/ExactCritical拒绝，Safe只修人工共面分片及一致共线节点。保留Strict至少六面及近似网格自交门禁，不证明全局嵌入或精确谓词；人工节点截面可能明确数值拒绝。兼容run仍含bbox代理实体语义，输入隔离是公开几何/拓扑摘要而非完整序列化。
- **剩余（工业闭环）**：通用曲面/曲边与精确谓词、全局壳嵌入证明、任意相切/薄层和工业Safe规则；通用imprint/trim/merge及全部失败分支诊断门禁。平面固定支持域已形成真实重建/Strict/受限Safe闭环，不据此声明全工业范围完成

### 需求 7.5 几何修改能力（OpsCore/HealCore）

- **部分完成**：偏置/抽壳/面替换/删除面补面等接口语义、失败/告警路径与来源传播已有基础回归
- **未开始/缺失**：真实几何修改算法、局部重建、稳定的失败分类（自交/薄壁/高曲率/容差冲突）与可回放修复记录

### 需求 7.6 圆角与倒角（OpsCore）

- **部分完成**：接口与非法输入保护、占位语义与基础诊断
- **未开始/缺失**：真实圆角/倒角几何生成（含角区）、变半径、失败原因细分与回归数据集

### 需求 7.7 查询与分析（Query/Eval/Rep）

- **cycle-0078 / S3-CONSISTENCY（历史代码全量门禁）**：五类建模→Strict→截面/最近点/距离/质量→full/local/shell 表示闭环，来源/实体/Eval 绑定、缓存身份和 owned 原子失败、成功提交及失败恢复、保存点/取消/移除体运行时清理回归通过；完整 16/16、164.60 s，逐项证据见验收 §1.5，正式状态见 §5.2.1。

- **cycle-0077 / S3-MODELING（历史代码全量门禁）**：五类主路径公开拓扑/表示/全部质量/Strict、活动事务失败不污染及回滚重试通过；真实平面 Face thicken 截面/最近边界可用，曲面编辑拒绝旧质量并可回滚恢复。完整 16/16、153.89 s，逐项证据见验收 §1.4；正式状态见 §5.2.1。

- **FR-QUERY-001 cycle-0076 / S3-MASS（历史代码门禁）**：统一当前拓扑与未编辑原生解析质量资格，删除全部 bbox/来源/创建缓存恢复；独立全属性参考、旋转采样误差、保存点/回滚、多壳材料/空腔/岛、代理/metadata 拒绝及稳定阶段/无部分值由必需 Query/Eval、Ops/Heal、representation/IO 回归覆盖。完整 CTest 16/16、147.97 s，三条证据见测试与验收 §1.3；正式状态见 §5.2.1。真实平面 thicken 已在 cycle-0077 补齐，通用曲面/曲边积分仍未交付。

- **FR-QUERY-001 cycle-0075 / S3-QUERY（全量门禁通过）**：通用/专用截面与最近点/体间距离共用真实拓扑，替换旧 bbox 截面和 bbox 间隔；世界坐标实际截面面积、三角索引、边/点接触、正距离见证与 FaceId/ShellId、多壳材料闭集语义一致。非轴对齐盒、凹带孔体、空腔岛、bbox 重叠但实体分离、相切及 `1e-18` 正间隙独立参考通过；`nextafter(1,2)` 近邻截面不吸附。支持 ExactBRep box/wedge、真实物化 Sweep、用户 Generic 平面直边嵌入闭壳；解析/曲边/旧占位体失败 `AXM-CORE-E-0004 / query.*.support_gate`，非法/预算/数值/当前编辑失败有稳定阶段及无部分值。成功/空/接触/失败/回滚、缓存/Eval/事务只读、rep/provenance/拓扑与质量一致回归随必需 Query/Eval、Ops/Heal 和 representation/IO 最终全量通过。详细 API、网格发布副作用和限制见统一支持矩阵；该记录为历史代码门禁，当前唯一任务及正式验收条件见 §5.2.1。

- **FR-QUERY-001 cycle-0074 实体空间查询（已通过完整门禁）**：公开 `BodyPointLocation/BodySpatialQueryOptions/BodyBoundaryPoint/BodyPointQuery/BodySegmentInterval/BodySegmentQuery` 及 `TopologyQueryService::locate_point/clip_segment`。点定位返回 Inside/Outside/Boundary 和真实最近面内/边/顶点位置、模型长度单位距离及 FaceId/ShellId，等距按稳定句柄选择；非空实体总有最近边界；内部零壳分支约定 Outside/空边界，公共 create_body 不接受空壳列表，本批未验收该分支。闭壳包含奇偶区分空腔与材料岛。有限线段返回无量纲 `t∈[0,1]` 的有序材料区间、共面边界段和孤立相切 `[t,t]`，仅 Inside 累计模型长度单位 `material_length`，无交集成功空结果；位置容差不膨胀材料或合并可分辨薄层，长度不大于容差的线段失败。
- **空间查询前置、预算与限制**：共用闭壳质量、壳间接触和严格包含检查；支持凹面/孔/无序多壳，但须为无自交的嵌入平面直边双边流形闭壳，未新增壳自身全局自交证明。曲面/曲边（含显式 trim）拒绝，不以端点弦代替曲边，面边界须位于支撑平面；相交/重叠/建模容差接触壳失败。`max_triangle_tests` 默认 1000000、非零，成功公开实际工作量，仅计前置检查之后的新距离/求交/绕数计算；预算耗尽、事件无法可靠分辨或数值溢出失败无部分值，尚无大规模加速。查询只增加诊断与一次顶层 Topo 读审计，不写 MeshId、缓存、Eval 或事务写计数，编辑即时可见、回滚恢复。最近面/边/角、容差带、穿透/反向/端点/相切/共面/空结果、凹面带孔、多壳空腔岛、薄层、旋转缩放、精确预算重放/耗尽、曲面曲边拒绝、支撑面替换/删除回滚及不污染回归随 `axiom_query_eval_test` 通过（0.37 s）。FR-QUERY-001 保持进行中。

- **FR-QUERY-001 第 72 批曲边区间长度（专项门禁闭合）**：显式裁剪曲边由 `edge_length` 调用 `CurveService::length(curve,t0,t1)`，环和面边界长度自然复用；闭合双半圆、NURBS/Bezier 子域、递增/递减区间、越域/零区间/端点错配、区间查询、曲边 bbox、缓存/对象/事务不污染及回滚均进入 `axiom_query_eval_test`。旧无区间曲边继续 `NotImplemented`，不扩大到曲边面积或质量积分。

- **FR-QUERY-001 第 71 批一般有界 3D 曲线求交（已通过完整门禁）**：公开 `GeometryIntersectionService::intersect_curve_curve` 及位置/参数/角容差、可选有限区间和求值/细分预算；返回横交/相切/端点、残差、连续重合区间和工作量证据。分段线性子域走解析 3D 路径，Bezier/BSpline/NURBS/圆锥曲线/CompositeChain 走保守包围、确定性细分和阻尼精化；无限 Line 须显式有限窗口，空集成功。回归覆盖反向重合、常值退化、Bezier 双交/相切、样条、周期缝、复合链、预算失败和只读事务。首次全量失败后修正相切终止与 CompositeChain 子片单侧端点语义，最终 `axiom_query_eval_test` 通过且完整 CTest 16/16 通过。拓扑曲边有限 trim 调用模型已由第 74 批闭合；一般高阶异参连续重合和无限曲线自动搜索窗口仍未闭合。

- **FR-QUERY-001 第 71 批多闭壳空间层级（已通过完整门禁）**：公开 `body_shell_regions`，以严格包含深度给出 Material/Void 和直接父壳；`body_mass_properties` 对独立材料相加、空腔相减、材料岛再相加，边界面积全部保留。壳相交、重叠或容差接触返回 `InvalidTopology / AXM-QUERY-E-0006`，查询仍不发布 MeshId、不写缓存。单壳直接复用质量积分，三角面临时边界走直取快路；随第 71 批最终完整 CTest 16/16 通过。

- **FR-QUERY-001 第 70 批闭合多面体拓扑质量属性（已通过完整门禁）**：公开 `TopologyQueryService::shell_mass_properties/body_mass_properties`，从当前真实拓扑重算单位密度体积、面积、质心与关于质心的世界坐标惯性张量；支持平面直边的凹/带孔双边流形闭壳和多个独立实体壳的平行轴汇总。`axiom_query_eval_test` 覆盖平移/尺度盒体、凹带孔拉伸、双壳、单位/惯性语义、无效/开壳/曲面/零体积、删除/回滚及对象/网格/缓存/Eval/事务不污染；完整 CTest 16/16 通过（1539.20 s）。独立内壳空腔与材料岛的严格包含语义已由第 71 批闭合，cycle-0074 又补齐实体空间查询；曲面/曲边闭壳积分与相交/重叠多壳仍待后续。

- **FR-QUERY-001 第 68 批解析曲面修剪面积（已通过完整门禁）**：公开 `TopologyQueryService::face_area` 通过曲面面积密度的 Green 边界积分计算外环减内环面积，支持 Plane/Cylinder/Cone/Sphere/Torus 及嵌套 Trimmed/Offset；未包装 Plane 完全没有 PCurve 时兼容 `planar_face_area`。`axiom_query_eval_test` 覆盖五类解析面、凹外环/孔/绕向、包装面、参数越域、PCurve 缺失/断裂/自交、删除、事务回滚及缓存/对象不污染；随本批 CTest 16/16 通过。边界目前仅支持完整折线 PCurve，周期缝须由调用方使用同一展开区间；Bezier/BSpline/NURBS/Revolved/Swept 面积仍返回 `NotImplemented`。

- **FR-QUERY-001 第 67 功能包（已纳入第 68 批全量门禁）**：`CurveService::length` 新增椭圆/抛物线/双曲线与 Bezier/BSpline/NURBS 真实导数自适应积分，公开 `CurveLengthOptions` 统一绝对/相对容差及整次查询求值预算。样条按非空结点区间积分，不计满重数断点跳跃；复合链保留子曲线局部 `[0,1]` 合同。预算耗尽、精度停滞或速度不可用使用 `AXM-GEO-E-0011`，失败无部分长度。相关独立参考、尺度/姿态/节点、失败诊断和不污染回归随本批全量门禁通过。

- **FR-QUERY-001 第 63 功能包（已纳入后续全量门禁）**：`CurveService::length` 的直线有限区间、线段、圆、折线和嵌套复合链解析长度，以及 `TopologyQueryService::edge_length/loop_length/face_boundary_length` 直线边长度接口族，已随全量门禁通过。其“拓扑曲边缺少裁剪参数”限制已由第 72 批对显式区间边闭合；平面直边闭壳的拓扑质量属性已由第 70/71 批扩展，曲面/曲边质量积分仍待闭合。

- **FR-QUERY-001 第 54 切片**：公开 `TopologyQueryService::planar_face_area` 对平面直线边面片按当前拓扑顶点计算外环减内环的面积，单位为模型长度单位平方；盒体六面的解析面积由 `axiom_query_eval_test` 跨 Ops/Topo 验证。无效或已删除面不返回数值，曲面不支持与非共面拓扑返回结构化失败；替换曲面、删除面及回滚后的查询重算由同一测试验证。仅此受限子域为真实边界计算，不扩大到曲边面积或通用体质量属性。
- **当前查询与历史 fallback 边界**：Stage 3 真实多面体的 section/closest_point/closest_points/min_distance 已走真实边界，box/wedge/Generic/真实物化 Sweep 的通用质量复用当前拓扑，不能在编辑失败后回退旧缓存或 bbox。球/柱/锥/环通用质量仅对未编辑原生记录保留解析资格（metadata/mesh 派生不继承），但对应实体截面/距离不支持。cycle-0076 / S3-MASS 已删除其余历史布尔/Modified/未真实物化 Sweep 质量恢复，未知/不支持体与代理面均明确无值拒绝；近似求交、批量查询与更广体类语义仍待后续。
- **未开始/缺失**：长度/面积/体积/重心/惯性矩的工业级正确性；曲率/厚度分析；稳定的曲线-曲面/曲面-曲面求交

### 需求 7.8 验证与修复（HealCore/Validation）

- **cycle-0085 / S5-IO（代码完整门禁通过）**：固定四格式精确文本/实际 STL 积分、64 MiB 预算、标准优先拒绝、原主文件保护与模型回滚；完整 16/16、227.56 s，证据见验收 §1.12，支持限制见 API §11.1.1。

- **cycle-0084 / S5-HEAL（历史代码门禁）**：[两条逐项证据](../quality/AxiomKernel_测试与验收方案.md#111-cycle-0084--s5-heal-门禁与逐项证据)随最终 16/16、214.19 s 通过。新真实规则仅在 Standard 预验证失败时进入，限至少六唯一面、单壳真实平面直边外环，无内孔/曲面/代理面；linear 须在 min_local/max_local 内，Strict 成功才保留输出。Generic + ExactBRep 和有限 PCurve 支持真实 UV/质量/截面。默认 STEP/IGES/BREP 仍为 Axiom 元数据子集，owned 缺陷由测试物化/注入；不宣称标准全实体交换或工业通用修复。Heal 回滚保留诊断和 ID 空档，IO 回滚恢复 next_id；二维网格验证不证明闭合/自交。
- **部分完成**：拓扑一致性/来源一致性/闭合性 Strict 校验；导入后验证与可选自动修复语义；缝合结果最小物化；**自交校验路径**（含网格自交分析与 `validate_self_intersection` 等）与 `axiom_heal_test` / `axiom_ops_heal_test` 回归
- **未开始/缺失**：自交/流形性在 **BRep 全类型与大数据集**上的工业级完备规则；容差冲突检查、参数域异常检查；小边/小面/法向/近重复归并等工业化修复策略与回放机制

### 需求 7.9 数据交换（IO）

- **cycle-0073 HEAL/IO 证据与原子性（已通过完整门禁）**：HEAL 单项后验失败回收派生对象，Plane/Cylinder/Sphere trim 失败恢复 PCurve 绑定，批量失败恢复此前派生对象及 Eval 失效；IO 主格式与 auto/后验/批量失败补齐阶段、实体、有限数值证据，STEP/AXMJSON/auto 批量实际失败恢复完整新分配对象与缓存/链接/Eval/next_id。两个模块 `audit_evidence`、JSON、源报告隔离、晚失败回滚与重试回归通过。cycle-0084 起未解决的导入后验失败无 value 并原子回滚；批量导出不保证文件回滚，普通文本/目录辅助接口与标准实体格式仍受限。

- **NFR-DIA-001 第 70 批精确 B-Rep 文本导入诊断（已通过完整门禁）**：`import_axmjson/import_iges/import_brep` 在分配 BodyId 前完成普通文件、64 MiB 上限/短读、严格结构、格式/BodyKind、有限数值、包围盒和轴退化校验；失败绑定 `io.import.<format>.input/path/open/read/parse/validation`，复用 IO/VAL 稳定错误码并返回可检索 `diagnostic_id`。`axiom_io_workflow_test` 覆盖三格式成功、截断/错格式、退化、阶段/错误码/JSON、Body/Mesh/next_id 隔离和修复后原位重试；完整 CTest 16/16 通过（1539.20 s）。AXMJSON 兼容早期身份+bbox 文件，扩展几何字段必须成组完整；标准 IGES 实体仍返回 NotImplemented。

- **NFR-DIA-001 第 64 功能包（已纳入第 68 批全量门禁）**：OBJ/STL/glTF/3MF 网格导出失败绑定 `io.export.<format>.input/path/convert/mesh/open/write/sidecar` 与输入 Body，严格 QA 保留 `io.export.mesh_strict_qa`。主文件和侧车均检查最终写入/关闭状态；失败回滚本次三角化新增网格、ID、体/面缓存及统计，保留已有网格。兼容模式继续允许退化三角形，但在打开目标前拒绝空网格、非法索引、非有限坐标；glTF 另拒绝超出 float32 范围的坐标。`axiom_io_workflow_test` 新增四格式 × 严格/兼容 × 侧车开关、诊断检索/JSON、冷/热/失效缓存、退化、参数失败文件保护、Linux `/dev/full` 主文件及侧车失败、重试和重新导入回归。复用现有错误码，无公开签名变化；故障修复复现并修正 9 处诊断辅助函数调用不匹配，复用 `failed_void` 保留 `InvalidInput`、Body 和阶段；补齐回归拉伸夹具必需的轮廓标签。相关测试随第 68 批最终完整 CTest 16/16 通过。该历史包当时未保护设备/侧车失败的主文件；cycle-0085 已改为临时 payload→关闭→侧车→publish，失败项原主文件受保护，侧车与全批无跨文件事务；不扩大格式或三角化精度承诺，需求保持受限可用。

- **部分完成**：STEP/AXMJSON 导入导出主链路、导入后自动验证与诊断回传；**STL/glTF 导入导出**（网格/内嵌子集）；**IGES/BREP/OBJ/3MF** 的 Axiom 子集路径；**严格导出 + 可选网格验证侧车 JSON**；**Kernel 与 IOService 格式能力、`import_auto`/`export_auto`/`detect_format` 对齐**（`axiom_smoke_test`）；**标准 STEP/IGES 物理文件形态探测**：对含 EXPRESS 实例的 ISO-10303-21 DATA 段、或典型 IGES 80 列/DE 卡片流，在**非** Axiom 子集时返回 **`StatusCode::NotImplemented`** 与 **`AXM-IO-E-0010` / `AXM-IO-E-0011`**（`io.import.step` / `io.import.iges`），并附带 **Info 级物理层扫描摘要** **`AXM-IO-D-0016` / `AXM-IO-D-0017`**（EXPRESS 类型名 / IGES 实体类型号频度，**非**几何物化），避免静默假成功（`tests/data/io/standard_*_stub`、`axiom_io_dataset_test`）；实施路线见 **`docs/plan/AxiomKernel_STEP_IGES_标准交换实施路线.md`**
- **未开始/缺失**：与 **STEPcode/Open CASCADE** 等集成的**真实实体解析与 BRep 物化**（扫描摘要仅为里程碑 0/1 能力）；工业级 **3MF/OBJ** 全量读写；导出策略与侧车字段的产品化矩阵与大数据集回归

### 需求 7.10 三角化与显示支撑（Rep）

- **受限已实现（cycle-0086，代码完整门禁通过）**：真实平面直边凹/孔与认证矩形双线性/线段扫掠/trim三角化，弦高/全单元法向门禁、精确焊接、有限/退化/资源拒绝、full/local/shell缓存一致性；独立OBJ积分及解析几何参考、编辑/rollback和batch失败隔离见[验收§1.13](../quality/AxiomKernel_测试与验收方案.md#113-cycle-0086--s5-tessellation-门禁与逐项证据)。FR-REP-001保持受限可用。
- **剩余限制**：一般高阶、曲边/曲面孔洞/非矩形裁剪、Offset/Revolved、全类型工业误差/连通性认证；通用自适应局部更新、大模型性能、UV seam/unwrap和完整属性保真仍待闭合。仅支持平面直边凹面/孔洞及经过真实矩形边界认证的四极点双线性、一阶等权且工厂归一化后单位夹持样条、LineSegment Swept 和矩形 Trimmed。一般曲边、高阶、不等权、曲面孔洞/非矩形、Offset/Revolved 拒绝；patch每向256、圆周4096、native百万顶点上限，不可达失败。小尺度polygon绝对门槛保留，UV seam/法向拆分不认证流形；metadata/implicit仅显示代理，开放曲面无实体质量资格，round-trip与固定积分参考不证明任意BRep保真或工业全局误差。OBJ不导出vertex normals，本批核对三角cross与解析/Geo法向。

### 需求 7.11 版本/事务/增量更新（Core/EvalGraph）

- **NFR-REL-001 第 74 批（已通过完整门禁）**：公开 `TopologySavepoint`、事务内创建/重复局部回滚/LIFO 释放/活动数量查询及 `TopologySavepointMetrics`；回滚到外层会使内层点失效，移动事务转移保存点所有权，非法或跨事务句柄以 `AXM-TX-E-0003` 失败且不污染模型，取消以 `AXM-TX-E-0007` 优先恢复整事务并清空保存点。保存点累计审计已纳入 `core_runtime_invariants_hold`。最终完整 CTest 16/16 通过；当前实现为内存全拓扑快照。
- **NFR-REL-001 第 69 批（已通过完整门禁）**：公开 `TopologyCancellationSource/Token`、带令牌 `begin_transaction`、显式 `poll_cancellation`、取消请求/观察/回滚写次数、活动写者查询及累计取消指标；预取消不占写者槽，取消在拓扑写入、提交、显式回滚和析构边界观察，完整恢复创建/删除/替换及反向索引，不推进版本或成功提交审计；`AXM-TX-E-0007` 可稳定导出，取消审计纳入 `core_runtime_invariants_hold`。
- **部分完成**：版本号与单写者快照事务、协作式取消与累计审计、嵌套保存点与局部回滚审计、EvalGraph 基础失效传播/重算计数/循环依赖保护
- **未开始/缺失**：单个调用的抢占式取消、BOOL/HEAL/IO 长耗时阶段轮询、真正嵌套子事务、增量/结构共享保存点、更细粒度隔离、跨模块增量重算和缓存命中指标体系

### 需求 7.12 插件扩展（PluginSDK）

- **部分完成**：`PluginRegistry` 注册与清单查询；**清单↔实现绑定**（`PluginManifest::implementation_type_name`：带实现注册时自动填充或校验与 `type_name()` 一致；按 `type_name` 注销实现时同步移除绑定清单）；**注销路径可诊断**（`unregister_*`；门面 **`unregister_plugin_*`**）；`PluginHostPolicy` 含 **`PluginSandboxLevel`**、容量与 API 版本等门禁；**`PluginApiVersionMatchMode`**；**`auto_validate_body_after_plugin_importer`** / **`auto_validate_body_before_plugin_exporter`** / **`auto_validate_body_after_plugin_repair`** / **`auto_verify_curve_after_plugin_curve`** 与门面 **`plugin_import_file`** / **`plugin_export_file`** / **`plugin_run_repair`** / **`plugin_create_curve`**、**`invoke_registered_importer`** / **`invoke_registered_exporter`** / **`invoke_registered_repair`** / **`invoke_registered_curve`**；**`verify_after_plugin_curve`**（与 `plugin_create_curve` 开启自动校验时语义一致，供绕过门面路径显式闭环）；`find_manifest` 未命中带 **`kPluginLoadFailure`**；能力发现 JSON 含 **`manifests`** 摘要；`validate_after_plugin_mutation`、`register_plugin_*` 诊断；`services_available` 含 **`plugin.import`**、**`plugin.export`**、**`plugin.repair`**、**`plugin.curve`**、**`plugin.verify_curve`**；示例与 `axiom_plugin_sdk_test` / smoke 回归
- **未开始/缺失**：**进程外/OS 级隔离**、动态库加载与供应链安全（签名/沙箱）；**完整 SemVer 与多 ABI 并存**（`SameMajor`/`SameMinor` 已支持剥离 `-` 预发布与 `+` 构建元数据后再做核心版本比较；`Exact` 仍为整串相等；多 ABI 并存与完整 SemVer 语义仍不足）；**已闭合（宿主绑定）**：`Kernel` 构造时对 `PluginRegistry::bind_host_kernel_for_plugin_invocation` 绑定 `weak_ptr<KernelState>` 后，**`invoke_registered_importer/exporter/repair/curve`** 在对应策略开关开启时会自动套用与 **`plugin_import_file` / `plugin_export_file` / `plugin_run_repair` / `plugin_create_curve`** 一致的宿主校验语义（未绑定宿主时行为与历史一致：仅调用插件）；**Body** 侧仍可显式 **`validate_after_plugin_mutation`**，**曲线**侧 **`verify_after_plugin_curve`** 与 `plugin_curve_host_consistency_check` 共用实现

### 需求 7.13 诊断与日志（Diagnostics）

- **FR-DIAG-001 第 69 批（已通过完整门禁）**：公开 `NumericEvidence` / `Issue::numeric_evidence` 与 `DiagnosticEvidencePolicy/Audit/Finding`，新增 `audit_evidence` 和 `export_evidence_audit_json`；单条/批量/全量 TXT/JSON 保留数值证据，非有限 JSON 值安全写为 `null`。审计按问题码、阶段前缀和最低严重级别检查阶段、关联实体与有限数值证据，重复报告去重，finding 限额以 `omitted_findings` 记录。BOOL 主运行的受覆盖失败分支及预处理统计导出失败已补阶段、实体和数值证据并纳入统一门禁；当时尚缺的 HEAL/IO 重量级迁移已由 cycle-0073 闭合，非主格式辅助接口仍待补齐，需求保持受限可用。

- **FR-DIAG-001 第 34 切片**：`export_report` / `export_report_json` 在序列化单条完整诊断后显式关闭并检查输出流，Linux `/dev/full` 等最终写入/关闭失败返回 `OperationFailed` / `AXM-IO-E-0005`，不再误报成功。`axiom_diagnostics_test` 覆盖 TXT/JSON 成功证据、无效 ID、空路径、目录打开失败、设备写入失败、源报告不污染及失败后重试。无公开签名或错误码变化；设备写入失败不保证恢复目标文件，需求仍为受限可用。

- **NFR-DIA-001 第 30 切片**：`import_step` 的空路径、文件不存在与非可读常规文件分别绑定 `io.import.step.input/path/open`；拒绝发生在 Body ID 分配和存储写入前。`axiom_io_workflow_test` 覆盖成功导入、空/缺失/目录输入、阶段检索、JSON 导出、模型计数不污染及失败后重试。复用既有 `AXM-IO-E-0004`，无公开签名变化；不宣称标准 STEP 实体交换，需求仍为受限可用。

- **NFR-DIA-001 第 25 切片**：`validate_self_intersection`、壳级及批量壳级变体的非法 Body/Shell、异属 Shell、空批量、退化偏置和 Strict 网格分析失败统一绑定 `heal.validate_self_intersection.*` 细分阶段，并保留目标 Body/Shell。`axiom_heal_test` 覆盖正常 Strict、非法/异属句柄、空批量、阶段检索、JSON 及模型计数不污染。复用既有错误码，无公开签名变化；网格 SAT 仍为三角化近似，需求保持受限可用。

- **FR-DIAG-001 第 29 切片**：`export_step` 的无效 Body/空路径、路径校验、打开与最终写入失败分别绑定 `io.export.step.input/path/open/write` 并关联输入 Body；显式检查写入与关闭状态，Linux `/dev/full` 不再误报成功。`axiom_io_workflow_test` 覆盖成功、空/无效输入、不存在父目录、设备写入失败、阶段检索、JSON、模型不污染及失败后重试。无公开签名或错误码变化，需求仍为受限可用。

- **FR-DIAG-001 第 24 切片**：`export_grouped_by_stage_txt/json` 显式关闭并检查输出流，设备写入/关闭失败不再误报成功；空路径在打开文件前返回 `InvalidInput`，打开或写入失败返回 `OperationFailed`，均复用 `AXM-IO-E-0005`。`axiom_diagnostics_test` 覆盖正常阶段、空阶段 `(unset)`、空路径、目录、Linux `/dev/full`、参数失败不截断既有文件、源报告不变及失败后重试。无公开签名或错误码变化；设备写入失败不保证恢复目标文件，需求仍为受限可用。

- **NFR-DIA-001 第 20 切片**：`ValidationService::validate_geometry` 的所有失败出口补齐 `heal.validate_geometry.*` 根因阶段；非法句柄和 bbox 失败显式关联目标 Body，owned B-Rep/Strict 检查保留目标 Body 与问题子实体。`axiom_heal_test` 覆盖合法成功、非法句柄、近重复顶点、面法向退化、阶段检索、JSON 导出及持久模型计数不污染。复用既有错误码且无公开签名变化；需求仍为受限可用，当时尚缺的 HEAL/IO 重量级证据门禁已由 cycle-0073 闭合；辅助接口和更广泛失败注入语料仍待扩充。

- NFR-DIA-001 第 15 切片：`export_boolean_prep_stats` 显式关闭并检查输出流，避免写入失败返回成功；参数、文件打开、写入失败分别绑定 `bool.prep.export.input/open/write` 和 `[lhs, rhs]`。参数失败复用 `AXM-BOOL-E-0001`，文件打开失败由误用的 BOOL 输入码修正为 `AXM-IO-E-0005`，写入失败复用该 IO 码。`axiom_boolean_prep_test` 覆盖重叠/相同/分离/接触零体积输入、无效句柄/空路径、目录打开失败、Linux `/dev/full`、阶段/错误码检索及 JSON、参数失败文件不污染、模型计数与 Eval 失效不污染和失败后重试。无公开签名变化；设备写入失败不保证目标文件恢复，需求保持受限可用。

- **FR-DIAG-001 第 14 切片**：补齐指定 ID 批量文本归档，复用单报告文本格式保留完整 Issue（严重级别、码、消息、阶段、实体），保留输入顺序、重复 ID 和特殊字节；打开文件前拒绝任意位置的无效 ID，参数失败不创建/截断目标且源报告不变；显式关闭并检查写入错误，复用现有 CORE/IO 错误码，无公开签名变化。`axiom_diagnostics_test` 覆盖成功、空报告/空阶段/重复 ID、非法参数、路径失败、Linux `/dev/full` 写入失败及拒绝后重新导出。设备写入失败不保证目标文件恢复；需求仍为受限可用，BOOL 受覆盖分支及 cycle-0073 HEAL/IO 重量级包已纳入证据门禁，辅助接口与更广泛语料仍待补齐。

- **NFR-DIA-001 第 10 切片**：`BooleanService::run` 拒绝未声明的 `BooleanOp` 值，返回 `InvalidInput` / `AXM-BOOL-E-0001`、`bool.input` 与输入实体，避免越过分支后静默创建结果体。`axiom_boolean_prep_test` 覆盖两种诊断模式下的负值/越界值、相同输入体及无效句柄、阶段/错误码检索与 JSON、模型计数及 Eval 失效传播不污染，并验证拒绝后四种合法枚举仍可执行。复用已有错误码，无公开签名变化；需求仍为受限可用，不提升现有 bbox 布尔为精确能力。

- **FR-DIAG-001 第 9 切片**：修复指定 ID 批量 JSON 导出丢失问题证据、未转义摘要及静默跳过无效 ID 的缺陷；输出复用单报告完整结构，保留控制字节、输入顺序和重复 ID。打开文件前校验所有 ID，参数失败不创建/截断文件且不改源报告；打开/写入错误返回已有 IO 错误码。`axiom_diagnostics_test` 覆盖完整证据、特殊字符、空报告、重复 ID、无效 ID 各位置、空参数与不可打开路径。FR-DIAG-001 仍为受限可用，BOOL 受覆盖分支及 cycle-0073 HEAL/IO 重量级包已纳入门禁，辅助接口和更广泛语料仍待推进。

- **NFR-DIA-001 本轮增量**：布尔早期失败在 `BooleanOptions::diagnostics=false` 时仍保留单条错误 Issue 的阶段与输入实体；无效输入、分离交集和包含导致空结果在两种诊断模式下均覆盖阶段/错误码检索、JSON 导出及模型计数不污染。`axiom_boolean_prep_test` 回归；需求仍为受限可用，不代表全部失败分支已覆盖。
- **本切片性能故障修复（2026-09-22）**：默认构建（GCC 13.3.0、`CMAKE_BUILD_TYPE` 为空、无优化参数，2 个可用 CPU，cgroup 无 CPU 配额限制；采集时 load average 为 1.10/1.04/0.95）复现性能门禁失败：150 次迭代 5518 ms，阈值 4000 ms。临时 `steady_clock` 累计计时确认：一次 6887 ms 的基准中，全局 `rebuild_topology_links` 调用 600 次、累计 6549 ms；每次新建图元都清空并重建全部已有拓扑的邻接索引，造成随模型累积增长的重复工作。修复仅在 Ops 新建独立图元拓扑时追加其邻接索引，派生体、修改和回滚仍保留完整重建；未改变物化几何、公共 API、错误码、构建模式、阈值或迭代数。临时计时代码已移除。`axiom_ops_heal_test` 新增连续创建 box/wedge/cylinder 后对全部已有体的五类反向索引查询回归，`axiom_perf_baseline_test` 保留原性能门禁；相关六项测试通过，性能项 3.09 s（ctest 墙钟）。完整 `ctest --test-dir build-agent --output-on-failure` 16/16 通过，性能项 3.03 s，总耗时 28.80 s；本切片门禁已通过。公有查询回归及完整测试未发现语义回归；这些是本机对比证据，不作为跨环境性能保证。

- **FR-DIAG-001 本轮增量**：布尔预处理告警 `AXM-BOOL-W-0001/W-0002` 写入报告时保留 `bool.prep` 与输入/输出体 ID；`axiom_boolean_prep_test` 覆盖成功、仅接触退化、无壳级候选、阶段检索与 JSON，以及前置失败不增加几何/拓扑/体计数。需求仍为受限可用，不提升精确布尔能力口径。

- **部分完成**：错误码常量、诊断报告、TXT/JSON/批量/全量导出（含 `Issue.stage` / `numeric_evidence`）、聚合检索与证据覆盖审计；BOOL 受覆盖失败分支已建立阶段/实体/数值证据门禁
- **未开始/缺失**：普通文本/目录等非主格式辅助接口的证据覆盖，更广泛失败注入语料及工业格式验证；HEAL/IO 重量级迁移与模块门禁已由 cycle-0073 闭合

### 需求 7.14 混合表示与转换（RepCore）

- **受限已实现**：BRep/Mesh/Implicit基础语义与网格检查；cycle-0086 batch失败恢复全部转换状态、两向round-trip临时状态恢复、mesh_to_brep已建立关联幂等，缺嵌入网格MeshRep明确拒绝且无bbox替代。报告不证明任意BRep保真或代理物理量。
- **未开始/缺失**：高质量转换、工业级误差控制、混合建模策略与对外接口长期冻结

## 3.4 距离“工业几何引擎”的核心不足清单（架构视角）

说明：本节聚焦“要达到工业几何引擎”必须补齐的关键能力，不等价于“接口存在”。每条不足都应能映射到可验证的 DoD（实现 + 诊断 + 回归）。

### A) GeoCore：高质量几何与鲁棒查询不足

- **样条与高阶曲面工业化**：曲线 B-Spline/NURBS 已有完整有效域的最近点预算与收敛证书；Bezier/BSpline/NURBS 曲面已用正权有理控制网凸包提供局部收紧证书，并穿透 Trimmed/Offset。高质量导数/曲率、极小齐次权重退化处理、旋转/扫掠专用局部界及大模型性能证书仍不足。
- **Trim 语义未工业化**：当前 `Trimmed` 更多是参数域裁剪占位；缺少基于 loop/coedge/PCurve 的真实修剪边界、以及与 3D curve 一致性的完整规则与算法闭环。
- **统一公差与尺度策略的贯通不足**：几何求值/最近点/求交/验证/修复对 tolerance 的一致解释仍需要进一步收敛到“可预测、可回归”的策略中心。

### B) TopoCore：一致性规则集与 trim bridge 不足

- **拓扑规则集不完整**：有限 Line/LineSegment、显式裁剪折线/线性复合链以及带 trim 的圆锥曲线、Bezier、BSpline、NURBS 和混合 CompositeChain 已统一进入跨环冲突门禁；一般高阶异参连续重合证明、面环方向及重复/悬挂/非流形完整规则仍不足。
- **PCurve-3D 一致性（trim bridge）未闭环**：已有最小校验接口，但缺少可用于工业修剪的强一致性约束与修复策略。
- **事务隔离深度不足**：单写者、整事务快照回滚、协作式取消、嵌套保存点和累计审计已落地；保存点仍复制完整拓扑，细粒度写集/读集、真正嵌套子事务、长流程内部轮询与跨进程隔离仍不足。

### C) OpsCore：工业级建模算法缺失（最大缺口）

- **布尔工业范围仍受限**：cycle-0083 / S4-EXIT冻结生产代码/公开API/既有码，以回归收口固定第一代工业模型集：真实并/差/交owned重建→Strict/来源/查询→owned网格/OBJ独立V/A，偏移盒U/D/I各总计两轮；暖缓存下七阶段诊断、Eval依赖/输入摘要与writer/保存点/rollback重试均有[逐项证据](../quality/AxiomKernel_测试与验收方案.md#110-cycle-0083--s4-exit-门禁与逐项证据)及[退出支持矩阵](../api/AxiomKernel_详细模块接口清单.md#824-stage-4-退出支持矩阵cycle-0083--s4-exit)。本批完整CTest **16/16、0失败、194.58 s**；现有性能基线 **2.03 s**只测兼容run/查询，run_rebuilt工业性能未认证。 支持限嵌入平面直边ExactBRep闭壳、double/奇偶材料及可解析外向源壳；内部共面、同形/同ID和面相切有真实参考，公开只读prep仍拒绝共面。边/点Union、未解析薄层/容差带、曲面曲边/ExactCritical拒绝，Safe只修人工共面分片及一致共线节点。保留Strict至少六面及近似网格自交门禁，不证明全局嵌入或精确谓词；人工节点截面可能明确数值拒绝。兼容run仍含bbox代理实体语义，输入隔离是公开几何/拓扑摘要而非完整序列化。
- **特征建模仍未工业化**：显式多边形 extrude、多类开放/周期曲线 sweep、`sweep_scaled`、带孔整周/有向部分角 revolve、`extrude_twisted` 及 cycle-0074 分段比例/扭转律已能物化真实多面体闭壳；但解析扫掠/精确旋转/螺旋曲面、零/负扫掠比例、一般非线性解析比例/扭转律、任意环匹配 loft、曲面/曲边 thicken 和显式轮廓历史仍缺失；平面直边 Face thicken 已有真实主路径，其他路径仍可能依赖最小物化骨架。
- **圆角/倒角缺失**：真实圆角倒角（含角区、变半径）未实现，失败原因细分与回归数据集不足。

### D) Heal/Validation：工业化验证与修复不足

- **自交/流形性/容差冲突**：已有**最小自交校验与网格侧分析**路径及回归，但距离 BRep 全场景、流形性完备与容差冲突的工业规则集仍有明显差距；cycle-0084 已闭合受限平面单壳的间隙/方向/重复引用/连续零长节点修复与事务重试，通用小边小面、重合几何、自交和容差冲突仍无工业闭环。

### E) Rep/Conversion：误差控制与 round-trip 的工业化不足

- **全类型误差与保真不足**：cycle-0086已有受限双线性内部偏差/法向界、native独立V/A、原子转换与往返临时状态合同；一般高阶/曲边裁剪/派生曲面、任意BRep属性保真和工业全局误差仍未认证。
- **显示管线能力不足**：受限local重算、当前边界缓存及rollback已有门禁；通用自适应增量、纹理seam/unwrap、流形连通性和大模型性能仍不足。

### F) IO：标准互操作深度与工业交付矩阵仍不足

- **格式深度**：已实现 **STL/glTF 导入**与 **IGES/BREP/OBJ/3MF（Axiom 子集）** 及严格导出/侧车首批闭环；对**明显为标准形态的 STEP/IGES**已做**显式拒绝**（`NotImplemented` + `E-0010`/`E-0011`）并附带 **Info 扫描摘要**（`D-0016`/`D-0017`），**全实体解析与拓扑物化**仍依赖后续内核集成（见 `docs/plan/AxiomKernel_STEP_IGES_标准交换实施路线.md`）；**通用 3MF** 等仍不足。
- **能力报告**：`Kernel` 与 `IOService` 主链路格式集合**已对齐**并由 `axiom_smoke_test` 固化；新增格式须同步 `detect_format` / `import_auto` / `export_auto` / 门面与测试。

### G) Diagnostics/Eval/Plugin：工程化可观测与扩展不足

- **诊断覆盖率仍需扩展**：已具备数值证据、覆盖审计与 JSON 门禁，BOOL 受覆盖分支及 cycle-0073 HEAL/IO 重量级包已纳入；普通文本/目录辅助接口及更广泛失败注入语料尚未覆盖。
- **EvalGraph 未进入参数化求解级**：当前更像状态/依赖治理基础设施，尚未与真实建模/重建深度耦合。
- **插件工程化不足**：进程内能力发现、宿主策略与诊断闭环已有雏形（见 7.12），**隔离/安全与动态扩展**仍不足以支撑开放生态。

## 4. 当前主要风险

- 当前 `OpsCore` 仍未进入真实工业算法阶段
- 当前 `TopoCore` 尚未形成严格拓扑一致性规则集
- 当前 `TopoCore` 虽已具备基础事务、验证、邻接查询和基础反向索引，但仍缺少更完整的稠密关联结构
- 当前 `GeoCore` 已有曲线全有效域最近点证书，但高质量样条曲率、曲面参数反求和修剪边界精度合同仍不足
- 诊断码、阶段、实体与数值证据已在 BOOL 受覆盖分支及 cycle-0073 HEAL/IO 重量级包形成审计门禁；普通文本/目录辅助接口及更广泛失败注入语料仍待扩充

## 5. 下一迭代 Sprint 焦点与本阶段 backlog

说明：为降低本文档膨胀，本节内容已同步拆分到 `docs/plan/AxiomKernel_近期迭代与Backlog.md`；本文仍保留事实上下文与历史兼容入口。

阶段口径：`Stage 5：修复、导入导出、三角化（进行中）`。唯一退出任务 S5-EXIT 优先于历史 remaining、需求权重和新增变体；状态见 §5.2.1，整阶段目标与退出标准见主路线图 §4.6。长期候选不构成本批授权。

### 5.1 Sprint 焦点（当前唯一任务）

**S5-EXIT**：当前唯一退出任务为 **cycle-0087 / S5-EXIT**，`stage_task_id=S5-EXIT`、`stage_outcome=ready_for_acceptance`。[两条逐项证据](../quality/AxiomKernel_测试与验收方案.md#114-cycle-0087--s5-exit-门禁与逐项证据)与[集成支持矩阵](../api/AxiomKernel_详细模块接口清单.md#1112-stage-5-集成退出支持矩阵cycle-0087--s5-exit)覆盖固定导入→验证→修复→三角化→导出/再导入、独立几何参考、稳定拒绝与失败隔离。调度器一轮完整构建成功，CTest **16/16、0 失败、196.58 s**；必需 io_workflow/io_dataset/heal/representation_io **13.05/0.55/0.78/14.32 s**，性能 **1.92 s**，无 repair。develop 的“尚未运行/待统一门禁”已由本批代码门禁取代；文档本轮同步，最终文档门禁及调度器提交成功未记录，不能记正式已验收。FR-IO-001 / FR-REP-001 保持受限可用，Stage 5 进行中，不追认历史阶段或 S5-HEAL/S5-IO/S5-TESSELLATION 的提交/验收。本包已收口，不扩展功能；阶段任务优先于需求权重、历史 remaining 和新增变体，基础层仅限直接阻断项。

### 5.2 本阶段 backlog 表（唯一入口，随迭代刷新）

说明：**状态**列区分已纳入门禁的交付与仍在推进项，避免与 §6 已落地条目冲突。

| 优先级 | 状态 | 模块 | 交付物（摘要） | 建议 `ctest` | 依赖 |
|--------|------|------|----------------|--------------|------|
| P0 | ready_for_acceptance（cycle-0087；正式状态见当前进度§5.2.1） | io/heal/rep/geo/topo/diag | S5-EXIT：固定导入验证修复、交换往返、三角化显示/分析的集成闭环与独立参考；代码完整门禁通过，停止扩展功能 | `axiom_io_workflow_test`、`axiom_io_dataset_test`、`axiom_heal_test`、`axiom_representation_io_test`；完整 CTest 及既有性能基线 | geo/topo/rep |
| P0 | 已闭合（门禁） | core/io | 门面 IO 能力与 `IOService` 一致 | `axiom_smoke_test` | — |
| P0～P1 | 已闭合（首批） | diag/ops/io/heal | 工作流 `Issue.stage` + JSON 导出可聚合；Heal 独立门禁 | `axiom_diagnostics_test`、`axiom_boolean_workflow_test`、`axiom_heal_test`、`axiom_ops_heal_test` | core |
| P1 | 已闭合（本阶段回归） | math | 退化/尺度谓词与容差策略回归（`orient*_effective`、`max_local`/`min_local`、`resolve_*_for_scale` 非有限尺度、大坐标谓词/点等） | `axiom_math_services_test` | core |
| P0～P1 | 已闭合（第 69 批） | diag/geo/topo/core | 数值证据审计、曲线最近点证书、有限直线跨环冲突、拓扑协作式取消 | `axiom_diagnostics_test`、`axiom_geometry_test`、`axiom_topology_test`、`axiom_query_eval_test` | math/core |
| P1 | 已闭合（第 74 批子域） | geo/topo/core | 解析无界面与高阶有理曲面最近点证书；显式 trim 真曲线跨环冲突；嵌套保存点与累计审计 | `axiom_geometry_test`、`axiom_topology_test`、`axiom_kernel_runtime_invariant_test` | math/geo |
| P1～P2 | 进行中 | geo/topo | 通用无限派生面与大模型证书；完整 trim bridge、周期缝/奇点、持久命名和 Strict 规则 | `axiom_geometry_test`、`axiom_topology_test` | math |
| P1～P2 | 已闭合（cycle-0073 重量级包） | diag/heal/io | HEAL 验证/修复/回滚与 IO 主格式/后验/批量数值证据、模块审计及失败原子性 | `axiom_diagnostics_test`、`axiom_heal_test`、`axiom_io_workflow_test` | core |
| P1～P2 | 进行中 | diag/io | 非主格式文本/目录辅助接口证据与更广泛失败注入语料 | `axiom_io_workflow_test`、`axiom_diagnostics_test` | core |
| P1 | 已闭合（cycle-0073 子域） | ops | 显式轮廓法向扭转拉伸、采样闭壳与体积偏差修复 | `axiom_ops_heal_test` | geo/topo |
| P1 | 已闭合（cycle-0074 三包） | ops | 分段高度拉伸律、采样弦长比例律与联合比例/扭转律，真实拓扑质量、关键站/周期/接触预算和失败原子性 | `axiom_ops_heal_test` | geo/topo |
| P1 | 已闭合（cycle-0074 子域） | topo/query | 平面直边实体点定位、最近真实边界、材料/共面/相切线段裁剪，多壳奇偶/薄层/预算与只读合同 | `axiom_query_eval_test` | geo/topo |
| P1～P2 | 进行中 | ops/query | 一般非线性解析律/解析扫掠/精确旋转/任意截面匹配；曲面曲边闭壳积分与定位、自身嵌入证明及空间加速 | `axiom_ops_heal_test`、`axiom_query_eval_test` | geo/topo |
| P2 | 后续独立工作，非本包扩展 | ops | 精确谓词、通用曲面/曲边及全局嵌入证明；平面真实重建/内部共面/受限Safe固定参考已通过，边点Union及薄层仍受限 | `axiom_boolean_*` | geo/topo |
| P2～P3 | 后续独立范围 | eval/rep | 受限误差/原子性已过cycle-0086代码门禁；全类型误差、seam连通性及大模型增量性能 | `axiom_query_eval_test`、`axiom_representation_io_test` | ops（部分） |

### 5.2.1 Stage 5 当前退出任务

| stage_task_id | stage_outcome | 代码门禁 | stage_evidence | 正式验收条件 |
|---|---|---|---|---|
| S5-EXIT | ready_for_acceptance | cycle-0087完整构建成功，CTest16/16、0失败、196.58 s；必需io_workflow/io_dataset/heal/representation_io 13.05/0.55/0.78/14.32 s，性能1.92 s | [两条逐项stage_evidence](../quality/AxiomKernel_测试与验收方案.md#114-cycle-0087--s5-exit-门禁与逐项证据)：①固定四格式闭环/独立参考/拒绝与回滚；②完整门禁及格式/桥接/三角化矩阵、误差与限制；[支持矩阵](../api/AxiomKernel_详细模块接口清单.md#1112-stage-5-集成退出支持矩阵cycle-0087--s5-exit) | 代码门禁已通过、文档本轮同步；最终文档门禁及调度器提交成功未记录，本轮不提交，不记正式已验收 |

默认 STEP/IGES/BREP 是 Axiom 元数据子集，ExactBRep 标签但零 owned shells；STL 是实际三角网格，固定参考 V=4 而 bbox 体积=24，误差≤1e-12。坐标保留模型单位，不证明标准单位转换或全实体交换。CMake 未定义或启用标准桥接，里程碑 1～4 ON 路线本轮不适用。四格式 64 MiB 读取预算、严格 STEP/STL 损坏拒绝、八格式 classic locale/max_digits10 及同目录临时文件→关闭→请求侧车→rename 发布已落地，失败保护旧主文件并恢复本次 mesh/cache/统计/next_id；glTF float32 等固有限制保留。侧车/全批无跨文件事务，不承诺掉电持久性或并发目录修改安全，publish 失败无直接注入回归。

cycle-0086 / S5-TESSELLATION 的历史真实边界/误差/法向/原子转换参考与200.26 s门禁保留于[验收§1.13](../quality/AxiomKernel_测试与验收方案.md#113-cycle-0086--s5-tessellation-门禁与逐项证据)及API§7.3.2，不追认正式验收。

cycle-0085 / S5-IO 的历史四格式参考、227.56 s完整门禁与默认子集/单文件发布限制保留于[验收§1.12](../quality/AxiomKernel_测试与验收方案.md#112-cycle-0085--s5-io-门禁与逐项证据)及API§11.1.1，不追认正式验收。

cycle-0084 / S5-HEAL 的两条历史证据、214.19 s 完整门禁及受限平面修复合同保留于 [验收 §1.11](../quality/AxiomKernel_测试与验收方案.md#111-cycle-0084--s5-heal-门禁与逐项证据)与 API §9.2.1；不由本轮反推其正式验收或提交。当前唯一任务为 S5-EXIT。

### 5.3 最近已关闭的功能批次（与 §6 互证；以下为已落地摘要）

下列条目已在仓库代码与回归中闭合，**详细时间线见 §6**（含早期「求值图循环依赖 `AXM-EVAL-E-0001`」至「EvalGraph 治理能力」及 **§6 尾部** 最近条目）。

1. 求值图循环依赖失败路径绑定 `AXM-EVAL-E-0001`；重算 DAG 去重；脏依赖防护；`body` 绑定去重。  
2. 表示层点分类线性容差；点到体距离无效包围盒语义；`BRep/Implicit -> Mesh` 参数校验与细分映射。  
3. `query_eval` / `representation_io` 等对上述语义的回归覆盖。  
4. `Kernel::io_supported_formats` / `io_can_import_format` / `io_can_export_format` 与 `IOService::detect_format`、`import_auto`、`export_auto` 对齐，`axiom_smoke_test` 固化。  
5. `Issue::stage` 字段；诊断 JSON/文本导出；BOOL/HEAL/IO 关键路径阶段标签与 `axiom_boolean_workflow_test`、`axiom_diagnostics_test`、`axiom_heal_test`、`axiom_ops_heal_test` 回归断言。  
6. 《主开发计划与阶段路线图》`§1`：Stage 1 已达成、处于 Stage 1.5/2 过渡；本文档 §5 Sprint/backlog 结构重写。  
7. **Math P1（本阶段）**：`axiom_math_services_test` 补齐退化/尺度/非有限谓词与容差策略回归（含 `orient*_effective`、`max_local` 钳制与负向定向）；表 5.2 中 math 行闭合为「已闭合（本阶段回归）」。

### 5.4 下一未闭合批次（以阶段任务优先）

1. 当前唯一任务 S5-EXIT 两条代码证据及最终完整构建/CTest已通过；API/字典/样例/矩阵/进度/Backlog本轮同步，最终文档门禁及调度器提交成功后才记录正式验收。本轮不提交、不扩展功能。
2. 本包两条验收对应主路线图§4.6的显示/分析三角化；通用高阶、一般曲边/非矩形/孔曲面裁剪、Offset/Revolved及工业误差保证仍为独立后续范围，不因固定语料通过宣称整阶段完成。
3. HEAL/IO、Eval/Plugin、建模/查询/trim/性能候选保留长期树，不抢占 S5-EXIT，不自动启动后续阶段。

### 5.5 后续所有应开发模块总清单（长期能力树，非单迭代承诺）

为达到“项目开发完成”的最终目标，后续应持续完成以下模块能力（按优先级从高到低）：

1. `core`：统一错误码绑定、配置策略中心、版本与兼容策略  
2. `diag`：诊断聚合检索、批量导出、跨模块追踪链路  
3. `eval`：节点生命周期管理、批量失效/重算、依赖图治理  
4. `math`：鲁棒谓词与尺度自适应公差策略深化  
5. `geo`：高质量样条/反求/曲面参数域与退化处理  
6. `topo`：拓扑一致性规则集、事务可观测性、索引完整性  
7. `rep`：表示转换质量控制、网格统计与检查报告  
8. `io`：多格式导入导出主链路、路径与批处理工程化  
9. `ops`：布尔真实求交/切分/分类/重建闭环  
10. `heal`：验证与修复工业化策略（小边小面/缝合/容差冲突）  
11. `plugin`：插件清单、能力发现、注册治理  
12. `sdk`：门面稳定性、调用一致性与向后兼容  
13. `tests`：单元/集成/回归/性能基线持续补齐  
14. `ci/release`：流水线门禁、发布产物与文档同步机制

说明：该清单是“全项目完成”所需长期任务树，实际编码将按批次持续落地，每批均保证可编译、可测试、可回归。

## 6. 最近完成的关键成果

说明：本节的批次摘要版本已同步整理到 `docs/plan/AxiomKernel_变更纪要.md`；本文仍暂时保留较细粒度条目，后续新增历史应优先沉淀到独立纪要文档。

- 修正文档中 `Kernel` 门面与样例调用不一致问题
- 实际跑通工程编译
- 解决构建中的真实编译错误
- 让示例输出从占位值变成有意义的体积结果
- 把布尔工作流和 IO 工作流测试接入 `ctest`
- 把错误码常量和诊断辅助函数接入主链路代码
- 为非法输入和导出失败补充诊断码验证测试
- 增强了直线、圆、椭圆、球面、圆柱面等基础求值语义
- 增强了拓扑事务对无效对象、空集合和退化边的校验
- 为 `TopoCore` 增加了面/壳/体修改回滚、反向邻接查询和更完整的专项测试
- 新增几何与拓扑专项测试并接入 `ctest`
- 增强了表示层点分类、点到体距离和体间最短距离语义
- 为 `BRep -> Mesh -> BRep` 增加了基础包围盒保真
- 为 STEP 导出增加基础元数据写出，并在导入时恢复包围盒信息
- 为截面查询增加了基于包围盒的相交判定
- 新增表示层与 IO 集成测试并接入 `ctest`
- 强化了布尔交集失败路径与 `Split` 的占位语义
- 为偏置、抽壳、圆角、倒角增加了更多尺寸和非法输入保护
- 为修复操作增加了派生结果体与 warning 语义
- 为验证器增加了基于包围盒有效性的几何/容差检查
- 新增操作层与修复层专项测试并接入 `ctest`
- 增强了直线、圆、椭圆、球面、圆柱面的最近参数/最近点语义
- 为评估图增加了循环依赖保护、体到节点绑定、失效传播和重算计数
- 为评估图补充了可观察状态查询接口
- 新增查询与评估图专项测试并接入 `ctest`
- 为交叠/包含关系增加了布尔前置分类逻辑
- 为线-平面、线-球、平面-平面、球-球增加了更具体的近似求交语义
- 为布尔减运算补充了“结果近似为空”失败路径
- 为并运算与交运算补充了更多 warning/失败分支
- 新增布尔前置与近似求交专项测试并接入 `ctest`
- 为 `TopoCore` 增加了索引化的 `edge/coedge/loop/face/shell/body` 反向关联
- 增加了 `edge -> shell`、`face -> body` 这类跨层桥接查询
- 增强了删除面与事务回滚后的拓扑索引一致性
- 为布尔、修改、修复与拓扑建体结果增加了基础来源体/来源面追踪
- 为来源链路补充了布尔、修改、修复和拓扑工作流测试覆盖
- 为布尔、面驱动修改和边驱动特征补充了来源壳/来源面传播逻辑
- 调整了拓扑回滚测试，使其覆盖共享面被多个壳/体及派生体共同引用的场景
- 为派生结果体引入“owned topology / source topology”分离语义，并增加 `Strict` 拓扑校验约束
- 为圆柱面引入了与轴向一致的局部坐标求值/参数反求，支持非 `Z` 轴方向的基础查询
- 为圆锥面与环面补充了基础 `eval`、`closest_point`、`closest_uv` 语义
- 修正了椭圆创建时对主轴输入的忽略问题，并补充了椭圆/曲面创建非法输入测试
- 扩充了 `axiom_geometry_test` 与 `axiom_query_eval_test`，覆盖倾斜圆柱、圆锥面与环面的基础行为
- 为圆创建补齐了基于法向的局部坐标框架，使 `make_circle(normal)` 真正进入求值/反求主链路
- 改进了圆与椭圆的包围盒估算，使其与定向主轴语义保持一致
- 为定向圆和倾斜椭圆补充了求值、最近点、最近参数与包围盒测试覆盖
- 清理了 `geometry_services.cpp` 中曲线/曲面记录构造的聚合初始化告警
- 为 `CurveRecord` 增加了 `NURBS` 权重存储，并修正了创建阶段对权重数据的丢失
- 为 `Bezier` 引入了基础 de Casteljau 求值，为 `BSpline/NURBS` 补充了更合理的近似求值与定义域
- 为样条曲线补充了 `eval/domain/closest_parameter` 的基础测试覆盖
- 为 `SurfaceRecord` 增加了 `NURBS` 权重存储，并补齐了曲面创建阶段的权重一致性校验
- 为 `BSpline/NURBS` 曲面补充了基于控制网格的基础 `eval/domain/closest_uv/closest_point` 语义
- 为样条曲面补充了 `eval/domain/closest_uv/closest_point` 的基础测试覆盖
- 新增最小拓扑物化辅助，为 `BooleanResult/Modified/BlendResult` 结果体生成占位 shell/face/loop/edge 骨架
- 将部分派生结果体从“仅有来源拓扑引用”推进到“可通过 Strict/Topo 验证的最小 owned topology”
- 为布尔、修改、修复结果体补充了 `owned shell` 与严格拓扑验证测试覆盖
- 为 `sew_faces` 补充最小拓扑物化，使缝合结果也能通过 `Strict/Topo` 验证
- 为缝合结果补充 `source_shells/source_faces/owned shell` 测试覆盖
- 将最小拓扑物化从单矩形面升级为共享边的闭合六面体壳骨架，便于后续结果重建继续演进
- 为布尔、修改、修复与缝合结果补充 `faces_of_shell == 6` 的闭合壳骨架测试覆盖
- 为 `sew_faces` 改为根据输入面实际拓扑推导包围盒，不再使用固定占位尺寸
- 为 `Strict` 拓扑验证增加了来源体/壳/面引用去重、存在性和 source-face/source-shell 一致性校验
- 为最小物化闭合壳增加了边使用次数检查，开始区分开放边界与非流形边场景
- 为 `Strict` 模式补充了“删掉 owned face 导致开壳失败”和“删掉 source face 导致悬空引用失败”的回归测试
- 为结果体物化增加“优先克隆来源闭合壳”的路径，开始复用 `source_shells` 的面/环/边/点布局
- 为 `OpsCore/HealCore` 增加清空 owned topology 前的 provenance 继承，使二代派生结果体可继续沿来源壳链路物化
- 为二代偏置结果补充“来源闭合壳克隆物化”回归测试，验证其不再退回匿名 `bbox` 占位壳
- 为物化层增加 `source_faces` 反查来源壳并自动补全 `source_shells` 的逻辑，开始统一 provenance 推导入口
- 在 `replace_face/delete_face_and_heal` 中移除显式来源壳拼装，改由物化层统一推导并保持测试通过
- 为物化层增加“按 `source_faces` 在来源壳中扩展到闭合面域”的局部重建策略，减少无差别整壳克隆
- 形成 `source_faces` 局部重建 -> 来源闭合壳克隆 -> `bbox` 占位壳 的分层回退链路
- 为 `source_faces -> source_shells` 推导增加 `source_bodies` 亲和优先级，在共享面多壳场景优先选择来源体真实拥有的壳
- 增加共享面被多个壳持有时的 `replace_face` 回归测试，验证来源壳选择不会误落到非来源体壳
- 为复杂来源退化场景补充容错：来源面/来源壳部分失效时优先净化来源引用，再执行局部重建与回退链路
- 将 `source_faces` 局部重建与来源壳克隆暂收敛为单来源壳场景优先启用，复杂多壳场景先走稳定回退以保证结果可验证
- 为多壳局部重建增加候选壳评分（来源体亲和、面重叠度、壳复杂度），并在候选验收中加入环连通/闭合检查
- 修复复杂来源退化链路中壳级来源面失效导致的 `validate_shell/validate_body` 失败，恢复回退路径稳定性
- 打通 `EvalGraph <-> OpsCore/HealCore` 失效联动：拓扑变更后自动失效已绑定节点，并通过 `query_eval` 回归验证重算计数递增
- 导入链路支持修复模式策略透传（`ImportOptions.repair_mode`），并记录 `AXM-IO-D-0005` 自动修复模式诊断
- 布尔前置由仅 bbox 分类扩展到“壳级重叠候选片段统计”，输出 `AXM-BOOL-D-0002` 预处理候选诊断
- 严格拓扑验证细化诊断码：开放边界/非流形边/来源引用失效/来源集合不一致
- 新增性能基线测试 `axiom_perf_baseline_test` 并接入 `ctest`，支持 `AXM_PERF_MAX_MS` 阈值门禁
- 布尔预处理继续推进：从“候选计数”升级为“局部重叠 bbox 聚合 + 结果局部裁剪原型”
- 新增 `AXM-BOOL-W-0002/AXM-BOOL-D-0003`，区分“无候选回退”与“已应用局部裁剪”两类语义
- 性能基线测试支持 `AXM_PERF_ITERATIONS`，用于在 CI 中按资源分层配置负载
- 增强 `ValidationService::validate_topology()`，对派生结果体的最小闭合壳骨架破坏给出失败
- 为删除派生体 `owned face` 后的验证失败与事务回滚恢复补充拓扑回归测试
- 将 `ImportOptions::run_validation` 接入 `STEP` 导入主链路，补齐“导入后自动验证”语义
- 为导入诊断补充 `AXM-IO-D-0004` 代码绑定，并将验证失败回流到导入 warning/diagnostic
- 为正常导入与非法 `bbox` 元数据导入补充 IO/诊断回归测试覆盖
- 将 `ImportOptions::auto_repair` 接入 `STEP` 导入流程，在验证失败时触发 `Safe` 自动修复
- 修正 `auto_repair` 对“`is_valid=true` 但 `bbox min/max` 非法”的导入脏数据无法兜底的问题
- 为导入后自动修复成功路径补充 IO 工作流与诊断回归测试覆盖
- 为 `AXM-HEAL-D-0005` 增加代码绑定，并将“自动修复后验证通过”接入修复诊断
- 为 `auto_repair()` 补充修复前验证问题回流与修复后验证结果记录
- 为自动修复诊断补充 `HEAL-W/HEAL-D` 与导入链路回归测试覆盖
- 修正 `auto_repair()` 对零厚度/退化但形式上仍 `is_valid` 的 `bbox` 不能正确兜底的问题
- 为自动修复前后验证问题补充 `related_entities` 回填，并将原体/结果体显式挂到修复 warning/成功诊断
- 为导入修复链路补充诊断 issue 的 `related_entities` 回填，使导入报告可追踪原始导入体与修复结果体
- 为 `ops_heal/io_workflow/diagnostics` 补充 `related_entities` 结构化回归测试覆盖
- 为 STEP 导出补充了基础体 `kind/origin/axis/params` 元数据写出，并在导入时恢复
- 修复了 STEP 三元组元数据解析会被 `AXIOM_BBOX` 行污染的问题
- 历史为 `cone/torus` 补充基础质量公式；cycle-0076 已收紧为未编辑原生解析资格，metadata 导入不继承，不再用 bbox/来源恢复质量
- 表示/IO 回归在 cycle-0076 改验 metadata/mesh/implicit 派生体质量明确拒绝，并独立核对原生球/锥解析值
- 为求值图循环依赖失败路径绑定 `AXM-EVAL-E-0001`，并补充诊断回归校验
- 为求值图重算增加共享依赖去重与缺失依赖防护，避免重复计数与静默异常
- 为表示层点分类接入线性容差，并为距离/转换链路补充无效参数失败语义
- 为 `BRep/Implicit` 三角化接入参数校验与细分密度映射，并补充表示层回归测试
- 为 `GeoCore` 增加曲线/曲面批量求值、批量最近参数与批量最近点接口，并补充几何回归测试
- 为曲线求值补充高阶导数占位输出，为最近参数近似补充固定迭代上限精修
- 统一曲线参数域语义（线段占位 `[0,1]`，圆/椭圆 `[0,2pi]`）
- 为曲面参数反求与最近点补充退化防护（球心/轴线退化、圆锥斜率退化、环面半径退化）
- 为 NURBS 曲线/曲面补充权重有限正值校验与归一化存储
- 为样条记录补齐节点向量占位结构，并在创建阶段写入均匀节点向量
- 为几何创建接口补齐输入有限性校验，并引入几何求值缓存占位机制
- 为表示层补充 `classify_points_batch/distances_to_body_batch` 批量查询接口
- 为线性代数服务补充 `centroid/average` 统计接口，用于批处理场景
- 为诊断服务补充 `export_report_json`，支持结构化报告落盘
- 为求值图补充 `dependencies_of/dependents_of`，支持依赖与反向依赖查询
- 为拓扑查询补充 `summary_of_shell/summary_of_body`，支持体/壳级计数摘要
- 为容差服务补充 `scale_policy_for_body_nonlinear`，支持基于体尺度的非线性缩放
- 为 `query_eval/topology/math_services` 补充对应回归测试，并保持全量 `ctest` 通过
- 为诊断服务补充 `find_by_related_entity`，支持按相关实体反查诊断报告
- 为诊断服务 `export_report_json` 补充回归测试，覆盖 JSON 关键字段校验
- 为 `GeoCore` 补充 `curve/surface bbox_batch`，支持曲线/曲面批量包围盒查询
- 新增 `GeometryTransformService`，支持 `transform_curve/transform_surface` 几何变换
- 为 `RepresentationConversionService` 补充 `export_mesh_report_json` 网格统计报告导出能力
- 为 `geometry/representation_io` 补充对应回归测试，并保持全量 `ctest` 通过
- 为 `IOService` 补充 `import_axmjson/export_axmjson`，支持简化 `AXMJSON` 导入导出
- 为 `io_workflow` 补充 AXMJSON 回归测试，覆盖导出-导入与包围盒保真语义
- 为 `BooleanService` 增加 `export_boolean_prep_stats`，输出布尔预处理统计 JSON
- 为 `boolean_prep_test` 增加统计导出回归校验，确保关键字段存在
- 为 `ModifyService::shell_body` 增加容差邻域厚度预检查，避免临界抽壳不稳定
- 为 `ModifyService::shell_body` 增加后验校验失败回滚，保证失败路径不污染状态
- 为 `ops_heal_test` 增加抽壳失败后源体不变回归断言
- 为 `RepairService::remove_small_faces` 增加阈值自适应策略（体尺度+全局容差）
- 为 `RepairService::merge_near_coplanar_faces` 增加角度阈值自适应策略并输出告警
- 为 `ops_heal_test` 增加自适应阈值行为回归（告警与结果变化校验）
- 为 `EvalGraphService` 增加节点存在性/类型/标签、依赖管理、批量失效重算、图清理与体绑定查询能力
- 为 `query_eval_test` 补充评估图治理能力回归测试，并保持全量 `ctest` 通过
- 对齐 `Kernel::io_supported_formats` / `io_can_import_format` / `io_can_export_format` 与 `IOService::detect_format`、`import_auto`、`export_auto`，并由 `axiom_smoke_test` 固化
- 扩展 IO：STL/glTF 导入；IGES/BREP/OBJ/3MF（Axiom 子集）；网格导出严格门控与 `mesh_report` 侧车诊断合并；`io_workflow` 补充非法路径/未知扩展名诊断断言
- 为 `Issue` 增加 `stage` 字段；诊断 JSON/文本导出携带阶段；布尔/导入验证/修复管线与 `AXM-HEAL-D-0006` 等绑定稳定 `stage` 标签；`axiom_boolean_workflow_test`、`axiom_diagnostics_test`、`axiom_heal_test`、`axiom_ops_heal_test` 增加回归断言
- 更新《主开发计划与阶段路线图》当前阶段表述为 Stage 1 已达成、处于 Stage 1.5/2 过渡；重写本文档 §5 Sprint/backlog 结构
- **工程结构（2026-04）**：将各模块实现从 `src/<module>/` 收敛到 `src/axiom/<module>/`，与 `include/axiom/<module>/` 对齐；同步更新 `cmake/AxiomKernelLibraries.cmake`、`AGENTS.md`、相关技能文档与进度文档中的实现路径引用；全量 `ctest` 通过
- **IO 子集与质量资产（2026-04）**：STEP/IGES 元数据行 `AXIOM_STEP_*` / `AXIOM_IGES_ENTITY` 与 `FILE_SCHEMA` 注释；OBJ `vn`/`vt`/组标签跳过与 3MF 路径/XML/1-based 索引容错；导出目录可写探测 `AXM-IO-E-0009`；批量失败 `AXM-IO-D-0009/D-0010/D-0011/D-0012/D-0013/D-0014/D-0015`（`detect_formats_with_paths` / `count_by_format` / `paths_of_format`、批量读取/预览、`compare_file_text_many_equal`、批量路径写操作、路径变换与 `validate_import_paths`/`validate_export_paths`）；`validate_import_path` 绑定 `AXM-IO-E-0001`；`validate_export_paths` 批量阶段 `io.batch_validate_export`；`export_auto_to_directory` 扩展名门禁 `AXM-IO-E-0002`；标准 STEP/IGES 物理层扫描摘要 `AXM-IO-D-0016`/`D-0017` 与 `docs/plan/AxiomKernel_STEP_IGES_标准交换实施路线.md`；`io_service.cpp` 对 `io_service_part*.inc` 设 **CMake `OBJECT_DEPENDS`** 防漏编译；新增 `docs/quality/AxiomKernel_IO_导出策略矩阵.md`、`tests/data/io`、`axiom_io_dataset_test`
- **结构治理与测试拆分（2026）**：移除未参与构建的 `src/math/math_internal_utils.cpp`，与 `src/axiom/math/**` 真源统一；新增 `tests/heal/heal_test.cpp` 与 `axiom_heal_test`，将纯 `Validation`/`Repair`/容差/自交等回归从 `axiom_ops_heal_test` 中拆分，`axiom_ops_heal_test` 保留 **ops + heal** 交叉工作流（布尔质量属性、修改、来源传播等）
- **Core 运行时不变量（随仓库演进）**：`axiom_kernel_runtime_invariant_test` 与门面 `runtime_store_counts`、`reset_runtime_stores`、`topology_commit_audit`、`eval_graph_metrics`、三角化缓存一致性等能力报告字段对齐（细节见上文 **core** 小节）
- **diag / eval / heal / plugin（公共层切片）**：`DiagnosticService::find_by_issue_code_prefix`、`top_issue_stages`（CI 聚合）；`EvalGraphTelemetry` 增补 `invalidate_node_redundant_calls` / `recompute_root_already_valid_calls` 并进入 `export_eval_graph_metrics_json` 与能力报告；`validate_tolerance` 失败路径统一 `Issue.stage=heal.validation.tolerance`；`PluginRegistry::registered_implementation_type_names_sorted`；对应 `axiom_diagnostics_test` / `axiom_query_eval_test` / `axiom_heal_test` / `axiom_plugin_sdk_test` / `axiom_kernel_runtime_invariant_test` 回归
- **Math P1 回归（退化/尺度/容差）**：`axiom_math_services_test` 增补负向 `orient2d`/`orient3d`、`max_local` 对 `effective_linear` 与 `point_equal_effective`/`orient2d_effective` 的钳制断言；与既有大尺度/近退化/`orient*_effective`/`resolve_*_for_scale` 非有限尺度等用例共同闭合表 5.2 本阶段 math 行

## 7. 结论

当前项目已经从“纯文档阶段”进入“真正可持续编码阶段”。**门禁层面**已形成：`16` 条 `ctest` 覆盖 smoke、核心不变量、各模块专项与工作流；`heal` / `io` / `sdk` 均具备**不止一条**回归入口，便于按风险缩小 `ctest -R` 范围。

下一步仍不应盲目堆高级功能，而应继续稳住：

- **公共层**（配置、store、遥测、不变量与失败不污染）
- **诊断体系**（阶段化与失败路径覆盖率）
- **几何/拓扑基础**（规则集与 trim 语义）
- **测试闭环**（数据集、工作流与专项测试同构于模块目录）

在此基线上，**工业化优先级**仍以 **`OpsCore` 布尔与真实特征拓扑** 为最大缺口；`Heal` / `IO` / `Rep` 需与产品路线同步选择「标准格式深度」或「误差预算与 round-trip」的下一里程碑。详细可执行 backlog 见 [近期迭代与 Backlog](AxiomKernel_近期迭代与Backlog.md) 与 [变更纪要](AxiomKernel_变更纪要.md)。
