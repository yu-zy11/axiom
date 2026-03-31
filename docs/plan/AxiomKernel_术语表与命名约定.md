# AxiomKernel 术语表与命名约定

本文档定义 `AxiomKernel` 项目中的核心术语、统一名词、英文对照、命名边界和代码命名约定，目标是减少团队在设计、研发、测试、文档和产品沟通中的歧义。

## 1. 文档目标

本文档用于统一：

- 核心几何与拓扑术语
- 中英文对照
- 产品文档和技术文档中的推荐表述
- 代码层命名规则
- 禁止混用的概念

## 2. 使用原则

- 一种概念尽量只保留一个主名词
- 文档和代码尽量保持同一语义
- 英文术语尽量采用行业通用叫法
- 不把“几何对象”和“拓扑对象”混称

## 3. 核心术语表

| 中文术语 | 英文术语 | 推荐缩写 | 说明 |
|---|---|---|---|
| 几何引擎 | Geometric Modeling Kernel | Kernel | 本项目的核心产品 |
| 精确边界表示 | Exact Boundary Representation | ExactBRep | 精确实体表示方式 |
| 网格表示 | Mesh Representation | MeshRep | 三角网格或多边形网格表示 |
| 隐式表示 | Implicit Representation | ImplicitRep | 基于距离场或函数的表示 |
| 混合表示 | Hybrid Representation | HybridRep | 多种表示共存 |
| 顶点 | Vertex | Vertex | 拓扑点 |
| 边 | Edge | Edge | 拓扑边 |
| 定向边 | Coedge | Coedge | 面内定向边 |
| 环 | Loop | Loop | 面的边界环 |
| 面 | Face | Face | 拓扑面 |
| 壳 | Shell | Shell | 面集合 |
| 体 | Body | Body | 实体对象 |
| 曲线 | Curve | Curve | 几何曲线 |
| 曲面 | Surface | Surface | 几何曲面 |
| 参数域曲线 | Parameter Curve | PCurve | 面参数域上的边界曲线 |
| 求值器 | Evaluator | Evaluator | 求点、法向、曲率的接口 |
| 布尔运算 | Boolean Operation | Boolean | 并、差、交、分割 |
| 倒角 | Chamfer | Chamfer | 倒角操作 |
| 圆角 | Fillet | Fillet | 圆角操作 |
| 抽壳 | Shelling | Shell | 修改类操作，不与拓扑 Shell 混淆 |
| 偏置 | Offset | Offset | 几何偏置或实体偏置 |
| 缝合 | Sewing | Sew | 修复面片边界 |
| 验证 | Validation | Validate | 检查模型合法性 |
| 修复 | Healing | Heal | 修正模型问题 |
| 三角化 | Tessellation | Tess | 精确模型转网格 |
| 事务 | Transaction | Tx | 写操作提交单元 |
| 版本 | Version | Ver | 模型版本状态 |
| 诊断 | Diagnostics | Diag | 问题、警告、证据集合 |

## 4. 容易混淆的术语

## 4.1 `Shell`

有两种语义：

- 拓扑层中的 `Shell`：壳体，面集合
- 修改操作中的 `Shelling`：抽壳

约定：

- 在代码层，拓扑对象使用 `Shell`
- 修改操作使用 `ShellBody` 或 `Shelling`

## 4.2 `Face` 与 `Surface`

区别：

- `Surface` 是几何曲面
- `Face` 是引用一个曲面并附带边界信息的拓扑面

约定：

- 不允许把 `Face` 直接叫“曲面”
- 不允许把 `Surface` 直接叫“面”

## 4.3 `Edge` 与 `Curve`

区别：

- `Curve` 是几何曲线
- `Edge` 是带端点和拓扑关系的边

约定：

- 文档中写“边界几何”时，可表述为 `Edge + Curve`

## 4.4 `Body` 与 `Part`

区别：

- `Body` 是内核级实体对象
- `Part` 更像上层产品语义

约定：

- 内核文档统一使用 `Body`
- 上层应用若需 `Part` 语义，应自行封装

## 5. 文档命名约定

### 5.1 文档标题

建议统一格式：

`AxiomKernel_文档主题.md`

例如：

- `docs/api/AxiomKernel_详细模块接口清单.md`
- `docs/quality/AxiomKernel_测试与验收方案.md`

### 5.2 章节层级

建议规则：

- 一级章节：业务和结构主题
- 二级章节：模块或子域
- 三级章节：具体条目和规则

### 5.3 文案语气

建议：

