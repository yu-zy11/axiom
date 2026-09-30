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
| `AXM-CORE-E-0002` | Error | 参数越界（含 `TopologyTransaction::create_vertex` 拒绝任一坐标为 NaN/±Inf；返回 `InvalidInput`，不写入拓扑或事务计数；带孔拉伸/扫掠/旋转及拓扑兼容放样的越界、相交/接触/嵌套、非共面/退化环及数值物化失败同样返回该码，失败不分配模型 ID；带孔轮廓触轴/形成尖顶、放样环拓扑或顶点数不匹配仍拒绝；第 65/66 包 `extrude_scaled` 缺失显式轮廓、负/非有限比例、带孔尖顶（无孔轮廓允许零比例尖顶）、离面或非有限中心、无效距离/方向、缩放截面数值退化沿用该码；第 71 批 `revolve_between` 非法有向区间和 `sweep_scaled` 非有限/非正比例、非单位比例周期导轨也沿用该码） |
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

第 73 批 `SweepService::extrude_twisted` 同样复用 `AXM-CORE-E-0002 / InvalidInput`：缺失显式轮廓/标签、非有限参数、非正距离、无效或非垂直方向、中心离开轮廓平面、扭角超出正负一周，以及轮廓/孔洞/采样侧壁数值退化均在模型分配前拒绝。正负部分角、正负整周和满足法向合同的零扭角均支持；该码不表示解析螺旋面已实现。

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
| `AXM-GEO-E-0001` | Error | 曲线创建参数非法（含椭圆轴长或派生法向长度溢出，以及显式 BSpline/NURBS 结点逆序、零长度有效参数域、结点重数超过 degree + 1） |
| `AXM-GEO-E-0002` | Error | 曲面创建参数非法（包括 BSpline/NURBS 任一轴结点非有限、逆序、零有效域或重数超过 `degree + 1`） |
| `AXM-GEO-E-0003` | Error | 几何对象退化 |
| `AXM-GEO-E-0004` | Error | 参数超出定义域 |
| `AXM-GEO-E-0005` | Error | 最近点求解失败 |
| `AXM-GEO-E-0006` | Error | 参数反求失败 |
| `AXM-GEO-E-0007` | Warning | 曲率结果数值不稳定 |
| `AXM-GEO-E-0008` | Error | 修剪域非法 |
| `AXM-GEO-E-0009` | Error | 求值器不可用 |
| `AXM-GEO-E-0010` | Error | 偏置曲面有效半径非正（球/圆柱等解析基面下偏置自交或退化壳） |
| `AXM-GEO-E-0011` | Error | 数值曲线长度积分失败（求值预算耗尽、精度停滞或速度不可用） |

