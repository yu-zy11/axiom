# AxiomKernel 几何引擎技术架构文档

下面这份文档可直接作为新一代几何引擎的立项初稿。目标不是写成学术论文，而是写成一份能指导研发团队拆模块、定接口、排优先级的工程设计说明。

## 目标定义

项目代号先叫 `AxiomKernel`。

### 核心目标

- `高效`：针对现代 CPU 缓存、多线程、任务图、增量重算优化。
- `简洁`：内核只负责几何、拓扑、鲁棒性和核心操作，不把 UI、历史树、装配、PLM 直接揉进去。
- `可扩展`：几何表示、布尔策略、修复器、数据交换器、求解器都可插拔。
- `可验证`：每个关键操作可输出诊断、失败证据、修复日志，而不是黑盒返回成功或失败。
- `混合表示`：原生支持 `B-Rep`、网格、隐式表示三类几何世界。
- `工业可用`：不是研究 demo，要能逐步走向 CAD/CAM/CAE 生产场景。

### 非目标

- 第一阶段不做完整 PLM。
- 第一阶段不做全套 CAD UI。
- 第一阶段不追求覆盖所有高阶曲面构造。
- 第一阶段不追求一次性超越所有商业内核的边角圆角能力。

## 一、总体架构

采用 `8 层架构 + 1 条统一诊断链路`。

### 架构分层

1. `MathCore`：数学与鲁棒谓词层
2. `GeoCore`：几何原语与求值层
3. `TopoCore`：拓扑结构层
4. `RepCore`：表示统一层
5. `OpsCore`：建模操作层
6. `HealCore`：修复与验证层
7. `EvalGraph`：缓存、依赖、增量执行层
8. `PluginSDK`：扩展与 ABI 层

统一贯穿：

- `Diagnostics`：诊断、日志、证据、指标体系

### 分层原则

- 上层依赖下层，下层不反向知道上层。
- 拓扑不直接依赖具体几何实现，只依赖几何接口。
- 参数化/特征树不放进几何内核内部，只通过依赖图和事务层接入。
- 所有修改通过事务完成，不允许隐式全局状态污染。

## 二、模块设计

## 2.1 `MathCore`

这是内核最重要的基础，不强就会导致所有布尔、偏置、分类最后都不稳。

### 主要职责

- 向量、矩阵、四元数、仿射变换
- 区间算术
- 浮点鲁棒谓词
- 多精度数值回退
- 误差传播估计
- 单位系统与公差策略

### 核心子模块

#### `LinearAlgebra`

- `Vec2/Vec3/Vec4`
- `Mat3/Mat4`
- `Transform3`
- `BoundingBox`
- `OrientedBox`
- `Frame3`

要求：

- `SIMD` 友好
- 小对象栈上分配
- 尽量无虚函数

#### `Predicates`

- 点在线上/面上判定
- 点在曲面参数域内判定
- 面内外分类
- 曲线/曲面相对位置判断
- 定向体积、定向面积
- 包含关系和邻接关系判定

要求：

- 默认双精度快路径
- 不确定时自动升级到区间算术
- 再不确定时进入多精度回退

#### `ExactFallback`

- 多精度浮点
- 有理数构造
- 精确符号判断

不是全局默认开启，只用于关键判定点：

- 布尔区域分类
- 自交检测边界判定
- 交线端点缝合决策

#### `ToleranceSystem`

- 全局默认容差
- 局部容差
- 操作级容差
- 输入数据源容差
- 容差场查询接口

核心思想：

- 不使用单一全局 tolerance 解决所有问题
- 每个操作都必须显式携带公差策略

### 核心接口示意

```cpp
struct TolerancePolicy {
  double global_linear;
  double global_angular;
  double min_local;
  double max_local;
  PrecisionMode precision_mode;
};

enum class PrecisionMode {
  FastFloat,
  AdaptiveCertified,
  ExactCritical
};
```

## 2.2 `GeoCore`

负责几何对象的精确定义和求值，但不负责拓扑。

### 支持的几何类型

#### 曲线

