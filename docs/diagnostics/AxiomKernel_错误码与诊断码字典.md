# AxiomKernel 错误码与诊断码字典

本文档定义 `AxiomKernel` 的错误码、警告码、诊断码、严重级别、编码规则和使用约定，目标是为研发、测试、上层应用和自动化系统提供统一的错误与诊断语言。

## 1. 文档目标

本文档用于明确以下内容：

- 错误码编码规则
- 诊断码编码规则
- 严重级别定义
- 模块前缀约定
- 标准错误码清单
- 标准诊断码清单
- 上层使用建议

## 2. 设计原则

错误与诊断体系必须满足以下原则：

- 可机器读取
- 可人类理解
- 可定位模块来源
- 可区分失败、警告和提示
- 可在日志、SDK、测试报告和 UI 中统一显示
- 同一问题在不同环境下保持稳定编码

## 3. 编码规则

## 3.1 错误码格式

统一格式：

`AXM-[模块]-E-[编号]`

示例：

- `AXM-GEO-E-0001`
- `AXM-BOOL-E-0102`
- `AXM-IO-E-0205`

含义：

- `AXM`：产品前缀
- `[模块]`：模块前缀
- `E`：Error，表示错误
- `[编号]`：四位数字编号

## 3.2 警告码格式

统一格式：

`AXM-[模块]-W-[编号]`

示例：

- `AXM-HEAL-W-0003`
- `AXM-BOOL-W-0107`

## 3.3 诊断码格式

统一格式：

`AXM-[模块]-D-[编号]`

示例：

- `AXM-TOPO-D-0004`
- `AXM-EVAL-D-0021`

## 3.4 信息码格式

统一格式：

`AXM-[模块]-I-[编号]`

示例：

- `AXM-IO-I-0001`
- `AXM-PLUGIN-I-0006`

## 4. 模块前缀定义

| 前缀 | 模块 |
|---|---|
| `CORE` | 内核公共层 |
| `MATH` | 数学与谓词层 |
| `GEO` | 几何层 |
| `TOPO` | 拓扑层 |
| `REP` | 表示层 |
| `OPS` | 通用建模操作层 |
| `BOOL` | 布尔模块 |
| `BLEND` | 圆角倒角模块 |
| `MOD` | 修改模块 |
| `QUERY` | 查询分析模块 |
| `HEAL` | 修复模块 |
| `VAL` | 验证模块 |
| `IO` | 数据交换模块 |
| `TES` | 三角化模块 |
| `EVAL` | 增量与缓存模块 |
| `PLUGIN` | 插件模块 |
| `TX` | 事务与版本模块 |

## 5. 严重级别定义

## 5.1 `Info`

含义：

- 非错误
- 仅表示流程状态、回退信息或补充说明

适用场景：

- 使用了兼容模式导出
- 自动采用了数值回退
- 走了近似三角化路径

## 5.2 `Warning`

含义：

- 操作仍然成功
- 但结果存在风险、退化、近似或需关注项

适用场景：

- 修复时放宽了局部容差
- 某些小特征被删除
- 模型通过验证但存在薄壁风险

## 5.3 `Error`

含义：

- 当前操作失败
- 输入、几何状态或算法结果不满足继续执行条件

适用场景：

- 非法拓扑
- 曲面求交失败
- 分类不确定且无法回退求解

## 5.4 `Fatal`

含义：

- 严重内部错误或不可恢复故障
- 当前流程必须终止

适用场景：

- 内部状态损坏
- 核心不变量被破坏
- 关键资源不可用

## 6. 使用约定

### 6.1 返回原则

- 面向 SDK 的每个失败结果必须携带至少一个错误码。
- 所有重量级操作建议同时附带诊断 ID。
- 一个操作允许同时返回多个 `Warning`，但至少应有一个主错误码。

### 6.2 稳定性原则

- 同一种根因应尽量复用同一个错误码。
- 不要用文本变化代替编码变化。
- 不能把业务逻辑差异塞进错误消息而不定义编码。

### 6.3 文案原则

- 错误消息要描述问题，不要描述程序情绪。
- 文案应包含对象类型和失败原因。
- 文案避免模糊词，如“失败了”“有点问题”。

## 7. 标准错误码清单