## 7.4 `TOPO` 拓扑层错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-TOPO-E-0001` | Error | 边缺少合法端点 |
| `AXM-TOPO-E-0002` | Error | 环未闭合（含单共边的定向首尾顶点 ID 不同；坐标重合不等于拓扑闭合） |
| `AXM-TOPO-E-0003` | Error | 面外环非法（含：外环已被其他面引用，或已闭合外环不足三条共边且不满足同曲线双弧例外时 `create_face` 拒绝） |
| `AXM-TOPO-E-0004` | Error | 面内环非法（含：内环已被其他面引用，或已闭合内环不足三条共边且不满足同曲线双弧例外时 `create_face` 拒绝） |
| `AXM-TOPO-E-0005` | Error | 壳未封闭或成员面拓扑受损（含：`create_shell` 拒绝不存在的面曲面引用、无效外环/内环；曲面失败关联面与曲面 ID，内环失败关联面与环 ID；`create_body` 拒绝壳成员面不存在的曲面引用并关联壳、面与曲面 ID；`validate_indices_consistency` 发现体记录中 `shells` 列表含重复壳 id） |
| `AXM-TOPO-E-0006` | Error | 非法悬挂边（含：`validate_edge` 与 `validate_indices_consistency` 反向索引发现边无共边引用） |
| `AXM-TOPO-E-0007` | Error | 拓扑关系不一致（含：`edge_to_coedges` 重复定向边、`face_to_shells`/`shell_to_bodies` 反向列表重复条目、`loop_to_faces` 重复面或同一环对应多面等索引自洽性失败） |
| `AXM-TOPO-E-0008` | Error | 参数曲线与空间曲线不一致（含 `validate_edge` 检测拓扑端点不在引用 3D Curve 上，关联 `edge/curve/vertex`；`validate_face_trim_consistency`：PCurve 绑定不完整、PCurve 控制点不足、边/曲线/顶点缺失时带 `face/loop/coedge/edge/pcurve` 等；**全量 trim 数据**下 `SurfaceService::closest_uv` 失败亦归此类；**全量 trim** 下 PCurve 定义域非法导致无法完成内点采样一致性校验；端点/曲面与 3D 边不一致时常含 `face/loop/coedge/edge/pcurve`；`validate_indices_consistency` 发现边记录引用不存在顶点时 `related_entities` 含 `edge` 与端点 id） |
| `AXM-TOPO-E-0009` | Fatal | 拓扑不变量被破坏 |
| `AXM-TOPO-E-0010` | Error | 壳内存在开放边界（边引用次数不足） |
| `AXM-TOPO-E-0011` | Error | 壳内存在非流形边（边被过多拓扑面共享） |
| `AXM-TOPO-E-0012` | Error | 派生/传播来源引用无效或丢失 |
| `AXM-TOPO-E-0013` | Error | 面/壳/体的来源集合不一致 |
| `AXM-TOPO-E-0014` | Error | 同一环内重复引用同一条拓扑边，或同一面跨环复用拓扑边；`create_face` 在写入前拒绝，关联两个冲突环与边 ID，`validate_face` 保留验证门禁 |
| `AXM-TOPO-E-0015` | Error | 面环方向与外向规则不一致（含：内外环在 UV 空间绕向不符合孔洞规则；**全量 PCurve** 且基曲面为**平面**时，外环 UV 映射到 3D 的 Newell 与基平面法向不一致；无 PCurve 时平面/球/柱/锥/环面外环与解析外向一致性；`validate_shell_closedness` 发现共享边两侧 coedge 方向相同） |
| `AXM-TOPO-E-0016` | Error | 定向边已归属其他环（共边跨环复用） |
| `AXM-TOPO-E-0017` | Warning / Error | 壳内重复面：`validate_shell` 对同曲面同边界环签名给 **Warning**；`create_shell` / `validate_indices_consistency` 对壳 `faces` 列表中重复 `FaceId` 给 **Error** |
| `AXM-TOPO-E-0018` | Warning / Error | 壳不连通（`validate_shell` 作为结构告警；`validate_shell_closedness` 将多个面连通分量作为 Strict 闭合性错误） |
| `AXM-TOPO-E-0019` | Error | 定向边未被任何环引用（悬挂定向边） |
| `AXM-TOPO-E-0020` | Error | 顶点未作为任何边的端点（悬挂顶点；`validate_vertex` 与 `validate_indices_consistency`） |
| `AXM-TOPO-E-0021` | Error | 环未被任何面引用（孤立环，例如删除面后残留） |
| `AXM-TOPO-E-0022` | Error | 面未被任何壳引用（孤立面；`face_to_shells` 无条目或为空；`validate_indices_consistency`） |
| `AXM-TOPO-E-0023` | Error | 环在闭合终点之外重复经过同一顶点，形成自接触的非简单边界；`create_loop` 在写入前拒绝并关联重复顶点与两条冲突定向边 |
| `AXM-TOPO-E-0024` | Error | 同一面中不同边界环共用同一拓扑顶点；`create_face` 在写入前拒绝，`validate_face` 检出存量缺陷，关联冲突环与顶点 ID |
| `AXM-TOPO-E-0025` | Error | 同一面中不同边界环的独立顶点具有完全相同的有限三维坐标；`create_face` 在写入前拒绝，`validate_face` 检出存量缺陷，关联两个环与两个顶点 ID。仅覆盖顶点坐标精确重合，不代表完整几何自交检测 |
| `AXM-TOPO-E-0026` | Error | 同一面中不同边界环的有限线性边界片段在三维空间内部相交；精确覆盖 Line/LineSegment 与显式裁剪的 CompositePolyline/线性 CompositeChain；`create_face` 在写入前拒绝，`validate_face` 检出存量缺陷，关联两个环与两条边 ID |
| `AXM-TOPO-E-0027` | Error | 上述线性边界片段在至少一条拓扑边的真实端点相接；复合折线/链的内部分段点不会被误当为边端点；创建与验证关联两环两边 ID |
| `AXM-TOPO-E-0028` | Error | 上述线性边界片段共线且存在正长度重叠；`create_face` 在分配 FaceId 前拒绝，`validate_face` 检出存量缺陷，关联两环两边 ID |
| `AXM-TOPO-E-0029` | Error | 不同边界环的线性片段或显式裁剪真曲线未以更严格容差命中，但在有效拓扑线性容差内邻近相接；`create_face` 与 `validate_face` 共用该判定 |
| `AXM-TOPO-E-0030` | Error | 跨环边界冲突无法确定：真曲边缺失必要的显式 trim，支撑曲线损坏，或有限区间求交无法在预算/数值约束内完成；预检、建面和存量面验证都闭合失败且无部分结果 |
| `AXM-TOPO-E-0031` | Error | 不同边界环的显式裁剪非线性曲线存在可证明的连续重合参数区间 |
| `AXM-TOPO-E-0032` | Error | 不同边界环的显式裁剪非线性曲线在拓扑边内部相交；使用误差受控 Geo 求交主流程 |
| `AXM-TOPO-E-0033` | Error | 不同边界环的显式裁剪非线性曲线在至少一条真实拓扑边端点接触 |

## 7.5 `BOOL` 布尔模块错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-BOOL-E-0001` | Error | 输入实体或布尔运算类型无效；预处理统计导出输入无效或路径为空 |
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
| `AXM-BLEND-W-0002` | Warning | 一次处理多条边时角区/连续圆角或倒角/变半径仍为占位实现 |

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
| `AXM-QUERY-E-0001` | Error | 最近点/实体距离查询失败（含距离/绕数数值失败及空实体无有限见证） |
| `AXM-QUERY-E-0002` | Error | 截面计算失败（含实体线段裁剪/事件分辨率/绕数数值失败） |
| `AXM-QUERY-E-0003` | Error | 质量属性或解析修剪面积的数值积分失败 |
| `AXM-QUERY-E-0004` | Error | 距离计算失败（既有保留码；S3-QUERY 体间距离数值失败复用 E-0001） |
| `AXM-QUERY-E-0005` | Warning | 质量属性基于近似网格计算 |
| `AXM-QUERY-E-0006` | Error | 多闭壳相交、重叠或容差接触，无法建立材料/空腔包含层级 |

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
| `AXM-VAL-E-0004` | Error | 检测到退化几何；`validate_geometry` 按根因绑定 `heal.validate_geometry.*` 阶段并关联目标 Body/问题子实体 |
| `AXM-VAL-E-0005` | Warning | 检测到薄壁高风险区域 |
| `AXM-VAL-E-0006` | Warning | 检测到高曲率不稳定区域 |
| `AXM-VAL-E-0010` | Error | 检测到非有限几何；第 70 批精确 B-Rep 文本子集导入绑定 `io.import.<format>.validation` |

`validate_geometry` 的失败阶段包括 `input`、`bbox`、`references`、`surface_domain`、`curve_domain`、
`vertices_finite`、`near_duplicate_vertices`、`edges`、`face_area` 与 `face_normal`，统一使用
`heal.validate_geometry.` 前缀，便于按阶段聚合。

