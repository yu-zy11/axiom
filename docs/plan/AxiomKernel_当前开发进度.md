# AxiomKernel 当前开发进度

> **第 73 切片（FR-TOPO-001，专项门禁闭合）**：跨环边界冲突从单段 Line/LineSegment 扩展到带显式裁剪区间的 CompositePolyline 与仅含线性子曲线的 CompositeChain；按真实参数方向精确拆分线性片段，支持递增/递减区间，并保留“复合曲线内部折点 ≠ 拓扑边端点”语义。`first_boundary_conflict` / `create_face` / `validate_face` 继续共用同一精确线段谓词；回归覆盖内部相交、共线重叠、容差邻近、只读预检、建面零污染与存量面验证。完整构建、Geo/Topo/Query/Core 定向回归 4/4 及除既有超长 `axiom_ops_heal_test` 外的 CTest **15/15 通过、0 失败、总耗时 25.57 s**，文档检查 35/35 通过；圆锥曲线/样条的误差受控求交仍未闭合。

> **第 72 批（FR-TOPO-001 / FR-QUERY-001，专项门禁闭合）**：公开 `create_trimmed_edge` 与 `edge_curve_interval`，在 EdgeId 分配前校验参数有限非零、曲线定义域和参数端点与 v0/v1 的容差一致性；显式裁剪曲边的长度查询复用真实曲线区间，解析区间极值或样条控制点凸包进入拓扑 bbox。旧 `create_edge` 兼容，未携带区间的曲边继续拒绝弦长冒充弧长；失败不写事务计数或几何求值缓存。完整构建与文档检查通过；除既有超长 `axiom_ops_heal_test` 外的 CTest **15/15 通过、0 失败、总耗时 29.61 s**，该 Ops 长矩阵未因本批重跑。

> **第 71 批（FR-GEO-001 / FR-OPS-001 / FR-QUERY-001，验收中）**：公开曲面 `closest_point_detailed` 的完整有界域精度/预算/下界合同，覆盖样条结点片、修剪孔、派生/偏置面并为可认证平面路径增加无缓存快路；`revolve` 支持轴分离孔洞，`loft` 支持拓扑兼容的凹/带孔多截面真实闭壳；新增 `body_shell_regions`，多壳质量属性按材料/空腔包含深度加减，并以 `AXM-QUERY-E-0006` 拒绝相交、重叠或容差接触。修复验收中发现的窄 knot 最近点精修、修剪平面预算耗尽和单壳重复空间分类。当前构建、`axiom_geometry_test`、`axiom_query_eval_test` 通过；闭合样条 48/48 与新增放样/带孔旋转隔离矩阵通过。完整 `axiom_ops_heal_test` 因既有大矩阵累计耗时在 10 分钟审计窗口内未完成，故本批尚未宣称全量门禁闭合。

本文档用于记录 `AxiomKernel` 当前阶段的实际开发状态、已完成内容、当前风险和下一阶段执行重点。

> **第 70 批（NFR-DIA-001 / FR-OPS-001 / FR-QUERY-001）**：AXMJSON、Axiom IGES 元数据与 Axiom BREP JSON 子集在物化前完成普通文件、64 MiB/短读、严格结构和几何验证，失败绑定 `io.import.<format>.input/path/open/read/parse/validation` 且不污染 Body/Mesh/ID；`SweepService` 新增首尾 G1 连续闭合样条、开放/闭合 `CompositeChain` 导轨和部分角多边形旋转的真实多面体闭壳；`TopologyQueryService` 新增 `shell_mass_properties/body_mass_properties`，从平面直边双边流形闭壳的当前拓扑重算单位密度体积、面积、质心和惯性。repair 修正了测试入口误用、早期 AXMJSON 兼容、扫掠弦段局部邻域判定、闭合复合导轨夹具和旋转闭壳绕向翻转后重算。调度器独立完整构建成功，最终 `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` **16/16 通过、0 失败、总耗时 1539.20 s**（`axiom_ops_heal_test` 1510.78 s，`axiom_io_workflow_test` 14.96 s，`axiom_query_eval_test` 0.17 s，性能基线 1.90 s）。本批门禁已闭合，第 60/61/62/65 等既有包不再有“待验收”状态。剩余限制：扫掠/旋转仍为保守采样多面体 BRep；带孔旋转、嵌套复合导轨、显式轮廓历史、曲面/曲边质量积分、空腔/相交多壳和标准 IGES 实体仍不支持。

> **第 69 批（FR-GEO-001 / FR-TOPO-001 / NFR-REL-001 / FR-DIAG-001）**：公开曲线全域最近点详细查询与精度/预算/收敛证书；公开跨环有限直线边界冲突预检并补齐共线重叠、容差邻近错误码；拓扑事务新增协作式取消、写者状态和累计取消审计；诊断新增结构化数值证据、证据覆盖审计与 JSON 导出，BOOL 受覆盖失败分支已接入门禁。调度器首次完整 CTest 为 14/16 通过（`axiom_geometry_test`、`axiom_query_eval_test` 失败），repair 以二阶保守距离下界和证书后的剩余预算精修修复两项回归；随后独立完整构建成功，最终 `ctest --test-dir /workspaces/axiom/build-agent --output-on-failure --no-tests=error` **16/16 通过、0 失败、总耗时 126.00 s**（性能基线 1.63 s）。构建日志仍有 `Issue::numeric_evidence` 聚合初始化缺失及一处既有未使用参数告警，但未导致构建或测试失败。本批门禁已闭合。需求仍为受限可用：曲面全域最近点证书、一般曲线跨环求交、抢占式/长流程内部取消，以及 HEAL/IO 全失败分支数值证据尚未闭合。

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

`Stage 1 已达成；Stage 2 可测基线已达成；当前为 Stage 2 深化与 Stage 3 准备：基础建模与查询分析为主线`

Stage 3 已有多条可回归的子路径，但统一支持矩阵、跨模块一致性与完整退出门禁尚未收口，因此不宣称“Stage 3 已完成”。

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
| **diag** | 检索、JSON、阶段/实体/数值证据、批量归档与证据审计已具备，**HEAL/IO 全分支覆盖**仍不足。 |
| **math** | 本阶段退化/尺度/容差谓词已在 `axiom_math_services_test` 固化，**全链路热点统一入口**仍待持续收敛。 |
| **geo / topo** | 曲线全域最近点证书、有限直线跨环冲突、Strict/trim 与可取消事务持续加严，**曲面证书、一般曲线 trim 和完备规则集**仍差。 |
| **rep / io** | 三角化、报告、子集格式与数据集门禁具备基础，**标准实体级互操作与误差预算工业闭环**仍差。 |
| **ops** | 布尔/特征仍以过渡与占位为主，**最大工业化缺口**。 |
| **heal** | 验证/修复/自交/容差等路径可回归且已拆独立测试，**BRep 全场景与可回放工业修复**仍差。 |
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
- **布尔**：尚未形成工业级求交/分类/重建闭环，但已具备面级候选、解析交线/交线段、交线入库、矩形面 imprint 原型及阶段化诊断与 `Issue.stage`（见 `axiom_boolean_workflow_test`），整体仍为过渡实现而非商用布尔引擎
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
| **ops** | 低 | **极高** | `axiom_boolean_workflow_test`、`axiom_boolean_prep_test`、`axiom_ops_heal_test` | **布尔真闭环**、特征建模真实拓扑、圆角倒角算法 |
| **heal** | 中偏低 | 高 | `axiom_heal_test`、`axiom_ops_heal_test`、`axiom_io_workflow_test` | 自交、流形性完备、容差冲突与小特征工业修复与回放 |
| **eval** | 中 | 高 | `axiom_query_eval_test` | 与真实建模/重建深度耦合、缓存与指标门禁 |
| **diag** | 中偏高 | 中偏高 | `axiom_diagnostics_test` | 阶段化（BOOL/HEAL/IO）**全覆盖绑定**仍不足；但诊断检索/统计/JSON 导出与 related_entities 回流在回归中已相对扎实 |
| **plugin & sdk** | 低～中偏低 | 高 | `axiom_smoke_test`、`axiom_plugin_sdk_test`、`axiom_diagnostics_test`（插件注册失败诊断） | **OS 级隔离/安全**仍未具备；动态加载与签名校验；长期兼容性策略需产品化 |

### 模块完成度与不足（详细版，可转 backlog）

说明：本节把“工业化差距”展开成**可执行缺口**。每条缺口建议具备 DoD：实现 + 诊断 + 回归（必要时含性能门禁）。

