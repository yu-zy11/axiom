# AxiomKernel 当前开发进度

本文档用于记录 `AxiomKernel` 当前阶段的实际开发状态、已完成内容、当前风险和下一阶段执行重点。

## 1. 当前阶段

当前已进入：

`Stage 1.5 / Stage 2 过渡：核心公共层增强 + 基础几何/拓扑深化`

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
- `include/src/tests/examples` 目录结构
- `Kernel` 门面骨架
- `core/math/geo/topo/rep/ops/heal/eval/diag/io/plugin` 模块头文件与实现骨架

### 2.3 构建与执行

已完成：

- 工程配置通过
- 工程完整编译通过
- 示例程序运行通过
- **多专项回归测试**接入 `ctest`（维持可重复门禁）

当前测试包括：

- `axiom_smoke_test`
- `axiom_plugin_sdk_test`（对应 `tests/sdk/plugin_sdk_test.cpp`，插件注册/能力发现/门面 JSON）
- `axiom_boolean_workflow_test`
- `axiom_io_workflow_test`
- `axiom_diagnostics_test`
- `axiom_geometry_test`
- `axiom_topology_test`
- `axiom_representation_io_test`
- `axiom_ops_heal_test`
- `axiom_query_eval_test`
- `axiom_boolean_prep_test`
- `axiom_math_services_test`
- `axiom_perf_baseline_test`（默认 30s 超时，可用环境变量调节负载）

## 3. 当前状态判断

### 3.1 已稳定内容

- 项目结构
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

### 3.2.0 证据口径说明（文档=代码真实状态）

本节及后续“完成度判断”以 **代码与测试为证据**，并遵循硬规则：**接口存在 ≠ 完成**。判定口径：

- **已完成（基础可回归）**：主链路实现存在 + 关键失败链路有明确状态/诊断码 + `tests/<模块>/*_test.cpp` 有回归覆盖
- **部分完成**：实现存在但仍以占位/近似/回退链路为主，或测试/诊断覆盖不完整
- **未开始/缺失**：无实现，或仅有接口/空语义

为便于追溯，本仓库当前以“单模块单实现单测试”为主，核心证据入口如下（随代码演进可补充）：

- **core/sdk**：`include/axiom/sdk/kernel.h`、`src/sdk/kernel.cpp`、`tests/sdk/smoke_test.cpp`、`tests/sdk/plugin_sdk_test.cpp`；示例 `examples/minimal_plugin.cpp`
- **diag**：`include/axiom/diag/diagnostic_service.h`、`src/diag/diagnostic_service.cpp`、`tests/diag/diagnostics_test.cpp`
- **io**：`include/axiom/io/io_service.h`、`src/io/io_service.cpp`、`tests/io/io_workflow_test.cpp`
- **topo**：`include/axiom/topo/topology_service.h`、`src/topo/topology_service.cpp`、`tests/topo/topology_test.cpp`
- **rep**：`include/axiom/rep/representation_conversion_service.h`、`src/rep/representation_conversion_service.cpp`、`tests/rep/representation_io_test.cpp`
- **eval**：`include/axiom/eval/eval_services.h`、`src/eval/eval_services.cpp`、`tests/eval/query_eval_test.cpp`
- **ops**：`include/axiom/ops/ops_services.h`、`src/ops/ops_services.cpp`、`tests/ops/boolean_workflow_test.cpp`、`tests/ops/boolean_prep_test.cpp`（修改/查询等亦在本实现中）
- **heal**：`include/axiom/heal/heal_services.h`、`src/heal/heal_services.cpp`；专项回归见 `tests/ops/ops_heal_test.cpp`（与验证/修复工作流同测）
- **math**：`include/axiom/math/math_services.h`、`src/math/math_services.cpp`、`tests/math/math_services_test.cpp`

### 3.2.1 各模块完成度一览（架构快照，相对「工业几何引擎」）

说明：下表 **不等于接口是否齐全**，而是指在工业场景下可用的**算法深度、容差与可诊断闭环**是否到位。`Stage2 内可用度` 表示当前 Stage 1.5/2 过渡目标下支撑持续开发的便利程度。