## 7.11 `IO` 数据交换模块错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-IO-E-0001` | Error | 文件不存在（如 `IOService::validate_import_path` 校验时目标路径不存在） |
| `AXM-IO-E-0002` | Error | 文件格式无法识别 |
| `AXM-IO-E-0003` | Error | 文件内容损坏；第 70 批 AXMJSON/Axiom IGES 元数据/Axiom BREP JSON 子集的截断结构、缺失必需字段、错误 `format`、不支持的 `body_kind` 或缺失 BREP 文件头均绑定 `io.import.<format>.parse` |
| `AXM-IO-E-0004` | Error | 导入失败；STEP 早期失败绑定 `io.import.step.input/path/open`；OBJ 物化前失败绑定 `io.import.obj.input/path/open/parse`；STL、glTF 与 3MF 物化前失败分别绑定 `io.import.stl.*`、`io.import.gltf.*`、`io.import.3mf.*` 的 `input/path/open/read/parse/validation` 阶段。第 70 批为 AXMJSON/Axiom IGES 元数据/Axiom BREP JSON 子集补齐 `io.import.<format>.input/path/open/read`：非普通文件在读取前拒绝，超过 64 MiB 或短读归入 `.read`。OBJ/STL/glTF/3MF 退化三角形复用 `AXM-VAL-E-0002` 与各自的 `.validation` 阶段。3MF 非有限顶点在 `.validation` 阶段复用本码，非法数值及索引溢出在 `.parse` 阶段复用本码；物化前无模型实体，cycle-0073 起显式关联零实体令牌。 |
| `AXM-IO-E-0005` | Error | 导出失败 |
| `AXM-IO-E-0006` | Error | 严格网格导出 QA 失败（越界索引、退化三角形或检查不可用；`Issue.stage=io.export.mesh_strict_qa`，关联输入 Body） |
| `AXM-IO-E-0007` | Warning | 导入后存在未映射属性 |
| `AXM-IO-E-0008` | Warning | 导出采用兼容模式降级 |
| `AXM-IO-E-0009` | Error | 导出目标目录不可写（`kIoExportPathNotWritable`，默认 `Issue.stage=io.export.path`；STEP/OBJ/STL/glTF/3MF 导出使用各自 `io.export.<format>.path`） |
| `AXM-IO-E-0010` | Error | 检测到标准 STEP 物理文件 DATA 段含 EXPRESS 实例，非 Axiom 子集；完整交换未实现（`kIoStepStandardEntitiesUnsupported`，`Issue.stage=io.import.step`） |
| `AXM-IO-E-0011` | Error | 检测到典型 IGES 卡片/DE 流，非 Axiom 子集；完整交换未实现（`kIgesStandardEntitiesUnsupported`，`Issue.stage=io.import.iges`） |

### cycle-0073 HEAL/IO 重量级失败证据合同（已通过门禁）

本批未新增错误码常量，复用 CORE/TOPO/VAL/HEAL/IO 的既有根因码。`Issue.stage` 是定位流程的标签，不是新的错误码；复制子报告时保留根因码和实体，不修改源诊断。

| 工作流 | 阶段与证据 | 一致性及限制 |
|---|---|---|
| HEAL 验证 | `heal.validate_geometry.*`、`heal.validate_topology.*`、`heal.validate_self_intersection.*`、`heal.validate_tolerance.*` 及聚合验证；关联 Body/Shell/问题子实体 | 有限状态、实体数量及分支模式/计数/几何量，不扩大验证算法范围 |
| HEAL 修复 | `heal.sew_faces/remove_small_edges/remove_small_faces/merge_near_coplanar_faces/auto_repair.*`；后验失败为相应 `.post_validate` | 失败回收本次派生体与物化对象，保留原模型和失败证据 |
| Trim 重建 | `heal.repair_trim.input/surface/loop/rebuild/post_validate`；关联 Face/Surface/Loop 等 | Plane/Cylinder/Sphere；重建或复验失败恢复原 PCurve 绑定、删除新增 PCurve |
| 批量修复 | `heal.repair_many_*.input/rollback`，关联失败子项目标；回滚记录 `completed_item_count/requested_item_count/rollback_applied`；`repair_many_auto` 另附 `allocated_object_count`（分配 ID 增量） | 任一子项失败回滚此前全部派生对象和 Eval 失效状态；子项根因保留在原诊断，批量报告不合并全部子项 issue；不恢复 `next_id` |
| 主格式 IO 与 auto | `io.import.<format>.*`、`io.export.<format>.*`、auto 路由阶段；有限状态、实体数量及分支路径/计数证据 | STEP/AXMJSON/IGES/BREP/OBJ/STL/glTF/3MF；导出关联输入 Body，预物化文件失败显式使用 `[0]` 令牌 |
| 导入后验管线 | `io.post_import.validation/repair/post_validate`；复制 HEAL 问题，附验证/修复模式 | STEP/AXMJSON 接入共享管线；失败 issue 可随 `Ok` 导入结果返回，调用者须读取报告或显式验证 |
| 批量导入/导出 | `io.batch_import/io.batch_export`；保留根因及 `failed_item_index`（从零开始）、`completed_item_count`、`path_length`（byte） | STEP/AXMJSON/auto 批量导入实际失败恢复模型/网格/拓扑/几何、链接、缓存、Eval 失效及 `next_id`；批量导出不承诺文件回滚 |

失败数值证据至少含 `status_code`（enum）与 `related_entity_count`（count）。空名称或非有限测量值被过滤；过滤数量非零时记录有限的 `non_finite_evidence_omitted`（count），避免将 NaN/Inf 冒充有效证据。零实体令牌说明尚无模型对象或无有效目标，不可用于句柄查询。候选导入、严格现有文件导入、目录导出及条件导出传播真实失败；AXMJSON/IGES/BREP 导出补齐 `input/path/open/write` 与最终流检查。

`axiom_heal_test`、`axiom_io_workflow_test` 用 `issue_code_prefix="AXM-"` 与 `stage_prefix="heal."/"io."` 审计 Error 及以上 issue，并覆盖 JSON 数值证据、源报告不污染和回滚重试；cycle-0073 修复后完整 CTest **16/16 通过**。普通文本/目录工具等非主格式辅助接口尚未纳入该重量级包。标准 STEP/IGES 实体交换限制不变，IGES 仍按 `NotImplemented / AXM-IO-E-0011` 拒绝；设备或侧车写入失败不保证恢复目标文件。

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
| `AXM-PLUGIN-E-0002` | Error | 插件 `plugin_api_version` 与宿主不兼容（未声明，或不符合 `PluginApiVersionMatchMode` 规则） |
| `AXM-PLUGIN-E-0003` | Error | 插件能力声明不完整 |
| `AXM-PLUGIN-E-0004` | Error | 插件执行失败 |
| `AXM-PLUGIN-E-0005` | Warning | 插件返回结果未通过验证 |
| `AXM-PLUGIN-E-0006` | Error | 重复注册（清单 `name` 重复，或同类插件 `type_name` 冲突） |
| `AXM-PLUGIN-E-0007` | Error | 插件实例数超过宿主策略上限 |
| `AXM-PLUGIN-E-0008` | Error | 注销/卸载目标不存在（如 `type_name` 未注册，或清单 `name` 未命中） |