- **core（Kernel/State/Result/Stores）**
  - **已具备**：`Kernel` 门面、基础配置写入/查询、运行时 store 清理/重置、对象计数与能力报告框架；**`io_supported_formats` / `io_can_import_format` / `io_can_export_format` 已与 `IOService::detect_format`、`import_auto`、`export_auto` 对齐**，并由 `axiom_smoke_test` 回归；**`runtime_store_counts`**（含 **`tessellation_metrics` 快照**）/ **`reset_runtime_stores`** 与 **`topology_version_next` / `topology_commit_audit`（成功提交次数、写操作累计、**`TopologyCommitWriteBreakdown` 上次/累计分项**，由 `TopologyTransaction::commit` 写回 `KernelState`；**`topology_commit_breakdown_created_entities_total`** 与能力报告 **`Core.Topology.Audit.Cumulative.CreatedEntities`** 同源）/ `eval_graph_metrics`（节点/失效/重算、**依赖出边节点数/依赖边总数/体绑定记录数与引用总数** + 内嵌 `EvalGraphTelemetry` + **`EvalInvalidationBridgeMetrics`**（`detail::invalidate_eval_for_*` 引擎入口计数 + **`downstream_invalidation_steps`**：`invalidate_eval_downstream` 每次入口，与 SDK `invalidate` / `invalidate_many` 共用，与 Heal 等非 SDK 路径对齐）；能力报告含 **`Core.EvalGraph.Telemetry.*`**（含 **`InvalidateManyBatches` / `InvalidateManyNodeTotal`**）、**`Core.EvalGraph.Bridge.*`**（含 **`DownstreamInvalidationSteps=`**）、**`Core.EvalGraph.DependencyEdges` / `BodyBindingRecords` / `BodyBindingRefs`**、**`Core.Topology.Audit.Cumulative.CreatedEntities`**）** / **`export_topology_commit_audit_json` / `export_eval_graph_metrics_json` / `export_runtime_observability_json`**（合并快照与分项导出字段一致；**`Core.Export.RuntimeObservabilityJson=1`**）** / **`kernel_config_numeric_wellformed` / `topology_version_audit_consistent` / `eval_graph_store_maps_consistent` / `core_runtime_invariants_hold`**（数值容差良定义、`next_version` 与提交审计字段自洽、**EvalGraph 各表键与依赖边节点引用对齐，且 `eval_dependencies`↔`eval_reverse_dependencies` 逐边互指**、三角化缓存一致；**`Core.EvalGraph.StoreMapsCheckedByCoreInvariants=1`**；供宿主/CI 一键门禁）** / **`runtime_tessellation_caches_consistent` / `tessellation_cache_stats` / `export_tessellation_cache_stats_json` / `prune_stale_tessellation_cache_entries`**（悬挂三角化缓存剔除 + stale 统计累计）、**`set_enable_cache` / `set_precision_mode`**、容差 **`isfinite` 校验**、能力报告 **`Core.*` 快照行**（含 **`Core.Topology.Isolation.Effective`**、**`Core.Topology.Audit.*`**、**`Core.EvalGraph.*`**）；**`reset_runtime_stores` / `EvalGraphService::clear_graph` 同步清零 `EvalGraphTelemetry`、`EvalInvalidationBridgeMetrics` 与 `TessellationCacheStats`**；由 **`axiom_kernel_runtime_invariant_test`** 回归
  - **主要不足**：
    - **工程化不变量门禁**：多隔离级/写集细粒度**策略**与「建模事件→Eval」**事件级**全链路仍待加强（当前为门面快照 + **提交写分项/累计** + **Eval 依赖边/体绑定/下游步数** + 遥测 + **组合不变量门面**）
  - **建议测试入口**：`axiom_smoke_test`、`axiom_kernel_runtime_invariant_test`

- **diag（Diagnostics）**
  - **已具备**：错误码/诊断码常量、诊断报告、JSON 导出（含 `Issue.stage` 与 `Issue.numeric_evidence`）、按 related entity 检索；BOOL/HEAL/IO 关键路径已绑定阶段标签并有回归断言；**按 `Issue.stage` 聚合**：`issue_stage_histogram`、`export_grouped_by_stage_txt/json`（空 `stage` 计入 `(unset)`；第 24 切片补齐空路径预校验及写入/关闭失败检测）；**全量归档**：`export_all_reports_json` / `export_all_reports_txt`；**结构化证据门禁**：`audit_evidence` / `export_evidence_audit_json` 按问题码、阶段前缀和最低严重级别审计阶段、实体与有限数值证据，重复报告去重并统计截断明细，BOOL 受覆盖失败分支已接入
  - **主要不足**：
    - **证据门禁全覆盖**：HEAL 验证/修复/回滚及 IO 导入导出/后验验证/批处理仍需把**全部**重量级失败分支纳入阶段、实体与数值证据门禁
  - **建议测试入口**：`axiom_diagnostics_test` + 对应 workflow 测试中的失败分支断言

- **math（Tolerance/Predicate/Linear Algebra）**
  - **已具备**：容差解析与尺度策略（`effective_*`、`resolve_*_for_scale`、`scale_policy_for_body_nonlinear`）、线代/谓词；**本阶段已在 `axiom_math_services_test` 固化**：退化/大尺度定向、非有限输入与门限、`orient2d/3d_effective` 与 `max_local`/`min_local` 钳制、负向定向对偶、`point_on_segment_*` 与 Rep/点在体内容差对齐等回归矩阵
  - **主要不足（长期）**：
    - **工业级全链路一致解释**：Geo/Topo/Ops/Heal/Rep 热点仍须持续收敛到同一容差解析入口并补跨模块回归
    - **更强谓词**：Shewchuk 级精确算术或扩展符号判定等仍不在本阶段范围
  - **建议测试入口**：`axiom_math_services_test`（本阶段 P1 条目已闭合）；跨模块对齐见各 workflow 测试

- **geo（Curves/Surfaces/PCurve/Eval/Closest）**
  - **已具备**：曲线/曲面/PCurve 的创建与 eval/domain/bbox/closest，含批量接口；曲线与有界曲面均公开 `closest_point_detailed`，返回距离下界、参数不确定度、预算计数与终止原因；曲面覆盖 Bezier/BSpline/NURBS、Revolved/Swept、Trimmed/Offset、非空结点片和修剪孔边界，可认证平面包装/共面张量面/直线轮廓扫掠走解析或支撑平面快路；旧复杂曲面最近参数复用主流程且不写 eval 缓存
  - **主要不足**：
    - **曲面全域精度合同后续**：无限域仍须先修剪；高阶变化界、通用退化曲面、极端尺度与大模型性能仍未工业化
    - **真实 Trim 语义**：Trimmed 目前偏“参数域裁剪占位”，缺基于 loop/coedge/PCurve 的修剪边界
  - **建议测试入口**：`axiom_geometry_test`（增加曲率/导数/退化场景后再逐步收紧）

- **topo（Transaction/Query/Validation/Trim Bridge）**
  - **第 69 批一致性与可靠性增量**：`first_boundary_conflict`、`create_face` 与 `validate_face` 共享有限 Line/LineSegment 最近段流程，统一分类内部相交、端点相接、共线正长度重叠及容差内正距离邻近；新增 `AXM-TOPO-E-0028/0029` 与可查询最近点/距离证据，拒绝不分配 FaceId、不改索引或事务计数。`TopologyCancellationSource/Token`、带令牌事务、显式轮询、状态/写次数查询及累计指标覆盖预取消、写/提交/显式回滚/析构边界，取消恢复完整快照、释放写者槽且不推进版本或成功提交审计。最终完整 CTest 16/16 通过。一般曲线 trim 求交、长耗时流程内部轮询和子事务/保存点仍未实现。
  - **第 56 切片一致性增量**：`create_face` 与 `validate_face` 拒绝不同边界环的非平行直线边在端点相接，`AXM-TOPO-E-0027` 关联两环与两边；`axiom_topology_test` 覆盖外/内及内/内相接、合法面、诊断 JSON、失败不污染和回滚。曲线求交、共线重叠、容差邻近相接、完整 trim bridge 与持久命名仍待覆盖，FR-TOPO-001 保持受限可用。
  - **第 52 切片一致性增量**：`create_face` 与 `validate_face` 检查不同边界环的直线/线段边在三维内部相交，`AXM-TOPO-E-0026` 关联两环与两边；`axiom_topology_test` 覆盖外/内及内/内相交、合法面、诊断 JSON、失败不污染和回滚。曲线边、端点触碰、共线重叠与容差邻近相接仍待覆盖，FR-TOPO-001 保持受限可用。
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
  - **已具备**：primitive 解析三角化 + owned topo face 组装三角化、顶点焊接、tessellation cache/face-cache、网格检查与 JSON 报告、BRep↔Mesh round-trip 报告与回归；**张量积（Bezier/BSpline/NURBS）与派生面（Revolved/Swept/Offset）patch 在参数域中心估计 `|∂S/∂u|·Δu` / `|∂S/∂v|·Δv`，用 `segments_for_tensor_direction` 同时叠和弦长步长与 `segments_for_circle`（含 `angular_error`）**；**`Kernel::tessellation_cache_stats` / `export_tessellation_cache_stats_json` 与 `runtime_store_counts.tessellation_metrics` 对齐**
  - **主要不足**：
    - **全类型误差预算**：修剪面/更复杂派生体的曲率敏感细分与主曲率驱动仍不足（当前为 patch 中心度量 + 工程上限）
    - **显示管线能力**：局部重三角化（patch）、UV seam/unwrap、增量更新与缓存指标门禁不足
  - **建议测试入口**：`axiom_representation_io_test`（报告字段与密度单调性）+ `axiom_io_workflow_test`（导出链路不回归）