| 模块 | Stage2 内可用度 | 工业化差距 | 主要回归/证据（ctest） | 最突出不足 |
|------|----------------|-----------|------------------------|------------|
| **core** | 中 | 高 | `axiom_smoke_test` | **工程化不变量**：`reset/clear` 后缓存/索引一致性、跨模块可观测性仍需更系统回归（IO 格式能力报告已与 `IOService` 主链路对齐并由 smoke 门禁） |
| **math** | 中 | 高 | `axiom_math_services_test` | 工业级鲁棒谓词、尺度自适应容差在全链路的一致解释仍未闭合 |
| **geo** | 中 | 高 | `axiom_geometry_test` | 高质量 B-Spline/NURBS、曲率与鲁棒最近点、真实 trim 语义 |
| **topo** | 中 | 高 | `axiom_topology_test` | 规则集仍不完整；但 **trim bridge（UV 闭合/面级 trim 一致性）与 Strict 闭合性（open/non-manifold）已进入回归**，离工业闭环仍有差距 |
| **rep** | 中 | 高 | `axiom_representation_io_test` | 全几何类型的误差预算三角化、工业 round-trip、显示/UV seam |
| **io** | 中偏低 | 高 | `axiom_io_workflow_test` | **mesh 格式导入**（如 STL/glTF）、更多互操作格式；**严格导出与输出验证报告**闭环（`Kernel` 与 `IOService` 能力报告已对齐并由 smoke 固化） |
| **ops** | 低 | **极高** | `axiom_boolean_workflow_test`、`axiom_boolean_prep_test`、`axiom_ops_heal_test` | **布尔真闭环**、特征建模真实拓扑、圆角倒角算法 |
| **heal** | 中偏低 | 高 | `axiom_ops_heal_test`、`axiom_io_workflow_test` | 自交、流形性完备、容差冲突与小特征工业修复与回放 |
| **eval** | 中 | 高 | `axiom_query_eval_test` | 与真实建模/重建深度耦合、缓存与指标门禁 |
| **diag** | 中偏高 | 中偏高 | `axiom_diagnostics_test` | 阶段化（BOOL/HEAL/IO）**全覆盖绑定**仍不足；但诊断检索/统计/JSON 导出与 related_entities 回流在回归中已相对扎实 |
| **plugin & sdk** | 低～中偏低 | 高 | `axiom_smoke_test`、`axiom_plugin_sdk_test`、`axiom_diagnostics_test`（插件注册失败诊断） | **OS 级隔离/安全**仍未具备；动态加载与签名校验；长期兼容性策略需产品化 |

### 3.2.2 模块完成度与不足（详细版，可转 backlog）

说明：本节把“工业化差距”展开成**可执行缺口**。每条缺口建议具备 DoD：实现 + 诊断 + 回归（必要时含性能门禁）。

- **core（Kernel/State/Result/Stores）**
  - **已具备**：`Kernel` 门面、基础配置写入/查询、运行时 store 清理/重置、对象计数与能力报告框架；**`io_supported_formats` / `io_can_import_format` / `io_can_export_format` 已与 `IOService::detect_format`、`import_auto`、`export_auto` 对齐**（`gltf`/`stl` 仅导出、不导入），并由 `axiom_smoke_test` 回归
  - **主要不足**：
    - **工程化不变量门禁**：reset/clear 后的缓存/索引一致性、跨模块可观测性仍需更系统回归
  - **建议测试入口**：`axiom_smoke_test`（能力报告与导入/导出能力断言）

- **diag（Diagnostics）**
  - **已具备**：错误码/诊断码常量、诊断报告、JSON 导出（含 `Issue.stage`）、按 related entity 检索；BOOL/HEAL/IO 关键路径已绑定阶段标签并有回归断言
  - **主要不足**：
    - **阶段化诊断全覆盖**：BOOL/HEAL/IO 等高风险链路仍需把**全部**失败分支纳入阶段结构与门禁（当前为关键路径首批绑定）
    - **批量导出/聚合策略**：面向 CI/回归资产归档的批量导出与检索效率策略仍缺
  - **建议测试入口**：`axiom_diagnostics_test` + 对应 workflow 测试中的失败分支断言

