# AxiomKernel 测试与验收方案

本文档定义 `AxiomKernel` 的测试体系、质量门禁、验收流程和阶段性度量指标，目标是确保几何引擎在正确性、鲁棒性、性能、互操作性和可维护性方面达到可交付标准。

## 1. 文档目标

本文档用于明确：

- 测试范围
- 测试层级
- 测试数据集要求
- 验收指标
- 自动化策略
- 发布前质量门禁

### 1.1 cycle-0073 批次验收记录

依据调度器独立门禁日志 [cycle-0073-gates.log](../../.axiom-agent/logs/cycle-0073-gates.log) 与当前代码/回归 diff，本批 HEAL、IO 和扭转拉伸功能包已验收。开发报告中的“未编译/未运行”仅描述 develop 阶段约束；本记录采用调度器修复后的最终结果。本次文档同步未重新构建或运行测试。

日志记录两轮 `cmake -S . -B /workspaces/axiom/build-agent -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON`、`cmake --build /workspaces/axiom/build-agent --parallel 4` 和 `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error`。首次完整 CTest **15/16 通过、1 失败、106.79 s**，`axiom_ops_heal_test` 在 `twisted_convex` 的拓扑/质量/Strict 检查失败（79.81 s）；HEAL 和 IO 当轮通过。repair 定位到扭转站间四边形固定对角线使一阶有向体积误差累积，改为仅对扭转侧壁交替对角线，保持角站数、实际拓扑、闭壳/反向索引及非扭转路径；未放宽断言或性能阈值。修复报告记录 Ops 定向构建和测试，最终验收以日志中的修复后全量 **16/16 通过、0 失败、126.44 s** 为准。

| 最终门禁 | 结果 | 本批回归证据 |
|---|---|---|
| 完整构建（并发 4，测试与示例开启） | 成功 | 全部库、测试与示例目标完成；首次构建仍有 BoundaryEdge/Issue 聚合成员初始化缺失及未使用参数告警，不宣称无告警构建 |
| `axiom_heal_test` | 通过，0.06 s | `AXM-` / `heal.` 模块 `audit_evidence`、有限数值 JSON 与源报告不污染；单项后验失败回收、批量晚失败及自动修复失败回滚 |
| `axiom_io_workflow_test` | 通过，13.45 s | `AXM-` / `io.` 模块 `audit_evidence`、JSON 数值/批量上下文、后验验证/修复证据；精确体和网格批量晚失败对象数量/next_id 回滚，精确体批次另核对体/面缓存恢复，并检查原位重试复用 ID；候选/目录/条件工作流真实失败传播 |
| `axiom_ops_heal_test` | 通过，98.84 s（低于既有 120 s 限制） | 扭转凸/凹与孔、绕向、空间旋转、方向缩放/反转、正负部分角/整周/零角；真实拓扑/反向索引/bbox/质量/Strict/网格、退化输入、活动事务失败原子性、拓扑编辑回滚与重试；第 60/61/62/65 包既有回归同次通过 |
| `axiom_representation_io_test` | 通过，10.46 s | 表示转换与 IO 集成回归 |
| `axiom_perf_baseline_test` | 通过，1.68 s | 保留现有性能门禁；该数值为 CTest 墙钟，不是新增扭转基准或跨环境性能保证 |
| 完整 CTest（含其余测试） | 16/16 通过，0 失败，126.44 s | 本批及既有包无待验收项；日志未记录本批文档检查结果 |

模块审计筛选 Error 及以上问题，使用 `issue_code_prefix="AXM-"`，避免漏掉复用的 CORE/VAL/TOPO 根因码；分别要求 `heal.` / `io.` 阶段、关联实体与有限数值证据。IO 预物化零令牌表示没有可关联的模型实体；审计通过仅证明证据结构完整，不代表模型、格式或算法工业完备。

验收边界：HEAL trim 仍限 Plane/Cylinder/Sphere，验证/修复规则范围不扩大；HEAL 回滚不恢复 `next_id`，批量返回报告不合并子项全部根因 issue，子项原诊断仍保留。IO 标准 STEP/IGES 实体、普通文本/目录辅助接口与更广泛失败注入语料未因此闭合。后验验证/自动修复 issue 可随 `Ok` 导入返回，批量模型回滚由子项实际失败触发，批量导出不保证文件事务。扭转方向必须法向、中心共面、正距离、扭角最多一周，角站差不超过 7.5°；真实结果仍为保守采样多面体，不是解析螺旋面，未组合变比例/至平面拉伸。FR-OPS-001 继续进行中，FR-DIAG-001/NFR-DIA-001 继续受限可用。

### 1.2 cycle-0075 / S3-QUERY 门禁与逐项证据

历史 develop/repair 报告记录 `stage_task_id: S3-QUERY`、`stage_outcome: ready_for_acceptance`；当前唯一任务已切换为 cycle-0079 / S3-EXIT（§1.6），本节保留历史证据。该批关闭已声明真实多面体的截面、最近边界和实体距离主链，未扩展后续阶段。依据 [cycle-0075-gates.log](../../.axiom-agent/logs/cycle-0075-gates.log) 与实际 diff：调度器两轮独立配置/完整构建（测试与示例开启、并发 4）；首次 CTest **14/16 通过、2 失败、141.18 s**，Ops 在有向区间旋转验证失败，Query 在实际截面/距离回归失败。repair 翻转负向旋转侧壁并在分配前验证共享边双边反向，修正非等边楔体斜面法向 `(dy,dx,0)`，修正把拒绝创建空体当有效空体的夹具。

repair 报告另记录三项目标定向构建/并发 CTest **3/3 通过、130.72 s**（Query 2.41 s、Ops 130.71 s、representation/IO 17.52 s）；最终结论采用调度器日志的修复后全量结果，不以定向测试替代完整门禁。

| 最终调度器门禁 | 实际结果 |
|---|---|
| `cmake -S . -B /workspaces/axiom/build-agent -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON`；完整构建 `--parallel 4` | 成功 |
| `axiom_query_eval_test`（必需） | 通过，0.90 s |
| `axiom_ops_heal_test`（必需） | 通过，121.81 s；以本次日志为准，历史 120 s 描述不作为本批门禁结论 |
| `axiom_representation_io_test`（空截面兼容回归） | 通过，9.87 s |
| `axiom_geometry_test` / `axiom_topology_test` / `axiom_perf_baseline_test` | 通过，0.53 / 0.11 / 1.45 s |
| `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` | **16/16 通过、0 失败、148.44 s** |

日志仍记录 BoundaryEdge/Issue 缺失成员初始化告警，未记录单独严格告警门禁或文档检查。本文档同步没有重新构建或运行测试；正式验收须调度器完成文档检查和提交，不预记任务已验收，Stage 3 与 FR-OPS-001 / FR-QUERY-001 均保持进行中。

