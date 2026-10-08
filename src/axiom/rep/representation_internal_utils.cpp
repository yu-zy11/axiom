#include "axiom/internal/rep/representation_internal_utils.h"

#include "axiom/internal/geo/geo_rep_tessellation_link.h"
#include "axiom/internal/core/topology_materialization.h"
#include "axiom/internal/topo/topo_service_internal.h"
#include "axiom/internal/math/math_internal_utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace axiom::detail {

namespace {

constexpr Scalar kPi = 3.14159265358979323846;

Scalar clamp(Scalar v, Scalar lo, Scalar hi) {
    return std::max(lo, std::min(hi, v));
}

Scalar radians_from_degrees(Scalar deg) {
    return deg * (kPi / 180.0);
}

Vec3 pick_orthogonal_unit(const Vec3& axis_unit) {
    // Pick a non-parallel vector, then normalize cross product.
    const Vec3 a = (std::abs(axis_unit.z) < 0.9) ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, 0.0, 0.0};
    const Vec3 u = cross(axis_unit, a);
    return normalize(u);
}

struct GridIndex {
    std::size_t u{};
    std::size_t v{};
};

void add_quad(std::vector<Index>& out, Index i00, Index i10, Index i11, Index i01) {
    out.insert(out.end(), {i00, i10, i11, i00, i11, i01});
}

}  // namespace

Scalar axis_distance(Scalar value, Scalar min_v, Scalar max_v) {
    if (value < min_v) {
        return min_v - value;
    }
    if (value > max_v) {
        return value - max_v;
    }
    return 0.0;
}

BoundingBox mesh_bbox_from_vertices(const std::vector<Point3>& vertices) {
    if (vertices.empty()) {
        return BoundingBox {};
    }

    Point3 min = vertices.front();
    Point3 max = vertices.front();
    for (const auto& p : vertices) {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
        max.z = std::max(max.z, p.z);
    }
    return BoundingBox {min, max, true};
}

std::vector<Point3> bbox_corners(const BoundingBox& bbox) {
    return {
        {bbox.min.x, bbox.min.y, bbox.min.z},
        {bbox.max.x, bbox.min.y, bbox.min.z},
        {bbox.max.x, bbox.max.y, bbox.min.z},
        {bbox.min.x, bbox.max.y, bbox.min.z},
        {bbox.min.x, bbox.min.y, bbox.max.z},
        {bbox.max.x, bbox.min.y, bbox.max.z},
        {bbox.max.x, bbox.max.y, bbox.max.z},
        {bbox.min.x, bbox.max.y, bbox.max.z},
    };
}

std::vector<Index> triangulate_bbox(std::size_t slices_per_face) {
    const std::array<std::array<Index, 4>, 6> faces {{
        {0, 1, 2, 3},
        {4, 5, 6, 7},
        {0, 1, 5, 4},
        {2, 3, 7, 6},
        {1, 2, 6, 5},
        {0, 3, 7, 4},
    }};

    std::vector<Index> indices;
    indices.reserve(faces.size() * slices_per_face * 6);
    for (const auto& face : faces) {
        for (std::size_t i = 0; i < slices_per_face; ++i) {
            if ((i % 2) == 0) {
                indices.insert(indices.end(), {face[0], face[1], face[2], face[0], face[2], face[3]});
            } else {
                indices.insert(indices.end(), {face[0], face[1], face[3], face[1], face[2], face[3]});
            }
        }
    }
    return indices;
}

bool is_valid_bbox(const BoundingBox& bbox) {
    return bbox.is_valid &&
           bbox.max.x >= bbox.min.x &&
           bbox.max.y >= bbox.min.y &&
           bbox.max.z >= bbox.min.z;
}

bool has_valid_tessellation_options(const TessellationOptions& options) {
    return std::isfinite(options.chordal_error) && std::isfinite(options.angular_error) &&
           std::isfinite(options.weld_shading_split_angle_deg) &&
           options.chordal_error > 0.0 && options.chordal_error <= std::numeric_limits<Scalar>::max() * 0.5 &&
           options.angular_error > 0.0 &&
           options.weld_shading_split_angle_deg >= 0.0 &&
           options.weld_shading_split_angle_deg <= 180.0 &&
           options.refine_patch_chordal_max_passes >= 0 && options.refine_patch_chordal_max_passes <= 12;
}

std::size_t tessellation_slices_per_face(const TessellationOptions& options) {
    const auto slices_from_chordal = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(std::min<Scalar>(8.0, 1.0 / options.chordal_error))));
    const auto slices_from_angular = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(std::min<Scalar>(8.0, 180.0 / options.angular_error))));
    return std::min<std::size_t>(8, std::max(slices_from_chordal, slices_from_angular));
}

std::string tessellation_cache_key(const KernelState& state, BodyId body_id, const TessellationOptions& options) {
    const auto& body = state.bodies.at(body_id.value);
    // Include identity and the current owned boundary: equal bboxes and creation
    // parameters do not imply equal geometry or equal mesh provenance.
    std::ostringstream oss;
    oss << std::setprecision(std::numeric_limits<Scalar>::max_digits10);
    oss << "body=" << body_id.value << "|kind=" << static_cast<std::uint32_t>(body.kind)
        << "|rep=" << static_cast<std::uint32_t>(body.rep_kind)
        << "|o=" << body.origin.x << "," << body.origin.y << "," << body.origin.z
        << "|ax=" << body.axis.x << "," << body.axis.y << "," << body.axis.z
        << "|a=" << body.a << "|b=" << body.b << "|c=" << body.c
        << "|bb=" << body.bbox.min.x << "," << body.bbox.min.y << "," << body.bbox.min.z
        << "," << body.bbox.max.x << "," << body.bbox.max.y << "," << body.bbox.max.z
        << "|tess=" << options.chordal_error << "," << options.angular_error
        << "," << (options.compute_normals ? 1 : 0)
        << "," << (options.generate_texcoords ? 1 : 0)
        << "," << options.weld_shading_split_angle_deg << "," << (options.use_principal_curvature_refinement ? 1 : 0)
        << "," << options.refine_patch_chordal_max_passes << "," << (options.uv_parametric_seam ? 1 : 0);
    oss << "|analytic=" << body.analytic_mass_valid
        << "|primitive=" << body.primitive_tessellation_valid;
    for (const auto shell_id : body.shells) {
        oss << "|shell=" << shell_id.value;
        const auto shell = state.shells.find(shell_id.value);
        if (shell == state.shells.end()) { oss << ":missing"; continue; }
        for (const auto face : shell->second.faces)
            oss << "|" << face_tessellation_cache_key(state, face, options);
    }
    return oss.str();
}

std::string tessellation_budget_digest_json(const TessellationOptions& options) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(12);
    oss << "{\"chordal_error\":" << options.chordal_error
        << ",\"angular_error_deg\":" << options.angular_error
        << ",\"compute_normals\":" << (options.compute_normals ? "true" : "false")
        << ",\"generate_texcoords\":" << (options.generate_texcoords ? "true" : "false")
        << ",\"weld_shading_split_angle_deg\":" << options.weld_shading_split_angle_deg
        << ",\"use_principal_curvature_refinement\":" << (options.use_principal_curvature_refinement ? "true" : "false")
        << ",\"refine_patch_chordal_max_passes\":" << options.refine_patch_chordal_max_passes
        << ",\"uv_parametric_seam\":" << (options.uv_parametric_seam ? "true" : "false")
        << "}";
    return oss.str();
}

ConversionErrorBudget conversion_error_budget_from_tessellation(const TessellationOptions& options) {
    ConversionErrorBudget b;
    b.chordal_error_basis = options.chordal_error;
    b.angular_error_basis_deg = options.angular_error;
    const auto e = std::max<Scalar>(options.chordal_error, std::numeric_limits<Scalar>::epsilon());
    b.bbox_abs_tol = e * 2.0;
    b.max_point_abs_tol = e;
    b.normal_angle_deg_tol = std::max<Scalar>(options.angular_error, 1e-6);
    return b;
}

std::string conversion_error_budget_digest_json(const ConversionErrorBudget& b) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(12);
    oss << "{\"bbox_abs_tol\":" << b.bbox_abs_tol << ",\"max_point_abs_tol\":" << b.max_point_abs_tol
        << ",\"normal_angle_deg_tol\":" << b.normal_angle_deg_tol
        << ",\"chordal_error_basis\":" << b.chordal_error_basis
        << ",\"angular_error_basis_deg\":" << b.angular_error_basis_deg
        << ",\"derivation\":\"tessellation_options_v1\"}";
    return oss.str();
}

