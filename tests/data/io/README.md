# IO 回归小数据集（`tests/data/io`）

用于 `axiom_io_dataset_test` 与文档中的「最小可解析」示例，**不是**工业级全量 STEP/IGES 实体库。

| 文件 | 用途 |
|------|------|
| `minimal_step_subset.step` | Axiom STEP 子集 HEADER：含 `AXIOM_STEP_SCHEMA` / `AXIOM_STEP_ENTITY` 与几何元数据 |
| `triangle_vn_vt.obj` | Wavefront OBJ：含 `vn`/`vt` 与 `f v/vt/vn`，验证非网格行跳过与面解析 |
| `standard_step_express_stub.step` | 最小 ISO-10303-21 + DATA 段 `#n=ENTITY(`，**无** Axiom 子集标记；导入应 `NotImplemented` + `AXM-IO-E-0010`，且含 Info **`AXM-IO-D-0016`** 物理层类型扫描摘要 |
| `standard_iges_deck_stub.iges` | 两行 80 列典型 DE 尾缀形态，**无** Axiom 元数据；导入应 `NotImplemented` + `AXM-IO-E-0011`，且含 Info **`AXM-IO-D-0017`** DE 扫描摘要 |

扩展数据集时请保持文件体积极小，并在 `tests/io/io_dataset_test.cpp` 增加对应断言。
