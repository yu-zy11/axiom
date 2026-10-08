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

验收边界：HEAL trim 仍限 Plane/Cylinder/Sphere，验证/修复规则范围不扩大；HEAL 回滚不恢复 `next_id`；cycle-0084 起 repair_many_auto 复制失败子项 issue 并保留真实子阶段，其他批量入口仍在原诊断保留根因。IO 标准 STEP/IGES 实体、普通文本/目录辅助接口与更广泛失败注入语料未因此闭合。cycle-0084 起未解决的后验验证/自动修复失败返回非 Ok、无 value 并回滚本次导入，批量模型回滚由子项实际失败触发，批量导出不保证文件事务。扭转方向必须法向、中心共面、正距离、扭角最多一周，角站差不超过 7.5°；真实结果仍为保守采样多面体，不是解析螺旋面，未组合变比例/至平面拉伸。FR-OPS-001 继续进行中，FR-DIAG-001/NFR-DIA-001 继续受限可用。

### 1.2 cycle-0075 / S3-QUERY 门禁与逐项证据

历史 develop/repair 报告记录 `stage_task_id: S3-QUERY`、`stage_outcome: ready_for_acceptance`；当前唯一任务为 cycle-0090 / S6-DIRECT-EDIT（§1.17），本节保留历史证据。该批关闭已声明真实多面体的截面、最近边界和实体距离主链，未扩展后续阶段。依据 [cycle-0075-gates.log](../../.axiom-agent/logs/cycle-0075-gates.log) 与实际 diff：调度器两轮独立配置/完整构建（测试与示例开启、并发 4）；首次 CTest **14/16 通过、2 失败、141.18 s**，Ops 在有向区间旋转验证失败，Query 在实际截面/距离回归失败。repair 翻转负向旋转侧壁并在分配前验证共享边双边反向，修正非等边楔体斜面法向 `(dy,dx,0)`，修正把拒绝创建空体当有效空体的夹具。

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

历史报告记录 `stage_task_id: S3-MODELING`、`stage_outcome: ready_for_acceptance`；当前唯一任务为 cycle-0090 / S6-DIRECT-EDIT（§1.17）。依据实际 diff 与调度器 [cycle-0077-gates.log](../../.axiom-agent/logs/cycle-0077-gates.log)，既有 extrude/revolve/sweep/loft 主路径验收断言补齐，新增真实平面直边 Face thicken。develop 的“尚未执行”只描述开发阶段；以下采用调度器实际结果，无 repair 报告，本轮仅同步文档，没有重建或运行测试。

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