- 直线
- 射线
- 线段
- 圆
- 椭圆
- 抛物线
- 双曲线
- `BezierCurve`
- `BSplineCurve`
- `NURBSCurve`
- 复合曲线
- 交线曲线
- 隐式曲线代理

#### 曲面

- 平面
- 圆柱面
- 圆锥面
- 球面
- 环面
- 旋转面
- 扫掠面
- `BezierSurface`
- `BSplineSurface`
- `NURBSSurface`
- 偏置面
- 修剪面
- 隐式曲面代理

#### 混合/辅助对象

- 参数域曲线 `PCurve`
- 交线表示 `IntersectionCurve`
- 局部近似面片 `LocalPatch`
- 几何缓存对象 `EvaluationCache`

### 核心设计原则

- 几何对象只负责“如何求值”，不关心它被哪个拓扑面引用。
- 所有几何对象都实现统一求值协议。
- 允许一个曲面拥有多个求值后端，例如原生解析求值、数值求值、近似求值。

### 统一求值接口

```cpp
class CurveEvaluator {
public:
  virtual Point3 eval(double t) const = 0;
  virtual Vec3 tangent(double t) const = 0;
  virtual CurveDerivatives derivatives(double t, int order) const = 0;
  virtual ClosestPointResult closest_point(const Point3&) const = 0;
  virtual ParameterRange domain() const = 0;
};

class SurfaceEvaluator {
public:
  virtual Point3 eval(double u, double v) const = 0;
  virtual Vec3 du(double u, double v) const = 0;
  virtual Vec3 dv(double u, double v) const = 0;
  virtual NormalResult normal(double u, double v) const = 0;
  virtual CurvatureResult curvature(double u, double v) const = 0;
  virtual ClosestPointResult closest_point(const Point3&) const = 0;
  virtual UVRange domain() const = 0;
};
```

### 几何对象存储策略

- 大对象放 arena 或 slab 中
- Handle/ID 稳定
- 求值器与几何数据分离
- 热路径数据连续存储
- 支持只读共享与写时复制

## 2.3 `TopoCore`

负责定义实体的拓扑关系，是内核的结构骨架。

### 拓扑实体

- `Vertex`
- `Edge`
- `Coedge`
- `Loop`
- `Face`
- `Shell`
- `Body`
- `Region`
- `AssemblyProxy` 可选，仅作轻连接，不承担完整装配语义

### 关键原则

- 几何与拓扑分离
- 使用稳定 ID 而不是深层对象指针图
- 支持流形实体，也允许受控非流形
- 支持版本化和事务回滚

### 拓扑关系模型

建议用 `incidence tables` 而不是纯 OO 树。

例如：

- `Edge -> [start_vertex, end_vertex]`
- `Coedge -> [edge_id, face_id, loop_id, orientation]`
- `Face -> [surface_id, outer_loop_id, inner_loop_ids...]`
- `Shell -> [face_ids...]`
- `Body -> [shell_ids...]`

### 为什么用表驱动

- 更利于缓存
- 更利于并行扫描
- 更利于序列化
- 更利于版本差异分析
- 更利于把拓扑验证做成批量算法

### 拓扑约束系统

- 边方向一致性
- 面环闭合性
- 壳封闭性
- 参数曲线与 3D 曲线一致性
- 拓扑邻接完整性
- 非流形规则白名单

### 拓扑事务接口

```cpp
class TopologyTransaction {
public:
  VertexId create_vertex(const Point3Ref&);
  EdgeId create_edge(CurveRef, VertexId v0, VertexId v1);
  FaceId create_face(SurfaceRef, LoopId outer);
  void attach_inner_loop(FaceId, LoopId);
  void remove_face(FaceId);
  CommitResult commit();
  void rollback();
};
```

## 2.4 `RepCore`

这层是新引擎区别于传统 CAD 内核的关键之一，用来统一多种表示。

### 支持的表示类型

#### `ExactBRep`

- 精确边界表示
- 工业 CAD 核心表示
- 最重要、优先级最高

#### `MeshRep`

- 三角网格/多边形网格
- 用于扫描、可视化、近似布尔、仿真桥接

#### `ImplicitRep`