std::string face_tessellation_cache_key(const KernelState& state, FaceId face_id, const TessellationOptions& options) {
    std::ostringstream oss;
    oss << std::setprecision(std::numeric_limits<Scalar>::max_digits10);
    oss << "face=" << face_id.value
        << "|tess=" << options.chordal_error << "," << options.angular_error
        << "," << (options.compute_normals ? 1 : 0)
        << "," << (options.generate_texcoords ? 1 : 0)
        << "," << options.weld_shading_split_angle_deg << "," << (options.use_principal_curvature_refinement ? 1 : 0)
        << "," << options.refine_patch_chordal_max_passes << "," << (options.uv_parametric_seam ? 1 : 0);
    const auto append_points = [&](const auto& points) {
        for (const auto& p : points) {
            oss << "," << p.x << "," << p.y;
            if constexpr (requires { p.z; }) oss << "," << p.z;
        }
    };
    std::vector<SurfaceId> surfaces;
    const auto face_it = state.faces.find(face_id.value);
    if (face_it != state.faces.end()) {
        oss << "|surf=" << face_it->second.surface_id.value
            << "|outer=" << face_it->second.outer_loop.value;
        surfaces.push_back(face_it->second.surface_id);
        const auto surf_it = state.surfaces.find(face_it->second.surface_id.value);
        if (surf_it != state.surfaces.end()) {
            const auto& s = surf_it->second;
            oss << "|kind=" << static_cast<int>(s.kind);
            if (s.kind == SurfaceKind::Trimmed) {
                oss << "|base=" << s.base_surface_id.value << "|tu=" << s.trim_u_min << ","
                    << s.trim_u_max << "|tv=" << s.trim_v_min << "," << s.trim_v_max
                    << "|tuvn=" << s.trim_uv_loop.size()
                    << "|holes=" << s.trim_uv_holes.size();
            }
        }
    }
    for (std::size_t i = 0; i < surfaces.size() && i < 16; ++i) {
        const auto found = state.surfaces.find(surfaces[i].value);
        if (found == state.surfaces.end()) continue;
        const auto& s = found->second;
        oss << "|surface_record=" << surfaces[i].value << "," << static_cast<int>(s.kind)
            << "," << s.origin.x << "," << s.origin.y << "," << s.origin.z
            << "," << s.axis.x << "," << s.axis.y << "," << s.axis.z
            << "," << s.normal.x << "," << s.normal.y << "," << s.normal.z
            << "," << s.radius_a << "," << s.radius_b << "," << s.semi_angle
            << "," << s.base_surface_id.value << "," << s.profile_curve_id.value
            << "," << s.sweep_angle_rad << "," << s.sweep_length << "," << s.offset_distance
            << "," << s.trim_u_min << "," << s.trim_u_max << "," << s.trim_v_min << "," << s.trim_v_max
            << "," << s.spline_degree_u << "," << s.spline_degree_v;
        oss << "|poles"; append_points(s.poles);
        oss << "|weights"; for (const Scalar w : s.weights) oss << "," << w;
        oss << "|knots_u"; for (const Scalar k : s.knots_u) oss << "," << k;
        oss << "|knots_v"; for (const Scalar k : s.knots_v) oss << "," << k;
        oss << "|trim_outer"; append_points(s.trim_uv_loop);
        for (const auto& hole : s.trim_uv_holes) { oss << "|trim_hole"; append_points(hole); }
        if (s.base_surface_id.value != 0 && std::find(surfaces.begin(), surfaces.end(), s.base_surface_id) == surfaces.end())
            surfaces.push_back(s.base_surface_id);
        const auto profile = state.curves.find(s.profile_curve_id.value);
        if (profile != state.curves.end()) {
            oss << "|profile=" << static_cast<int>(profile->second.kind);
            append_points(profile->second.poles);
            const auto& c = profile->second;
            oss << "," << c.origin.x << "," << c.origin.y << "," << c.origin.z
                << "," << c.direction.x << "," << c.direction.y << "," << c.direction.z;
        }
    }
    if (face_it != state.faces.end()) {
        const auto append_loop = [&](LoopId loop_id) {
            oss << "|loop=" << loop_id.value;
            const auto loop = state.loops.find(loop_id.value);
            if (loop == state.loops.end()) { oss << ":missing"; return; }
            for (const auto id : loop->second.coedges) {
                oss << "|coedge=" << id.value;
                const auto coedge = state.coedges.find(id.value);
                if (coedge == state.coedges.end()) { oss << ":missing"; continue; }
                oss << ":" << coedge->second.edge_id.value << ":" << coedge->second.reversed
                    << ":pc=" << coedge->second.pcurve_id.value;
                const auto edge = state.edges.find(coedge->second.edge_id.value);
                if (edge == state.edges.end()) { oss << ":missing_edge"; continue; }
                const auto pcurve = state.pcurves.find(coedge->second.pcurve_id.value);
                if (pcurve != state.pcurves.end()) { oss << ":pcurve=" << static_cast<int>(pcurve->second.kind); append_points(pcurve->second.poles); }
                const auto curve = state.curves.find(edge->second.curve_id.value);
                if (curve != state.curves.end()) {
                    const auto& c = curve->second;
                    oss << ":curve_record=" << static_cast<int>(c.kind)
                        << "," << c.origin.x << "," << c.origin.y << "," << c.origin.z
                        << "," << c.direction.x << "," << c.direction.y << "," << c.direction.z
                        << "," << c.radius << "," << c.param_a << "," << c.param_b;
                    append_points(c.poles);
                    for (const Scalar w : c.weights) oss << "," << w;
                    for (const Scalar k : c.knots_u) oss << "," << k;
                }
                oss << ":curve=" << edge->second.curve_id.value
                    << ":range=" << edge->second.has_parameter_interval << ","
                    << edge->second.start_parameter << "," << edge->second.end_parameter;
                for (const auto vertex_id : {edge->second.v0, edge->second.v1}) {
                    oss << ":vertex=" << vertex_id.value;
                    const auto vertex = state.vertices.find(vertex_id.value);
                    if (vertex == state.vertices.end()) { oss << ":missing"; continue; }
                    const auto& p = vertex->second.point;
                    oss << "," << p.x << "," << p.y << "," << p.z;
                }
            }
        };
        append_loop(face_it->second.outer_loop);
        for (const auto loop : face_it->second.inner_loops) append_loop(loop);
    }
    return oss.str();
}

std::size_t segments_for_circle(Scalar radius, const TessellationOptions& options) {
    if (!(radius > 0.0) || !std::isfinite(radius) || !has_valid_tessellation_options(options)) return 0;
    // asin form retains small sag budgets that 1-e/r would round to 1.
    const Scalar theta_chordal = 4.0 * std::asin(std::sqrt(std::min<Scalar>(1.0, options.chordal_error / radius * 0.5)));
    const Scalar theta = std::min(theta_chordal, radians_from_degrees(options.angular_error));
    if (!(theta > 0.0) || !std::isfinite(theta)) return 0;
    const Scalar n = std::ceil(2.0 * kPi / theta);
    if (!std::isfinite(n) || n > 4096.0) return 0;
    return std::max<std::size_t>(8, static_cast<std::size_t>(n));
}

std::size_t segments_for_length(Scalar length, const TessellationOptions& options) {
    // For low-curvature directions, we still want longitudinal segmentation to avoid skinny triangles
    // and to allow future local re-tessellation patches. We base this on chordal error as a linear step.
    const auto L = std::max<Scalar>(length, 0.0);
    const auto e = std::max<Scalar>(options.chordal_error, std::numeric_limits<Scalar>::epsilon());
    const auto n = static_cast<std::size_t>(std::ceil(std::min<Scalar>(2048.0, L / (e * 2.0))));
    return std::max<std::size_t>(1, std::min<std::size_t>(2048, n));
}

/// 参数域 patch 上沿某一等参方向的近似 3D 弧长 `L`：同时满足弦长步长（`segments_for_length`）与
/// 等效圆周上的弦高+角度约束（`segments_for_circle`，与 primitive 一致）。
std::size_t segments_for_tensor_direction(Scalar approximate_arclength, const TessellationOptions& options) {
    const Scalar L = std::max<Scalar>(approximate_arclength, 0.0);
    const Scalar e = std::max<Scalar>(options.chordal_error, std::numeric_limits<Scalar>::epsilon());
    const std::size_t n_len = segments_for_length(L, options);
    if (!(L > static_cast<Scalar>(1e-24))) {
        return std::max<std::size_t>(2, std::min<std::size_t>(256, n_len));
    }
    const Scalar two_pi = 2.0 * std::acos(-1.0);
    const Scalar R_eff = std::max(L / two_pi, e);
    const std::size_t n_circ = segments_for_circle(R_eff, options);
    const std::size_t n = std::max(n_len, n_circ);
    return std::max<std::size_t>(2, std::min<std::size_t>(256, n));
}

MeshRecord tessellate_box(const BodyRecord& body, const TessellationOptions& options) {
    MeshRecord mesh;
    mesh.source_body = BodyId{0};
    mesh.label = "mesh_from_box";
    mesh.bbox = body.bbox;

    // 6 faces as grids, then weld boundary vertices to keep a single connected component.
    const auto dx = std::max<Scalar>(0.0, body.a);
    const auto dy = std::max<Scalar>(0.0, body.b);
    const auto dz = std::max<Scalar>(0.0, body.c);
    const auto nx = std::max<std::size_t>(1, segments_for_length(dx, options));
    const auto ny = std::max<std::size_t>(1, segments_for_length(dy, options));
    const auto nz = std::max<std::size_t>(1, segments_for_length(dz, options));
    if (2 * ((nx + 1) * (ny + 1) + (nx + 1) * (nz + 1) + (ny + 1) * (nz + 1)) > 1000000) return {};

    // Precompute axis coordinates so shared edges match bitwise across faces.
    const auto o = body.origin;
    std::vector<Scalar> xs(nx + 1), ys(ny + 1), zs(nz + 1);
    for (std::size_t i = 0; i <= nx; ++i) xs[i] = o.x + dx * (static_cast<Scalar>(i) / static_cast<Scalar>(nx));
    for (std::size_t i = 0; i <= ny; ++i) ys[i] = o.y + dy * (static_cast<Scalar>(i) / static_cast<Scalar>(ny));
    for (std::size_t i = 0; i <= nz; ++i) zs[i] = o.z + dz * (static_cast<Scalar>(i) / static_cast<Scalar>(nz));

    // Emit a face grid by selecting which axis corresponds to (u,v) and holding the third axis constant.
    enum class Axis { X, Y, Z };
    auto emit_face_grid = [&](Axis u_axis, Axis v_axis, Scalar w_value,
                              const Vec3& n, std::size_t nu, std::size_t nv) {
        const auto base = static_cast<Index>(mesh.vertices.size());
        auto coord = [&](Axis a, std::size_t idx_u, std::size_t idx_v) -> Scalar {
            switch (a) {
                case Axis::X: return (u_axis == Axis::X) ? xs[idx_u] : (v_axis == Axis::X) ? xs[idx_v] : w_value;
                case Axis::Y: return (u_axis == Axis::Y) ? ys[idx_u] : (v_axis == Axis::Y) ? ys[idx_v] : w_value;
                case Axis::Z: return (u_axis == Axis::Z) ? zs[idx_u] : (v_axis == Axis::Z) ? zs[idx_v] : w_value;
            }
            return 0.0;
        };
        for (std::size_t j = 0; j <= nv; ++j) {
            const auto tv = (nv == 0) ? 0.0 : (static_cast<Scalar>(j) / static_cast<Scalar>(nv));
            for (std::size_t i = 0; i <= nu; ++i) {
                const auto tu = (nu == 0) ? 0.0 : (static_cast<Scalar>(i) / static_cast<Scalar>(nu));
                const Point3 p {
                    coord(Axis::X, i, j),
                    coord(Axis::Y, i, j),
                    coord(Axis::Z, i, j),
                };
                mesh.vertices.push_back(p);
                if (options.compute_normals) mesh.normals.push_back(n);
                if (options.generate_texcoords) {
                    mesh.texcoords.push_back(Point2{tu, tv});
                }
            }
        }
        for (std::size_t j = 0; j < nv; ++j) {
            for (std::size_t i = 0; i < nu; ++i) {
                const auto i00 = base + static_cast<Index>(j * (nu + 1) + i);
                const auto i10 = base + static_cast<Index>(j * (nu + 1) + i + 1);
                const auto i01 = base + static_cast<Index>((j + 1) * (nu + 1) + i);
                const auto i11 = base + static_cast<Index>((j + 1) * (nu + 1) + i + 1);
                const auto geometric = cross(subtract(mesh.vertices[i10],mesh.vertices[i00]),
                                             subtract(mesh.vertices[i01],mesh.vertices[i00]));
                if (dot(geometric,n) >= 0.0) add_quad(mesh.indices,i00,i10,i11,i01);
                else add_quad(mesh.indices,i00,i01,i11,i10);
            }
        }
    };

    // +Z, -Z
    emit_face_grid(Axis::X, Axis::Y, zs.back(), Vec3{0,0,1}, nx, ny);
    emit_face_grid(Axis::X, Axis::Y, zs.front(), Vec3{0,0,-1}, nx, ny);
    // +Y, -Y
    emit_face_grid(Axis::X, Axis::Z, ys.back(), Vec3{0,1,0}, nx, nz);
    emit_face_grid(Axis::X, Axis::Z, ys.front(), Vec3{0,-1,0}, nx, nz);
    // +X, -X
    emit_face_grid(Axis::Y, Axis::Z, xs.back(), Vec3{1,0,0}, ny, nz);
    emit_face_grid(Axis::Y, Axis::Z, xs.front(), Vec3{-1,0,0}, ny, nz);

    weld_mesh_vertices_quantized(mesh,options.compute_normals,0.0,options.weld_shading_split_angle_deg);
    if (!options.generate_texcoords) {
        mesh.texcoords.clear();
    }
    return mesh;
}