支持合同及五类矩阵见 [API §8.1.1](../api/AxiomKernel_详细模块接口清单.md#811-stage-3-五类建模主路径cycle-0077--s3-modeling)，质量/查询矩阵 §6.1.2～6.1.3 已加入真实平面 thicken，历史代理记录继续拒绝。正式状态统一见 [当前进度 §5.2.1](../plan/AxiomKernel_当前开发进度.md#521-stage-6-当前退出任务)。

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

公开签名与错误码常量均未新增，仅补合同注释。缓存身份、发布原子性、primitive 编辑资格与事务/Eval 语义见 [API §7.3.1](../api/AxiomKernel_详细模块接口清单.md#731-stage-3-表示来源与-eval-一致性合同cycle-0078--s3-consistency)；质量/查询矩阵及五类主路径边界保持有效。当前S6-DIRECT-EDIT任务正式状态见 [当前进度 §5.2.1](../plan/AxiomKernel_当前开发进度.md#521-stage-6-当前退出任务)。通用曲面/曲边闭壳、相交多壳、全局嵌入证明、大规模加速和后续阶段均不扩展。

### 1.6 cycle-0079 / S3-EXIT 门禁与逐项证据

本节为 Stage 3 历史批次记录，保留当时的门禁及验收条件；当前主线和唯一任务为 Stage 6 / S6-DIRECT-EDIT（§1.17），不由此追认历史提交成功。

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

文档检查结果（本次 docs 阶段实际执行）：`python3 scripts/check_docs.py` 检查 **35 个 Markdown，0 错误、0 警告**；`git diff --check` 通过。补充静态检查覆盖本批 **8 个已修改 Markdown 的 69 个新增/更新本地链接、54 个标题锚点和代码围栏**，0 错误；统一矩阵的 **17 个行名及顺序**与 `stage3_exit_support_matrix_regression` 一致。上次文档阶段的 900 s 超时不作为验收成功证据，本次重新完成上述文档检查。未重新构建或运行 CTest，不复用历史检查值。调度器日志尚未包含文档门禁或提交成功，正式验收须调度器核验最终文档检查及提交；当时 Stage 3 保持进行中，退出任务为 S3-EXIT；当前主线及唯一退出任务见 §1.7，不追认历史提交或验收。

### 1.7 cycle-0080 / S4-INTERSECTION 门禁与逐项证据

`stage_task_id=S4-INTERSECTION`，`stage_outcome=ready_for_acceptance`，需求 FR-BOOL-001（进行中），模块 Ops；阶段主线为 [主路线图 §4.5 Stage 4](../plan/AxiomKernel_主开发计划与阶段路线图.md#45-stage-4-布尔与验证器第一代)。这是 cycle-0080 历史退出任务；当前唯一任务为 cycle-0090 / S6-DIRECT-EDIT（§1.17）。本历史包冻结第一代平面直边闭壳的候选/求交准备范围，不认证完整布尔实体闭环。

实际依据为 [cycle-0080-gates.log](../../.axiom-agent/logs/cycle-0080-gates.log)、本批实际 diff 与 develop/repair 报告。develop 的 tests=[]/“未执行”已由调度器最终门禁取代；repair 的两个定向回归通过不能代替全量门禁。本次 docs 阶段只读取这些结果，不重新构建或运行测试。

| 调度器门禁 | 实际结果 |
|---|---|
| 两轮配置 `cmake -S . -B /workspaces/axiom/build-agent -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON`，两轮完整构建 `cmake --build /workspaces/axiom/build-agent --parallel 4` | 均成功；复用 build-agent，非清理重建；构建总耗时未记录 |
| 首轮串行全量 CTest | **15/16 通过，1 失败，185.75 s**；axiom_boolean_prep_test 在 planar preparation reference/isolation 回归失败（0.05 s）；Boolean workflow 通过（0.05 s）。不能将该轮记为通过 |
| repair 后最终 `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` | **16/16 通过，0 失败，172.43 s** |
| `axiom_boolean_prep_test`（必需） | 最终通过，**0.17 s**；包含下列支持/参考/拒绝/隔离断言 |
| `axiom_boolean_workflow_test`（必需） | 最终通过，**0.12 s**；包含真实裁剪及读取失败传播/隔离 |
| 保留 Ops/Heal、Query/Eval、Topology、Rep/IO、Runtime、Diagnostics、smoke | 最终通过，144.54 / 1.54 / 0.13 / 9.46 / 0.02 / 0.12 / 0.02 s |
| 保留 IO workflow、IO dataset、Heal、Geometry、Math、Plugin、性能 | 最终通过，13.32 / 0.64 / 0.08 / 0.53 / 0.00 / 0.02 / 1.72 s；CTest 墙钟不作为新性能保证，阈值/迭代未修改 |

首轮构建实发 **18 条** `Issue::numeric_evidence` 初始化告警：SDK kernel_plugin.cpp 1 条、helpers 17 条；最终增量构建未重编译这些单元、未实发新告警，不代表告警清零。日志未记录独立无告警/警告即错误门禁、文档门禁或提交成功。

repair 不重复计为新功能包：原 box(5e-7) 会物化为真实 1.5e-6 边，改用显式闭壳（先 1e-9 建模、恢复声明 1e-6）并由公共 edge_length 确认真正 5e-7 内环边，保留退化拒绝预期。同时修复兼容 run 读取失败被 continue 静默吞掉的已复现路径，先完成所有局部读取/裁剪再物化，不提前创建结果体或失效 Eval；新增源边独立插值、输入摘要及 Eval/事务隔离回归。独立 Agent 路径与开发期运行细节只见报告，不把它们当全量门禁证据。

下列 `stage_evidence` 为按三条验收要求排列的证据数组；每项含测试文件/断言、独立参考及限制。所有列出的回归入口已由 main 调用并随最终全量 CTest 执行；参考结果是测试期望，日志没有逐项打印测量值。

| 验收条目 | 测试文件 / 断言 | 独立参考结果 | 限制说明 |
|---|---|---|---|
| 1. 支持体类、容差、精确/采样边界、固定参考模型及结构化拒绝 | `tests/ops/boolean_prep_test.cpp::check_planar_intersection_references` 固定重叠盒、RxRz 非轴向盒、斜楔、凹 U、挤出孔洞、make_reference_holed_prism 显式内环、分离/包含/横向相切；`check_planar_preparation_failure_isolation` 拒绝 sphere/cylinder 代理、开壳、共面面接触/相同体、ExactCritical、无效容差及真实短边。公开合同 `include/axiom/ops/ops_services.h::prepare_intersections` 与 core/types.h::BooleanIntersectionOptions 同步 [支持矩阵/固定参考集](../api/AxiomKernel_详细模块接口清单.md#821-stage-4-第一代求交准备支持矩阵cycle-0080--s4-intersection) | 盒六条单位交边及 R 转置；楔体 x+y≤2、斜交段总长 √2、半空间外盒零交；凹/孔带内 16 条解析区间；分离/包含边界空交；相切位置 x=.75/y=1.25/z∈[.25,.75]。默认线/角容差 1e-6、局部界 1e-9..1e-3；真实 5e-7 边小于声明容差，由 edge_length 先确认 | 限真实嵌入、闭合一致定向的平面直边 ExactBRep；FastFloat/AdaptiveCertified 是带残差检查的 double 解析计算，无采样 fallback/精确谓词认证。共面候选保守拒绝；局部检查不证明全局壳嵌入或多壳材料层级。支持预算最多 2000000 面比较/100000 交段/256 边每面，不声明通用工业曲面成功率或 FR 全部完成 |
| 2. 真实 face/edge 候选、切分关联与无 bbox 伪交/漏交的独立参考 | prep `check_planar_intersection_references`：普通/交换盒 6 候选/6 段，公共 loops_of_face/edges_of_loop 核对完整真实边集合，端点面/边归属与 fraction∈[0,1]；六参考边及逆旋转区间完整覆盖/无重复长度；凹 U 与两种孔洞逐条匹配全部 16 区间，显式内环盖面两个截面各 2 段。源 hit 用创建时 VertexId/Point3 表、公共 vertices_of_edge 独立插值，误差≤1e-8，内环 hit 非零。`tests/ops/boolean_workflow_test.cpp::check_geometric_preparation_workflow` 验证真实入库 1 集合/6 段、伪交 0 集合/0 段及 bool.intersect.trim | 凹/孔四条 z=0 cap 区间、四条 z=.5 side 区间、八条 z∈[0,.5] 竖向区间，共 16 条，逐参考长度与覆盖无缺口/重复；来源插值 v0+fraction×(v1−v0) 独立于 coedge 方向；斜楔与 x+y>2 盒候选非空但零交段，真实斜交段 √2 | `src/axiom/internal/ops/ops_internal_b.inc::prepare_planar_boolean_intersections/trim_boolean_planar_line` 读取真实外/内环及连续 coedge，bbox 仅筛候选；结果为坐标/源 face/edge/边参数及点接触，可供后续切分。完整性仅证明固定平面参考集；当前兼容 run 输出及 Strict 通过不能证明重建实体物理正确性 |
| 3. 退化/数值/预算失败的稳定码、diagnostic_id、阶段与输入/活动事务隔离 | prep `check_planar_preparation_failure_isolation` 覆盖无效句柄、曲面/代理、共面、真实短边、1e12 坐标、角容差内近平行、角容差外远交线（交换输入）、开壳、面比较/每面边/交段预算及非法容差；断言 E-0001/E-0011..0014、无部分 value、非零可查 diagnostic_id、bool.prep.candidates 或 bool.intersect、输入上下文和有限 numeric_evidence，find_by_issue_stage 与 JSON 可检索。失败及成功重试比对六类 store 计数、壳/面/曲面/环/边/顶点关联及逐面 bbox 摘要、5 项 Eval bridge 计数、writer/哨兵/写次数，继续写入后 rollback、Strict 原输入。workflow `check_trim_failure_isolation` 对真实短边、257 边与 1e12 失稳，在无 writer/有哨兵 writer 两种状态断言 E-0001/E-0012/E-0013、bool.intersect.trim、diagnostic_id、无模型/Eval/输入摘要变化，writer 可继续写入及 rollback | 短边 5e-7 < 1e-6；1e12 坐标舍入尺度超容差；近平行角 5e-7 < 默认 1e-6；微扭转角 1e-8 > 声明 1e-10，但约 1e8 远交线舍入尺度超 1e-6，裁剪前即拒绝；预算 1 小于盒的 36 面比较/6 交段/4 边每面。兼容 run 已实际复现短边错误 Ok 且存储增加，修复后结构化拒绝与零污染通过 | 允许增加诊断记录；输入坐标摘要为公开逐面极值，不是逐顶点坐标全序列。只读隔离认证针对 prepare；兼容 run 的失败夹具为 Generic 单面壳，不属于 prepare 闭壳支持认证，也不认证 run 全部后续重建/修复事务行为 |

准备包全部三条验收要求已有最终通过证据，过时“测试尚未执行/等待完整编译”的表述在本批状态中取消。FR-BOOL-001 与 Stage 4 仍进行中：cycle-0081 的有限平面只读切分/分类见 §1.8；实体重建/二维共面在该历史准备包未交付，cycle-0082受限重建见§1.9；精确谓词认证、连续曲面/曲边求交及全局壳嵌入证明仍属后续工作，不在本包扩展或据行数继续加功能。主路线图“工业模型稳定布尔/失败可定位”的整阶段标准不由准备包单独达成。

文档同步范围：API §8.2/§8.2.1、调用样例 §7.5、错误码字典 §7.5 与用户文案、需求矩阵、当前进度 §5.2.1、Backlog 与主路线图 §1/§4.5/§7；历史 S3-EXIT 的当前任务表述和失效锚点已修正，历史证据保留。调度器最终文档检查和提交成功才记录任务已验收。本轮不提交/推送，不写自动开发台账或 .axiom-agent/，不另写 result.json。

本次 docs 阶段只读核对已全部结束：`/root/api_review` 确认公开签名/类型、支持矩阵与固定独立参考一致，并建议澄清样例未单独编译和全局容差显式复制；`/root/diagnostic_review` 确认 E-0011..0014、候选/求交/run 裁剪阶段及只读隔离范围，保留 run 后续行为未认证限制；`/root/evidence_review` 确认两轮真实门禁和三条断言证据，指出需求矩阵/历史任务口径需同步。主 Agent 整合并独占文档写入，三个子 Agent 均未修改文件、构建测试、提交或派生 Agent。

本次实际静态文档检查：`python3 scripts/check_docs.py` 检查 **35 个 Markdown，0 错误、0 警告**；补充检查本批 **9 个已修改 Markdown 的 163 个本地链接、112 个标题锚点及代码围栏，0 错误**；`git diff --check` 通过。这些是本轮文档检查，不是重新编译/运行测试，也不补记为调度器日志中的文档门禁或提交成功。

### 1.8 cycle-0081 / S4-SPLIT-CLASSIFY 门禁与逐项证据

`stage_task_id=S4-SPLIT-CLASSIFY`，`stage_outcome=ready_for_acceptance`；Stage 4 历史只读准备任务（当前任务见§1.14），需求 FR-BOOL-001 / 模块 Ops，需求与阶段均保持进行中。目标为真实交线切分与实体内外分类，保留 face/edge 来源及共面/相切语义；阶段任务优先于需求权重、历史 remaining 和新增变体。本包不扩展布尔实体重建、二维共面区域或曲面/曲边。

依据 [cycle-0081-gates.log](../../.axiom-agent/logs/cycle-0081-gates.log)、实际 diff（含新增 boolean_split_classify.cpp）及 develop/repair 报告：开发阶段“未运行”已由最终门禁结果取代，repair 定向通过与调度器最终全量结果分别记录。本轮仅同步 docs Markdown，没有构建或运行测试。

| 门禁 / 回归 | 真实结果 |
|---|---|
| 首轮配置（测试/示例开启），`cmake --build /workspaces/axiom/build-agent --parallel 4` | 配置成功；完整构建因 Math 的 valid_tolerance_policy 声明缺失失败，该轮没有 CTest |
| repair 定向验证（报告记录，不在本日志中逐轮打印） | 全目标构建成功；定向构建5次（4成功、1定位日志编译失败）；必需三项回归5次（前4次定位失败、最后 **3/3、0失败、0.83 s**，prep/workflow/topology **0.79/0.82/0.45 s**） |
| 调度器第二轮配置及并发4完整构建 | 成功；复用 build-agent，未清空缓存；构建总耗时未记录 |
| 最终串行 `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` | **16/16 通过、0 失败、173.96 s** |
| 必需 axiom_boolean_prep_test / axiom_boolean_workflow_test / axiom_topology_test | 最终各通过，**0.45 / 0.61 / 0.18 s** |
| 保留 Ops/Heal、Query/Eval、Rep/IO、Runtime、Diagnostics、smoke | 最终各通过，**142.77 / 1.90 / 10.06 / 0.09 / 0.16 / 0.04 s** |
| 保留 IO workflow、IO dataset、Heal、Geometry、Math、Plugin、性能 | 最终各通过，**13.90 / 0.67 / 0.09 / 0.56 / 0.07 / 0.12 / 2.26 s**；阈值/迭代不变，CTest 墙钟不是跨环境性能保证 |

本批日志未打印编译告警，不能据增量构建宣称全仓告警清零；未记录独立无告警/警告即错误、文档门禁或提交成功。repair 修正缺失 Math 声明头、旋转/凹U参考 label、未绑定 PCurve 快照误要求、相切参考遗漏有限交段内部顶点，以及 rollback 重建后无序关联集合次序；定位日志输出编译错误也已修复。未删去或放宽来源、面积、分类、邻接、交段完整覆盖、预算、诊断和隔离断言；仅无序 incident coedges / owners face 集合规范排序，保留重复项与有序环。实测触碰点 (3.625,2.5,0) 位于段 (3.375,2.5,0)→(4,2.5,0) 内，独立参考修正后仍保留完整切分断言。旧三项审查问题仅核查已有修复，不声称本轮另行 runtime 复现。

下表为按用户三条验收要求顺序排列的 `stage_evidence` 数组；包含测试文件/断言、独立参考及限制。两个新增回归函数均由 main 调用并随最终全量 CTest 执行；具体参考数值是断言期望，最终日志未逐项打印测量值。

| 验收条目 | 测试文件 / 断言 | 独立参考结果 | 限制说明 |
|---|---|---|---|
| 1. 交线真实切分受支持面与边，裁剪环/方向/邻接一致，覆盖非轴对齐、凹/孔模型，不用 bbox 替代 | `tests/ops/boolean_prep_test.cpp::check_split_classification_references`：逐 source face 面积守恒与绕向、唯一反向整边/对称邻接、两 source face 有限交段边参数并集 [0,1]、source edge 参数无重叠/遗漏、交段端点成为 knot；显式孔端点表独立核对面片源边与 edge_fragment 的 v0+fraction×(v1−v0)。实现 `src/axiom/ops/boolean_split_classify.cpp::prepare_split_classification` 从实际外/孔环三角化，按有限交段支撑线/端点横截切分，同步相邻源边切点，验证覆盖与邻接 | 轴向及两次刚性旋转/逆映射盒总面积各 **24**、内部面积各 **3**；凹U/显式孔棱柱总面积 **64/72**、内部面积各 **4**，cutter 总面积 **26**；交段连续覆盖与原边参数完整性独立检查 | 限固定嵌入平面直边 ExactBRep 闭壳与 double 舍入尺度；单环绕向须符合支撑法线门禁，允许额外三角细分；输出为只读分片，不认证实体重建/全局壳嵌入。prep 与 topology 最终通过 **0.45/0.18 s** |
| 2. 分类用实体几何边界，覆盖内外、共面、相切、包含与小间隙；不确定性明确拒绝 | 同一 prep 回归用独立箱体/U/孔材料公式与逆旋转，检查每分片中心及朝各顶点内部样本、严格包含、正间隙、真实面/角点 Boundary 与 FaceId；`tests/ops/boolean_workflow_test.cpp::check_split_classification_isolation` 检查非共面 wedge/box 相切、point_contact 真实顶点及有限竖交段两源面完整覆盖、所有开放面片 Outside、bbox 内而真实材料外点 Outside，共面 E-0014/bool.intersect、近边界 E-0013/bool.classify | 严格包含无交段，外体面积 **24**、内部小体面积 **1.5**；**1e-4** 正间隙可解析；**5e-7** 近边界点明确 NumericalInstability；面接触/self 共面明确 NotImplemented。实现对实际 trimmed boundary 使用至少两条有效且一致的射线，舍弃穿边/相切/不可分辨方向 | 共面二维区域保守拒绝，不提供精确谓词或连续曲面认证；多壳奇偶材料约定，壳自身/壳间无交为前置，未证明全局嵌入。Boundary 是舍入尺度可解析真实边界，不是整个容差带；prep/workflow 最终通过 **0.45/0.61 s** |
| 3. split/classify 各有稳定阶段，失败/回滚不污染拓扑、表示、来源及 Eval | workflow `check_split_classification_isolation` 在无/活动writer下检验片数预算 bool.split、输入读取预算 bool.prep.candidates、共面 bool.intersect、近边界/NaN/无效BodyId/曲面代理/点数预算 bool.classify；检查稳定码、非零 diagnostic_id、无部分 value、Error severity、实体、有限 numeric_evidence、阶段/代码检索与JSON。成功 split/classify/相切及失败都比对输入环序/边v0-v1/来源、表示kind、face/body bbox、真实面积/边长、incident PCurveId（合法0）及完整绑定UV、模型计数、Eval bridge五指标、缓存六指标、writer状态/写次数/sentinel；继续写入并rollback后核对原状态和Strict。旧 prep::check_planar_preparation_failure_isolation / workflow::check_trim_failure_isolation 同次保留 | 新入口无模型分配或拓扑/表示/来源/Eval/cache/事务写入，局部结果全部验证后返回，失败仅新增诊断；bool.split E-0004 为切分不一致，预算E-0012，分类数值拒绝E-0013；先行阶段原样传播。最终必需三项与完整16项均通过 | 快照为公开摘要，不是逐顶点直接坐标或全部几何状态编码；部分PCurve绑定面未获固定快照认证。兼容run后续物化/重建/修复事务不获全链路认证；新增常量复用既有E-0004编号，不新增D码，独立分类不使用旧E-0005 |

E-0004 各生产拒绝分支已静态核对；本批 workflow 的切分失败注入是预算 E-0012/bool.split，并未单独触发全部 E-0004 根因。诊断码合同与运行覆盖不混为同一结论。

全部三条要求已有最终运行证据，取消本包“测试尚未执行/等待完整构建”的过时状态。`ready_for_acceptance` 仍不表示任务已验收；调度器完成最终文档门禁及提交成功后才记已验收，本轮不提交或推送，不写自动开发进度台账、.axiom-agent/ 或 result.json。Stage 4 / FR-BOOL-001 保持进行中：仅认证固定支持域内只读切分/分类准备，兼容 run 仍含 bbox 实体语义；当时实体重建与二维共面未交付；cycle-0082受限闭环见§1.9，精确谓词、曲面/曲边与全局壳嵌入仍未认证。整阶段工业模型布尔稳定闭环不由本包单独达成。

文档同步：API §8.2/§8.2.2、样例 §7.6、错误码字典 §7.5/用户文案 §5.4、需求矩阵、当前进度 §5.2.1、Backlog 与主路线图；cycle-0080 / S4-INTERSECTION 与 Stage 3 证据作为历史保留，不推断历史提交成功。

本轮三个只读子 Agent 已全部结束：`/root/api_review` 核对两个公开签名、六个新类型、来源/整边邻接/预算及样例边界；`/root/diagnostic_review` 核对 E-0004 常量与 E-0011..0014 扩展语义、bool.split/bool.classify 及失败原子性摘要限制；`/root/evidence_review` 核对最终日志、三条 main 已调用断言及阶段/需求状态。三个 Agent 均未写文件、构建测试、写 result.json、提交/推送或派生 Agent；主 Agent 为唯一文档写者。

本轮实际静态文档检查：`python3 scripts/check_docs.py` 检查 **35 个 Markdown，0 错误、0 警告**；补充检查 **9 个改动 Markdown、178 个本地链接、125 个标题锚点及代码围栏，0 错误**；`git diff --check` 通过。这是本轮文档检查，不是重建/运行测试，不补记为调度器日志中的最终文档门禁或提交成功。

### 1.9 cycle-0082 / S4-REBUILD 门禁与逐项证据

`stage_task_id=S4-REBUILD`，`stage_outcome=ready_for_acceptance`；Stage 4 历史重建任务，当前唯一退出任务为 cycle-0090 / S6-DIRECT-EDIT（§1.17），需求FR-BOOL-001 / 模块Ops，阶段与需求保持进行中。目标为受支持模型并/差/交的真实重建、Strict验证及受限可选Safe修复闭环。依据 [cycle-0082-gates.log](../../.axiom-agent/logs/cycle-0082-gates.log)、实际diff（含boolean_rebuild.cpp）与develop/repair报告，三个开发包“尚未运行/继续缓冲”已被最终实际门禁取代。本轮仅同步文档，没有构建或运行测试。

| 门禁 / 回归 | 实际结果 |
|---|---|
| 首轮配置（测试/示例开启）、完整构建 --parallel 4 | 成功；首轮CTest **11/16通过、5失败、28.43 s**，失败为workflow、ops_heal、heal、query_eval、prep |
| repair定向构建与回归（报告记录，单独保留） | 并发4增量构建成功；当前代码7/7通过：workflow3.53s、ops_heal147.98s、heal0.63s、query_eval1.87s、prep4.98s、topology0.14s、representation_io9.82s；不替代调度器最终全量 |
| repair后调度器第二轮配置 / 完整构建 | 成功，复用build-agent，未清缓存；构建总耗时未记录 |
| 最终串行 ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error | **16/16通过、0失败、184.89 s** |
| 必需 axiom_boolean_workflow_test / axiom_ops_heal_test / axiom_heal_test / axiom_query_eval_test | 最终各通过，**3.27 / 147.51 / 0.64 / 1.80 s** |
| 孔洞证据 axiom_boolean_prep_test；直接基础修补 topology / representation_io | 最终各通过，**5.00 / 0.17 / 9.62 s** |
| 性能 axiom_perf_baseline_test | 通过 **1.91 s**，预算/阈值未提高；墙钟不是跨环境性能保证 |
| 保留 smoke / runtime / plugin / diagnostics / geometry / math / IO workflow / IO dataset | 最终各通过，**0.06 / 0.06 / 0.06 / 0.13 / 0.60 / 0.01 / 13.31 / 0.73 s** |

首轮实发18条SDK/helpers的Issue::numeric_evidence初始化告警，最终增量构建未打印新告警，不证明全仓清零；日志未记录独立无告警门禁、最终文档门禁或提交成功。repair保留canonical edge PCurve/反向coedge核对、共线节点三角化、共面SAT、Safe明确几何资格与无序摘要规范化；消除无信息中心细分和重建端点整面横切，显式同步有限端点，以节点/来源/反向边索引保留真实有限覆盖与访问计费。准备分片分类的面BVH仅在数值guard证明安全时使用，否则全脸扫描；公开分类和重建探针契约不变。人工节点截面仍按真实数值拒绝处理，常规参考仍强制成功，未放宽Strict或预算/性能门禁。

下表为按用户验收条目排列的三条 `stage_evidence`；列举函数均由测试main调用，并随最终全量执行。固定数值为独立断言期望，日志没有逐项测量输出；限制是认证边界的一部分。

| 验收条目 | 测试文件 / 断言 | 独立参考结果 | 限制说明 |
|---|---|---|---|
| 1. 并/差/交从真实切分分类重建实体，公开面/边/壳与来源可查，非空成功Strict | tests/ops/boolean_workflow_test.cpp::check_real_rebuild_references核对owned新面边、整边两侧、面壳体来源、同ID去重/共面双来源、ExactBRep/Strict和UV世界环有向体积面积积分；check_safe_rebuild_repair_references锁Union初次bool.validate失败、Safe成功及面数减少，Subtract直接Strict成功。tests/heal/heal_test.cpp::rebuilt_cavity_validation_regression/rebuilt_safe_hole_validation_regression核对材料/空腔壳、genus-one孔、cap inner loops、trim/self-intersection/Strict；tests/ops/ops_heal_test.cpp::rebuilt_boolean_heal_chain_regression核对连续真实布尔 | 内部run_rebuilt非空发布须Strict及body_shell_regions；共面共享片唯一归属且双来源。Safe真孔V/A/S=39.9984/95.9976/19.9992；连续通孔链11.5/35.5/5.75及7.5/27.5/3.75，Safe后空腔15.8742/41.4984/7.7496 | 边来源经edge→face→source_faces间接查询；Safe只修人工共面分片/一致共线节点，保留真外孔环和角点，不放宽Strict六面及近似网格自交门禁；非全局嵌入/精确谓词证明 |
| 2. 独立V/A或截面锁定分离、包含、相切、共面与孔洞；空材料一致且无bbox/mesh实体替代 | workflow::check_real_rebuild_references从公共UV环积分并按独立盒公式检查；tests/ops/boolean_prep_test.cpp::check_rebuild_holed_references按V=hS、A=2S+hP、孔/凹材料点及壳数检查。空交/完全减除Ok、output=nullopt且体计数不增。workflow/heal/ops_heal补Safe真孔、连续空腔/通孔参考 | 偏移/旋转U/D/I：V15/7/1、A42/24/6、S7/3/1；分离U V11/A37、D8/24；包含空腔7.875/25.5/3.75。同ID/同形U/I8/24、D空；共面U12/32/6、D/I4/16/2；面相切U16/40/8、D8/24/4、I空；边点D保留/I空。孔跨高度V29/23/1、A90/72/8、S16/10/2；凹UV25/19/1、A82/64/8、S14/8/2；同高度孔V32/20/4、A92/68/20，凹UV28/16/4、A84/60/20，截面沿用对应16/10/2及14/8/2。Safe U15.9992/39.9984/7.9996、D7.9992/23.9984/3.9996 | Safe fixture prep1e-6/service Strict1e-3，只有Union实际修复成功，Subtract直接成功repaired=false。旋转local z1.375、通孔z0.875截面须成功；原人工节点z1.5/z1只允许正确面积或NumericalInstability/kQuerySectionFailure/query.section.numeric、无value。边/点Union、未解析薄层/容差带、曲面曲边/ExactCritical及任意截面成功不认证；兼容run代理未认证 |
| 3. 重建/验证/Safe失败分阶段，输入有效、活动事务无泄漏，回滚可重试 | workflow::check_real_rebuild_isolation/check_safe_rebuild_repair_references核bool.intersect/rebuild/validate/repair、E0013近共面、E0006边点/同壳夹点/Strict/Safe真实薄交失败、JSON；公开拓扑/UV世界点/支撑面/曲线端点/边长摘要、store/cache/bridge、双输入Eval依赖、writer状态/写次数失败不变，delete→rollback不复活。tests/eval/query_eval_test.cpp::rebuilt_boundary_eval_rollback_regression覆盖非共面/共面/Safe s0/out1/s1/out2保存点链、支撑几何/cache清理、输入Eval有效无重算及重试；兼容trim三根因×writer两状态与孔源边fraction独立插值同次保留 | 服务失败撤销新增对象与缓存/Eval，首次Safe失败物化在二轮前撤销，最终成功才登记writer服务allocation区间。完整rollback清输出且不复活，保存点保留前缀；重试参考V/A一致。合法bridge.for_body_entries每移除服务体+2、两体+4，其余字段不变 | 诊断与递增ID保留；合法累计遥测不回退；隔离摘要非完整几何序列化。成功服务登记不增加显式write_operation_count，不改变输入Eval；本范围不扩展至兼容run全部物化事务行为 |

全部三条验收要求已有最终执行证据，支持矩阵及固定参考见 [API §8.2.3](../api/AxiomKernel_详细模块接口清单.md#823-stage-4-真实实体重建支持矩阵cycle-0082--s4-rebuild)，样例见 §7.7，错误码字典 §7.5/§9.1、用户文案、矩阵、进度/Backlog和路线图同步。取消本批“尚未测试/等待完整构建”状态；`ready_for_acceptance`不表示已验收，只有调度器最终文档门禁及提交成功后才记录。本轮不提交推送，不写自动台账、.axiom-agent/或result.json；Stage4/FR-BOOL-001仍进行中，不追认0081/0080或Stage3历史提交与验收，不自行扩展后续阶段。

三个只读子Agent已全部结束：`/root/api_review`核对公开类型/签名、查询门禁、内部共面与只读prep区别、Safe及样例；`/root/diagnostic_review`核对复用码/阶段、cause证据、失败恢复及writer/保存点/完整rollback；`/root/evidence_review`核对两轮日志、main断言与独立参考和进度/Backlog一致性。三者未改文件、运行构建测试、写result.json、提交推送或派生Agent；主Agent为唯一文档写者。

本轮静态文档检查：`python3 scripts/check_docs.py`检查 **35个Markdown，0错误、0警告**；补充检查 **9个修改Markdown、195个本地链接、140个标题锚点及代码围栏，0错误**；`git diff --check`通过。这是本轮文档检查，不是重新编译/测试，也不补记为调度器最终文档门禁或提交成功。

### 1.10 cycle-0083 / S4-EXIT 门禁与逐项证据

`stage_task_id=S4-EXIT`，`stage_outcome=ready_for_acceptance`；Stage 4 历史退出任务，需求 FR-BOOL-001 / 模块 Ops；当前 Stage 6 / S6-DIRECT-EDIT 见 §1.17。依据 [主路线图 §4.5](../plan/AxiomKernel_主开发计划与阶段路线图.md#45-stage-4-布尔与验证器第一代)，本包收口第一代固定工业模型集的稳定布尔、全链路诊断与支持矩阵。本批实际代码 diff 仅修改 `tests/ops/boolean_workflow_test.cpp`、`tests/ops/boolean_prep_test.cpp`；生产代码、公开 API、既有错误码、性能门槛及测试注册均未变，无 repair 报告。开发报告中的“尚未执行/等待统一构建”已由 [cycle-0083-gates.log](../../.axiom-agent/logs/cycle-0083-gates.log) 的实际结果取代，cycle-0082 的 184.89 s 保留为 §1.9 历史结果。本轮文档同步没有重新构建或运行测试。

| 门禁 / 回归 | 本批实际结果 |
|---|---|
| `cmake -S . -B /workspaces/axiom/build-agent -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON` | 一轮配置成功 |
| `cmake --build /workspaces/axiom/build-agent --parallel 4` | 完整 target 构建成功，两份 Boolean 测试重新编译；复用现有构建目录，未清缓存 |
| `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` | **16/16 通过、0 失败、194.58 s** |
| 必需 `axiom_boolean_workflow_test` / `axiom_boolean_prep_test` | **4.25 / 5.63 s，通过** |
| 必需 `axiom_query_eval_test` / `axiom_representation_io_test` / `axiom_kernel_runtime_invariant_test` | **2.01 / 10.17 / 0.04 s，通过** |
| `axiom_perf_baseline_test` | **2.03 s，通过**；默认 150 次 / 4000 ms，CTest 超时 30 s 不变 |
| 关联 `axiom_ops_heal_test` / `axiom_heal_test` | **153.97 / 0.67 s，通过** |
| 保留 smoke / plugin / IO workflow / IO dataset / diagnostics / geometry / topology / math | **0.05 / 0.04 / 14.09 / 0.66 / 0.15 / 0.58 / 0.19 / 0.03 s，通过** |

日志未记录构建总时长、最终文档门禁或提交成功；增量构建未打印告警，不证明全仓告警清零。性能 2.03 s 是 CTest 墙钟，日志未打印内部 elapsed_ms、环境变量或逐模型测量；不得推出跨环境性能保证。`tests/perf/perf_baseline_test.cpp` 的实际工作负载是兼容 `run` 与查询，允许既有 OperationFailed 和代理质量查询的受限拒绝；不能据此认证 `run_rebuilt` 工业性能，也未提高阈值或减少迭代。

以下三行是按本次验收条目逐条排列的 `stage_evidence` 数组内容。新增断言由现有 main 调用，随本批 targets 通过；参考数值是独立断言期望，日志未逐项打印实测值。

| 验收条目 | 测试文件 / 断言 | 独立参考结果 | 限制说明 |
|---|---|---|---|
| 1. 固定模型集并/差/交及建模→布尔→Strict→查询/表示闭环，有独立参考与可重复回归；列明体类、精确/采样与限制 | `tests/ops/boolean_workflow_test.cpp::check_real_rebuild_references` 保留 owned 面/共享边/连通壳、来源、Strict、当前质量/截面与 UV 世界环积分；新增所有经此 helper 检查的非空固定输出 `brep_to_mesh/inspect_mesh`→OBJ 独立解析三角形，以 long double 累计有向 V/A，误差 ≤1e-7；标签 `mesh_from_brep_owned_faces`、策略 `owned_topo_welded`、无越界/退化三角、顶点/三角计数及重复转换同 MeshId。偏移盒 U/D/I 各总计两轮。`tests/ops/boolean_prep_test.cpp::check_rebuild_holed_references` 保留孔/凹U独立公式；`check_planar_intersection_references` 保留源边 `v0+fraction*(v1-v0)` 与强制内环命中。heal/ops_heal/query_eval 的真实孔/空腔/Safe/连续布尔闭环同次通过 | 偏移盒 U/D/I 的 V/A/S：15/42/7、7/24/3、1/6/1；分离 U 11/37/4，包含差集空腔 7.875/25.5/3.75，排除 bbox 代理；同形/同ID、旋转、共面/面相切、空交/完全减除 Ok/nullopt 既有参考保留。孔跨高度 U/D/I 29/90/16、23/72/10、1/8/2，凹U 25/82/14、19/64/8、1/8/2，独立按 V=hS、A=2S+hP；其余固定参考见 API §8.2.3 | 仅嵌入平面直边 ExactBRep、double/奇偶材料及可解析外向源壳；“精确”指平面边界在浮点容差内独立参考，非精确谓词。网格是当前平面边界三角化，不认证通用曲面/曲边采样；孔/凹U helper 未新增 OBJ 积分。公开 prep 仍拒绝共面，内部重建支持有限共面。边/点 Union、未解析薄层/容差带、曲面曲边/ExactCritical 拒绝；Strict 至少六面、近似网格自交门禁和人工节点截面数值拒绝保留，无全局嵌入证明；Safe 仅修人工分片/一致共线节点 |
| 2. 候选/求交/切分/分类/重建/验证/修复失败均有阶段诊断与 JSON；失败/提交/回滚跨模块一致 | prep `check_planar_preparation_failure_isolation` 覆盖候选/求交稳定码、阶段索引与 JSON；新增两输入网格预热、固定 CurveId 域和四点无缓存 `point_at_parameter` 对照缓存 eval（1e-12）、三点支撑面采样/边长/面归属、10 项 store / 6 项 tessellation / 5 项 bridge、真实 Eval 节点依赖/有效性/重算不变。workflow `check_split_classification_isolation` 保留切分/分类按阶段/码检索与 JSON；`check_real_rebuild_isolation` 新增重建/验证/修复按阶段/码索引命中失败 diagnostic_id，保留 JSON 与无/活动 writer 隔离。`check_safe_rebuild_repair_references` 与 `tests/eval/query_eval_test.cpp::rebuilt_boundary_eval_rollback_regression` 保留 Safe、保存点/完整 rollback、delete 后不复活及重试；`stage3_eval_rollback_consistency_regression` 等既有提交/后续失败恢复回归同次通过 | 七阶段为 `bool.prep.candidates / bool.intersect / bool.split / bool.classify / bool.rebuild / bool.validate / bool.repair`；E-0001、E-0006、E-0011..0014 等复用原语义。prep 失败与成功不改输入摘要、缓存/统计、依赖与 writer 写次数；rollback 删除临时体/顶点，无 writer 重试交段来源与端点逐段 1e-12 一致。未绑定 open_body 删除仅合法 `for_body_entries+1`；既有 rebuilt 服务体回滚各 +2 / 两体 +4，分别核对。兼容 trim 短边/257边/大坐标 E-0001/E-0012/E-0013、两种 writer 状态既有回归保留 | prep 此函数直接检索阶段，报告/JSON 核码；不声称逐 prep 案例新增双索引。不是全部生产根因注入，输入摘要非完整原始顶点/几何序列化。失败保留诊断/递增ID，合法累计遥测不回退。成功服务 allocation 不增加显式 write_operation_count 或失效输入 Eval。兼容 trim 读取失败传播及物化前原子性已在基线，非本批新修复，不认证兼容 run 全部后续事务行为 |
| 3. 完整构建/CTest、性能基线及文档检查通过，证据对应路线图 §4.5，保留未支持工业能力 | 本批日志完整配置/并发4构建/16项CTest；上述五项必需 targets 与 `tests/perf/perf_baseline_test.cpp` 均通过；API/字典/样例/需求矩阵/进度/Backlog/路线图和性能规范同步，静态文档检查结果单列于本节末尾 | 16/16、0失败、194.58 s；五项必需依序 4.25/5.63/2.01/10.17/0.04 s；性能墙钟 2.03 s。对应路线图“基础工业模型集布尔稳定运行/失败可定位”，范围由 API §8.2.4 固定 | 现有 perf 只覆盖兼容 run/查询，run_rebuilt 工业性能未认证；文档阶段未重跑测试。调度器最终文档门禁与提交成功尚未记录，ready_for_acceptance 不等于已验收；未支持的工业能力继续列为限制，不据此追认历史阶段或宣称项目完成 |

支持矩阵收口见 [API §8.2.4](../api/AxiomKernel_详细模块接口清单.md#824-stage-4-退出支持矩阵cycle-0083--s4-exit)，[当前进度 §5.2.1](../plan/AxiomKernel_当前开发进度.md#521-stage-6-当前退出任务) 为正式状态入口。代码全量门禁已通过，本批不再保留“尚未运行/待统一构建”的开发旧状态；最终文档门禁及调度器提交成功后才记录正式已验收，Stage 4 / FR-BOOL-001 仍进行中。停止扩展本包，不启动后续阶段，不追认 0082/0081/0080 或 Stage 3 历史提交与验收。本轮仅改 docs Markdown，不写自动开发进度台账、.axiom-agent/ 或 result.json，不提交或推送。

三个只读子 Agent 已全部结束：`/root/api_review` 核对 API/实际 diff/支持范围/样例，确认生产与签名冻结、OBJ覆盖与限制；`/root/diagnostic_review` 核对既有码/七阶段/失败原子性，确认新检索断言及两种 rollback 遥测区别；`/root/evidence_review` 核对一轮日志、三项验收及矩阵/进度/Backlog，确认 16/16 与正式验收条件。三者未修改文件、运行构建测试、写 result.json、提交推送或派生 Agent；主 Agent 为唯一文档写者。

本轮静态文档检查：`python3 scripts/check_docs.py` 检查 **35个Markdown、0错误、0警告**；补充检查 **10个修改Markdown、211个本地链接、153个标题锚点及代码围栏，0错误**；`git diff --check` 通过。这是主Agent本轮实际文档检查，不是重新编译/测试，也不补记为调度器最终文档门禁或提交成功。

### 1.11 cycle-0084 / S5-HEAL 门禁与逐项证据

本节保留 S5-HEAL 历史代码门禁及范围；当前唯一退出任务为 cycle-0090 / S6-DIRECT-EDIT，见 §1.17，不追认本节历史正式验收或提交。

`stage_task_id=S5-HEAL`、`stage_outcome=ready_for_acceptance`；当前主线按 [主路线图 §4.6](../plan/AxiomKernel_主开发计划与阶段路线图.md#46-stage-5-修复导入导出三角化) 为 Stage 5。本节采用 [调度器日志](../../.axiom-agent/logs/cycle-0084-gates.log) 中 repair 后最终全量结果，不沿用 develop 的“未运行”或 repair 定向耗时。日志记录两轮测试/示例开启配置与完整并发 4 构建成功；首轮 **11/16、5 失败、26.25 s**，失败为 Heal、Ops/Heal、IO workflow、Diagnostics、Topology。repair 补 canonical 端点 PCurve 和既有 Generic 真实输出资格，保留子阶段/回滚证据，纠正闭合查询口径及非法 bbox/单面开壳旧成功预期；最终 **16/16、0 失败、214.19 s**。

| 最终调度器门禁 | 真实结果 |
|---|---|
| 完整构建，测试/示例开启、并发 4 | 成功；首轮实发 18 条 Issue 成员初始化告警，末轮未重编相关单元，不宣称全仓无告警 |
| `axiom_heal_test`（必需） | 通过，1.05 s |
| `axiom_ops_heal_test`（必需） | 通过，172.05 s |
| `axiom_io_workflow_test`（必需） | 通过，15.07 s |
| `axiom_diagnostics_test` / `axiom_topology_test` | 通过，0.31 / 0.19 s |
| `axiom_perf_baseline_test` | 通过，2.01 s；CTest 墙钟，不是新修复专用性能保证 |
| 完整 CTest | 16/16、0 失败、214.19 s |

日志配置为现有默认构建，未启用标准交换桥接；没有显式 `BRIDGE=OFF` 配置记录，也没有标准全实体交换认证。默认既有 16 项及性能门禁保留；性能默认 150 次/4000 ms、CTest 超时 30 s 未变，日志未打印内部 elapsed_ms 或环境覆盖。repair 报告的五目标定向成功（Heal 0.84、Ops/Heal 154.50、IO 14.96、Diagnostics 0.25、Topology 0.18 s）仅为定位复验，不替代上述最终结果。

以下 `stage_evidence` 数组按两条验收要求逐条排列，每项同时给出测试/断言、独立参考与限制：

1. **固定缺陷、独立缺陷/几何偏差预期、成功结果验证**。`tests/heal/heal_test.cpp:68::planar_import_repair_regression` 固定公开 Topo 六面 2×3×4 夹具，输入独立预期 24 顶点/24 单侧边及逆向侧面；binary-exact linear=1/1024，gap=0、linear/2、linear 成功，1.01linear 拒绝。成功输出 Strict、6 面/12 边/8 顶点，每边两张不同邻接面及两次相反方向使用；独立 public UV 世界环积分 V=24/A=52、八固定角点和 maximum_vertex_displacement=gap≤linear，源缺陷不变。`tests/io/io_workflow_test.cpp:79::check_stage5_import_repair_loop` 与 `tests/data/io/s5_heal_box_subset.step`、`s5_heal_invalid_subset.step`：显式注入 1 重复 FaceId/1 零长节点，独立 7 面引用/5 共边→6 面/12 边/8 顶点，清理计数各 1、位移 0、Strict 和 io.post_import.post_validate，独立 V=24/A=52/截面 6、原顶点精确不变。`tests/ops/ops_heal_test.cpp:71::stage5_repaired_modeling_chain_regression` 修复→真实 Boolean 交→Strict，独立 1×3×4、V=12/A=38 及内外点；IO `:24::check_stage5_flat_mesh_validation` 合法二维 OBJ 的 Standard/Strict 与越界/退化/NaN 拒绝。**限制**：间隙/方向由公开 Topo 构造，STEP 文件仅 Axiom 元数据，owned 重复/退化由测试物化/注入，不是标准 STEP 实体缺陷交换。现有 boundary_edge_count_of_body 修复输出断言 **12**，源为 24；水密由独立面邻接/反向共边证明，取消 develop 报告 API=0 的错误口径。新规则仅 Standard 失败的至少六唯一面、单壳平面直边外环，无内孔/曲面/代理面。上述三个必需回归均随最终全量通过。

2. **策略/容差边界、稳定阶段、失败回滚无污染及重试**。Heal `:68` 锁定 auto_repair ReportOnly/SuggestOnly 的原体/报告预检状态、模型/cache/统计/Eval 不变，超容差两次 `heal.auto_repair.planar.orient` 失败及有限实体证据；`:252::planar_post_validation_rollback_regression` 用 angular=0 触发真实分配后的 Strict 失败，要求 planar.post_validate、allocated_object_count>0，两轮对象/几何/体/反向索引/拓扑版本/源引用/缓存统计/Eval 恢复，修正 angular 后同源 Strict 重试成功。Ops `:71` 首项已重建、五面后项 planar.extract 失败，保留子项阶段及批量 rollback 证据，前项派生/Eval 全部回滚并可重试；Strict 后验失败保留实际子阶段。IO `:79` 暖缓存与既有哨兵下，固定无效 STEP 的观察策略/ Safe 分别两次 io.post_import.validation/repair 失败，模型/cache/统计/Eval/next_id 不变后合法文件成功；Sphere 支持外输入稳定 planar.extract 拒绝，保留源和成功兄弟体。`tests/diag/diagnostics_test.cpp` 锁定非法 bbox 默认与 Safe 两次原子失败、既有码/有限阶段证据，显式 Aggressive 历史单位盒兼容必须 metadata_bbox/synthetic_bbox=1；`tests/topo/topology_test.cpp` 单面开壳拒绝无污染，真实六面 Strict 合法体成功保留谱系。**限制**：linear 须有限正值且位于 min_local/max_local 内，拒绝焊接歧义/链式漂移；新规则不扩大曲面/孔洞/多壳。Heal 失败诊断保留、允许 ID 空档；IO 回滚恢复 next_id。观察语义仅针对 auto_repair/ImportOptions，不泛化旧局部接口；外层观察 Result 可 Ok，内部 OpReport::status 可失败。MeshRep 顶点/索引/非退化检查不证明闭合/自交。上述三个必需回归及 Diagnostics/Topology 均随最终全量通过。

公开签名冻结、既有码复用，API/字典/样例/矩阵/进度/Backlog 已同步，支持矩阵见 [API §9.2.1](../api/AxiomKernel_详细模块接口清单.md#921-stage-5-第一代导入修复闭环cycle-0084--s5-heal)。代码完整门禁已通过，不再保留本批“尚未运行/等待统一构建”的开发旧状态；调度器最终文档门禁和提交成功仍未记录，**不标记正式已验收**，FR-HEAL-001 保持进行中。本轮仅改 docs Markdown，没有构建测试、修改自动台账/.axiom-agent/、写 result.json 或提交推送；历史 Stage 4 / Stage 3 验收与提交不作反推。

三个只读子 Agent 已全部结束：`/root/api_review` 确认公开签名、受限支持域、观察报告状态与导入样例变化；`/root/diagnostic_review` 确认既有码复用、Heal 子阶段保留/IO 外层阶段映射及回滚/ID 差异；`/root/evidence_review` 确认两轮日志、最终 16/16、两条独立验收证据与矩阵/进度/Backlog 对齐。三者未修改文件、运行构建测试、写 result.json、提交推送或派生 Agent；主 Agent 为唯一文档写者。

本轮主 Agent 静态文档检查：`python3 scripts/check_docs.py` 检查 **35 个 Markdown、0 错误、0 警告**；补充检查 **9 个修改 Markdown、227 个本地链接、168 个标题锚点及代码围栏，0 错误**，`git diff --check` 通过。此为本轮实际文档检查，不是构建/CTest，也不补记为调度器最终文档门禁或提交成功。

### 1.12 cycle-0085 / S5-IO 门禁与逐项证据

`stage_task_id=S5-IO`、`stage_outcome=ready_for_acceptance` 为历史报告；当前唯一任务为 cycle-0090 / S6-DIRECT-EDIT（§1.17），本节保留 IO 历史代码门禁及范围。依据 [调度器独立门禁日志](../../.axiom-agent/logs/cycle-0085-gates.log) 和实际实现/测试 diff，一轮测试与示例开启的配置、并发 4 完整构建成功，完整 CTest **16/16 通过、0 失败、227.56 s**，无 repair。本轮文档同步未重新构建或运行测试。develop 报告中的“未运行/待统一构建”是旧状态；代码门禁已通过，最终文档门禁及调度器提交成功尚未记录，不能记为正式已验收。

| 调度器门禁 | 真实结果 |
|---|---|
| 完整构建 | 成功；实发 SDK `Issue::numeric_evidence` 缺失初始化告警 1 条，不宣称全仓告警清零；构建总耗时未记录 |
| `axiom_io_workflow_test`（必需） | 通过，14.77 s |
| `axiom_io_dataset_test`（必需） | 通过，0.70 s |
| `axiom_representation_io_test`（必需） | 通过，12.23 s |
| `axiom_ops_heal_test` / `axiom_heal_test` | 通过，182.48 / 0.91 s |
| `axiom_perf_baseline_test` | 通过，2.00 s；CTest 墙钟，非新增 IO 专用性能认证 |
| 完整 CTest | 16/16、0 失败、227.56 s |

以下 `stage_evidence` 数组按三条验收要求逐条排列，三项必需回归及保留回归均随上述全量通过：

1. **固定格式闭环、实际几何/拓扑、单位与误差**。`tests/io/io_dataset_test.cpp::check_stage5_fixed_precision_corpus` 使用 `tests/data/io/s5_io_precision_subset.step/.iges/.brep` 与 `s5_io_precision_tetra.stl`，覆盖导入→Standard 验证及可选 auto_repair→导出→再导入；独立文本解析对 double 参数/坐标及 STL 36 个 facet 坐标作精确比较，comma locale 由 RAII 恢复。前三格式固定 origin=(123456.123456789,-1.2345678901234567,3.456789012345679)、params=(2.345678901234568,3.456789012345679,4.567890123456789)，ExactBRep 标签但 **零 owned shells**，不授予真实实体交换资格。`tests/rep/representation_io_test.cpp::stage5_stl_geometry_reference_regression` 对导入及再导入的实际三角形独立积分：边长 2/3/4 的平移四面体 **V=4、A=13+sqrt(244)/2、C=origin+(0.5,0.75,1)**，绝对误差 **1e-12**；4 三角形/12 顶点、`io_import_stl` / MeshRep，与 bbox 体积 24 区分。IO workflow 保留 `check_stage5_import_repair_loop` 和 dirty STEP Aggressive 验证→修复→再验证。**限制**：前三格式为 Axiom 元数据子集，owned 缺陷由测试物化/注入；坐标保留模型单位，未证明标准单位转换、一般工业几何或全实体交换。网格验证不证明闭合/自交，固定积分不泛化到所有 STL。

2. **默认路径与可选标准桥接分开**。当前 `CMakeLists.txt/cmake` 未定义或启用 `AXM_ENABLE_STEP_IGES_BRIDGE`，不新增外部依赖；默认行为等同未启用桥接，不能描述成实际配置了 `BRIDGE=OFF`。本轮不适用 [标准交换路线](../plan/AxiomKernel_STEP_IGES_标准交换实施路线.md) 里程碑 1～4 的 ON 路线 DoD。dataset 保留 `standard_step_express_stub.step` / `standard_iges_deck_stub.iges` 的 NotImplemented、`AXM-IO-E-0010/0011` 与 `AXM-IO-D-0016/0017` 扫描摘要；workflow `check_stage5_io_rejection_boundaries` 核对标准数据混入 Axiom 标记仍优先拒绝、根因/scan 诊断与暖模型不污染；dataset 的合法 IGES Hollerith 长标签不误拒。**限制**：标准检测是启发式物理扫描，Axiom schema/entity 名称、文件容器和代理 bbox 不等于 EXPRESS 或 DE/PD 实体解析；默认完整 16 项门禁保持通过，不强制引入桥接。

3. **损坏/不支持、读写/预算失败阶段诊断及回滚**。workflow `check_stage5_io_rejection_boundaries` 覆盖空/随机/字段截断 STEP、NaN、STEP/IGES/BREP/STL 四格式 **64 MiB+1** sparse 预读预算（`.read`）、STL 缺 endsolid/闭合后垃圾、独立 134 字节 binary NaN 及 1e200 面积溢出（均为 `.validation` / AXM-VAL-E-0010）；核对无 value、阶段/根因码/实体令牌/有限数值、stores/Eval/暖缓存/统计/next_id 不污染及合法固定文件重试。保留 `check_exact_brep_import_failure_package` 和既有读取/损坏/导入回滚。`check_mesh_export_failure_package` 的冷/暖缓存及策略组合证明侧车失败不新建主文件、已有主文件逐字节保留、临时目录清理、mesh/cache/stats/ID/Eval 恢复；边界包为 STEP/IGES/BREP 补目录 `.open`、设备 `.write`、旧目标保护和成功覆盖重试。八格式实现同目录独占临时文件，finish（检查写入/关闭）→请求侧车→rename publish，发布失败复用 `AXM-IO-E-0005 / io.export.<format>.publish` 并回滚本次模型状态；未发布主文件不报成功。**限制**：publish 失败分支由实现静态核验，测试没有独立 rename 竞态失败注入，不单独宣称该分支已运行；侧车沿用 REP 出口，侧车/主文件及全批无跨文件事务，rename 不承诺掉电持久性或并发目录修改安全。

三个只读子 Agent 已全部结束：`/root/api_review` 核实签名/选项冻结、元数据零 owned shells 与 STL 实际参考、模型单位及调用样例范围；`/root/diagnostic_review` 核实稳定根因/阶段、单主文件保护与侧车/全批限制，并指出 publish 无直接失败注入；`/root/evidence_review` 核实一轮真实门禁、三个必需回归时间及阶段/矩阵/进度/Backlog 一致性。三者未修改文件、运行构建测试、写 result.json、提交推送或派生 Agent；主 Agent 是唯一文档写者。

本轮静态文档检查：`python3 scripts/check_docs.py` 检查 **35 个 Markdown、0 错误、0 警告**；补充检查 **12 个修改 Markdown、251 个本地链接、181 个标题锚点及代码围栏，0 错误**；`git diff --check` 通过。此为本轮文档检查，不替代调度器最终文档门禁或提交成功，不是重新构建/CTest。

公开 API 签名及选项冻结，既有错误码复用；支持合同见 [API §11.1.1](../api/AxiomKernel_详细模块接口清单.md#1111-stage-5-受限-io-主链路cycle-0085--s5-io)，文件发布策略见 [IO 矩阵](AxiomKernel_IO_导出策略矩阵.md)。FR-IO-001 保持受限可用，Stage 5 进行中；本轮不反推历史阶段的提交或正式验收，不修改自动开发台账或 `.axiom-agent/`，不写 result.json、不提交推送。

### 1.13 cycle-0086 / S5-TESSELLATION 门禁与逐项证据

本节保留历史三角化代码门禁与受限支持合同；当前唯一退出任务为 cycle-0090 / S6-DIRECT-EDIT（§1.17），不追认本节正式验收或提交。

`stage_task_id=S5-TESSELLATION`、`stage_outcome=ready_for_acceptance`；Stage 5 历史三角化任务。依据 [调度器独立门禁日志](../../.axiom-agent/logs/cycle-0086-gates.log)、实际实现/测试 diff 及 develop/repair 报告，两轮测试与示例开启的配置、并发 4 完整构建成功。首轮 CTest **9/16、7 失败、6.96 s**，失败目标为 Boolean workflow、IO workflow、representation_io、Ops-Heal、Heal、query_eval、Boolean prep；repair 后最终全量 **16/16、0 失败、200.26 s**。本轮仅同步文档，未重新构建或运行测试。

repair 修复平面 PCurve 端点舍入误拒绝、匹配容差被误作最小边长、既有 MeshRep 检查抢占 IO QA、有限大坐标退化运算溢出；补充关闭可选验证时 glTF float32 NaN/Inf 的物化前拒绝，并修正工厂归一化节点与合法 PCurve 的测试预期，保留真正非夹持节点及错误 PCurve 拒绝。最终完整门禁取代 develop 的“未运行”和 repair 的“待完整门禁”，定向定位期间的 19.03/2.12/2.05 s 不作为最终必需回归耗时。

| 最终门禁 | 结果 | 证据范围 |
|---|---|---|
| 完整构建（并发 4，测试/示例开启） | 成功 | 首次实发 18 条既有 Issue::numeric_evidence 初始化告警（helpers 17、kernel_plugin 1）；最终增量无告警输出不证明全仓清零，构建总耗时未记录 |
| axiom_representation_io_test（必需） | 通过，15.40 s | 原生/双线性/平面法向/凹孔真实边界、OBJ 独立积分、缓存/批量/往返/幂等及 MeshRep 缺网格拒绝 |
| axiom_geometry_test（必需） | 通过，0.66 s | 双线性三类曲面与矩形 trim 的独立位置/法向参考 |
| axiom_query_eval_test（必需） | 通过，1.82 s | warm/cold/local/shell/batch/roundtrip 失败隔离、PCurve 编辑及事务/Eval/rollback |
| 关联 workflow/prep/IO workflow/dataset/Ops-Heal/Heal | 通过，4.01/5.61/13.34/0.51/155.95/0.75 s | 本批原失败链路最终通过，IO QA/兼容诊断及 NaN/Inf 原子拒绝保留 |
| axiom_perf_baseline_test | 通过，1.89 s | 既有性能门槛；不证明本批一般曲面或大模型工业性能 |
| 全量 CTest | 16/16，0 失败，200.26 s | 最终文档门禁及调度器提交成功未记录，不标记正式已验收 |

`stage_evidence` 按本任务两个验收条目排列，不能将固定参考扩张为通用曲面认证：

1. **受支持曲面/裁剪面、凹面/孔洞与真实边界一致，明确误差、法向、退化及不支持范围。** `tests/rep/representation_io_test.cpp` 的 `stage5_native_boundary_reference_regression`、`stage5_bilinear_boundary_regression`、`stage5_planar_normal_boundary_regression`、`stage3_representation_consistency_regression` 与 main Swept fixture，及 `tests/geo/geometry_test.cpp::stage5_bilinear_geometry_reference_regression` 已随最终必需门禁通过。独立 `r=(u,v,uv)`、法向 `(-v,-u,1)/sqrt(1+u²+v²)` 核对 Bezier/degree1 等权 BSpline/NURBS 与 `[0.2,0.8]` 矩形 trim；Swept 独立 A=2、y=0、外法向 −y。凹口/孔端盖探针确认没有跨孔填充。native 五类核对三角**重心**到解析面的偏差≤0.1、重心法向角≤5°、正确绕序/无退化/无越界及预算拒绝；bilinear 核对重心 `z-xy` 偏差≤0.1、三角三个顶点解析法向角≤5°。实现的三角内部理论界 `|mixed|/(4*nu*nv)`（3/4 弦高预算）和仿射法向四角锥覆盖全单元为实现核验，测试未穷尽所有内部点。合法倾斜薄环 30° 成功、5° full/local/shell 拒绝；归一化 `{2,2,5,5}→{0,0,1,1}` 成功，真实非夹持 `{-1,0,1,2}`、高阶、不等权、错误 PCurve、非矩形/孔裁剪拒绝。匹配容差不是最小特征尺寸，关联 Boolean workflow 的 1e-3 模型容差/2e-4 窄重叠减法通过。限制：仅真实四边矩形的四极点双线性、工厂归一化后单位夹持一阶等权样条、LineSegment Swept 及矩形 Trimmed；一般曲边、高阶、曲面孔洞/非矩形、Offset/Revolved 不支持。patch 每向256、圆周4096、native百万顶点上限，不可达失败；小尺度 polygon 绝对门槛保留，UV seam/法向拆分不认证流形连通性。

2. **几何偏差与面积/体积由独立参考核对，缓存/编辑/回滚正确，转换失败无部分结果及 bbox 物理回退。** Rep 上述回归独立解析公开 OBJ，以 long double 有向四面体体积/三角面积积分核对下表；双线性面积与独立 64×64 二维 Simpson 积分 `sqrt(1+u²+v²)` 参考绝对误差<0.002；凹形 V=10/A=34/C=(1.1,1.1,1)、孔洞棱柱 V=24/A=72/C=(2,2,1) 独立精确参考保留。full/local/shell、前项成功后项失败的两个 batch、双向 round-trip 成功/失败、重复转换/重复 batch 幂等、独立 Kernel 的 box→mesh→MeshRep→clear_mesh_store 后 `.support` 拒绝且无 bbox 替代，核对 mesh/body/cache/Geo缓存/六项统计/next_id/MeshId 绑定的13项指纹。`tests/eval/query_eval_test.cpp::stage3_eval_rollback_consistency_regression` 核对14项指纹、事务写集、Eval重算、旧网格快照不变及 rollback 恢复；错误 PCurve 明确拒绝，合法绑定用公开端点与独立 profile+Z4/平面UV再映射世界坐标误差≤1e-12，核对新 MeshId/缓存身份/dirty-recompute/rollback。`tests/io/io_workflow_test.cpp` 保留严格/兼容 QA、旧文件及状态隔离，新增 `run_validation=false` 的 float32 NaN/Inf 拒绝且 IDs/mesh/cache 不变。实现快照恢复 batch 失败及往返临时 mesh/body/source_body/cache/Geo缓存/统计/next_id，诊断保留；新结果发布前检查非有限/退化，已有嵌入网格保留 IO 检查政策。限制：开放双线性 MeshRep 质量查询拒绝；metadata/implicit 仅显示代理；round-trip 不证明任意 BRep 保真；OBJ 不导出 vertex normals，本轮核对三角 cross 与解析/Geo法向；固定参考不是工业全局误差保证。

| 独立 native 参考 | 体积 V / 面积 A | OBJ 积分门槛 |
|---|---|---|
| box 2×3×4 | 24 / 52 | 绝对误差≤1e-10 |
| sphere r=2 | 32π/3 / 16π | V、A 相对误差≤1% |
| cylinder r=2,h=4 | 16π / 24π | 同上 |
| cone h=4,r=4tan(π/6) | πr²h/3 / πr(r+hypot(r,h)) | 同上 |
| torus R=2,r=0.5 | π² / 4π² | 同上 |

支持/预算/事务合同见 [API §7.3.2](../api/AxiomKernel_详细模块接口清单.md#732-stage-5-真实边界三角化与转换一致性cycle-0086--s5-tessellation)，稳定阶段见 [字典 §7.12](../diagnostics/AxiomKernel_错误码与诊断码字典.md#712-tes-三角化错误码)。FR-REP-001 保持受限可用；Stage 5 进行中，正式状态见 [当前进度 §5.2.1](../plan/AxiomKernel_当前开发进度.md#521-stage-6-当前退出任务)。本轮不反推历史 Stage 3/4 或 S5-HEAL/S5-IO 的提交/验收，不修改自动开发台账。

### 1.14 cycle-0087 / S5-EXIT 门禁与逐项证据

本节保留当批代码门禁、文档检查与集成支持合同；当前唯一任务为 cycle-0090 / S6-DIRECT-EDIT（§1.17），本轮不追认本节正式验收/提交。

`stage_task_id=S5-EXIT`、`stage_outcome=ready_for_acceptance`。当批主线 Stage 5，依据[主路线图 §4.6](../plan/AxiomKernel_主开发计划与阶段路线图.md#46-stage-5-修复导入导出三角化)，本包收口固定导入验证修复、交换往返与三角化显示/分析支持。实际 diff 为 `include/axiom/heal/heal_services.h` 合同注释、`src/axiom/internal/heal/heal_helpers_a.inc` 中仅 `auto_repair` 的 MeshRep 完整快照分支、`tests/io/io_dataset_test.cpp` 集成回归；公开签名、共享数据、错误码常量及构建/性能配置冻结。无 repair；develop 的“尚未运行/等待完整门禁”已由[本批调度器日志](../../.axiom-agent/logs/cycle-0087-gates.log)取代，0084～0086保留历史事实，不替代0087结果。本轮文档同步未重跑构建或测试。

| 本批调度器门禁 | 实际结果 |
|---|---|
| 测试/示例开启配置；`cmake --build /workspaces/axiom/build-agent --parallel 4` | 一轮配置、完整构建成功；SDK `kernel_plugin.cpp:122` 实发1条 `Issue::numeric_evidence` 缺失初始化告警，不宣称全仓无告警 |
| `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` | **16/16、0失败、196.58 s** |
| 必需 `axiom_io_workflow_test` / `axiom_io_dataset_test` | **13.05 / 0.55 s，通过** |
| 必需 `axiom_heal_test` / `axiom_representation_io_test` | **0.78 / 14.32 s，通过** |
| 关联 `axiom_ops_heal_test` / `axiom_geometry_test` / `axiom_query_eval_test` | **153.92 / 0.57 / 1.77 s，通过** |
| `axiom_perf_baseline_test` | **1.92 s，通过**；既有性能门槛不变，非 IO/Heal/三角化工业专用性能认证 |

日志未记录构建总耗时、最终文档门禁或调度器提交成功。下列 `stage_evidence` 数组按两条验收要求排列；参考数值是断言期望，日志未逐项输出测量值。

1. **固定数据导入→验证→修复→三角化→导出/再导入，几何/拓扑/错误预期可复现。** `tests/io/io_dataset_test.cpp:32::check_stage5_fixed_precision_corpus` 由 main 调用，复用 `tests/data/io/s5_io_precision_subset.step/.iges/.brep` 与 `s5_io_precision_tetra.stl`。metadata 显式 Standard→ReportOnly 原体/零 owned shells，原 Box 三角化拒绝 `AXM-TES-E-0001 / rep.tessellation.topology`，质量拒绝 `AXM-TOPO-E-0005 / query.mass_properties.empty_gate`；Safe 生成 Modified 合成 bbox owned 边界，Standard及 `owned_topo_welded` 显示成功，但 `AXM-CORE-E-0004 / query.mass_properties.support_gate` 拒绝质量。逗号 locale 下 classic locale/max_digits10 导出，独立文本解析12个 origin/params/bbox double 精确比较；再导入仍零壳、`bbox_proxy` 显示、质量拒绝。STL 显式 Strict→Safe 新BodyId/新MeshId完整网格快照→显式 Strict→三角化→导出/再导入，源 MeshId 不变；源/派生/再导入各实际4三角形、零 owned shells、`io_import_stl`，独立 ASCII 顶点解析逐坐标精确比较与三角积分 **V=4、A=13+sqrt(244)/2、C=origin+(0.5,0.75,1)，绝对误差≤1e-12**，bbox V=24不是几何参考。`:284` 缺网格两次 `AXM-HEAL-E-0006` 与 `AXM-VAL-E-0004 / heal.auto_repair.post_validate` 拒绝，模型/mesh/cache/统计/Eval保持，合法重新导入后Strict成功；`:338` angular=0 在实际复制网格后失败，根因 `AXM-VAL-E-0003`、`allocated_object_count>=2`、`rollback_applied=1`，回收本次体/mesh、保留源MeshId/源坐标及独立积分、缓存统计与Eval。MeshRep内部后验为 **Standard**，Strict为固定回归显式额外调用；独立Heal允许ID空档，IO外层另恢复next_id。已有 `tests/io/io_workflow_test.cpp:79::check_stage5_import_repair_loop` 的固定HEAL元数据显式owned物化/重复引用与零长节点注入→真实Safe→Strict，6面12边8点、V=24/A=52/截面6与源缺陷保留；`tests/heal/heal_test.cpp:68/:252` 间隙/退化/分配后回滚及 `tests/rep/representation_io_test.cpp` 独立真实边界/法向/积分同批通过。**限制**：metadata显示闭环和测试显式owned物化不证明标准BRep交换；MeshRep Strict及固定STL积分不证明任意网格流形/自交、全实体质量或工业保真。

2. **完整构建/CTest及文档检查，对齐 §4.6 的格式/桥接/三角化矩阵、误差与限制。** 本批完整构建、16项CTest及四必需回归已通过，结果如上；[集成支持矩阵](../api/AxiomKernel_详细模块接口清单.md#1112-stage-5-集成退出支持矩阵cycle-0087--s5-exit)、[IO导出策略](AxiomKernel_IO_导出策略矩阵.md)、[固定数据](AxiomKernel_基准数据集与性能管理规范.md#32-cycle-0087--s5-exit-固定闭环)、API/字典/样例/需求矩阵/进度/Backlog及主路线图本轮同步，实际文档检查结果在本节末记录。沿用§1.13真实平面直边凹/孔、认证矩形四极点双线性、degree1等权工厂归一化后单位夹持样条、LineSegment Swept/矩形Trimmed；一般曲边、高阶、不等权、曲面孔洞/非矩形、Offset/Revolved拒绝，patch每向256/圆周4096/native百万顶点及不可达失败、小尺度绝对门槛、UV seam/法向拆分不认证流形、开放曲面无实体质量资格全部保留。OBJ无vertex normals，参考核对三角cross与解析/Geo法向；固定积分/round-trip不是任意BRep或工业全局误差证明。CMake未定义或启用标准桥接，未实际执行BRIDGE=OFF，标准交换里程碑1～4 ON路线不适用；默认元数据及标准文件拒绝路径保留。四格式64 MiB、模型单位、八格式classic locale/max_digits10、glTF float32、同目录临时payload→关闭→请求侧车→rename单主文件发布及旧文件/状态保护保留；侧车/全批无跨文件事务、不承诺掉电持久性或并发目录修改安全，publish失败无直接注入回归。最终文档门禁及提交成功尚未记录，故 **ready_for_acceptance不表示正式已验收**。

FR-IO-001 / FR-REP-001受限可用，Stage 5进行中；正式状态见[当前进度 §5.2.1](../plan/AxiomKernel_当前开发进度.md#521-stage-6-当前退出任务)。本轮只编辑docs Markdown，不改 `tests/data/io/README.md`、自动开发进度台账、代码/测试/配置/脚本或 `.axiom-agent/`，不写result.json，不提交推送，不扩展后续阶段或追认历史提交/验收。

三个只读子 Agent 均已结束：`/root/api_review` 确认公开签名冻结、MeshRep快照合同及metadata显示/质量资格区别；`/root/diagnostic_review` 确认既有码、Standard后验/显式Strict、阶段映射和Heal/IO的ID恢复差异；`/root/evidence_review` 确认本批一轮16/16、四必需回归、两条证据及当前状态一致性。三者未修改文件、运行构建测试、写result.json、提交推送或派生Agent；主Agent唯一文档写者。

本轮主Agent静态文档检查：`python3 scripts/check_docs.py` 检查 **35个Markdown、0错误、0警告**；补充检查 **12个修改Markdown、286个本地链接、213个标题锚点及代码围栏，0错误**；`git diff --check` 通过。此为本轮实际文档检查，未重新构建/运行CTest，不补记为调度器最终文档门禁或提交成功。

### 1.15 cycle-0088 / S6-BLEND 门禁与逐项证据

本节保留 cycle-0088 / S6-BLEND 历史代码门禁；当前唯一任务为 cycle-0090 / S6-DIRECT-EDIT（§1.17）。历史报告 `stage_task_id=S6-BLEND`，develop/repair 的 `stage_outcome=ready_for_acceptance`；本节记录代码门禁与文档事实，不记录正式验收或 Stage 6 已退出。依据 [cycle-0088-gates.log](../../.axiom-agent/logs/cycle-0088-gates.log)、实际 diff（含新增 `src/axiom/ops/blend_services.cpp`）与回归断言：首轮配置成功，完整构建因 Query/Eval 回归两处错误调用 `representation().brep_to_mesh` 失败，没有首轮 CTest。repair 改为既有 `convert().brep_to_mesh`，修正 boundary 查询预期，并仅为真实平面倒角开放既有闭壳查询白名单；保留原 Plane/Line、非 proxy、闭合/绕向与数值检查，无新积分算法。repair 报告的提前定向 **3/3、161.19 s** 单列，不替代完整门禁。

| 修复后调度器完整门禁 | 真实结果 |
|---|---|
| 配置 / 完整构建 | 测试与示例开启；第二轮配置及 `cmake --build /workspaces/axiom/build-agent --parallel 4` 成功 |
| `axiom_ops_heal_test` | 通过，155.87 s |
| `axiom_topology_test` | 通过，0.36 s |
| `axiom_query_eval_test` | 通过，2.23 s |
| `axiom_perf_baseline_test` | 通过，1.79 s；既有基线，非新增 Blend 工业性能认证 |
| 完整 CTest | `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error`，16/16、0 失败、200.33 s |

日志有 SDK/helpers 的 `Issue::numeric_evidence` 聚合初始化告警，不宣称无告警构建；未记录构建总耗时、最终文档门禁或提交成功。develop 的“测试尚未执行”和 repair 的“完整门禁尚待运行”已由最终独立代码门禁取代。本轮未重新构建或运行测试。

`stage_evidence` 按两条验收要求逐条排列；以下独立参考来自解析公式与操作前快照，**没有外部工业参考内核认证**：

1. **常规机械夹具产生真实圆角/倒角几何与拓扑，公开查询、Strict 和独立参数参考通过。** [Ops 回归](../../tests/ops/ops_heal_test.cpp) `stage6_blend_geometry_regression`（:22）随必需测试通过。三轴分别使用尺度 0.1/1/10，覆盖四角单边及 2/3/4 条平行凸边与平移；不是三轴×三个尺度全部组合。通过 `point_of_vertex/curve_of_edge/edge_curve_interval/surface_of_face` 核对 V=`8+2n`、E=`12+3n`、F=`6+n`、零非流形边、源边面保持不变及 Strict==Ok。:106 起逐边核对两不同 coedge、两不同面，以公开 `edges_of_loop/vertices_of_loop` 证明两次相反使用；既有 `boundary_edge_count_of_body` 单壳返回 E，不能当作零开放边证据。圆角解析参考为 κ=`1/r`、弧长=`πr/2`、45°圆心位置/半径/切线、端点切触；Cylinder 主曲率 `(0,1/r)` 与圆弧中点解析 `closest_point`。倒角参考为邻面退让 d、斜边 `sqrt(2)d`、截面积 `ab−nd²/2`、体积 `L*section`、表面积 `2*section+L*[2(a+b)+n*(sqrt(2)−2)d]`，真实平面质量参考通过。**限制说明**：仅当前完整轴对齐矩形六平面闭壳的 1–4 条互不干涉平行凸边，输出为独立 Circle/Cylinder/Plane owned 闭壳；一般曲面、角区、连续二次处理、变半径/变距未支持。圆角质量/通用实体空间/无 PCurve 面面积仍不支持，:196 起 `NotImplemented/no value/AXM-CORE-E-0004/query.mass_properties.support_gate` 拒绝守卫通过；Rep 共享圆弧端盖/Cylinder 条带采样只解决此 Strict 直接阻断，非任意曲边采样认证。

2. **相交/退化/不支持边明确诊断，失败与活动事务回滚无污染。** [Query/Eval 回归](../../tests/eval/query_eval_test.cpp) `stage6_blend_eval_rollback_regression`（:17）及 [Topo 回归](../../tests/topo/topology_test.cpp) `stage6_blend_topology_guard_regression`（:23）随必需测试通过。前者覆盖空/非法/重复/外来边、NaN/Inf、非平行相交角区、相邻平行退让恰接触、亚容差、1e16 坐标舍入退化及 wedge 拒绝；断言 input/support/intersection/radius 或 distance/geometry 的稳定阶段与 BLEND 错误码。对照前后对象/几何/body/mesh/Eval/cache 数量、源边面/依赖/重算，成功后的保存点与 writer rollback 移除派生几何/缓存、保留源暖缓存并重试。后者注入支撑线 XYZ 偏移、NaN 顶点、反向 coedge、proxy 面、缺失 edge 索引、悬空 curve 六种损伤；NaN/proxy/悬空为 E-0003/support_gate，其余为 E-0006/validation，核对 next_id/next_version/关系索引/runtime/事务写计数/服务分配范围不污染，getter 非法/非finite/悬空及 rollback 失效也有断言；:56 起旧 proxy BlendResult 质量拒绝守卫通过。独立参考是调用前模型和运行时快照、非finite 输入及解析零剩余侧壁。**限制说明**：源和结果在私有暂存状态 Strict 检查，失败不发布、不消耗模型 ID、保留源索引而不静默修复；诊断记录允许增加。成功分配后回滚允许 ID 空档，遵循既有事务语义；不把未支持角区/一般曲面视为已可用，也不声称 validation 所有防御分支逐根因注入。

[API 支持矩阵](../api/AxiomKernel_详细模块接口清单.md#841-stage-6-真实圆角与倒角支持矩阵cycle-0088--s6-blend)、字典/文案/样例、需求矩阵、当前进度、Backlog 及主路线图本轮同步。FR-BLEND-001 记受限可用；Stage 6 进行中；仅在调度器完整门禁、最终文档检查及提交成功后记任务已验收。本轮不写自动进度台账或 result.json，不修改代码/测试/配置/脚本，不提交推送，不扩大后续阶段。

三个只读子 Agent 均已结束：`/root/api_review` 核对签名、当前边界和查询/采样支持范围；`/root/diagnostic_review` 核对稳定码/阶段及失败原子性；`/root/evidence_review` 核对完整门禁和两条验收证据、阶段状态。三者未修改文件、构建测试、写 result.json、提交推送或派生 Agent；主 Agent 为唯一文档写者。

本轮主 Agent 实际静态文档检查：`python3 scripts/check_docs.py` 检查 **35 个 Markdown、0 错误、0 警告**；补充检查 **9 个修改 Markdown、282 个本地链接、213 个标题/兼容锚点及代码围栏，0 错误**；`git diff --check` 通过。当前进度保留 Stage 5 旧锚点作为兼容入口，当前状态标题及本轮链接已指向 Stage 6。这些结果不替代调度器最终文档门禁或提交成功，本轮没有重跑构建/CTest。

### 1.16 cycle-0089 / S6-OFFSET-SHELL 门禁与逐项证据

本节保留cycle-0089 / S6-OFFSET-SHELL历史代码门禁与范围；当前唯一任务cycle-0090 / S6-DIRECT-EDIT见§1.17，不追认历史正式验收或提交。历史报告`stage_task_id=S6-OFFSET-SHELL`、`stage_outcome=ready_for_acceptance`，目标为增强偏置/抽壳并保持厚度与实际边界一致。依据 [cycle-0089-gates.log](../../.axiom-agent/logs/cycle-0089-gates.log)、实际 diff（含新 `src/axiom/ops/offset_shell_services.cpp`、`tests/ops/offset_shell_test.cpp`）和 develop/repair 报告，公开签名保持，结果由旧代理路径改为真实 owned 平面直边体。新增回归并入现有 `axiom_ops_heal_test`，Heal/Rep main 也实际调用新增回归，没有新增 CTest 名称。

首轮完整配置/构建成功，CTest **15/16、198.12 s**，唯一 Query/Eval 失败为 `expected eval graph to be invalidated by topology-changing operation`。repair 在私有 Strict、追加模型与事务登记后恢复既有成功 Eval 通知，失败仍隔离；`tests/eval/query_eval_test.cpp` 原失效/重算门禁未修改。repair 报告的提前定向 **4/4、0失败、181.71 s**（query_eval/ops_heal/heal/representation_io **6.04/181.70/2.72/29.69 s**）单列，不能替代最终全量。

| repair 后调度器完整门禁 | 真实结果 |
|---|---|
| 配置 / 完整构建 | 测试与示例开启；第二轮配置及 `cmake --build /workspaces/axiom/build-agent --parallel 4` 成功 |
| `axiom_ops_heal_test`（必需） | 通过，156.06 s |
| `axiom_heal_test`（必需） | 通过，0.91 s |
| `axiom_representation_io_test`（必需） | 通过，15.09 s |
| `axiom_query_eval_test`（关联修复） | 通过，1.96 s，既有门禁未改 |
| `axiom_perf_baseline_test` | 通过，1.69 s；既有基线，非偏置/抽壳工业性能认证 |
| 完整 CTest | `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error`，**16/16、0失败、200.60 s** |

首轮构建有18处既有 `Issue::numeric_evidence` 初始化告警（helpers17、kernel_plugin1）；最终增量构建未显示这些告警不证明全仓清零或独立 Strict warnings 门禁通过。日志未记录构建总耗时、最终文档门禁或提交成功。develop 的“全部未执行”和 repair 的“完整门禁尚待运行”均已由真实最终代码门禁取代；本轮仅同步文档，未重新构建或测试。

`stage_evidence` 按两条验收要求逐条排列，独立参考为解析公式、公开几何/OBJ 和调用前快照，**没有外部工业内核认证**：

1. **至少一个常见机械零件偏置/抽壳工作流有真实内外边界及厚度独立参考，成功结果通过 Strict。** [Ops 回归](../../tests/ops/offset_shell_test.cpp) `success_references/check_boundary` 覆盖0.1/1/10三尺度正负偏置、六方向单面开口、封闭内腔以及偏置→上开口机械盒。独立 outer-minus-inner 公式核对体积、面积、质心；逐个匹配真实内外角点、Plane 位置/法向、面面积和每边两次相反使用，断言 Strict、closedness、sources、腔内点 Outside 及 Material/Void 壳角色。4×5×6、t=0.5 闭腔参考 **V=60/A=242**，原盒上开口 **V=54/A=225**；偏置+0.5后5×6×7、同厚上开口 **V=80/A=331**。无开口为外6+反向内6双闭壳，单面开口为外5+内5+口沿4的闭合材料壳，不能把机械开口写成拓扑开壳。[Heal 回归](../../tests/heal/heal_test.cpp) `offset_shell_heal_regression` 核对 Strict、逐壳自交/trim及 ReportOnly 保持真实厚度；[Rep 回归](../../tests/rep/representation_io_test.cpp) `offset_shell_representation_regression` 独立读公开 OBJ 顶点、有向三角积分核对原盒两种抽壳 V/A、8个真实内角点、双壳2组件/单开口1组件及暖网格身份。三个必需回归随最终完整 CTest 通过。**限制说明**：仅当前完整 owned 轴对齐六平面矩形闭壳、无移除面或单面开口；一般曲面、多开口、修改式 Safe 修复保形不属于本工作流。

2. **自交、厚度过大或不支持曲面有阶段诊断，失败不泄漏部分模型，回滚后拓扑/表示/来源一致。** [Ops 回归](../../tests/ops/offset_shell_test.cpp) `failures_and_rollback/source_validation_failure` 覆盖负偏置塌缩 `self_intersection`、过厚 `thickness`、近容差 `cavity_tolerance`、一般曲面/多开口 `support_gate`、异属/重复/不存在移除面 `invalid_faces`、NaN/Inf/非法容差 `input_gate`、1e16坐标舍入/DBL_MAX溢出 `geometry_gate` 与错误 PCurve `source_validate`。两种源 Strict 拒绝分别核对 E-0011/E-0008；fine tol=1e-9、offset=-1.99999975 的约5e-7余宽暂存输出触发 **AXM-MOD-E-0011 / modify.offset.validate**。断言稳定 MOD 码/阶段、无 value、live 对象/几何/拓扑/表示/模型计数、next_id、源来源/索引、事务写数、Eval clean/dirty 状态及桥/重算计数、暖缓存保持。成功 offset/shell 各一次桥通知（body/batch/list各+1，此两节点链downstream+2），只使直接输入绑定链失效；失败保留已有状态。显式重算后继续活动事务成功→失败→rollback，检查结果几何/拓扑句柄、表示和缓存清除，源索引/来源及暖 Mesh 身份保留，再次 Strict 重试通过。[旧 Ops 回归](../../tests/ops/ops_heal_test.cpp) 保留多代历史来源退化隔离；既有 Query/Eval 门禁最终通过。**限制说明**：自交拒绝只证明认证盒域内塌缩/接触，不宣称一般曲面算法；抽壳输出 `.validate` 是防御合同，不称全部失败根因直接注入。成功桥可增加 Eval 指标，回滚允许成功 ID 空档，诊断可增加；回滚不承诺累计遥测倒退。

[API支持矩阵](../api/AxiomKernel_详细模块接口清单.md#831-stage-6-真实偏置与抽壳支持矩阵cycle-0089--s6-offset-shell)、MOD 字典/文案、样例、需求矩阵、进度、Backlog与主路线图本轮同步。FR-MOD-001受限可用，FR-BLEND-001保留§1.15历史受限事实，Stage 6进行中；最终文档门禁及调度器提交成功后才记任务正式已验收。本轮不写自动进度台账或result.json、不修改代码/测试/配置/脚本或 `.axiom-agent/`，不提交推送，不扩展直接编辑或后续阶段。

三个只读子 Agent 均已结束：`/root/api_review` 确认签名保持、真实 owned 边界/厚度和查询/样例范围；`/root/diagnostic_review` 确认 MOD 码/阶段、成功 Eval 通知及失败/事务隔离；`/root/evidence_review` 确认两轮门禁、两条逐项证据与阶段状态一致。三者未修改文件、运行构建测试、写result.json、提交推送或派生 Agent；主 Agent 为唯一文档写者。

本轮实际静态文档检查：`python3 scripts/check_docs.py` 检查 **35个Markdown、0错误、0警告**；补充检查 **9个修改Markdown、301个本地链接、226个标题锚点及代码围栏，0错误**；`git diff --check` 通过。此为主 Agent 文档检查，不补记为调度器最终文档门禁或提交成功；未重新构建或运行CTest。

### 1.17 cycle-0090 / S6-DIRECT-EDIT 门禁与逐项证据

当前主线为[主路线图§4.7](../plan/AxiomKernel_主开发计划与阶段路线图.md#47-stage-6-高级特征与直接编辑)的Stage 6；唯一退出任务`stage_task_id=S6-DIRECT-EDIT`、`stage_outcome=ready_for_acceptance`。依据[调度器门禁日志](../../.axiom-agent/logs/cycle-0090-gates.log)、实际diff（含新增`src/axiom/ops/direct_edit_services.cpp`、`tests/ops/direct_edit_test.cpp`）及develop报告：新增`move_face`，`replace_face`改为真实平行Plane替换，删除补面移除旧占位成功并显式拒绝。直接编辑回归并入既有Ops目标，Topology/Query-Eval/Runtime main均实际调用相应回归，没有新增CTest名称或扩大后续阶段。

| 调度器独立完整门禁 | 真实结果 |
|---|---|
| 配置 / 完整构建 | 一轮测试与示例开启的配置、`cmake --build /workspaces/axiom/build-agent --parallel 4`成功；无repair报告 |
| `axiom_ops_heal_test`（必需） | 通过，158.99 s |
| `axiom_topology_test`（必需） | 通过，0.46 s |
| `axiom_query_eval_test`（必需） | 通过，2.06 s |
| `axiom_kernel_runtime_invariant_test`（必需） | 通过，0.09 s |
| `axiom_representation_io_test`（关联） | 通过，14.62 s；新增直接编辑OBJ断言在Ops目标内 |
| `axiom_perf_baseline_test` | 通过，1.72 s；既有基线，非直接编辑工业性能认证 |
| 完整CTest | `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error`，**16/16、0失败、203.36 s** |

构建保留18处既有`Issue::numeric_evidence`初始化告警（helpers17、kernel_plugin1），不宣称全仓无告警或独立Strict warnings门禁通过。日志未记录构建总时长、最终文档门禁或调度器提交成功。develop中的“未构建/未执行、断言就绪”已由本次实际完整代码门禁取代；本轮只同步文档，未重新构建或运行测试。

`stage_evidence`按两条验收要求逐项排列，参考为独立解析公式、公开OBJ三角积分及调用前快照，**无外部工业内核认证**：

1. **冻结已支持编辑类型，至少一条真实直接编辑主路径完成编辑→验证→查询；几何变化与独立参考一致。** [Ops直接编辑回归](../../tests/ops/direct_edit_test.cpp)的`direct_edit_success_references/direct_edit_boundary_reference`覆盖0.1/1/10三尺度、六方向、正负移动/替换与反向替换法向；断言真实8角点/12边/6面/1壳、Plane位置/正确外法向、四邻面重裁面积、双向边使用、Strict/closedness/source、解析V/A/质心/中心惯性及bbox、Inside/Outside。`direct_edit_obj_reference`由公开OBJ顶点与三角独立积分核对当前表示V/A，覆盖单位尺度和连续机械链路，误差门槛1e-6；三尺度解析断言按尺度使用1e-7门槛。机械链路4×5×6上面+1得到4×5×7，**V=140/A=166**；再从当前结果选右面替换至x=4.5得到4.5×5×7，**V=157.5/A=178、质心=(2.25,2.5,3.5)**。[Query/Eval回归](../../tests/eval/query_eval_test.cpp)`direct_edit_query_eval_regression`核对z=6.5由源Outside变结果Inside、z=7为Boundary，完成真实编辑→Strict→质量/点查询。**限制说明**：冻结为完整独占owned轴对齐六Plane ExactBRep盒的单面move_face/平行Plane replace_face；微小离轴法向也拒绝。一般曲面、非轴对齐/共享/开放边界、批量编辑与删除补面不在支持域，删除补面明确结构化不支持；不认证通用工业直接编辑。

2. **编辑后表示/来源/Eval与当前拓扑一致，不支持与失败有结构化诊断，事务回滚恢复原结果。** Ops的`direct_edit_success_references`核对立即source_body/source_shell/全部六source_faces及逐面一对一来源，OBJ断言证明当前owned表示跟随真实编辑边界。`direct_edit_failure_isolation`覆盖无效/异属面、零/非有限/亚容差距离、塌缩/近容差/溢出、不可表示坐标、曲面/倾斜替换、无变化替换、删除补面拒绝，断言稳定码/阶段/no-value与live next_id、模型/几何/拓扑计数、来源、next_version、事务写数、Eval桥/重算指标和暖缓存保持。[Topology回归](../../tests/topo/topology_test.cpp)`direct_edit_topology_regression`以错误PCurve注入move的**E-0013/modify.move_face.source_validate**，微倾斜源/共享闭壳以E-0009/support_gate拒绝；编辑结果随后删面导致Strict失败，回滚恢复。Query/Eval的`direct_edit_query_eval_regression`覆盖成功输入绑定节点/下游失效、失败保持重算状态、保存点回滚及过时历史来源隔离后的连续编辑。[Runtime回归](../../tests/sdk/kernel_runtime_invariant_test.cpp)`direct_edit_runtime_rollback_regression`对移动/替换均核对活动事务回滚清除派生几何/拓扑/网格/评估缓存与体绑定、使消费节点失效、源暖MeshId/原几何/来源恢复、runtime/index一致。四必需目标随本次完整门禁通过。**限制说明**：源/结果均私有Strict，历史来源仅暂存隔离后恢复；失败不消耗live模型ID，成功只追加新记录、登记事务范围并通知输入Eval及下游。服务内部结果`.validate`为防御合同，本批没有直接注入两种操作的输出验证失败；随后删面后的Strict失败不代表该分支故障注入。成功ID可空档、诊断可增加、累计遥测不承诺倒退；消费者节点保留，移除的是派生体绑定。

[API支持矩阵](../api/AxiomKernel_详细模块接口清单.md#832-stage-6-真实直接编辑支持矩阵cycle-0090--s6-direct-edit)、MOD字典/文案、样例、需求矩阵、进度、Backlog与主路线图同步本批事实。FR-MOD-001受限可用；cycle-0088 Blend与cycle-0089偏置/抽壳保留历史范围及门禁，Stage 6进行中。完整代码门禁已通过；最终文档门禁与调度器提交成功未记录，不记正式已验收或Stage 6已退出。本包停止功能扩展，不启动后续阶段。

本轮三个只读子Agent均已结束：`/root/api_review`确认实际公开签名、独占盒域/严格平行Plane及样例参考；`/root/diagnostic_review`确认新旧MOD码、完整阶段、失败原子性与回滚合同，并区分结果validate防御分支；`/root/evidence_review`确认一轮完整门禁、四必需目标、两条逐项证据与阶段状态。三者未修改文件、构建测试、写result.json、提交推送或派生Agent；主Agent为唯一文档写者，只修改docs下Markdown。

本轮主Agent静态文档检查：`python3 scripts/check_docs.py`检查**35个Markdown、0错误、0警告**；补充检查**9个修改Markdown、318个本地链接、238个标题锚点及代码围栏，0错误**；`git diff --check`通过。此为本轮文档检查，不补记为调度器最终文档门禁或提交成功；未重新构建或运行CTest。

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
