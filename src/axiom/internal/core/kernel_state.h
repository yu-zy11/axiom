#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "axiom/core/types.h"
#include "axiom/plugin/plugin_registry.h"

namespace axiom::detail {

enum class CurveKind {
    Line,
    LineSegment,
    Circle,
    Ellipse,
    Parabola,
    Hyperbola,
    Bezier,
    BSpline,
    Nurbs,
    CompositePolyline,
    CompositeChain
};

enum class PCurveKind {
    Polyline
};

enum class SurfaceKind {
    Plane,
    Cylinder,
    Cone,
    Sphere,
    Torus,
    Bezier,
    BSpline,
    Nurbs,
    // Stage 2 minimal analytic/derived surfaces.
    Revolved,
    Swept,
    Trimmed,
    Offset
};

enum class BodyKind {
    Generic,
    Box,
    Sphere,
    Cylinder,
    Cone,
    Torus,
    /// 直角三棱柱：XY 平面内直角三角形顶点 (0,0)、(+dx,0)、(0,+dy)，沿 +Z 拉伸 dz（相对 BodyRecord.origin）。
    Wedge,
    Sweep,
    BooleanResult,
    Modified,
    BlendResult,
    Imported
};

struct CurveRecord {
    CurveKind kind {CurveKind::Line};
    Point3 origin {};
    Vec3 direction {1.0, 0.0, 0.0};
    Scalar radius {0.0};
    Scalar param_a {0.0};
    Scalar param_b {0.0};
    Vec3 normal {0.0, 0.0, 1.0};
    Vec3 axis_u {1.0, 0.0, 0.0};
    Vec3 axis_v {0.0, 1.0, 0.0};
    std::vector<Point3> poles;
    std::vector<Scalar> weights;
    std::vector<Scalar> knots_u;
    std::vector<CurveId> children;
};

struct PCurveRecord {
    PCurveKind kind {PCurveKind::Polyline};
    std::vector<Point2> poles;
};

struct SurfaceRecord {
    SurfaceKind kind {SurfaceKind::Plane};
    Point3 origin {};
    Vec3 axis {0.0, 0.0, 1.0};
    Vec3 normal {0.0, 0.0, 1.0};
    Scalar radius_a {0.0};
    Scalar radius_b {0.0};
    Scalar semi_angle {0.0};
    // For derived surfaces.
    SurfaceId base_surface_id {};
    CurveId profile_curve_id {};
    Scalar sweep_angle_rad {0.0};
    Scalar sweep_length {0.0};
    Scalar trim_u_min {0.0};
    Scalar trim_u_max {1.0};
    Scalar trim_v_min {0.0};
    Scalar trim_v_max {1.0};
    Scalar offset_distance {0.0};
    std::vector<Point3> poles;
    std::vector<Scalar> weights;
    std::vector<Scalar> knots_u;
    std::vector<Scalar> knots_v;
};

struct VertexRecord {
    Point3 point {};
};

struct EdgeRecord {
    CurveId curve_id {};
    VertexId v0 {};
    VertexId v1 {};
};

struct CoedgeRecord {
    EdgeId edge_id {};
    bool reversed {false};
    PCurveId pcurve_id {};
};

struct LoopRecord {
    std::vector<CoedgeId> coedges;
};

struct FaceRecord {
    SurfaceId surface_id {};
    LoopId outer_loop {};
    std::vector<LoopId> inner_loops;
    std::vector<FaceId> source_faces;
};

struct ShellRecord {
    std::vector<FaceId> faces;
    std::vector<ShellId> source_shells;
    std::vector<FaceId> source_faces;
};

struct BodyRecord {
    BodyKind kind {BodyKind::Generic};
    RepKind rep_kind {RepKind::ExactBRep};
    Point3 origin {};
    Vec3 axis {0.0, 0.0, 1.0};
    Scalar a {0.0};
    Scalar b {0.0};
    Scalar c {0.0};
    /// 多边形/占位 `extrude`、`thicken`、多边形 `revolve`：`mass_properties` 等；`extrude_poly_cap_area<=0` 表示未缓存（`thicken` 为面面积估计；`revolve` 可为轮廓面积）。
    Scalar extrude_poly_cap_area {0.0};
    Scalar extrude_lateral_area {0.0};
    Point3 extrude_mass_centroid {};
    /// 多边形 `revolve`：子午面轮廓副本，供真实旋转体 BRep 物化（轴须在轮廓平面内，转角 `< 2π`）。
    std::vector<Point3> revolve_profile_xyz;
    /// 三角网格闭壳（ρ=1）质量：体积/质心/惯性（关于质心，世界系行主序 3×3）；与 `sweep_cached_surface_area` 在 `sweep_polyhedral_mass_valid` 时由物化写入。
    bool sweep_polyhedral_mass_valid {false};
    Scalar sweep_polyhedral_volume {0.0};
    Scalar sweep_cached_surface_area {0.0};
    Point3 sweep_polyhedral_centroid {};
    std::array<Scalar, 9> sweep_inertia_about_centroid {};
    std::string label;
    BoundingBox bbox;
    std::vector<ShellId> shells;
    std::vector<BodyId> source_bodies;
    std::vector<ShellId> source_shells;
    std::vector<FaceId> source_faces;
};

struct MeshRecord {
    BodyId source_body {};
    std::string label;
    /// Stable strategy tag: owned_welded, primitive_*, bbox_proxy, implicit, local_welded, …
    std::string tessellation_strategy;
    /// Short digest of TessellationOptions / budget inputs (JSON fragment).
    std::string tessellation_budget_digest;
    BoundingBox bbox;
    std::vector<Point3> vertices;
    std::vector<Vec3> normals;
    std::vector<Point2> texcoords;
    std::vector<Index> indices;
};

struct IntersectionRecord {
    std::string label;
    BoundingBox bbox;
    std::vector<CurveId> curves;
    std::vector<SurfaceId> surfaces;
};

struct KernelState {
    explicit KernelState(const KernelConfig& in_config) : config(in_config) {
        plugin_registry.set_host_policy(in_config.plugin_host_policy);
    }