## 7.1 `CORE` 公共错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-CORE-E-0001` | Error | 输入对象为空或句柄无效 |
| `AXM-CORE-E-0002` | Error | 参数越界 |
| `AXM-CORE-E-0003` | Error | 当前对象不存在 |
| `AXM-CORE-E-0004` | Error | 不支持的操作模式 |
| `AXM-CORE-E-0005` | Fatal | 内部状态损坏 |
| `AXM-CORE-E-0006` | Error | 必要依赖模块不可用 |
| `AXM-CORE-E-0007` | Error | 类型不匹配 |
| `AXM-CORE-E-0008` | Error | 版本不兼容 |

推荐文案示例：

- `输入句柄无效或对象已被释放`
- `参数超出允许范围`
- `请求的对象不存在于当前版本中`

## 7.2 `MATH` 数学与谓词错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-MATH-E-0001` | Error | 数值溢出 |
| `AXM-MATH-E-0002` | Error | 数值下溢或精度丢失严重 |
| `AXM-MATH-E-0003` | Error | 矩阵不可逆 |
| `AXM-MATH-E-0004` | Warning | 谓词结果不确定，已触发回退 |
| `AXM-MATH-E-0005` | Error | 回退后仍无法确定符号 |
| `AXM-MATH-E-0006` | Error | 非法零向量归一化 |

## 7.3 `GEO` 几何层错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-GEO-E-0001` | Error | 曲线创建参数非法 |
| `AXM-GEO-E-0002` | Error | 曲面创建参数非法 |
| `AXM-GEO-E-0003` | Error | 几何对象退化 |
| `AXM-GEO-E-0004` | Error | 参数超出定义域 |
| `AXM-GEO-E-0005` | Error | 最近点求解失败 |
| `AXM-GEO-E-0006` | Error | 参数反求失败 |
| `AXM-GEO-E-0007` | Warning | 曲率结果数值不稳定 |
| `AXM-GEO-E-0008` | Error | 修剪域非法 |
| `AXM-GEO-E-0009` | Error | 求值器不可用 |

## 7.4 `TOPO` 拓扑层错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-TOPO-E-0001` | Error | 边缺少合法端点 |
| `AXM-TOPO-E-0002` | Error | 环未闭合 |
| `AXM-TOPO-E-0003` | Error | 面外环非法 |
| `AXM-TOPO-E-0004` | Error | 面内环非法 |
| `AXM-TOPO-E-0005` | Error | 壳未封闭 |
| `AXM-TOPO-E-0006` | Error | 非法悬挂边 |
| `AXM-TOPO-E-0007` | Error | 拓扑关系不一致 |
| `AXM-TOPO-E-0008` | Error | 参数曲线与空间曲线不一致 |
| `AXM-TOPO-E-0009` | Fatal | 拓扑不变量被破坏 |
| `AXM-TOPO-E-0010` | Error | 壳内存在开放边界（边引用次数不足） |
| `AXM-TOPO-E-0011` | Error | 壳内存在非流形边（边被过多拓扑面共享） |
| `AXM-TOPO-E-0012` | Error | 派生/传播来源引用无效或丢失 |
| `AXM-TOPO-E-0013` | Error | 面/壳/体的来源集合不一致 |
| `AXM-TOPO-E-0014` | Error | 同一环内重复引用同一条拓扑边 |
| `AXM-TOPO-E-0015` | Error | 修剪面外环与内环在 UV 空间方向不符合孔洞规则 |
| `AXM-TOPO-E-0016` | Error | 定向边已归属其他环（共边跨环复用） |
| `AXM-TOPO-E-0017` | Warning | 壳内存在重复面（相同曲面与边界环签名） |
| `AXM-TOPO-E-0018` | Warning | 壳不连通（面集合存在多个连通分量） |

## 7.5 `BOOL` 布尔模块错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-BOOL-E-0001` | Error | 输入实体无效 |
| `AXM-BOOL-E-0002` | Error | 候选相交对生成失败 |
| `AXM-BOOL-E-0003` | Error | 曲面求交失败 |
| `AXM-BOOL-E-0004` | Error | 交线切分失败 |
| `AXM-BOOL-E-0005` | Error | 区域分类失败 |
| `AXM-BOOL-E-0006` | Error | 拓扑重建失败 |
| `AXM-BOOL-E-0007` | Warning | 检测到近共面退化情形 |
| `AXM-BOOL-E-0008` | Warning | 检测到近切触退化情形 |
| `AXM-BOOL-E-0009` | Error | 自动修复后仍不合法 |
| `AXM-BOOL-E-0010` | Error | 运算结果为空且不符合预期 |

