---
name: axiom-rep-dev
description: Develop and harden AxiomKernel RepCore (industrial tessellation/triangulation, mesh inspection & reporting, caching, high-quality representation conversion, round-trip verification and error budgets) aligned with AGENTS.md and docs/plan progress. Use when implementing BRep->Mesh / Mesh->BRep / Implicit->Mesh, adding curvature-sensitive subdivision, local re-tessellation, mesh stats/quality metrics, conversion tolerances, or updating rep/io workflow tests.
---

# AxiomKernel Rep 开发（triangulation & conversion）

## 适用范围与目标

本 skill 用于在本仓库内推进 `rep` 模块到“可回归、可观测、可扩展”的工业化雏形：
- 工业级三角化：曲率敏感细分、局部重三角化（patch/局部 dirty）、缓存与统计报告
- 高质量表示转换：误差预算（budget）与 round-trip 验证闭环

约束：
- 保持全量 `ctest` 通过
- 所有失败路径必须返回明确 `StatusCode` 并携带诊断码（diagnostics）
- 不引入过度复杂的几何算法；先把接口形状、可观测性与测试闭环做稳

## 代码入口（必须先读懂）

- `rep` 主服务
  - `include/axiom/rep/representation_conversion_service.h`
  - `src/rep/representation_conversion_service.cpp`
  - `include/axiom/rep/representation_conversion_service.h`（RepresentationService/ConversionService）
- `rep` 内部工具（网格检查、三角化帮助函数、primitive tessellation）
  - `include/axiom/internal/rep/representation_internal_utils.h`
  - `src/rep/representation_internal_utils.cpp`
- `KernelState`（网格存储、缓存存储、诊断存储）
  - `include/axiom/internal/core/kernel_state.h`
  - `src/sdk/kernel.cpp`（清理/重置 runtime stores）
- IO 对 `rep` 的依赖（glTF/STL 导出会调用 `brep_to_mesh`）
  - `src/io/io_service.cpp`
- 关键回归测试
  - `tests/rep/representation_io_test.cpp`
  - 视改动范围可能影响 `tests/io/io_workflow_test.cpp`（glTF 输出）

## 工业级三角化：实现策略（默认路线）

### 1) 参数校验与容差映射（不可跳过）
- 任何入口（`brep_to_mesh / implicit_to_mesh / batch`）先调用 `has_valid_tessellation_options()`
- 对来自 `KernelConfig.tolerance` 的容差策略：**只用于查询/验证**，不要隐式改变用户指定的 `TessellationOptions`

### 2) 曲率敏感细分（优先从 primitive 落地）
先实现对 `BodyKind::{Box,Sphere,Cylinder,Cone,Torus}` 的解析 tessellation：
- 球/柱/锥/环：分段数必须同时受以下两条约束：
  - **chordal error**：弦高误差 \(e\)
  - **angular error**：法向变化（度）
- 盒体：允许用 chordal error 控制网格密度（用于可视化/一致性），但不要破坏连通性（connected components 应为 1）

如果无法解析（派生体/占位体）：
- 回退为 bbox proxy 网格，但要在 label 或报告里能区分（例如 `mesh_from_brep_bbox_proxy`）

### 3) 局部重三角化（patch）— 当前阶段的落地方式
如果公开 API 暂无“局部输入”（例如 FaceId 集合、AABB、dirty set）：
- 先确保**缓存键**正确可用（相同体+相同 options 必命中）
- 在 internal utils 里保留“按 patch 生成”的函数形状（可先以 primitive 参数域 patch 为单位）
- 任何 patch 思路必须能解释如何拼接（weld）、如何处理法向/uv seam、如何保证 index 合法

### 4) 缓存（必须做到可控、可清理、可观测）
- 缓存 key：稳定串行化 `BodyRecord` 几何参数 + bbox + options（不是 hash 也可以）
- 缓存命中返回必须校验 mesh 记录仍存在；避免 stale key
- `Kernel::clear_mesh_store()` 和 `Kernel::reset_runtime_stores()` 必须同时清空 tessellation cache

### 5) 网格质量检查与统计报告
至少输出：
- vertex/index/triangle count
- connected components
- out-of-range indices / degenerate triangles
- bbox、是否有 normals/texcoords
- min/max edge length、min/max triangle area、degenerate_triangle_count

导出 JSON 时：
- 字段保持向后兼容（旧字段不删）
- 新字段可选，但要有回归测试断言关键字段存在

## 表示转换与误差控制：round-trip 验证闭环

### 1) 误差预算（budget）要显式化
- 建议预算从 `TessellationOptions` 派生（线性容差、角度容差），并写入 `RoundTripReport`
- 默认 budget 以“工程可回归”为优先，不追求严格几何最优

### 2) round-trip 验证的最低要求
实现两条链路并可测试：
- `BRep -> Mesh -> BRep`：至少 bbox 保真 +（对 primitive）点偏差评估
- `Mesh -> BRep -> Mesh`：至少 bbox 保真（未来可扩展 Hausdorff/采样距离）

输出 `RoundTripReport`：
- `passed` 必须由 budget 自动判定
- 失败必须返回诊断（InvalidInput / OperationFailed / DegenerateGeometry 等）

## 测试与验收（每次改动必做）

### 必须更新/新增测试的场景
- 三角化密度策略变化（coarse/fine 行为）
- 新增/修改缓存逻辑（重复调用应命中缓存）
- JSON 报告字段变更（至少断言核心字段存在）
- round-trip/budget 改动（`passed` 的期望需稳定）

### 最低测试清单（建议）
- `representation_io_test` 覆盖：
  - `brep_to_mesh` 误差参数非法输入
  - coarse/fine 网格密度单调性
  - `inspect_mesh` / `export_mesh_report_json` 内容校验
  - `verify_brep_mesh_round_trip` / `verify_mesh_brep_round_trip` 通过
- `io_workflow_test`：glTF/STL 仍能导出（因为依赖 `brep_to_mesh`）

## 常见坑（避免）
- 不要把“bbox proxy 网格”当成最终工业级实现；必须能在 label/报告/diagnostic 上区分
- 不要生成 out-of-range index、不要生成 NaN/Inf 顶点
- 不要让 box 的三角化破坏连通性（connected components 变多会影响上层）
- 不要把 `KernelConfig.tolerance` 偷偷混入 tessellation options（导致用户无法预测密度）

