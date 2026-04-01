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
  bool compatibility_mode{false};
  bool embed_metadata{true};
};

struct BSplineCurveDesc {
  std::vector<Point3> poles;
};

struct NURBSCurveDesc {
  std::vector<Point3> poles;
  std::vector<Scalar> weights;
};

struct BSplineSurfaceDesc {
  std::vector<Point3> poles;
};

struct NURBSSurfaceDesc {
  std::vector<Point3> poles;
  std::vector<Scalar> weights;
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
  /// Optional sharp-angle-based weld split for shading normals (degrees, validated in [0, 180]).
  Scalar weld_shading_split_angle_deg{60.0};
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
};

struct MassProperties {
  Scalar volume{};
  Scalar area{};
  Point3 centroid{};
  std::array<Scalar, 9> inertia{};
};

struct MeshInspectionReport {
  std::uint64_t vertex_count{};
  std::uint64_t index_count{};
  std::uint64_t triangle_count{};
  std::uint64_t connected_components{};
  bool is_indexed{false};
  bool has_out_of_range_indices{false};
  bool has_degenerate_triangles{false};
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
};

struct RoundTripReport {
  bool passed{false};
  ConversionErrorBudget budget{};
  Scalar bbox_max_abs_delta{0.0};
  Scalar max_point_abs_delta{0.0};
  std::uint64_t source_triangles{0};
  std::uint64_t roundtrip_triangles{0};
  /// BRep→Mesh path tag used for the source mesh (same semantics as MeshInspectionReport).
  std::string tessellation_strategy;
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