- **math（Tolerance/Predicate/Linear Algebra）**
  - **已具备**：容差解析与尺度策略雏形、线代统计/谓词与基础回归
  - **主要不足**：
    - **工业鲁棒谓词**：近退化/大尺度/NaN-Inf 传播的系统化行为定义与覆盖不足
    - **容差“全链路一致解释”**：Geo/Topo/Ops/Heal/Rep 对同一容差概念的解释仍需要更严格统一与回归固化
  - **建议测试入口**：`axiom_math_services_test`（增加退化与大尺度用例）

- **geo（Curves/Surfaces/PCurve/Eval/Closest）**
  - **已具备**：曲线/曲面/PCurve 的创建与最小 eval/domain/bbox/closest，含批量接口；派生曲面（revolved/swept/trimmed/offset）具备 stage2 minimal 语义
  - **主要不足**：
    - **高质量样条与曲率**：NURBS/BSpline 的导数/曲率、鲁棒反求（最近点/最近参数）与退化处理未工业化
    - **真实 Trim 语义**：Trimmed 目前偏“参数域裁剪占位”，缺基于 loop/coedge/PCurve 的修剪边界
  - **建议测试入口**：`axiom_geometry_test`（增加曲率/导数/退化场景后再逐步收紧）

- **topo（Transaction/Query/Validation/Trim Bridge）**
  - **已具备**：事务 begin/commit/rollback、反向索引查询、Strict 的闭合性/来源一致性一部分校验、trim bridge 的最小校验入口
  - **主要不足**：
    - **拓扑规则集不完整**：面环方向、重复/悬挂/非流形的系统化规则与诊断覆盖不足
    - **PCurve↔3D 一致性闭环**：仍缺可用于工业修剪的强一致性约束与（可选）修复策略
    - **事务可观测性/审计**：读写集、隔离级别、审计统计未落地
  - **建议测试入口**：`axiom_topology_test`（按规则逐条加严并固化诊断码）

- **rep（Tessellation/Conversion/Inspection/Reports）**
  - **已具备**：primitive 解析三角化 + owned topo face 组装三角化、顶点焊接、tessellation cache/face-cache、网格检查与 JSON 报告、BRep↔Mesh round-trip 报告与回归
  - **主要不足**：
    - **全类型误差预算**：修剪面/高阶曲面/派生体的误差预算与细分策略仍不足
    - **显示管线能力**：局部重三角化（patch）、UV seam/unwrap、增量更新与缓存指标门禁不足
  - **建议测试入口**：`axiom_representation_io_test`（报告字段与密度单调性）+ `axiom_io_workflow_test`（导出链路不回归）

- **io（STEP/AXMJSON/glTF/STL + workflow）**
  - **已具备**：STEP/AXMJSON 导入导出主链路；glTF/STL 导出最小实现；导入后 validation/auto-repair 与诊断回流
  - **主要不足**：
    - **导入侧互操作**：glTF/STL 导入、IGES/BREP 等互操作缺口
    - **严格/兼容模式与输出验证报告**：面向工业交付的导出策略与“输出验证闭环”缺失
    - **能力报告对齐**：Kernel 与 IOService 的格式能力口径需要回归门禁
  - **建议测试入口**：`axiom_io_workflow_test` + `axiom_diagnostics_test`

- **ops（Primitive/Sweep/Boolean/Modify/Blend/Query）**
  - **已具备**：前置分类、近似求交语义、来源传播、最小物化拓扑骨架与 Strict 回归闭环、部分修改/修复工作流语义
  - **主要不足（最大缺口）**：
    - **布尔真闭环**：精确求交/切分/分类/重建/验证修复闭环缺失
    - **特征建模真实构造**：extrude/revolve/sweep/loft/thicken 的真实几何与拓扑生成缺失
    - **圆角/倒角工业化**：含角区/变半径/失败分类与回归数据集缺失
  - **建议测试入口**：`axiom_boolean_workflow_test`、`axiom_boolean_prep_test`、`axiom_ops_heal_test`