    KernelConfig config;
    std::uint64_t next_id {1};
    VersionId next_version {1};
    std::uint64_t next_diag_id {1};

    std::unordered_map<std::uint64_t, CurveRecord> curves;
    std::unordered_map<std::uint64_t, PCurveRecord> pcurves;
    std::unordered_map<std::uint64_t, SurfaceRecord> surfaces;
    std::unordered_map<std::uint64_t, VertexRecord> vertices;
    std::unordered_map<std::uint64_t, EdgeRecord> edges;
    std::unordered_map<std::uint64_t, CoedgeRecord> coedges;
    std::unordered_map<std::uint64_t, LoopRecord> loops;
    std::unordered_map<std::uint64_t, FaceRecord> faces;
    std::unordered_map<std::uint64_t, ShellRecord> shells;
    std::unordered_map<std::uint64_t, BodyRecord> bodies;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> edge_to_coedges;
    std::unordered_map<std::uint64_t, std::uint64_t> coedge_to_loop;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> loop_to_faces;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> face_to_shells;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> shell_to_bodies;
    std::unordered_map<std::uint64_t, MeshRecord> meshes;
    // key: stable hash of (body geometry params + tessellation options).
    std::unordered_map<std::string, MeshId> tessellation_cache;
    // key: stable hash of (face topology + tessellation options) for local re-tessellation.
    std::unordered_map<std::string, MeshId> face_tessellation_cache;
    std::unordered_map<std::uint64_t, IntersectionRecord> intersections;
    std::unordered_map<std::string, CurveEvalResult> curve_eval_cache;
    std::unordered_map<std::string, SurfaceEvalResult> surface_eval_cache;
    std::unordered_map<std::uint64_t, DiagnosticReport> diagnostics;
    std::unordered_map<std::uint64_t, NodeKind> eval_nodes;
    std::unordered_map<std::uint64_t, std::string> eval_labels;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> eval_dependencies;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> eval_reverse_dependencies;
    std::unordered_map<std::uint64_t, bool> eval_invalid;
    std::unordered_map<std::uint64_t, std::uint64_t> eval_recompute_count;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> eval_body_bindings;
    PluginRegistry plugin_registry;