void weld_mesh_vertices_quantized(MeshRecord& mesh, bool weld_normals, Scalar position_quant_step,
                                  Scalar shading_split_angle_deg) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return;
    }
    const bool exact_positions = position_quant_step == 0.0;
    const Scalar q =
        (position_quant_step > 0.0 && std::isfinite(position_quant_step)) ? position_quant_step : 1e-7;
    const auto representable = [q](Scalar v) {
        return std::isfinite(v) && std::abs(v / q) < static_cast<Scalar>(std::numeric_limits<std::int64_t>::max()) * 0.5;
    };
    if (!exact_positions) {
        for (const auto& p : mesh.vertices)
            if (!representable(p.x) || !representable(p.y) || !representable(p.z)) return;
        for (const auto& uv : mesh.texcoords)
            if (!representable(uv.x) || !representable(uv.y)) return;
    }
    auto quant = [q](Scalar v) -> std::int64_t {
        return static_cast<std::int64_t>(std::llround(v / q));
    };
    std::unordered_map<std::string, Index> map;
    map.reserve(mesh.vertices.size());
    std::vector<Point3> new_vertices;
    std::vector<Vec3> new_normals;
    std::vector<Point2> new_uvs;
    std::vector<Vec3> rep_normal;
    std::vector<Index> remap(mesh.vertices.size(), 0);
    new_vertices.reserve(mesh.vertices.size());
    if (weld_normals && !mesh.normals.empty()) new_normals.reserve(mesh.vertices.size());
    if (!mesh.texcoords.empty()) new_uvs.reserve(mesh.vertices.size());
    const bool use_uv_key =
        !mesh.texcoords.empty() && mesh.texcoords.size() == mesh.vertices.size();
    const bool split_by_shading =
        weld_normals && !mesh.normals.empty() && mesh.normals.size() == mesh.vertices.size() &&
        shading_split_angle_deg + static_cast<Scalar>(1e-6) < static_cast<Scalar>(180);
    const Scalar cos_thresh =
        split_by_shading ? std::cos(radians_from_degrees(
                               clamp(shading_split_angle_deg, static_cast<Scalar>(0), static_cast<Scalar>(180))))
                         : static_cast<Scalar>(-2);

    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto& p = mesh.vertices[i];
        std::string base_key;
        if (exact_positions) {
            std::ostringstream key;
            key.imbue(std::locale::classic());
            key << std::hexfloat << (p.x == 0.0 ? 0.0 : p.x) << "," << (p.y == 0.0 ? 0.0 : p.y)
                << "," << (p.z == 0.0 ? 0.0 : p.z);
            if (use_uv_key) key << "|uv=" << mesh.texcoords[i].x << "," << mesh.texcoords[i].y;
            base_key = key.str();
        } else {
            base_key = std::to_string(quant(p.x)) + "," + std::to_string(quant(p.y)) + "," + std::to_string(quant(p.z));
            if (use_uv_key) {
                const auto& uv = mesh.texcoords[i];
                base_key += "|uv=" + std::to_string(quant(uv.x)) + "," + std::to_string(quant(uv.y));
            }
        }

        auto push_new_vertex = [&](const std::string& key) -> Index {
            const auto ni = static_cast<Index>(new_vertices.size());
            map.emplace(key, ni);
            new_vertices.push_back(p);
            if (weld_normals && !mesh.normals.empty()) {
                new_normals.push_back(mesh.normals[i]);
                if (split_by_shading) {
                    rep_normal.push_back(normalize(mesh.normals[i]));
                }
            }
            if (use_uv_key) {
                new_uvs.push_back(mesh.texcoords[i]);
            }
            return ni;
        };

        if (!split_by_shading) {
            const auto it = map.find(base_key);
            if (it == map.end()) {
                remap[i] = push_new_vertex(base_key);
            } else {
                remap[i] = it->second;
                if (weld_normals && !mesh.normals.empty()) {
                    auto& acc = new_normals[it->second];
                    acc.x += mesh.normals[i].x;
                    acc.y += mesh.normals[i].y;
                    acc.z += mesh.normals[i].z;
                }
            }
            continue;
        }

        bool placed = false;
        for (unsigned s = 0; s < 8192u && !placed; ++s) {
            const std::string key = s == 0 ? base_key : base_key + "|sh=" + std::to_string(s);
            const auto it = map.find(key);
            if (it == map.end()) {
                remap[i] = push_new_vertex(key);
                placed = true;
            } else {
                const Vec3 ni = normalize(mesh.normals[i]);
                const Vec3 nr = rep_normal[static_cast<std::size_t>(it->second)];
                if (dot(ni, nr) >= cos_thresh) {
                    remap[i] = it->second;
                    auto& acc = new_normals[it->second];
                    acc.x += mesh.normals[i].x;
                    acc.y += mesh.normals[i].y;
                    acc.z += mesh.normals[i].z;
                    placed = true;
                }
            }
        }
        if (!placed) {
            remap[i] = push_new_vertex(base_key + "|sh=ovf");
        }
    }
    for (auto& idx : mesh.indices) {
        idx = remap[static_cast<std::size_t>(idx)];
    }
    mesh.vertices = std::move(new_vertices);
    if (weld_normals && !mesh.normals.empty()) {
        for (auto& n : new_normals) n = normalize(n);
        mesh.normals = std::move(new_normals);
    }
    if (use_uv_key) {
        mesh.texcoords = std::move(new_uvs);
    }
}

void weld_mesh_vertices(MeshRecord& mesh, const TessellationOptions& options) {
    constexpr Scalar q = 1e-7;
    weld_mesh_vertices_quantized(mesh, options.compute_normals, q, options.weld_shading_split_angle_deg);
}

MeshRecord tessellate_sphere(const BodyRecord& body, const TessellationOptions& options) {
    MeshRecord mesh;
    mesh.source_body = BodyId{0};
    mesh.label = "mesh_from_sphere";
    mesh.bbox = body.bbox;

    const auto center = body.origin;
    const auto r = std::max<Scalar>(body.a, std::numeric_limits<Scalar>::epsilon());
    auto directional = options;
    directional.chordal_error *= 0.125;
    directional.angular_error *= 0.5;
    const auto nu = segments_for_circle(r, directional);
    const auto nv = std::max<std::size_t>(4, (nu + 1) / 2);
    if (nu == 0 || (nu + 1) * (nv + 1) > 1000000) return {};

    // UV sphere: u in [0,2pi], v in [0,pi]
    const auto base = static_cast<Index>(0);
    (void)base;
    for (std::size_t j = 0; j <= nv; ++j) {
        const auto v = (static_cast<Scalar>(j) / static_cast<Scalar>(nv)) * kPi;
        const auto sv = (j == 0 || j == nv) ? 0.0 : std::sin(v);
        const auto cv = std::cos(v);
        for (std::size_t i = 0; i <= nu; ++i) {
            const auto u = (static_cast<Scalar>(i) / static_cast<Scalar>(nu)) * (2.0 * kPi);
            const auto su = std::sin(u);
            const auto cu = std::cos(u);
            const Vec3 n {cu * sv, su * sv, cv};
            const Point3 p {center.x + r * n.x, center.y + r * n.y, center.z + r * n.z};
            mesh.vertices.push_back(p);
            if (options.compute_normals) {
                mesh.normals.push_back(n);
            }
            if (options.generate_texcoords) {
                mesh.texcoords.push_back(Point2{static_cast<Scalar>(i) / static_cast<Scalar>(nu),
                                                static_cast<Scalar>(j) / static_cast<Scalar>(nv)});
            }
        }
    }
    for (std::size_t j = 0; j < nv; ++j) {
        for (std::size_t i = 0; i < nu; ++i) {
            const auto row0 = static_cast<Index>(j * (nu + 1));
            const auto row1 = static_cast<Index>((j + 1) * (nu + 1));
            const auto i00 = row0 + static_cast<Index>(i);
            const auto i10 = row0 + static_cast<Index>(i + 1);
            const auto i01 = row1 + static_cast<Index>(i);
            const auto i11 = row1 + static_cast<Index>(i + 1);
            if (j == 0) mesh.indices.insert(mesh.indices.end(), {i00, i01, i11});
            else if (j + 1 == nv) mesh.indices.insert(mesh.indices.end(), {i00, i01, i10});
            else add_quad(mesh.indices, i00, i01, i11, i10);
        }
    }
    return mesh;
}