- **heal（Validation/Repair）**
  - **已具备**：Strict 拓扑与来源一致性校验、导入后验证与可选自动修复语义、部分 repair 策略与 related_entities 回流
  - **主要不足**：
    - **工业验证项缺失**：自交、流形性完备、容差冲突、参数域异常检查仍缺
    - **工业修复策略缺失**：小特征/近重复/法向与退化处理策略与回放机制不足
  - **建议测试入口**：`axiom_ops_heal_test`、`axiom_io_workflow_test`

- **eval（EvalGraph）**
  - **已具备**：循环保护、失效传播、重算计数与治理接口，已有专项回归
  - **主要不足**：与真实建模/重建深度耦合与可度量指标（命中率/重算成本）门禁不足
  - **建议测试入口**：`axiom_query_eval_test`

- **plugin & sdk**
  - **已具备**：注册/清单/门面骨架与基础查询
  - **主要不足**：能力发现、隔离/安全、示例插件与回归、兼容性策略
  - **建议测试入口**：`axiom_smoke_test`、`axiom_plugin_sdk_test`、`axiom_diagnostics_test`（插件注册失败诊断）

## 3.3 对照《几何引擎功能需求文档》的差距清单（按模块）

说明：本节用“已完成 / 部分完成 / 未开始”对齐 `docs/plan/AxiomKernel_几何引擎功能需求文档.md` 的一级需求模块，便于下一批迭代拆解。这里的“已完成”指**具备可回归的最小可用链路**，不等于工业级。

### 7.1 几何基础对象管理（GeoCore）

- **曲线类型**
  - **已完成（基础可用）**：直线、线段、圆、椭圆、抛物线、双曲线、Bezier、BSpline（近似）、NURBS（近似）、复合曲线 polyline（占位）
  - **部分完成（Stage 2 minimal）**：复合曲线 chain（子曲线拼接到参数域 `[0,n]` 的最小实现）、`PCurve`（UV polyline 的最小实现，含 eval/domain/bbox/closest）
  - **未开始/缺失（按需求文档口径）**：高质量复合曲线（连续性/弧长参数化/拼接光顺）、完整 `PCurve`（与面参数域/修剪边界的强一致性与桥接规则）
- **曲面类型**
  - **已完成（基础可用）**：平面、圆柱、圆锥、球面、环面、Bezier 曲面（控制网格占位）、BSpline（控制网格）、NURBS（控制网格）
  - **部分完成（Stage 2 minimal）**：旋转面、扫掠面（线性扫掠）、修剪面（参数域裁剪占位）、偏置面（offset record 占位）
  - **未开始/缺失（按需求文档口径）**：真实修剪面（基于 `PCurve` 的 trim loops + 与 3D 曲线一致性）、高质量偏置面（几何意义上的 surface offset + 自交/退化处理）、高质量扫掠/放样/加厚等特征曲面
- **求值/查询能力**
  - **已完成（基础可用）**：eval / bbox / domain（含 analytic 的 ±inf 语义）、closest_point / closest_parameter / closest_uv（含 domain 收敛与角度归一化）
  - **未开始/缺失**：曲面曲率高质量实现、稳定最近点（迭代/鲁棒收敛）、参数域边界与修剪（trim）语义

### 7.2 拓扑结构管理（TopoCore）

- **拓扑元素与关系**
  - **已完成（基础可用）**：Vertex/Edge/Coedge/Loop/Face/Shell/Body；外环与内环；基础反向邻接索引；来源追踪的基础查询
  - **部分完成**：壳闭合/非流形检测（既有告警 + 新增 Strict hard 校验接口）；来源引用一致性 Strict 校验
  - **未开始/缺失**：面环方向规则集、悬挂边/重复边重复面更强规则、`PCurve` 与 3D curve 的一致性（trim bridge）
- **事务/一致性**
  - **已完成（基础可用）**：begin/commit/rollback；删除面/壳/体支持；回滚后索引恢复回归覆盖
  - **未开始/缺失**：更细粒度写集/读集、并发/隔离级别定义、事务可观测性（更系统的审计/统计）