### 7.14.1 `PLUGIN` 插件模块诊断码

| 诊断码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-PLUGIN-D-0001` | Info | 插件能力发现快照已生成 |

## 7.15 `TX` 事务与版本模块错误码

| 错误码 | 严重级别 | 含义 |
|---|---|---|
| `AXM-TX-E-0001` | Error | 提交失败 |
| `AXM-TX-E-0002` | Error | 回滚失败 |
| `AXM-TX-E-0003` | Error | 写事务冲突，或保存点句柄无效/跨事务/已失效，或尝试释放非最内层保存点；均返回 `OperationFailed` 且不改模型 |
| `AXM-TX-E-0004` | Error | 目标版本不存在 |
| `AXM-TX-E-0005` | Fatal | 版本图损坏 |
| `AXM-TX-E-0006` | Error | 活动事务禁止清空跟踪记录；`clear_tracking_records` 返回 `OperationFailed`，保留模型与撤销记录，须先提交或回滚（含空事务） |
| `AXM-TX-E-0007` | Error | 拓扑事务在预取消、显式轮询、写入、提交、显式回滚或作用域退出边界观察到协作式取消。已取得写者槽的事务恢复完整快照并释放写者槽，不推进版本或成功提交审计；预取消事务不取得写者槽 |

## 8. 标准警告码清单

以下警告码建议作为常用跨模块警告：

| 警告码 | 含义 |
|---|---|
| `AXM-CORE-W-0001` | 当前操作采用默认容差 |
| `AXM-MATH-W-0001` | 谓词进入高精度回退 |
| `AXM-GEO-W-0001` | 几何求值结果接近退化区域 |
| `AXM-BOOL-W-0001` | 布尔预处理近退化/仅接触或受限 bbox 语义告警（包括分离输入与 Split 占位）；`Issue.stage=bool.prep` |
| `AXM-BOOL-W-0002` | 壳/区域级无局部候选，交集回退全局 bbox 或减运算保留左体；`Issue.stage=bool.prep` |
| `AXM-BOOL-W-0003` | 布尔结果在重建后 Strict 验证仍残留问题（可审计告警） |
| `AXM-BLEND-W-0002` | 圆角/倒角一次处理多条边时角区仍为占位实现 |
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

`BooleanService::export_boolean_prep_stats` 失败均携带 `[lhs, rhs]`（保留无效 ID）：参数失败为 `InvalidInput` / `AXM-BOOL-E-0001` / `bool.prep.export.input`；文件打开失败为 `OperationFailed` / `AXM-IO-E-0005` / `bool.prep.export.open`（修正此前误用的 BOOL 输入码）；写入或关闭失败为 `OperationFailed` / `AXM-IO-E-0005` / `bool.prep.export.write`。参数校验先于文件打开，失败不创建或截断目标；所有失败不修改模型，底层写入失败不保证目标文件恢复。回归入口：`axiom_boolean_prep_test`，Linux 使用 `/dev/full` 覆盖缓冲写入失败。

`DiagnosticService::export_grouped_by_stage_txt/json` 的空路径、文件打开及最终写入/关闭失败复用 `AXM-IO-E-0005`；空路径在打开文件前拒绝，失败不修改参与聚合的源报告，底层设备写入失败不保证恢复目标文件。回归入口：`axiom_diagnostics_test`。

`IOService::export_obj/export_stl/export_gltf/export_3mf` 的失败阶段为 `io.export.<format>.input/path/convert/mesh/open/write/sidecar`，并关联输入 Body（无效 ID 也保留）。输入、网格数据、打开与最终写入/关闭失败复用 `AXM-IO-E-0005`，路径与转换/侧车失败保留下层错误码；严格 QA 继续使用 `AXM-IO-E-0006 / io.export.mesh_strict_qa`。失败回滚本次新增网格、实体 ID、体/面三角化缓存及统计，保留诊断；输入/转换/校验失败保护已有文件，设备写入失败不保证恢复文件，侧车失败可能保留完整主文件。策略和回归见 [IO 导出策略矩阵](../quality/AxiomKernel_IO_导出策略矩阵.md)；第 64 包已纳入第 68 批全量门禁。

`IOService::export_step` 的失败继续复用 `AXM-IO-E-0005`，并关联输入 Body：无效 Body/空路径为 `io.export.step.input`，父目录不存在或不可写为 `io.export.step.path`，打开失败为 `io.export.step.open`，最终写入或关闭失败为 `io.export.step.write`。输入/路径失败不创建目标文件，所有失败不修改模型；底层设备写入失败不保证恢复目标文件。回归入口：`axiom_io_workflow_test`。

与 **`AXM-BOOL-E-*` 错误码**绑定的布尔早期失败路径会在 `Issue.stage` 中写入可聚合阶段标签（与 `export_report_json` 一致）：`bool.input`（输入体或布尔运算类型无效，`AXM-BOOL-E-0001`）、`bool.abort.intersect`（交集在包围盒层面不相交，`AXM-BOOL-E-0003`）、`bool.abort.classify`（如减运算右包左无法表达空结果，`AXM-BOOL-E-0005`）。上述早期失败即使设置 `BooleanOptions::diagnostics=false`，也保留单条 Error Issue、阶段标签与 `[lhs, rhs]`（包括无效输入值），只省略候选阶段/统计信息；成功时关闭诊断的行为不变。启用布尔诊断时，返回的预处理告警 `AXM-BOOL-W-0001/W-0002` 同步写入报告，保留原文案和 Warning 级别，并绑定 `bool.prep` 与 `[lhs, rhs, output]` 实体 ID，可按阶段检索及导出 JSON；这些告警不表示精确布尔能力。Strict 残留告警见 `bool.validate.residual`（`AXM-BOOL-W-0003`）。

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

自交验证失败使用可聚合阶段标签：体级非法输入为 `heal.validate_self_intersection.input`，退化偏置为
`heal.validate_self_intersection.degenerate`，Strict 三角化/SAT 分析失败为
`heal.validate_self_intersection.mesh`；壳级及批量壳级对应
`heal.validate_self_intersection.shell_input`、`heal.validate_self_intersection.shell_degenerate` 和
`heal.validate_self_intersection.shell_mesh`。失败 Issue 关联目标 Body，并在适用时同时关联目标 Shell。
这些阶段描述当前网格近似自交验证流程，不表示精确曲面自交能力。

## 9.4 `IO` 诊断码

| 诊断码 | 含义 |
|---|---|
| `AXM-IO-D-0001` | 文件已打开 |
| `AXM-IO-D-0002` | 解析实体中 |
| `AXM-IO-D-0003` | 发现未映射属性 |
| `AXM-IO-D-0004` | 导入后触发自动验证 |
| `AXM-IO-D-0005` | 导入后自动修复策略提示（`kIoPostImportRepairMode`） |
| `AXM-IO-D-0008` | 网格验证 JSON 侧车已写出（`kIoExportMeshReportSidecar`） |
| `AXM-IO-D-0009` | 批量导入在指定项失败（`Issue.stage=io.batch_import`，`kIoBatchImportItemContext`） |
| `AXM-IO-D-0010` | 批量导出在指定项失败（`Issue.stage=io.batch_export`，`kIoBatchExportItemContext`） |
| `AXM-IO-D-0011` | 批量格式识别在指定项失败（`Issue.stage=io.batch_detect_format`，`kIoBatchDetectFormatItemContext`） |
| `AXM-IO-D-0012` | 批量读取/行数统计/文本预览在指定项失败（`Issue.stage=io.batch_read`，`kIoBatchReadItemContext`） |
| `AXM-IO-D-0013` | 批量文本比较在指定项失败（`Issue.stage=io.batch_compare`，`kIoBatchCompareItemContext`） |
| `AXM-IO-D-0014` | 批量路径写操作在指定项失败（`Issue.stage=io.batch_path_op`，`kIoBatchPathOpItemContext`；如追加/touch/删文件/创建父目录） |
| `AXM-IO-D-0015` | 批量路径变换/校验在指定项失败（`Issue.stage` 多为 `io.batch_path_transform`、`io.batch_validate_import` 或 `io.batch_validate_export`，`kIoBatchPathTransformItemContext`；如 `normalize_paths` / `compose_paths` / `change_extensions` / `validate_import_paths` / `validate_export_paths`） |
| `AXM-IO-D-0016` | 标准 STEP：物理层 `#id=TYPE` 实例类型频度扫描摘要（`kIoStepStandardFileScanSummary`，`Issue.severity=Info`，`Issue.stage=io.import.step`；与 `E-0010` 同报告） |
| `AXM-IO-D-0017` | 标准 IGES：Directory Entry 实体类型号频度扫描摘要（`kIgesStandardFileScanSummary`，`Issue.severity=Info`，`Issue.stage=io.import.iges`；与 `E-0011` 同报告） |

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


