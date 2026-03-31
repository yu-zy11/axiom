# AxiomKernel 模块依赖图与时序图

本文档定义 `AxiomKernel` 的模块依赖关系、调用边界和典型操作时序，目标是帮助研发团队明确模块耦合方式、帮助架构评审识别不合理依赖，并帮助上层调用方理解内核内部的数据流和控制流。

## 1. 文档目标

本文档用于明确：

- 模块间依赖关系
- 模块调用方向
- 禁止耦合关系
- 典型操作时序
- 上层系统接入位置

## 2. 核心模块列表

`AxiomKernel` 的核心模块如下：

- `MathCore`
- `GeoCore`
- `TopoCore`
- `RepCore`
- `OpsCore`
- `HealCore`
- `EvalGraph`
- `Diagnostics`
- `IO`
- `PluginSDK`
- `Kernel Facade`

## 3. 总体依赖原则

### 3.1 基本原则

- 下层模块不能反向依赖上层模块
- 几何层与拓扑层分离，但允许拓扑层引用几何句柄
- 修复、验证、布尔等复杂操作必须通过服务层组合，不得倒灌到基础层
- `IO` 模块不应成为几何和拓扑的前置依赖
- 插件只能依赖公开稳定接口，不应直接依赖内部私有实现

### 3.2 允许依赖

- `GeoCore -> MathCore`
- `TopoCore -> GeoCore`
- `RepCore -> GeoCore + TopoCore`
- `OpsCore -> MathCore + GeoCore + TopoCore + RepCore + Diagnostics`
- `HealCore -> MathCore + GeoCore + TopoCore + RepCore + Diagnostics`
- `EvalGraph -> Core Handles + Diagnostics`
- `IO -> RepCore + GeoCore + TopoCore + Diagnostics`
- `PluginSDK -> Kernel Facade / Public API`

### 3.3 禁止依赖

- `MathCore -> 任何上层模块`
- `GeoCore -> TopoCore`
- `TopoCore -> OpsCore`
- `TopoCore -> IO`
- `Diagnostics -> 业务算法实现`
- `PluginSDK -> 内部私有数据结构`

## 4. 文本依赖图

下面给出建议的分层依赖图：

```text
                +-------------------+
                |   Kernel Facade   |
                +---------+---------+
                          |
     +--------------------+--------------------+
     |                    |                    |
     v                    v                    v
+-----------+      +-------------+      +-------------+
|  OpsCore  |      |  HealCore   |      |     IO      |
+-----+-----+      +------+------+      +------+------+
      |                   |                    |
      +---------+---------+---------+----------+
                |                   |
                v                   v
          +-----------+       +-----------+
          |  RepCore  |       | EvalGraph |
          +-----+-----+       +-----+-----+
                |                   |
          +-----+-----+             |
          | TopoCore  |             |
          +-----+-----+             |
                |                   |
          +-----+-----+             |
          | GeoCore   |             |
          +-----+-----+             |
                |                   |
                v                   v
             +--------+       +-----------+
             |MathCore|       |Diagnostics|
             +--------+       +-----------+
```

说明：

- `Diagnostics` 是横切能力，但不应该持有算法逻辑。
- `EvalGraph` 更多负责缓存、依赖和失效传播，不应侵入几何和拓扑内部实现。
- `Kernel Facade` 仅作为聚合入口，不承担算法逻辑。

## 5. 模块职责与边界

## 5.1 `MathCore`

职责：

- 基础向量矩阵
- 变换
- 鲁棒谓词
- 公差策略底座

不负责：

- 实体建模
- 拓扑结构
- 数据交换

输入：

- 标量和基础坐标对象

输出：

- 数值结果
- 谓词结果
- 公差策略对象

## 5.2 `GeoCore`

职责：

- 曲线曲面表示
- 参数求值
- 最近点
- 参数反求

不负责：

- 拓扑关系维护
- 布尔重建
- 修复流程管理

依赖：

- `MathCore`

## 5.3 `TopoCore`

职责：

- 顶点、边、环、面、壳、体结构
- 拓扑关系维护
- 事务和局部结构修改

不负责：

- 曲线曲面求值实现
- 布尔算法
- 文件导入导出

依赖：

- `GeoCore` 句柄和接口

## 5.4 `RepCore`

职责：

- 管理 `ExactBRep`、`MeshRep`、`ImplicitRep`
- 表示转换和统一查询

不负责：

- 高层产品逻辑
- 草图与历史树

依赖：

- `GeoCore`
- `TopoCore`

## 5.5 `OpsCore`

职责：

- 基础建模
- 布尔
- 修改
- 圆角倒角
- 查询分析

不负责：

- 文件格式解析
- 插件生命周期管理

依赖：

- `MathCore`
- `GeoCore`
- `TopoCore`
- `RepCore`
- `Diagnostics`

## 5.6 `HealCore`

职责：

- 验证
- 小边小面修复
- 缝合
- 自交检查

依赖：

- `MathCore`
- `GeoCore`
- `TopoCore`
- `RepCore`
- `Diagnostics`

## 5.7 `EvalGraph`