MeshRecord tessellate_cylinder(const BodyRecord& body, const TessellationOptions& options) {
    MeshRecord mesh;
    mesh.source_body = BodyId{0};
    mesh.label = "mesh_from_cylinder";
    mesh.bbox = body.bbox;

    const auto center = body.origin;
    const auto axis = normalize(body.axis);
    const auto r = std::max<Scalar>(body.a, std::numeric_limits<Scalar>::epsilon());
    const auto h = std::max<Scalar>(body.b, std::numeric_limits<Scalar>::epsilon());
    const auto nu = segments_for_circle(r, options);
    const auto nv = std::max<std::size_t>(1, segments_for_length(h, options));
    if (nu == 0 || (nu + 1) * (nv + 3) + 2 > 1000000) return {};

    const auto udir = pick_orthogonal_unit(axis);
    const auto vdir = normalize(cross(axis, udir));
    const auto half = scale(axis, h * 0.5);
    const auto base_center = Point3{center.x - half.x, center.y - half.y, center.z - half.z};

    // Side surface (shared seam duplicated: nu+1)
    for (std::size_t j = 0; j <= nv; ++j) {
        const auto tz = (static_cast<Scalar>(j) / static_cast<Scalar>(nv));
        const auto c = add_point_vec(base_center, scale(axis, h * tz));
        for (std::size_t i = 0; i <= nu; ++i) {
            const auto u = (static_cast<Scalar>(i) / static_cast<Scalar>(nu)) * (2.0 * kPi);
            const auto cu = std::cos(u);
            const auto su = std::sin(u);
            const Vec3 radial = Vec3{udir.x * cu + vdir.x * su,
                                     udir.y * cu + vdir.y * su,
                                     udir.z * cu + vdir.z * su};
            const Point3 p = add_point_vec(c, scale(radial, r));
            mesh.vertices.push_back(p);
            if (options.compute_normals) {
                mesh.normals.push_back(radial);
            }
            if (options.generate_texcoords) {
                mesh.texcoords.push_back(Point2{static_cast<Scalar>(i) / static_cast<Scalar>(nu), tz});
            }
        }
    }
    for (std::size_t j = 0; j < nv; ++j) {
        for (std::size_t i = 0; i < nu; ++i) {
            const auto row0 = static_cast<Index>(j * (nu + 1));
            const auto row1 = static_cast<Index>((j + 1) * (nu + 1));
            const auto i00 = row0 + static_cast<Index>(i);
            const auto i10 = row0 + static_cast<Index>(i + 1);
            const auto i01 = row1 + static_cast<Index>(i);
            const auto i11 = row1 + static_cast<Index>(i + 1);
            add_quad(mesh.indices, i00, i10, i11, i01);
        }
    }

    // Caps (fan triangulation, separate vertices to get flat normals)
    const auto cap_center_top = add_point_vec(base_center, scale(axis, h));
    const auto cap_center_bottom = base_center;
    const auto bottom_center_idx = static_cast<Index>(mesh.vertices.size());
    mesh.vertices.push_back(cap_center_bottom);
    if (options.compute_normals) mesh.normals.push_back(scale(axis, -1.0));
    if (options.generate_texcoords) mesh.texcoords.push_back(Point2{0.5, 0.5});
    const auto top_center_idx = static_cast<Index>(mesh.vertices.size());
    mesh.vertices.push_back(cap_center_top);
    if (options.compute_normals) mesh.normals.push_back(axis);
    if (options.generate_texcoords) mesh.texcoords.push_back(Point2{0.5, 0.5});

    const auto bottom_ring_base = static_cast<Index>(mesh.vertices.size());
    for (std::size_t i = 0; i <= nu; ++i) {
        const auto u = (static_cast<Scalar>(i) / static_cast<Scalar>(nu)) * (2.0 * kPi);
        const auto cu = std::cos(u);
        const auto su = std::sin(u);
        const Vec3 radial = Vec3{udir.x * cu + vdir.x * su,
                                 udir.y * cu + vdir.y * su,
                                 udir.z * cu + vdir.z * su};
        mesh.vertices.push_back(add_point_vec(cap_center_bottom, scale(radial, r)));
        if (options.compute_normals) mesh.normals.push_back(scale(axis, -1.0));
        if (options.generate_texcoords) mesh.texcoords.push_back(Point2{0.5 + 0.5 * cu, 0.5 + 0.5 * su});
    }
    const auto top_ring_base = static_cast<Index>(mesh.vertices.size());
    for (std::size_t i = 0; i <= nu; ++i) {
        const auto u = (static_cast<Scalar>(i) / static_cast<Scalar>(nu)) * (2.0 * kPi);
        const auto cu = std::cos(u);
        const auto su = std::sin(u);
        const Vec3 radial = Vec3{udir.x * cu + vdir.x * su,
                                 udir.y * cu + vdir.y * su,
                                 udir.z * cu + vdir.z * su};
        mesh.vertices.push_back(add_point_vec(cap_center_top, scale(radial, r)));
        if (options.compute_normals) mesh.normals.push_back(axis);
        if (options.generate_texcoords) mesh.texcoords.push_back(Point2{0.5 + 0.5 * cu, 0.5 + 0.5 * su});
    }
    for (std::size_t i = 0; i < nu; ++i) {
        const auto b0 = bottom_ring_base + static_cast<Index>(i);
        const auto b1 = bottom_ring_base + static_cast<Index>(i + 1);
        // bottom faces outward along -axis, winding chosen accordingly
        mesh.indices.insert(mesh.indices.end(), {bottom_center_idx, b1, b0});

        const auto t0 = top_ring_base + static_cast<Index>(i);
        const auto t1 = top_ring_base + static_cast<Index>(i + 1);
        mesh.indices.insert(mesh.indices.end(), {top_center_idx, t0, t1});
    }

    return mesh;
}

MeshRecord tessellate_cone(const BodyRecord& body, const TessellationOptions& options) {
    MeshRecord mesh;
    mesh.source_body = BodyId{0};
    mesh.label = "mesh_from_cone";
    mesh.bbox = body.bbox;

    const auto apex = body.origin;
    const auto axis = normalize(body.axis);
    const auto semi_angle = std::max<Scalar>(body.a, 1e-6);
    const auto h = std::max<Scalar>(body.b, std::numeric_limits<Scalar>::epsilon());
    const auto r_base = std::max<Scalar>(0.0, h * std::tan(semi_angle));
    const auto nu = segments_for_circle(std::max<Scalar>(r_base, 1e-6), options);
    const auto nv = std::max<std::size_t>(1, segments_for_length(h, options));
    if (nu == 0 || (nu + 1) * (nv + 3) + 2 > 1000000) return {};

    const auto udir = pick_orthogonal_unit(axis);
    const auto vdir = normalize(cross(axis, udir));

    // Side surface (apex to base)
    for (std::size_t j = 0; j <= nv; ++j) {
        const auto t = (static_cast<Scalar>(j) / static_cast<Scalar>(nv));
        const auto r = r_base * t;
        const auto c = add_point_vec(apex, scale(axis, h * t));
        for (std::size_t i = 0; i <= nu; ++i) {
            const auto u = (static_cast<Scalar>(i) / static_cast<Scalar>(nu)) * (2.0 * kPi);
            const auto cu = std::cos(u);
            const auto su = std::sin(u);
            const Vec3 radial = Vec3{udir.x * cu + vdir.x * su,
                                     udir.y * cu + vdir.y * su,
                                     udir.z * cu + vdir.z * su};
            const Point3 p = add_point_vec(c, scale(radial, r));
            mesh.vertices.push_back(p);
            if (options.compute_normals) {
                // Analytic cone normal (unit): normalize(radial * cos(a) - axis * sin(a))
                const Vec3 n = normalize(Vec3{
                    radial.x * std::cos(semi_angle) - axis.x * std::sin(semi_angle),
                    radial.y * std::cos(semi_angle) - axis.y * std::sin(semi_angle),
                    radial.z * std::cos(semi_angle) - axis.z * std::sin(semi_angle)
                });
                mesh.normals.push_back(n);
            }
            if (options.generate_texcoords) {
                mesh.texcoords.push_back(Point2{static_cast<Scalar>(i) / static_cast<Scalar>(nu), t});
            }
        }
    }
    for (std::size_t j = 0; j < nv; ++j) {
        for (std::size_t i = 0; i < nu; ++i) {
            const auto row0 = static_cast<Index>(j * (nu + 1));
            const auto row1 = static_cast<Index>((j + 1) * (nu + 1));
            const auto i00 = row0 + static_cast<Index>(i);
            const auto i10 = row0 + static_cast<Index>(i + 1);
            const auto i01 = row1 + static_cast<Index>(i);
            const auto i11 = row1 + static_cast<Index>(i + 1);
            if (j == 0) mesh.indices.insert(mesh.indices.end(), {i00, i11, i01});
            else add_quad(mesh.indices, i00, i10, i11, i01);
        }
    }

    // Base cap
    const auto base_center = add_point_vec(apex, scale(axis, h));
    const auto base_center_idx = static_cast<Index>(mesh.vertices.size());
    mesh.vertices.push_back(base_center);
    if (options.compute_normals) mesh.normals.push_back(axis);
    if (options.generate_texcoords) mesh.texcoords.push_back(Point2{0.5, 0.5});
    const auto ring_base = static_cast<Index>(mesh.vertices.size());
    for (std::size_t i = 0; i <= nu; ++i) {
        const auto u = (static_cast<Scalar>(i) / static_cast<Scalar>(nu)) * (2.0 * kPi);
        const auto cu = std::cos(u);
        const auto su = std::sin(u);
        const Vec3 radial = Vec3{udir.x * cu + vdir.x * su,
                                 udir.y * cu + vdir.y * su,
                                 udir.z * cu + vdir.z * su};
        mesh.vertices.push_back(add_point_vec(base_center, scale(radial, r_base)));
        if (options.compute_normals) mesh.normals.push_back(axis);
        if (options.generate_texcoords) mesh.texcoords.push_back(Point2{0.5 + 0.5 * cu, 0.5 + 0.5 * su});
    }
    for (std::size_t i = 0; i < nu; ++i) {
        const auto i0 = ring_base + static_cast<Index>(i);
        const auto i1 = ring_base + static_cast<Index>(i + 1);
        // outward normal is +axis (pointing away from cone interior for current convention)
        mesh.indices.insert(mesh.indices.end(), {base_center_idx, i0, i1});
    }
    return mesh;
}