### FR-QUERY-001 长度查询诊断（第 63/67 功能包）

- `AXM-CORE-E-0001`：长度查询目标句柄无效（包括删除或回滚后的句柄）。
- `AXM-CORE-E-0002`：长度区间、容差或结果非有限，请求无限直线全域长度，长度或累计长度超出 `Scalar` 范围，容差为负、两种容差同时为零，或速度求值预算为零；无数值。
- `AXM-GEO-E-0004`：有界曲线长度参数超出定义域；不做钳制。
- `AXM-GEO-E-0011`：椭圆/圆锥曲线或样条的自适应长度积分因预算耗尽、精度停滞或非有限速度失败；`OperationFailed`，无部分长度。
- `AXM-GEO-E-0003`：长度查询所用直线方向退化；`DegenerateGeometry`。
- `AXM-CORE-E-0004`：未来新增但尚无长度实现的曲线类型，或缺少裁剪区间的拓扑曲边；`NotImplemented`。
- `AXM-TOPO-E-0008`：直线边长度查询的端点重合、端点偏离支撑曲线或超出线段范围；`InvalidTopology`。
- `AXM-TOPO-E-0006`：边引用的曲线/顶点缺失；`InvalidTopology`。
- `AXM-TOPO-E-0002`：环为空、未闭合或成员关系损坏；`InvalidTopology`。
- `AXM-TOPO-E-0003/0004`：面外/内环引用缺失或重复；`InvalidTopology`。

边→环→面失败原样传播诊断且不返回部分和；查询只增加诊断和 Topo 查询审计，不修改模型、事务写计数、几何/网格缓存或 Eval 失效状态。

### FR-QUERY-001 解析曲面修剪面积诊断（第 68 批）

- `AXM-CORE-E-0001`：`TopologyQueryService::face_area` 目标面句柄无效、已删除或已回滚；`InvalidInput`，无面积。
- `AXM-CORE-E-0004`：Bezier/BSpline/NURBS/Revolved/Swept 面尚无面积实现；`NotImplemented`，无面积。
- `AXM-TOPO-E-0008`：非平面缺少 PCurve、同一面仅部分定向边绑定 PCurve、曲面包装链/偏置/参数域与拓扑不兼容，或 PCurve 端点映射与定向拓扑顶点不一致；`InvalidTopology`。
- `AXM-TOPO-E-0003/0004`：外/内 PCurve 环缺失、空、非折线、断裂、未闭合、退化、自交或越出参数域；内环不严格位于外环内，或内环之间相交、重叠、嵌套，使用 `E-0004`。
- `AXM-GEO-E-0003`：扣除内环后面积非正或数值退化；`DegenerateGeometry`。
- `AXM-QUERY-E-0003`：解析面积密度积分产生非有限结果；`NumericalInstability`。
- `AXM-CORE-E-0002`：有限解析结果超出 `Scalar` 范围；`InvalidInput`。