- **io（STEP/AXMJSON/glTF/STL + workflow）**
  - **已具备**：STEP/AXMJSON 导入导出主链路；**STL/glTF 导入**（Axiom 导出子集/嵌入数据可解析）；**IGES/BREP/OBJ/3MF** 的 **Axiom 元数据或自描述 ZIP 子集**互操作（非完整工业标准文件）；网格导出 **`ExportOptions::compatibility_mode` 严格门控** + 可选 **`write_mesh_validation_report` JSON 侧车**（`merge` 回流导出诊断）；**`Kernel` 格式能力报告与 `detect_format` / `import_auto` / `export_auto` 已对齐**（`axiom_smoke_test`）；`io_workflow` 含往返与**空路径/未知扩展名**失败诊断断言；**STEP/IGES 子集** HEADER 写入/解析 **`AXIOM_STEP_SCHEMA` / `AXIOM_STEP_ENTITY` / `AXIOM_IGES_ENTITY`** 及 ISO-10303-21 风格 `FILE_SCHEMA` 注释；**OBJ** 跳过 `vn`/`vt`/`o`/`g` 等常见非网格行；**3MF** 模型路径匹配更宽松、XML 顶点/三角形属性顺序容错及 **1-based 三角形索引**自动纠偏；**导出策略矩阵**文档与 **`axiom_io_dataset_test` + `tests/data/io`** 小型 CI 数据集；导出目录**只读探测**与批量失败 **`io.batch_*` 诊断**（含 **`detect_formats_with_paths` / `count_by_format` / `paths_of_format` `AXM-IO-D-0011`**、**批量读取 `AXM-IO-D-0012`**、**`compare_file_text_many_equal` `AXM-IO-D-0013`**、**批量路径写操作 `AXM-IO-D-0014`**（`append_text_many` / `touch_empty_files` / `remove_files` / `ensure_parent_directories`）、**路径变换批量 `AXM-IO-D-0015`**（`normalize_paths` / `compose_paths` / `change_extensions` / **`validate_import_paths`** / **`validate_export_paths`**）、**`export_body_summaries_many` 合并 `D-0010`**）；**`validate_import_path`** 区分空路径 / **不存在（`AXM-IO-E-0001`）** / 非常规文件；**`validate_export_path`/`validate_export_paths`**（父目录可写，批量项 `Issue.stage=io.batch_validate_export`）；**`export_auto_to_directory`** 前置**扩展名白名单**（`AXM-IO-E-0002`）与目录可写探测；**标准 STEP/IGES 物理文件**在 **`AXM-IO-E-0010`/`E-0011`** 同诊断内附 **Info `AXM-IO-D-0016`/`D-0017`**（EXPRESS 类型名 / IGES DE 实体号频度扫描，**非**几何物化）；全实体路线见 **`docs/plan/AxiomKernel_STEP_IGES_标准交换实施路线.md`**
  - **主要不足**：
    - **标准实体级互操作**：标准 IGES/STEP **全实体**交换、通用 3MF/OBJ **全工业阅读器**仍不足（当前为子集 + 渐进兼容 + 标准文件**扫描摘要**；**BRep 物化**仍依赖外部内核集成）
    - **工业交付深化**：侧车字段产品化矩阵、大规模数据集与性能门禁、批处理失败聚合策略仍可加强
  - **建议测试入口**：`axiom_io_workflow_test` + `axiom_io_dataset_test` + `axiom_diagnostics_test`

- **ops（Primitive/Sweep/Boolean/Modify/Blend/Query）**
  - **已具备**：前置分类、近似求交语义、来源传播、最小物化拓扑骨架与 Strict 回归闭环、部分修改/修复工作流语义；**Modify/Blend 失败与关键告警**在启用诊断时写入 **`Issue.stage`**（如 `modify.offset.self_intersection`、`modify.shell.*`、`blend.fillet.placeholder` / `blend.fillet.multi_edge` 等），`axiom_ops_heal_test` 对典型分支做断言
  - **主要不足（最大缺口）**：
    - **布尔真闭环**：精确求交/切分/分类/重建/验证修复闭环缺失
    - **特征建模完整性**：显式多边形的多类 extrude/revolve/sweep 子域已生成真实多面体闭壳，但解析扫掠/旋转曲面、任意放样、thicken 及显式轮廓历史仍缺失
    - **圆角/倒角工业化**：含角区/变半径/失败分类与回归数据集缺失
  - **建议测试入口**：`axiom_boolean_workflow_test`、`axiom_boolean_prep_test`、`axiom_ops_heal_test`

- **heal（Validation/Repair）**
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

- **FR-GEO-001 第 69 批（已通过完整门禁）**：新增 `CurveService::closest_point_detailed`、精度/预算选项和解析/距离容差/参数容差终止结果；解析覆盖 Line/LineSegment/Circle/CompositePolyline，分支限界覆盖 Ellipse/Parabola/Hyperbola/Bezier/BSpline/NURBS/CompositeChain，样条按全部非空结点段独立覆盖并以保守速度/加速度界剪枝。旧非解析 `closest_parameter/closest_point` 复用该流程；预算、非法选项和数值不可表示均结构化失败且不写求值缓存。首次全量测试的椭圆与 NURBS 参数回归经 repair 修复，最终完整 CTest 16/16 通过。曲面详细全域精度入口仍未实现，需求保持受限可用。

- **FR-GEO-001 第 55 切片**：UV 折线 PCurve 最近参数的逐段投影和距离比较使用扩展精度中间量，有限大坐标的距离平方超出 `Scalar` 范围时仍能选中正确分段。`axiom_geometry_test` 覆盖段内最近点、端点、重复点、非法查询/句柄、稳定错误码、失败不污染与重试。该切片未改变公开签名；3D 曲线全域精度合同已由第 69 批闭合，PCurve 详细证书与曲面合同仍待推进。
- **FR-GEO-001 第 51 切片**：线段最近参数的解析投影使用扩展精度中间量，有限大尺度端点和查询点不再因 `Scalar` 长度平方溢出产生非有限参数；保持端点钳制。`axiom_geometry_test` 覆盖段内最近点、端点、退化创建、非法查询/句柄、稳定错误码、几何与缓存不污染及重试。该切片未改变公开签名；其余 3D 曲线全域精度合同已由第 69 批闭合，PCurve 详细证书与曲面合同仍待推进。
- **FR-GEO-001 第 46 切片**：3D 复合折线最近参数逐段解析投影并按三维距离比较，有限大坐标用扩展精度中间量防止平方溢出；等距取最早参数，重复控制点按零长度段处理。`axiom_geometry_test` 覆盖窄分支、段内投影、退化段、大坐标、非法查询/句柄、错误码、失败不污染与重试。未改变公开签名或错误码，其他曲线/曲面全局最近点精度仍待定义，需求保持受限可用。
- **FR-GEO-001 第 41 切片**：UV 折线 PCurve 最近参数逐段投影并比较距离，避免固定全域采样漏掉短分段；重复控制点形成的零长度段可安全参与比较，等距取最早参数。`axiom_geometry_test` 覆盖短分段、段内投影、重复点、非法查询与句柄、错误码、失败不污染及重试。未改变公开签名或错误码，不扩展至任意曲线的全局精度保证，需求保持受限可用。
- **FR-GEO-001 第 36 切片**：椭圆最近参数以粗采样为初值按三维欧氏距离阻尼细化，周期缝结果归一到 `[0, 2pi)`；创建时拒绝轴长或派生法向长度溢出。回归覆盖解析可知最近点、周期缝、非有限查询、有限但溢出的轴向量及失败不污染。未改变公开签名或错误码，不宣称任意退化椭圆全局最优，需求保持受限可用。
- **FR-GEO-001 第 31 切片**：BSpline/NURBS 曲面满重数断点、上端点及域外钳制的点值、一二阶偏导和曲率统一使用同一非空单侧片；回归覆盖双轴断点、非单位权重、常值退化、非法结点失败不污染和既有曲面继续求值。未改变公开签名或错误码，不宣称断点全局可微，需求保持受限可用。
- **FR-GEO-001 第 26 切片**：BSpline/NURBS 曲面最近点初值逐个覆盖非空张量积结点片，修复固定全域网格漏掉极窄、双轴满重数隔离片的问题；回归覆盖多项式与非单位权重有理曲面、非法查询及几何/缓存不污染。未改变公开签名或错误码，不宣称任意曲面全局最优，需求保持受限可用。
- **FR-GEO-001 第 21 切片**：BSpline/NURBS 曲面 u/v 显式结点现在拒绝零长度有效域和超过 `degree + 1` 的结点重数，复用 `AXM-GEO-E-0002`；回归覆盖两轴、两类曲面、合法满重数断点、失败诊断以及几何/缓存不污染。未改变公开签名或错误码，需求保持受限可用。
- **FR-GEO-001 第 16 切片**：BSpline/NURBS 最近参数初值搜索逐个覆盖非空结点分段，修复固定全域网格漏掉极窄、满重数断开分支的问题；回归覆盖显式/推断次数、非单位权重、常值退化、非法查询点诊断和失败不污染。该切片当时仅为初值改进；第 69 批已补曲线全域预算与收敛证书，曲面合同仍待推进。

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

