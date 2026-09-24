# AxiomKernel 近期迭代与 Backlog

本文档承接《当前开发进度》中的“近期执行项”，把**当前迭代焦点、本阶段 backlog、下一未闭合批次、长期能力树**集中到一个更适合持续刷新的入口。事实状态仍以 `docs/plan/AxiomKernel_当前开发进度.md` 为准；总路线见 `docs/plan/AxiomKernel_主开发计划与阶段路线图.md`。

## 1. 当前阶段口径

当前阶段保持为：

`Stage 1.5 / Stage 2 过渡：核心公共层增强 + 基础几何/拓扑深化`

## 2. 当前迭代焦点

### 2.1 `diag + ops`

- **NFR-DIA-001 第 45 切片**：glTF 导入的物化前失败绑定 `io.import.gltf.input/path/open/read/parse/validation`，目录输入在读取前结构化拒绝；`axiom_io_workflow_test` 覆盖路径/解析/网格验证失败、阶段检索、JSON、Body/Mesh 不污染及合法重试。复用现有 IO/VAL 错误码，需求保持受限可用；其他 BOOL/HEAL/IO 失败出口仍待闭合。

- **FR-DIAG-001 第 44 切片**：问题码前缀检索先确定匹配报告并按 `DiagnosticId` 升序排序，再应用 `max_results`；空报告/空结果、重复匹配 issue、限额、非法参数、源报告不污染与重试进入 `axiom_diagnostics_test`。需求保持受限可用；后续继续补齐高风险流程的阶段、实体和数值证据。

- **NFR-DIA-001 第 40 切片**：STL 导入的物化前输入/路径/打开/读取/解析/网格验证失败分别绑定 `io.import.stl.input/path/open/read/parse/validation`；非普通文件在读取前拒绝。`axiom_io_workflow_test` 覆盖稳定错误码、阶段检索、JSON、Body/Mesh 不污染及失败后重试。复用现有 IO/VAL 错误码，需求仍为受限可用；其他 BOOL/HEAL/IO 失败出口仍待闭合。

- **FR-DIAG-001 第 39 切片**：阶段精确与前缀检索先按 `DiagnosticId` 升序确定匹配集合，再应用 `max_results`，避免无序存储遍历使限额结果不稳定。`axiom_diagnostics_test` 覆盖匹配/空结果、限额、非法参数、源报告不污染及拒绝后重试。复用现有错误码；需求保持受限可用，全部高风险流程的阶段、实体和数值证据仍待补齐。

- **NFR-DIA-001 第 35 切片**：OBJ 导入的空路径、缺失文件、非普通文件/打开失败、解析失败与退化三角形分别绑定 `io.import.obj.input/path/open/parse/validation`；非普通文件在读取前拒绝，避免目录读取抛出裸异常。`axiom_io_workflow_test` 覆盖阶段检索、JSON、Body/Mesh 不污染及失败后成功重试。复用现有 IO/VAL 错误码，不扩大 OBJ 格式支持范围，需求仍为受限可用。

- **FR-DIAG-001 第 34 切片**：单报告 `export_report` / `export_report_json` 显式关闭并检查输出流，设备写入/关闭失败不再误报成功；空路径、目录打开失败与 Linux `/dev/full` 均返回结构化失败。`axiom_diagnostics_test` 覆盖成功完整证据、参数/打开/写入失败、源报告不污染及失败后重试。复用 `AXM-IO-E-0005`，无公开签名变化；需求仍为受限可用。

- **NFR-DIA-001 第 30 切片**：STEP 导入的空路径、文件不存在与非可读常规文件分别绑定 `io.import.step.input/path/open`；失败均发生在 Body ID 分配和存储写入之前。`axiom_io_workflow_test` 覆盖阶段检索、JSON、退化输入、模型不污染和失败后成功重试。复用 `AXM-IO-E-0004`，无公开签名变化；需求仍为受限可用，不扩展 STEP 实体交换范围。