查询不返回部分面积，不写几何/求值/网格缓存，不创建模型对象。未包装 Plane 且完全无 PCurve 时兼容调用 `planar_face_area`，并沿用其既有失败码。

### FR-QUERY-001 闭合多面体拓扑质量属性诊断（第 70 批）

- `AXM-CORE-E-0001`：`shell_mass_properties/body_mass_properties` 目标句柄无效、已删除或已回滚；`InvalidInput`，无部分值。
- `AXM-CORE-E-0004`：壳含曲面或曲边；`NotImplemented`，不使用弦长、网格或 bbox 近似。
- `AXM-TOPO-E-0005`：壳为空、开放、非流形，或引用的面/曲面缺失；`InvalidTopology`。
- `AXM-TOPO-E-0015`：共享边在两个相邻面中同向，或外/内环绕向与平面法向不一致；`InvalidTopology`。
- `AXM-TOPO-E-0003/0004/0006/0009`：面环、定向边、顶点链或重复壳/面引用损坏；`InvalidTopology`。
- `AXM-GEO-E-0003`：平面法向、面环面积或闭壳有向体积退化；`DegenerateGeometry`。
- `AXM-QUERY-E-0003`：单壳积分或多壳汇总产生非有限/越界结果；`NumericalInstability`。
- `AXM-QUERY-E-0006`：`body_shell_regions/body_mass_properties` 发现闭壳相交、重叠、容差接触或矛盾包含关系；`InvalidTopology`，关联 Body 与冲突 Shell，无部分层级或质量属性。
- `AXM-CORE-E-0002`：顶点坐标或拓扑规模超出可处理范围；`InvalidInput`。

查询从当前真实拓扑重算且不返回部分值；不分配网格、不写缓存、不修改 Eval 状态或事务写计数，仅增加诊断和一次顶层查询审计。

### FR-QUERY-001 有界 3D 曲线-曲线求交诊断（第 71 批）

- `AXM-CORE-E-0001`：`intersect_curve_curve` 的任一曲线句柄无效或已回滚；`InvalidInput`。
- `AXM-CORE-E-0002`：位置/参数容差非有限正数、角容差不在 `[0,1]`、求值预算小于 6、细分预算为 0，或无限 Line 未指定有限参数区间；`InvalidInput`。
- `AXM-GEO-E-0004`：请求的有限参数区间超出有界曲线定义域；`InvalidInput`。
- `AXM-GEO-E-0007`：点值求值或候选参数矩形预算耗尽时为 `OperationFailed`；无法建立有限保守界或参数精化停滞时为 `NumericalInstability`。

空交集为成功的空结果，不生成错误码。上述失败均不返回部分交点/重合区间，不写曲线求值缓存、Intersection/拓扑存储或活动事务计数；仅新增可检索诊断。

第 71 批 repair 只修正 Bezier 相切邻域的细分终止判据和非连续 CompositeChain 子片的单侧端点建界；既有位置/参数/角容差、包围排除、最终残差判定和预算失败合同均未放宽，也未新增或改变错误码。

### FR-GEO-001 曲面全域最近点证书（第 74 批）

- `AXM-CORE-E-0002`：查询点非有限，距离/参数容差非法，或数值求值预算小于 5；`InvalidInput`。
- `AXM-GEO-E-0006`：目标曲面不存在、通用无限派生面尚无有限搜索域，解析有限化/结果超出可表示范围，或有界搜索无法建立保守变化界、初始片或后续细分耗尽预算；状态按根因为 `InvalidInput` / `NumericalInstability` / `OperationFailed` / `DegenerateGeometry`。
- `AXM-GEO-E-0010`：嵌套 Offset 链在中间层或最终层发生有效半径坍缩，负向完整圆锥偏置在全域自交，或规则环面偏置失去规则性；`DegenerateGeometry`。

Plane/Cylinder/Cone/规则 Sphere/Torus 及其嵌套 Offset 的成功解析路径返回 `Analytic`，无界域同时提供 `effective_domain` 和 `domain_was_finiteized=true`。Bezier/BSpline/NURBS 以正权有理控制网凸包 AABB 建立下界，并通过 `control_net_bound_patches/pruned_patches` 暴露证书工作量。上述失败均无部分值、不写 surface eval 缓存。

### FR-TOPO-001 显式真曲边跨环冲突（第 74 批）

- `AXM-TOPO-E-0030`：真曲边缺少有限 trim、曲线记录损坏，或 Geo 求交预算/数值失败。`first_boundary_conflict` 不返回部分 `optional`；`create_face/validate_face` 闭合失败。
- `AXM-TOPO-E-0031`：两条显式裁剪非线性边界曲线存在可证明的连续重合区间；`InvalidTopology`。
- `AXM-TOPO-E-0032`：两条显式裁剪非线性边界曲线在拓扑边内部相交；`InvalidTopology`。
- `AXM-TOPO-E-0033`：两条显式裁剪非线性边界曲线在真实拓扑边端点接触；`InvalidTopology`。
- `AXM-TOPO-E-0029`：第二轮统一线性容差求交发现真曲线邻近接触；与既有线性路径复用同一语义。

冲突证据的 `error_controlled/solver_tolerance/curve_evaluations/parameter_rectangles_processed` 标识真曲线求解路径、有效容差和工作量。该路径不写曲线求值缓存或 Intersection 存储；建面失败不分配实体、不增加事务写计数。

### NFR-REL-001 拓扑事务保存点（第 74 批）

- `AXM-TX-E-0003`：`rollback_to_savepoint/release_savepoint` 收到默认、跨事务、已失效句柄，或释放目标不是当前最内层保存点；`OperationFailed`，模型、撤销基线、写审计和保存点栈不变。
- `AXM-TX-E-0007`：保存点操作入口观察到取消；优先恢复整个事务快照、关闭事务并清空保存点，不执行局部回滚。

正常局部回滚不生成错误码；目标保存点保留供重复回滚，其内层句柄失效。`TopologySavepointMetrics` 累计创建/回滚/释放/内层丢弃及局部回滚写次数，`core_runtime_invariants_hold()` 验证审计自洽性。

