# AGENTS.md（AxiomKernel 项目开发指南）

本文件面向所有在本仓库中写代码的人（含人类开发者与自动化代理/脚本）。目标是把仓库内分散的规则收敛为**可执行的日常开发约定**：怎么构建、怎么写接口、怎么返回错误、怎么补测试、怎么提 MR、怎么排障与验收。

> 详细背景与设计依据请参阅 `docs/` 下的 `AxiomKernel_*.md` 文档索引（见本文末尾）。

---

## 开发者日常工作流（最少步骤）

- **拉起与构建（推荐）**：

```text
cmake -S . -B build -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON
cmake --build build
ctest --test-dir build
```

- **清理重建（排除缓存/编译器切换问题）**：

```text
rm -rf build
cmake -S . -B build -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON
cmake --build build
ctest --test-dir build
```

- **常用构建类型**：
  - **Debug**：本地调试与诊断（断言/内部检查建议开启）
  - **RelWithDebInfo**：接近真实性能的调试复现
  - **Release**：性能基线与发布制品

- **建议的 CMake 选项（按需打开）**：
  - `AXM_ENABLE_TESTS`
  - `AXM_ENABLE_EXAMPLES`
  - `AXM_ENABLE_BENCHMARKS`
  - `AXM_ENABLE_DIAGNOSTICS`
  - `AXM_ENABLE_STRICT_WARNINGS`

> 说明：以当前仓库 `CMakeLists.txt` 为准，上述选项中 **实际已定义并生效** 的包括  
> `AXM_ENABLE_TESTS/AXM_ENABLE_EXAMPLES/AXM_ENABLE_BENCHMARKS/AXM_ENABLE_DIAGNOSTICS/AXM_ENABLE_STRICT_WARNINGS`。

---

## 仓库结构与模块边界（必须遵守）

### 目录约定（以当前仓库为准）

- **`include/axiom/`**：对外公开头文件（Public API）
- **`src/`**：实现（不应泄漏实现细节到 `include/`）
- **`tests/`**：自动化测试（按模块子目录组织，见下）
- **`cmake/`**：CMake 片段（模块库定义等）
- **`examples/`**：示例程序（用于演示主链路可用）

### 构建 target 约定（模块化）

- 代码按模块拆分为多个 CMake target（例如 `axiom_math/axiom_geo/axiom_topo/...`），并用 `target_link_libraries` 显式表达依赖方向（参见“允许依赖/禁止依赖”章节）。
- 各模块库定义集中在 `cmake/AxiomKernelLibraries.cmake`，根目录 `CMakeLists.txt` 负责选项、示例与测试 target。
- `axiom_kernel` 作为聚合入口 target（供示例/工作流测试链接），避免测试因链接过小而丢失集成链路覆盖。

### 当前仓库的模块目录（对齐实际代码）

- **Public API（对外头文件）**：`include/axiom/`
  - `core/`：`types.h`, `result.h`
  - `diag/`：`error_codes.h`, `diagnostic_service.h`
  - `eval/`：`eval_services.h`
  - `geo/`：`geometry_services.h`
  - `topo/`：`topology_service.h`
  - `rep/`：`representation_conversion_service.h`
  - `ops/`：`ops_services.h`
  - `heal/`：`heal_services.h`
  - `io/`：`io_service.h`
  - `plugin/`：`plugin_registry.h`
  - `sdk/`：`kernel.h`
- **Internal（内部实现细节头）**：`src/axiom/internal/`（模块私有，不安装；禁止上层/插件绕过 Public API 直接依赖）
- **实现源码**：`src/<module>/...`（与 `include/axiom/<module>/...` 对应；internal 头通过 CMake 的 PRIVATE include 暴露给允许依赖的模块）
- **测试**：`tests/<模块>/*_test.cpp`（按模块分子目录；`ctest` 名称不变）
- **示例**：`examples/basic_workflow.cpp`、`examples/minimal_plugin.cpp`（插件宿主与能力发现）

### 模块分层（核心约束）

