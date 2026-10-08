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

### cycle-0085 / S5-IO 默认路径事实

当前 CMake **未定义或启用** `AXM_ENABLE_STEP_IGES_BRIDGE`，默认行为等同未启用桥接；不是实际执行了 `-DBRIDGE=OFF`。本轮无外部依赖，里程碑 1～4 的 ON 路线 DoD 不适用。默认子集及标准拒绝回归随调度器完整 **16/16、0 失败、227.56 s** 通过，[三条证据](../quality/AxiomKernel_测试与验收方案.md#112-cycle-0085--s5-io-门禁与逐项证据)与 [API 支持矩阵](../api/AxiomKernel_详细模块接口清单.md#1111-stage-5-受限-io-主链路cycle-0085--s5-io)区分零 owned shells 的元数据与实际 STL 三角网格。标准实体混入 Axiom 标记仍优先拒绝，合法 IGES Hollerith 长标签回归保留；扫描是启发式物理检测。STEP/IGES/BREP/STL 64 MiB 预读预算、严格 STEP/STL 损坏拒绝和八格式单主文件发布均已落地，不证明单位转换、标准全实体交换或跨文件事务。

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
- 大文件内存与 **64 MiB（67108864 字节）预读上限** 策略与产品一致（流式或 mmap）。

## 6. 建议决策点（需产品/架构拍板）

1. **首选内核**：OCCT（生态成熟）vs STEPcode（更轻、集成成本高）。  
2. **交付形态**：静态链入内核 vs **进程外转换服务**（插件/子进程），影响许可证与部署。  
3. **支持 AP/实体范围**：先 AP214 实体子集再扩展，避免「全 AP242」一次性承诺。

---

**结论**：「标准 STEP/IGES **全实体**」= 里程碑 1～4 的连续交付；当前仓库已完成 **拒绝路径 + 物理层扫描摘要**，为后续桥接提供可测试、可检索的基线。