推荐文案示例：

- `布尔求交阶段失败：无法稳定生成相交曲线`
- `布尔分类阶段失败：局部区域 inside/outside 不确定`
- `布尔重建阶段失败：输出壳体未封闭`

## 7.6 `BLEND` 圆角倒角错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-BLEND-E-0001` | Error | 目标边不存在 |
| `AXM-BLEND-E-0002` | Error | 半径或倒角距离非法 |
| `AXM-BLEND-E-0003` | Error | 邻面不支持当前圆角求解 |
| `AXM-BLEND-E-0004` | Error | 角区求解失败 |
| `AXM-BLEND-E-0005` | Warning | 局部圆角结果存在近自交风险 |
| `AXM-BLEND-E-0006` | Error | 圆角修剪失败 |
| `AXM-BLEND-W-0001` | Warning | 圆角/倒角为拓扑占位与参数门禁：工业级滚球、角区、变半径等未实现 |

## 7.7 `MOD` 修改模块错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-MOD-E-0001` | Error | 偏置距离非法 |
| `AXM-MOD-E-0002` | Error | 偏置后发生自交 |
| `AXM-MOD-E-0003` | Error | 抽壳失败 |
| `AXM-MOD-E-0004` | Error | 拔模方向非法 |
| `AXM-MOD-E-0005` | Error | 替换面与目标不兼容 |
| `AXM-MOD-E-0006` | Error | 删除面补面失败 |
| `AXM-MOD-E-0007` | Warning | 修改导致小特征被移除 |

## 7.8 `QUERY` 查询分析错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-QUERY-E-0001` | Error | 最近点查询失败 |
| `AXM-QUERY-E-0002` | Error | 截面计算失败 |
| `AXM-QUERY-E-0003` | Error | 质量属性计算失败 |
| `AXM-QUERY-E-0004` | Error | 距离计算失败 |
| `AXM-QUERY-E-0005` | Warning | 质量属性基于近似网格计算 |

## 7.9 `HEAL` 修复模块错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-HEAL-E-0001` | Error | 缝合失败 |
| `AXM-HEAL-E-0002` | Error | 小边清理失败 |
| `AXM-HEAL-E-0003` | Error | 小面清理失败 |
| `AXM-HEAL-E-0004` | Warning | 自动修复放宽了局部容差 |
| `AXM-HEAL-E-0005` | Warning | 自动修复删除了局部特征 |
| `AXM-HEAL-E-0006` | Error | 自动修复未能生成合法模型 |

## 7.10 `VAL` 验证模块错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-VAL-E-0001` | Error | 检测到自交 |
| `AXM-VAL-E-0002` | Error | 检测到非法非流形 |
| `AXM-VAL-E-0003` | Error | 检测到容差冲突 |
| `AXM-VAL-E-0004` | Error | 检测到退化几何 |
| `AXM-VAL-E-0005` | Warning | 检测到薄壁高风险区域 |
| `AXM-VAL-E-0006` | Warning | 检测到高曲率不稳定区域 |

## 7.11 `IO` 数据交换模块错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-IO-E-0001` | Error | 文件不存在 |
| `AXM-IO-E-0002` | Error | 文件格式无法识别 |
| `AXM-IO-E-0003` | Error | 文件内容损坏 |
| `AXM-IO-E-0004` | Error | 导入解析失败 |
| `AXM-IO-E-0005` | Error | 导出失败 |
| `AXM-IO-E-0006` | Warning | 导入后发生几何近似 |
| `AXM-IO-E-0007` | Warning | 导入后存在未映射属性 |
| `AXM-IO-E-0008` | Warning | 导出采用兼容模式降级 |

## 7.12 `TES` 三角化错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-TES-E-0001` | Error | 三角化失败 |
| `AXM-TES-E-0002` | Error | 法向计算失败 |
| `AXM-TES-E-0003` | Warning | 三角化结果未满足目标误差 |
| `AXM-TES-E-0004` | Warning | 使用近似曲面片替代精确曲面片 |

