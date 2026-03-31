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

struct Warning {
  std::string code;
  std::string message;
};

struct Issue {
  std::string code;
  IssueSeverity severity{IssueSeverity::Info};
  std::string message;
  std::vector<std::uint64_t> related_entities;
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

struct KernelConfig {
  TolerancePolicy tolerance{};
  PrecisionMode precision_mode{PrecisionMode::AdaptiveCertified};
  bool enable_diagnostics{true};
  bool enable_cache{true};
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
};

struct TessellationOptions {
  Scalar chordal_error{0.1};
  Scalar angular_error{5.0};
  bool compute_normals{true};
};

struct CurveEvalResult {
  Point3 point{};
  Vec3 tangent{};
  std::vector<Vec3> derivatives;
};

struct PCurveEvalResult {
  Point2 point{};
  Vec2 tangent{};
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
};

} // namespace axiom