    std::uint64_t allocate_id() {
        return next_id++;
    }

    DiagnosticId create_diagnostic(std::string summary, std::vector<Issue> issues = {}) {
        if (!config.enable_diagnostics && issues.empty()) {
            return {};
        }
        const auto id = DiagnosticId {next_diag_id++};
        diagnostics.emplace(id.value, DiagnosticReport {id, std::move(issues), std::move(summary)});
        return id;
    }

    void append_diagnostic_issue(DiagnosticId id, Issue issue) {
        if (id.value == 0) {
            return;
        }
        const auto it = diagnostics.find(id.value);
        if (it == diagnostics.end()) {
            return;
        }
        it->second.issues.push_back(std::move(issue));
    }
};

inline Vec3 subtract(const Point3& lhs, const Point3& rhs) {
    return Vec3 {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

inline Point3 add_point_vec(const Point3& point, const Vec3& vec) {
    return Point3 {point.x + vec.x, point.y + vec.y, point.z + vec.z};
}

inline Vec3 scale(const Vec3& value, Scalar s) {
    return Vec3 {value.x * s, value.y * s, value.z * s};
}

inline Scalar dot(const Vec3& lhs, const Vec3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

inline Vec3 cross(const Vec3& lhs, const Vec3& rhs) {
    return Vec3 {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x
    };
}

inline Scalar norm(const Vec3& value) {
    return std::sqrt(dot(value, value));
}

inline Vec3 normalize(const Vec3& value) {
    const auto n = norm(value);
    if (n <= 0.0) {
        return value;
    }
    return scale(value, 1.0 / n);
}

inline BoundingBox make_bbox(const Point3& min, const Point3& max) {
    return BoundingBox {min, max, true};
}

inline BoundingBox bbox_from_center_radius(const Point3& center, Scalar rx, Scalar ry, Scalar rz) {
    return BoundingBox {
        Point3 {center.x - rx, center.y - ry, center.z - rz},
        Point3 {center.x + rx, center.y + ry, center.z + rz},
        true
    };
}

inline bool has_body(const KernelState& state, BodyId body_id) {
    return state.bodies.find(body_id.value) != state.bodies.end();
}

inline bool has_curve(const KernelState& state, CurveId curve_id) {
    return state.curves.find(curve_id.value) != state.curves.end();
}

inline bool has_pcurve(const KernelState& state, PCurveId pcurve_id) {
    return state.pcurves.find(pcurve_id.value) != state.pcurves.end();
}

inline bool has_surface(const KernelState& state, SurfaceId surface_id) {
    return state.surfaces.find(surface_id.value) != state.surfaces.end();
}

inline Issue make_issue(std::string code, IssueSeverity severity, std::string message) {
    return Issue {std::move(code), severity, std::move(message), {}, {}};
}

inline bool has_vertex(const KernelState& state, VertexId vertex_id) {
    return state.vertices.find(vertex_id.value) != state.vertices.end();
}

inline bool has_edge(const KernelState& state, EdgeId edge_id) {
    return state.edges.find(edge_id.value) != state.edges.end();
}

inline bool has_loop(const KernelState& state, LoopId loop_id) {
    return state.loops.find(loop_id.value) != state.loops.end();
}

inline bool has_face(const KernelState& state, FaceId face_id) {
    return state.faces.find(face_id.value) != state.faces.end();
}

inline bool has_shell(const KernelState& state, ShellId shell_id) {
    return state.shells.find(shell_id.value) != state.shells.end();
}

}  // namespace axiom::detail

