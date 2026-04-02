# AxiomKernel IO：`ExportOptions` 策略矩阵（工程口径）

本文档固化 **网格类导出**（STL / glTF / OBJ / 3MF）与 **体元数据导出**（STEP / AXMJSON / IGES / BREP）的主要开关组合，供 MR 评审与 CI 回归对齐；**不等于**工业交付矩阵（全格式误差预算、全 STEP 实体仍见进度文档）。

## 1. `ExportOptions` 字段

| 字段 | 默认 | 语义 |
|------|------|------|
| `compatibility_mode` | `false` | `false`：网格导出前 `inspect_mesh` 严格门控，越界索引/退化三角形 → `AXM-IO-E-0006`；`true`：跳过门控，尽力写出。 |
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
| `run_validation` | 导入后 `validate_all`；失败进入诊断与可选 `auto_repair`。 |
| `auto_repair` | 仅在验证失败且为真时触发修复管线；与导出策略独立。 |

## 4. 回归入口

- `axiom_io_workflow_test`：主链路 + 非法路径 + 批处理失败诊断等。
- `axiom_io_dataset_test`：`tests/data/io` 最小 STEP/OBJ 数据集 + STL 策略组合烟测。

## 5. 刻意不覆盖（避免误解）

- 标准 **STEP/AP203/AP214 全实体**、**通用工业 3MF/OBJ** 读写不在本矩阵承诺范围内；当前为 **Axiom 子集 + 渐进鲁棒性**。