## 7.13 `EVAL` 缓存与增量模块错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-EVAL-E-0001` | Error | 依赖图存在环 |
| `AXM-EVAL-E-0002` | Error | 缓存版本不匹配 |
| `AXM-EVAL-E-0003` | Warning | 缓存失效已触发重算 |
| `AXM-EVAL-E-0004` | Error | 局部失效传播失败 |

## 7.14 `PLUGIN` 插件模块错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-PLUGIN-E-0001` | Error | 插件加载失败 |
| `AXM-PLUGIN-E-0002` | Error | 插件版本不兼容 |
| `AXM-PLUGIN-E-0003` | Error | 插件能力声明不完整 |
| `AXM-PLUGIN-E-0004` | Error | 插件执行失败 |
| `AXM-PLUGIN-E-0005` | Warning | 插件返回结果未通过验证 |

## 7.15 `TX` 事务与版本模块错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-TX-E-0001` | Error | 提交失败 |
| `AXM-TX-E-0002` | Error | 回滚失败 |
| `AXM-TX-E-0003` | Error | 写事务冲突 |
| `AXM-TX-E-0004` | Error | 目标版本不存在 |
| `AXM-TX-E-0005` | Fatal | 版本图损坏 |

## 8. 标准警告码清单

以下警告码建议作为常用跨模块警告：

| 警告码 | 含义 |
|---|---|
| `AXM-CORE-W-0001` | 当前操作采用默认容差 |
| `AXM-MATH-W-0001` | 谓词进入高精度回退 |
| `AXM-GEO-W-0001` | 几何求值结果接近退化区域 |
| `AXM-BOOL-W-0001` | 布尔操作遇到近共面情形 |
| `AXM-HEAL-W-0001` | 修复时删除了局部小特征 |
| `AXM-IO-W-0001` | 导入后部分属性未映射 |
| `AXM-TES-W-0001` | 三角化误差达到上限边缘 |

## 9. 标准诊断码清单

诊断码用于比错误码更细粒度地描述流程状态、局部问题和证据类别。

## 9.1 `BOOL` 诊断码

| 诊断码 | 含义 |
|---|---|
| `AXM-BOOL-D-0001` | 布尔候选构建阶段开始（`kBoolStageCandidates`） |
| `AXM-BOOL-D-0002` | 布尔预处理候选片段统计已生成（`kBoolPrepCandidatesBuilt`） |
| `AXM-BOOL-D-0003` | 布尔局部裁剪已应用于结果 bbox（`kBoolLocalClipApplied`） |
| `AXM-BOOL-D-0004` | 布尔单次运行阶段与输入摘要（bbox 关系、预处理统计等，`kBoolRunStageSummary`） |
| `AXM-BOOL-D-0005` | 布尔输出占位物化完成（`kBoolStageOutputMaterialized`） |
| `AXM-BOOL-D-0006` | 布尔面级候选对已生成（`kBoolFaceCandidatesBuilt`） |
| `AXM-BOOL-D-0007` | 布尔精确求交已生成交线/交曲线（解析入口，`kBoolIntersectionCurvesBuilt`） |
| `AXM-BOOL-D-0008` | 布尔交线裁剪为面域内线段完成（`kBoolIntersectionSegmentsBuilt`） |
| `AXM-BOOL-D-0009` | 布尔交线集合已保存（Intersection wires，`kBoolIntersectionWiresStored`） |
| `AXM-BOOL-D-0010` | 布尔切分/imprint 已应用（占位：沿对角线切分矩形面，`kBoolImprintApplied`） |
| `AXM-BOOL-D-0011` | 布尔切分/imprint 已应用（按交线段切分矩形面，`kBoolImprintSegmentApplied`） |
| `AXM-BOOL-D-0012` | 布尔分类阶段完成（占位：分类统计，`kBoolClassificationCompleted`） |
| `AXM-BOOL-D-0013` | 布尔重建阶段完成（占位：Strict 校验摘要，`kBoolRebuildCompleted`） |
| `AXM-BOOL-D-0014` | 布尔切分阶段开始（imprint/split/trim 入口，`kBoolStageSplit`） |
| `AXM-BOOL-D-0015` | 布尔分类阶段开始（cell/face classification 入口，`kBoolStageClassify`） |
| `AXM-BOOL-D-0016` | 布尔重建阶段开始（shell rebuild/stitch/merge 入口，`kBoolStageRebuild`） |
| `AXM-BOOL-D-0017` | 布尔验证阶段开始（Strict/Standard validation 入口，`kBoolStageValidate`） |
| `AXM-BOOL-D-0018` | 布尔修复阶段开始（auto_repair/heal 入口，`kBoolStageRepair`） |

