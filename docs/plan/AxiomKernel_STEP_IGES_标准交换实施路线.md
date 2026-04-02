# AxiomKernel：标准 STEP / IGES 全实体交换实施路线

本文档界定「工业级标准 STEP/IGES」与当前仓库能力边界，并给出可验收的分阶段路线。  
**事实前提**：完整交换需要 **EXPRESS 语义**（STEP）或 **DE/PD 关联与实体定义**（IGES），并将结果 **物化为 Axiom 的 `BodyRecord` + 拓扑/几何**，工作量与 **外部内核（STEPcode、Open CASCADE 等）** 或等价自研解析器相当；不可能仅靠注释行/元数据子集完成。

## 1. 当前已实现（里程碑 0～0.5）

| 能力 | 说明 |
|------|------|
| Axiom 子集 STEP/IGES | HEADER/注释元数据 + 参数体恢复等现有主链路 |
| 标准形态显式拒绝 | `import_step` / `import_iges` 对典型标准物理文件返回 `NotImplemented`，错误码 **`AXM-IO-E-0010` / `AXM-IO-E-0011`**，避免静默假成功 |
| 物理层扫描摘要（Info） | **`AXM-IO-D-0016`**：DATA 段 `#id=EXPRESS_TYPE` 实例计数与类型名频度 Top；**`AXM-IO-D-0017`**：疑似 DE 行与 IGES 实体类型号（字段 1）频度 Top。**不**构造曲面/边/壳，仅供诊断与 CI 可解析 |
| 实现位置 | `src/axiom/internal/io/step_iges_standard_scan.cpp` |

扫描器为 **启发式物理层** 解析（括号/引号内分号处理等），不替代 SCHEMA 校验与完整 AP 语义。

## 2. 里程碑 1：外部内核集成骨架（CMake + 可选编译）

- 增加 **`AXM_ENABLE_STEP_IGES_BRIDGE`**（默认 `OFF`）或分列 STEP / IGES 开关。
- `find_package` / `FetchContent` 引入 **STEPcode** 或 **Open CASCADE**（需与项目法务确认 **LGPL** 等许可证链路）。
- 桥接目标 `axiom_io_step_bridge`（`PRIVATE` 链接），**未开启时**行为与现版一致。
- 验收：`ctest` 在开关 OFF 时全绿；ON 时在 CI 镜像中至少通过 **编译 + 链接**（可无运行时用例）。

## 3. 里程碑 2：读入 → 中性 BRep 或网格

- STEP：读入选定 AP（如 CONFIG_CONTROL_DESIGN / AP214 子集）→ 中性拓扑 + 几何句柄。
- IGES：读入 Type 186/144 等常见实体子集 → 同上或先落 **三角网格** 再可选 BRep。
- 验收：固定小型工业样例（或开源样例）导入后 **`validate_all` Standard** 可通过或失败带稳定 **HEAL/TOPO** 码；**禁止**仅 bbox 假成功。

## 4. 里程碑 3：写入与往返

- 导出子集 STEP/IGES（与读入 schema 对齐声明）。
- 验收：选定样例 **导入 → 导出 → 再导入** 的 bbox/体积/面数等在约定误差内；诊断可追踪。

## 5. 里程碑 4：与 Heal / 事务 / 诊断闭环

- 导入失败阶段 `io.import.step` / `io.import.iges` 细分；`related_entities` 绑定路径与可选 `BodyId`。
- 大文件内存与 **64MB 探测上限** 策略与产品一致（流式或 mmap）。

## 6. 建议决策点（需产品/架构拍板）

1. **首选内核**：OCCT（生态成熟）vs STEPcode（更轻、集成成本高）。  
2. **交付形态**：静态链入内核 vs **进程外转换服务**（插件/子进程），影响许可证与部署。  
3. **支持 AP/实体范围**：先 AP214 实体子集再扩展，避免「全 AP242」一次性承诺。

---

**结论**：「标准 STEP/IGES **全实体**」= 里程碑 1～4 的连续交付；当前仓库已完成 **拒绝路径 + 物理层扫描摘要**，为后续桥接提供可测试、可检索的基线。
