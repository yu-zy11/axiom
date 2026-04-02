#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace axiom {

using Scalar = double;
using Index = std::uint32_t;
using VersionId = std::uint64_t;

struct Point2 {
  Scalar x{};
  Scalar y{};
};

struct Point3 {
  Scalar x{};
  Scalar y{};
  Scalar z{};
};

struct Vec2 {
  Scalar x{};
  Scalar y{};
};

struct Vec3 {
  Scalar x{};
  Scalar y{};
  Scalar z{};
};

struct Range1D {
  Scalar min{};
  Scalar max{};
};

struct Range2D {
  Range1D u{};
  Range1D v{};
};

struct BoundingBox {
  Point3 min{};
  Point3 max{};
  bool is_valid{false};
};

struct Transform3 {
  std::array<Scalar, 16> m{1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
                           0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
};

struct Axis3 {
  Point3 origin{};
  Vec3 direction{0.0, 0.0, 1.0};
};

struct Plane {
  Point3 origin{};
  Vec3 normal{0.0, 0.0, 1.0};
};

struct CurveId {
  std::uint64_t value{};
};

struct SurfaceId {
  std::uint64_t value{};
};

struct PCurveId {
  std::uint64_t value{};
};

struct VertexId {
  std::uint64_t value{};
};

struct EdgeId {
  std::uint64_t value{};
};

struct CoedgeId {
  std::uint64_t value{};
};

struct LoopId {
  std::uint64_t value{};
};

struct FaceId {
  std::uint64_t value{};
};

struct ShellId {
  std::uint64_t value{};
};

struct BodyId {
  std::uint64_t value{};
};

struct MeshId {
  std::uint64_t value{};
};

struct IntersectionId {
  std::uint64_t value{};
};

struct DiagnosticId {
  std::uint64_t value{};
};

struct NodeId {
  std::uint64_t value{};
};

struct ImplicitFieldId {
  std::uint64_t value{};
};

inline constexpr bool operator==(CurveId lhs, CurveId rhs) {
  return lhs.value == rhs.value;
}
inline constexpr bool operator==(SurfaceId lhs, SurfaceId rhs) {
  return lhs.value == rhs.value;
}
inline constexpr bool operator==(VertexId lhs, VertexId rhs) {
  return lhs.value == rhs.value;
}
inline constexpr bool operator==(EdgeId lhs, EdgeId rhs) {
  return lhs.value == rhs.value;
}
inline constexpr bool operator==(CoedgeId lhs, CoedgeId rhs) {
  return lhs.value == rhs.value;
}
inline constexpr bool operator==(LoopId lhs, LoopId rhs) {
  return lhs.value == rhs.value;
}
inline constexpr bool operator==(FaceId lhs, FaceId rhs) {
  return lhs.value == rhs.value;
}
inline constexpr bool operator==(ShellId lhs, ShellId rhs) {
  return lhs.value == rhs.value;
}
inline constexpr bool operator==(BodyId lhs, BodyId rhs) {
  return lhs.value == rhs.value;
}
inline constexpr bool operator==(MeshId lhs, MeshId rhs) {
  return lhs.value == rhs.value;
}
inline constexpr bool operator==(DiagnosticId lhs, DiagnosticId rhs) {
  return lhs.value == rhs.value;
}

enum class StatusCode {
  Ok,
  InvalidInput,
  InvalidTopology,
  DegenerateGeometry,
  NumericalInstability,
  ToleranceConflict,
  NotImplemented,
  OperationFailed,
  InternalError
};

enum class PrecisionMode { FastFloat, AdaptiveCertified, ExactCritical };

enum class ValidationMode { Fast, Standard, Strict };

enum class RepairMode { ReportOnly, SuggestOnly, Safe, Aggressive };

enum class BooleanOp { Union, Subtract, Intersect, Split };

enum class RepKind { ExactBRep, MeshRep, ImplicitRep, HybridRep };

enum class IssueSeverity { Info, Warning, Error, Fatal };

enum class Sign { Negative, Zero, Positive, Uncertain };

enum class NodeKind {
  Geometry,
  Topology,
  Operation,
  Cache,
  Visualization,
  Analysis
};

/// 插件宿主沙箱级别（当前均为进程内模型；非 OS 级隔离）。
enum class PluginSandboxLevel : std::uint8_t {
  None = 0,       ///< 与历史行为一致：共享地址空间
  Annotated = 1   ///< 能力/发现报告中标注更高安全期望（占位，执行模型仍同 None）
};

/// 宿主对清单 `plugin_api_version` 与 SDK API 的比对规则（仅当 `require_plugin_api_version_match` 为真时参与注册门禁）。
enum class PluginApiVersionMatchMode : std::uint8_t {
  Exact = 0,     ///< 声明串（去空白后）与 `kPluginSdkApiVersion` 完全一致
  SameMinor = 1, ///< `major.minor` 须与宿主一致；允许额外 patch（如宿主 `1.0` 接受 `1.0.3`）
  SameMajor = 2  ///< `major` 须一致（更宽松，仅建议在受控环境使用）
};

struct Warning {
  std::string code;
  std::string message;
};

struct Issue {
  std::string code;
  IssueSeverity severity{IssueSeverity::Info};
  std::string message;
  std::vector<std::uint64_t> related_entities;
  /// 工作流阶段标签（如 bool.validate、io.post_import.validation）；空表示未标注。
  std::string stage;
};

struct DiagnosticReport {
  DiagnosticId id{};
  std::vector<Issue> issues;
  std::string summary;
};

struct DiagnosticStats {
  std::uint64_t total{};
  std::uint64_t info{};
  std::uint64_t warning{};
  std::uint64_t error{};
  std::uint64_t fatal{};
};

/// 求值图可观测性计数（与节点重算计数独立；用于 CI/性能门禁与失效传播分析）。
struct EvalGraphTelemetry {
  std::uint64_t invalidate_node_calls{};
  std::uint64_t invalidate_body_calls{};
  std::uint64_t invalidate_many_batches{};
  std::uint64_t invalidate_many_node_total{};
  /// `recompute_many` 调用批次数（与 `invalidate_many_batches` 对称，供 CI/成本分析）。
  std::uint64_t recompute_many_batches{};
  /// 各批 `recompute_many` 传入的根节点总数（单次批大小之和）。
  std::uint64_t recompute_many_root_total{};
  /// 重算过程中因依赖 DAG 去重而跳过的「已完成重算」节点次数。
  std::uint64_t recompute_transitive_dedup_skips{};
  /// 单次 `recompute` 递归中实际执行「清除失效并递增 per-node 重算计数」的节点次数。
  std::uint64_t recompute_finish_events{};
  /// `invalidate(NodeId)` 调用时根节点在失效前**已经**处于失效状态的次数（仍可能向下游传播）。
  std::uint64_t invalidate_node_redundant_calls{};
  /// `recompute(NodeId)` 入口时根节点**已非失效**仍触发重算流程的次数（依赖链或幂等重算成本分析）。
  std::uint64_t recompute_root_already_valid_calls{};
  /// `exists` / `is_invalid` / `recompute_count` 等**轻量状态读 API**调用次数（与失效/重算写路径互补，供治理与成本门禁）。
  std::uint64_t eval_graph_state_read_calls{};
  /// 单次 `recompute(NodeId)` 调用中，实际完成「清除失效并递增 per-node 计数」的**不同节点数**的历史峰值（传递闭包规模门禁）。
  std::uint64_t recompute_single_root_max_finish_nodes{};
  /// 单次 `recompute` 中依赖 DFS 的**最大递归深度**（根深度记为 1；与 `recompute_single_root_max_finish_nodes` 互补）。
  std::uint64_t recompute_single_root_max_stack_depth{};
};

/// 内核内部「体/面 → Eval 失效传播」引擎入口计数（含 Heal 等绕过 `EvalGraphService` 的调用）；与 `EvalGraphTelemetry` 的 API 级计数互补。
struct EvalInvalidationBridgeMetrics {
  /// `invalidate_eval_for_body` 调用次数（含由面/多体入口间接产生）。
  std::uint64_t for_body_entries{};
  /// `invalidate_eval_for_faces` 顶层调用次数。
  std::uint64_t for_faces_entries{};
  /// `invalidate_eval_for_bodies` 批次数。
  std::uint64_t for_bodies_batches{};
  /// 各次 `invalidate_eval_for_bodies` 传入列表长度之和（未去重；与 `initializer_list::size()` 一致）。
  std::uint64_t for_bodies_list_size_total{};
  /// `invalidate_eval_downstream` 每次入口（含递归）；与 `EvalGraphService::invalidate` / `invalidate_many` 及 `invalidate_eval_for_*` 共用，用于传播成本门禁。
  std::uint64_t downstream_invalidation_steps{};
};

struct TolerancePolicy {
  Scalar linear{1e-6};
  Scalar angular{1e-6};
  Scalar min_local{1e-9};
  Scalar max_local{1e-3};
  PrecisionMode precision_mode{PrecisionMode::AdaptiveCertified};
};

/// 内核侧插件宿主策略（进程内注册，非 OS 级隔离；用于容量与声明校验）。
struct PluginHostPolicy {
  /// 曲线/修复/导入/导出插件实例总数上限；0 表示不限制。
  std::uint32_t max_plugin_slots{0};
  /// 同一类插件内 `type_name()` 是否必须唯一。
  bool enforce_unique_implementation_type_name{true};
  /// 注册时清单 `name` 必须非空且非全空白。
  bool require_non_empty_manifest_name{true};
  /// 已存在同名 manifest 时拒绝再次注册（默认 false，兼容多槽位共用一个展示名）。
  bool require_unique_manifest_name{false};
  /// 清单 capabilities 非空。
  bool require_non_empty_capabilities{false};
  /// 实现侧 `type_name()` 非空且非全空白。
  bool require_non_empty_plugin_type_name{true};
  /// 为真时：`PluginManifest::plugin_api_version`（去空白后）必须满足 `plugin_api_version_match_mode` 与宿主 SDK API 的兼容规则。
  bool require_plugin_api_version_match{false};
  /// 与 `require_plugin_api_version_match` 联用；默认 `Exact` 保持历史行为。
  PluginApiVersionMatchMode plugin_api_version_match_mode{PluginApiVersionMatchMode::Exact};
  /// 安全/隔离策略占位（供能力发现、审计与未来扩展；当前不改变插件调用路径）。
  PluginSandboxLevel sandbox_level{PluginSandboxLevel::None};
  /// 为真时：`Kernel::plugin_import_file` 在导入成功后对该 `BodyId` 调用 `ValidationService::validate_all`（默认 false）。
  bool auto_validate_body_after_plugin_importer{false};
  /// 为真时：`Kernel::plugin_export_file` 在调用导出插件之前对该 `BodyId` 调用 `validate_all`（默认 false）。
  bool auto_validate_body_before_plugin_exporter{false};
  /// 为真时：`Kernel::plugin_run_repair` 在修复插件返回 Ok 后，对结果体（`OpReport::output` 存在且仍注册则优先，否则为输入 `BodyId`）调用 `validate_all`（默认 false）。
  bool auto_validate_body_after_plugin_repair{false};
  /// 为真时：`Kernel::plugin_create_curve` 在曲线插件返回 Ok 后校验 `CurveId` 已在内核注册且 `CurveService::domain` 成功且为合法开区间（默认 true，用于拦截“悬空”返回值）。
  bool auto_verify_curve_after_plugin_curve{true};
};

struct KernelConfig {
  TolerancePolicy tolerance{};
  PrecisionMode precision_mode{PrecisionMode::AdaptiveCertified};
  bool enable_diagnostics{true};
  bool enable_cache{true};
  PluginHostPolicy plugin_host_policy{};
};

struct ImportOptions {
  bool run_validation{true};
  bool auto_repair{false};
  RepairMode repair_mode{RepairMode::Safe};
};

struct ExportOptions {
  /// 为 false（默认）时：网格类导出（STL/glTF/OBJ/3MF）在写出前执行 `inspect_mesh` 门控，存在越界索引或退化三角形则拒绝导出（工业严格交付）。
  /// 为 true 时：兼容模式，尽力写出（与历史行为一致，仅受网格生成本身失败约束）。
  bool compatibility_mode{false};
  bool embed_metadata{true};
  /// 为 true 时：网格类导出成功后，将 `RepresentationConversionService::export_mesh_report_json` 写到与主文件同目录的 `stem + ".mesh_report.json"`。
  bool write_mesh_validation_report{false};
};

struct BSplineCurveDesc {
  std::vector<Point3> poles;
  /// 曲线阶数（次数 = degree）；\<0 表示按控制点数量使用内核默认策略（开放均匀 B 样条）。
  int degree{-1};
  /// 非空时作为结点向量，长度须为 `poles.size() + degree + 1`（若指定 `degree`），或由长度隐含阶数 `knots.size() - poles.size() - 1`。
  std::vector<Scalar> knots;
};

struct NURBSCurveDesc {
  std::vector<Point3> poles;
  std::vector<Scalar> weights;
  /// \<0 表示默认：单段 clamped NURBS，阶数为 `poles.size()-1`（与有理 Bézier 段一致）。
  int degree{-1};
  /// 非空时作为结点向量；校验规则同 `BSplineCurveDesc::knots`（须与非负权重数量一致）。
  std::vector<Scalar> knots;
};

struct BSplineSurfaceDesc {
  std::vector<Point3> poles;
  /// u 向阶数；<0 表示由控制点数量按内核默认策略推断（与开放均匀结点一致）。
  int degree_u{-1};
  /// v 向阶数；<0 表示自动推断。
  int degree_v{-1};
  /// 非空时作为 u 向结点向量（长度须为 n_u + degree_u + 1，degree 由向量长度隐含或与 degree_u 一致）；求值前会仿射归一化到与参数域 [0, max(1,n_u-1)] 对齐。
  std::vector<Scalar> knots_u;
  /// v 向结点；语义同 knots_u。
  std::vector<Scalar> knots_v;
};

struct NURBSSurfaceDesc {
  std::vector<Point3> poles;
  std::vector<Scalar> weights;
  int degree_u{-1};
  int degree_v{-1};
  std::vector<Scalar> knots_u;
  std::vector<Scalar> knots_v;
};

struct PluginCurveDesc {
  std::string type_name;
};

struct ProfileRef {
  std::string label;
  /// Optional planar polygon profile in world coordinates (closedness implicit: last connects to first).
  /// When provided, `sweeps().extrude(...)` can materialize a real prism shell (Stage-2 minimal).
  std::vector<Point3> polygon_xyz;
};

struct TessellationOptions {
  Scalar chordal_error{0.1};
  Scalar angular_error{5.0};
  bool compute_normals{true};
  /// If true, attempt to generate per-vertex UVs for display/export.
  bool generate_texcoords{false};
  /// 焊接时若法向夹角大于该值则保留折边顶点（不合并索引）；`180` 表示仅按位置/UV 合并（与历史行为一致）。
  Scalar weld_shading_split_angle_deg{180.0};
  /// 对张量积/派生参数域 patch：用 `SurfaceService`/张量主曲率估计加强细分（与 `chordal_error`/`angular_error` 叠合）。
  bool use_principal_curvature_refinement{true};
  /// 对 Bezier/BSpline/NURBS/Revolved/Swept/Offset 的 patch：用双线性单元中点与真实曲面的偏差迭代加密（0 关闭）；上限受实现 cap。
  int refine_patch_chordal_max_passes{3};
  /// 与 `generate_texcoords` 联用：输出曲面参数 `(u,v)` 而非归一化 patch `[0,1]`，便于多面拼接时在纹理空间保留 **UV seam**（焊接键含 UV 时不误合并）。
  bool uv_parametric_seam{false};
};

struct CurveEvalResult {
  Point3 point{};
  Vec3 tangent{};
  /// 3D 空间曲线曲率 κ = |r'×r''| / |r'|³（弧长参数意义下）；直线/退化处为 0。
  Scalar curvature{};
  std::vector<Vec3> derivatives;
};

struct PCurveEvalResult {
  Point2 point{};
  Vec2 tangent{};
  /// UV 平面内带符号曲率 (x'y''−y'x'') / |r'|³；退化处为 0。
  Scalar curvature{};
  std::vector<Vec2> derivatives;
};

struct SurfaceEvalResult {
  Point3 point{};
  Vec3 du{};
  Vec3 dv{};
  Vec3 normal{};
  Scalar k1{};
  Scalar k2{};
  /// `SurfaceService::eval(..., deriv_order >= 2)` 且张量 NURBS/BSpline 路径成功时填充。
  Vec3 duu{};
  Vec3 dvv{};
  Vec3 duv{};
};

struct MassProperties {
  Scalar volume{};
  Scalar area{};
  Point3 centroid{};
  std::array<Scalar, 9> inertia{};
};

/// 三角化缓存可观测性（体级 `tessellation_cache` + 面级 `face_tessellation_cache`）；`clear_mesh_store` / `reset_runtime_stores` 会清零。
struct TessellationCacheStats {
  std::uint64_t body_cache_hits{};
  std::uint64_t body_cache_misses{};
  std::uint64_t body_cache_stale_evictions{};
  std::uint64_t face_cache_hits{};
  std::uint64_t face_cache_misses{};
  std::uint64_t face_cache_stale_evictions{};
};

/// 与 `TopologyTransaction` 在单次成功 `commit()` 时可观测的写操作分项一致（用于 `Kernel::topology_commit_audit`）。
struct TopologyCommitWriteBreakdown {
  std::uint64_t created_vertices{};
  std::uint64_t created_edges{};
  std::uint64_t created_coedges{};
  std::uint64_t created_loops{};
  std::uint64_t created_faces{};
  std::uint64_t created_shells{};
  std::uint64_t created_bodies{};
  std::uint64_t deleted_faces{};
  std::uint64_t deleted_shells{};
  std::uint64_t deleted_bodies{};
  std::uint64_t replaced_surfaces{};
  std::uint64_t coedge_pcurve_binds{};
  std::uint64_t coedge_pcurve_clears{};
};

/// 各次成功拓扑提交中「新建实体」分项之和（与能力报告 `Core.Topology.Audit.Cumulative.CreatedEntities` 一致）。
inline std::uint64_t topology_commit_breakdown_created_entities_total(const TopologyCommitWriteBreakdown& b) {
  return b.created_vertices + b.created_edges + b.created_coedges + b.created_loops + b.created_faces +
         b.created_shells + b.created_bodies;
}

struct MeshInspectionReport {
  std::uint64_t vertex_count{};
  std::uint64_t index_count{};
  std::uint64_t triangle_count{};
  std::uint64_t connected_components{};
  bool is_indexed{false};
  bool has_out_of_range_indices{false};
  bool has_degenerate_triangles{false};
  /// `MeshRecord::label`（如 `mesh_from_brep_bbox_proxy`），便于与 `tessellation_strategy` 对照。
  std::string mesh_label;
  /// Pipeline id for rep closure (budget → strategy → QA); empty if unavailable.
  std::string tessellation_strategy;
  /// Serialized tessellation inputs (chordal/angular/normals) for regression exports.
  std::string tessellation_budget_digest;
};

struct ConversionErrorBudget {
  // Linear budgets in model units.
  Scalar bbox_abs_tol{0.0};
  Scalar max_point_abs_tol{0.0};
  // Angular budgets in degrees.
  Scalar normal_angle_deg_tol{0.0};
  /// 派生上述线性预算时使用的 `TessellationOptions::chordal_error` 快照（审计/回归对齐）。
  Scalar chordal_error_basis{0.0};
  /// 派生角度预算时使用的 `TessellationOptions::angular_error` 快照（度）。
  Scalar angular_error_basis_deg{0.0};
};

struct RoundTripReport {
  bool passed{false};
  ConversionErrorBudget budget{};
  Scalar bbox_max_abs_delta{0.0};
  Scalar max_point_abs_delta{0.0};
  /// 与解析曲面法向的最大夹角（度）；未做该项测量时为 0（如盒体或未算法向）。
  Scalar max_normal_angle_deg_delta{0.0};
  /// 为真时表示已执行法向偏差测量并应用 `budget.normal_angle_deg_tol` 参与 `passed`。
  bool normal_deviation_measured{false};
  std::uint64_t source_triangles{0};
  std::uint64_t roundtrip_triangles{0};
  /// BRep→Mesh path tag used for the source mesh (same semantics as MeshInspectionReport).
  std::string tessellation_strategy;
  /// 与本次验证所用 `TessellationOptions` 对齐的预算摘要（JSON 片段，与 `MeshRecord::tessellation_budget_digest` 同形）。
  std::string tessellation_budget_digest;
};

struct TopologySummary {
  std::uint64_t shell_count{};
  std::uint64_t face_count{};
  std::uint64_t loop_count{};
  std::uint64_t edge_count{};
  std::uint64_t vertex_count{};
};

struct BooleanOptions {
  TolerancePolicy tolerance{};
  bool diagnostics{true};
  bool auto_repair{false};
};

struct OpReport {
  StatusCode status{StatusCode::Ok};
  BodyId output{};
  DiagnosticId diagnostic_id{};
  std::vector<Warning> warnings;
};

struct PluginManifest {
  std::string name;
  std::string version;
  std::string vendor;
  std::vector<std::string> capabilities;
  /// 声明与宿主 `kPluginSdkApiVersion` / `Kernel::plugin_sdk_api_version` 兼容，例如 `"1.0"`；可空（由策略决定是否允许）。
  std::string plugin_api_version;
  /// 与进程内实现 `type_name()` 对齐；`register_*` 带实现时若为空则自动填为插件 `type_name`（去空白），若已填则须一致。`register_manifest_only` 可预填供审计；按 `type_name` 注销实现时会移除同字段匹配的清单条目。
  std::string implementation_type_name;
};

} // namespace axiom