- 用“应”“必须”“建议”区分强弱约束
- 避免“可能”“大概”“差不多”这类模糊语气

## 6. 代码命名约定

## 6.1 类型命名

规则：

- 类、结构体、枚举使用 `PascalCase`

示例：

- `BodyId`
- `TolerancePolicy`
- `BooleanService`

## 6.2 函数命名

规则：

- 普通函数和成员函数使用 `snake_case` 或统一小写风格
- 需全项目统一，建议 API 层使用 `snake_case`

示例：

- `make_plane`
- `validate_all`
- `mass_properties`

## 6.3 变量命名

规则：

- 局部变量使用小写加下划线
- 避免无语义的单字母命名

示例：

- `input_body`
- `candidate_faces`
- `repair_mode`

## 6.4 常量命名

规则：

- 编译期常量可使用 `kPascalCase`

示例：

- `kDefaultLinearTolerance`
- `kMaxFallbackIterations`

## 6.5 枚举命名

规则：

- 枚举类型使用 `PascalCase`
- 枚举项使用 `PascalCase`

示例：

```cpp
enum class ValidationMode {
  Fast,
  Standard,
  Strict
};
```

## 7. 模块命名约定

建议模块名尽量简洁稳定：

- `math`
- `geo`
- `topo`
- `rep`
- `ops`
- `heal`
- `eval`
- `diag`
- `io`
- `plugin`
- `sdk`

不建议：

- `geometry_super_core`
- `all_ops_manager`
- `misc_utils`

## 8. 文件命名约定

### 8.1 源文件

建议：

- 文件名与核心类型或模块能力一致
- 一个文件尽量聚焦一个主类型或一个小主题

示例：

- `curve_service.h`
- `surface_factory.cpp`
- `boolean_service.h`
- `topology_transaction.cpp`

### 8.2 测试文件

建议：

- 使用被测对象加 `_test`

示例：

- `curve_service_test.cpp`
- `boolean_service_test.cpp`

## 9. 错误码与诊断命名约定

### 9.1 错误码

统一格式：

- `AXM-[模块]-E-[编号]`

### 9.2 警告码

统一格式：

- `AXM-[模块]-W-[编号]`

### 9.3 诊断码

统一格式：

- `AXM-[模块]-D-[编号]`

## 10. API 命名风格建议

建议 API 表达遵循“动词 + 对象”：

- `make_plane`
- `create_face`
- `replace_surface`
- `remove_small_edges`
- `validate_topology`

不建议：

- `plane_make`
- `do_validation_for_topology`
- `execute_the_boolean_operation`

## 11. 结果对象命名约定

建议统一：

- 通用返回：`Result<T>`
- 操作结果：`OpResult`
- 查询结果：`QueryResult`
- 三角化结果：`MeshResult`
- 诊断结果：`DiagnosticReport`

## 12. 布尔与修改术语建议

推荐使用：

- `Union`
- `Subtract`
- `Intersect`
- `Split`
- `Offset`
- `ShellBody`
- `DraftFaces`
- `ReplaceFace`
- `DeleteFaceAndHeal`

不建议：

- `Fuse`
- `CutOut`
- `OverlapDo`

除非后续需要兼容特定外部术语。

## 13. 用户文档与研发文档命名差异

### 13.1 用户文档

建议语言：

- 更关注操作结果
- 少用底层术语
- 多用任务型标题

例如：

- `为什么布尔运算失败`
- `如何修复导入模型`

### 13.2 研发文档

建议语言：

- 更关注模块边界和实现约束
- 精确区分几何、拓扑、表示和操作

## 14. 禁止混用词汇清单

以下表达不建议在项目内混用：

- 把 `Surface` 称为 `Face`
- 把 `Curve` 称为 `Edge`
- 把 `Body` 称为 `Model`
- 把 `Shelling` 称为 `Shell`
- 把 `Validation` 和 `Healing` 混为一谈
- 把 `MeshRep` 当作 `ExactBRep`

## 15. 新术语引入流程建议

当项目中出现新的术语时，建议遵循以下流程：

1. 明确中文定义
2. 明确英文对照
3. 明确属于几何、拓扑、表示还是操作层
4. 决定代码命名
5. 补充到本术语表

## 16. 结论

术语一致性看起来像“小事”，但对几何引擎项目特别重要。因为这个领域最容易出的问题不是“没人写代码”，而是“大家说的是同一个词，脑子里却不是同一个东西”。

后续建议继续补充：

- `AxiomKernel_名词翻译表_中英对照扩展版.md`
- `AxiomKernel_API命名冻结清单.md`