- **FR-DIAG-001 第 29 切片**：STEP 导出的无效 Body/空路径、路径校验、打开与最终写入失败绑定 `io.export.step.input/path/open/write` 及输入 Body；显式检查写入/关闭状态，避免 `/dev/full` 误报成功。`axiom_io_workflow_test` 覆盖成功、退化输入、路径/设备失败、阶段检索、JSON、模型不污染和重试。复用 `AXM-IO-E-0005`，无公开签名变化；需求仍为受限可用，其他 IO/BOOL/HEAL 失败出口仍待闭合。

- **NFR-DIA-001 第 25 切片**：体级、壳级与批量壳级自交验证失败绑定 `heal.validate_self_intersection.*` 细分阶段及 Body/Shell。`axiom_heal_test` 覆盖正常 Strict、非法 Body/Shell、异属 Shell、空批量、阶段检索、JSON 和失败不污染。复用现有错误码且无公开签名变化；网格 SAT 仍为近似验证，需求保持受限可用，其他 BOOL/HEAL/IO 失败出口仍待系统闭合。

- **FR-DIAG-001 第 24 切片**：阶段聚合文本/JSON 导出显式检查最终写入与关闭状态，避免设备写入失败误报成功；空路径在打开文件前拒绝，打开/写入失败复用 `AXM-IO-E-0005`。`axiom_diagnostics_test` 覆盖正常阶段、空阶段 `(unset)`、空路径、目录、Linux `/dev/full`、参数失败不截断、源报告不变及失败后重试。无公开签名或错误码变化，需求仍为受限可用。

- **FR-DIAG-001 第 19 切片**：严格网格导出 QA 的 `AXM-IO-E-0006` 失败绑定 `io.export.mesh_strict_qa` 与输入 Body，补齐越界索引、退化三角形、阶段检索、JSON 证据及失败不污染回归；同时修正文档中该稳定错误码的旧语义。无公开签名变化，需求仍为受限可用。

- NFR-DIA-001 第 15 切片：`export_boolean_prep_stats` 显式关闭并检查输出流，避免写入失败返回成功；参数、文件打开、写入失败分别绑定 `bool.prep.export.input/open/write` 和 `[lhs, rhs]`。参数失败复用 `AXM-BOOL-E-0001`，文件打开失败由误用的 BOOL 输入码修正为 `AXM-IO-E-0005`，写入失败复用该 IO 码。`axiom_boolean_prep_test` 覆盖重叠/相同/分离/接触零体积输入、无效句柄/空路径、目录打开失败、Linux `/dev/full`、阶段/错误码检索及 JSON、参数失败文件不污染、模型计数与 Eval 失效不污染和失败后重试。无公开签名变化；设备写入失败不保证目标文件恢复，需求保持受限可用。

- **FR-DIAG-001 第 14 切片**：补齐指定 ID 批量文本归档，复用单报告文本格式保留完整 Issue（严重级别、码、消息、阶段、实体），保留输入顺序、重复 ID 和特殊字节；打开文件前拒绝任意位置的无效 ID，参数失败不创建/截断目标且源报告不变；显式关闭并检查写入错误，复用现有 CORE/IO 错误码，无公开签名变化。`axiom_diagnostics_test` 覆盖成功、空报告/空阶段/重复 ID、非法参数、路径失败、Linux `/dev/full` 写入失败及拒绝后重新导出。设备写入失败不保证目标文件恢复；需求仍为受限可用，全部重量级流程阶段/实体/数值证据待补齐。

- NFR-DIA-001 第 10 切片：`BooleanService::run` 拒绝未声明的 `BooleanOp` 值，返回 `InvalidInput` / `AXM-BOOL-E-0001`、`bool.input` 与输入实体，避免越过分支后静默创建结果体。`axiom_boolean_prep_test` 覆盖两种诊断模式下的负值/越界值、相同输入体及无效句柄、阶段/错误码检索与 JSON、模型计数及 Eval 失效传播不污染，并验证拒绝后四种合法枚举仍可执行。复用已有错误码，无公开签名变化；需求仍为受限可用，不提升现有 bbox 布尔为精确能力。