- **FR-TOPO-001 第 73 切片线性复合边跨环冲突（专项门禁闭合）**：显式裁剪 CompositePolyline 与线性 CompositeChain 按参数折点展开为精确有限线段，共用既有跨环冲突分类、错误码与事务隔离。内部折点不冒充拓扑边端点，递减区间与线性嵌套链可用；含任一真曲线子段的链保守跳过，不以弦线冒充真实边界。

- **FR-TOPO-001 第 72 批显式边裁剪区间（专项门禁闭合）**：`EdgeRecord` 保存有向起止参数；`create_trimmed_edge` 以稳定错误码拒绝非有限/零区间、越域或端点不一致，拒绝发生在 ID 分配前并携带数值证据。`edge_curve_interval` 区分显式区间与兼容旧边；校验器同步检查存量区间。圆、椭圆、抛物线、双曲线、Bezier/BSpline/NURBS、折线及复合链可据真实区间查询长度，拓扑 bbox 纳入解析极值或保守控制凸包。一般曲线跨环求交、完整 PCurve trim bridge 与持久命名仍未闭合。

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
  - **未开始/缺失**：更细粒度写集/读集与隔离级别、子事务/保存点、跨进程并发语义；BOOL/HEAL/IO 长耗时内部阶段取消轮询

### 需求 7.3 基础建模能力（OpsCore/TopoCore/GeoCore）

- **基础体构造**
  - **部分完成**：`PrimitiveService` 已提供 `box/wedge/sphere/cylinder/cone/torus` 等；物化层对部分 primitive 可走 **解析壳路径**（如 `Wedge` 专用壳、`Cylinder` 棱柱壳等），其余仍常退回 **bbox 壳** 以满足 Strict/闭环回归；派生结果体仍大量依赖最小物化骨架、`source_*` 推导与来源壳克隆
  - **未开始/缺失（工业化）**：与工业内核一致的 **全 primitive 精确拓扑面环 + 与曲面参数域严格一致** 的 BRep；楔体/盒体等若仍部分依赖 bbox 占位壳，需升级为完整解析拓扑与几何
- **特征构造**
  - **部分完成（接口/占位）**：`SweepService` 等接口存在，部分路径产出派生体与工作流语义
  - **第 71 批 SweepService 增量（验收中）**：`revolve` 的整周/部分角显式子午面支持轴分离凹外环及多个分离孔洞；`loft` 以相同外环/孔环顶点数定义站间对应，支持倾斜、凹/带孔三截面并物化真实单闭壳，质量/惯性来自闭合多面体积分。新增矩阵覆盖绕向/起点/孔序、姿态、Strict、邻接、bbox、owned 网格、失败零污染、活动事务和回滚重试；隔离回归已通过，完整长目标待统一门禁。不同环拓扑自动匹配、分支/尖顶/坍塌截面仍不支持。
  - **第 70 批 SweepService 扩展（已通过完整门禁）**：端点重合且首尾切向连续的 Bezier/BSpline/NURBS 导轨以 holonomy 校正旋转最小标架，生成无端盖周期闭壳；`CompositeChain` 可连接有界直线/线段、圆/椭圆弧、Bezier/BSpline/NURBS 和 polyline 首段，强制接缝位置与 G1 连续，开放链有端盖、闭合链无端盖；`revolve` 对 `(0,2π)` 显式平面轮廓以每周 48 段的分辨率生成侧壁和约束剖分首尾端盖。三组回归分别覆盖 48、32、48 组成功变体，并覆盖真实拓扑、邻接/bbox、Strict、owned 网格、质量/惯性、失败零污染、活动事务零写入、回滚和重试。完整 CTest 16/16 通过（1539.20 s）。仍不支持解析扫掠/精确旋转、带孔旋转、嵌套复合链、抛物/双曲子段、急弯/自靠近输入和显式轮廓历史。
  - **第 68 批 SweepService 扩展（已通过完整门禁）**：`extrude_to_plane` 沿射线把显式凹/带孔平面轮廓投影到斜目标面；整周 `revolve` 以 48 站周期分片支持离轴环形体及唯一连续轴边闭合的实心轮廓；曲线 `sweep` 以旋转最小化标架支持 Bezier/BSpline/NURBS 开放导轨和整圆/椭圆周期导轨。三条路径均物化实际三角面、共享边/顶点闭壳并缓存闭合多面体积分质量属性。周期带孔扫掠经故障复验改为外边界与各孔边界分别物化独立闭壳，壳数及 owned 网格连通分量均为 `1 + holes_xyz.size()`；开放带孔导轨仍由端盖连成单壳。回归分别覆盖 240、32、40 组主要变体，以及公开拓扑、bbox、Strict、owned 网格、质量/惯性、退化失败不污染与回滚重试。最终 `build-agent` 完整构建成功，CTest 16/16 通过（136.35 s；`axiom_ops_heal_test` 111.16 s，性能基线 1.70 s）。当前仍是保守浮点剖分/采样多面体 BRep，不是解析扫掠或旋转曲面；带孔旋转、闭合样条/复合导轨、尖点、过紧曲率和自靠近导轨拒绝，部分角旋转仍沿用受限路径。
  - **等比变截面与尖顶拉伸（第 65/66 包，已纳入第 68 批全量门禁）**：`extrude_scaled` 支持凸/凹及单孔/多孔轮廓的正比例收缩、扩张和等截面；`end_scale=0` 支持无孔三角/凸/凹轮廓收敛到共享尖顶。既有 360 组正比例、72 组矩形/L 形尖顶及 4 组四面体回归随本批 16/16 CTest 通过。带孔尖顶、负比例、任意截面放样和逐壁恒角拔模仍不支持；近退化或极端尺度输入可保守拒绝。
  - **带孔拉伸与折线平移扫掠（第 61/62 包，已纳入第 68 批全量门禁）**：显式带孔凸/凹多边形拉伸及线段/折线固定方向平移扫掠已由真实闭壳、公开邻接、解析质量属性、Strict、网格、失败不污染和回滚回归覆盖。折线每段仍须沿截面法向严格同向推进，不支持回退、切向或闭合折线；无显式轮廓路径仍为历史占位。第 60 包无孔凹轮廓及 Rep UV/焊接回归同次通过。
  - **显式多边形线段扫掠受限可用（第 59 切片）**：`sweep` 对有界线段导轨复用真实棱柱拉伸链路，保持轮廓世界坐标并按终点减起点平移；双绕向、反向/斜向、不同轮廓平面均由公开面/边/顶点查询、面面积、质量属性及 Strict 验证回归验收。不支持的轮廓/导轨及退化位移在物化前拒绝，失败不污染；曲线导轨与无显式轮廓路径不属于本轮真实能力范围
  - **未开始/缺失（工业化）**：拉伸/旋转/扫掠/放样/加厚等 **真实几何求交 + 拓扑构造 + 失败可诊断** 的完整实现

### 需求 7.4 布尔运算能力（OpsCore）

- **部分完成**：布尔前置、面级候选、解析交线/交线段、交线入库、矩形面 imprint 原型、近似求交与占位语义、来源传播；**阶段化诊断与 `Issue.stage` 已在关键路径绑定**（见 `axiom_boolean_workflow_test`），整体仍为过渡实现
- **未开始/缺失（工业闭环）**：候选对→精确求交→切分→分类→重建→验证/修复的**完整**闭环；通用 imprint/trim/merge；**全部**失败分支的阶段化诊断门禁

### 需求 7.5 几何修改能力（OpsCore/HealCore）

- **部分完成**：偏置/抽壳/面替换/删除面补面等接口语义、失败/告警路径与来源传播已有基础回归
- **未开始/缺失**：真实几何修改算法、局部重建、稳定的失败分类（自交/薄壁/高曲率/容差冲突）与可回放修复记录

### 需求 7.6 圆角与倒角（OpsCore）

- **部分完成**：接口与非法输入保护、占位语义与基础诊断
- **未开始/缺失**：真实圆角/倒角几何生成（含角区）、变半径、失败原因细分与回归数据集

### 需求 7.7 查询与分析（Query/Eval/Rep）