- 有符号距离场
- 程序化体
- 晶格/TPMS/生成式设计支撑

#### `HybridRep`

- 同时包含多个子表示
- 一部分区域精确、一部分区域近似
- 用于复杂制造和混合设计流程

### 统一查询接口

任何上层算法都不直接写死“只对 B-Rep 操作”，而是通过统一 query 层判断能力。

```cpp
class Representation {
public:
  virtual RepKind kind() const = 0;
  virtual BoundingBox bbox() const = 0;
  virtual ClassificationResult classify(const Point3&) const = 0;
  virtual DistanceResult distance_to(const Point3&) const = 0;
  virtual SectionResult intersect_with(const Plane&) const = 0;
};
```

### 表示转换器

- `MeshRep -> ExactBRep`：近似逆向，不保证完美恢复
- `ExactBRep -> MeshRep`：高质量三角化
- `ImplicitRep -> MeshRep`：提取等值面
- `ExactBRep -> ImplicitRep`：SDF 包装
- `HybridRep` 局部转换器

### 设计收益

- 后续能把点云、隐式晶格、网格修补都接进来
- CAD/CAE/AM 不再是割裂工具链

## 2.5 `OpsCore`

真正的建模操作层。

### 主要子模块

#### `PrimitiveOps`

- 盒、球、圆柱、圆锥、环面、楔体
- 平面草图轮廓成体
- 线框和基础曲面生成

#### `SweepOps`

- 拉伸
- 旋转
- 单导轨扫描
- 双导轨扫描
- 放样
- 加厚

#### `BooleanOps`

- 并
- 差
- 交
- 分割
- imprint / merge / trim

#### `ModifyOps`

- 偏置
- 抽壳
- 拔模
- 面替换
- 删除面补面
- 局部变形
- 直接编辑

#### `BlendOps`

- 倒角
- 常半径圆角
- 变半径圆角
- 角区求解

#### `QueryOps`

- 截面
- 求交
- 最短距离
- 最近点
- 质量属性
- 厚度分析
- 曲率分析

### 操作统一返回格式

所有操作不能只返回新实体，必须返回完整结果对象。

```cpp
struct OpResult {
  bool ok;
  BodyId output;
  DiagnosticReport report;
  RepairLog repairs;
  PerformanceStats perf;
  std::vector<Warning> warnings;
};
```

### 操作执行模式

- `DryRun`：只检查，不真正提交
- `Execute`：执行并提交
- `ExecuteWithDiagnostics`：执行并输出诊断证据
- `ExecuteWithRepair`：允许自动修复
- `Transactional`：失败不污染原模型

## 2.6 `HealCore`

很多内核把修复做成隐式黑箱，我建议单独做成一级模块。

### 修复器分类

#### 几何修复

- 曲线/曲面退化修复
- 参数域异常修复
- 法向异常修复
- 曲率爆点识别

#### 拓扑修复

- 小边合并
- 小面消除
- 环闭合修复
- 面缝合
- 重复边/孤立点清理

#### 语义修复

- 近共面面合并
- 近共线边合并
- 近重复特征归并
- 数据交换导入后的结构规整

### 验证器分类

- 几何有效性
- 拓扑一致性
- 容差合法性
- 流形性
- 自交检查
- 薄壁/高曲率风险
- 制造风险规则

### 修复输出

每次修复必须输出：

- 修复前问题
- 修复策略
- 修改实体
- 容差变化
- 修复后验证结果

### 修复策略模式

- `ReportOnly`
- `SuggestOnly`
- `AutoRepairSafe`
- `AutoRepairAggressive`

## 2.7 `EvalGraph`

这是现代内核的核心竞争力之一，决定你能不能做到增量更新和高性能。

### 核心能力

- 操作依赖图
- 几何求值缓存
- 交线缓存
- 三角化缓存
- 局部失效传播
- 多线程任务图执行
- 版本间差异定位

### 图节点类型

- 几何节点
- 拓扑节点
- 操作节点
- 缓存节点
- 可视化节点
- 分析节点

### 失效传播例子

如果修改一个草图尺寸：

