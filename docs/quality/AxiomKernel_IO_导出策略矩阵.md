# AxiomKernel IO：`ExportOptions` 策略矩阵（工程口径）

本文档固化 **网格类导出**（STL / glTF / OBJ / 3MF）与 **体元数据导出**（STEP / AXMJSON / IGES / BREP）的主要开关组合，供 MR 评审与 CI 回归对齐；**不等于**工业交付矩阵（全格式误差预算、全 STEP 实体仍见进度文档）。

## 1. `ExportOptions` 字段

| 字段 | 默认 | 语义 |
|------|------|------|
| `compatibility_mode` | `false` | `false`：网格导出前 `inspect_mesh` 严格门控，越界索引/退化三角形 → `AXM-IO-E-0006`；`true`：跳过退化三角形门控；两种模式均在打开文件前拒绝空网格、非法索引和非有限坐标，glTF 还拒绝超出 float32 范围的坐标。 |
| `embed_metadata` | `true` | STEP/AXMJSON/IGES/BREP 等写出 Axiom 元数据行；`false` 时仍写出容器格式，但几何提示可能退化。 |
| `write_mesh_validation_report` | `false` | 为真时写出 `stem.mesh_report.json` 侧车，并在主导出诊断中合并 `AXM-IO-D-0008`。 |

## 2. 推荐组合（最小门禁）

| 场景 | `compatibility_mode` | `write_mesh_validation_report` | 期望 |
|------|---------------------|--------------------------------|------|
| CI / 交付严格 | `false` | 按需 `true` | 网格不合格则失败可诊断；侧车用于归档。 |
| 兼容旧宿主 / 快速预览 | `true` | `false` | 允许尽力写出；无严格 QA。 |
| 严格 + 侧车 | `false` | `true` | 与 `axiom_io_workflow_test` / `axiom_io_dataset_test` 中 STL 路径一致。 |

## 3. 与 `ImportOptions` 的配对（导入后闭环）

| `ImportOptions` | 与导出的关系 |
|-----------------|-------------|
| `run_validation` | 导入后 `validate_all(Standard)`；失败进入诊断与可选 `auto_repair`。未解决的验证/修复/再验证失败无 Body value，并回滚本次模型、缓存/统计、Eval 和 next_id；false 显式跳过闭环。 |
| `auto_repair` | 仅在验证失败且为真时触发修复管线；与导出策略独立。 |

## 4. 回归入口

- `axiom_io_workflow_test`：主链路 + 非法路径 + 批处理失败诊断；第 64 包四种网格格式与严格/兼容、侧车开关组合，失败诊断检索/JSON、缓存与模型不污染、设备写入失败、重试及重新导入已纳入第 68 批完整门禁。cycle-0073 又补齐主格式有限数值证据与模块 `audit_evidence`，修复后完整 CTest **16/16 通过、0 失败、126.44 s**（本测试 13.45 s），见 [门禁日志](../../.axiom-agent/logs/cycle-0073-gates.log)；本次文档同步未重跑测试。
- `axiom_io_dataset_test`：`tests/data/io` 最小 STEP/OBJ 数据集 + STL 策略组合烟测。

cycle-0085 调度器完整构建成功，CTest **16/16、0 失败、227.56 s**；必需 workflow/dataset/representation_io **14.77/0.70/12.23 s**。四格式固定 double/实际 STL 积分、64 MiB 预算及旧主文件/冷暖缓存失败保护见 [验收 §1.12](AxiomKernel_测试与验收方案.md#112-cycle-0085--s5-io-门禁与逐项证据)。publish 失败由实现静态核验，无独立 rename 失败注入；本轮未重跑测试，正式验收条件保留。

## 5. 刻意不覆盖（避免误解）

- 标准 **STEP/AP203/AP214 全实体**、**通用工业 3MF/OBJ** 读写不在本矩阵承诺范围内；当前为 **Axiom 子集 + 渐进鲁棒性**。

## 6. 网格导出失败合同（cycle-0085 更新，代码完整门禁通过）

四种网格格式以 `io.export.<format>.` 为前缀（`format` 为 `obj/stl/gltf/3mf`）：

| 阶段 | 根因与稳定错误码 |
|------|----------------|
| `input` | Body 不存在或路径为空，`InvalidInput / AXM-IO-E-0005`，保留传入 Body（包括 0） |
| `path` | 创建父目录或目录可写性检查失败，保留 `AXM-IO-E-0005/0009` |
| `convert` | 三角化失败，保留下层错误码（如无效 bbox 的 `AXM-VAL-E-0004`） |
| `mesh` | 无可用网格、非法顶点/索引或 glTF 坐标超出 float32 范围，`AXM-IO-E-0005` |
| `open` | 无法创建/打开同目录独占临时 payload 或目标为目录，`OperationFailed / AXM-IO-E-0005` |
| `write` | 临时 payload 写入、刷新或关闭失败（已存在特殊设备目标拒绝也保留 write 阶段），`OperationFailed / AXM-IO-E-0005` |
| `sidecar` | 临时 payload 成功关闭后，网格报告侧车写出失败，主文件尚未发布，保留下层 REP 错误码 |
| `publish` | rename 发布主文件失败，`OperationFailed / AXM-IO-E-0005`，含有限 filesystem_error |

严格 QA 仍使用 `AXM-IO-E-0006 / io.export.mesh_strict_qa`，所有失败关联输入 Body；不改变现有错误码含义或公开签名。cycle-0073 的 Error 及以上失败 issue 补齐有限 `status_code/related_entity_count` 与分支证据，非有限测量值过滤并计入 `non_finite_evidence_omitted`；审计使用 `issue_code_prefix="AXM-"` 和 `stage_prefix="io."`，覆盖复用下层根因码的失败。

失败保留输入模型、已有网格、实体 ID 及三角化缓存/统计；成功仍保留转换缓存。回滚范围仅为本次导出的三角化状态，诊断报告保留以供检索。输入、转换和网格校验失败不创建或截断主文件；临时 payload 写入/关闭失败不尝试写侧车。cycle-0085 起八格式采用同目录独占临时文件→检查关闭→请求侧车→rename 发布，失败清理临时 payload，已有主文件逐字节保留、无旧文件时不新建被宣称成功的主文件，可修复路径后重试。所有格式使用 classic locale/max_digits10；glTF float32 等格式固有限制保留。侧车沿用 REP 出口，自身可能已写出；侧车与主文件、批量文件无跨文件事务，不承诺掉电持久性或并发目录修改安全。

缓存回滚目前保存体/面缓存映射快照（不复制网格载荷），额外开销随缓存条目数增长；大模型缓存规模的性能预算仍需后续基线。