### 7.3 基础建模能力（OpsCore/TopoCore/GeoCore）

- **基础体构造**
  - **部分完成**：`PrimitiveService` 已提供 `box/wedge/sphere/cylinder/cone/torus` 等；物化层对部分 primitive 可走 **解析壳路径**（如 `Wedge` 专用壳、`Cylinder` 棱柱壳等），其余仍常退回 **bbox 壳** 以满足 Strict/闭环回归；派生结果体仍大量依赖最小物化骨架、`source_*` 推导与来源壳克隆
  - **未开始/缺失（工业化）**：与工业内核一致的 **全 primitive 精确拓扑面环 + 与曲面参数域严格一致** 的 BRep；楔体/盒体等若仍部分依赖 bbox 占位壳，需升级为完整解析拓扑与几何
- **特征构造**
  - **部分完成（接口/占位）**：`SweepService` 等接口存在，部分路径产出派生体与工作流语义
  - **未开始/缺失（工业化）**：拉伸/旋转/扫掠/放样/加厚等 **真实几何求交 + 拓扑构造 + 失败可诊断** 的完整实现

### 7.4 布尔运算能力（OpsCore）

- **部分完成**：布尔前置、近似求交与占位语义、来源传播与诊断链路已具备基础框架
- **未开始/缺失**：候选对生成→精确求交→切分→分类→重建→验证/修复闭环；imprint/trim/merge；阶段化诊断（候选/求交/切分/分类/重建/验证/修复）

### 7.5 几何修改能力（OpsCore/HealCore）

- **部分完成**：偏置/抽壳/面替换/删除面补面等接口语义、失败/告警路径与来源传播已有基础回归
- **未开始/缺失**：真实几何修改算法、局部重建、稳定的失败分类（自交/薄壁/高曲率/容差冲突）与可回放修复记录

### 7.6 圆角与倒角（OpsCore）

- **部分完成**：接口与非法输入保护、占位语义与基础诊断
- **未开始/缺失**：真实圆角/倒角几何生成（含角区）、变半径、失败原因细分与回归数据集

### 7.7 查询与分析（Query/Eval/Rep）

- **部分完成**：bbox、点分类、部分距离/近似求交、截面（偏占位）与批量接口雏形；`QueryService::mass_properties` 等已存在，但对多数体仍偏 **bbox 近似**，非工业级物理属性
- **未开始/缺失**：长度/面积/体积/重心/惯性矩的工业级正确性；曲率/厚度分析；稳定的曲线-曲面/曲面-曲面求交

### 7.8 验证与修复（HealCore/Validation）

- **部分完成**：拓扑一致性/来源一致性/闭合性 Strict 校验；导入后验证与可选自动修复语义；缝合结果最小物化
- **未开始/缺失**：自交检查、流形性完整规则、容差冲突检查、参数域异常检查；小边/小面/法向/近重复归并等工业化修复策略与回放机制

### 7.9 数据交换（IO）

- **部分完成**：STEP/AXMJSON 导入导出主链路雏形、导入后自动验证与诊断回传；glTF/STL **导出**已具备最小实现（基于当前网格记录写出）
- **未开始/缺失**：IGES/BREP 完整互操作；STL/glTF **导入**；第二阶段 OBJ/3MF 等；导出严格/兼容模式与“输出验证报告”闭环（**Kernel 与 IOService 的格式能力报告已与 `import_auto`/`export_auto`/`detect_format` 对齐**，见 `axiom_smoke_test`）

### 7.10 三角化与显示支撑（Rep）

- **部分完成**：三角化语义与参数校验、基础检查报告雏形；已开始具备（1）primitive 的解析三角化（可曲率敏感，受 chordal/angular options 影响）（2）有 owned topo 的派生体按 face 组装三角化 + 顶点焊接（3）tessellation cache 与 face tessellation cache（含失效清理）
- **未开始/缺失**：更完整的曲率敏感细分（覆盖更多曲面/修剪面/高阶曲面）；更可靠的局部重三角化（patch 级误差控制与 seam/weld 规则）；显示级缓存/增量更新；纹理参数（UV seam/unwrap）等完整输出