MeshRecord tessellate_torus(const BodyRecord& body, const TessellationOptions& options) {
    MeshRecord mesh;
    mesh.source_body = BodyId{0};
    mesh.label = "mesh_from_torus";
    mesh.bbox = body.bbox;

    const auto center = body.origin;
    const auto axis = normalize(body.axis);
    const auto R = std::max<Scalar>(body.a, std::numeric_limits<Scalar>::epsilon());
    const auto r = std::max<Scalar>(body.b, std::numeric_limits<Scalar>::epsilon());
    auto directional = options;
    directional.chordal_error *= 0.125;
    directional.angular_error *= 0.5;
    const auto nu = segments_for_circle(R + r, directional);
    const auto nv = segments_for_circle(r, directional);
    if (nu == 0 || nv == 0 || (nu + 1) * (nv + 1) > 1000000) return {};

    const auto udir = pick_orthogonal_unit(axis);
    const auto vdir = normalize(cross(axis, udir));

    for (std::size_t j = 0; j <= nv; ++j) {
        const auto v = (static_cast<Scalar>(j) / static_cast<Scalar>(nv)) * (2.0 * kPi);
        const auto cv = std::cos(v);
        const auto sv = std::sin(v);
        for (std::size_t i = 0; i <= nu; ++i) {
            const auto u = (static_cast<Scalar>(i) / static_cast<Scalar>(nu)) * (2.0 * kPi);
            const auto cu = std::cos(u);
            const auto su = std::sin(u);
            const Vec3 dir_major = Vec3{udir.x * cu + vdir.x * su,
                                        udir.y * cu + vdir.y * su,
                                        udir.z * cu + vdir.z * su};
            const Vec3 dir_minor = Vec3{
                dir_major.x * cv + axis.x * sv,
                dir_major.y * cv + axis.y * sv,
                dir_major.z * cv + axis.z * sv
            };
            const Point3 p = add_point_vec(add_point_vec(center, scale(dir_major, R)), scale(dir_minor, r));
            mesh.vertices.push_back(p);
            if (options.compute_normals) {
                const Vec3 n = normalize(dir_minor);
                mesh.normals.push_back(n);
            }
            if (options.generate_texcoords) {
                mesh.texcoords.push_back(Point2{static_cast<Scalar>(i) / static_cast<Scalar>(nu),
                                                static_cast<Scalar>(j) / static_cast<Scalar>(nv)});
            }
        }
    }
    for (std::size_t j = 0; j < nv; ++j) {
        for (std::size_t i = 0; i < nu; ++i) {
            const auto row0 = static_cast<Index>(j * (nu + 1));
            const auto row1 = static_cast<Index>((j + 1) * (nu + 1));
            const auto i00 = row0 + static_cast<Index>(i);
            const auto i10 = row0 + static_cast<Index>(i + 1);
            const auto i01 = row1 + static_cast<Index>(i);
            const auto i11 = row1 + static_cast<Index>(i + 1);
            add_quad(mesh.indices, i00, i10, i11, i01);
        }
    }
    return mesh;
}

namespace rep_internal {

Vec3 newell_normal(const std::vector<Point3>& poly) {
    Vec3 n{0.0, 0.0, 0.0};
    if (poly.size() < 3) return n;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const auto& p0 = poly[i];
        const auto& p1 = poly[(i + 1) % poly.size()];
        n.x += (p0.y - p1.y) * (p0.z + p1.z);
        n.y += (p0.z - p1.z) * (p0.x + p1.x);
        n.z += (p0.x - p1.x) * (p0.y + p1.y);
    }
    return normalize(n);
}

struct OrthoFrame {
    Vec3 u {1.0, 0.0, 0.0};
    Vec3 v {0.0, 1.0, 0.0};
    Vec3 w {0.0, 0.0, 1.0};
};

OrthoFrame make_frame_from_w(Vec3 axis_w) {
    const auto w = normalize(axis_w);
    const Vec3 ref = (std::abs(w.z) < 0.9) ? Vec3{0.0, 0.0, 1.0} : Vec3{0.0, 1.0, 0.0};
    const auto u = normalize(cross(ref, w));
    const auto v = normalize(cross(w, u));
    return OrthoFrame{u, v, w};
}

Point3 point_from_local(const Point3& origin, const OrthoFrame& frame, Scalar x, Scalar y,
                        Scalar z) {
    return Point3{origin.x + frame.u.x * x + frame.v.x * y + frame.w.x * z,
                  origin.y + frame.u.y * x + frame.v.y * y + frame.w.y * z,
                  origin.z + frame.u.z * x + frame.v.z * y + frame.w.z * z};
}

Vec3 vec_from_local(const OrthoFrame& frame, Scalar x, Scalar y, Scalar z) {
    return Vec3{frame.u.x * x + frame.v.x * y + frame.w.x * z,
                frame.u.y * x + frame.v.y * y + frame.w.y * z,
                frame.u.z * x + frame.v.z * y + frame.w.z * z};
}

bool eval_surface_grid_point(const KernelState* state, SurfaceId surface_id, const SurfaceRecord& surface,
                             Scalar u, Scalar v, Point3& p, Vec3& n) {
    const auto frame = make_frame_from_w(surface.normal);
    switch (surface.kind) {
    case SurfaceKind::Plane:
        p = point_from_local(surface.origin, frame, u, v, 0.0);
        n = frame.w;
        return true;
    case SurfaceKind::Sphere: {
        const auto R = std::max<Scalar>(surface.radius_a, std::numeric_limits<Scalar>::epsilon());
        p = Point3{surface.origin.x + R * std::cos(u) * std::sin(v),
                   surface.origin.y + R * std::sin(u) * std::sin(v),
                   surface.origin.z + R * std::cos(v)};
        n = normalize(Vec3{p.x - surface.origin.x, p.y - surface.origin.y, p.z - surface.origin.z});
        return true;
    }
    case SurfaceKind::Cylinder: {
        const auto R = std::max<Scalar>(surface.radius_a, std::numeric_limits<Scalar>::epsilon());
        p = point_from_local(surface.origin, frame, R * std::cos(u), R * std::sin(u), v);
        n = vec_from_local(frame, std::cos(u), std::sin(u), 0.0);
        return true;
    }
    case SurfaceKind::Cone: {
        const auto slope = std::tan(surface.semi_angle);
        const auto radial = slope * v;
        const Vec3 radial_dir = vec_from_local(frame, std::cos(u), std::sin(u), 0.0);
        p = point_from_local(surface.origin, frame, radial * std::cos(u), radial * std::sin(u), v);
        const Vec3 du =
            vec_from_local(frame, -radial * std::sin(u), radial * std::cos(u), 0.0);
        const Vec3 dv = vec_from_local(frame, slope * std::cos(u), slope * std::sin(u), 1.0);
        n = normalize(cross(du, dv));
        if (dot(n, radial_dir) < 0.0) {
            n = scale(n, -1.0);
        }
        return true;
    }
    case SurfaceKind::Torus: {
        const auto R = std::max<Scalar>(surface.radius_a, std::numeric_limits<Scalar>::epsilon());
        const auto r = std::max<Scalar>(surface.radius_b, std::numeric_limits<Scalar>::epsilon());
        const auto udir = pick_orthogonal_unit(surface.axis);
        const auto vdir = normalize(cross(surface.axis, udir));
        const Vec3 dir_major = Vec3{udir.x * std::cos(u) + vdir.x * std::sin(u),
                                    udir.y * std::cos(u) + vdir.y * std::sin(u),
                                    udir.z * std::cos(u) + vdir.z * std::sin(u)};
        const Vec3 dir_minor = Vec3{dir_major.x * std::cos(v) + surface.axis.x * std::sin(v),
                                     dir_major.y * std::cos(v) + surface.axis.y * std::sin(v),
                                     dir_major.z * std::cos(v) + surface.axis.z * std::sin(v)};
        p = Point3{surface.origin.x + dir_major.x * R + dir_minor.x * r,
                   surface.origin.y + dir_major.y * R + dir_minor.y * r,
                   surface.origin.z + dir_major.z * R + dir_minor.z * r};
        n = normalize(dir_minor);
        return true;
    }
    case SurfaceKind::Bezier: {
        Point3 out_p;
        Vec3 du{};
        Vec3 dv{};
        if (!geo_internal::bezier_tensor_surface_eval_with_partials(surface, u, v, out_p, du, dv)) {
            return false;
        }
        p = out_p;
        n = normalize(cross(du, dv));
        if (!(dot(n, n) > 1e-30) || !std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) {
            n = Vec3{0.0, 0.0, 1.0};
        }
        return true;
    }
    case SurfaceKind::BSpline:
    case SurfaceKind::Nurbs: {
        Point3 out_p;
        Vec3 du{};
        Vec3 dv{};
        if (!geo_internal::nurbs_tensor_surface_eval_with_partials(surface, u, v, out_p, du, dv)) {
            return false;
        }
        p = out_p;
        n = normalize(cross(du, dv));
        if (!(dot(n, n) > 1e-30) || !std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) {
            n = Vec3{0.0, 0.0, 1.0};
        }
        return true;
    }
    case SurfaceKind::Revolved:
    case SurfaceKind::Swept:
    case SurfaceKind::Offset: {
        if (state == nullptr || surface_id.value == 0) {
            return false;
        }
        return geo_internal::rep_surface_eval_grid_point(const_cast<KernelState*>(state), surface_id, u, v,
                                                           p, n);
    }
    default:
        return false;
    }
}