- FR-DIAG-001 第 9 切片闭合诊断批量 JSON 归档缺口：指定 ID 导出保留完整 Issue 证据与字符串，参数失败不污染目标文件或源报告；空报告/重复 ID/无效 ID/路径失败由 `axiom_diagnostics_test` 覆盖。DoD：与单报告结构一致，失败有稳定错误码；全部流程阶段覆盖仍待推进。

- 扩展 BOOL 阶段化诊断覆盖与 JSON `stage` 字段在更多失败分支中的一致性。FR-DIAG-001 预处理告警切片已闭合：`W-0001/W-0002` 绑定 `bool.prep` 与输入/输出体，覆盖重叠、分离、仅接触、无壳级候选及前置失败不污染回归；全部失败分支门禁仍待补齐。
- NFR-DIA-001 早期失败最小诊断切片已闭合（完整测试 16/16 通过）：关闭详细布尔诊断时，无效输入、分离交集及包含导致空结果的失败仍保留错误码、阶段与输入实体；两种模式的检索、JSON 和失败不污染由 `axiom_boolean_prep_test` 覆盖。全部失败分支门禁仍待补齐。
- 将布尔真求交子里程碑拆为“面级候选 -> 交线 -> imprint”可回归切片。

**DoD**

- 对应错误码/诊断码可检索。
- `axiom_boolean_workflow_test` 与 `axiom_boolean_prep_test` 不回归。

### 2.2 `math + geo`

- FR-GEO-001 第 46 切片：3D 复合折线最近参数逐段解析投影并比较三维距离，使用扩展精度中间量避免有限大坐标平方溢出；等距取最早参数，重复控制点安全参与比较。`axiom_geometry_test` 覆盖窄分支、段内投影、退化段、大坐标、非法查询/句柄、稳定错误码和失败不污染。需求保持受限可用；其他曲线/曲面最近点的全局精度合同待完成。
- FR-GEO-001 第 41 切片：UV 折线 PCurve 的最近参数改为对每段做解析投影和距离比较，覆盖固定网格容易漏掉的短分段、重复控制点和等距最早参数。`axiom_geometry_test` 覆盖成功、非法查询/句柄、稳定错误码、失败不污染和重试；需求仍为受限可用，其他曲线/曲面最近点的全局精度合同待完成。
- FR-GEO-001 第 36 切片：椭圆 `closest_parameter/closest_point` 从粗采样初值阻尼细化三维欧氏距离，并把周期缝归一到 `[0, 2pi)`；`make_ellipse` 在写入前拒绝轴长或派生法向长度溢出。`axiom_geometry_test` 覆盖解析最近点、周期缝、非有限查询、有限但溢出的轴向量和几何/缓存不污染；不宣称任意退化椭圆的全局最优，需求仍为受限可用。
- FR-GEO-001 第 31 切片：BSpline/NURBS 曲面内部满重数断点统一选择右侧非空片，上端点及域外钳制统一选择左侧非空片，且点值、一二阶偏导和曲率使用同一单侧语义。`axiom_geometry_test` 覆盖双轴断点、非单位权重、常值退化片、非法结点失败不污染及既有曲面继续求值；不宣称断点全局可微，需求仍为受限可用。
- FR-GEO-001 第 26 切片：BSpline/NURBS 曲面最近参数初值搜索除全域网格外逐个覆盖非空张量积结点片，避免极窄及双轴满重数隔离片被漏采。`axiom_geometry_test` 覆盖多项式/非单位权重有理曲面、成功、非法查询与几何/缓存不污染；不承诺任意曲面全局最优，需求仍为受限可用。
- FR-GEO-001 第 21 切片：补齐 BSpline/NURBS 曲面 u/v 显式结点的非零有效域与 `degree + 1` 重数上限校验，非法输入返回 `AXM-GEO-E-0002` 且不污染几何/缓存；保留合法满重数断点的单侧曲面片求值。`axiom_geometry_test` 覆盖两轴、两类曲面、成功、退化、失败与失败后继续求值，需求仍为受限可用。
- FR-GEO-001 第 16 切片：BSpline/NURBS 最近参数初值搜索在全域粗采样之外逐个采样非空结点分段，避免极窄合法分段和满重数断开的分支被跳过。`axiom_geometry_test` 覆盖显式/推断次数、非单位权重、常值退化分段、非法查询点诊断及几何/缓存不污染。该增量不宣称任意曲线全局最优或工业精度，需求仍为受限可用。
- 收敛退化/大尺度下的谓词与容差行为定义。
- FR-GEO-001 第 11 切片：修复非夹持 BSpline/NURBS 有效域端点重复结点导致选中零长度分段的问题。DoD：端点点值、一二阶导数及曲率使用非空单侧分段；覆盖显式/推断次数、非单位权重、域外钳制、常值退化、非法重数诊断及模型/缓存不污染。回归入口为 `axiom_geometry_test`，需求仍为受限可用。
- 推进样条导数/曲率或稳定最近点的最小增量。显式样条结点逆序/零长度有效域拒绝切片已闭合；结点重数超过 `degree + 1` 的拒绝切片已闭合，覆盖端点/内部超限、显式/推断次数、失败不污染及合法满重数断点单侧一、二阶导数（含非单位权重 NURBS）；后续继续完善其他高重数结点处的导数/曲率语义。