### FR-OPS-001 SweepService 新路径诊断（第 68/70/71 批）

- `AXM-CORE-E-0001`：`sweep/sweep_scaled` 轮廓标签为空或导轨句柄无效。
- `AXM-CORE-E-0002`：`extrude_to_plane` 的轮廓/孔非法、方向或平面非有限/近退化、方向切向或反向、平面接触/相交或舍入塌缩。
- `AXM-CORE-E-0002`：`revolve` 角度不在 `(0,2π]`，或 `revolve_between` 起止角非有限、有符号跨度为零/绝对值超过 `2π`；也用于轴非法，或轮廓区域跨轴/孤立触轴/近轴、自交、非共面/偏轴。带孔区域必须与轴严格分离。正/负部分角与整周均在分配前完成闭壳、质量和惯性检查。
- `AXM-CORE-E-0002`：`loft` 截面不足、缺显式轮廓、非共面/自交、环拓扑或顶点数不兼容、站序不严格，或插值截面退化/翻折；失败不回退 bbox 占位体。
- `AXM-CORE-E-0002`：曲线 `sweep/sweep_scaled` 截面非法或未与导轨起点/切向对齐，样条伪闭合/首尾切向断裂，复合链接缝错位/折角/尖点、嵌套链或含未支持子段，过紧曲率、非局部弦段自靠近，或周期标架/物化/质量积分失败。`sweep_scaled` 还以此码拒绝非有限/零/负终端比例、非单位比例周期导轨、非显式轮廓、截面折叠/退化和失配端盖。

上述 Ops 失败允许新增诊断，但在模型/拓扑对象和 ID 分配前完成验证，不改变活动拓扑事务写计数，也不污染求值或网格缓存。

### NFR-DIA-001 精确 B-Rep 文本导入诊断（第 70 批）

- `AXM-IO-E-0004`：AXMJSON、Axiom IGES 元数据子集和 Axiom BREP JSON 子集的空路径、不存在路径、非普通文件/打开失败、文件超过 64 MiB 或短读，分别绑定 `io.import.<format>.input/path/open/read`。
- `AXM-IO-E-0003`：截断 JSON、缺少或无效必需字段、入口与 `format` 不匹配、`body_kind` 不支持或 BREP 文件头缺失；绑定 `io.import.<format>.parse`。
- `AXM-VAL-E-0010`：几何元数据含 NaN 或 Inf；返回 `InvalidInput`，绑定 `io.import.<format>.validation`。
- `AXM-VAL-E-0004`：包围盒反转/无效或轴退化；返回 `DegenerateGeometry`，绑定 `io.import.<format>.validation`。

三种格式的物化前失败都返回可检索 `diagnostic_id`，不写 Body/Mesh store、不推进模型 `next_id`；修复文件后可原位重试。标准 IGES 实体仍沿用 `NotImplemented / AXM-IO-E-0011` 和 `io.import.iges`，不纳入 Axiom 子集的 `.parse/.validation` 承诺。

### FR-OPS-001 分段截面律诊断（cycle-0074，未新增错误码）

- `AXM-CORE-E-0002 / InvalidInput`：`extrude_with_law` 无显式轮廓/标签、中心非有限或离面、方向非有限/非零法向合同不满足、距离非正/非有限；三个律入口的关键站数量不在 2 至 4097、fraction 非有限/未严格递增或首末不为 0/1、首比例不为 1（带扭角时首角不为 0）、比例非有限或不严格正、扭角非有限、累计绝对扭角超过一周、联合采样超过 4096 区间；实际截面舍入坍塌、孔非法、壁片交叠/容差接触、盖片失配、折叠或闭壳质量积分失败也复用此码。
- `AXM-CORE-E-0001 / InvalidInput`：`sweep_with_scale_law/sweep_with_law` 的空轮廓标签或无效导轨句柄。`extrude_with_law` 的空标签走前述 `E-0002`。
- `AXM-CORE-E-0002 / InvalidInput`：扫掠起点/截面平面失配、直线/折线不严格同向推进、曲线标架退化、全律最大比例下曲率/间距门禁失败、周期末比例不为 1、联合周期末扭角不是 0 或 ±2π（`1e-10 rad` 容差）、数值不可分辨关键站、曲线接触宽相候选超过 2000000。不推断对称截面的顶点置换。

扫掠律的 `sweep_law_sampling` 表示站点分辨率/联合采样/插值标架失败，`sweep_law_materialization` 表示实际截面/壁片/接触候选/闭壳质量失败，`sweep_law_seam` 表示周期末扭角失配。这些分支附导轨实体及 `law_key_count/sampled_intervals/maximum_intervals/minimum_scale/maximum_scale/maximum_scale_step_ratio/maximum_contact_candidates` 数值证据；联合律另附 `absolute_twist_travel/maximum_twist_step/terminal_twist`（rad）。输入和曲率等前置失败仍走既有诊断，不承诺所有输入失败都有这三个阶段。拉伸律当前复用输入/物化根因文案及 `diagnostic_id`，未增加上述扫掠阶段。

失败不返回 BodyId，检查在模型/几何/拓扑/ID 分配前完成，保留活动事务、缓存与 Eval；可增加诊断。正比例/分段线性律及停顿/反向属于已验收子域；零/负比例、尖顶、至平面组合、一般非线性解析律和解析扫掠/螺旋曲面仍未实现。

### FR-QUERY-001 多面体实体空间查询诊断（cycle-0074，未新增错误码）