- **FR-QUERY-001 第 72 批曲边区间长度（专项门禁闭合）**：显式裁剪曲边由 `edge_length` 调用 `CurveService::length(curve,t0,t1)`，环和面边界长度自然复用；闭合双半圆、NURBS/Bezier 子域、递增/递减区间、越域/零区间/端点错配、区间查询、曲边 bbox、缓存/对象/事务不污染及回滚均进入 `axiom_query_eval_test`。旧无区间曲边继续 `NotImplemented`，不扩大到曲边面积或质量积分。

- **FR-QUERY-001 第 71 批多闭壳空间层级（验收中）**：公开 `body_shell_regions`，以严格包含深度给出 Material/Void 和直接父壳；`body_mass_properties` 对独立材料相加、空腔相减、材料岛再相加，边界面积全部保留。壳相交、重叠或容差接触返回 `InvalidTopology / AXM-QUERY-E-0006`，查询仍不发布 MeshId、不写缓存。单壳直接复用质量积分，三角面临时边界走直取快路；`axiom_query_eval_test` 专项通过，完整门禁待闭合。

- **FR-QUERY-001 第 70 批闭合多面体拓扑质量属性（已通过完整门禁）**：公开 `TopologyQueryService::shell_mass_properties/body_mass_properties`，从当前真实拓扑重算单位密度体积、面积、质心与关于质心的世界坐标惯性张量；支持平面直边的凹/带孔双边流形闭壳和多个独立实体壳的平行轴汇总。`axiom_query_eval_test` 覆盖平移/尺度盒体、凹带孔拉伸、双壳、单位/惯性语义、无效/开壳/曲面/零体积、删除/回滚及对象/网格/缓存/Eval/事务不污染；完整 CTest 16/16 通过（1539.20 s）。曲面/曲边、独立内壳空腔语义、相交/重叠多壳和稳定求交仍未闭合。

- **FR-QUERY-001 第 68 批解析曲面修剪面积（已通过完整门禁）**：公开 `TopologyQueryService::face_area` 通过曲面面积密度的 Green 边界积分计算外环减内环面积，支持 Plane/Cylinder/Cone/Sphere/Torus 及嵌套 Trimmed/Offset；未包装 Plane 完全没有 PCurve 时兼容 `planar_face_area`。`axiom_query_eval_test` 覆盖五类解析面、凹外环/孔/绕向、包装面、参数越域、PCurve 缺失/断裂/自交、删除、事务回滚及缓存/对象不污染；随本批 CTest 16/16 通过。边界目前仅支持完整折线 PCurve，周期缝须由调用方使用同一展开区间；Bezier/BSpline/NURBS/Revolved/Swept 面积仍返回 `NotImplemented`。

- **FR-QUERY-001 第 67 功能包（已纳入第 68 批全量门禁）**：`CurveService::length` 新增椭圆/抛物线/双曲线与 Bezier/BSpline/NURBS 真实导数自适应积分，公开 `CurveLengthOptions` 统一绝对/相对容差及整次查询求值预算。样条按非空结点区间积分，不计满重数断点跳跃；复合链保留子曲线局部 `[0,1]` 合同。预算耗尽、精度停滞或速度不可用使用 `AXM-GEO-E-0011`，失败无部分长度。相关独立参考、尺度/姿态/节点、失败诊断和不污染回归随本批全量门禁通过。

- **FR-QUERY-001 第 63 功能包（已纳入后续全量门禁）**：`CurveService::length` 的直线有限区间、线段、圆、折线和嵌套复合链解析长度，以及 `TopologyQueryService::edge_length/loop_length/face_boundary_length` 直线边长度接口族，已随全量门禁通过。其“拓扑曲边缺少裁剪参数”限制已由第 72 批对显式区间边闭合；平面直边闭壳的拓扑质量属性已由第 70/71 批扩展，曲面/曲边质量积分仍待闭合。

- **FR-QUERY-001 第 54 切片**：公开 `TopologyQueryService::planar_face_area` 对平面直线边面片按当前拓扑顶点计算外环减内环的面积，单位为模型长度单位平方；盒体六面的解析面积由 `axiom_query_eval_test` 跨 Ops/Topo 验证。无效或已删除面不返回数值，曲面不支持与非共面拓扑返回结构化失败；替换曲面、删除面及回滚后的查询重算由同一测试验证。仅此受限子域为真实边界计算，不扩大到曲边面积或通用体质量属性。
- **部分完成**：bbox、点分类、部分距离/近似求交、截面（偏占位）与批量接口雏形；`QueryService::mass_properties` 等已存在，但对多数体仍偏 **bbox 近似**，非工业级物理属性；**基本体**（盒/楔/球/柱/锥/环）已走统一解析路径；**Sweep** 质量口径集中到 `try_sweep_body_mass_properties` 并与查询共用；**布尔**结果体在 `BodyRecord` 中写入 **`has_boolean_op`/`boolean_op`**（`BooleanService::run`）后，`mass_properties` 可区分 **Union/Split** 与 **Subtract/Intersect**：**Intersect**+双盒且结果 bbox≈**AABB 交** 时按交叠长方体解析；**Subtract**+**LhsContainsRhs**+双盒时 **体积差** 与 **质心/惯性差分**（表面积为外+内占位和）；**LhsContainsRhs/RhsContainsLhs** 下取大包络体 **仅 Union/Split**（无 `has_boolean_op` 时保留原兼容启发式）；其余 Disjoint 并集相加、Touching/Overlapping 体积一致性、左体减、Sweep 操作数等规则同前；**Modified** `replace_face` 包围盒不变时继承来源；`axiom_ops_heal_test` 含 **相交体积**、**嵌套减**、面接触并集、包含并集、extrude+盒、`replace_face` 继承
- **未开始/缺失**：长度/面积/体积/重心/惯性矩的工业级正确性；曲率/厚度分析；稳定的曲线-曲面/曲面-曲面求交

### 需求 7.8 验证与修复（HealCore/Validation）

- **部分完成**：拓扑一致性/来源一致性/闭合性 Strict 校验；导入后验证与可选自动修复语义；缝合结果最小物化；**自交校验路径**（含网格自交分析与 `validate_self_intersection` 等）与 `axiom_heal_test` / `axiom_ops_heal_test` 回归
- **未开始/缺失**：自交/流形性在 **BRep 全类型与大数据集**上的工业级完备规则；容差冲突检查、参数域异常检查；小边/小面/法向/近重复归并等工业化修复策略与回放机制

### 需求 7.9 数据交换（IO）

- **NFR-DIA-001 第 70 批精确 B-Rep 文本导入诊断（已通过完整门禁）**：`import_axmjson/import_iges/import_brep` 在分配 BodyId 前完成普通文件、64 MiB 上限/短读、严格结构、格式/BodyKind、有限数值、包围盒和轴退化校验；失败绑定 `io.import.<format>.input/path/open/read/parse/validation`，复用 IO/VAL 稳定错误码并返回可检索 `diagnostic_id`。`axiom_io_workflow_test` 覆盖三格式成功、截断/错格式、退化、阶段/错误码/JSON、Body/Mesh/next_id 隔离和修复后原位重试；完整 CTest 16/16 通过（1539.20 s）。AXMJSON 兼容早期身份+bbox 文件，扩展几何字段必须成组完整；标准 IGES 实体仍返回 NotImplemented。

- **NFR-DIA-001 第 64 功能包（已纳入第 68 批全量门禁）**：OBJ/STL/glTF/3MF 网格导出失败绑定 `io.export.<format>.input/path/convert/mesh/open/write/sidecar` 与输入 Body，严格 QA 保留 `io.export.mesh_strict_qa`。主文件和侧车均检查最终写入/关闭状态；失败回滚本次三角化新增网格、ID、体/面缓存及统计，保留已有网格。兼容模式继续允许退化三角形，但在打开目标前拒绝空网格、非法索引、非有限坐标；glTF 另拒绝超出 float32 范围的坐标。`axiom_io_workflow_test` 新增四格式 × 严格/兼容 × 侧车开关、诊断检索/JSON、冷/热/失效缓存、退化、参数失败文件保护、Linux `/dev/full` 主文件及侧车失败、重试和重新导入回归。复用现有错误码，无公开签名变化；故障修复复现并修正 9 处诊断辅助函数调用不匹配，复用 `failed_void` 保留 `InvalidInput`、Body 和阶段；补齐回归拉伸夹具必需的轮廓标签。相关测试随第 68 批最终完整 CTest 16/16 通过。设备写入失败不保证恢复文件，侧车失败时主文件可能已完整写出；不扩大格式或三角化精度承诺，需求保持受限可用。