**DoD**

- 失败具备稳定错误码。
- `axiom_math_services_test` 与 `axiom_geometry_test` 覆盖新增语义。

### 2.3 `topo`

- **NFR-REL-001 第 48 切片**：`create_shell` 在分配 ShellId 前验证成员面引用的曲面确实存在；空句柄或悬空 `SurfaceId` 返回 `InvalidTopology / AXM-TOPO-E-0005`，关联面与曲面 ID。`axiom_topology_test` 通过受损面注入覆盖诊断 JSON、ID/存储/事务计数不污染，以及恢复曲面引用后的重试、回滚和提交。需求保持受限可用；协作式取消与更广泛的 S0/S1 失败注入仍待闭合。

- **FR-TOPO-001 第 47 切片**：`create_face` 在分配 FaceId 前拒绝不同环中独立 `VertexId` 的三维坐标精确重合，`validate_face` 对已有面执行同一规则；`AXM-TOPO-E-0025` 关联两环与两个顶点。`axiom_topology_test` 覆盖外/内及内/内相接、成功双孔面、诊断 JSON、失败不污染和回滚。仍缺边段相交、容差邻近相接与完整 trim bridge，需求保持受限可用。

- **NFR-REL-001 第 43 切片**：`create_shell` 在分配 ShellId 前检查成员面全部内环，拒绝缺失或已损坏的内环，复用 `AXM-TOPO-E-0005` 并关联面和内环。`axiom_topology_test` 覆盖故障注入、诊断 JSON、ID/存储/事务计数不污染，以及修复输入后的重试、回滚与提交。需求保持受限可用，协作式取消和更广泛的 S0/S1 失败注入门禁仍待闭合。

- **FR-TOPO-001 第 42 切片**：`create_face` 在分配面 ID 前拒绝同一面的不同边界环共用 VertexId，`validate_face` 对存量面执行同一规则；使用 `AXM-TOPO-E-0024` 关联两环与冲突顶点。`axiom_topology_test` 覆盖外/内环及内/内环相接、合法双孔面、诊断 JSON、失败不污染和回滚。规则只识别拓扑顶点 ID，几何自交与完整 trim bridge 仍待补齐，需求保持受限可用。