下表按四条验收要求排列 `stage_evidence`；支持矩阵与接口细节见 [接口清单 §6.1.2](../api/AxiomKernel_详细模块接口清单.md#612-stage-3-截面最近点与距离支持矩阵cycle-0075--s3-query)。

| 验收条目 | 测试文件 / 断言 | 独立参考结果 | 限制说明 |
|---|---|---|---|
| 1. 真实平面截面，非轴对齐与凹/带孔，排除 bbox 伪截面 | `tests/eval/query_eval_test.cpp::stage3_section_distance_regression/check_section`：通用/专用面积一致、三角索引有效、实际面积求和、顶点平面残差；同函数凹带孔 cap 夹具逐三角核对重心处于材料；`tests/ops/ops_heal_test.cpp::test_stage3_model_query_chain` 与 `test_directed_interval_revolutions`：建模后真实截面 | 单位盒 `x+y+z=1` 为 `sqrt(3)/2`，单位楔体水平 `0.5`；凹 L 减孔 `19`，`x+y=5` 两真实矩形 `20*sqrt(2)`，旋转平移后 `19`；空腔+岛 `100−36+4=68`；平移非等边楔体 z=5 面积 `3`。建模截面 `4` 或 `4*1.5²=9`；旋转采样环 `n*(R²−r²)*sin(span/n)/2`，带孔有向轮廓 `7*n*sin(span/n)` | 浮点计算当前平面直边多面体。扫掠/旋转/扭转是采样剖分结果，不能以连续解析截面公式取代；bbox 内缺角的真实空集已验证 |
| 2. 真实边界最近点/距离，bbox 重叠但实体分离及相切 | Query 同函数 `check_distance`：通用/专用/标量/反向一致，距离等于见证点欧氏距离，FaceId/ShellId 一致，locate_point 核对材料闭集及正距离边界；既有 `body_spatial_query_regression` 面/边/角独立参考；Ops 有向区间四方向×四绕序/起点变体 | L 缺角小盒 `1`、孔内盒 `0.25`、分离三角棱柱 `1.5/sqrt(2)`、斜交棱线内部 `1`、空腔悬浮体 `0.5`，自身/包含/面与点相切 `0`。单位楔体点 `(1,1,0.5)` 最近 `(0.5,0.5,0.5)` 距离 `1/sqrt(2)`，孔心 `0.5`；非等边楔体点 `(3,5,5)` 最近 `(1+8/13,2+27/13,5)` 距离 `6/sqrt(13)`。`1e-18` 正间隙相对误差 ≤`1e-10` 且见证 z 为 `0/1e-18`；旋转轴最近边界 `2*cos(span/(2*n))`，独立采样体积 `35*n*sin(span/n)` | 实际三角边界浮点距离，没有大规模加速；不支持解析曲面/曲边或占位体。内部点查最近边界与两个实体材料闭集距离语义不同 |
| 3. 精确/采样与不支持边界、稳定错误码/阶段、空交集一致 | Query sphere/旧 thicken、非法输入、换曲面/支撑面错配/删面/删体、预算重放和少一预算、远隔小分量数值失败；`tests/rep/representation_io_test.cpp::main` 旧 section 无交集成功且句柄为零 | 不支持 `NotImplemented / AXM-CORE-E-0004 / query.section.support_gate/query.closest_point.support_gate/query.distance.support_gate`；无交集空容器/无效 bbox/`MeshId{}`，共面有面积，边/点接触零面积；`nextafter(1,2)` 外部近邻平面保持空集；非法输入 `AXM-CORE-E-0002 / input_gate`，预算耗尽同码 `/budget`，不可分辨截面 `AXM-QUERY-E-0002 / query.section.numeric` | ExactBRep 为表示标签，查询仍是浮点计算；默认 1000000 工作预算不计前置检查；壳间建模容差接触拒绝，未证明壳自身全局嵌入。公共 `create_body({})` 是 `OperationFailed / AXM-TX-E-0001`，只验证拒绝及写计数不变，零壳截面/距离内部合同未构造或验收 |
| 4. 成功/退化/失败/回滚、模型/缓存/Eval/事务只读与跨模块一致性 | Query 同函数比较对象总数、mesh/intersection、体/面三角化和曲线/曲面求值缓存、Eval invalid/recompute、事务 writes；Ops 主链比较 rep ExactBRep/真实 topo bbox、面/壳归属、source_faces/provenance、通用/专用质量/截面及缓存/Eval，换面失败禁止旧质量恢复，rollback 后 Strict/截面恢复；有向旋转失败原子性与重试 | 所有详细查询不写模型/缓存/Eval/事务；删体/面及曲面/错配支撑面失败无 value，回滚恢复真实查询；兼容 section 仅有面积时 mesh_records 精确增加 `1`，不填三角化缓存，空集/失败不发布；正负旋转共享边反向与楔体支撑修复已由完整门禁验证 | 诊断记录和 Topo 只读审计可增长；兼容网格发布是约定副作用。跨模块新增断言是受支持路径的选定夹具，不宣称后续阶段、全体类或零壳分支完备 |

### 1.3 cycle-0076 / S3-MASS 门禁与逐项证据

历史报告记录 `stage_task_id: S3-MASS`、`stage_outcome: ready_for_acceptance`；当前任务为 §1.6 的 S3-EXIT。依据实际代码/回归 diff 和调度器 [cycle-0076-gates.log](../../.axiom-agent/logs/cycle-0076-gates.log)，本批完成已支持模型的质量来源统一、独立参考和拒绝合同。develop 仅静态检查、未执行测试是开发阶段事实；现已由调度器实际完整门禁覆盖，不再写作等待编译/测试。无 repair 报告；本次文档同步没有重建或运行测试。

| 调度器门禁 | 实际结果 |
|---|---|
| 配置 `build-agent`，`AXM_ENABLE_TESTS=ON`、`AXM_ENABLE_EXAMPLES=ON`；完整构建 `--parallel 4` | 成功，全部库/示例/测试目标完成 |
| `axiom_query_eval_test`（必需） | 通过，0.95 s |
| `axiom_ops_heal_test`（必需） | 通过，120.75 s；采用日志结果，不沿用历史 120 s 叙述作门禁结论 |
| `axiom_representation_io_test`（必需） | 通过，9.68 s |
| `axiom_boolean_workflow_test` | 通过，0.03 s；质量拒绝和 imprint 代理面继承回归 |
| `axiom_perf_baseline_test` | 通过，1.58 s；保留 Boolean/query 工作负载，改验代理结果拒绝并额外查真实 box；阈值/迭代数未变 |
| `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` | **16/16 通过，0 失败，总耗时 147.97 s** |

构建仍有 BoundaryEdge/Issue 缺失成员初始化与未使用参数告警；日志未记录独立严格告警门禁、文档检查或提交成功，不宣称无告警或已正式验收。性能 1.58 s 为 CTest 墙钟，工作负载的质量成功/拒绝合同已变化，不能直接据此声称质量积分性能提升。默认 `AXM_PERF_MAX_MS=4000`、`AXM_PERF_ITERATIONS=150` 与 30 s 测试超时保持不变。

cycle-0076 文档阶段曾完成静态检查：当时 9 个已修改 Markdown 的 68 个本地链接（含锚点）及代码围栏检查通过，`git diff --check` 通过。此结果仅记录本地文档检查，调度器文档门禁及提交成功仍须分别确认。

以下 `stage_evidence` 按用户三条验收要求逐条排列；参考详情和误差分开说明，避免接口间互比掩盖共同错误。

| 验收条目 | 测试文件 / 断言 | 独立参考结果 | 限制说明 |
|---|---|---|---|
| 1. 已支持 primitive 与 extrude/revolve/sweep/loft/thicken 的独立质量参考及采样误差 | `tests/eval/query_eval_test.cpp::topology_mass_properties_regression` 盒体解析全属性；`::stage3_mass_authority_regression` 原生球/柱/锥/环两轴方向、完整九项世界惯性；`tests/ops/ops_heal_test.cpp::test_stage3_model_mass_references` 平移/倾斜 extrude、直线 sweep、方截面 loft、正负整周/部分 revolve；既有 `test_holed_extrusions/test_polyline_sweeps/test_scaled_extrusions/test_extrusions_to_plane/test_sweep_scale_laws` 及 wedge 参考；cycle-0076 当时的 thicken_ok 断言拒绝且无值（cycle-0077 已改验真实质量） | 球 r=2：V=32π/3、A=16π；柱 r=2/h=6：V=24π、A=32π；锥 r=2/h=6：V=8π、A=4π+2π√40、自 apex 沿轴重心偏移 4.5、I_perp=V(3r²/20+3h²/80)；环 R=5/r=2：V=A=40π²。extrude/直线 sweep：V=16、A=40、局部 C=(0,0,2)、I=diag(80/3,80/3,32/3)；loft 由截面积一维积分：V=112/3、A=20+12√17、Cz=17/7、I_perp=V*508/245、I_axis=V*62/35。旋转用 Green 多边形边积分独立核对所有属性，再与光滑环扇解析积分对照 | 非采样夹具只有浮点误差；原生解析比较容差 `2e-10*max(1,abs(reference))`，新增建模比较 `2e-8*max(1,abs(reference))`。旋转采样误差界见下文，仅适用该夹具。曲线/律的独立矩形/截面积数值积分参考不构成通用误差证书。该历史批次 thicken 当时占位，拒绝参考不计真实路径；cycle-0077 真实主路径与回归见 §1.4 |
| 2. 当前拓扑/表示、编辑重新查询、回滚与多壳材料/空腔语义 | Query `topology_mass_properties_regression`：通用/专用质量、删除壳与保存点恢复、无序嵌套材料/空腔/岛、偏心空腔非零积惯量；`stage3_mass_authority_regression`：失败编辑、成功替换/PCurve 绑定/删除、保存点/整回滚/提交、代理面重新归属/删除 owner；真实 box 热缓存后换曲面/移开平面/删面/删末壳；Ops `test_stage3_model_mass_references/test_stage3_model_query_chain`：通用/体/壳、当前面面积和 owned_topo_welded/Strict、来源/缓存/Eval；`tests/rep/representation_io_test.cpp::main` 原生与 metadata/mesh/implicit 区分及空截面兼容 | 双单位立方体 V=2/A=12/C=(2,.5,.5)/I=diag(1/3,29/6,29/6)，删除一壳后 V=1/A=6/C=(.5,.5,.5)/I=diag(1/6,1/6,1/6)，保存点恢复原值。嵌套与偏心空腔由解析体差分/平行轴定理核对九项惯性；面积包括内边界，壳顺序不决定材料角色。失败查询不写事务计数、缓存或 Eval，回滚后质量与来源恢复 | 原生 sphere/cylinder/cone/torus 的兼容壳不是物理边界，仅未编辑原生记录具有解析资格，专用体/壳积分拒绝；失败编辑保留资格，成功编辑撤销、保存点/回滚恢复、提交后拒绝。metadata 往返不是实际 BRep 恢复，派生体不继承资格。相交/接触/重合多壳明确无值拒绝，未扩展全局嵌入证明 |
| 3. 未知/不支持体不伪造 bbox 质量，稳定阶段与无部分值 | Query `stage3_mass_authority_regression` 核对 status/code/stage/无 value，原生解析 r=1e70/1e-70、无效句柄与实际拓扑编辑；`topology_mass_properties_regression` 退化/曲边/非法环/壳冲突；Ops 旧 label-only extrude 与当时的 thicken、Boolean Union/Subtract/Intersect、Modified 拒绝；`tests/ops/boolean_workflow_test.cpp` imprint 代理面重组及删除 owner；`tests/perf/perf_baseline_test.cpp` 拒绝诊断/真实 box；Rep metadata/mesh 派生拒绝 | NotImplemented/AXM-CORE-E-0004/query.mass_properties.support_gate；无效/删末壳导致已删除 BodyId 为 InvalidInput/AXM-CORE-E-0001/preflight；换曲面 support_gate，错配平面 AXM-TOPO-E-0008/preflight，开壳 AXM-TOPO-E-0005/preflight；解析溢出/惯性下溢 NumericalInstability/AXM-QUERY-E-0003/numeric。全部无 value，不泄漏部分数值 | 公开查询无 bbox、Boolean/Modified 来源或 Sweep 创建缓存 fallback；代理标记随克隆/imprint 继承，重新组壳不会洗掉拒绝语义。壳/体非有限惯性或非正对角项 numeric 拒绝。empty_gate 是内部零壳防御，公共 API 不提供夹具，本批未声称执行/验收该分支 |

旋转误差夹具的子午面是半径 `[2,3]`、轴高 `[0,4]` 的矩形，起角 0.3，正负 `π/2` 与 `2π`，同时检查平移/倾斜坐标。令实际角步长 `Δθ=abs(end-start)/n`：体积和端盖面积相对误差 `1−sin(Δθ)/Δθ ≤ Δθ²/6`，弧长相对误差 ≤`Δθ²/24`，总面积相对误差 ≤`Δθ²/6`；局部平面重心差 ≤`3Δθ²`，九项局部惯性差各 ≤`V_smooth*9Δθ²`，断言另加 `1e-10` 浮点余量。返回值始终对应采样多面体；这些界针对固定尺寸夹具，不是任意旋转/曲线扫掠或联合律的尺度无关保证。

支持合同见 [API §6.1.3](../api/AxiomKernel_详细模块接口清单.md#613-stage-3-质量属性支持矩阵cycle-0076--s3-mass)。三条要求的已声明支持子集与拒绝边界均有该次实际回归证据；当时缺失的真实 thicken 已由 cycle-0077 补齐（§1.4），通用解析/曲边闭壳积分与相交多壳未扩展。任务仅在调度器完整门禁、文档检查及提交成功后记为已验收；Stage 3 / FR-QUERY-001 / FR-OPS-001 保持进行中。

### 1.4 cycle-0077 / S3-MODELING 门禁与逐项证据

历史报告记录 `stage_task_id: S3-MODELING`、`stage_outcome: ready_for_acceptance`；当前唯一任务为 cycle-0079 / S3-EXIT（§1.6）。依据实际 diff 与调度器 [cycle-0077-gates.log](../../.axiom-agent/logs/cycle-0077-gates.log)，既有 extrude/revolve/sweep/loft 主路径验收断言补齐，新增真实平面直边 Face thicken。develop 的“尚未执行”只描述开发阶段；以下采用调度器实际结果，无 repair 报告，本轮仅同步文档，没有重建或运行测试。

| 调度器门禁 | 实际结果 |
|---|---|
| 配置 `build-agent`，测试/示例开启；完整构建 `--parallel 4` | 成功，全部库/示例/测试目标完成 |
| `axiom_ops_heal_test`（必需） | 通过，126.52 s |
| `axiom_topology_test`（必需） | 通过，0.13 s |
| `axiom_query_eval_test`（关联查询合同） | 通过，0.95 s |
| `axiom_representation_io_test` / `axiom_perf_baseline_test` | 通过，9.79 s / 1.60 s |
| 全量 `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` | **16/16 通过、0 失败、总耗时 153.89 s** |

日志有 BoundaryEdge/Issue 缺失成员初始化和未使用参数告警，未记录独立 `AXM_ENABLE_STRICT_WARNINGS=ON` 配置/门禁、文档检查或提交成功。五类路径的 Strict 拓扑验证断言随 CTest 通过，不能由此声明无告警或独立 Strict warnings 门禁通过。性能时间是本次 CTest 墙钟，不作为性能提升结论。任务仅在调度器完整门禁、文档检查及提交成功后记为已验收；Stage 3 / FR-OPS-001 / FR-QUERY-001 仍进行中。

cycle-0077 文档阶段曾完成静态检查：9 个当时已修改 Markdown 的代码围栏、本批 diff 新增行中的 37 个本地链接及锚点、`git diff --check`；检查范围不含其他历史链接。未重跑构建测试、未提交/推送，未修改自动开发台账或 `.axiom-agent/`。此检查不代替调度器文档门禁或提交成功。

以下 `stage_evidence` 按三条验收要求排列，均已随上述实际 CTest 执行。

成功 thicken 的独立质量参考为矩形、L 形凹轮廓、单矩形孔，组合两种姿态与正反法向共 12 个夹具；另有源体 Face 的 provenance/回滚重试和 Topo 损坏注入组件回归。支持合同允许分离非嵌套孔，但本批不声称多孔组合或所有环绕向/姿态组合均已穷举。文档中的参考数值来自断言期望，CTest 日志只报告测试目标通过与耗时，没有逐个打印质量测量值。

| 验收条目 | 测试文件 / 断言 | 独立参考结果 | 限制说明 |
|---|---|---|---|
| 1. extrude/revolve/sweep/loft/thicken 各有非占位、非 bbox 真实拓扑主路径并通过 Strict | `tests/ops/ops_heal_test.cpp::test_stage3_model_mass_references` 显式 polygon extrude、直线 rail sweep、兼容 polygon loft、有向部分/整周 revolve；`::test_planar_face_thicken` 矩形/凹形/孔、平移倾斜、独立环绕向与正反法向、反向裁剪 segment coedge；所有结果 `validate_all(Strict)==Ok`、owned ExactBRep。`tests/topo/topology_test.cpp::main` S3-MODELING 组件损坏恢复后，thicken 及源体均 Strict Ok | 棱柱 V=16/A=40，方锥台 V=112/3；厚度 2 的 thicken 矩形 V=48/A=88、凹形 V=40/A=88、孔 V=40/A=96 | 只验收 API §8.1.1 声明子集；revolve 为采样弦面，非精确旋转曲面。thicken 只支持真实平面直边 Face 和有限正厚度；没有曲面/曲边/代理 fallback。Strict 验证指拓扑模式 |
| 2. 每条主路径覆盖公开面/边/壳查询、实际表示与质量，精确/采样明确 | Ops 上述两函数核对 faces/edges/vertices/shells、面唯一 owner、边两共边/两邻面/正长度、planar_face_area 总和、ExactBRep 和 owned_topo_welded；通用/体/壳三入口逐项比较 V/A/C 与九项惯性。thicken 检查源面/壳/体追溯、独立 Face 不虚构 owner、mesh 计数、截面面积及 nearest_boundary distance=1。`::test_stage3_model_query_chain` 保留拓扑/表示/质量/查询/缓存/provenance 链；`tests/eval/query_eval_test.cpp::stage3_section_distance_regression` 将旧 thicken 拒绝改为真实质量及最近点可用 | 矩形乘积积分 I=diag(80/3,80/3,32/3)；方截面积分 A=20+12√17、Cz=17/7、I_perp=V×508/245、I_axis=V×62/35。thicken 由有符号矩形区域积分：V=区域面积×厚度、A=2×区域面积+所有环周长×厚度，平移/旋转后的重心及九项惯性独立对照；顶点=2n、面=4n+4h−4、边=6n+6h−6。revolve 使用 Green 边积分与光滑参考，V/A 相对误差 ≤Δθ²/6、质心/惯性 O(Δθ²) | 棱柱及该共面侧壁 loft 夹具在浮点容差内精确，比较容差 2e-8×max(1,参考绝对值)，thicken 使用两侧数值尺度。一般放样按实际剖分计算；采样质量对应多面体，ExactBRep 不代表平滑解析。旋转误差仅针对 §1.3 固定夹具，无通用曲线/律误差证书 |
| 3. 边界/退化/不支持输入结构化诊断，活动事务失败不污染、回滚后重试 | Ops `::test_stage3_modeling_failures` 空/共线/外置孔 extrude、零角/跨轴 revolve、无效/无限/切向 rail 及外置孔 sweep、截面不足/重合 loft，断言无 value、码和 input_gate/materialization；`::test_planar_face_thicken` 无效 Face、零/负/Inf/NaN/不可分辨厚度、代理/曲面/偏离支撑平面，四阶段及无污染，结果曲面编辑后三入口质量/空间查询拒绝、回滚恢复。Topo `::main` 注入断链/缺共边/丢曲线/曲边/错误直线或裁剪/零法向/重用孔环，核对阶段/码/关联 Face、全部存储计数/next_id/索引/缓存/事务写数不变，恢复后 rollback/retry/Strict | extrude/revolve/sweep/loft 使用 AXM-CORE-E-0001/0002；thicken 使用 AXM-CORE-E-0001/0004 或 AXM-MOD-E-0003 与 thicken.input_gate/topology_gate/support_gate/materialization。失败不改变模型/几何数、next_id、mesh/求值/三角化缓存、Eval invalid/recompute 或活动事务写数；rollback 删除临时顶点后五类主路径可重试。曲面编辑 query.mass_properties.support_gate 拒绝旧值，回滚恢复质量/截面/最近边界/provenance | 诊断可增加，不要求诊断轨迹回滚；复用既有错误码且不放宽门禁。不扩展通用解析/曲边闭壳积分、相交多壳、全局嵌入证明或后续阶段；内部零壳查询分支未声称已验收 |

支持合同及五类矩阵见 [API §8.1.1](../api/AxiomKernel_详细模块接口清单.md#811-stage-3-五类建模主路径cycle-0077--s3-modeling)，质量/查询矩阵 §6.1.2～6.1.3 已加入真实平面 thicken，历史代理记录继续拒绝。正式状态统一见 [当前进度 §5.2.1](../plan/AxiomKernel_当前开发进度.md#521-stage-3-当前退出任务)。

### 1.5 cycle-0078 / S3-CONSISTENCY 门禁与逐项证据

`stage_task_id: S3-CONSISTENCY`；`stage_outcome: ready_for_acceptance`。依据实际代码/回归 diff 和调度器独立 [cycle-0078-gates.log](../../.axiom-agent/logs/cycle-0078-gates.log)，本批补齐基础零件建模→验证→查询→表示、来源及 Eval 一致性闭环，修复缓存身份、owned fallback、失败发布与事务恢复漂移。develop 的“未编译/未执行”仅描述开发阶段，以下采用已执行的调度器结果；无 repair 报告，本次文档同步未重建或运行测试。

| 调度器门禁 | 实际结果 |
|---|---|
| `cmake -S . -B /workspaces/axiom/build-agent -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON`；`cmake --build /workspaces/axiom/build-agent --parallel 4` | 成功，全部库/示例/测试目标完成 |
| `axiom_ops_heal_test`（必需） | 通过，137.40 s |
| `axiom_query_eval_test`（必需） | 通过，0.95 s |
| `axiom_representation_io_test`（必需） | 通过，9.44 s |
| `axiom_kernel_runtime_invariant_test`（必需） | 通过，0.02 s |
| `axiom_topology_test`（保留门禁） / `axiom_perf_baseline_test` | 通过，0.23 s / 1.74 s |
| `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` | **16/16 通过、0 失败、总耗时 164.60 s** |

日志记录一轮完整构建及全量 CTest，仍有 BoundaryEdge/Issue 缺失成员初始化和未使用参数告警，未记录独立 `AXM_ENABLE_STRICT_WARNINGS=ON` 配置/门禁、文档检查或提交成功；Strict 拓扑验证通过不代表无编译告警。测试耗时是本次 CTest 墙钟，不作为性能提升或跨环境性能保证。正式已验收仍须调度器完整门禁、文档检查及提交成功；Stage 3 / FR-OPS-001 / FR-QUERY-001 保持进行中。

下列 `stage_evidence` 按三条验收要求逐条排列，全部已随本次全量 CTest 执行。参考值来自测试断言期望，日志仅报告目标通过与耗时，不逐项打印测量值。

| 验收条目 | 测试文件 / 断言 | 独立参考结果 | 限制说明 |
|---|---|---|---|
| 1. 代表性基础零件通过建模→验证→截面/距离/质量查询→表示转换的集成闭环 | `tests/ops/ops_heal_test.cpp::test_stage3_consistency_chain`（main 已接入）：带孔 extrude、直线 sweep、平行 loft、负向四分之一 revolve、真实平面 Face thicken，逐体 Strict、Query/Topo 质量与九项惯性一致、面面积总和、独立质心/截面积/最近点/体间距离及双侧见证，full/local/shell 网格非空且索引/连通有效。`tests/rep/representation_io_test.cpp::stage3_representation_consistency_regression` 独立读取 OBJ 实际三角形积分 V/A/C，涵盖五类路径、凹形及孔；既有 `test_stage3_model_mass_references` 保留完整惯性独立参考 | 按五类顺序 V=36/24/24/4a/12，A=96/52/52/(2a+4p)/32，截面积=12/6/6/a/6；a=2.5n sin(π/(2n))，p=10n sin(π/(4n))+2，n=公开 revolve 顶点数/4−1。质心=(2,2,1.5)/(1,1.5,2)/(1,1.5,2)/(c,−c,2)/(1,1.5,−1)，c=19/(15n tan(π/(4n)))。远端盒见证=(50,50,50)，本体见证依次 (4,4,3)/(2,3,4)/(2,3,4)/(3,0,4)/(2,3,0)，体间距离独立取欧氏范数；查询外点为 (60,60,mid_z)，最近边界为上述本体见证的 (x,y,mid_z)，距离为 hypot(60−x,60−y)。OBJ 参考 V/A=12/36、24/72、10/34、24/52、12/32 及同一采样旋转公式 | Ops 比较容差 2e-8×max(1,两侧绝对值)，OBJ 积分 1e-8×max(1,参考绝对值)。支持当前平面直边多面体；revolve 为采样弦面而非解析旋转，thicken 沿支撑法向单侧正厚度；不穷举所有建模变体 |
| 2. topology/rep/provenance/eval 对同一主链结果一致，关联实体有效 | Ops 同函数：owned 壳/面、ExactBRep、面唯一 owner、有效支撑句柄，source_bodies/source_shells/source_faces 及逐面来源存在且区别于新 owned 实体；thicken 来源精确为输入 Face 和原 owner Body；最近点/距离 FaceId/ShellId 属于对应体，`nodes_of_body` 精确绑定，只读查询/转换不改变 dirty/recompute_count。Rep 同函数：相同 bbox/面积/高度的不同三角棱柱及重复几何不同 BodyId 不混用 MeshId；mesh_to_brep 重绑定后原体缓存不能返回重绑定网格。`tests/sdk/kernel_runtime_invariant_test.cpp::stage3_discarded_body_runtime_regression` 核对共享源壳派生体来源与源 owned 面/Strict/网格保留 | 两同 bbox 棱柱 OBJ 质心为 (4/3,1,1) 与 (8/3,2,1)，V=12/A=36；重复体仍有独立 MeshId。五类来源/面归属及 Eval 绑定均通过断言；查询与表示不会主动重算 Eval | Eval recompute 仅为图管理动作，不执行质量/表示算法；metadata/mesh 派生不取得物理查询或解析质量资格。旧 metadata 显示 bbox 代理保留，不属于本批 owned 主链；Rep 的 bbox 分类/距离辅助入口不等同实体精确查询 |
| 3. 修改/失败/回滚后的拓扑、表示、来源与 Eval 状态一致并有回归 | `tests/eval/query_eval_test.cpp::stage3_eval_rollback_consistency_regression`：非法修改零写入/不脏化，换面/PCurve 绑定传播 bound/downstream 而 unrelated clean；事务内 recompute 后保存点/显式/析构/取消恢复再次 dirty，面/壳/体删除恢复面集/来源/Strict/原网格和查询；成功提交等价 Plane 生成新当前网格，随后失败回滚恢复已提交支撑/网格并再次 dirty。Rep 同函数：warm/cold/full/local/shell 位移支撑拒绝且对象数/next_id/写数/mesh/两级缓存计数不增长，曲面支持拒绝、保存点/整回滚、非法 options、编辑 native box/sphere 无创建参数 fallback。SDK 同函数：保存点/整事务/取消回滚及删除提交移除派生体网格与 Eval 绑定、消费者 dirty、源体保留、mesh/cache 回基线且运行时/Eval 映射不变量成立；Ops 既有 `test_stage3_modeling_failures/test_planar_face_thicken` 保留活动事务原子性与重试 | 恢复棱柱 V=24/A=52、九项惯性等于原值，截面=6、外点最近边界=1、重叠体距离=0。位移支撑 OperationFailed/AXM-TES-E-0001/rep.tessellation.face 且关联目标 FaceId；曲面 NotImplemented/同码/rep.tessellation.support；非法 options InvalidInput/AXM-CORE-E-0002。移除体网格不可查询、绑定不再存在，源网格仍命中 | 旧网格是不可变快照，存活体可保留历史边界缓存，仅当前边界键且 source_body 正确可命中；不承诺事务 ID 回收或 Eval 自动重算。重复保存点无新增写入时不要求再次 dirty；诊断轨迹可增长。topology/assembly 防御阶段已在实现提供，本批不宣称逐分支失败注入穷举 |

公开签名与错误码常量均未新增，仅补合同注释。缓存身份、发布原子性、primitive 编辑资格与事务/Eval 语义见 [API §7.3.1](../api/AxiomKernel_详细模块接口清单.md#731-stage-3-表示来源与-eval-一致性合同cycle-0078--s3-consistency)；质量/查询矩阵及五类主路径边界保持有效。当前 S3-EXIT 任务正式状态见 [当前进度 §5.2.1](../plan/AxiomKernel_当前开发进度.md#521-stage-3-当前退出任务)。通用曲面/曲边闭壳、相交多壳、全局嵌入证明、大规模加速和后续阶段均不扩展。

### 1.6 cycle-0079 / S3-EXIT 门禁与逐项证据

`stage_task_id: S3-EXIT`；`stage_outcome: ready_for_acceptance`。依据 [cycle-0079-gates.log](../../.axiom-agent/logs/cycle-0079-gates.log) 与实际 diff：本批增加 17 行统一可执行支持矩阵、门面 smoke 闭环和公开支持边界注释，补强成功/拒绝查询的对象/几何/下一 ID 只读断言，并清理所涉 Topo/Rep 告警。没有新增公开签名、错误码或工业能力；无 repair。develop 报告 tests=[] 是开发阶段未执行，以下为调度器独立执行的实际结果；本次 docs 阶段未重新构建或运行 CTest。

| 调度器门禁 | 实际结果 |
|---|---|
| 配置 `cmake -S . -B /workspaces/axiom/build-agent -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON`，完整构建 `cmake --build /workspaces/axiom/build-agent --parallel 4` | 成功，全部库/示例/测试目标完成；复用 build-agent，非清理重建。日志未记录构建总耗时，不填估计值 |
| `axiom_ops_heal_test`（必需） | 通过，137.10 s |
| `axiom_query_eval_test`（必需，含 17 行矩阵） | 通过，1.41 s |
| `axiom_representation_io_test`（必需） | 通过，9.01 s |
| `axiom_kernel_runtime_invariant_test`（必需） | 通过，0.02 s |
| `axiom_smoke_test`（必需，新增门面闭环） | 通过，0.02 s |
| 保留 `axiom_topology_test` / `axiom_boolean_workflow_test` / `axiom_boolean_prep_test` | 通过，0.17 / 0.03 / 0.04 s |
| 保留 `axiom_perf_baseline_test` | 通过，1.71 s；未改原阈值/迭代，该耗时为 CTest 墙钟，不是新性能保证 |
| 串行 `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` | **16/16 通过、0 失败、163.78 s** |

`build-agent/CMakeCache.txt` 实际为 `AXM_ENABLE_STRICT_WARNINGS:BOOL=ON`，本批完整构建已在该配置下通过。Topo `BoundaryEdge` 值初始化后逐项赋 loop/edge/curve，Rep `emit_face_grid` 删除未用 w_axis 并同步六个调用；cycle-0078 日志的 2 条 interval/segments 遗漏初始化及 1 条 w_axis 告警在本批重编译对应文件时不再出现。cycle-0079 日志实际剩余 **1 条**：`src/axiom/sdk/kernel_plugin.cpp:122` 的 `Issue::numeric_evidence` 遗漏初始化（`-Wmissing-field-initializers`）。历史 `src/axiom/internal/sdk/kernel_plugin_helpers.cpp` 的 17 条同类告警所属编译单元未在本批日志重编译，不能宣称已消除；不把历史 18 条直接计成本次实发数量，也不宣称干净重建无告警。没有单独无告警/警告即错误门禁记录。

下列 `stage_evidence` 严格按本批三条验收要求排列，代码断言已随上述全量 CTest 执行；参考值来自测试期望，日志不逐项打印测量值。

| 验收条目 | 测试文件 / 断言 | 独立参考结果 | 限制说明 |
|---|---|---|---|
| 1. 统一矩阵映射体类、五类建模、截面/最近点/距离/质量与精确/采样/拒绝 | `tests/eval/query_eval_test.cpp::stage3_exit_support_matrix_regression`（main 已调用）17 行，逐行核对 Query/Topo 质量、section_detailed/section、closest_point/locate_point、closest_points 与正反 min_distance；支持行 Strict；拒绝无 value 且诊断含既有码及 support_gate/preflight；整体对象/几何/next_id/网格/求值/交线缓存/Eval/事务只读。`stage3_mass_authority_regression/topology_mass_properties_regression` 保留原生编辑资格、全九项质心惯性、材料空腔独立积分；Rep main 保留 metadata/mesh/implicit 拒绝 | box V=24/A=52/C=(1,1.5,2)/S=6；wedge V=12/A=26+4√13/C=(2/3,1,2)/S=3；孔 extrude 36/96/12，直线 sweep 与 loft 24/52/6，thicken 12/32/C=(1,1.5,-1)/S=6；空腔/岛 V=1000−216+8=792、A=600+216+24=840、S=100−36+4=68。revolve δ=π/(2n)，a=2.5n sinδ、p=10n sin(δ/2)+2、V=4a、A=2a+4p、C=(c,-c,2)、c=19/(15n tan(δ/2))；最近点与远盒距离来自已知角点和欧氏长度。四类原生解析体仅通用质量成功 | [统一矩阵 §6.1.4](../api/AxiomKernel_详细模块接口清单.md#614-stage-3-统一退出支持矩阵cycle-0079--s3-exit) 明列 17 行。精确限平面多面体浮点容差，采样只测实际多面体，不保证连续曲面误差；占位 Sweep/Boolean、mesh 派生均拒绝。内部 empty_gate 无公共夹具未验收；成功兼容 section 会发布 MeshId，矩阵只测其拒绝路径的只读性 |
| 2. Stage 3 五项退出标准逐项对应集成回归，保留工业限制 | SDK `tests/sdk/smoke_test.cpp::main` 缩放三角拉伸→质量→截面→最近点→距离→owned 网格→Strict；Ops `test_stage3_model_mass_references/test_planar_face_thicken/test_stage3_consistency_chain/test_stage3_modeling_failures`；Query 上述矩阵及 `stage3_section_distance_regression/stage3_eval_rollback_consistency_regression`；Rep `stage3_representation_consistency_regression`；SDK `stage3_discarded_body_runtime_regression`；详见下表五项映射 | smoke V=10.5、S=6×0.75²=3.375、最近边界/远盒距离=1、triangle_count>0、owned_topo_welded、Strict=Ok；编辑/恢复参考 V=24/A=52/S=6/最近边界=1/重叠距离=0；OBJ 三角独立积分、双侧实体见证、来源/缓存身份和运行时清理沿用 §1.5 | 不以工业需求全部满足作为 Stage 3 完成前提；通用曲面/曲边积分与实体查询、曲面 thicken、任意 loft 匹配、相交多壳/全局嵌入证明、空间加速、完整 trim/标准交换、工业 Boolean 保留。Eval 只管理图、旧网格快照、metadata 显示代理无物理资格、不承诺 ID 回收 |
| 3. 完整构建/CTest 与文档检查后同步进度/需求/Backlog，压缩所涉 strict warnings 并记录残余 | 本节记录上述完整门禁，API §6.1.2/§6.1.3/§6.1.4/§7.3.1/§8.1.1、诊断字典、调用样例、主路线图 §4.4、当前进度 §5.2.1、需求矩阵 FR-QUERY-001/FR-OPS-001 与近期 Backlog 同步。所涉告警修改由 Query/Ops 孔边界及 Rep/smoke 网格回归覆盖，保留 Topology；文档链接/标题检查使用 scripts/check_docs.py，结果见本节下方 | 完整构建成功、全量 16/16、163.78 s，strict warnings 配置 ON；所涉 Topo 2 条与 Rep 1 条未再出现，本批实发残余 SDK 1 条；历史 helpers 17 条未重编译，保留潜在残余 | 构建时长未记录；不宣称无告警干净重建。文档检查与调度器提交是独立条件，提交尚未执行，不预记 S3-EXIT 已验收或 Stage 3 已退出；FR-QUERY-001/FR-OPS-001 仍进行中，不自动启动后续阶段 |

| 主路线图 §4.4 退出项 | 集成回归及断言 | 支持边界 |
|---|---|---|
| 基础零件建模闭环跑通 | smoke main 上述门面闭环；Ops test_stage3_consistency_chain 五类 Strict→查询→full/local/shell 网格 | 真实 owned 平面边界及已声明采样体 |
| 质量属性结果正确 | Ops test_stage3_model_mass_references/test_planar_face_thicken；Query stage3_mass_authority_regression/topology_mass_properties_regression 与统一矩阵；Rep OBJ 三角独立积分 | 独立 V/A/C/全惯性参考，当前拓扑与未编辑原生解析资格分别核验，禁止来源/bbox/创建缓存恢复 |
| extrude/revolve/sweep/loft/thicken 各一条非占位主路径 | Ops test_stage3_consistency_chain/test_stage3_modeling_failures/test_planar_face_thicken，Query 矩阵五类行；Strict、owned 壳面、真实质量/截面/距离、稳定失败阶段与重试 | 显式 polygon/直线 sweep/兼容 loft/平面 Face 单侧 thicken，旋转为弦面采样；各入口变体范围沿用 API |
| 截面/最近点/距离/质量语义一致且稳定 Issue.stage | Query stage3_exit_support_matrix_regression/stage3_section_distance_regression；空截面/线点相切、凹孔/多壳、包含零距离、bbox 重叠正实体距离、可表示小间隙、预算/数值拒绝、编辑/恢复 | 共用当前物理边界；NotImplemented/support_gate 与 InvalidInput/preflight 等既有阶段，无部分结果 |
| topology/rep/provenance/eval 一致，无失败回滚/fallback 漂移 | Ops 五类有效来源/见证；Rep 缓存身份/source_body/owned 原子拒绝；Query stage3_eval_rollback_consistency_regression 保存点/析构/取消/提交后失败恢复；SDK stage3_discarded_body_runtime_regression 丢弃/永久删除清理与源体保留 | 当前边界与 owner 命中；旧网格快照、Eval 图管理、ID 回收限制保留 |

文档检查结果（本次 docs 阶段实际执行）：`python3 scripts/check_docs.py` 检查 **35 个 Markdown，0 错误、0 警告**；`git diff --check` 通过。补充静态检查覆盖本批 **8 个已修改 Markdown 的 69 个新增/更新本地链接、54 个标题锚点和代码围栏**，0 错误；统一矩阵的 **17 个行名及顺序**与 `stage3_exit_support_matrix_regression` 一致。上次文档阶段的 900 s 超时不作为验收成功证据，本次重新完成上述文档检查。未重新构建或运行 CTest，不复用历史检查值。调度器日志尚未包含文档门禁或提交成功，正式验收须调度器核验最终文档检查及提交；Stage 3 保持进行中，唯一退出任务为 S3-EXIT，不再将 S3-CONSISTENCY 列为当前待执行任务。

## 2. 测试总体原则

几何引擎测试必须遵循以下原则：

- 正确性优先于性能
- 失败可诊断优先于静默失败
- 回归稳定优先于偶然成功
- 自动化优先于人工验证
- 典型工业模型与极端边界模型并重

## 3. 测试范围

本方案覆盖以下测试对象：

- 数学基础模块
- 几何求值模块
- 拓扑模块
- 基础建模模块
- 布尔模块
- 查询分析模块
- 修复与验证模块
- 数据交换模块
- 三角化模块
- 事务与版本模块
- 诊断与日志模块

## 4. 测试层级

说明：当前仓库**已经落地**的门禁以根目录 `CMakeLists.txt` 注册的 `ctest` 为准，且多数测试通过聚合 target `axiom_kernel` 链接整个内核，偏向“模块专项 + 工作流回归”的混合形态。下文的“单元/组件/集成/回归”是**目标分层**，其中更细粒度、仅链接子模块的测试 target 仍需后续逐步补齐。

## 4.1 单元测试

目标：

- 验证单个类或单个函数的正确性

覆盖对象：

- 向量矩阵运算
- 曲线曲面求值
- 基本拓扑关系
- 错误码与结果对象
- 基础格式解析器

要求：

- 每个核心模块最终都应具备独立单元测试；在当前阶段，至少要有明确的主回归入口（例如 `tests/<模块>/` 或对应 workflow 测试）
- 单元测试不依赖大型外部数据文件

## 4.2 组件测试

目标：

- 验证单个模块内部多个类协同工作的正确性

覆盖对象：

- `CurveFactory + CurveService`
- `SurfaceFactory + SurfaceService`
- `TopologyTransaction + TopologyValidationService`
- `PrimitiveService`
- `STEP` 导入器

要求：

- 组件级接口最终应能在隔离环境运行；在当前阶段，允许先通过聚合 target 验证组件语义，再逐步拆分更细粒度 target
- 每个组件测试都必须定义输入、执行、输出和诊断预期

## 4.3 集成测试

目标：

- 验证多个模块联动时的稳定性

覆盖对象：

- 几何创建到拓扑构造
- 构造体到布尔
- 导入到修复到导出
- 建模到三角化到查询分析

要求：

- 集成测试必须使用真实模型或接近真实模型
- 必须覆盖成功路径和失败路径

## 4.4 回归测试

目标：

- 保证新版本不破坏旧能力

覆盖对象：

- 历史 bug
- 黄金模型输出
- 已知脏模型修复结果
- 典型布尔失败案例

要求：

- 所有修复过的缺陷必须形成回归测试
- 每次版本发布前必须全量执行

## 4.5 属性测试

目标：

- 验证几何与拓扑规则在一批输入上满足不变量

典型不变量：

- 封闭实体体积应非负
- 刚体变换前后拓扑等价
- 纯旋转不改变体积和面积
- 同一输入多次运行结果稳定
- 布尔结果应通过有效性验证

## 4.6 Fuzz测试

目标：

- 用随机或半随机输入寻找鲁棒性缺陷

重点对象：

- 近共面
- 近切触
- 极小边
- 极小角
- 薄壁
- 高曲率样条面
- 导入的脏拓扑

要求：

- 记录随机种子
- 失败时自动保存复现输入

## 4.7 性能测试

目标：

- 建立性能基线
- 跟踪性能回退

覆盖对象：

- 几何求值
- 体构造
- 布尔
- 导入导出
- 三角化
- 质量属性计算

## 4.8 互操作测试

目标：

- 验证与外部格式和外部系统的兼容能力

覆盖对象：

- `STEP`
- `IGES`
- 原生 `BREP`
- `STL`

要求：

- 导入后可验证
- 导出后可被参考工具读取
- 结构丢失需可记录

## 5. 测试数据集设计

## 5.1 数据集分类

建议维护以下数据集：

- `unit-fixtures`
- `primitive-models`
- `mechanical-parts`
- `surface-parts`
- `dirty-models`
- `boolean-stress-cases`
- `interop-cases`
- `performance-cases`

## 5.2 基础模型集

应包含：

- 盒体
- 球体
- 圆柱体
- 圆锥体
- 环体
- 简单拉伸体
- 简单旋转体

用途：

- 验证基础构造
- 验证几何求值
- 验证体积、面积、重心

## 5.3 机械零件集

应包含：

- 通孔块体
- 带台阶零件
- 薄壁零件
- 多孔板件
- 倒角和圆角零件

用途：

- 验证布尔
- 验证查询分析
- 验证三角化

## 5.4 曲面件集

应包含：

- 样条曲面拼接件
- 修剪面组合件
- 高曲率件
- 近切触曲面件

用途：

- 验证曲面求值
- 验证曲面求交
- 验证复杂布尔

## 5.5 脏模型集

应包含：

- 小边模型
- 小面模型
- 缝隙模型
- 法向错误模型
- 参数域异常模型
- 非法拓扑模型

用途：

- 验证修复器
- 验证验证器
- 验证导入后处理

## 5.6 性能基线集

应包含：

- 小模型
- 中等复杂模型
- 大型实体模型
- 大曲面模型
- 布尔压力模型

用途：

- 跟踪性能趋势
- 比较优化效果

## 6. 详细测试项

## 6.1 数学基础测试

测试内容：

- 向量加减乘除
- 点积、叉积
- 归一化
- 变换前后坐标一致性
- 包围盒计算
- 边界数值稳定性

验收标准：

- 结果与理论值一致
- 数值误差在允许范围内

## 6.2 曲线求值测试

测试内容：

- 曲线端点求值
- 中间参数点求值
- 一阶、二阶导数
- 最近点
- 参数域边界处理

验收标准：

- 求值连续稳定
- 导数方向合理
- 越界参数有明确行为定义

## 6.3 曲面求值测试

测试内容：

- 法向求值
- 曲率求值
- 最近点和参数反求
- 修剪域内外行为

验收标准：

- 法向方向一致
- 曲率结果与参考值接近

## 6.4 拓扑测试

测试内容：

- 边连接合法性
- 环闭合性
- 面与环关系
- 壳封闭性
- 实体合法性

验收标准：

- 合法模型通过验证
- 非法模型给出明确错误

## 6.5 基础建模测试

测试内容：

- 基础体构造
- 拉伸
- 旋转
- 扫描
- 放样基础场景

验收标准：

- 输出体有效
- 输出质量属性合理
- 输出可继续参与后续布尔

## 6.6 布尔测试

测试内容：

- 并集
- 差集
- 交集
- 共面布尔
- 近接触布尔
- 薄壁布尔
- 曲面件布尔

验收标准：

- 输出体通过验证器
- 失败场景可归类
- 重复运行结果稳定

## 6.7 查询分析测试

测试内容：

- 最近点
- 截面
- 点分类
- 体积、面积、重心
- 惯性矩

验收标准：

- 与参考解差异在阈值内
- 批量调用结果稳定

## 6.8 修复与验证测试

测试内容：

- 小边清理
- 小面清理
- 缝合
- 法向修复
- 自动修复前后对比

验收标准：

- 修复动作可追踪
- 修复后模型更接近有效状态
- 不允许修复后引入静默破坏

## 6.9 数据交换测试

测试内容：

- `STEP` 导入
- `STEP` 导出
- 往返一致性
- 非法文件处理
- 容差信息处理

验收标准：

- 标准数据可导入
- 导出结果可被参考工具读取
- 往返误差和信息丢失可记录

## 6.10 三角化测试

测试内容：

- 规则体三角化
- 曲面件三角化
- 精度参数变化
- 局部修改后的局部重三角化

验收标准：

- 网格可用于渲染
- 法向和面索引一致
- 局部修改后不强制全量重算

## 6.11 事务与版本测试

测试内容：

- 提交
- 回滚
- 失败回滚
- 版本差异
- 多次连续修改

验收标准：

- 失败不污染原版本
- 撤销重做语义清晰

## 7. 自动化策略

## 7.1 CI流水线

建议至少分三层：

1. 快速单元测试
2. 标准集成与回归测试
3. 夜间压力、Fuzz 与性能测试

## 7.2 提交门禁

每次合并前至少要求：

- 编译通过
- 单元测试通过
- 关键组件测试通过
- 新增代码覆盖到对应测试

## 7.3 夜间任务

夜间任务应执行：

- 全量回归
- 大模型数据交换
- 布尔压力集
- Fuzz 测试
- 性能对比

## 7.4 失败归档

所有自动化失败必须归档：

- 输入数据
- 日志
- 诊断报告
- 随机种子
- 运行版本

## 8. 质量指标

## 8.1 正确性指标

- 单元测试通过率 `100%`
- `P0` 集成测试通过率 `100%`
- 回归测试通过率 `100%`

## 8.2 稳定性指标

- 同一输入多次运行结果一致
- 不允许随机崩溃
- 不允许静默损坏模型

## 8.3 诊断性指标

- 核心操作失败必须附带错误码
- 布尔、修复、导入失败必须有诊断报告
- 修复动作必须可追踪

## 8.4 性能指标

第一阶段不以绝对性能冠军为目标，但必须具备：

- 清晰性能基线
- 优化前后可测量
- 不出现明显性能回退

## 9. 验收流程

## 9.1 模块验收

每个模块完成后执行：

- 接口检查
- 单元测试
- 组件测试
- 文档检查

## 9.2 阶段验收

每个阶段结束执行：

- 功能演示
- 阶段测试报告
- 风险清单更新
- 已知问题归档

## 9.3 MVP验收

`MVP` 验收应覆盖：

- 基础构造
- 布尔
- 查询分析
- 验证与修复
- `STEP` 导入导出
- 三角化
- 事务与版本
- 诊断输出

## 10. MVP验收标准

若同时满足以下条件，则 `MVP` 可验收：

- `P0` 功能全部实现
- 核心标准模型集全部通过
- 导入导出互操作可用
- 布尔在标准零件集上稳定
- 失败场景可诊断
- 文档完整
- 已知问题明确

## 11. 发布门禁

发布前必须满足：

- 无阻断级缺陷
- 无数据损坏类未关闭缺陷
- 回归测试全部通过
- 性能无显著回退
- 示例工程通过

## 12. 缺陷分级建议

### `S0`

- 崩溃
- 数据损坏
- 输出静默错误

### `S1`

- 核心功能不可用
- 布尔大面积失败
- 互操作主链路中断

### `S2`

- 局部功能异常
- 诊断信息不完整
- 局部性能明显回退

### `S3`

- 文案问题
- 低影响兼容问题
- 非关键性能小回退

## 13. 测试组织建议

建议测试工作由三部分组成：

- 开发自测
- QA 自动化测试
- 失败案例库维护

要求：

- 几何算法工程师必须为新能力补单元测试
- QA 必须维护回归集和自动化流水线
- 所有线上或集成失败案例都必须回流到案例库

## 14. 测试输出物

每阶段应输出：

- 测试计划
- 测试用例清单
- 测试报告
- 回归对比报告
- 缺陷列表
- 失败案例归档
- 性能趋势图

## 15. 结论

几何引擎的测试不能只看“能不能跑出结果”，更要看：

- 结果是否正确
- 模型是否有效
- 失败是否可解释
- 回归是否稳定
- 性能是否可跟踪

只有把这五件事同时做稳，`AxiomKernel` 才能从一个“能演示的算法项目”变成一个“能交付的工业几何内核”。