- 只让受影响的轮廓边失效
- 由此传播到相关拉伸特征
- 再传播到受影响的布尔
- 最后传播到局部显示网格和质量属性
- 不触发整个模型完全重建

### 调度策略

- 任务图 `DAG`
- 并行执行无冲突节点
- 基于版本戳进行缓存命中
- 对重操作做分块任务化
- 提供取消和中断机制

### 缓存层次

- `L1`：求值点缓存
- `L2`：局部交线片段缓存
- `L3`：局部三角化缓存
- `L4`：操作级结果缓存

## 2.8 `PluginSDK`

内核要可扩展，不然几年后又会变成新一代大泥球。

### 插件种类

- 新曲线/曲面类型
- 新布尔策略
- 新修复器
- 新导入导出器
- 新分析器
- 新网格器
- 新求解器

### 插件边界原则

- 插件只能通过稳定 API 访问核心对象
- 默认读多写少
- 写操作必须走事务
- 插件崩溃不能损坏内核状态
- 插件结果必须可验证

### ABI 策略

- 内部核心可快速迭代
- 对外暴露 C 风格稳定边界，外面再包 C++/Rust/Python
- 版本兼容通过 capability negotiation

## 三、核心算法设计

## 3.1 布尔算法

这是几何内核最核心也最容易翻车的模块。

### 建议的流水线

1. `Broad Phase`
2. `Surface-Surface Intersection`
3. `Curve Trimming & Splitting`
4. `Cell Decomposition`
5. `Region Classification`
6. `Topology Reconstruction`
7. `Validation & Healing`

### 详细说明

#### `Broad Phase`

- `AABB Tree`
- `BVH`
- OBB 可选
- 先做面级候选过滤，再做曲面级精确检查

#### `Surface-Surface Intersection`

- 解析对解析优先走闭式或半闭式
- 样条对样条走自适应数值法
- 输出交线段及置信度
- 不确定区间打标记，供后续精确回退

#### `Curve Trimming & Splitting`

- 以交线切分原拓扑边/面
- 保留原几何引用和新子域映射
- 所有切分都带 parent-child 溯源

#### `Cell Decomposition`

- 将原体按交线与切分结果分解为可分类区域
- 每个 cell 有局部边界、局部几何、局部邻接

#### `Region Classification`

- 使用鲁棒 inside/outside 判定
- 优先双精度+区间
- 边界不确定点回退精确算法
- 输出分类证据，不只输出标签

#### `Topology Reconstruction`

- 重组 shell/body
- 清理重复边界
- 缝合容差显式记录
- 输出重建映射

### 布尔的设计目标

- 先保证“可解释地正确”
- 再做极限性能
- 失败时必须告诉用户是交线失败、分类失败、缝合失败还是自交失败

## 3.2 偏置与抽壳

### 难点

- 高曲率区域自交
- 小特征消失
- 壁厚不一致
- 偏置后拓扑变化

### 设计方案

- 局部法向偏置
- 自交检测器单独模块化
- 局部重构器负责补边和补面
- 提供两种策略：
  - `TopologyPreserving`
  - `ShapePreserving`

### 输出要求

- 哪些区域自交
- 哪些特征被删除
- 哪些区域需要局部补片
- 最终壁厚统计报告

## 3.3 圆角与倒角

### 阶段化实现

- 第一阶段：常半径边圆角
- 第二阶段：多边链连续圆角
- 第三阶段：变半径圆角
- 第四阶段：复杂角区解决

### 模块拆分

- 候选边识别器
- 滚球面/倒角面生成器
- 修剪求交器
- 角区补片求解器
- 质量检查器

### 圆角失败的可诊断性

必须能报告：

- 半径过大
- 邻面曲率不兼容
- 角区无解
- 局部自交
- 目标边拓扑不稳定

## 3.4 直接编辑

直接编辑是现代 CAD 非常重要的能力。

### 支持的编辑模式

- 平移面
- 偏移面
- 删除面补面
- 替换面
- 拉动特征面
- 局部重构

### 关键能力

- 局部拓扑重建
- 与历史特征弱耦合
- 对最终实体几何优先负责

## 四、数据结构设计

## 4.1 核心对象模型

### 几何句柄