- **NFR-REL-001 第 38 切片**：`create_body` 在包围盒校验成功后才分配 BodyId；受损壳导致的失败保留全局实体 ID、存储和事务计数。`axiom_topology_test` 以受损空壳注入覆盖错误码/JSON、非法句柄、回滚与成功重试。需求保持受限可用，完整取消和失败注入门禁仍待闭合。

- **FR-TOPO-001 第 37 切片**：`create_face` 在写入前执行已存在的面绑定环边数规则：外/内环至少三条共边，保留同空间曲线双弧环例外；不合格外/内环分别返回 `AXM-TOPO-E-0003/0004` 并关联环。`axiom_topology_test` 覆盖双直线闭合退化环、成功三角环重试、已有同曲线双弧环、诊断 JSON、失败不污染与回滚。需求仍为受限可用，几何自交及完整 trim bridge 尚待补齐。

- **NFR-REL-001 第 33 切片**：落实 `SnapshotSerializable` 已声明的单写约束；同一内核已有活动拓扑事务时，重叠事务以关闭状态返回，其写入、提交和回滚均失败且不能污染所有者状态。所有者提交/回滚或空事务析构后释放写槽，后续事务可重试。`axiom_topology_test` 覆盖重叠拒绝、所有者提交、拒绝者失败不污染、回滚重试和空事务释放。需求保持受限可用，仍不宣称跨进程 SERIALIZABLE 或完整协作式取消。

- FR-TOPO-001 第 32 切片：`create_loop` 在写入前拒绝闭合终点之外重复经过同一 `VertexId` 的自接触非简单环，返回 `InvalidTopology` / `AXM-TOPO-E-0023` 并关联重复顶点与两条冲突定向边。`axiom_topology_test` 覆盖拒绝、JSON、存储/反向索引/事务计数不污染、拒绝后合法三角环重试与回滚。新增稳定错误码与文档映射，未将几何自交检测或周期 seam 扩大为本切片能力，需求保持受限可用。

- NFR-REL-001 第 28 切片：修复 `replace_surface` 成功后未计入事务写操作的可靠性缺口；仅执行曲面替换的活动事务现在会在析构时自动回滚，提交后总写入数与 `replaced_surfaces` 分项一致。`axiom_topology_test` 覆盖非法替换失败不污染、仅替换作用域回滚、显式回滚和提交审计。无公开签名或错误码变化，需求保持受限可用。

- FR-TOPO-001 第 27 切片：`validate_edge` 使用内核线性容差检查两个拓扑端点是否位于引用的 3D Curve 上；偏离或最近点求解失败返回 `InvalidTopology` / `AXM-TOPO-E-0008`，诊断关联边、曲线与问题顶点。`axiom_topology_test` 覆盖成功、容差内端点、明显偏离、诊断 JSON、验证不污染和回滚；同时修正一处既有跨环测试夹具中与端点不一致的直线方向。无公开签名或错误码变化，需求保持受限可用。

- NFR-REL-001 第 23 切片：活动 `TopologyTransaction` 未显式关闭便离开作用域时由 `noexcept` 析构自动回滚，防止创建、删除和替换直接泄漏到共享 store；空事务、已关闭事务和移动后的源对象析构不改变模型。`axiom_topology_test` 覆盖创建、既有面替换、级联删除、移动目标回滚、显式提交持久性和索引不变量。需求保持受限可用，完整隔离、协作式取消和全量 S0/S1 门禁仍待闭合。

- FR-TOPO-001 第 22 切片：`validate_shell_closedness` 将“只有一个面连通分量”纳入 Strict 壳闭合性合同；两个各自闭合但互不共享边的分量返回 `InvalidTopology` / `AXM-TOPO-E-0018`，诊断关联壳和各分量代表面。`axiom_topology_test` 覆盖单分量成功、双闭合分量失败、诊断 JSON、失败不污染及回滚。未增加公开签名或错误码，需求保持受限可用。