### 7.11 版本/事务/增量更新（Core/EvalGraph）

- **部分完成**：版本号与事务、EvalGraph 基础失效传播/重算计数/循环依赖保护
- **未开始/缺失**：结构共享策略细化、跨模块增量重算接入真实建模链路、缓存命中指标体系

### 7.12 插件扩展（PluginSDK）

- **部分完成**：`PluginRegistry` 注册与清单查询；**清单↔实现绑定**（`PluginManifest::implementation_type_name`：带实现注册时自动填充或校验与 `type_name()` 一致；按 `type_name` 注销实现时同步移除绑定清单）；**注销路径可诊断**（`unregister_*`；门面 **`unregister_plugin_*`**）；`PluginHostPolicy` 含 **`PluginSandboxLevel`**、容量与 API 版本等门禁；**`PluginApiVersionMatchMode`**；`find_manifest` 未命中带 **`kPluginLoadFailure`**；能力发现 JSON 含 **`manifests`** 摘要；`validate_after_plugin_mutation`、`register_plugin_*` 诊断；示例与 `axiom_plugin_sdk_test` / smoke 回归
- **未开始/缺失**：**进程外/OS 级隔离**、动态库加载与供应链安全（签名/沙箱）；**完整 SemVer 与多 ABI 并存**（当前仅为宿主侧轻量版本三元组解析）；插件侧自动 validate 仍依赖宿主/插件显式调用约定

### 7.13 诊断与日志（Diagnostics）

- **部分完成**：错误码常量、诊断报告、JSON 导出、按相关实体检索、部分链路绑定
- **未开始/缺失**：覆盖所有失败路径的系统化绑定；阶段化诊断（尤其 BOOL/HEAL/IO）；诊断聚合检索与批量导出策略

### 7.14 混合表示与转换（RepCore）

- **部分完成**：BRep/Mesh/Implicit 的基础语义与部分测试；BRep↔Mesh round-trip 报告与网格检查已有专项回归
- **未开始/缺失**：高质量转换、工业级误差控制、混合建模策略与对外接口长期冻结

## 3.4 距离“工业几何引擎”的核心不足清单（架构视角）

说明：本节聚焦“要达到工业几何引擎”必须补齐的关键能力，不等价于“接口存在”。每条不足都应能映射到可验证的 DoD（实现 + 诊断 + 回归）。

### A) GeoCore：高质量几何与鲁棒查询不足

- **样条与高阶曲面工业化**：B-Spline/NURBS 的高质量求值、导数/曲率、稳定反求（最近点/最近参数）与退化处理仍不足。
- **Trim 语义未工业化**：当前 `Trimmed` 更多是参数域裁剪占位；缺少基于 loop/coedge/PCurve 的真实修剪边界、以及与 3D curve 一致性的完整规则与算法闭环。
- **统一公差与尺度策略的贯通不足**：几何求值/最近点/求交/验证/修复对 tolerance 的一致解释仍需要进一步收敛到“可预测、可回归”的策略中心。

### B) TopoCore：一致性规则集与 trim bridge 不足

- **拓扑规则集不完整**：面环方向、重复/悬挂/非流形的完整规则与诊断覆盖仍不足，Strict 目前更多聚焦闭合性与来源一致性的一部分。
- **PCurve-3D 一致性（trim bridge）未闭环**：已有最小校验接口，但缺少可用于工业修剪的强一致性约束与修复策略。
- **事务可观测性/审计不足**：写集/读集、隔离级别、审计统计等工程化能力未落地。

### C) OpsCore：工业级建模算法缺失（最大缺口）

- **布尔闭环缺失**：候选对生成 → 精确求交 → 切分 → 分类 → 重建 → 验证/修复闭环未实现；当前更多是前置与近似/占位语义。
- **特征建模缺失**：extrude/revolve/sweep/loft/thicken 等仍缺真实几何与拓扑构造；目前主要依赖最小物化拓扑骨架支撑工作流回归。
- **圆角/倒角缺失**：真实圆角倒角（含角区、变半径）未实现，失败原因细分与回归数据集不足。