- `CurveId`
- `SurfaceId`
- `PCurveId`
- `ImplicitFieldId`

### 拓扑句柄

- `VertexId`
- `EdgeId`
- `CoedgeId`
- `LoopId`
- `FaceId`
- `ShellId`
- `BodyId`

### 结果与缓存句柄

- `MeshId`
- `IntersectionId`
- `DiagnosticId`
- `VersionId`

### 属性系统

统一外置 attribute store：

- 命名
- 材料
- 颜色
- 制造属性
- 用户标签
- 来源追踪

不直接塞进核心拓扑对象，避免对象膨胀。

## 4.2 内存管理

### 策略

- 小对象池化
- 大块 arena
- 版本化对象用结构共享
- 热路径 SoA 优先，冷数据 AoS 可接受

### 为什么不用全 OO 深指针

- 缓存命中低
- 遍历慢
- 并行困难
- 序列化困难
- 差异比较困难

### 推荐布局

- `GeometryTable`：按类型分块
- `TopologyIncidenceTable`：连续索引数组
- `AttributeStore`：稀疏映射
- `CacheStore`：版本感知缓存

## 4.3 版本与事务

### 版本模型

- 每次提交生成 `VersionId`
- 未变对象结构共享
- 所有对象可追溯 parent version

### 事务模型

- 读事务：无锁快照
- 写事务：局部 copy-on-write
- 提交时做一致性检查
- 冲突时可回滚

### 好处

- 撤销重做天然支持
- 并行分析更安全
- 云端协同更容易扩展

## 五、API 设计

## 5.1 API 风格

原则：

- 显式输入输出
- 无隐式全局状态
- 失败可诊断
- 事务化
- 默认不可变，修改产生新版本

### 典型 API

```cpp
Kernel kernel(config);

BodyId a = kernel.primitives().box({0,0,0}, 100, 80, 30);
BodyId b = kernel.primitives().cylinder({20,20,0}, {0,0,1}, 10, 30);

OpResult r = kernel.booleans().subtract({
  .lhs = a,
  .rhs = b,
  .tolerance = AdaptiveTolerance::Default(),
  .diagnostics = true,
  .repair_mode = RepairMode::Safe
});
```

### 结果对象必须包含

- 输出对象 ID
- 诊断报告
- 警告
- 自动修复日志
- 性能统计
- 局部失败位置

## 5.2 语言绑定

建议内核实现：

- 核心用 `C++`
- 稳定 ABI 用 `C`
- 高级封装：
  - `Python`
  - `Rust`
  - `C#`

### 原因

- `C++` 便于高性能与模板优化
- `C ABI` 便于插件稳定和跨语言
- `Python` 便于算法验证和自动化建模
- `Rust` 便于外部工具和安全扩展

## 六、性能架构

## 6.1 CPU 策略

- 热路径 `SIMD`
- 空间索引连续内存布局
- 避免深层虚调用
- 面/边/交线级任务并行
- 细粒度锁最小化

## 6.2 并行模型

- 基于任务图而不是简单线程池
- 支持工作窃取
- 支持取消
- 支持优先级调度

## 6.3 GPU 策略

GPU 不建议先拿来做精确建模核心。
适合放在：

- 三角化后处理
- 大规模碰撞候选
- 可视化
- 距离场近似
- 网格后处理

精确布尔、拓扑重建、鲁棒分类仍主要在 CPU。

## 6.4 指标体系

每个操作记录：

- 总耗时
- 候选对数量
- 求交次数
- 回退到精确算法次数
- 缓存命中率
- 自动修复次数
- 输出实体复杂度变化

## 七、诊断与可观测性

这是区别“工程内核”和“黑盒库”的关键。

### 诊断对象

- `DiagnosticReport`
- `Issue`
- `Evidence`
- `RepairLog`
- `PerfStats`

### Issue 分类

- `NumericalInstability`
- `InvalidTopology`
- `SelfIntersection`
- `ToleranceConflict`
- `DegenerateGeometry`
- `ClassificationAmbiguous`
- `OperationUnsupported`

### Evidence 形式

