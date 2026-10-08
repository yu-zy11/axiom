# AxiomKernel 近期迭代与 Backlog

本文档承接《当前开发进度》中的“近期执行项”，把**当前迭代焦点、本阶段 backlog、下一未闭合批次、长期能力树**集中到一个更适合持续刷新的入口。事实状态仍以 `docs/plan/AxiomKernel_当前开发进度.md` 为准；总路线见 `docs/plan/AxiomKernel_主开发计划与阶段路线图.md`。

## 1. 当前阶段口径

当前阶段保持为：

`Stage 1 已达成；Stage 2 可测基线已达成；当前主线为 Stage 6：高级特征与直接编辑（进行中）`

当前唯一退出任务为 **cycle-0091 / S6-EXIT**，`stage_task_id=S6-EXIT`、`stage_outcome=ready_for_acceptance`。[两条逐项证据](../quality/AxiomKernel_测试与验收方案.md#118-cycle-0091--s6-exit-门禁与逐项证据)与[固定机械夹具支持矩阵](../api/AxiomKernel_详细模块接口清单.md#833-stage-6-固定机械夹具退出支持矩阵cycle-0091--s6-exit)覆盖建模→直接编辑/偏置→圆角、倒角及两种抽壳独立分支→Strict→查询/表示闭环、独立解析/OBJ参考及失败/全链回滚。调度器独立完整构建成功，CTest **16/16、0失败、201.93 s**；四必需 ops_heal/query_eval/representation_io/runtime **158.06/1.95/14.88/0.13 s**，性能 **1.83 s**。develop的未运行表述已由实际完整代码门禁取代，无repair报告；FR-MOD-001与FR-BLEND-001保持受限可用，Stage 6进行中。文档本轮同步，最终文档门禁及调度器提交成功未记录，不记正式已验收或Stage 6已退出。本包停止功能扩展；阶段任务优先于需求权重、历史remaining和新增变体，基础层修复仅限本任务直接阻断项。

本批支持边界：仅保留完整祖先的当前完整、独占、owned轴对齐六Plane ExactBRep盒链：8×5×3→单面move_face→9×5×3→严格平行Plane replace_face→9×5×4→offset(+0.5)→10×6×5；从同一最终毛坯独立分支单边圆角、单边倒角、封闭内腔抽壳和+Z单面开口抽壳，r=d=t=0.5。全部成功阶段通过Strict、立即来源与当前owned表示检查；平面分支支持独立质量/点查询，圆角只认证解析Circle/Cylinder和采样OBJ，通用质量/实体空间查询及无PCurve面面积仍不支持。高级特征终点继续盒域操作拒绝，一般曲面、非轴对齐/共享/开放边界、批量/删除补面不支持；删除祖先后再Blend未认证。Modify成功通知输入绑定Eval及下游失效，Blend保持源Eval；失败无输出、不消耗live模型ID、不改源拓扑/来源/索引、Eval与暖缓存。全writer回滚清理7个派生体及拓扑、几何、表示/缓存和体Eval绑定，使派生消费者失效，保留源暖MeshId、原拓扑/来源及源Eval；成功ID可空档、诊断可增加。独立参考为解析公式、公开OBJ积分及快照，无外部工业内核认证；圆角采样误差由默认5°角预算独立限定。

## 2. 当前迭代焦点

### 2.1 当前任务 `ops + geo + topo + rep + heal + diag + eval`

- **cycle-0091 / S6-EXIT**：当前唯一退出任务，固定机械夹具已接入Ops；完整代码门禁16/16、201.93 s，四必需ops_heal/query_eval/representation_io/runtime 158.06/1.95/14.88/0.13 s，性能1.83 s。两条证据见验收§1.18，正式状态见当前进度§5.2.1。
- **cycle-0090 / S6-DIRECT-EDIT（历史）**：单面移动/平行Plane替换的16/16、203.36 s及两条证据保留于验收§1.17与API§8.3.2；不追认正式验收或提交。
- **支持与剩余限制**：仅保留完整祖先的当前完整、独占、owned轴对齐六Plane ExactBRep盒链：8×5×3→单面move_face→9×5×3→严格平行Plane replace_face→9×5×4→offset(+0.5)→10×6×5；从同一最终毛坯独立分支单边圆角、单边倒角、封闭内腔抽壳和+Z单面开口抽壳，r=d=t=0.5。全部成功阶段通过Strict、立即来源与当前owned表示检查；平面分支支持独立质量/点查询，圆角只认证解析Circle/Cylinder和采样OBJ，通用质量/实体空间查询及无PCurve面面积仍不支持。高级特征终点继续盒域操作拒绝，一般曲面、非轴对齐/共享/开放边界、批量/删除补面不支持；删除祖先后再Blend未认证。Modify成功通知输入绑定Eval及下游失效，Blend保持源Eval；失败无输出、不消耗live模型ID、不改源拓扑/来源/索引、Eval与暖缓存。全writer回滚清理7个派生体及拓扑、几何、表示/缓存和体Eval绑定，使派生消费者失效，保留源暖MeshId、原拓扑/来源及源Eval；成功ID可空档、诊断可增加。独立参考为解析公式、公开OBJ积分及快照，无外部工业内核认证；圆角采样误差由默认5°角预算独立限定。
- **收口条件**：文档本轮同步，最终文档检查及调度器提交成功后才记已验收；停止扩大功能，不以新增行数继续开发。历史候选不构成本批授权。
- **cycle-0089 / S6-OFFSET-SHELL（历史）**：真实偏置/抽壳受限支持、200.60 s完整16/16及两条证据保留于验收§1.16与API§8.3.1；不追认正式验收或提交。
- **cycle-0088 / S6-BLEND（历史）**：真实圆角/倒角受限支持、200.33 s完整16/16与两条证据保留于验收§1.15及API§8.4.1；FR-BLEND-001保持受限可用，不追认正式验收。
- **cycle-0087 / S5-EXIT（历史）**：完整代码门禁16/16、196.58 s和集成交换矩阵保留于验收§1.14及API§11.1.2；本轮不追认历史任务提交/正式验收。

### 2.1.1 `diag + ops` 历史与长期候选

- **cycle-0083 / S4-EXIT（历史代码门禁，ready_for_acceptance）**：cycle-0083 / S4-EXIT冻结生产代码/公开API/既有码，以回归收口固定第一代工业模型集：真实并/差/交owned重建→Strict/来源/查询→owned网格/OBJ独立V/A，偏移盒U/D/I各总计两轮；暖缓存下七阶段诊断、Eval依赖/输入摘要与writer/保存点/rollback重试均有[逐项证据](../quality/AxiomKernel_测试与验收方案.md#110-cycle-0083--s4-exit-门禁与逐项证据)及[退出支持矩阵](../api/AxiomKernel_详细模块接口清单.md#824-stage-4-退出支持矩阵cycle-0083--s4-exit)。本批完整CTest **16/16、0失败、194.58 s**；现有性能基线 **2.03 s**只测兼容run/查询，run_rebuilt工业性能未认证。 支持限嵌入平面直边ExactBRep闭壳、double/奇偶材料及可解析外向源壳；内部共面、同形/同ID和面相切有真实参考，公开只读prep仍拒绝共面。边/点Union、未解析薄层/容差带、曲面曲边/ExactCritical拒绝，Safe只修人工共面分片及一致共线节点。保留Strict至少六面及近似网格自交门禁，不证明全局嵌入或精确谓词；人工节点截面可能明确数值拒绝。兼容run仍含bbox代理实体语义，输入隔离是公开几何/拓扑摘要而非完整序列化。
- **cycle-0083历史门禁事实**：[日志](../../.axiom-agent/logs/cycle-0083-gates.log)一轮完整配置/并发4构建成功，CTest16/16、0失败、194.58s；五项必需workflow/prep/query_eval/representation_io/runtime 4.25/5.63/2.01/10.17/0.04s，性能2.03s，ops_heal/heal153.97/0.67s。无repair、构建总时长/最终文档门禁/提交记录；增量未打印告警不证明全仓清零。现有性能基线仅兼容run/查询，run_rebuilt工业性能未认证。本轮仅同步docs。
- **cycle-0082门禁事实（历史）**：首轮11/16、5失败、28.43s；repair后调度器完整构建成功，最终16/16、0失败、184.89s，四项必需3.27/147.51/0.64/1.80s、prep5.00s、topology0.17s、rep_io9.62s、性能1.91s。首次18条SDK/helpers告警，最终无新告警不证明全仓清零；文档门禁/提交未记录。本轮不重建测试或提交。

- **cycle-0081 / S4-SPLIT-CLASSIFY（历史只读准备，ready_for_acceptance）**：新增只读 prepare_split_classification/classify_points；实际外/孔环切分、有限交段两源面完整覆盖、来源边v0→v1参数、唯一反向整边邻接与真实实体分类。旋转盒、凹U/显式孔、包含/间隙/非共面相切及近边界拒绝都有 [逐项证据](../quality/AxiomKernel_测试与验收方案.md#18-cycle-0081--s4-split-classify-门禁与逐项证据)，范围见 [API矩阵](../api/AxiomKernel_详细模块接口清单.md#822-stage-4-第一代切分与实体分类支持矩阵cycle-0081--s4-split-classify)；共面二维区域、曲面/曲边、ExactCritical、精确谓词、全局嵌入及实体重建不认证。
- **cycle-0081 门禁事实（历史）**：[日志](../../.axiom-agent/logs/cycle-0081-gates.log) 首轮配置成功而完整构建失败（Math声明缺失），无该轮CTest；repair后第二轮配置/并发4完整构建成功，最终 **16/16、0失败、173.96 s**；prep/workflow/topology **0.45/0.61/0.18 s**，性能 **2.26 s**。repair定向3/3、0.83s单列，不替代最终全量；日志未记录构建总耗时/文档检查/提交，增量无告警不证明全仓清零。本轮只读日志并同步文档，没有重建测试或提交。
- **cycle-0080 / S4-INTERSECTION（历史代码门禁，ready_for_acceptance）**：冻结真实嵌入平面直边闭壳范围、容差/预算与 double 解析边界；固定旋转盒、斜楔 bbox 伪交、凹 U、两种孔洞、分离/包含/相切/共面参考。真实候选/交段及源 face/edge/fraction 可供后续切分，prepare 不物化模型或写事务。兼容 run 改真实面边界裁剪，读取失败在 bool.intersect.trim 结构化传播；不认证后续重建实体物理正确性。[三条证据](../quality/AxiomKernel_测试与验收方案.md#17-cycle-0080--s4-intersection-门禁与逐项证据) 与 [矩阵/参考集](../api/AxiomKernel_详细模块接口清单.md#821-stage-4-第一代求交准备支持矩阵cycle-0080--s4-intersection) 已同步。
- **cycle-0080 门禁事实（历史）**：[日志](../../.axiom-agent/logs/cycle-0080-gates.log) 两轮配置/并发 4 完整构建成功；首轮 15/16、185.75 s，prep 失败；repair 修正真实短边夹具并补源边插值/隔离，修复 run 读取失败静默吞错。最终全量 **16/16、0 失败、172.43 s**，prep 0.17 s、workflow 0.12 s、性能 1.72 s。首轮实发 SDK/helpers 初始化告警 18 条，最终未重编译这些单元不能声称清零；构建总耗时未记录。代码门禁已闭合；文档检查见验收 §1.7，提交尚未记录，本轮未重建测试或提交。

- **cycle-0079 / S3-EXIT（Stage 3 历史记录，ready_for_acceptance）**：统一 box/wedge、五类真实建模、Generic 闭壳/空腔/岛、四类原生解析质量资格、占位 Sweep/Boolean、mesh 派生及无效句柄共 17 行；截面/最近点/双侧距离/质量的独立参考、稳定拒绝及只读不污染已锁定。smoke 的缩放拉伸 V=10.5、截面=3.375、最近边界/远盒距离=1、owned 网格与 Strict 闭环通过。三条证据和五项退出映射见 [验收 §1.6](../quality/AxiomKernel_测试与验收方案.md#16-cycle-0079--s3-exit-门禁与逐项证据)，范围见 [API §6.1.4](../api/AxiomKernel_详细模块接口清单.md#614-stage-3-统一退出支持矩阵cycle-0079--s3-exit)。
- **cycle-0079 门禁事实（历史）**：[日志](../../.axiom-agent/logs/cycle-0079-gates.log) 独立配置/并发 4 完整构建成功，串行全量 **16/16、163.78 s**；必需 Ops 137.10 s、Query 1.41 s、Rep 9.01 s、Runtime/smoke 各 0.02 s，保留 Topology 0.17 s、Boolean workflow/prep 0.03/0.04 s、性能 1.71 s。Strict warnings 配置 ON，Topo 2 条和 Rep 1 条所涉历史告警本批未再出现，实发残余 SDK 1 条；helpers 历史 17 条未重编译不宣称清零；构建时长未记录。本地文档检查 **35 个 Markdown、0 错误、0 警告**，新增/更新链接锚点、矩阵映射及 diff 检查通过，细目见验收 §1.6；仍须调度器核验及提交成功。无 repair，本轮不重建测试或提交。
- **cycle-0078 / S3-CONSISTENCY（历史代码全量门禁）**：五类建模→Strict→截面/最近点/距离/质量→full/local/shell 表示，独立质心/距离/见证和 OBJ 三角积分通过；来源及关联实体有效、重复几何/相同 bbox 不混用缓存。owned 面先完整三角化/组装再发布，失败无新 MeshId/部分缓存/bbox 或创建参数回退；primitive 编辑资格随修改撤销/恢复，Eval dirty 传播、成功提交与后续失败回滚、保存点/取消/删除清理一致。见 [一致性合同](../api/AxiomKernel_详细模块接口清单.md#731-stage-3-表示来源与-eval-一致性合同cycle-0078--s3-consistency) 和 [三条验收证据](../quality/AxiomKernel_测试与验收方案.md#15-cycle-0078--s3-consistency-门禁与逐项证据)。
- **cycle-0078 门禁事实（历史）**：[日志](../../.axiom-agent/logs/cycle-0078-gates.log) 一轮测试/示例开启配置、并发 4 完整构建成功，完整 CTest **16/16、0 失败、164.60 s**；四个必需回归 Ops **137.40 s**、Query **0.95 s**、Rep **9.44 s**、Runtime **0.02 s**，保留 Topology **0.23 s**、性能 **1.74 s**。无 repair，仍有初始化/未使用参数告警，未记录独立 Strict warnings、文档门禁或提交成功；本轮仅同步文档，没有重建测试/提交。旧 MeshId 为快照，历史边界缓存可保留；Eval recompute 只管理图，metadata 显示代理不授予物理查询资格，不扩展曲面/曲边/全局嵌入或后续阶段。
- **cycle-0077 / S3-MODELING（历史代码门禁）**：核验 extrude/revolve/sweep/loft 并补齐真实平面直边 Face thicken；当前定向外/内环与裁剪边沿支撑法向生成独立 owned 闭壳，支持凹形/分离非嵌套孔/有限正厚度，拒绝曲面/曲边/代理面，无 bbox fallback。五类公开面/边/壳查询、ExactBRep/owned_topo_welded、独立全质量/Strict、阶段诊断、活动事务不污染与回滚重试已执行通过。平面棱柱及共面侧壁 loft 夹具在浮点容差内精确，revolve 仍为采样多面体。见 [五类主路径矩阵](../api/AxiomKernel_详细模块接口清单.md#811-stage-3-五类建模主路径cycle-0077--s3-modeling) 与 [三条证据](../quality/AxiomKernel_测试与验收方案.md#14-cycle-0077--s3-modeling-门禁与逐项证据)。
- **cycle-0077 门禁事实（历史）**：[日志](../../.axiom-agent/logs/cycle-0077-gates.log) 一轮测试/示例开启配置、并发 4 完整构建成功，CTest **16/16、0 失败、153.89 s**；必需 Ops **126.52 s**、Topology **0.13 s**，关联 Query **0.95 s**、Rep **9.79 s**、性能 **1.60 s**。无 repair，仍有初始化/未使用参数告警，未记录独立 Strict warnings、文档检查或提交成功；回归的 Strict 拓扑验证通过。历史报告为 ready_for_acceptance，不由本批反推正式验收；当前唯一任务以§1的S6-EXIT为准。


- **cycle-0076 / S3-MASS（历史代码门禁）**：真实多面体从当前拓扑积分全部质量，未编辑原生球/柱/锥/环保留解析资格，成功拓扑/PCurve 编辑撤销，保存点/回滚恢复。删除 bbox、Boolean/Modified 来源与 Sweep 创建缓存 fallback；代理面重组/克隆/imprint/删除 owner 不改变拒绝，metadata/mesh/implicit 派生不继承资格。独立 primitive/建模全量质量参考、矩形子午面旋转采样误差、材料/空腔/岛与偏心积惯量、热缓存编辑拒绝/回滚/只读及稳定阶段/无部分值均随完整门禁通过，见 [质量矩阵](../api/AxiomKernel_详细模块接口清单.md#613-stage-3-质量属性支持矩阵cycle-0076--s3-mass) 与 [三条证据](../quality/AxiomKernel_测试与验收方案.md#13-cycle-0076--s3-mass-门禁与逐项证据)。该批当时的 thicken 质量拒绝已在 cycle-0077 更新为真实平面 Face 主路径；不因代码门禁通过自动退出 Stage 3。
- **cycle-0076 门禁事实**：[日志](../../.axiom-agent/logs/cycle-0076-gates.log) 一轮完整构建（并发 4，测试/示例开启）成功，CTest **16/16、0 失败、147.97 s**；必需 Query **0.95 s**、Ops **120.75 s**、Rep **9.68 s**；Boolean **0.03 s**、性能 **1.58 s**，阈值/迭代数未变。无 repair，仍有初始化/未使用参数告警，未记录独立严格告警、文档检查/提交成功；本次只同步文档，未重建测试或提交。

- **cycle-0075 / S3-QUERY（历史代码门禁）**：通用与专用实际截面、最近边界和体间材料距离共用真实拓扑；凹/孔/多壳奇偶、空集/共面/线点接触、bbox 重叠实体分离、相切、`1e-18` 正间隙及近邻平面不吸附参考已回归。支持 ExactBRep box/wedge、真实 Sweep、用户 Generic 平面直边嵌入闭壳；采样建模计算物化多面体，解析/曲边/旧 thicken 占位明确拒绝。真实多面体质量消费当前拓扑，编辑失败不得恢复旧缓存；topology/rep/provenance/缓存/Eval/事务与回滚一致性通过。repair 修正负向旋转侧壁反向与非等边楔体支撑，公共空列表建体夹具改验创建拒绝，零壳查询分支未验收。支持矩阵见 [API §6.1.2](../api/AxiomKernel_详细模块接口清单.md#612-stage-3-截面最近点与距离支持矩阵cycle-0075--s3-query)，四条证据见 [验收 §1.2](../quality/AxiomKernel_测试与验收方案.md#12-cycle-0075--s3-query-门禁与逐项证据)。
- **cycle-0075 门禁事实**：[日志](../../.axiom-agent/logs/cycle-0075-gates.log) 首轮 14/16（141.18 s），repair 后独立完整构建（并发 4）成功，最终全量 **16/16、0 失败、148.44 s**，必需 Query/Eval **0.90 s**、Ops/Heal **121.81 s**、representation/IO **9.87 s**；本轮不重建测试、不提交。日志仍有初始化告警，未记录文档检查/独立严格告警门禁；历史日志不用于推断文档检查/提交成功；当前唯一任务按当前进度记录。

- **cycle-0074 FR-OPS-001（三包已通过完整门禁）**：新增 `ExtrusionLawStation/extrude_with_law` 归一化高度分段比例/扭转、`SweepScaleStation/sweep_with_scale_law` 归一化采样弦长分段比例、`SweepLawStation/sweep_with_law` 联合比例/扭转。支持凸/凹及分离非嵌套孔、扩张/收缩/停顿、扭转反向、空间旋转/方向反转；保留原站和关键站，数值重合原站/关键站共享位置/标架，不可分辨关键站失败。每步扭角 ≤7.5°、比例变化 ≤较小端 25%、联合采样 ≤4096 区间、累计绝对扭角 ≤一周。曲线沿传输标架局部前向切向扭转，线段/严格同向推进折线绕定向初始法向；周期末比例 1、联合律末角 0 或 ±2π（1e-10 rad 容差），首末焊接无端盖、不推断对称顶点置换。实际截面/盖片/折叠/壁片交叠及容差接触/质量在分配前检查，最大比例用于曲率/间距门禁、曲线宽相候选上限 2000000。独立质量积分、真实拓扑/Strict/网格、中间站与周期孔、旧入口兼容、数值失败/阶段证据/预算、活动事务失败原子性及编辑回滚重试随 Ops 测试通过。`revolve_between/sweep_scaled/extrude_twisted` 旧合同保留；联合变比例扭转已由新入口覆盖，仍无至平面组合、零/负比例、尖顶、解析扫掠/螺旋面或一般非线性解析律。
- **cycle-0074 门禁事实（历史）**：调度器独立配置测试/示例并完整构建（并发 4），完整 CTest **16/16 通过、0 失败、134.05 s**（Ops/Heal **106.39 s**、Query/Eval **0.37 s**、IO **13.95 s**、representation/IO **9.97 s**、性能基线 **1.75 s**），见 [cycle-0074 日志](../../.axiom-agent/logs/cycle-0074-gates.log)。仍有 BoundaryEdge/Issue 聚合初始化和未使用参数告警；本批无 repair 报告，日志未记录文档检查，本轮未重建测试。四个功能包及第 60/61/62/65 包现有回归均已通过，无本批待验收包；自动开发台账保持不变。FR-OPS-001 / FR-QUERY-001 保持进行中。

- **cycle-0073 FR-DIAG-001 / NFR-DIA-001 / FR-OPS-001（已通过完整门禁）**：HEAL 验证/修复/后验/trim/批量失败补齐 `heal.*`、实体及有限数值证据，单项/自动修复后验失败回收派生对象，trim 失败恢复原 PCurve 绑定，批量后项失败恢复此前全部派生对象及 Eval 失效。IO 主格式（STEP/AXMJSON/IGES/BREP/OBJ/STL/glTF/3MF）及 auto/后验/批处理补齐 `io.*` 与有限数值证据；STEP/AXMJSON 接入共享后验管线，批量 STEP/AXMJSON/auto 实际失败恢复新分配对象、链接、缓存、Eval 失效和 `next_id`。两个模块 `audit_evidence` 与 JSON/源报告隔离/回滚重试通过。新增 `extrude_twisted`，支持显式平面凸/凹与分离非嵌套孔、任意朝向、方向反转/缩放、正负部分角/整周及零角。首次完整 CTest 15/16（106.79 s），扭转质量检查失败；repair 交替侧壁站间对角线消除系统性体积偏差，未增加站点或放宽断言/阈值。调度器完整重建（并发 4）后最终 **16/16 通过、0 失败、126.44 s**（Ops 98.84 s、IO 13.45 s、HEAL 0.06 s、性能基线 1.68 s），见 [门禁日志](../../.axiom-agent/logs/cycle-0073-gates.log)。本批及第 60/61/62/65 包无待验收项。扭转仍为不超过 7.5° 角站的采样多面体，方向须法向、中心共面、正距离、角度最多一周；该旧入口未组合变比例/至平面；分段比例与扭转组合已由 cycle-0074 新入口提供，至平面组合仍不支持。FR-OPS-001 保持进行中，FR-DIAG-001/NFR-DIA-001 保持受限可用。

- **第 71 批 FR-OPS-001（已通过完整门禁）**：`revolve_between` 支持偏置/对称起始角、顺逆时针不超过一周的有向区间及正负整周，部分角端盖绕向与方向匹配；`sweep_scaled` 支持直线、CompositePolyline、Bezier、BSpline、NURBS 及开放 CompositeChain 上从 1 按采样弧长线性变到有限正终端比例，凹/带孔轮廓物化真实闭壳。既有 `revolve` 正角和 `sweep` 单位比例合同保持兼容。调度器 repair 后独立完整构建成功，连续两次完整 CTest 均 16/16 通过（0 失败，总耗时 1865.97 s / 1863.95 s；Ops 1838.47 s / 1823.22 s）。当前仍是每周 48 段/导轨采样的保守多面体 BRep；带孔触轴、零/负/一般非线性解析比例律、嵌套复合导轨和显式轮廓历史仍不支持。

- **第 70 批 NFR-DIA-001 / FR-OPS-001（已通过完整门禁）**：AXMJSON、Axiom IGES 元数据与 Axiom BREP JSON 子集导入补齐 `input/path/open/read/parse/validation` 阶段、64 MiB/短读和物化前几何验证，失败不污染 Body/Mesh/ID；扫掠新增首尾 G1 连续闭合样条、开放/闭合 `CompositeChain` 导轨和部分角多边形旋转真实闭壳。repair 后调度器独立完整构建成功，CTest 16/16 通过（0 失败，1539.20 s；Ops 1510.78 s、IO 14.96 s、Query/Eval 0.17 s、性能基线 1.90 s）。这些功能包已验收，不再标记为“待统一验收”。当批尚缺的带孔旋转以及有向区间/变截面扫掠已由第 71 批部分闭合；标准 IGES 实体、解析扫掠/精确旋转、嵌套复合链、显式轮廓历史及过大截面/急弯/自靠近输入限制仍在。

- **FR-DIAG-001 第 69 批（已通过完整门禁）**：新增 `Issue::numeric_evidence`、诊断证据策略/审计/finding 与 `audit_evidence` / `export_evidence_audit_json`；单条、批量和全量 TXT/JSON 保留数值证据，非有限 JSON 值写为 `null`。审计按问题码前缀、阶段前缀和最低严重级别检查阶段、实体与有限数值证据，重复 ID 去重、明细截断计入 `omitted_findings`。BOOL 主运行受覆盖失败分支和预处理统计导出已接入门禁。最终完整 CTest 16/16 通过；当时尚缺的 HEAL/IO 重量级证据与模块门禁已由 cycle-0073 闭合；普通文本/目录辅助接口及更广泛失败注入语料仍待补齐，需求保持受限可用。

- **NFR-DIA-001 第 64 功能包（已纳入第 68 批全量门禁）**：OBJ/STL/glTF/3MF 网格导出失败绑定 `io.export.<format>.input/path/convert/mesh/open/write/sidecar` 与输入 Body，严格 QA 保留 `io.export.mesh_strict_qa`。主文件和侧车均检查最终写入/关闭状态；失败回滚本次三角化新增网格、ID、体/面缓存及统计，保留已有网格。兼容模式继续允许退化三角形，但在打开目标前拒绝空网格、非法索引、非有限坐标；glTF 另拒绝超出 float32 范围的坐标。`axiom_io_workflow_test` 新增四格式 × 严格/兼容 × 侧车开关、诊断检索/JSON、冷/热/失效缓存、退化、参数失败文件保护、Linux `/dev/full` 主文件及侧车失败、重试和重新导入回归。复用现有错误码，无公开签名变化；故障修复复现并修正 9 处诊断辅助函数调用不匹配，复用 `failed_void` 保留 `InvalidInput`、Body 和阶段；补齐回归拉伸夹具必需的轮廓标签。相关测试随第 68 批最终完整 CTest 16/16 通过。该历史包当时未保护设备/侧车失败的主文件；cycle-0085 已改为临时 payload→关闭→侧车→publish，失败项原主文件受保护，侧车与全批无跨文件事务；不扩大格式或三角化精度承诺，需求保持受限可用。

- **FR-DIAG-001 第 58 切片**：严重级别检索在限额前按 `DiagnosticId` 升序排序；`report_ids_by_severity` 继承相同语义。`axiom_diagnostics_test` 覆盖限额、重复 issue、空报告/空结果、非法参数、源报告不污染及重试。需求保持受限可用；现有 BOOL 受覆盖分支及 cycle-0073 HEAL/IO 重量级包已通过模块门禁，后续补齐辅助接口与更广泛失败注入语料。

- **FR-OPS-001 第 68 批（已通过完整门禁）**：新增 `extrude_to_plane`，完成整周显式多边形 `revolve` 与旋转最小化标架曲线 `sweep` 的真实多面体 BRep 物化；周期带孔扫掠经 repair 改为外边界与各孔边界分别物化独立闭壳，开放带孔扫掠仍为单壳。回归分别覆盖 240 组至平面拉伸、32 组整周旋转、40 组曲线扫掠变体，并在第 70/71 批完整门禁中继续通过。第 70/71 批已进一步闭合闭合样条/复合导轨、带孔及有向区间旋转、正比例变截面扫掠子域；仍缺解析扫掠/精确旋转、零/负/一般非线性解析比例律、任意截面放样、逐壁恒角拔模、嵌套复合导轨及显式轮廓历史。

- **NFR-DIA-001 第 50 切片**：3MF 导入在物化前按根因绑定 `io.import.3mf.input/path/open/read/parse/validation`；目录输入结构化拒绝，非法坐标与超范围索引返回解析或验证失败，不再泄漏数值转换异常或静默截断索引。`axiom_io_workflow_test` 覆盖正常、退化、路径/ZIP/XML/数值失败、阶段检索、JSON、Body/Mesh 不污染及成功重试；`axiom_io_dataset_test` 验证往返。复用现有 IO/VAL 错误码，需求保持受限可用；当时缺少的 HEAL/IO 重量级证据已由 cycle-0073 闭合，辅助接口与更广泛语料仍待扩充。

- **FR-DIAG-001 第 49 切片**：相关实体检索在应用 `max_results` 前按 `DiagnosticId` 升序排序，重复 issue 只返回一次报告；`axiom_diagnostics_test` 覆盖成功、空报告/空结果、限额、非法参数、源报告不污染及重试。需求保持受限可用；后续继续补齐高风险流程的阶段、实体和数值证据。

- **NFR-DIA-001 第 45 切片**：glTF 导入的物化前失败绑定 `io.import.gltf.input/path/open/read/parse/validation`，目录输入在读取前结构化拒绝；`axiom_io_workflow_test` 覆盖路径/解析/网格验证失败、阶段检索、JSON、Body/Mesh 不污染及合法重试。复用现有 IO/VAL 错误码，需求保持受限可用；当时缺少的 HEAL/IO 重量级证据已由 cycle-0073 闭合，辅助接口与更广泛语料仍待扩充。

- **FR-DIAG-001 第 44 切片**：问题码前缀检索先确定匹配报告并按 `DiagnosticId` 升序排序，再应用 `max_results`；空报告/空结果、重复匹配 issue、限额、非法参数、源报告不污染与重试进入 `axiom_diagnostics_test`。需求保持受限可用；后续继续补齐高风险流程的阶段、实体和数值证据。

- **NFR-DIA-001 第 40 切片**：STL 导入的物化前输入/路径/打开/读取/解析/网格验证失败分别绑定 `io.import.stl.input/path/open/read/parse/validation`；非普通文件在读取前拒绝。`axiom_io_workflow_test` 覆盖稳定错误码、阶段检索、JSON、Body/Mesh 不污染及失败后重试。复用现有 IO/VAL 错误码，需求仍为受限可用；当时缺少的 HEAL/IO 重量级证据已由 cycle-0073 闭合，辅助接口与更广泛语料仍待扩充。

- **FR-DIAG-001 第 39 切片**：阶段精确与前缀检索先按 `DiagnosticId` 升序确定匹配集合，再应用 `max_results`，避免无序存储遍历使限额结果不稳定。`axiom_diagnostics_test` 覆盖匹配/空结果、限额、非法参数、源报告不污染及拒绝后重试。复用现有错误码；需求保持受限可用，全部高风险流程的阶段、实体和数值证据仍待补齐。

- **NFR-DIA-001 第 35 切片**：OBJ 导入的空路径、缺失文件、非普通文件/打开失败、解析失败与退化三角形分别绑定 `io.import.obj.input/path/open/parse/validation`；非普通文件在读取前拒绝，避免目录读取抛出裸异常。`axiom_io_workflow_test` 覆盖阶段检索、JSON、Body/Mesh 不污染及失败后成功重试。复用现有 IO/VAL 错误码，不扩大 OBJ 格式支持范围，需求仍为受限可用。

- **FR-DIAG-001 第 34 切片**：单报告 `export_report` / `export_report_json` 显式关闭并检查输出流，设备写入/关闭失败不再误报成功；空路径、目录打开失败与 Linux `/dev/full` 均返回结构化失败。`axiom_diagnostics_test` 覆盖成功完整证据、参数/打开/写入失败、源报告不污染及失败后重试。复用 `AXM-IO-E-0005`，无公开签名变化；需求仍为受限可用。

- **NFR-DIA-001 第 30 切片**：STEP 导入的空路径、文件不存在与非可读常规文件分别绑定 `io.import.step.input/path/open`；失败均发生在 Body ID 分配和存储写入之前。`axiom_io_workflow_test` 覆盖阶段检索、JSON、退化输入、模型不污染和失败后成功重试。复用 `AXM-IO-E-0004`，无公开签名变化；需求仍为受限可用，不扩展 STEP 实体交换范围。

- **FR-DIAG-001 第 29 切片**：STEP 导出的无效 Body/空路径、路径校验、打开与最终写入失败绑定 `io.export.step.input/path/open/write` 及输入 Body；显式检查写入/关闭状态，避免 `/dev/full` 误报成功。`axiom_io_workflow_test` 覆盖成功、退化输入、路径/设备失败、阶段检索、JSON、模型不污染和重试。复用 `AXM-IO-E-0005`，无公开签名变化；需求仍为受限可用，HEAL/IO 重量级证据已由 cycle-0073 闭合，辅助接口和更广泛语料仍待扩充。

- **NFR-DIA-001 第 25 切片**：体级、壳级与批量壳级自交验证失败绑定 `heal.validate_self_intersection.*` 细分阶段及 Body/Shell。`axiom_heal_test` 覆盖正常 Strict、非法 Body/Shell、异属 Shell、空批量、阶段检索、JSON 和失败不污染。复用现有错误码且无公开签名变化；网格 SAT 仍为近似验证，需求保持受限可用，当时缺少的 HEAL/IO 重量级证据已由 cycle-0073 闭合，辅助接口与更广泛语料仍待扩充。

- **FR-DIAG-001 第 24 切片**：阶段聚合文本/JSON 导出显式检查最终写入与关闭状态，避免设备写入失败误报成功；空路径在打开文件前拒绝，打开/写入失败复用 `AXM-IO-E-0005`。`axiom_diagnostics_test` 覆盖正常阶段、空阶段 `(unset)`、空路径、目录、Linux `/dev/full`、参数失败不截断、源报告不变及失败后重试。无公开签名或错误码变化，需求仍为受限可用。

- **FR-DIAG-001 第 19 切片**：严格网格导出 QA 的 `AXM-IO-E-0006` 失败绑定 `io.export.mesh_strict_qa` 与输入 Body，补齐越界索引、退化三角形、阶段检索、JSON 证据及失败不污染回归；同时修正文档中该稳定错误码的旧语义。无公开签名变化，需求仍为受限可用。

- NFR-DIA-001 第 15 切片：`export_boolean_prep_stats` 显式关闭并检查输出流，避免写入失败返回成功；参数、文件打开、写入失败分别绑定 `bool.prep.export.input/open/write` 和 `[lhs, rhs]`。参数失败复用 `AXM-BOOL-E-0001`，文件打开失败由误用的 BOOL 输入码修正为 `AXM-IO-E-0005`，写入失败复用该 IO 码。`axiom_boolean_prep_test` 覆盖重叠/相同/分离/接触零体积输入、无效句柄/空路径、目录打开失败、Linux `/dev/full`、阶段/错误码检索及 JSON、参数失败文件不污染、模型计数与 Eval 失效不污染和失败后重试。无公开签名变化；设备写入失败不保证目标文件恢复，需求保持受限可用。

- **FR-DIAG-001 第 14 切片**：补齐指定 ID 批量文本归档，复用单报告文本格式保留完整 Issue（严重级别、码、消息、阶段、实体），保留输入顺序、重复 ID 和特殊字节；打开文件前拒绝任意位置的无效 ID，参数失败不创建/截断目标且源报告不变；显式关闭并检查写入错误，复用现有 CORE/IO 错误码，无公开签名变化。`axiom_diagnostics_test` 覆盖成功、空报告/空阶段/重复 ID、非法参数、路径失败、Linux `/dev/full` 写入失败及拒绝后重新导出。设备写入失败不保证目标文件恢复；需求仍为受限可用，BOOL 受覆盖分支及 cycle-0073 HEAL/IO 重量级包已纳入证据门禁，辅助接口与更广泛语料仍待补齐。

- NFR-DIA-001 第 10 切片：`BooleanService::run` 拒绝未声明的 `BooleanOp` 值，返回 `InvalidInput` / `AXM-BOOL-E-0001`、`bool.input` 与输入实体，避免越过分支后静默创建结果体。`axiom_boolean_prep_test` 覆盖两种诊断模式下的负值/越界值、相同输入体及无效句柄、阶段/错误码检索与 JSON、模型计数及 Eval 失效传播不污染，并验证拒绝后四种合法枚举仍可执行。复用已有错误码，无公开签名变化；需求仍为受限可用，不提升现有 bbox 布尔为精确能力。

- FR-DIAG-001 第 9 切片闭合诊断批量 JSON 归档缺口：指定 ID 导出保留完整 Issue 证据与字符串，参数失败不污染目标文件或源报告；空报告/重复 ID/无效 ID/路径失败由 `axiom_diagnostics_test` 覆盖。DoD：与单报告结构一致，失败有稳定错误码；现有 BOOL 受覆盖分支及 cycle-0073 HEAL/IO 重量级包已通过证据门禁，辅助接口与更广泛语料仍待推进。

- 扩展 BOOL 阶段化诊断覆盖与 JSON `stage` 字段在更多失败分支中的一致性。FR-DIAG-001 预处理告警切片已闭合：`W-0001/W-0002` 绑定 `bool.prep` 与输入/输出体，覆盖重叠、分离、仅接触、无壳级候选及前置失败不污染回归；全部失败分支门禁仍待补齐。
- NFR-DIA-001 早期失败最小诊断切片已闭合（完整测试 16/16 通过）：关闭详细布尔诊断时，无效输入、分离交集及包含导致空结果的失败仍保留错误码、阶段与输入实体；两种模式的检索、JSON 和失败不污染由 `axiom_boolean_prep_test` 覆盖。全部失败分支门禁仍待补齐。
- 将布尔真求交子里程碑拆为“面级候选 -> 交线 -> imprint”可回归切片。

**DoD**

- 对应错误码/诊断码可检索。
- `axiom_boolean_workflow_test` 与 `axiom_boolean_prep_test` 不回归。

### 2.2 `math + geo`

- **FR-GEO-001 / FR-QUERY-001 第 74 批（已通过完整门禁）**：曲面 `closest_point_detailed` 对 Plane/Cylinder/Cone/规则 Sphere/Torus 及其嵌套 Offset 链提供无缓存解析全域求解，无界方向公开 `effective_domain` 与 `domain_was_finiteized`；Bezier/BSpline/NURBS 的非空结点片和递归子片使用正权有理 Bezier 控制网凸包 AABB 下界，并通过 `control_net_bound_patches/pruned_patches` 暴露证书工作量。偏置半径坍缩、负向完整锥偏置自交和数值溢出结构化失败且无部分值。`cycle-0072-gates.log` 最终完整 CTest 16/16 通过（0 失败，120.81 s；Geo 0.58 s、Query/Eval 0.23 s）。仍缺通用无限派生面自动有限化、spindle/horn 环面解析证书、旋转/扫掠专用局部界和大模型独立性能基线。

- **FR-QUERY-001 第 71 批（已通过完整门禁）**：新增 `GeometryIntersectionService::intersect_curve_curve`，对一般有界 3D 曲线返回离散横交/相切/端点、残差、连续重合区间与求值/参数矩形工作量；支持显式有限区间、空集、反向重合、常值退化、Bezier、BSpline、NURBS、圆锥曲线和 CompositeChain。无限 Line 须显式窗口；预算耗尽/无法建界失败不返回部分结果，查询不写求值缓存、Intersection/拓扑存储或事务。首次全量的 Bezier 相切/CompositeChain 回归失败后，repair 改用相对弦保守偏差终止细分，并分别取子片起点右极限/终点左极限建包围。定向 Geo/Query 复验通过，随后两次完整 CTest 均 16/16 通过（0 失败，总耗时 1865.97 s / 1863.95 s；Query/Eval 0.32 s / 0.20 s）。一般高阶异参连续重合证明和无限曲线自动搜索窗口仍待后续；其显式 trim 拓扑接入已由第 74 批闭合受支持子域。

- **FR-GEO-001 第 69 批（已通过完整门禁）**：新增曲线 `closest_point_detailed` 与距离/参数容差、求值预算、解析/距离界/参数分辨率终止结果。Line/LineSegment/Circle/CompositePolyline 解析求解；Ellipse/Parabola/Hyperbola/Bezier/BSpline/NURBS/CompositeChain 全有效域分支限界，样条逐非空结点段覆盖并用保守速度/加速度界剪枝。旧非解析最近参数复用主流程，失败不写求值缓存。首次全量测试暴露的椭圆距离和 NURBS 参数回归已 repair，最终 CTest 16/16 通过。后续仅把“全域最近点精度与收敛”缺口保留在曲面侧：Bezier/BSpline/NURBS 与派生/修剪曲面的完整参数域及 trim 边界。

- FR-GEO-001 第 55 切片：UV 折线 PCurve 最近参数逐段投影改用扩展精度计算差值、点积及距离平方，有限大坐标下仍可选中正确分段；保留端点钳制、零长度段和等距最早参数语义。`axiom_geometry_test` 覆盖段内最近点、端点、重复点、非法查询/句柄、稳定错误码、失败不污染与重试。3D 曲线合同已由第 69 批闭合，曲面合同已由第 71/74 批扩展；PCurve 详细证书仍待推进。
- FR-GEO-001 第 51 切片：线段最近参数的解析投影改用扩展精度计算中间差值、点积和长度平方，避免有限大尺度线段因 `Scalar` 平方溢出返回非有限参数；保持 `[0,1]` 钳制。`axiom_geometry_test` 覆盖段内最近点、端点钳制、退化创建、非法查询/句柄、稳定错误码、失败不污染与重试。其余 3D 曲线合同已由第 69 批闭合，曲面合同已由第 71/74 批扩展；PCurve 详细证书及通用无限派生面仍待推进。
- FR-GEO-001 第 46 切片：3D 复合折线最近参数逐段解析投影并比较三维距离，使用扩展精度中间量避免有限大坐标平方溢出；等距取最早参数，重复控制点安全参与比较。`axiom_geometry_test` 覆盖窄分支、段内投影、退化段、大坐标、非法查询/句柄、稳定错误码和失败不污染。3D 曲线全域精度合同已由第 69 批闭合；第 71/74 批已补曲面详细入口及受支持证书。
- FR-GEO-001 第 41 切片：UV 折线 PCurve 的最近参数改为对每段做解析投影和距离比较，覆盖固定网格容易漏掉的短分段、重复控制点和等距最早参数。`axiom_geometry_test` 覆盖成功、非法查询/句柄、稳定错误码、失败不污染和重试；3D 曲线合同已由第 69 批闭合，曲面合同已由第 71/74 批扩展，PCurve 详细证书仍待推进。
- FR-GEO-001 第 36 切片：椭圆 `closest_parameter/closest_point` 从粗采样初值阻尼细化三维欧氏距离，并把周期缝归一到 `[0, 2pi)`；`make_ellipse` 在写入前拒绝轴长或派生法向长度溢出。`axiom_geometry_test` 覆盖解析最近点、周期缝、非有限查询、有限但溢出的轴向量和几何/缓存不污染；不宣称任意退化椭圆的全局最优，需求仍为受限可用。
- FR-GEO-001 第 31 切片：BSpline/NURBS 曲面内部满重数断点统一选择右侧非空片，上端点及域外钳制统一选择左侧非空片，且点值、一二阶偏导和曲率使用同一单侧语义。`axiom_geometry_test` 覆盖双轴断点、非单位权重、常值退化片、非法结点失败不污染及既有曲面继续求值；不宣称断点全局可微，需求仍为受限可用。
- FR-GEO-001 第 26 切片：BSpline/NURBS 曲面最近参数初值搜索除全域网格外逐个覆盖非空张量积结点片，避免极窄及双轴满重数隔离片被漏采。`axiom_geometry_test` 覆盖多项式/非单位权重有理曲面、成功、非法查询与几何/缓存不污染；不承诺任意曲面全局最优，需求仍为受限可用。
- FR-GEO-001 第 21 切片：补齐 BSpline/NURBS 曲面 u/v 显式结点的非零有效域与 `degree + 1` 重数上限校验，非法输入返回 `AXM-GEO-E-0002` 且不污染几何/缓存；保留合法满重数断点的单侧曲面片求值。`axiom_geometry_test` 覆盖两轴、两类曲面、成功、退化、失败与失败后继续求值，需求仍为受限可用。
- FR-GEO-001 第 16 切片：BSpline/NURBS 最近参数初值搜索在全域粗采样之外逐个采样非空结点分段，避免极窄合法分段和满重数断开的分支被跳过。`axiom_geometry_test` 覆盖显式/推断次数、非单位权重、常值退化分段、非法查询点诊断及几何/缓存不污染。该切片当时仅为初值改进；第 69 批已补曲线全域预算与收敛证书，第 71/74 批已补曲面详细入口及高阶有理控制网证书。
- 收敛退化/大尺度下的谓词与容差行为定义。
- FR-GEO-001 第 11 切片：修复非夹持 BSpline/NURBS 有效域端点重复结点导致选中零长度分段的问题。DoD：端点点值、一二阶导数及曲率使用非空单侧分段；覆盖显式/推断次数、非单位权重、域外钳制、常值退化、非法重数诊断及模型/缓存不污染。回归入口为 `axiom_geometry_test`，需求仍为受限可用。
- 推进样条导数/曲率或稳定最近点的最小增量。显式样条结点逆序/零长度有效域拒绝切片已闭合；结点重数超过 `degree + 1` 的拒绝切片已闭合，覆盖端点/内部超限、显式/推断次数、失败不污染及合法满重数断点单侧一、二阶导数（含非单位权重 NURBS）；后续继续完善其他高重数结点处的导数/曲率语义。

**DoD**

- 失败具备稳定错误码。
- `axiom_math_services_test` 与 `axiom_geometry_test` 覆盖新增语义。

### 2.3 `topo`

- **cycle-0074 FR-QUERY-001 实体空间查询（已通过完整门禁）**：`locate_point` 返回材料奇偶定位、真实最近面内/边/顶点及 ShellId/FaceId，距离为模型长度单位、等距按稳定句柄；`clip_segment` 返回无量纲有序 Inside 区间、共面 Boundary 段和孤立相切 `[t,t]`，只累计 Inside 的材料长度，无交集成功空结果。支持凹面带孔、无序多壳空腔/材料岛与可分辨薄层，位置容差不膨胀材料或合并薄层。共用质量/壳接触/包含前置检查；支撑面与面边界须一致，显式 trim 曲边不能用端点弦替代。预算默认 1000000、非零，仅覆盖前置之后的新三角形计算，实际工作量可精确重放；耗尽/事件分辨率不足/溢出无部分值。最近点、穿透/反向/内部/端点/相切/共面/空集、薄层/姿态尺度、多壳、数值预算失败、支撑面编辑/删除回滚及读审计/缓存/Eval 不污染回归通过（Query/Eval 0.37 s）。仍仅支持无自交的嵌入平面直边双边流形闭壳；曲面/曲边、相交/重叠/容差接触壳、自身全局嵌入证明和大规模加速留待后续。相关类型、单位及只读合同见接口清单 §6.1.1。

- **FR-TOPO-001 / NFR-REL-001 第 74 批（已通过完整门禁）**：`first_boundary_conflict/create_face/validate_face` 复用无诊断、无缓存的 Geo 有限区间求交，覆盖带显式 trim 的圆锥曲线、Bezier、BSpline、NURBS 与混合 CompositeChain，支持递减区间、端点接触、容差邻近和可证连续重合；缺 trim、曲线损坏或预算/数值失败以 `AXM-TOPO-E-0030` 闭合拒绝，冲突类别使用 `AXM-TOPO-E-0031..0033`。事务新增嵌套保存点、保留目标的重复局部回滚、回滚外层使内层失效、LIFO 释放、移动所有权和累计审计；无效句柄复用 `AXM-TX-E-0003`，取消以 `AXM-TX-E-0007` 优先恢复整事务。首次完整门禁暴露保存点测试夹具污染，第二次暴露 Ops/Heal 120.03 s 超时；两次 repair 均未放宽门禁，最终完整 CTest 16/16 通过（0 失败，120.81 s；Topo 0.13 s、Ops/Heal 95.13 s、运行时不变量 0.00 s）。保存点仍为内存全拓扑快照和单活动写者，取消不抢占单次调用。

- **FR-TOPO-001 第 73 切片（专项通过）**：`first_boundary_conflict` / `create_face` / `validate_face` 精确展开显式裁剪 CompositePolyline 和线性 CompositeChain，支持递增/递减区间，并保留复合曲线内部折点的边内部语义。相交、共线重叠、容差邻近、建面零写入与存量面验证已回归；当时未接入的真曲线子域已由第 74 批复用误差受控 Geo 求交器闭合。

- **FR-TOPO-001 / FR-QUERY-001 第 72 批（专项通过）**：公开 `create_trimmed_edge` 与 `edge_curve_interval`，在 EdgeId 分配前验证有限非零参数区间、曲线定义域和参数端点到 v0/v1 的容差一致性；显式曲边的 `edge_length/loop_length/face_boundary_length` 复用曲线真实区间弧长，曲边区间极值或控制点凸包进入面/壳/体拓扑 bbox。旧 `create_edge` 完全兼容，未带区间曲边继续拒绝弦长冒充弧长；失败不增加事务写计数或几何缓存。一般曲线跨环求交已由第 74 批接入该区间；完整 PCurve/曲面参数域一致性仍待推进。

- **FR-QUERY-001 第 70 批（已通过完整门禁）**：新增 `shell_mass_properties/body_mass_properties`，从当前真实拓扑重算平面直边双边流形闭壳的单位密度体积、面积、质心和世界坐标惯性，支持凹面、孔与多个独立实体壳的平行轴汇总。查询不分配网格、不写缓存/Eval/事务计数；`axiom_query_eval_test` 覆盖解析盒体、凹带孔拉伸、双壳、失败类、删除和回滚，最终完整 CTest 16/16 通过。空腔与材料岛严格包含语义已由第 71 批闭合，cycle-0074 又补齐实体空间查询；曲面/曲边及相交/重叠多壳仍未支持。

- **FR-TOPO-001 / NFR-REL-001 第 69 批（已通过完整门禁）**：公开 `first_boundary_conflict`，统一有限 Line/LineSegment 的内部相交、端点相接、共线正长度重叠与容差内正距离邻近，新增 `AXM-TOPO-E-0028/0029`；公开取消源/令牌、带令牌事务、轮询、状态/写次数、活动写者和累计指标，取消恢复完整快照且不推进版本或成功提交审计，`AXM-TX-E-0007` 与 core runtime invariant 已覆盖。最终完整 CTest 16/16 通过。当时待办的显式 trim 真曲线跨环求交与受限保存点已由第 74 批闭合；BOOL/HEAL/IO 长阶段轮询、增量保存点和更细粒度隔离仍待推进。

- **NFR-REL-001 第 57 切片**：`create_body` 在写入前拒绝壳成员面空或悬空曲面引用，避免有效包围盒掩盖受损拓扑；复用 `AXM-TOPO-E-0005` 并关联壳、面、曲面 ID。`axiom_topology_test` 覆盖诊断 JSON、ID/存储/事务计数不污染，以及修复后重试、回滚和提交。拓扑 API 边界取消已由第 69 批闭合，更广泛 S0/S1 失败注入仍待推进。

- **FR-TOPO-001 第 56 切片**：`create_face` 写入前拒绝不同边界环的非平行直线边端点相接，`validate_face` 检出存量缺陷；`AXM-TOPO-E-0027` 关联两环与两边。`axiom_topology_test` 覆盖外/内及内/内、诊断 JSON、失败不污染和回滚。其后的共线重叠、容差邻近和显式 trim 真曲线求交已由第 69/74 批验收；完整 trim bridge 与持久命名仍待推进。

- **FR-TOPO-001 第 52 切片**：`create_face` 写入前拒绝不同边界环的直线/线段边在三维内部相交，`validate_face` 检出存量缺陷；`AXM-TOPO-E-0026` 关联两环与两边。`axiom_topology_test` 覆盖外/内及内/内相交、合法双孔面、诊断 JSON、失败不污染和回滚。当时缺少的端点触碰、共线重叠、容差邻近和显式 trim 真曲线规则已由第 56/69/74 批逐步闭合；需求仍因完整 trim bridge 等缺口保持受限可用。

- **NFR-REL-001 第 48 切片**：`create_shell` 在分配 ShellId 前验证成员面引用的曲面确实存在；空句柄或悬空 `SurfaceId` 返回 `InvalidTopology / AXM-TOPO-E-0005`，关联面与曲面 ID。`axiom_topology_test` 通过受损面注入覆盖诊断 JSON、ID/存储/事务计数不污染，以及恢复曲面引用后的重试、回滚和提交。拓扑 API 边界取消已由第 69 批闭合，更广泛 S0/S1 失败注入仍待推进。

- **FR-TOPO-001 第 47 切片**：`create_face` 在分配 FaceId 前拒绝不同环中独立 `VertexId` 的三维坐标精确重合，`validate_face` 对已有面执行同一规则；`AXM-TOPO-E-0025` 关联两环与两个顶点。`axiom_topology_test` 覆盖外/内及内/内相接、成功双孔面、诊断 JSON、失败不污染和回滚。仍缺边段相交、容差邻近相接与完整 trim bridge，需求保持受限可用。

- **NFR-REL-001 第 43 切片**：`create_shell` 在分配 ShellId 前检查成员面全部内环，拒绝缺失或已损坏的内环，复用 `AXM-TOPO-E-0005` 并关联面和内环。`axiom_topology_test` 覆盖故障注入、诊断 JSON、ID/存储/事务计数不污染，以及修复输入后的重试、回滚与提交。拓扑 API 边界取消已由第 69 批闭合，更广泛 S0/S1 失败注入仍待推进。

- **FR-TOPO-001 第 42 切片**：`create_face` 在分配面 ID 前拒绝同一面的不同边界环共用 VertexId，`validate_face` 对存量面执行同一规则；使用 `AXM-TOPO-E-0024` 关联两环与冲突顶点。`axiom_topology_test` 覆盖外/内环及内/内环相接、合法双孔面、诊断 JSON、失败不污染和回滚。规则只识别拓扑顶点 ID，几何自交与完整 trim bridge 仍待补齐，需求保持受限可用。

- **NFR-REL-001 第 38 切片**：`create_body` 在包围盒校验成功后才分配 BodyId；受损壳导致的失败保留全局实体 ID、存储和事务计数。`axiom_topology_test` 以受损空壳注入覆盖错误码/JSON、非法句柄、回滚与成功重试。拓扑 API 边界取消已由第 69 批闭合，更广泛失败注入门禁仍待推进。

- **FR-TOPO-001 第 37 切片**：`create_face` 在写入前执行已存在的面绑定环边数规则：外/内环至少三条共边，保留同空间曲线双弧环例外；不合格外/内环分别返回 `AXM-TOPO-E-0003/0004` 并关联环。`axiom_topology_test` 覆盖双直线闭合退化环、成功三角环重试、已有同曲线双弧环、诊断 JSON、失败不污染与回滚。需求仍为受限可用，几何自交及完整 trim bridge 尚待补齐。

- **NFR-REL-001 第 33 切片**：落实 `SnapshotSerializable` 已声明的单写约束；同一内核已有活动拓扑事务时，重叠事务以关闭状态返回，其写入、提交和回滚均失败且不能污染所有者状态。所有者提交/回滚或空事务析构后释放写槽，后续事务可重试。`axiom_topology_test` 覆盖重叠拒绝、所有者提交、拒绝者失败不污染、回滚重试和空事务释放。第 69 批已在该语义上补齐拓扑 API 边界协作式取消；跨进程 SERIALIZABLE 仍不在支持范围内。

- FR-TOPO-001 第 32 切片：`create_loop` 在写入前拒绝闭合终点之外重复经过同一 `VertexId` 的自接触非简单环，返回 `InvalidTopology` / `AXM-TOPO-E-0023` 并关联重复顶点与两条冲突定向边。`axiom_topology_test` 覆盖拒绝、JSON、存储/反向索引/事务计数不污染、拒绝后合法三角环重试与回滚。新增稳定错误码与文档映射，未将几何自交检测或周期 seam 扩大为本切片能力，需求保持受限可用。

- NFR-REL-001 第 28 切片：修复 `replace_surface` 成功后未计入事务写操作的可靠性缺口；仅执行曲面替换的活动事务现在会在析构时自动回滚，提交后总写入数与 `replaced_surfaces` 分项一致。`axiom_topology_test` 覆盖非法替换失败不污染、仅替换作用域回滚、显式回滚和提交审计。无公开签名或错误码变化，需求保持受限可用。

- FR-TOPO-001 第 27 切片：`validate_edge` 使用内核线性容差检查两个拓扑端点是否位于引用的 3D Curve 上；偏离或最近点求解失败返回 `InvalidTopology` / `AXM-TOPO-E-0008`，诊断关联边、曲线与问题顶点。`axiom_topology_test` 覆盖成功、容差内端点、明显偏离、诊断 JSON、验证不污染和回滚；同时修正一处既有跨环测试夹具中与端点不一致的直线方向。无公开签名或错误码变化，需求保持受限可用。

- NFR-REL-001 第 23 切片：活动 `TopologyTransaction` 未显式关闭便离开作用域时由 `noexcept` 析构自动回滚，防止创建、删除和替换直接泄漏到共享 store；空事务、已关闭事务和移动后的源对象析构不改变模型。`axiom_topology_test` 覆盖创建、既有面替换、级联删除、移动目标回滚、显式提交持久性和索引不变量。第 69 批已补拓扑 API 边界协作式取消；细粒度隔离和全量 S0/S1 门禁仍待推进。

- FR-TOPO-001 第 22 切片：`validate_shell_closedness` 将“只有一个面连通分量”纳入 Strict 壳闭合性合同；两个各自闭合但互不共享边的分量返回 `InvalidTopology` / `AXM-TOPO-E-0018`，诊断关联壳和各分量代表面。`axiom_topology_test` 覆盖单分量成功、双闭合分量失败、诊断 JSON、失败不污染及回滚。未增加公开签名或错误码，需求保持受限可用。

- NFR-REL-001 第 18 切片：收紧 `TopologyTransaction` 唯一所有权。事务只能移动构造，禁止复制和移动赋值；移动后源对象保持可查询的关闭状态，不能写入、提交或回滚，避免默认移动留下活动源对象并发生空状态访问或误撤销。`axiom_topology_test` 覆盖编译期所有权约束、目标提交/回滚、源对象重复关闭操作、失败不污染和已提交实体存续。第 69 批已补拓扑 API 边界协作式取消；细粒度隔离仍待交付。

- FR-TOPO-001 第 17 切片：`validate_shell_closedness` 在边恰由两个不同面使用时进一步要求两侧 coedge 的 `reversed` 相反；同向配对返回 `InvalidTopology` / `AXM-TOPO-E-0015`，关联壳、边、两面与两 coedge。`axiom_topology_test` 覆盖同向失败、反向成功、零厚度双面退化壳、JSON 诊断、验证失败不改变拓扑/事务写计数及回滚。未增加公开签名或错误码，需求保持受限可用。

- NFR-REL-001 第 13 切片：修复同一事务新建面/壳/体被修改或删除后，回滚从快照复活新建实体的问题（S0：回滚污染）。先恢复快照再清理本事务创建的高层拓扑，保留已有快照/触达计数语义。`axiom_topology_test` 覆盖新建面替换曲面、面/壳/体删除、空壳/空体级联删除、已有面与新建共享壳混合快照、重复删除失败不污染、全部新建拓扑句柄失效、原模型/反向索引恢复、空回滚及后续提交。第 69 批已补拓扑 API 边界协作式取消；细粒度隔离仍待交付。

- FR-TOPO-001 第 12 切片：`create_face` 在写入前拒绝同一面外环/内环及内环之间复用 EdgeId，复用 `AXM-TOPO-E-0014` 并关联两个冲突环与边。`axiom_topology_test` 覆盖单边共享、完全重合边界、正反共边、内环顺序、诊断 JSON、计数/索引不污染、拒绝后合法双孔面及回滚后重新提交；不同面通过独立共边共享 Edge 仍允许。未增加公开签名或错误码，未扩展 seam/周期修剪支持，需求保持受限可用。

- NFR-REL-001 第 8 切片：保护事务撤销记录，活动事务（含空事务）拒绝 `clear_tracking_records`；提交/回滚后允许幂等清理。DoD：稳定错误码 `AXM-TX-E-0006` 可检索/导出，重复拒绝不改变模型与计数，创建/删除及 PCurve 修改仍可回滚，关闭后清理不改变模型；由 `axiom_topology_test` 回归。

- 继续加严 Strict 规则。拓扑创建入口的有限坐标检查切片已闭合：`create_vertex` 拒绝 NaN/±Inf，失败不写入模型或事务计数，并覆盖诊断 JSON 导出与回滚。
- FR-TOPO-001 单共边环闭合检查切片已闭合（完整测试 16/16 通过）：移除单共边放行，按定向端点 ID 校验闭合；覆盖正反方向、坐标重合但 ID 不同的退化输入、失败不污染、后续闭合三角环与回滚、`E-0002` JSON 导出。
- 推进 trim bridge 的可物化子规则。NFR-REL-001 共边 PCurve 绑定回滚切片已闭合：已有共边重复绑定/清除后回滚恢复原值，无效句柄失败不污染，提交保留新值；由 `axiom_topology_test` 回归。

**DoD**

- `axiom_topology_test` 有新增失败类回归。
- 对应诊断码可稳定导出。

### 2.4 `heal + io`

- **cycle-0073 重量级证据与失败原子性已闭合**：HEAL 模块回收派生结果、恢复 trim 原绑定和批量 Eval 失效；IO 主格式与 auto/后验/批处理补齐证据，批量实际失败恢复对象/缓存/链接/Eval/next_id。STEP/AXMJSON 后验问题复制为 `io.post_import.*` 且不改源报告，cycle-0084 起未解决的后验失败返回失败并原子回滚，ReportOnly/SuggestOnly 不升级为修改策略。批量导出不保证文件事务，标准 STEP/IGES 实体及格式子集限制不变。
- **长期候选（不抢占 S5-HEAL）**：普通文本/目录等非主格式辅助接口的阶段/实体/有限数值证据，以及更广泛失败注入、导入后验报告和文件写入一致性语料；不重复开发已通过的 HEAL/IO 重量级迁移。

- NFR-DIA-001 第 20 切片：`validate_geometry` 的非法目标、bbox、owned B-Rep 引用、Strict 参数域/有限值/近重复顶点/退化边面/面法向失败统一绑定 `heal.validate_geometry.*` 细分阶段，并关联目标 Body 与已有问题子实体。`axiom_heal_test` 覆盖成功、非法句柄、Strict 退化、阶段检索、JSON 导出及模型计数不污染；复用现有错误码，无公开签名变化，需求保持受限可用。

- 本批直接阻断项 MeshRep 有限顶点/索引/非退化验证允许合法二维网格；通用闭合/自交不在本批支持域。

**DoD**

- `axiom_heal_test`、`axiom_io_workflow_test` 与 `axiom_ops_heal_test` 增量断言通过。
- `related_entities` 保持可追踪。

## 3. 本阶段 backlog 表

| 优先级 | 状态 | 模块 | 交付物（摘要） | 建议 `ctest` | 依赖 |
|--------|------|------|----------------|--------------|------|
| P0 | ready_for_acceptance（cycle-0091；正式状态见当前进度§5.2.1） | ops/geo/topo/rep/diag/eval/core | S6-EXIT：固定盒域move→replace→offset及圆角/倒角/闭腔与单开口抽壳独立分支→Strict→查询/表示，独立解析/OBJ及稳定拒绝/全链回滚；完整代码门禁通过，停止功能扩展 | `axiom_ops_heal_test`、`axiom_query_eval_test`、`axiom_representation_io_test`、`axiom_kernel_runtime_invariant_test`；完整CTest及既有性能基线 | geo/topo/rep |
| P0 | 已闭合（门禁） | core/io | 门面 IO 能力与 `IOService` 一致 | `axiom_smoke_test` | — |
| P0～P1 | 已闭合（首批） | diag/ops/io/heal | 工作流 `Issue.stage` + JSON 导出可聚合 | `axiom_diagnostics_test`、`axiom_boolean_workflow_test`、`axiom_heal_test`、`axiom_ops_heal_test` | core |
| P0～P1 | 已闭合（第 69 批） | diag/ops | 结构化数值证据、覆盖审计与 BOOL 受覆盖失败门禁 | `axiom_diagnostics_test`、`axiom_boolean_prep_test` | core |
| P1 | 已闭合（第 69 批） | geo | 曲线全有效域最近点精度、预算和收敛证书 | `axiom_geometry_test`、`axiom_query_eval_test` | math |
| P1 | 已闭合（第 69 批） | topo/core | 有限直线跨环冲突；拓扑协作式取消与累计审计 | `axiom_topology_test`、`axiom_kernel_runtime_invariant_test` | geo |
| P1 | 已闭合（第 70 批子域） | ops/topo | 闭合样条/复合导轨扫掠、部分角旋转、平面直边闭壳拓扑质量属性 | `axiom_ops_heal_test`、`axiom_query_eval_test` | geo/topo |
| P1 | 已闭合（第 70 批子域） | io/diag | AXMJSON/Axiom IGES/Axiom BREP 子集物化前诊断与失败隔离 | `axiom_io_workflow_test` | core |
| P1 | 已闭合（第 71 批子域） | ops | 带孔有向区间/整周旋转；开放导轨正比例变截面扫掠 | `axiom_ops_heal_test` | geo/topo |
| P1 | 已闭合（第 71 批子域） | geo/query | 一般有界 3D 曲线求交、可证明重合、预算与只读合同 | `axiom_query_eval_test`、`axiom_geometry_test` | math |
| P1 | 已闭合（第 74 批子域） | geo/topo/core | 解析无界面与高阶有理曲面最近点证书；显式 trim 真曲线跨环冲突；嵌套保存点与累计审计 | `axiom_geometry_test`、`axiom_topology_test`、`axiom_kernel_runtime_invariant_test` | math/geo |
| P1～P2 | 进行中 | geo/topo | 通用无限派生面与大模型证书；高阶异参重合证明、完整 trim bridge / 周期缝 / 奇点 / Strict 规则 | `axiom_geometry_test`、`axiom_topology_test` | math |
| P1～P2 | 已闭合（cycle-0073 重量级包） | diag/heal/io | HEAL 验证/修复/后验/trim/批量与 IO 主格式/auto/后验/批量有限数值证据、模块审计和失败原子性 | `axiom_diagnostics_test`、`axiom_heal_test`、`axiom_io_workflow_test` | core |
| P1～P2 | 进行中 | diag/io | 非主格式文本/目录辅助接口证据、更广泛失败注入与文件一致性语料 | `axiom_io_workflow_test`、`axiom_diagnostics_test` | core |
| P1 | 已闭合（cycle-0073 子域） | ops | 显式轮廓法向扭转拉伸、真实采样闭壳、交替剖分体积偏差修复 | `axiom_ops_heal_test` | geo/topo |
| P1 | 已闭合（cycle-0074 三包） | ops | 分段高度拉伸律、采样弦长比例律与联合比例/扭转律，关键站/周期焊接/侧壁接触、真实拓扑质量及失败原子性 | `axiom_ops_heal_test` | geo/topo |
| P1 | 已闭合（cycle-0074 子域） | topo/query | 平面直边实体点定位、真实最近边界与线段材料/共面/相切裁剪，多壳奇偶/薄层/预算及只读合同 | `axiom_query_eval_test` | geo/topo |
| P1～P2 | 进行中 | topo/query | 曲面/曲边闭壳质量与空间定位、相交多壳、自身全局嵌入证明及大规模加速 | `axiom_query_eval_test` | geo/topo |
| P1～P2 | 进行中 | ops | 带孔尖顶/触轴旋转、零或负扫掠比例、一般非线性解析比例/扭转律、任意截面匹配、分支/坍塌放样、逐壁恒角拔模、嵌套复合导轨、解析圆弧/样条扫掠、精确旋转/解析螺旋面及显式轮廓历史 | `axiom_ops_heal_test` | geo/topo |
| P2 | 后续独立工作，非本包扩展 | ops | 精确谓词、通用曲面/曲边求交及全局嵌入；平面真实重建/内部共面/受限Safe固定参考已通过，边点Union和薄层仍拒绝 | `axiom_boolean_*` | geo/topo |
| P2～P3 | 后续独立范围 | eval/rep | 受限误差/原子性已过cycle-0086代码门禁；全类型误差、seam连通性及大模型增量性能 | `axiom_query_eval_test`、`axiom_representation_io_test` | ops（部分） |

- **FR-QUERY-001 第 68/70/71/74 批（已通过完整门禁）**：第 68 批 `face_area` 支持 Plane/Cylinder/Cone/Sphere/Torus 及 Trimmed/Offset 的折线 PCurve 修剪面积；第 70/71 批 `shell_mass_properties/body_shell_regions/body_mass_properties` 补齐平面直边双边流形闭壳的质量属性、多实体壳、嵌套空腔和材料岛；第 71 批 `intersect_curve_curve` 闭合一般有界 3D 曲线离散求交与可证明连续重合子域；第 74 批把该有限区间求交流程接入显式 trim 跨环门禁，并补齐解析无界面与高阶有理曲面最近点证书。仍不支持高阶/派生曲面修剪面积、曲面/曲边闭壳质量积分、相交/重叠多壳、壳自身全局嵌入证明、大规模加速和一般高阶异参连续重合；cycle-0074 已新增平面直边实体点定位、真实最近边界及线段材料/共面/相切裁剪，空腔与材料岛按严格包含奇偶处理。

## 4. 下一未闭合批次

当前唯一退出任务为 **cycle-0091 / S6-EXIT**，`stage_task_id=S6-EXIT`、`stage_outcome=ready_for_acceptance`。[两条逐项证据](../quality/AxiomKernel_测试与验收方案.md#118-cycle-0091--s6-exit-门禁与逐项证据)与[固定机械夹具支持矩阵](../api/AxiomKernel_详细模块接口清单.md#833-stage-6-固定机械夹具退出支持矩阵cycle-0091--s6-exit)覆盖建模→直接编辑/偏置→圆角、倒角及两种抽壳独立分支→Strict→查询/表示闭环、独立解析/OBJ参考及失败/全链回滚。调度器独立完整构建成功，CTest **16/16、0失败、201.93 s**；四必需 ops_heal/query_eval/representation_io/runtime **158.06/1.95/14.88/0.13 s**，性能 **1.83 s**。develop的未运行表述已由实际完整代码门禁取代，无repair报告；FR-MOD-001与FR-BLEND-001保持受限可用，Stage 6进行中。文档本轮同步，最终文档门禁及调度器提交成功未记录，不记正式已验收或Stage 6已退出。本包停止功能扩展；阶段任务优先于需求权重、历史remaining和新增变体，基础层修复仅限本任务直接阻断项。

以下是保留的长期候选，不构成本批授权，不抢占 S6-EXIT 或自动启动后续阶段：

1. FR-GEO-001 / FR-QUERY-001：继续极端尺度、通用退化曲面、通用无限派生面自动有限化、旋转/扫掠专用局部界与大模型共享空间证书；无限曲线仍须显式有限窗口，一般高阶异参连续重合证明仍待闭合。
2. FR-TOPO-001：在已交付的显式 trim 真曲线跨环门禁之上，推进 3D edge trim 与 PCurve/曲面参数域双向一致性、周期缝、奇点和持久命名。
3. FR-DIAG-001：保留 cycle-0073 HEAL/IO 重量级数值证据及模块审计门禁，扩充普通文本/目录辅助接口与更广泛失败注入语料。
4. NFR-REL-001：在长耗时 BOOL/HEAL/IO 内部阶段轮询取消，推进增量/结构共享保存点、真正嵌套子事务及更细粒度隔离语义。
5. BOOL平面真实重建/共面与受限Safe已有固定参考，后续需精确谓词、通用曲面/曲边与全局嵌入证明、工业相切/薄层支持；HEAL 自交/流形性/容差冲突的可回放修复。
6. IO 标准 IGES/STEP 实体交换（Axiom元数据子集已有历史代码门禁，正式验收未由本轮追认）或通用 3MF 下一里程碑；EvalGraph 成本门禁与 Plugin 隔离继续按长期树推进。
7. FR-OPS-001：在已验收的有向区间旋转与分段正比例/扭转律上，继续解析圆弧/样条扫掠曲面、精确旋转或一般非线性解析律；零/负扫掠比例、带孔尖顶/触轴、任意截面匹配、嵌套复合导轨和显式轮廓历史仍不支持。
8. FR-QUERY-001：在平面直边 `locate_point/clip_segment` 上继续曲面/曲边闭壳质量与空间定位、壳自身全局嵌入证明、相交多壳及大规模加速；质量前置检查仍不纳入新三角形预算。

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
