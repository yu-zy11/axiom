# AxiomKernel 基准数据集与性能管理规范

本文档定义 `AxiomKernel` 的基准数据集分类、性能指标、回归方式和记录规范，目标是确保性能优化有统一度量，不会变成“感觉快了”。

## 1. 文档目标

本文档用于明确：

- 需要哪些性能基准
- 如何组织基准数据集
- 如何记录性能结果
- 什么算性能回退

### 1.1 cycle-0083 / S4-EXIT 性能证据边界

[本批门禁](../../.axiom-agent/logs/cycle-0083-gates.log)记录完整配置/并发4构建成功，CTest **16/16、0失败、194.58 s**，其中 `axiom_perf_baseline_test` **2.03 s，通过**。性能源码及CMake本批无改动，默认 `AXM_PERF_ITERATIONS=150`、`AXM_PERF_MAX_MS=4000`、CTest超时30 s不变；日志未记录覆盖环境变量或内部elapsed_ms，2.03 s是CTest墙钟，不是P95/峰值内存或跨环境保证。

实际基线循环构造box/cylinder，调用兼容 `BooleanService::run(Subtract)` 与质量查询；允许既有OperationFailed及代理质量NotImplemented诊断合同，成功代理后另检查原box质量。**该基线不调用run_rebuilt，不认证真实重建的工业性能**。本批偏移盒U/D/I总计两轮及独立OBJ三角V/A是正确性/重复稳定性证据，不是工业压力或性能数据集。固定支持域和剩余限制见 [API退出矩阵](../api/AxiomKernel_详细模块接口清单.md#824-stage-4-退出支持矩阵cycle-0083--s4-exit)，三项验收见 [验收§1.10](AxiomKernel_测试与验收方案.md#110-cycle-0083--s4-exit-门禁与逐项证据)。本轮仅同步文档，未重跑性能测试、提高阈值、减少迭代或清缓存。

## 2. 性能测试原则

- 优先建立稳定基线
- 同一模型、同一配置、同一环境下对比
- 不只看平均值，也看波动和尾部
- 不牺牲正确性换取无意义性能数字

## 3. 数据集分类

建议维护：

- 基础体数据集
- 机械零件数据集
- 曲面件数据集
- 布尔压力数据集
- 导入导出数据集
- 三角化数据集

### 3.1 cycle-0085 / S5-IO 固定语料与独立参考

本批仅编辑 docs，固定数据说明集中于本节，`tests/data/io/README.md` 保持不变。固定输入与测试代码已随调度器完整 CTest **16/16、0 失败、227.56 s** 通过；必需 IO workflow/dataset/representation_io **14.77/0.70/12.23 s**。本轮未重跑测试，逐条断言及阶段限制见 [验收 §1.12](AxiomKernel_测试与验收方案.md#112-cycle-0085--s5-io-门禁与逐项证据)。

| 固定输入 | 语义与独立参考 |
|---|---|
| [s5_io_precision_subset.step](../../tests/data/io/s5_io_precision_subset.step)、[.iges](../../tests/data/io/s5_io_precision_subset.iges)、[.brep](../../tests/data/io/s5_io_precision_subset.brep) | Axiom 元数据 Box，origin=(123456.123456789,-1.2345678901234567,3.456789012345679)，params=(2.345678901234568,3.456789012345679,4.567890123456789)；独立文本解析 origin/params/bbox double 逐项精确相等，逗号 locale 下导出/恢复；ExactBRep 标签但零 owned shells，不是标准实体交换 |
| [s5_io_precision_tetra.stl](../../tests/data/io/s5_io_precision_tetra.stl) | 模型单位下平移四面体，三轴长度2/3/4；4三角形、12 facet 顶点（36 坐标量），独立解析精确比较；实际三角积分 V=4、A=13+sqrt(244)/2、C=origin+(0.5,0.75,1)，绝对误差1e-12，bbox体积24不能替代；MeshRep/io_import_stl，零 owned shells |
| [standard_step_express_stub.step](../../tests/data/io/standard_step_express_stub.step)、[standard_iges_deck_stub.iges](../../tests/data/io/standard_iges_deck_stub.iges) | 标准物理形态拒绝固定回归；根因0010/0011与扫描D-0016/0017；混入Axiom标记仍拒绝，不视容器/标记为实体交换 |

workflow 动态最小失败语料包括四格式 64 MiB+1 sparse 文件、STEP 空/随机/字段损坏/NaN、STL 未闭合/尾垃圾、134字节binary NaN、1e200面积溢出，核对稳定阶段和模型状态隔离/重试。旧主文件/侧车失败/设备写入与冷暖缓存回归保留；publish 失败语义有实现检查，未直接注入。坐标保留模型单位，未验证单位转换；ASCII double 往返不推广为 binary STL float32 无损或所有工业文件精度保证。

cycle-0085 性能基线 **2.00 s** 仅为既有 CTest 墙钟，默认150次/4000 ms及30 s超时未修改；无新增IO专用总耗时/P95/内存基准，不将精确语料回归视为工业交换性能认证。

## 4. 核心指标

建议记录：

- 总耗时
- 峰值内存
- 平均耗时
- P95 耗时
- 错误率
- 警告率
- 回退精度触发次数

## 5. 性能回退判断建议

建议：

- 关键路径出现显著回退时触发告警
- 连续多个版本变慢时必须进入专项分析

## 6. 基准场景建议

至少覆盖：

- 盒体、圆柱体等基础构造
- 盒减柱等基础布尔
- 中等复杂零件导入导出
- 曲面件三角化
- 体积与质量属性计算

## 7. 结果记录格式建议

建议采用结构化格式，例如 `JSON`：

```json
{
  "case": "boolean_box_minus_cylinder",
  "build": "0.1.0",
  "platform": "linux-gcc",
  "timeMs": 12.4,
  "memoryMb": 18.7,
  "warnings": 1
}
```

## 8. 结论

性能管理最怕没有基线和没有统一记录。只要数据集、指标和记录格式稳定，后续优化才有真正比较价值。