- 3D 点/边/面定位
- 相关实体 ID
- 局部参数域范围
- 失败数值上下文
- 回退精度记录

### 用户价值

- 开发者能定位算法问题
- 上层 CAD 能高亮失败区域
- 自动测试能比较失败模式是否回归

## 八、数据交换与生态接口

## 8.1 文件格式

第一阶段优先：

- `STEP`
- `IGES`
- `BREP` 自定义原生格式
- `STL`
- `OBJ` 可选，仅网格桥接

第二阶段：

- `Parasolid XT` 若有合法授权桥接
- `3MF`
- `glTF` 用于显示流

## 8.2 交换原则

- 导入时保留来源元数据
- 导入时记录来源容差
- 导入后立即跑验证器
- 导出支持“严格模式”和“兼容模式”

## 九、测试体系

没有测试体系，几何内核迟早会失控。

## 9.1 测试分类

- 单元测试
- 属性测试
- 模型回归测试
- Fuzz 测试
- 数值边界测试
- 性能回归测试
- 数据交换互操作测试

## 9.2 特别重要的测试策略

### 属性测试

例如：

- 布尔交换律在特定条件下成立
- 几何变换前后拓扑等价
- 细分三角化不改变体积符号
- 近似转换前后误差受控

### Fuzz 测试

随机生成：

- 小边
- 薄壁
- 极小角
- 近切触
- 近重合面
- 高曲率样条面

这些正是实际最容易把内核搞崩的输入。

### 黄金模型库

建立专门模型集：

- 机械零件
- 曲面件
- 薄壁件
- 导入脏模型
- 失败案例归档

## 十、最小可用版本 `MVP`

如果马上开做，我建议 `MVP` 只做以下范围。

### `MVP` 功能

- `ExactBRep`
- 解析几何 + 基础 `NURBS`
- 顶点/边/面/壳/体
- 拉伸、旋转、扫描基础版
- 并、差、交
- 基础倒角、常半径圆角
- 截面、质量属性、最近点
- STEP 导入导出
- 验证器和修复器基础版
- 诊断报告系统
- 三角化缓存
- 事务与版本系统

### `MVP` 不做

- 高级角区圆角
- 全隐式建模
- 点云逆向重建
- 云端协同
- 全套参数化历史树

## 十一、推荐技术选型

### 实现语言

- 核心：`C++20/23`
- ABI：`C`
- 工具脚本：`Python`
- 测试编排：`Python + CTest` 或同类方案

### 第三方可借鉴但不宜深耦合替代的部分

- 数学库可参考 `Eigen` 风格，但热路径建议自控
- 空间索引可参考现代 `BVH`
- 多精度可引入成熟库，但只做 fallback
- 文件交换可先自研关键路径，再考虑兼容桥接

## 十二、研发组织建议

建议至少按下面分工拆小组：

- `数学与鲁棒性组`
- `几何求值组`
- `拓扑与事务组`
- `布尔与修复组`
- `数据交换与测试组`
- `性能与缓存组`

最危险的组织错误是：

- 一个小组全包所有几何算法
- 没有专职测试和失败案例管理
- 先做 UI，再补内核正确性

## 十三、最关键的风险点

- 布尔和偏置的鲁棒性会吞掉远超预期的时间
- 容差系统如果设计草率，后面全内核都会被污染
- 如果几何和拓扑没彻底分离，后面无法优雅支持混合表示
- 如果没有版本与事务层，后面很难做好增量重算
- 如果诊断系统不是一开始就做，后面调试成本会指数上升

## 十四、最终架构摘要

一句话概括这个新内核：

**它是一个以 `精确 B-Rep` 为核心、以 `自适应鲁棒数值` 为基础、以 `几何/拓扑/操作/修复/缓存` 分层为结构、以 `事务化 + 诊断化 + 混合表示` 为特征的现代几何引擎。**

你如果要继续往下推进，下一步最合适的是这两个方向之一：

1. 我直接继续给你写 `详细模块接口清单`，把每个模块拆到类、接口、输入输出级别。
2. 我给你写 `MVP 实施蓝图`，按 3 个月、6 个月、12 个月拆成可执行研发计划。