## 9.2 `HEAL` 诊断码

| 诊断码 | 含义 |
|---|---|
| `AXM-HEAL-D-0001` | 检测到小边 |
| `AXM-HEAL-D-0002` | 检测到小面 |
| `AXM-HEAL-D-0003` | 缝合操作已执行 |
| `AXM-HEAL-D-0004` | 局部容差已放宽 |
| `AXM-HEAL-D-0005` | 修复后验证通过 |

## 9.3 `VAL` 诊断码

| 诊断码 | 含义 |
|---|---|
| `AXM-VAL-D-0001` | 几何验证开始 |
| `AXM-VAL-D-0002` | 拓扑验证开始 |
| `AXM-VAL-D-0003` | 自交检查开始 |
| `AXM-VAL-D-0004` | 检测到薄壁区域 |
| `AXM-VAL-D-0005` | 全量验证通过 |

## 9.4 `IO` 诊断码

| 诊断码 | 含义 |
|---|---|
| `AXM-IO-D-0001` | 文件已打开 |
| `AXM-IO-D-0002` | 解析实体中 |
| `AXM-IO-D-0003` | 发现未映射属性 |
| `AXM-IO-D-0004` | 导入后触发自动验证 |
| `AXM-IO-D-0005` | 导出采用兼容模式 |

## 10. 诊断报告结构建议

建议统一输出格式如下：

```json
{
  "diagnosticId": "diag-20260326-0001",
  "operation": "boolean_subtract",
  "status": "Error",
  "primaryCode": "AXM-BOOL-E-0005",
  "warnings": [
    "AXM-MATH-W-0001",
    "AXM-BOOL-W-0001"
  ],
  "trace": [
    "AXM-BOOL-D-0002",
    "AXM-BOOL-D-0003",
    "AXM-BOOL-D-0004"
  ],
  "relatedEntities": [
    "Body:1001",
    "Face:2003",
    "Face:2011"
  ],
  "message": "布尔分类阶段失败：局部区域 inside/outside 不确定"
}
```

## 11. 上层应用展示建议

### 11.1 面向开发者

建议展示：

- 主错误码
- 诊断轨迹
- 关联实体 ID
- 失败阶段
- 是否触发高精度回退

### 11.2 面向终端用户

建议展示：

- 简化后的用户友好文案
- 失败对象定位
- 是否可尝试自动修复
- 推荐下一步操作

例如：

- `布尔失败：两个实体在局部接触区域无法稳定分类，建议降低局部复杂度或启用自动修复模式`

## 12. 测试与验收要求

错误码和诊断体系本身也必须测试。

### 12.1 必测项

- 同一种错误是否稳定返回相同编码
- 是否能从错误码定位到模块
- 是否能从诊断码恢复主要流程轨迹
- 是否存在重复定义或语义冲突

### 12.2 验收标准

- `P0` 核心模块必须定义完整错误码
- 布尔、修复、导入导出必须具备诊断轨迹
- 所有文档化错误码都要有至少一个测试用例覆盖

## 13. 版本维护规则

- 新增错误码时不得重用旧编号表示新语义
- 废弃错误码应保留历史说明
- 主版本内不应随意修改已发布错误码含义
- 每个版本应附带错误码差异清单

## 14. 结论

错误码和诊断码不是“日志附属品”，而是 `AxiomKernel` 的一部分公共接口。只要这套编码体系稳定：

- 研发能更快定位问题
- 测试能更精确做回归
- 上层 CAD 能更好展示失败原因
- 自动化系统能更可靠地分类统计

后续如果继续细化，建议再补一份：

`docs/diagnostics/AxiomKernel_用户可读错误文案映射表.md`