职责：

- 依赖图
- 缓存
- 失效传播
- 局部重算调度

依赖：

- 核心句柄
- `Diagnostics`

注意：

- `EvalGraph` 不能替代事务系统
- `EvalGraph` 不能直接写几何/拓扑底层存储

## 5.8 `IO`

职责：

- 导入导出
- 格式映射
- 数据源元信息保留

依赖：

- `GeoCore`
- `TopoCore`
- `RepCore`
- `Diagnostics`

## 5.9 `PluginSDK`

职责：

- 插件发现
- 插件注册
- 插件能力暴露

依赖：

- 仅依赖公开接口层

## 5.10 `Kernel Facade`

职责：

- 对外提供统一入口
- 聚合各个服务

不负责：

- 具体算法实现
- 内部状态重计算逻辑

## 6. 典型调用路径

## 6.1 创建基础体

```text
Client
  -> Kernel Facade
  -> PrimitiveService
  -> GeoCore
  -> TopoCore
  -> ValidationService
  -> Diagnostics
  -> Result
```

说明：

- 基础体构造应先生成几何对象，再生成拓扑实体，最后做轻量验证。

## 6.2 布尔操作

```text
Client
  -> Kernel Facade
  -> BooleanService
  -> RepCore
  -> TopologyQueryService
  -> GeoCore / MathCore
  -> TopologyTransaction
  -> ValidationService
  -> RepairService(Optional)
  -> Diagnostics
  -> Result
```

说明：

- 布尔是跨模块协同最复杂的路径。
- `BooleanService` 应是 orchestrator，不应把所有实现细节散落到多个无主模块里。

## 6.3 导入 `STEP`

```text
Client
  -> Kernel Facade
  -> ImportService
  -> IO Parser
  -> GeoCore
  -> TopoCore
  -> RepCore
  -> ValidationService
  -> RepairService(Optional)
  -> Diagnostics
  -> Result
```

说明：

- 导入后的自动验证是必须的。
- 修复是否执行取决于导入策略。

## 6.4 三角化

```text
Client
  -> Kernel Facade
  -> RepresentationConversionService
  -> RepCore
  -> GeoCore
  -> TessellationService
  -> CacheService(Optional)
  -> Diagnostics
  -> MeshResult
```

## 7. 典型时序图

以下时序图使用纯文本形式表达。

## 7.1 基础体创建时序图

```text
Client           Kernel        Primitive      GeoCore      TopoCore      Validate      Diag
  |                |               |             |             |             |           |
  | createBox()    |               |             |             |             |           |
  |--------------->|               |             |             |             |           |
  |                | box()         |             |             |             |           |
  |                |-------------->|             |             |             |           |
  |                |               | buildSurf   |             |             |           |
  |                |               |-----------> |             |             |           |
  |                |               | buildTopo                 |             |           |
  |                |               |-------------------------> |             |           |
  |                |               | validate                                |           |
  |                |               |---------------------------------------> |           |
  |                |               | diag emit                                            |
  |                |               |----------------------------------------------------> |
  |                | <-------------------------- result ----------------------------------|
  | <------------------------------------------- result ----------------------------------|
```

## 7.2 布尔差集时序图

```text
Client         Kernel       BooleanSvc     Rep/Topo     Geo/Math     Txn       Validate   Repair   Diag
  |              |              |             |            |           |           |         |       |
  | subtract()   |              |             |            |           |           |         |       |
  |------------->|              |             |            |           |           |         |       |
  |              |------------->|             |            |           |           |         |       |
  |              |              | precheck    |            |           |           |         |       |
  |              |              |-----------> |            |           |           |         |       |
  |              |              | intersect                |           |           |         |       |
  |              |              |-------------------------> |           |           |         |       |
  |              |              | rebuild topo                                      |       |       |
  |              |              |--------------------------------------> |           |       |       |
  |              |              | validate result                                    |       |       |
  |              |              |--------------------------------------------------->|       |       |
  |              |              | repair?                                                     |       |
  |              |              |------------------------------------------------------------->|       |
  |              |              | diag log                                                            |
  |              |              |-------------------------------------------------------------------->|
  |              |<---------------------------------------------- report/result ----------------------|
  |<------------------------------------------------------------- report/result ----------------------|
```

## 7.3 导入模型时序图

```text
Client         Kernel        ImportSvc       IOParser      GeoCore      TopoCore      Validate    Repair    Diag
  |              |               |              |             |             |             |         |        |
  | import()     |               |              |             |             |             |         |        |
  |------------->|               |              |             |             |             |         |        |
  |              |-------------->|              |             |             |             |         |        |
  |              |               | parse file   |             |             |             |         |        |
  |              |               |------------->|             |             |             |         |        |
  |              |               | map geom                   |             |             |         |        |
  |              |               |--------------------------->|             |             |         |        |
  |              |               | map topo                                 |             |         |        |
  |              |               |----------------------------------------->|             |         |        |
  |              |               | validate imported                                      |         |        |
  |              |               |------------------------------------------------------->|         |        |
  |              |               | optional repair                                                   |        |
  |              |               |------------------------------------------------------------------>|        |
  |              |               | diag emit                                                                   |
  |              |               |------------------------------------------------------------------------->   |
  |              |<----------------------------------------------- import result -----------------------------|
  |<-------------------------------------------------------------- import result -----------------------------|
```