bool finite_point(const Point3& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// Certify the actual edge support, independently of the display error budget.
bool straight_edge_matches(const KernelState& state, const EdgeRecord& edge, Scalar tolerance) {
    const auto curve = state.curves.find(edge.curve_id.value);
    const auto a = state.vertices.find(edge.v0.value), b = state.vertices.find(edge.v1.value);
    if (curve == state.curves.end() || a == state.vertices.end() || b == state.vertices.end() ||
        !finite_point(a->second.point) || !finite_point(b->second.point)) return false;
    const auto& c = curve->second;
    Point3 origin = c.origin;
    Vec3 direction = c.direction;
    if (c.kind == CurveKind::LineSegment) {
        if (c.poles.size() != 2) return false;
        origin = c.poles.front();
        direction = subtract(c.poles.back(), origin);
    } else if (c.kind != CurveKind::Line) return false;
    const Scalar d2 = dot(direction, direction);
    if (!finite_point(origin) || !std::isfinite(d2) || !(d2 > 0.0)) return false;
    const auto parameter = [&](const Point3& point, Scalar& t) {
        t = dot(subtract(point, origin), direction) / d2;
        return std::isfinite(t) && norm(subtract(point, add_point_vec(origin, scale(direction, t)))) <= tolerance;
    };
    // Matching tolerance bounds support residuals, not the minimum feature
    // size: a valid subdivided edge can be shorter than the model tolerance.
    const Scalar edge_length = norm(subtract(a->second.point,b->second.point));
    Scalar t0{}, t1{};
    if (!std::isfinite(edge_length) || !(edge_length > 0.0) ||
        !parameter(a->second.point, t0) || !parameter(b->second.point, t1)) return false;
    if (c.kind == CurveKind::LineSegment &&
        (std::min(t0, t1) < -tolerance / std::sqrt(d2) || std::max(t0, t1) > 1.0 + tolerance / std::sqrt(d2))) return false;
    if (edge.has_parameter_interval) {
        if (!std::isfinite(edge.start_parameter) || !std::isfinite(edge.end_parameter) ||
            norm(scale(direction, t0 - edge.start_parameter)) > tolerance ||
            norm(scale(direction, t1 - edge.end_parameter)) > tolerance) return false;
    }
    return true;
}

// Shared arc stations keep planar caps and their cylindrical walls welded.
// Only finite, explicitly trimmed analytic circles are accepted here.
bool sample_circle_edge(const KernelState& state, const EdgeRecord& edge,
                        const TessellationOptions& options, Scalar tolerance,
                        std::vector<Point3>& points) {
    const auto curve = state.curves.find(edge.curve_id.value);
    const auto start = state.vertices.find(edge.v0.value), end = state.vertices.find(edge.v1.value);
    if (curve == state.curves.end() || start == state.vertices.end() || end == state.vertices.end() ||
        curve->second.kind != CurveKind::Circle || !edge.has_parameter_interval) return false;
    const auto& c = curve->second;
    const Scalar span = edge.end_parameter - edge.start_parameter;
    if (!finite_point(c.origin) || !finite_point(start->second.point) || !finite_point(end->second.point) ||
        !(c.radius > 0.0) || !std::isfinite(c.radius) || !std::isfinite(edge.start_parameter) ||
        !std::isfinite(edge.end_parameter) || !(std::abs(span) > 0.0) || std::abs(span) > 2.0 * kPi ||
        !std::isfinite(norm(c.axis_u)) || !std::isfinite(norm(c.axis_v)) ||
        std::abs(norm(c.axis_u) - 1.0) > 1e-12 || std::abs(norm(c.axis_v) - 1.0) > 1e-12 ||
        std::abs(dot(c.axis_u, c.axis_v)) > 1e-12) return false;
    const auto circle_segments = segments_for_circle(c.radius, options);
    if (circle_segments == 0) return false;
    const auto count = std::max<std::size_t>(1, static_cast<std::size_t>(
        std::ceil(static_cast<Scalar>(circle_segments) * std::abs(span) / (2.0 * kPi))));
    if (count > 4096) return false;
    points.clear();
    points.reserve(count + 1);
    for (std::size_t i = 0; i <= count; ++i) {
        const Scalar t = edge.start_parameter + span * static_cast<Scalar>(i) / static_cast<Scalar>(count);
        const Scalar a = c.radius * std::cos(t), b = c.radius * std::sin(t);
        const auto p = add_point_vec(c.origin,
            Vec3{c.axis_u.x * a + c.axis_v.x * b, c.axis_u.y * a + c.axis_v.y * b,
                 c.axis_u.z * a + c.axis_v.z * b});
        if (!finite_point(p)) return false;
        points.push_back(p);
    }
    if (norm(subtract(points.front(), start->second.point)) > tolerance ||
        norm(subtract(points.back(), end->second.point)) > tolerance) return false;
    // Endpoints are the topology authority, shared by every incident face.
    points.front() = start->second.point;
    points.back() = end->second.point;
    return true;
}

MeshRecord tessellate_face_planar_mesh(const KernelState& state, FaceId face_id, const TessellationOptions& options) {
    MeshRecord mesh;
    mesh.source_body = BodyId{0};
    mesh.label = "mesh_from_face_planar";

    const auto face = state.faces.find(face_id.value);
    if (face == state.faces.end()) return mesh;
    const auto surface = state.surfaces.find(face->second.surface_id.value);
    if (surface == state.surfaces.end() || surface->second.kind != SurfaceKind::Plane) return mesh;
    const auto support_normal = normalize(surface->second.normal);
    if (!(norm(support_normal) > 0.0)) return mesh;
    const Scalar tolerance = std::min(std::max<Scalar>(1e-12, std::abs(state.config.tolerance.linear)),options.chordal_error * 0.25);
    std::vector<LoopId> loops{face->second.outer_loop};
    loops.insert(loops.end(), face->second.inner_loops.begin(), face->second.inner_loops.end());
    std::vector<std::vector<Point3>> rings;
    for (const auto loop_id : loops) {
        std::vector<Point3> ring;
        std::string reason;
        const auto loop_record = state.loops.find(loop_id.value);
        if (loop_record == state.loops.end() ||
            !topo_internal::validate_loop_record(state, loop_record->second, reason)) return {};
        std::vector<Point3> vertices;
        if (!topo_internal::loop_vertex_chain_3d(state, loop_id, vertices, reason) || vertices.size() < 3) return {};
        const auto& loop = state.loops.at(loop_id.value);
        for (const auto coedge_id : loop.coedges) {
            const auto& edge = state.edges.at(state.coedges.at(coedge_id.value).edge_id.value);
            const auto curve = state.curves.find(edge.curve_id.value);
            if (curve == state.curves.end()) return {};
            const auto& coedge = state.coedges.at(coedge_id.value);
            if (curve->second.kind == CurveKind::Circle) {
                if (coedge.pcurve_id.value != 0) return {};
                const auto& circle = curve->second;
                if (std::abs(dot(subtract(circle.origin, surface->second.origin), support_normal)) > tolerance ||
                    std::abs(dot(circle.axis_u, support_normal)) > 1e-12 ||
                    std::abs(dot(circle.axis_v, support_normal)) > 1e-12) return {};
                std::vector<Point3> arc;
                if (!sample_circle_edge(state, edge, options, tolerance, arc)) return {};
                if (coedge.reversed) std::reverse(arc.begin(), arc.end());
                ring.insert(ring.end(), arc.begin(), arc.end() - 1);
            } else {
                if (!straight_edge_matches(state, edge, tolerance)) return {};
                ring.push_back(state.vertices.at(coedge.reversed ? edge.v1.value : edge.v0.value).point);
            }
            if (ring.size() > 16384) return {};
            if (coedge.pcurve_id.value != 0) {
                const auto pc = state.pcurves.find(coedge.pcurve_id.value);
                if (pc == state.pcurves.end() || pc->second.poles.size() < 2) return {};
                const auto frame = make_frame_from_w(surface->second.normal);
                const auto& a = state.vertices.at(edge.v0.value).point;
                const auto& b = state.vertices.at(edge.v1.value).point;
                for (std::size_t i = 0; i < pc->second.poles.size(); ++i) {
                    const auto& uv = pc->second.poles[i];
                    const auto p = point_from_local(surface->second.origin,frame,uv.x,uv.y,0.0);
                    if (!finite_point(p)) return {};
                    const auto ab = subtract(b,a);
                    const Scalar t = dot(subtract(p,a),ab)/dot(ab,ab);
                    const Scalar parameter_tolerance = tolerance / norm(ab);
                    if (!std::isfinite(t) || t < -parameter_tolerance || t > 1.0 + parameter_tolerance ||
                        norm(subtract(p,add_point_vec(a,scale(ab,t)))) > tolerance) return {};
                    if ((i == 0 && norm(subtract(p,a)) > tolerance) ||
                        (i + 1 == pc->second.poles.size() && norm(subtract(p,b)) > tolerance)) return {};
                }
            }
        }
        for (const auto& point : ring) {
            const Scalar distance = std::abs(dot(subtract(point, surface->second.origin), support_normal));
            if (!std::isfinite(distance) || distance > tolerance) return {};
        }
        rings.push_back(std::move(ring));
    }
    const auto loop_normal = newell_normal(rings.front());
    if (!std::isfinite(norm(loop_normal)) || !(norm(loop_normal) > 0.0)) return {};
    const auto n = scale(support_normal,dot(loop_normal,support_normal) >= 0.0 ? 1.0 : -1.0);
    std::vector<std::array<int, 3>> triangles;
    if (rings.size() == 1) {
        mesh.vertices = rings.front();
        if (!triangulate_extrude_profile(mesh.vertices, n, triangles, true)) return {};
    } else {
        std::vector<std::pair<int, int>> boundary;
        const std::vector<std::vector<Point3>> holes(rings.begin() + 1, rings.end());
        if (!triangulate_extrude_region(rings.front(), holes, n, tolerance,
                                       mesh.vertices, boundary, triangles, true)) return {};
    }
    const Scalar angle_limit = radians_from_degrees(std::min<Scalar>(90.0,options.angular_error));
    for (const auto& triangle : triangles) {
        const auto tn = cross(subtract(mesh.vertices[triangle[1]],mesh.vertices[triangle[0]]),
                              subtract(mesh.vertices[triangle[2]],mesh.vertices[triangle[0]]));
        const Scalar length = norm(tn);
        if (!std::isfinite(length) || !(length > 0.0) ||
            std::atan2(norm(cross(tn,n)),dot(tn,n)) > angle_limit) return {};
    }
    mesh.bbox = mesh_bbox_from_vertices(mesh.vertices);
    if (options.compute_normals) mesh.normals.assign(mesh.vertices.size(), n);
    if (options.generate_texcoords) {
        mesh.texcoords.reserve(mesh.vertices.size());
        const auto& p0 = mesh.vertices.front();
        const auto u_axis = pick_orthogonal_unit(n);
        const auto v_axis = normalize(cross(n, u_axis));
        for (const auto& p : mesh.vertices) {
            const Vec3 d {p.x - p0.x, p.y - p0.y, p.z - p0.z};
            mesh.texcoords.push_back(Point2{dot(d, u_axis), dot(d, v_axis)});
        }
    }
    mesh.indices.reserve(triangles.size() * 3);
    for (const auto& triangle : triangles)
        for (const auto index : triangle) mesh.indices.push_back(static_cast<Index>(index));
    return mesh;
}

// Analytic cylindrical strip bounded by two opposite circular trims and two
// straight generators. No UV-box fallback: certify both physical boundaries.
MeshRecord tessellate_cylinder_strip(const KernelState& state, FaceId face_id,
                                     const TessellationOptions& options) {
    MeshRecord mesh;
    const auto& face = state.faces.at(face_id.value);
    const auto& surface = state.surfaces.at(face.surface_id.value);
    if (!face.inner_loops.empty() || !finite_point(surface.origin) ||
        !(surface.radius_a > 0.0) || !std::isfinite(surface.radius_a) ||
        !std::isfinite(norm(surface.axis)) || !(norm(surface.axis) > 0.0) ||
        !std::isfinite(norm(surface.normal)) || !(norm(surface.normal) > 0.0)) return {};
    const auto axis = normalize(surface.axis);
    const auto normal_axis = normalize(surface.normal);
    if (norm(Vec3{axis.x - normal_axis.x, axis.y - normal_axis.y, axis.z - normal_axis.z}) > 1e-12) return {};
    const auto loop = state.loops.find(face.outer_loop.value);
    std::string reason;
    if (loop == state.loops.end() || loop->second.coedges.size() != 4 ||
        !topo_internal::validate_loop_record(state, loop->second, reason)) return {};
    const Scalar tolerance = std::min(std::max<Scalar>(1e-12, std::abs(state.config.tolerance.linear)),
                                      options.chordal_error * 0.25);
    std::array<std::vector<Point3>, 4> boundary;
    std::vector<std::size_t> arcs;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& coedge = state.coedges.at(loop->second.coedges[i].value);
        const auto& edge = state.edges.at(coedge.edge_id.value);
        if (coedge.pcurve_id.value != 0) return {};
        const auto curve = state.curves.find(edge.curve_id.value);
        if (curve == state.curves.end()) return {};
        if (curve->second.kind == CurveKind::Circle) {
            const auto& c = curve->second;
            if (std::abs(c.radius - surface.radius_a) > tolerance ||
                std::abs(dot(c.axis_u, axis)) > 1e-12 || std::abs(dot(c.axis_v, axis)) > 1e-12 ||
                norm(reject_vec(subtract(c.origin, surface.origin), axis)) > tolerance ||
                !sample_circle_edge(state, edge, options, tolerance, boundary[i])) return {};
            arcs.push_back(i);
        } else {
            if (!straight_edge_matches(state, edge, tolerance)) return {};
            boundary[i] = {state.vertices.at(edge.v0.value).point, state.vertices.at(edge.v1.value).point};
            const auto delta = subtract(boundary[i].back(), boundary[i].front());
            if (norm(reject_vec(delta, axis)) > tolerance) return {};
        }
        if (coedge.reversed) std::reverse(boundary[i].begin(), boundary[i].end());
    }
    if (arcs.size() != 2 || (arcs[0] + 2) % 4 != arcs[1]) return {};
    const auto& a = boundary[arcs[0]];
    auto b = boundary[arcs[1]];
    std::reverse(b.begin(), b.end());
    if (a.size() != b.size() || a.size() < 2) return {};
    const auto travel = subtract(b.front(), a.front());
    const Scalar height = dot(travel, axis);
    if (!std::isfinite(height) || std::abs(height) <= tolerance ||
        norm(reject_vec(travel, axis)) > tolerance) return {};
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (norm(subtract(b[i], add_point_vec(a[i], travel))) > tolerance) return {};
        for (const auto& p : {a[i], b[i]}) {
            const auto radial = reject_vec(subtract(p, surface.origin), axis);
            if (std::abs(norm(radial) - surface.radius_a) > tolerance) return {};
            mesh.vertices.push_back(p);
            if (options.compute_normals) mesh.normals.push_back(normalize(radial));
            if (options.generate_texcoords)
                mesh.texcoords.push_back(Point2{static_cast<Scalar>(i) / static_cast<Scalar>(a.size() - 1),
                                                static_cast<Scalar>(mesh.vertices.size() % 2 == 0)});
        }
    }
    for (std::size_t i = 0; i + 1 < a.size(); ++i) {
        const Index a0 = static_cast<Index>(2 * i), b0 = a0 + 1, a1 = a0 + 2, b1 = a0 + 3;
        const auto n = cross(subtract(mesh.vertices[a1], mesh.vertices[a0]),
                             subtract(mesh.vertices[b1], mesh.vertices[a0]));
        const auto radial = reject_vec(subtract(mesh.vertices[a0], surface.origin), axis);
        if (!std::isfinite(norm(n)) || !(norm(n) > 0.0)) return {};
        if (dot(n, radial) > 0.0) add_quad(mesh.indices, a0, a1, b1, b0);
        else add_quad(mesh.indices, a0, b0, b1, a1);
    }
    mesh.bbox = mesh_bbox_from_vertices(mesh.vertices);
    mesh.label = "mesh_from_cylinder_strip";
    return mesh;
}