### D) Heal/Validation：工业化验证与修复不足

- **自交/流形性/容差冲突**：关键工业验证项仍缺；修复策略（小边小面、近重复归并、法向/退化处理）尚未形成可回放与可追踪的工业闭环。

### E) Rep/Conversion：误差控制与 round-trip 的工业化不足

- **误差预算与质量度量不足**：需要把“误差预算（budget）→细分策略→质量报告→round-trip 验证”做成稳定闭环，并覆盖更多几何类型/修剪体。
- **显示管线能力不足**：局部重三角化、缓存更新、纹理坐标与 seam 规则仍不足以支撑工业显示/编辑体验。

### F) IO：互操作与能力报告收敛不足

- **格式互操作缺口**：IGES/BREP/STL/glTF **导入**等缺失；导出严格/兼容模式与输出验证报告闭环未完成。
- **能力报告**：`Kernel` 与 `IOService` 主链路格式集合**已对齐**并由 `axiom_smoke_test` 固化；后续新增格式须同步改门面与测试。

### G) Diagnostics/Eval/Plugin：工程化可观测与扩展不足

- **诊断覆盖率未系统化**：虽然已有诊断服务与导出/检索能力，且 **BOOL/HEAL/IO 关键路径已绑定 `Issue.stage` 并进入 `export_report_json`/`axiom_boolean_workflow_test`/`axiom_diagnostics_test`/`axiom_ops_heal_test` 回归**，但“覆盖所有失败路径与阶段化归类”仍未形成系统门禁。
- **EvalGraph 未进入参数化求解级**：当前更像状态/依赖治理基础设施，尚未与真实建模/重建深度耦合。
- **插件工程化不足**：进程内能力发现、宿主策略与诊断闭环已有雏形（见 7.12），**隔离/安全与动态扩展**仍不足以支撑开放生态。

## 4. 当前主要风险

- 当前 `OpsCore` 仍未进入真实工业算法阶段
- 当前 `TopoCore` 尚未形成严格拓扑一致性规则集
- 当前 `TopoCore` 虽已具备基础事务、验证、邻接查询和基础反向索引，但仍缺少更完整的稠密关联结构
- 当前 `GeoCore` 还没有高质量样条和参数反求实现
- 诊断码与错误码**已在主链路与多条回归中绑定**，但距离“所有失败路径可检索、可阶段化归类”的工业门禁仍有明显差距

## 5. 下一迭代 Sprint 焦点与本阶段 backlog

阶段口径保持：`Stage 1.5 / Stage 2 过渡：核心公共层增强 + 基础几何/拓扑深化`。本节区分 **本迭代可交付**、**最近已闭合批次** 与 **全项目长期树**，避免与 §6 历史清单重复。

### 5.1 Sprint 焦点（当前迭代 3～5 条，可验收）

1. **diag + ops**：扩展 BOOL 阶段化诊断覆盖与 JSON `stage` 字段在更多失败分支的一致性；布尔真求交子里程碑（面级候选 → 交线 → imprint）按 DoD 切片交付。  
   **DoD**：对应码可检索；`axiom_boolean_workflow_test` / `axiom_boolean_prep_test` 不回归。  
2. **math + geo**：容差与谓词在退化/大尺度下的行为定义 + `axiom_math_services_test` 用例；样条导数/曲率或稳定最近点的最小增量。  
   **DoD**：失败有稳定错误码；几何测试覆盖新增语义。  
3. **topo**：Strict 规则逐项加严（面环方向、悬挂/重复等）与 trim bridge 可物化子规则。  
   **DoD**：`axiom_topology_test` 失败类回归 + 诊断码。  
4. **heal + io**：导入侧 mesh 格式导入或验证项（自交/流形性）中的下一可闭合条；维持导入后验证/修复诊断与 `related_entities` 可追溯。  
   **DoD**：`axiom_io_workflow_test` / `axiom_ops_heal_test` 增量断言。

### 5.2 本阶段 backlog 表（唯一入口，随迭代刷新）

说明：**状态**列区分已纳入门禁的交付与仍在推进项，避免与 §6 已落地条目冲突。