- NFR-REL-001 第 18 切片：收紧 `TopologyTransaction` 唯一所有权。事务只能移动构造，禁止复制和移动赋值；移动后源对象保持可查询的关闭状态，不能写入、提交或回滚，避免默认移动留下活动源对象并发生空状态访问或误撤销。`axiom_topology_test` 覆盖编译期所有权约束、目标提交/回滚、源对象重复关闭操作、失败不污染和已提交实体存续。需求保持受限可用，完整隔离与取消仍待交付。

- FR-TOPO-001 第 17 切片：`validate_shell_closedness` 在边恰由两个不同面使用时进一步要求两侧 coedge 的 `reversed` 相反；同向配对返回 `InvalidTopology` / `AXM-TOPO-E-0015`，关联壳、边、两面与两 coedge。`axiom_topology_test` 覆盖同向失败、反向成功、零厚度双面退化壳、JSON 诊断、验证失败不改变拓扑/事务写计数及回滚。未增加公开签名或错误码，需求保持受限可用。

- NFR-REL-001 第 13 切片：修复同一事务新建面/壳/体被修改或删除后，回滚从快照复活新建实体的问题（S0：回滚污染）。先恢复快照再清理本事务创建的高层拓扑，保留已有快照/触达计数语义。`axiom_topology_test` 覆盖新建面替换曲面、面/壳/体删除、空壳/空体级联删除、已有面与新建共享壳混合快照、重复删除失败不污染、全部新建拓扑句柄失效、原模型/反向索引恢复、空回滚及后续提交。无公开 API 或错误码变化；需求仍为受限可用，完整隔离与取消仍待交付。

- FR-TOPO-001 第 12 切片：`create_face` 在写入前拒绝同一面外环/内环及内环之间复用 EdgeId，复用 `AXM-TOPO-E-0014` 并关联两个冲突环与边。`axiom_topology_test` 覆盖单边共享、完全重合边界、正反共边、内环顺序、诊断 JSON、计数/索引不污染、拒绝后合法双孔面及回滚后重新提交；不同面通过独立共边共享 Edge 仍允许。未增加公开签名或错误码，未扩展 seam/周期修剪支持，需求保持受限可用。

- NFR-REL-001 第 8 切片：保护事务撤销记录，活动事务（含空事务）拒绝 `clear_tracking_records`；提交/回滚后允许幂等清理。DoD：稳定错误码 `AXM-TX-E-0006` 可检索/导出，重复拒绝不改变模型与计数，创建/删除及 PCurve 修改仍可回滚，关闭后清理不改变模型；由 `axiom_topology_test` 回归。

- 继续加严 Strict 规则。拓扑创建入口的有限坐标检查切片已闭合：`create_vertex` 拒绝 NaN/±Inf，失败不写入模型或事务计数，并覆盖诊断 JSON 导出与回滚。
- FR-TOPO-001 单共边环闭合检查切片已闭合（完整测试 16/16 通过）：移除单共边放行，按定向端点 ID 校验闭合；覆盖正反方向、坐标重合但 ID 不同的退化输入、失败不污染、后续闭合三角环与回滚、`E-0002` JSON 导出。
- 推进 trim bridge 的可物化子规则。NFR-REL-001 共边 PCurve 绑定回滚切片已闭合：已有共边重复绑定/清除后回滚恢复原值，无效句柄失败不污染，提交保留新值；由 `axiom_topology_test` 回归。

**DoD**

- `axiom_topology_test` 有新增失败类回归。
- 对应诊断码可稳定导出。

### 2.4 `heal + io`

- NFR-DIA-001 第 20 切片：`validate_geometry` 的非法目标、bbox、owned B-Rep 引用、Strict 参数域/有限值/近重复顶点/退化边面/面法向失败统一绑定 `heal.validate_geometry.*` 细分阶段，并关联目标 Body 与已有问题子实体。`axiom_heal_test` 覆盖成功、非法句柄、Strict 退化、阶段检索、JSON 导出及模型计数不污染；复用现有错误码，无公开签名变化，需求保持受限可用。

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