MeshRecord tessellate_face(const KernelState& state, FaceId face_id, const TessellationOptions& options) {
    const auto face_it = state.faces.find(face_id.value);
    if (face_it == state.faces.end() || !has_valid_tessellation_options(options)) return {};
    const auto& face = face_it->second;
    const auto surface_it = state.surfaces.find(face.surface_id.value);
    if (surface_it == state.surfaces.end()) return {};
    if (surface_it->second.kind == SurfaceKind::Plane)
        return tessellate_face_planar_mesh(state, face_id, options);
    if (surface_it->second.kind == SurfaceKind::Cylinder)
        return tessellate_cylinder_strip(state, face_id, options);
    // Four straight, certified isoparametric edges are the supported curved
    // boundary. Never replace arbitrary loops or trim holes with their UV bbox.
    if (!face.inner_loops.empty()) return {};
    const auto loop_it = state.loops.find(face.outer_loop.value);
    std::string reason;
    if (loop_it == state.loops.end() || loop_it->second.coedges.size() != 4 ||
        !topo_internal::validate_loop_record(state, loop_it->second, reason)) return {};
    const auto& trim = surface_it->second;
    const SurfaceRecord* support = &trim;
    if (trim.kind == SurfaceKind::Trimmed) {
        const auto base = state.surfaces.find(trim.base_surface_id.value);
        if (base == state.surfaces.end() || !trim.trim_uv_holes.empty()) return {};
        support = &base->second;
    }
    const auto& surf = *support;
    const bool tensor = surf.kind == SurfaceKind::Bezier || surf.kind == SurfaceKind::BSpline || surf.kind == SurfaceKind::Nurbs;
    if (tensor) {
        if (surf.poles.size() != 4) return {};
        for (const auto& p : surf.poles) if (!finite_point(p)) return {};
        if (surf.kind != SurfaceKind::Bezier) {
            if ((surf.spline_degree_u >= 0 && surf.spline_degree_u != 1) ||
                (surf.spline_degree_v >= 0 && surf.spline_degree_v != 1)) return {};
            for (const auto* knots : {&surf.knots_u, &surf.knots_v}) {
                if (knots->empty()) continue;
                if (knots->size() != 4 || !std::isfinite((*knots)[0]) || !std::isfinite((*knots)[3]) ||
                    (*knots)[0] != 0.0 || (*knots)[1] != 0.0 || (*knots)[2] != 1.0 || (*knots)[3] != 1.0) return {};
            }
            if (!surf.weights.empty()) {
                if (surf.weights.size() != 4 || !(surf.weights.front() > 0.0) || !std::isfinite(surf.weights.front())) return {};
                for (const Scalar w : surf.weights) if (w != surf.weights.front()) return {};
            }
        }
    } else if (surf.kind == SurfaceKind::Swept) {
        const auto profile = state.curves.find(surf.profile_curve_id.value);
        // A Line has an infinite profile domain and cannot define this bounded patch.
        if (profile == state.curves.end() || profile->second.kind != CurveKind::LineSegment ||
            profile->second.poles.size() != 2 || !(surf.sweep_length > 0.0) || !std::isfinite(surf.sweep_length) ||
            !finite_point(profile->second.poles[0]) || !finite_point(profile->second.poles[1]) ||
            !std::isfinite(norm(surf.axis)) || !(norm(surf.axis) > 0.0)) return {};
    } else if (surf.kind != SurfaceKind::Plane || trim.kind != SurfaceKind::Trimmed) return {};

    const auto evaluate = [&](Scalar u, Scalar v, Point3& p) {
        Vec3 n{};
        if (surf.kind == SurfaceKind::Swept) {
            const auto& poles = state.curves.at(surf.profile_curve_id.value).poles;
            p = add_point_vec(add_point_vec(poles[0], scale(subtract(poles[1], poles[0]), v)),
                              scale(normalize(surf.axis), surf.sweep_length * u));
            return finite_point(p);
        }
        if (!eval_surface_grid_point(nullptr, {}, surf, u, v, p, n)) return false;
        return finite_point(p);
    };
    const Scalar tolerance = std::min(std::max<Scalar>(1e-12, std::abs(state.config.tolerance.linear)),options.chordal_error * 0.25);
    Scalar u0 = 0.0, u1 = 1.0, v0 = 0.0, v1 = 1.0;
    if (trim.kind == SurfaceKind::Trimmed) {
        u0 = trim.trim_u_min; u1 = trim.trim_u_max; v0 = trim.trim_v_min; v1 = trim.trim_v_max;
    }
    std::array<Point2, 4> uv{};
    bool any_pcurve = false, all_pcurves = true;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& coedge = state.coedges.at(loop_it->second.coedges[i].value);
        const auto& edge = state.edges.at(coedge.edge_id.value);
        if (!straight_edge_matches(state, edge, tolerance)) return {};
        any_pcurve |= coedge.pcurve_id.value != 0;
        all_pcurves &= coedge.pcurve_id.value != 0;
    }
    if (any_pcurve && !all_pcurves) return {};
    if (all_pcurves) {
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& coedge = state.coedges.at(loop_it->second.coedges[i].value);
            const auto pcurve = state.pcurves.find(coedge.pcurve_id.value);
            if (pcurve == state.pcurves.end() || pcurve->second.poles.size() != 2) return {};
            uv[i] = pcurve->second.poles[coedge.reversed ? 1 : 0];
            const auto& end = pcurve->second.poles[coedge.reversed ? 0 : 1];
            const auto& next = state.coedges.at(loop_it->second.coedges[(i + 1) % 4].value);
            const auto next_pc = state.pcurves.find(next.pcurve_id.value);
            if (next_pc == state.pcurves.end() || next_pc->second.poles.size() != 2) return {};
            const auto& next_uv = next_pc->second.poles[next.reversed ? 1 : 0];
            if (end.x != next_uv.x || end.y != next_uv.y) return {};
        }
        if (trim.kind != SurfaceKind::Trimmed) {
            u0 = u1 = uv[0].x; v0 = v1 = uv[0].y;
            for (const auto& p : uv) { u0 = std::min(u0, p.x); u1 = std::max(u1, p.x); v0 = std::min(v0, p.y); v1 = std::max(v1, p.y); }
        }
    }
    if (!std::isfinite(u0) || !std::isfinite(u1) || !std::isfinite(v0) || !std::isfinite(v1) ||
        !(u0 < u1) || !(v0 < v1)) return {};
    if (surf.kind != SurfaceKind::Plane && (u0 < 0.0 || u1 > 1.0 || v0 < 0.0 || v1 > 1.0)) return {};
    const std::array<Point2, 4> corners {{{u0,v0},{u1,v0},{u1,v1},{u0,v1}}};
    std::array<Point3, 4> positions{};
    for (std::size_t i = 0; i < 4; ++i) if (!evaluate(corners[i].x, corners[i].y, positions[i])) return {};
    std::array<int, 4> order{};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& coedge = state.coedges.at(loop_it->second.coedges[i].value);
        const auto& edge = state.edges.at(coedge.edge_id.value);
        const auto& point = state.vertices.at((coedge.reversed ? edge.v1 : edge.v0).value).point;
        int match = -1;
        for (int j = 0; j < 4; ++j) {
            if (all_pcurves && (uv[i].x != corners[j].x || uv[i].y != corners[j].y)) continue;
            if (norm(subtract(point, positions[j])) <= tolerance) { if (match >= 0) return {}; match = j; }
        }
        if (match < 0) return {};
        order[i] = match;
    }
    const int step = (order[1] - order[0] + 4) % 4;
    if (step != 1 && step != 3) return {};
    for (std::size_t i = 0; i < 4; ++i)
        if (order[i] != (order[0] + static_cast<int>(i) * step) % 4) return {};
    if (trim.kind == SurfaceKind::Trimmed && !trim.trim_uv_loop.empty()) {
        if (trim.trim_uv_loop.size() != 4) return {};
        std::array<int, 4> trim_order{};
        for (std::size_t i = 0; i < 4; ++i) {
            trim_order[i] = -1;
            for (int j = 0; j < 4; ++j)
                if (trim.trim_uv_loop[i].x == corners[j].x && trim.trim_uv_loop[i].y == corners[j].y) trim_order[i] = j;
            if (trim_order[i] < 0) return {};
        }
        const int trim_step = (trim_order[1] - trim_order[0] + 4) % 4;
        if (trim_step != 1 && trim_step != 3) return {};
        for (std::size_t i = 0; i < 4; ++i)
            if (trim_order[i] != (trim_order[0] + static_cast<int>(i) * trim_step) % 4) return {};
    }
    const Point3 p00 = positions[0];
    const Vec3 b = subtract(positions[1], p00), c = subtract(positions[3], p00);
    const Vec3 d {positions[2].x - positions[1].x - c.x,
                  positions[2].y - positions[1].y - c.y,
                  positions[2].z - positions[1].z - c.z};
    const auto point_at = [&](Scalar u, Scalar v) {
        return Point3{p00.x+b.x*u+c.x*v+d.x*u*v,
                      p00.y+b.y*u+c.y*v+d.y*u*v,
                      p00.z+b.z*u+c.z*v+d.z*u*v};
    };
    const auto normal_at = [&](Scalar u, Scalar v) {
        return cross(Vec3{b.x+d.x*v,b.y+d.y*v,b.z+d.z*v},Vec3{c.x+d.x*u,c.y+d.y*u,c.z+d.z*u});
    };
    std::size_t nu = segments_for_tensor_direction(norm(b), options);
    std::size_t nv = segments_for_tensor_direction(norm(c), options);
    // Bilinear-to-triangle interpolation error is bounded everywhere by |d|/(4 nu nv).
    // Raw bilinear normals are affine; the four corner cone checks certify the
    // entire cell, rather than merely checking a sample at its center.
    for (;;) {
        bool valid = norm(d) / (4.0 * static_cast<Scalar>(nu) * static_cast<Scalar>(nv)) <= options.chordal_error * 0.75;
        const Scalar angle_limit = radians_from_degrees(std::min<Scalar>(90.0, options.angular_error));
        for (std::size_t j = 0; valid && j < nv; ++j) {
            for (std::size_t i = 0; valid && i < nu; ++i) {
                const Scalar a = static_cast<Scalar>(i)/nu, x = static_cast<Scalar>(i+1)/nu;
                const Scalar z = static_cast<Scalar>(j)/nv, y = static_cast<Scalar>(j+1)/nv;
                const auto p0 = point_at(a,z), p1 = point_at(x,z), p2 = point_at(x,y), p3 = point_at(a,y);
                const std::array<Vec3,2> triangles {{cross(subtract(p1,p0),subtract(p2,p0)),cross(subtract(p2,p0),subtract(p3,p0))}};
                for (const auto& triangle : triangles) {
                    const Scalar tnorm = norm(triangle);
                    if (!std::isfinite(tnorm) || !(tnorm > 0.0)) return {};
                    for (const auto& uv_corner : std::array<Point2,4>{{{a,z},{x,z},{x,y},{a,y}}}) {
                        const auto n = normal_at(uv_corner.x,uv_corner.y);
                        const Scalar nnorm = norm(n);
                        if (!std::isfinite(nnorm) || !(nnorm > 0.0)) return {};
                        if (std::atan2(norm(cross(normalize(n),normalize(triangle))),dot(normalize(n),normalize(triangle))) > angle_limit) valid = false;
                    }
                }
            }
        }
        if (valid) break;
        if (nu >= 256 && nv >= 256) return {};
        nu = std::min<std::size_t>(256,nu*2); nv = std::min<std::size_t>(256,nv*2);
    }
    MeshRecord mesh;
    mesh.label = "mesh_from_face_certified_bilinear";
    const Scalar sign = step == 1 ? 1.0 : -1.0;
    for (std::size_t j = 0; j <= nv; ++j) for (std::size_t i = 0; i <= nu; ++i) {
        const Scalar u = static_cast<Scalar>(i)/nu, v = static_cast<Scalar>(j)/nv;
        const auto p = point_at(u,v);
        if (!finite_point(p)) return {};
        mesh.vertices.push_back(p);
        if (options.compute_normals) mesh.normals.push_back(scale(normalize(normal_at(u,v)),sign));
        if (options.generate_texcoords) mesh.texcoords.push_back(options.uv_parametric_seam ? Point2{u0+u*(u1-u0),v0+v*(v1-v0)} : Point2{u,v});
    }
    for (std::size_t j = 0; j < nv; ++j) for (std::size_t i = 0; i < nu; ++i) {
        const Index a = static_cast<Index>(j*(nu+1)+i), bidx = a+1, cidx = static_cast<Index>((j+1)*(nu+1)+i+1), didx = cidx-1;
        if (step == 1) add_quad(mesh.indices,a,bidx,cidx,didx);
        else add_quad(mesh.indices,a,didx,cidx,bidx);
    }
    mesh.bbox = mesh_bbox_from_vertices(mesh.vertices);
    return mesh;
}

} // namespace rep_internal