- **部分完成**：STEP/AXMJSON 导入导出主链路、导入后自动验证与诊断回传；**STL/glTF 导入导出**（网格/内嵌子集）；**IGES/BREP/OBJ/3MF** 的 Axiom 子集路径；**严格导出 + 可选网格验证侧车 JSON**；**Kernel 与 IOService 格式能力、`import_auto`/`export_auto`/`detect_format` 对齐**（`axiom_smoke_test`）；**标准 STEP/IGES 物理文件形态探测**：对含 EXPRESS 实例的 ISO-10303-21 DATA 段、或典型 IGES 80 列/DE 卡片流，在**非** Axiom 子集时返回 **`StatusCode::NotImplemented`** 与 **`AXM-IO-E-0010` / `AXM-IO-E-0011`**（`io.import.step` / `io.import.iges`），并附带 **Info 级物理层扫描摘要** **`AXM-IO-D-0016` / `AXM-IO-D-0017`**（EXPRESS 类型名 / IGES 实体类型号频度，**非**几何物化），避免静默假成功（`tests/data/io/standard_*_stub`、`axiom_io_dataset_test`）；实施路线见 **`docs/plan/AxiomKernel_STEP_IGES_标准交换实施路线.md`**
- **未开始/缺失**：与 **STEPcode/Open CASCADE** 等集成的**真实实体解析与 BRep 物化**（扫描摘要仅为里程碑 0/1 能力）；工业级 **3MF/OBJ** 全量读写；导出策略与侧车字段的产品化矩阵与大数据集回归

### 需求 7.10 三角化与显示支撑（Rep）

- **部分完成**：三角化语义与参数校验、基础检查报告雏形；已开始具备（1）primitive 的解析三角化（可曲率敏感，受 chordal/angular options 影响）（2）有 owned topo 的派生体按 face 组装三角化 + 顶点焊接（3）tessellation cache 与 face tessellation cache（含失效清理）
- **未开始/缺失**：更完整的曲率敏感细分（覆盖更多曲面/修剪面/高阶曲面）；更可靠的局部重三角化（patch 级误差控制与 seam/weld 规则）；显示级缓存/增量更新；纹理参数（UV seam/unwrap）等完整输出

### 需求 7.11 版本/事务/增量更新（Core/EvalGraph）

- **NFR-REL-001 第 69 批（已通过完整门禁）**：公开 `TopologyCancellationSource/Token`、带令牌 `begin_transaction`、显式 `poll_cancellation`、取消请求/观察/回滚写次数、活动写者查询及累计取消指标；预取消不占写者槽，取消在拓扑写入、提交、显式回滚和析构边界观察，完整恢复创建/删除/替换及反向索引，不推进版本或成功提交审计；`AXM-TX-E-0007` 可稳定导出，取消审计纳入 `core_runtime_invariants_hold`。
- **部分完成**：版本号与单写者快照事务、协作式取消与累计审计、EvalGraph 基础失效传播/重算计数/循环依赖保护
- **未开始/缺失**：单个调用的抢占式取消、BOOL/HEAL/IO 长耗时阶段轮询、子事务/保存点、更细粒度隔离、结构共享策略细化、跨模块增量重算和缓存命中指标体系

### 需求 7.12 插件扩展（PluginSDK）

- **部分完成**：`PluginRegistry` 注册与清单查询；**清单↔实现绑定**（`PluginManifest::implementation_type_name`：带实现注册时自动填充或校验与 `type_name()` 一致；按 `type_name` 注销实现时同步移除绑定清单）；**注销路径可诊断**（`unregister_*`；门面 **`unregister_plugin_*`**）；`PluginHostPolicy` 含 **`PluginSandboxLevel`**、容量与 API 版本等门禁；**`PluginApiVersionMatchMode`**；**`auto_validate_body_after_plugin_importer`** / **`auto_validate_body_before_plugin_exporter`** / **`auto_validate_body_after_plugin_repair`** / **`auto_verify_curve_after_plugin_curve`** 与门面 **`plugin_import_file`** / **`plugin_export_file`** / **`plugin_run_repair`** / **`plugin_create_curve`**、**`invoke_registered_importer`** / **`invoke_registered_exporter`** / **`invoke_registered_repair`** / **`invoke_registered_curve`**；**`verify_after_plugin_curve`**（与 `plugin_create_curve` 开启自动校验时语义一致，供绕过门面路径显式闭环）；`find_manifest` 未命中带 **`kPluginLoadFailure`**；能力发现 JSON 含 **`manifests`** 摘要；`validate_after_plugin_mutation`、`register_plugin_*` 诊断；`services_available` 含 **`plugin.import`**、**`plugin.export`**、**`plugin.repair`**、**`plugin.curve`**、**`plugin.verify_curve`**；示例与 `axiom_plugin_sdk_test` / smoke 回归
- **未开始/缺失**：**进程外/OS 级隔离**、动态库加载与供应链安全（签名/沙箱）；**完整 SemVer 与多 ABI 并存**（`SameMajor`/`SameMinor` 已支持剥离 `-` 预发布与 `+` 构建元数据后再做核心版本比较；`Exact` 仍为整串相等；多 ABI 并存与完整 SemVer 语义仍不足）；**已闭合（宿主绑定）**：`Kernel` 构造时对 `PluginRegistry::bind_host_kernel_for_plugin_invocation` 绑定 `weak_ptr<KernelState>` 后，**`invoke_registered_importer/exporter/repair/curve`** 在对应策略开关开启时会自动套用与 **`plugin_import_file` / `plugin_export_file` / `plugin_run_repair` / `plugin_create_curve`** 一致的宿主校验语义（未绑定宿主时行为与历史一致：仅调用插件）；**Body** 侧仍可显式 **`validate_after_plugin_mutation`**，**曲线**侧 **`verify_after_plugin_curve`** 与 `plugin_curve_host_consistency_check` 共用实现

### 需求 7.13 诊断与日志（Diagnostics）

- **FR-DIAG-001 第 69 批（已通过完整门禁）**：公开 `NumericEvidence` / `Issue::numeric_evidence` 与 `DiagnosticEvidencePolicy/Audit/Finding`，新增 `audit_evidence` 和 `export_evidence_audit_json`；单条/批量/全量 TXT/JSON 保留数值证据，非有限 JSON 值安全写为 `null`。审计按问题码、阶段前缀和最低严重级别检查阶段、关联实体与有限数值证据，重复报告去重，finding 限额以 `omitted_findings` 记录。BOOL 主运行的受覆盖失败分支及预处理统计导出失败已补阶段、实体和数值证据并纳入统一门禁；HEAL 与 IO 尚未全面迁移，需求保持受限可用。

- **FR-DIAG-001 第 34 切片**：`export_report` / `export_report_json` 在序列化单条完整诊断后显式关闭并检查输出流，Linux `/dev/full` 等最终写入/关闭失败返回 `OperationFailed` / `AXM-IO-E-0005`，不再误报成功。`axiom_diagnostics_test` 覆盖 TXT/JSON 成功证据、无效 ID、空路径、目录打开失败、设备写入失败、源报告不污染及失败后重试。无公开签名或错误码变化；设备写入失败不保证恢复目标文件，需求仍为受限可用。

- **NFR-DIA-001 第 30 切片**：`import_step` 的空路径、文件不存在与非可读常规文件分别绑定 `io.import.step.input/path/open`；拒绝发生在 Body ID 分配和存储写入前。`axiom_io_workflow_test` 覆盖成功导入、空/缺失/目录输入、阶段检索、JSON 导出、模型计数不污染及失败后重试。复用既有 `AXM-IO-E-0004`，无公开签名变化；不宣称标准 STEP 实体交换，需求仍为受限可用。

- **NFR-DIA-001 第 25 切片**：`validate_self_intersection`、壳级及批量壳级变体的非法 Body/Shell、异属 Shell、空批量、退化偏置和 Strict 网格分析失败统一绑定 `heal.validate_self_intersection.*` 细分阶段，并保留目标 Body/Shell。`axiom_heal_test` 覆盖正常 Strict、非法/异属句柄、空批量、阶段检索、JSON 及模型计数不污染。复用既有错误码，无公开签名变化；网格 SAT 仍为三角化近似，需求保持受限可用。

- **FR-DIAG-001 第 29 切片**：`export_step` 的无效 Body/空路径、路径校验、打开与最终写入失败分别绑定 `io.export.step.input/path/open/write` 并关联输入 Body；显式检查写入与关闭状态，Linux `/dev/full` 不再误报成功。`axiom_io_workflow_test` 覆盖成功、空/无效输入、不存在父目录、设备写入失败、阶段检索、JSON、模型不污染及失败后重试。无公开签名或错误码变化，需求仍为受限可用。

- **FR-DIAG-001 第 24 切片**：`export_grouped_by_stage_txt/json` 显式关闭并检查输出流，设备写入/关闭失败不再误报成功；空路径在打开文件前返回 `InvalidInput`，打开或写入失败返回 `OperationFailed`，均复用 `AXM-IO-E-0005`。`axiom_diagnostics_test` 覆盖正常阶段、空阶段 `(unset)`、空路径、目录、Linux `/dev/full`、参数失败不截断既有文件、源报告不变及失败后重试。无公开签名或错误码变化；设备写入失败不保证恢复目标文件，需求仍为受限可用。