项目采用“分层模块 + 统一诊断链路”的理念。你写的每一行代码都必须能回答两个问题：
- **它属于哪一层/哪个模块？**
- **它依赖方向是否正确？**

#### 允许依赖（高层依赖低层）

- `GeoCore -> MathCore`
- `TopoCore -> GeoCore`
- `RepCore -> GeoCore + TopoCore`
- `OpsCore -> MathCore + GeoCore + TopoCore + RepCore + Diagnostics`
- `HealCore -> MathCore + GeoCore + TopoCore + RepCore + Diagnostics`
- `EvalGraph -> Core Handles + Diagnostics`
- `IO -> RepCore + GeoCore + TopoCore + Diagnostics`
- `PluginSDK -> Kernel Facade / Public API`

#### 禁止依赖（评审硬门禁）

- `MathCore -> 任何上层模块`
- `GeoCore -> TopoCore`（几何层不反向知道拓扑）
- `TopoCore -> OpsCore / IO`
- `Diagnostics -> 业务算法实现`（诊断不能“变成算法模块”）
- `PluginSDK -> 内部私有数据结构/内存布局`

---

## API 与错误处理（对外接口的硬标准）

### 统一返回：结构化结果，而不是裸 `bool`

- **所有可能失败的对外操作**必须返回结构化结果（例如 `Result<T>` / `OpReport` / `DiagnosticReport`）。
- **失败必须可诊断**：至少包含稳定错误码；重量级操作必须能给出 `diagnostic_id`（诊断轨迹）。
- **不要用异常作为常规失败路径**：异常仅用于真正不可恢复的编程错误或极少数内部致命情形。

#### 在当前仓库中如何落地（对齐实际类型）

- **统一返回类型**：`axiom::Result<T>`（见 `include/axiom/core/result.h`）
- **错误码常量定义位置**：`include/axiom/diag/error_codes.h`（命名空间 `axiom::diag_codes`）
- **诊断查询/导出**：`axiom::DiagnosticService`
  - 导出文本：`export_report(...)`
  - 导出 JSON：`export_report_json(...)`

### 错误码/诊断码规范（必须稳定）

- **错误码**：`AXM-[模块]-E-[四位编号]`
- **警告码**：`AXM-[模块]-W-[四位编号]`
- **诊断码**：`AXM-[模块]-D-[四位编号]`
- **信息码**：`AXM-[模块]-I-[四位编号]`

要求：
- 同一种根因尽量复用同一错误码，避免“改文案不改码”或“换码不换语义”的混乱。
- 新增失败路径时，先考虑是否属于已有错误码；确需新增时必须同步文档字典与测试覆盖。

### 诊断输出与“可解释失败”

以下模块属于**高风险/高复杂链路**，默认要求更强的诊断：
- `BOOL`（布尔）
- `HEAL/VAL`（修复与验证）
- `IO`（导入导出）

失败时必须能区分阶段（示例：布尔至少要能区分候选对/求交/切分/分类/重建/验证/修复哪个环节失败）。

---

## 命名与术语（减少歧义是生产力）

### 不允许混淆的概念

- **`Surface`（几何曲面） ≠ `Face`（拓扑面）**
- **`Curve`（几何曲线） ≠ `Edge`（拓扑边）**
- **`Shell`（拓扑壳） ≠ `Shelling`（抽壳操作）**

### 命名风格（建议统一）

- **类型**：`PascalCase`（如 `BodyId`, `TolerancePolicy`）
- **函数**：建议 API 层统一 `snake_case`（如 `make_plane`, `validate_all`）
- **变量**：小写下划线（如 `input_body`, `candidate_faces`）

---

## 事务与一致性（修改型能力的基本盘）

- **修改型操作必须事务化**：要么显式 `begin_transaction/commit/rollback`，要么由服务层内部确保“失败不污染原模型”。
- **禁止半成功泄漏**：一旦失败，必须保证可回滚到一致状态（尤其是拓扑修改与派生结果体物化链路）。
- **验证器是闭环的一部分**：关键操作后建议显式调用 `validate_all`，并把验证失败折回诊断体系。

