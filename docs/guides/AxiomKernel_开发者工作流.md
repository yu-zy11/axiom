# AxiomKernel 开发者工作流

> 状态：已接受
> 责任域：Project
> 维护者：项目负责人
> 最后核验：2026-09-18
> 核验依据：`CMakeLists.txt`、`AGENTS.md`

## 1. 首次构建

要求 CMake 3.20+、支持 C++20 的编译器和 CTest。推荐使用独立构建目录：

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAXM_ENABLE_TESTS=ON \
  -DAXM_ENABLE_EXAMPLES=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

可选开关及默认值以根 `CMakeLists.txt` 为准：`AXM_ENABLE_TESTS`、`AXM_ENABLE_EXAMPLES`、`AXM_ENABLE_BENCHMARKS`、`AXM_ENABLE_DIAGNOSTICS`、`AXM_ENABLE_STRICT_WARNINGS`。

## 2. 从需求到 MR

1. 在[需求追踪矩阵](../requirements/AxiomKernel_需求追踪矩阵.md)确认需求 ID、责任模块和验收点。
2. 阅读模块 Public API、架构依赖和相邻测试；不要从长远目标推断当前实现。
3. 先定义成功、失败、退化和回滚场景，再修改实现。
4. Public API 使用 `Result<T>`；新增根因先复用错误码，确需新增时同步诊断字典与测试。
5. 修改型操作保证失败不污染；用验证器或不变量测试证明。
6. 运行最小相关测试，再运行完整测试集。
7. 按[变更影响速查](../README.md#5-变更影响速查)更新文档并填写 MR 模板。

## 3. 测试选择

```bash
# 单模块快速反馈
ctest --test-dir build -R axiom_geometry_test --output-on-failure

# 跨模块子集
ctest --test-dir build -R 'axiom_(geometry|topology)_test' --output-on-failure

# 完整门禁
ctest --test-dir build --output-on-failure
```

改动热路径时使用 Release 或 RelWithDebInfo 构建，并记录编译器、机器、数据集、迭代次数和阈值。性能测试可通过 `AXM_PERF_MAX_MS`、`AXM_PERF_ITERATIONS` 配置，但不能通过放宽阈值隐藏回退。

## 4. 文档检查

提交前至少检查相对 Markdown 链接和重复标题：

```bash
python3 scripts/check_docs.py
```

文档中的代码若对应可运行 API，应优先把代码放进 `examples/` 或测试，由编译器持续验证。概念性伪代码必须显式标注“示意”。

## 5. 何时写 ADR

满足任一条件就使用 `docs/decisions/` 的模板：

- 改变模块边界或依赖方向；
- 改变 Public API/ABI、数据持久化或格式兼容策略；
- 引入长期依赖、并发模型、内存模型或容差模型；
- 决策影响多个模块且回退成本高；
- 团队反复讨论同一取舍，需要保存原因。

局部实现细节、易逆转重构和单一缺陷修复通常不需要 ADR。

## 6. 提交前清单

- [ ] 需求 ID 和验收条件明确。
- [ ] 依赖方向符合架构。
- [ ] 成功、失败和回滚路径有自动化测试。
- [ ] API、错误码、样例、追踪矩阵按影响同步。
- [ ] `python3 scripts/check_docs.py` 通过。
- [ ] `ctest --test-dir build --output-on-failure` 通过。
- [ ] MR 写明风险、限制和未覆盖项。