- **NFR-DIA-001 第 20 切片**：`ValidationService::validate_geometry` 的所有失败出口补齐 `heal.validate_geometry.*` 根因阶段；非法句柄和 bbox 失败显式关联目标 Body，owned B-Rep/Strict 检查保留目标 Body 与问题子实体。`axiom_heal_test` 覆盖合法成功、非法句柄、近重复顶点、面法向退化、阶段检索、JSON 导出及持久模型计数不污染。复用既有错误码且无公开签名变化；需求仍为受限可用，其他 HEAL/BOOL/IO 失败路径及全覆盖门禁待继续闭合。

- NFR-DIA-001 第 15 切片：`export_boolean_prep_stats` 显式关闭并检查输出流，避免写入失败返回成功；参数、文件打开、写入失败分别绑定 `bool.prep.export.input/open/write` 和 `[lhs, rhs]`。参数失败复用 `AXM-BOOL-E-0001`，文件打开失败由误用的 BOOL 输入码修正为 `AXM-IO-E-0005`，写入失败复用该 IO 码。`axiom_boolean_prep_test` 覆盖重叠/相同/分离/接触零体积输入、无效句柄/空路径、目录打开失败、Linux `/dev/full`、阶段/错误码检索及 JSON、参数失败文件不污染、模型计数与 Eval 失效不污染和失败后重试。无公开签名变化；设备写入失败不保证目标文件恢复，需求保持受限可用。

- **FR-DIAG-001 第 14 切片**：补齐指定 ID 批量文本归档，复用单报告文本格式保留完整 Issue（严重级别、码、消息、阶段、实体），保留输入顺序、重复 ID 和特殊字节；打开文件前拒绝任意位置的无效 ID，参数失败不创建/截断目标且源报告不变；显式关闭并检查写入错误，复用现有 CORE/IO 错误码，无公开签名变化。`axiom_diagnostics_test` 覆盖成功、空报告/空阶段/重复 ID、非法参数、路径失败、Linux `/dev/full` 写入失败及拒绝后重新导出。设备写入失败不保证目标文件恢复；需求仍为受限可用，全部重量级流程阶段/实体/数值证据待补齐。

- **NFR-DIA-001 第 10 切片**：`BooleanService::run` 拒绝未声明的 `BooleanOp` 值，返回 `InvalidInput` / `AXM-BOOL-E-0001`、`bool.input` 与输入实体，避免越过分支后静默创建结果体。`axiom_boolean_prep_test` 覆盖两种诊断模式下的负值/越界值、相同输入体及无效句柄、阶段/错误码检索与 JSON、模型计数及 Eval 失效传播不污染，并验证拒绝后四种合法枚举仍可执行。复用已有错误码，无公开签名变化；需求仍为受限可用，不提升现有 bbox 布尔为精确能力。

- **FR-DIAG-001 第 9 切片**：修复指定 ID 批量 JSON 导出丢失问题证据、未转义摘要及静默跳过无效 ID 的缺陷；输出复用单报告完整结构，保留控制字节、输入顺序和重复 ID。打开文件前校验所有 ID，参数失败不创建/截断文件且不改源报告；打开/写入错误返回已有 IO 错误码。`axiom_diagnostics_test` 覆盖完整证据、特殊字符、空报告、重复 ID、无效 ID 各位置、空参数与不可打开路径。FR-DIAG-001 仍为受限可用，全部重量级流程阶段绑定仍待完成。

- **NFR-DIA-001 本轮增量**：布尔早期失败在 `BooleanOptions::diagnostics=false` 时仍保留单条错误 Issue 的阶段与输入实体；无效输入、分离交集和包含导致空结果在两种诊断模式下均覆盖阶段/错误码检索、JSON 导出及模型计数不污染。`axiom_boolean_prep_test` 回归；需求仍为受限可用，不代表全部失败分支已覆盖。
- **本切片性能故障修复（2026-09-22）**：默认构建（GCC 13.3.0、`CMAKE_BUILD_TYPE` 为空、无优化参数，2 个可用 CPU，cgroup 无 CPU 配额限制；采集时 load average 为 1.10/1.04/0.95）复现性能门禁失败：150 次迭代 5518 ms，阈值 4000 ms。临时 `steady_clock` 累计计时确认：一次 6887 ms 的基准中，全局 `rebuild_topology_links` 调用 600 次、累计 6549 ms；每次新建图元都清空并重建全部已有拓扑的邻接索引，造成随模型累积增长的重复工作。修复仅在 Ops 新建独立图元拓扑时追加其邻接索引，派生体、修改和回滚仍保留完整重建；未改变物化几何、公共 API、错误码、构建模式、阈值或迭代数。临时计时代码已移除。`axiom_ops_heal_test` 新增连续创建 box/wedge/cylinder 后对全部已有体的五类反向索引查询回归，`axiom_perf_baseline_test` 保留原性能门禁；相关六项测试通过，性能项 3.09 s（ctest 墙钟）。完整 `ctest --test-dir build-agent --output-on-failure` 16/16 通过，性能项 3.03 s，总耗时 28.80 s；本切片门禁已通过。公有查询回归及完整测试未发现语义回归；这些是本机对比证据，不作为跨环境性能保证。

- **FR-DIAG-001 本轮增量**：布尔预处理告警 `AXM-BOOL-W-0001/W-0002` 写入报告时保留 `bool.prep` 与输入/输出体 ID；`axiom_boolean_prep_test` 覆盖成功、仅接触退化、无壳级候选、阶段检索与 JSON，以及前置失败不增加几何/拓扑/体计数。需求仍为受限可用，不提升精确布尔能力口径。

- **部分完成**：错误码常量、诊断报告、TXT/JSON/批量/全量导出（含 `Issue.stage` / `numeric_evidence`）、聚合检索与证据覆盖审计；BOOL 受覆盖失败分支已建立阶段/实体/数值证据门禁
- **未开始/缺失**：HEAL 验证/修复/回滚与 IO 导入导出/后验验证/批处理**所有重量级失败路径**的结构化数值证据迁移及模块级门禁

### 需求 7.14 混合表示与转换（RepCore）

- **部分完成**：BRep/Mesh/Implicit 的基础语义与部分测试；BRep↔Mesh round-trip 报告与网格检查已有专项回归
- **未开始/缺失**：高质量转换、工业级误差控制、混合建模策略与对外接口长期冻结

## 3.4 距离“工业几何引擎”的核心不足清单（架构视角）

说明：本节聚焦“要达到工业几何引擎”必须补齐的关键能力，不等价于“接口存在”。每条不足都应能映射到可验证的 DoD（实现 + 诊断 + 回归）。

### A) GeoCore：高质量几何与鲁棒查询不足

- **样条与高阶曲面工业化**：曲线 B-Spline/NURBS 已有完整有效域的最近点预算与收敛证书，但高质量导数/曲率与退化处理仍不足；高阶及派生/修剪曲面的全域反求尚无同等级合同。
- **Trim 语义未工业化**：当前 `Trimmed` 更多是参数域裁剪占位；缺少基于 loop/coedge/PCurve 的真实修剪边界、以及与 3D curve 一致性的完整规则与算法闭环。
- **统一公差与尺度策略的贯通不足**：几何求值/最近点/求交/验证/修复对 tolerance 的一致解释仍需要进一步收敛到“可预测、可回归”的策略中心。

### B) TopoCore：一致性规则集与 trim bridge 不足

- **拓扑规则集不完整**：有限 Line/LineSegment 及显式裁剪的折线/线性复合链已统一覆盖跨环相交、相接、重叠与容差邻近；圆锥曲线与样条的误差受控求交、面环方向及重复/悬挂/非流形完整规则仍不足。
- **PCurve-3D 一致性（trim bridge）未闭环**：已有最小校验接口，但缺少可用于工业修剪的强一致性约束与修复策略。
- **事务隔离深度不足**：单写者、快照回滚、协作式取消和累计审计已落地；细粒度写集/读集、子事务/保存点、长流程内部轮询与跨进程隔离仍不足。

### C) OpsCore：工业级建模算法缺失（最大缺口）

- **布尔闭环缺失**：候选对生成 → 精确求交 → 切分 → 分类 → 重建 → 验证/修复闭环未实现；当前更多是前置与近似/占位语义。
- **特征建模仍未工业化**：显式多边形 extrude、多类曲线 sweep 和整周/部分角 revolve 已能物化真实多面体闭壳；但解析扫掠/旋转曲面、任意 loft、thicken、带孔旋转和显式轮廓历史仍缺失，其他路径仍可能依赖最小物化骨架。
- **圆角/倒角缺失**：真实圆角倒角（含角区、变半径）未实现，失败原因细分与回归数据集不足。

### D) Heal/Validation：工业化验证与修复不足

- **自交/流形性/容差冲突**：已有**最小自交校验与网格侧分析**路径及回归，但距离 BRep 全场景、流形性完备与容差冲突的工业规则集仍有明显差距；修复策略（小边小面、近重复归并、法向/退化处理）尚未形成可回放与可追踪的工业闭环。