---

## 测试策略（质量门禁，不是可选项）

### 测试分层

必须按层补齐（至少包含）：
- **单元测试**：小函数/小类型的正确性（不依赖大数据集）
- **组件测试**：模块内协作（如 factory + service）
- **集成测试**：跨模块链路（如“导入→修复→导出”“建模→布尔→验证”）
- **回归测试**：每个重要缺陷必须沉淀为回归案例
- **Fuzz/属性测试（建议放夜间）**：覆盖退化与边界输入
- **性能基线（建议独立门禁）**：必须可度量、可对比、可追溯

### 必测原则

- 正确性优先于性能
- 失败可诊断优先于静默失败
- 回归稳定优先于偶然成功

### 当前仓库的测试目标（对齐实际 `ctest` 名称）

这些名字可以直接用于 `ctest -R`：

- **smoke**：`axiom_smoke_test`
- **plugin / sdk**：`axiom_plugin_sdk_test`（宿主策略、能力发现 JSON、`register_plugin_*` 诊断闭环）
- **diagnostics**：`axiom_diagnostics_test`
- **geometry**：`axiom_geometry_test`
- **topology**：`axiom_topology_test`
- **math**：`axiom_math_services_test`
- **ops/heal**：`axiom_ops_heal_test`
- **io workflow**：`axiom_io_workflow_test`
- **representation + io**：`axiom_representation_io_test`
- **query + eval**：`axiom_query_eval_test`
- **boolean workflow**：`axiom_boolean_workflow_test`
- **boolean prep**：`axiom_boolean_prep_test`
- **perf baseline**：`axiom_perf_baseline_test`（带 30s 超时）

### 运行单个测试 / 子集（推荐）

```text
ctest --test-dir build -R axiom_geometry_test
ctest --test-dir build -R "axiom_(geometry|topology)_test"
ctest --test-dir build --output-on-failure
```

---

## 性能基线与数据集（避免“感觉优化”）

### 建议记录的指标

- 总耗时、平均耗时、P95
- 峰值内存
- 错误率/警告率
- 高精度回退触发次数（例如谓词/分类回退）

### 结果记录格式建议

建议以结构化 JSON 形式记录（便于 CI 比对与回归分析）。

### 当前仓库的性能基线开关（对齐实际实现）

`axiom_perf_baseline_test` 支持用环境变量调节门槛：

- **`AXM_PERF_MAX_MS`**：最大允许总耗时（毫秒），默认 `4000`
- **`AXM_PERF_ITERATIONS`**：迭代次数，默认 `150`

示例：

```text
AXM_PERF_MAX_MS=2000 AXM_PERF_ITERATIONS=80 ctest --test-dir build -R axiom_perf_baseline_test --output-on-failure
```

---

## 合并请求（MR）与评审（必须按模板）

### MR 必填信息（最小）

- 变更背景与范围
- 影响模块
- 是否修改公开接口
- 是否新增/修改错误码或诊断码
- 测试情况（跑了什么，新增了什么）
- 风险评估（尤其 BOOL/HEAL/IO/容差/事务）
- 文档同步情况

### 评审硬检查点（拒绝合并的常见原因）

- 新能力无测试
- 失败路径无错误码/无诊断
- 破坏模块边界或引入反向依赖
- 引入静默错误风险
- 把修复逻辑塞进基础层（如几何/拓扑底层）
- 公开接口变更但未更新接口清单/样例/错误码文档

---

## 缺陷处理（必须形成回归资产）

### 严重级别（简表）

- **S0**：崩溃 / 数据损坏 / 静默错误 / 事务回滚失败污染
- **S1**：核心链路不可用（布尔主链路、主 IO 链路、验证器失效等）
- **S2**：边界场景失败、诊断缺失、局部性能回退
- **S3**：文档/文案/低影响质量问题

### 修复闭环（必须完成）