| 优先级 | 状态 | 模块 | 交付物（摘要） | 建议 `ctest` | 依赖 |
|--------|------|------|----------------|--------------|------|
| P0 | 已闭合（门禁） | core/io | 门面 IO 能力与 `IOService` 一致 | `axiom_smoke_test` | — |
| P0～P1 | 已闭合（首批） | diag/ops/io/heal | 工作流 `Issue.stage` + JSON 导出可聚合 | `axiom_diagnostics_test`、`axiom_boolean_workflow_test`、`axiom_ops_heal_test` | core |
| P1 | 进行中 | math | 退化/尺度谓词与容差策略回归 | `axiom_math_services_test` | core |
| P1～P2 | 进行中 | geo/topo | trim / trim bridge / Strict 规则 | `axiom_geometry_test`、`axiom_topology_test` | math |
| P2 | 进行中 | ops | 布尔非 bbox 结果子里程碑 | `axiom_boolean_*` | geo/topo |
| P2～P3 | 进行中 | eval/rep | 重算指标、Rep 误差预算 | `axiom_query_eval_test`、`axiom_representation_io_test` | ops（部分） |

### 5.3 最近已关闭的功能批次（与 §6 互证；以下为已落地摘要）

下列条目已在仓库代码与回归中闭合，**详细时间线见 §6**（含早期「求值图循环依赖 `AXM-EVAL-E-0001`」至「EvalGraph 治理能力」及 **§6 尾部** 最近条目）。

1. 求值图循环依赖失败路径绑定 `AXM-EVAL-E-0001`；重算 DAG 去重；脏依赖防护；`body` 绑定去重。  
2. 表示层点分类线性容差；点到体距离无效包围盒语义；`BRep/Implicit -> Mesh` 参数校验与细分映射。  
3. `query_eval` / `representation_io` 等对上述语义的回归覆盖。  
4. `Kernel::io_supported_formats` / `io_can_import_format` / `io_can_export_format` 与 `IOService::detect_format`、`import_auto`、`export_auto` 对齐（`gltf`/`stl` 仅导出），`axiom_smoke_test` 固化。  
5. `Issue::stage` 字段；诊断 JSON/文本导出；BOOL/HEAL/IO 关键路径阶段标签与 `axiom_boolean_workflow_test`、`axiom_diagnostics_test`、`axiom_ops_heal_test` 回归断言。  
6. 《主开发计划与阶段路线图》`§1`：Stage 1 已达成、处于 Stage 1.5/2 过渡；本文档 §5 Sprint/backlog 结构重写。

### 5.4 下一未闭合批次（建议按表 5.2 顺序推进）

1. BOOL 全阶段失败路径诊断绑定与工业数据集雏形。  
2. 特征建模（拉伸/旋转/扫掠）真实拓扑物化，替代过度依赖最小骨架。  
3. HEAL 自交/流形性/容差冲突的验证与可回放修复。  
4. IO：STL/glTF 导入或 IGES 互操作（与产品路线一致后择一）。  
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
- 对齐 `Kernel::io_supported_formats` / `io_can_import_format` / `io_can_export_format` 与 `IOService::detect_format`、`import_auto`、`export_auto`（`gltf`/`stl` 仅导出），并由 `axiom_smoke_test` 固化
- 为 `Issue` 增加 `stage` 字段；诊断 JSON/文本导出携带阶段；布尔/导入验证/修复管线与 `AXM-HEAL-D-0006` 等绑定稳定 `stage` 标签；`axiom_boolean_workflow_test`、`axiom_diagnostics_test`、`axiom_ops_heal_test` 增加回归断言
- 更新《主开发计划与阶段路线图》当前阶段表述为 Stage 1 已达成、处于 Stage 1.5/2 过渡；重写本文档 §5 Sprint/backlog 结构

## 7. 结论

当前项目已经从“纯文档阶段”进入“真正可持续编码阶段”。下一步不应盲目堆高级功能，而应继续稳住：

- 公共层
- 诊断体系
- 几何/拓扑基础
- 测试闭环

在这个基础上，再进入真实布尔、修复、圆角、偏置等工业难点实现。