MeshRecord tessellate_face_planar(const KernelState& state, FaceId face_id, const TessellationOptions& options) {
    return rep_internal::tessellate_face_planar_mesh(state, face_id, options);
}

MeshRecord tessellate_face(const KernelState& state, FaceId face_id, const TessellationOptions& options) {
    return rep_internal::tessellate_face(state, face_id, options);
}

bool has_out_of_range_indices(const std::vector<Point3>& vertices, const std::vector<Index>& indices) {
    return std::any_of(indices.begin(), indices.end(),
                       [&vertices](Index idx) { return static_cast<std::size_t>(idx) >= vertices.size(); });
}

bool has_degenerate_triangles(const std::vector<Point3>& vertices, const std::vector<Index>& indices) {
    if ((indices.size() % 3) != 0 || has_out_of_range_indices(vertices, indices)) {
        return true;
    }

    for (std::size_t i = 0; i < indices.size(); i += 3) {
        const auto& p0 = vertices[indices[i]];
        const auto& p1 = vertices[indices[i + 1]];
        const auto& p2 = vertices[indices[i + 2]];
        // Keep invalid coordinate classification separate from geometric QA.
        // IO reports nonfinite input at its format-specific mesh stage.
        if (!rep_internal::finite_point(p0) || !rep_internal::finite_point(p1) ||
            !rep_internal::finite_point(p2)) continue;
        std::array<long double,3> e1 {{static_cast<long double>(p1.x)-p0.x,
            static_cast<long double>(p1.y)-p0.y,static_cast<long double>(p1.z)-p0.z}};
        std::array<long double,3> e2 {{static_cast<long double>(p2.x)-p0.x,
            static_cast<long double>(p2.y)-p0.y,static_cast<long double>(p2.z)-p0.z}};
        const auto scale1 = std::max({std::abs(e1[0]),std::abs(e1[1]),std::abs(e1[2])});
        const auto scale2 = std::max({std::abs(e2[0]),std::abs(e2[1]),std::abs(e2[2])});
        if (!(scale1 > 0.0L) || !(scale2 > 0.0L)) return true;
        for (auto& value : e1) value /= scale1;
        for (auto& value : e2) value /= scale2;
        const std::array<long double,3> cp {{e1[1]*e2[2]-e1[2]*e2[1],
            e1[2]*e2[0]-e1[0]*e2[2],e1[0]*e2[1]-e1[1]*e2[0]}};
        const auto area2 = cp[0]*cp[0]+cp[1]*cp[1]+cp[2]*cp[2];
        const auto length_product = (e1[0]*e1[0]+e1[1]*e1[1]+e1[2]*e1[2]) *
                                    (e2[0]*e2[0]+e2[1]*e2[1]+e2[2]*e2[2]);
        const long double relative = 64.0L * std::numeric_limits<Scalar>::epsilon();
        if (area2 <= relative * relative * length_product) return true;
    }
    return false;
}

std::uint64_t mesh_connected_components(const std::vector<Index>& indices, std::size_t vertex_count) {
    if (indices.empty() || vertex_count == 0) {
        return 0;
    }

    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> adjacency;
    adjacency.reserve(vertex_count);
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const auto a = static_cast<std::uint64_t>(indices[i]);
        const auto b = static_cast<std::uint64_t>(indices[i + 1]);
        const auto c = static_cast<std::uint64_t>(indices[i + 2]);
        adjacency[a].push_back(b);
        adjacency[a].push_back(c);
        adjacency[b].push_back(a);
        adjacency[b].push_back(c);
        adjacency[c].push_back(a);
        adjacency[c].push_back(b);
    }

    std::unordered_set<std::uint64_t> visited;
    visited.reserve(adjacency.size());
    std::uint64_t components = 0;
    for (const auto& [seed, _] : adjacency) {
        if (visited.contains(seed)) {
            continue;
        }
        ++components;
        std::queue<std::uint64_t> q;
        q.push(seed);
        visited.insert(seed);
        while (!q.empty()) {
            const auto current = q.front();
            q.pop();
            const auto it = adjacency.find(current);
            if (it == adjacency.end()) {
                continue;
            }
            for (const auto next : it->second) {
                if (!visited.contains(next)) {
                    visited.insert(next);
                    q.push(next);
                }
            }
        }
    }
    return components;
}

}  // namespace axiom::detail