复现 → 分级 → 修复 → 验证 → **回归沉淀** → 关闭  
尤其：`S0/S1` 与布尔/修复/导入导出相关的 `S2` 必须沉淀回归案例（含输入数据、期望输出、错误码预期、最小复现路径）。

---

## 插件与远程调用（边界先定清）

### 插件（PluginSDK）

- 插件只能通过 **公开 API** 访问内核，不得依赖私有内存布局。
- 插件失败必须返回结构化结果（不要用裸异常作为常规失败）。
- 插件输出建议经过验证器校验，并能输出诊断信息（至少错误码，最好有诊断轨迹）。

### REST/远程服务化（仅作为形态参考）

若对外暴露服务接口，建议：
- 把 `diagnosticId` 作为一等公民（响应里返回，且可 `GET /diagnostics/{id}` 查询）
- 返回结构与本地 `Result/Report` 语义一致（便于本地与云端共用测试与回归资产）

---

## 文档同步规则（代码变更的必做项）

出现以下变化时必须同步文档（至少同步对应文档条目或索引）：
- 新模块加入、目录结构变化
- 公共 API / 门面签名变化
- 错误码/诊断码新增或语义变更
- 测试策略、质量门禁或数据集规则变化

### “改动完成”自检清单（建议每个 MR 都过一遍）

- **接口**：是否只改了 `src/` 却忘了 `include/`（或相反）？Public API 是否保持稳定？
- **错误码**：是否新增失败路径？是否在 `include/axiom/diag/error_codes.h` 增补常量，并同步 `docs/diagnostics/AxiomKernel_错误码与诊断码字典.md`？
- **诊断**：失败是否能通过 `diagnostic_id` 追踪？是否能导出 JSON 以便 CI/回归归档？
- **事务/一致性**：修改型操作失败是否保证不污染原模型？（至少要能回滚到一致状态）
- **测试**：是否新增/更新了对应 `tests/<模块>/` 下的回归？是否可用 `ctest -R ...` 稳定复现？
- **性能**：是否触碰热路径（循环/批处理/布尔/IO）？是否需要更新/新增 `perf_baseline` 或记录对比数据？
- **文档**：是否需要同步接口样例、评审清单、或用户可读文案映射？

---

## 详细文档索引（按主题查阅）

- **架构与边界**：
  - `docs/architecture/AxiomKernel_几何引擎技术架构文档.md`
  - `docs/architecture/AxiomKernel_模块依赖图与时序图.md`
  - `docs/api/AxiomKernel_详细模块接口清单.md`
- **构建与工程**：
  - `docs/build/AxiomKernel_构建系统与编译选项建议.md`
  - `docs/build/AxiomKernel_代码目录与编码规范建议.md`
- **测试与质量**：
  - `docs/quality/AxiomKernel_测试与验收方案.md`
  - `docs/quality/AxiomKernel_基准数据集与性能管理规范.md`
  - `docs/build/AxiomKernel_CI与发布流水线建议.md`
- **错误码/诊断与用户提示**：
  - `docs/diagnostics/AxiomKernel_错误码与诊断码字典.md`
  - `docs/diagnostics/AxiomKernel_用户可读错误文案映射表.md`
- **流程模板**：
  - `docs/templates/AxiomKernel_合并请求模板.md`
  - `docs/templates/AxiomKernel_代码评审清单模板.md`
  - `docs/templates/AxiomKernel_缺陷分类与处理流程.md`
  - `docs/templates/AxiomKernel_发布说明模板.md`
- **使用样例**：
  - `docs/api/AxiomKernel_接口调用样例集.md`
  - `docs/api/AxiomKernel_插件开发样例集.md`
  - `docs/api/AxiomKernel_REST与远程调用样例集.md`
- **计划与进度**：
  - `docs/plan/AxiomKernel_MVP实施蓝图.md`
  - `docs/plan/AxiomKernel_主开发计划与阶段路线图.md`
  - `docs/plan/AxiomKernel_当前开发进度.md`
  - `docs/plan/AxiomKernel_几何引擎功能需求文档.md`
  - `docs/plan/AxiomKernel_术语表与命名约定.md`