| 码 / 状态 | `locate_point / clip_segment` 触发条件 |
|---|---|
| `AXM-CORE-E-0001 / InvalidInput` | BodyId 无效、已删除或已回滚 |
| `AXM-CORE-E-0002 / InvalidInput` | 坐标非有限、位置容差为负/非有限、三角形预算为零或耗尽、线段长度超出 Scalar 有限范围 |
| `AXM-GEO-E-0003 / DegenerateGeometry` | 线段长度不大于有效位置容差；亦沿用质量前置检查的几何退化语义 |
| `AXM-QUERY-E-0001 / NumericalInstability` | 点到三角形距离退化/溢出或点定位绕数无法可靠判定 |
| `AXM-QUERY-E-0002 / NumericalInstability` | 局部面片求交退化/溢出、不同边界事件在归一化参数中无法可靠分离、区间绕数无法可靠判定或材料长度溢出 |
| `AXM-CORE-E-0004 / NotImplemented` | 闭壳含曲面或曲边（包括显式 trim 曲边），拒绝以端点弦替代曲边 |
| `AXM-TOPO-E-0008 / InvalidTopology` | 面边界不位于支撑平面；共享质量属性前置检查同样加严 |
| `AXM-QUERY-E-0006 / InvalidTopology` | 壳间相交、重叠、建模容差接触或矛盾包含关系 |

其余闭壳/面/环/边/质量失败沿用上文质量属性与包含层级诊断并原样传播，不返回部分最近点、区间或材料长度。线段无交集为成功空结果；内部零壳分支的空结果合同不代表公共 API 可创建空体，cycle-0075 未构造或验收该分支。位置容差不膨胀材料，事件分辨率不足闭合失败；新三角形预算仅覆盖质量/壳关系前置检查之后的距离、求交与绕数，成功公开实际 `triangle_tests`。诊断与一次顶层 Topo 查询审计允许增加；模型、MeshId、缓存、Eval 和活动事务写计数不变。要求无自交的嵌入平面直边双边流形闭壳，本批未新增壳自身全局自交证明。

上述 Ops/Query 失败码、阶段、数值证据、预算及事务/只读回归分别纳入 `axiom_ops_heal_test` 与 `axiom_query_eval_test`；[cycle-0074 门禁日志](../../.axiom-agent/logs/cycle-0074-gates.log) 记录完整 CTest 16/16、0 失败、134.05 s。本轮未重新构建或运行测试。


### S3-QUERY 截面与距离诊断（cycle-0075，复用既有错误码）

通用/专用入口共用失败状态、稳定错误码和 Issue.stage；兼容 `section` 与 `min_distance` 沿用详细结果 diagnostic_id。前置检查不能因为平面远离 bbox 而跳过，所有失败无部分结果。

共享体类门禁也用于 `body_shell_regions`，该入口不支持体类返回 `NotImplemented / AXM-CORE-E-0004 / query.body.support_gate`。`body_mass_properties` 将前置失败映射为 `query.mass_properties.support_gate/preflight`；通用基本解析体质量仍有独立解析路径，不因实体截面/距离拒绝而一并宣称不支持。

| 错误码 / StatusCode | 稳定 `Issue.stage` | 根因与约定 |
|---|---|---|
| `AXM-CORE-E-0002 / InvalidInput` | `query.section.input_gate`、`query.closest_point.input_gate`、`query.distance.input_gate` | 平面法向非有限/零、坐标非有限、负或非有限位置容差、零工作预算（各入口适用的输入） |
| `AXM-CORE-E-0002 / InvalidInput` | `query.section.budget`、`query.closest_point.budget`、`query.distance.budget` | 前置检查之后实际工作预算耗尽；无部分值，不继续发布网格 |
| `AXM-CORE-E-0004 / NotImplemented` | `query.section.support_gate`、`query.closest_point.support_gate`、`query.distance.support_gate`、`query.mass_properties.support_gate` | 解析曲面、曲边、旧占位或非 ExactBRep 等不支持真实多面体体类/边界；不允许 bbox 壳代替 |
| `AXM-CORE-E-0001 / InvalidInput` | `query.section.preflight`、`query.closest_point.preflight`、`query.distance.preflight`、`query.mass_properties.preflight` | 无效、删除或已回滚 BodyId |
| `AXM-TOPO-E-0005 / InvalidTopology` | 相应 `query.*.preflight` | 删除面后壳不闭合等闭壳前置失败；其他环/边/面根因沿用已有 TOPO/GEO 码 |
| `AXM-TOPO-E-0008 / InvalidTopology` | 相应 `query.*.preflight` | 当前面边界与支撑平面错配 |
| `AXM-QUERY-E-0006 / InvalidTopology` | 相应 `query.*.preflight` | 多壳相交、重叠、建模容差接触或包含关系冲突 |
| `AXM-QUERY-E-0002 / NumericalInstability` | `query.section.numeric` | 平面有向距离符号、不同交段/事件或世界坐标不可分辨，面积溢出；远隔小分量不可静默丢失 |
| `AXM-QUERY-E-0001 / NumericalInstability` | `query.closest_point.numeric`、`query.distance.numeric` | 三角形/面边距离退化、溢出或材料绕数无法可靠判定 |
| `AXM-QUERY-E-0003 / NumericalInstability` | `query.mass_properties.numeric` | 当前拓扑质量汇总、质心或惯性不可表示；不恢复编辑前缓存值 |
| `AXM-QUERY-E-0001 / DegenerateGeometry` | `query.distance.empty_gate` | 内部零壳体分支无有限最近见证；合同保留，但本批未构造/验证该查询分支 |
| `AXM-TOPO-E-0005 / InvalidTopology` | `query.mass_properties.empty_gate` | 内部零壳体无可积分边界；不是本批零壳查询验收证据 |

公共 `create_body({})` 已回归为 `OperationFailed / AXM-TX-E-0001`、无 value、事务写计数不变；这不能用来声称测试了零壳体查询。真正无交集截面是成功空结果，bbox 无效，兼容入口有 value 的 `MeshId{}`；共面面有面积，线/点相切成功零面积，均无失败 issue。最近体间距离的相交、包含、相切为成功 0；可表示正间隙不被位置容差或裁剪舍入带抹为零。预算默认 1000000，仅计前置之后工作，精确重放与少一预算失败均已回归。

依据 [cycle-0075 最终全量门禁](../../.axiom-agent/logs/cycle-0075-gates.log)，Query/Eval、Ops/Heal、representation/IO 及全量 CTest 已通过；逐项证据见 [测试与验收方案](../quality/AxiomKernel_测试与验收方案.md)。未新增错误码，S3-QUERY 正式验收以调度器文档检查及提交成功为准，FR-QUERY-001 保持进行中。