## 7.4 局部修改后的增量更新时序图

```text
Client        Kernel       ModifySvc      Txn        EvalGraph      Cache       Validate      Diag
  |             |             |            |            |             |            |          |
  | editFace()  |             |            |            |             |            |          |
  |------------>|             |            |            |             |            |          |
  |             |-----------> |            |            |             |            |          |
  |             |             | open txn   |            |             |            |          |
  |             |             |----------->|            |             |            |          |
  |             |             | update topo/geom        |             |            |          |
  |             |             |----------->|            |             |            |          |
  |             |             | invalidate nodes                     |             |          |
  |             |             |------------------------>|             |            |          |
  |             |             | cache reload                         |<------------|          |
  |             |             |------------------------>|-------------|            |          |
  |             |             | validate                                               |       |
  |             |             |------------------------------------------------------->|       |
  |             |             | diag emit                                                        |
  |             |             |----------------------------------------------------------------->|
  |             |<----------------------------------- updated result -----------------------------|
  |<----------------------------------------------- updated result -------------------------------|
```

## 8. 关键时序的详细说明

## 8.1 布尔时序的关键节点

布尔操作至少应显式经过以下阶段：

1. 输入合法性检查
2. 表示检查与归一化
3. 候选相交对生成
4. 几何求交
5. 局部切分
6. 分类
7. 拓扑重建
8. 验证
9. 可选修复
10. 诊断输出

不建议：

- 直接把求交、分类、重建混成一个大函数
- 在底层拓扑模块里偷偷做自动修复

## 8.2 导入流程的关键节点

导入流程至少应包括：

1. 文件打开与格式识别
2. 实体级解析
3. 几何对象构造
4. 拓扑对象构造
5. 来源容差记录
6. 导入后验证
7. 可选修复
8. 结果报告

## 8.3 局部修改流程的关键节点

局部修改流程至少应包括：

1. 打开写事务
2. 应用局部几何或拓扑修改
3. 标记相关节点失效
4. 使用缓存做局部重算
5. 验证结果
6. 提交事务
7. 输出新版本

## 9. 模块依赖约束清单

为了防止架构腐化，建议在代码评审中明确检查以下约束。

### 9.1 强约束

- `MathCore` 禁止依赖 `GeoCore` 之外任何建模语义
- `GeoCore` 禁止依赖 `TopoCore`
- `TopoCore` 禁止依赖 `IO`
- `Diagnostics` 禁止持有模型修改能力
- `PluginSDK` 禁止访问私有内存布局

### 9.2 弱约束

- `OpsCore` 尽量通过服务组合而非直接操作底层存储
- `HealCore` 尽量复用验证器和事务接口
- `IO` 尽量避免了解过多布尔和修复实现细节

## 10. 建议的源码目录映射

建议目录结构如下：

```text
axiom/
  core/
  math/
  geo/
  topo/
  rep/
  ops/
  heal/
  eval/
  diag/
  io/
  plugin/
  sdk/
```

建议映射关系：

- `core/`：句柄、结果对象、基础类型
- `math/`：谓词、数值、公差
- `geo/`：曲线曲面和求值
- `topo/`：拓扑实体和事务
- `rep/`：表示与转换
- `ops/`：构造、布尔、修改、查询
- `heal/`：修复和验证
- `eval/`：依赖图、缓存、增量
- `diag/`：错误码、诊断、日志
- `io/`：格式导入导出
- `plugin/`：插件注册与适配
- `sdk/`：门面和语言绑定

## 11. 架构评审检查表

每次架构评审建议检查以下问题：

- 新模块是否依赖了不该依赖的层
- 新接口是否把事务和诊断遗漏了
- 是否把实现细节泄漏到 SDK 层
- 是否把修复逻辑放进了基础几何或拓扑层
- 是否存在双向依赖或隐式全局状态

## 12. 对上层产品的接入建议

上层 CAD 或脚本系统建议只接入：

- `Kernel Facade`
- `C API`
- `Python Binding`

不建议：

- 直接调用底层私有模块
- 直接修改拓扑内部存储
- 直接构造未校验对象

## 13. 结论

这份文档的核心价值在于把“模块怎么分”和“调用怎么走”明确下来，避免后期架构退化成互相穿透的大泥球。

一句话总结：

- `MathCore` 负责算
- `GeoCore` 负责形
- `TopoCore` 负责结构
- `RepCore` 负责表示统一
- `OpsCore` 负责操作编排
- `HealCore` 负责验和修
- `EvalGraph` 负责增量和缓存
- `Diagnostics` 负责可观测性
- `IO` 负责进出
- `Kernel Facade` 负责对外

后续如果继续补齐，最适合追加的是：

1. `docs/api/AxiomKernel_接口调用样例集.md`
2. `docs/diagnostics/AxiomKernel_用户可读错误文案映射表.md`