### E) Rep/Conversion：误差控制与 round-trip 的工业化不足

- **误差预算与质量度量不足**：需要把“误差预算（budget）→细分策略→质量报告→round-trip 验证”做成稳定闭环，并覆盖更多几何类型/修剪体。
- **显示管线能力不足**：局部重三角化、缓存更新、纹理坐标与 seam 规则仍不足以支撑工业显示/编辑体验。

### F) IO：标准互操作深度与工业交付矩阵仍不足

- **格式深度**：已实现 **STL/glTF 导入**与 **IGES/BREP/OBJ/3MF（Axiom 子集）** 及严格导出/侧车首批闭环；对**明显为标准形态的 STEP/IGES**已做**显式拒绝**（`NotImplemented` + `E-0010`/`E-0011`）并附带 **Info 扫描摘要**（`D-0016`/`D-0017`），**全实体解析与拓扑物化**仍依赖后续内核集成（见 `docs/plan/AxiomKernel_STEP_IGES_标准交换实施路线.md`）；**通用 3MF** 等仍不足。
- **能力报告**：`Kernel` 与 `IOService` 主链路格式集合**已对齐**并由 `axiom_smoke_test` 固化；新增格式须同步 `detect_format` / `import_auto` / `export_auto` / 门面与测试。

### G) Diagnostics/Eval/Plugin：工程化可观测与扩展不足

- **诊断覆盖率未全面系统化**：已具备 `Issue.numeric_evidence`、覆盖审计与 JSON 门禁，BOOL 受覆盖失败分支已纳入；HEAL/IO 的全部重量级失败路径仍未完成阶段、实体与数值证据迁移。
- **EvalGraph 未进入参数化求解级**：当前更像状态/依赖治理基础设施，尚未与真实建模/重建深度耦合。
- **插件工程化不足**：进程内能力发现、宿主策略与诊断闭环已有雏形（见 7.12），**隔离/安全与动态扩展**仍不足以支撑开放生态。

## 4. 当前主要风险

- 当前 `OpsCore` 仍未进入真实工业算法阶段
- 当前 `TopoCore` 尚未形成严格拓扑一致性规则集
- 当前 `TopoCore` 虽已具备基础事务、验证、邻接查询和基础反向索引，但仍缺少更完整的稠密关联结构
- 当前 `GeoCore` 已有曲线全有效域最近点证书，但高质量样条曲率、曲面参数反求和修剪边界精度合同仍不足
- 诊断码、阶段、实体与数值证据已在 BOOL 受覆盖分支形成审计门禁，但 HEAL/IO 全部重量级失败路径尚未完成迁移

## 5. 下一迭代 Sprint 焦点与本阶段 backlog

说明：为降低本文档膨胀，本节内容已同步拆分到 `docs/plan/AxiomKernel_近期迭代与Backlog.md`；本文仍保留事实上下文与历史兼容入口。

阶段口径保持：`Stage 2 深化 + Stage 3 准备：基础建模与查询分析为主线`。本节区分 **本迭代可交付**、**最近已闭合批次** 与 **全项目长期树**，避免与 §6 历史清单重复。

### 5.1 Sprint 焦点（当前迭代 3～5 条，可验收）

1. **diag + heal/io**：把 HEAL 验证/修复/回滚与 IO 导入导出/后验验证/批处理的重量级失败分支迁移到阶段、实体、数值证据门禁。
   **DoD**：`audit_evidence` 模块级门禁通过；`axiom_diagnostics_test` / `axiom_heal_test` / `axiom_io_workflow_test` 不回归。
2. **geo**：把第 69 批曲线全域最近点合同扩展到 Bezier/BSpline/NURBS 与派生/修剪曲面的完整参数域和 trim 边界。
   **DoD**：公开预算、距离/参数终止证据；`axiom_geometry_test` 覆盖窄参数片、边界和失败不污染。
3. **topo + reliability**：为拓扑边增加显式 trim 区间并覆盖一般曲线跨环求交；把协作式取消扩展到 BOOL/HEAL/IO 长耗时内部阶段。
   **DoD**：`axiom_topology_test` / 对应 workflow 测试覆盖取消、回滚、诊断和审计不变量。
4. **ops**：布尔真求交子里程碑（面级候选 → 交线 → imprint）按 DoD 切片交付。
   **DoD**：对应码与结构化证据可审计；`axiom_boolean_workflow_test` / `axiom_boolean_prep_test` 不回归。

### 5.2 本阶段 backlog 表（唯一入口，随迭代刷新）

说明：**状态**列区分已纳入门禁的交付与仍在推进项，避免与 §6 已落地条目冲突。

| 优先级 | 状态 | 模块 | 交付物（摘要） | 建议 `ctest` | 依赖 |
|--------|------|------|----------------|--------------|------|
| P0 | 已闭合（门禁） | core/io | 门面 IO 能力与 `IOService` 一致 | `axiom_smoke_test` | — |
| P0～P1 | 已闭合（首批） | diag/ops/io/heal | 工作流 `Issue.stage` + JSON 导出可聚合；Heal 独立门禁 | `axiom_diagnostics_test`、`axiom_boolean_workflow_test`、`axiom_heal_test`、`axiom_ops_heal_test` | core |
| P1 | 已闭合（本阶段回归） | math | 退化/尺度谓词与容差策略回归（`orient*_effective`、`max_local`/`min_local`、`resolve_*_for_scale` 非有限尺度、大坐标谓词/点等） | `axiom_math_services_test` | core |
| P0～P1 | 已闭合（第 69 批） | diag/geo/topo/core | 数值证据审计、曲线最近点证书、有限直线跨环冲突、拓扑协作式取消 | `axiom_diagnostics_test`、`axiom_geometry_test`、`axiom_topology_test`、`axiom_query_eval_test` | math/core |
| P1～P2 | 进行中 | geo/topo | 曲面全域最近点、一般曲线显式 trim / trim bridge / Strict 规则 | `axiom_geometry_test`、`axiom_topology_test` | math |
| P1～P2 | 进行中 | diag/heal/io | HEAL/IO 数值证据与模块级审计门禁 | `axiom_diagnostics_test`、`axiom_heal_test`、`axiom_io_workflow_test` | core |
| P2 | 进行中 | ops | 布尔非 bbox 结果子里程碑 | `axiom_boolean_*` | geo/topo |
| P2～P3 | 进行中 | eval/rep | 重算指标、Rep 误差预算 | `axiom_query_eval_test`、`axiom_representation_io_test` | ops（部分） |

### 5.3 最近已关闭的功能批次（与 §6 互证；以下为已落地摘要）

下列条目已在仓库代码与回归中闭合，**详细时间线见 §6**（含早期「求值图循环依赖 `AXM-EVAL-E-0001`」至「EvalGraph 治理能力」及 **§6 尾部** 最近条目）。

1. 求值图循环依赖失败路径绑定 `AXM-EVAL-E-0001`；重算 DAG 去重；脏依赖防护；`body` 绑定去重。  
2. 表示层点分类线性容差；点到体距离无效包围盒语义；`BRep/Implicit -> Mesh` 参数校验与细分映射。  
3. `query_eval` / `representation_io` 等对上述语义的回归覆盖。  
4. `Kernel::io_supported_formats` / `io_can_import_format` / `io_can_export_format` 与 `IOService::detect_format`、`import_auto`、`export_auto` 对齐，`axiom_smoke_test` 固化。  
5. `Issue::stage` 字段；诊断 JSON/文本导出；BOOL/HEAL/IO 关键路径阶段标签与 `axiom_boolean_workflow_test`、`axiom_diagnostics_test`、`axiom_heal_test`、`axiom_ops_heal_test` 回归断言。  
6. 《主开发计划与阶段路线图》`§1`：Stage 1 已达成、处于 Stage 1.5/2 过渡；本文档 §5 Sprint/backlog 结构重写。  
7. **Math P1（本阶段）**：`axiom_math_services_test` 补齐退化/尺度/非有限谓词与容差策略回归（含 `orient*_effective`、`max_local` 钳制与负向定向）；表 5.2 中 math 行闭合为「已闭合（本阶段回归）」。

### 5.4 下一未闭合批次（建议按表 5.2 顺序推进）

1. BOOL 全阶段失败路径诊断绑定与工业数据集雏形。  
2. 特征建模（拉伸/旋转/扫掠）真实拓扑物化，替代过度依赖最小骨架。  
3. HEAL 自交/流形性/容差冲突的验证与可回放修复。  
4. IO：标准 IGES/STEP 实体交换或通用 3MF 读写的下一里程碑（与产品路线一致后择一）。  
5. EvalGraph 与建模事件的命中率/成本指标门禁。  
6. Plugin：OS 级隔离与动态加载安全（中长期）。

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
- 为 `cone/torus` 补充了基础质量属性公式，使导入恢复后的语义不再退化为包围盒近似
- 为导入后的基础体质量属性恢复补充了表示层与 IO 集成测试覆盖
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
