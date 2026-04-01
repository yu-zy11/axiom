# AxiomKernel（axiom）

一个分层的几何/拓扑内核雏形工程，强调**模块边界**与**统一诊断链路**：对外接口使用结构化结果（如 `axiom::Result<T>`），失败可追踪、可导出。

## 快速开始

### 构建（推荐）

```bash
cmake -S . -B build -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON
cmake --build build
ctest --test-dir build
```

### 清理重建

```bash
rm -rf build
cmake -S . -B build -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON
cmake --build build
ctest --test-dir build
```

### 运行示例

当开启 `AXM_ENABLE_EXAMPLES=ON` 时会生成示例程序：

```bash
./build/axiom_basic_workflow
```

## CMake 选项

- `AXM_ENABLE_TESTS`: 构建测试（默认 ON）
- `AXM_ENABLE_EXAMPLES`: 构建示例（默认 ON）
- `AXM_ENABLE_BENCHMARKS`: 构建基准（默认 OFF）
- `AXM_ENABLE_DIAGNOSTICS`: 启用诊断能力（默认 ON）
- `AXM_ENABLE_STRICT_WARNINGS`: 开启严格编译告警（默认 ON）

## 目录结构

- `include/axiom/`: 对外公开头文件（Public API）
- `src/axiom/`: 模块实现真源
- `src/axiom/internal/`: 模块私有 internal 头与实现分片，不安装、不对外暴露
- `tests/`: 自动化测试（按模块分子目录，如 `tests/geo/`、`tests/ops/`）
- `cmake/`: 构建片段（如 `AxiomKernelLibraries.cmake` 定义各模块静态库）
- `examples/`: 示例程序
- `docs/`: 设计/接口/质量/诊断等文档

当前文档与结构治理约定要求：

- 公开 API 以 `include/axiom/**` 为准
- 实现路径以 `src/axiom/**` 为准
- 构建与模块依赖以 `cmake/AxiomKernelLibraries.cmake` 为准
- `ctest` 清单以根目录 `CMakeLists.txt` 为准

## 构建产物（模块化 targets）

当前仓库已按模块拆分为多个 CMake target（如 `axiom_math/axiom_geo/axiom_topo/...`），并保留 `axiom_kernel` 作为聚合入口（供示例与测试链接）。

## 诊断与错误处理约定（摘要）

- **可失败的对外操作**应返回结构化结果（如 `axiom::Result<T>`），而不是裸 `bool`
- 错误码定义见 `include/axiom/diag/error_codes.h`（命名空间 `axiom::diag_codes`）
- 诊断服务见 `axiom::DiagnosticService`，支持导出文本/JSON 报告

## 文档索引

建议阅读顺序：

1. `docs/README.md`
2. `docs/plan/AxiomKernel_当前开发进度.md`
3. `docs/architecture/AxiomKernel_项目结构与文档治理建议.md`
4. `docs/plan/AxiomKernel_主开发计划与阶段路线图.md`
5. `AGENTS.md`

