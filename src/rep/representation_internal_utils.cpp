#include "axiom/internal/rep/representation_internal_utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
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

Scalar safe_acos(Scalar v) {
    return std::acos(clamp(v, -1.0, 1.0));
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
    return options.chordal_error > 0.0 && options.angular_error > 0.0 &&
           options.weld_shading_split_angle_deg >= 0.0 &&
           options.weld_shading_split_angle_deg <= 180.0;
}

std::size_t tessellation_slices_per_face(const TessellationOptions& options) {
    const auto slices_from_chordal = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(1.0 / options.chordal_error)));
    const auto slices_from_angular = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(180.0 / options.angular_error)));
    return std::min<std::size_t>(8, std::max(slices_from_chordal, slices_from_angular));
}

std::string tessellation_cache_key(const BodyRecord& body, const TessellationOptions& options) {
    // Stable-enough key: primitive parameters + bbox + tess options.
    // This is not cryptographic; it is a pragmatic cache key for Stage 1.5.
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(12);
    oss << "kind=" << static_cast<std::uint32_t>(body.kind)
        << "|rep=" << static_cast<std::uint32_t>(body.rep_kind)
        << "|o=" << body.origin.x << "," << body.origin.y << "," << body.origin.z
        << "|ax=" << body.axis.x << "," << body.axis.y << "," << body.axis.z
        << "|a=" << body.a << "|b=" << body.b << "|c=" << body.c
        << "|bb=" << body.bbox.min.x << "," << body.bbox.min.y << "," << body.bbox.min.z
        << "," << body.bbox.max.x << "," << body.bbox.max.y << "," << body.bbox.max.z
        << "|tess=" << options.chordal_error << "," << options.angular_error
        << "," << (options.compute_normals ? 1 : 0)
        << "," << (options.generate_texcoords ? 1 : 0)
        << "," << options.weld_shading_split_angle_deg;
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
        << "}";
    return oss.str();
}

ConversionErrorBudget conversion_error_budget_from_tessellation(const TessellationOptions& options) {
    ConversionErrorBudget b;
    const auto e = std::max<Scalar>(options.chordal_error, std::numeric_limits<Scalar>::epsilon());
    b.bbox_abs_tol = e * 2.0;
    b.max_point_abs_tol = e;
    b.normal_angle_deg_tol = std::max<Scalar>(options.angular_error, 1e-6);
    return b;
}

std::string face_tessellation_cache_key(const KernelState& state, FaceId face_id, const TessellationOptions& options) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(12);
    oss << "face=" << face_id.value
        << "|tess=" << options.chordal_error << "," << options.angular_error
        << "," << (options.compute_normals ? 1 : 0)
        << "," << (options.generate_texcoords ? 1 : 0)
        << "," << options.weld_shading_split_angle_deg;
    const auto face_it = state.faces.find(face_id.value);
    if (face_it != state.faces.end()) {
        oss << "|surf=" << face_it->second.surface_id.value
            << "|outer=" << face_it->second.outer_loop.value;
        const auto surf_it = state.surfaces.find(face_it->second.surface_id.value);
        if (surf_it != state.surfaces.end()) {
            const auto& s = surf_it->second;
            oss << "|kind=" << static_cast<int>(s.kind);
            if (s.kind == SurfaceKind::Trimmed) {
                oss << "|base=" << s.base_surface_id.value << "|tu=" << s.trim_u_min << ","
                    << s.trim_u_max << "|tv=" << s.trim_v_min << "," << s.trim_v_max;
            }
        }
    }
    return oss.str();
}

std::size_t segments_for_circle(Scalar radius, const TessellationOptions& options) {
    // Use both chordal error and angular error constraints.
    // chordal: e >= r * (1 - cos(theta/2)) -> theta <= 2*acos(1 - e/r)
    const auto r = std::max<Scalar>(radius, std::numeric_limits<Scalar>::epsilon());
    const auto e = std::max<Scalar>(options.chordal_error, std::numeric_limits<Scalar>::epsilon());
    const auto theta_chordal = 2.0 * safe_acos(1.0 - clamp(e / r, 0.0, 2.0));
    const auto theta_angular = radians_from_degrees(std::max<Scalar>(options.angular_error, 1e-6));
    const auto theta = std::max<Scalar>(1e-6, std::min(theta_chordal, theta_angular));
    const auto n = static_cast<std::size_t>(std::ceil((2.0 * kPi) / theta));
    return std::max<std::size_t>(8, std::min<std::size_t>(4096, n));
}

std::size_t segments_for_length(Scalar length, const TessellationOptions& options) {
    // For low-curvature directions, we still want longitudinal segmentation to avoid skinny triangles
    // and to allow future local re-tessellation patches. We base this on chordal error as a linear step.
    const auto L = std::max<Scalar>(length, 0.0);
    const auto e = std::max<Scalar>(options.chordal_error, std::numeric_limits<Scalar>::epsilon());
    const auto n = static_cast<std::size_t>(std::ceil(L / (e * 2.0)));
    return std::max<std::size_t>(1, std::min<std::size_t>(2048, n));
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

    // Precompute axis coordinates so shared edges match bitwise across faces.
    const auto o = body.origin;
    std::vector<Scalar> xs(nx + 1), ys(ny + 1), zs(nz + 1);
    for (std::size_t i = 0; i <= nx; ++i) xs[i] = o.x + dx * (static_cast<Scalar>(i) / static_cast<Scalar>(nx));
    for (std::size_t i = 0; i <= ny; ++i) ys[i] = o.y + dy * (static_cast<Scalar>(i) / static_cast<Scalar>(ny));
    for (std::size_t i = 0; i <= nz; ++i) zs[i] = o.z + dz * (static_cast<Scalar>(i) / static_cast<Scalar>(nz));

    // Emit a face grid by selecting which axis corresponds to (u,v) and holding the third axis constant.
    enum class Axis { X, Y, Z };
    auto emit_face_grid = [&](Axis u_axis, Axis v_axis, Axis w_axis, Scalar w_value,
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
                add_quad(mesh.indices, i00, i10, i11, i01);
            }
        }
    };

    // +Z, -Z
    emit_face_grid(Axis::X, Axis::Y, Axis::Z, zs.back(), Vec3{0,0,1}, nx, ny);
    emit_face_grid(Axis::X, Axis::Y, Axis::Z, zs.front(), Vec3{0,0,-1}, nx, ny);
    // +Y, -Y
    emit_face_grid(Axis::X, Axis::Z, Axis::Y, ys.back(), Vec3{0,1,0}, nx, nz);
    emit_face_grid(Axis::X, Axis::Z, Axis::Y, ys.front(), Vec3{0,-1,0}, nx, nz);
    // +X, -X
    emit_face_grid(Axis::Y, Axis::Z, Axis::X, xs.back(), Vec3{1,0,0}, ny, nz);
    emit_face_grid(Axis::Y, Axis::Z, Axis::X, xs.front(), Vec3{-1,0,0}, ny, nz);

    weld_mesh_vertices(mesh, options.compute_normals);
    if (!options.generate_texcoords) {
        mesh.texcoords.clear();
    }
    return mesh;
}

void weld_mesh_vertices(MeshRecord& mesh, bool weld_normals) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return;
    }
    // Welding uses quantization to tolerate floating noise from face-wise generation.
    // Keep it small enough to not collapse meaningful features, but large enough to merge shared box edges.
    constexpr Scalar q = 1e-7;
    auto quant = [](Scalar v) -> std::int64_t {
        return static_cast<std::int64_t>(std::llround(v / q));
    };
    std::unordered_map<std::string, Index> map;
    map.reserve(mesh.vertices.size());
    std::vector<Point3> new_vertices;
    std::vector<Vec3> new_normals;
    std::vector<Point2> new_uvs;
    std::vector<Index> remap(mesh.vertices.size(), 0);
    new_vertices.reserve(mesh.vertices.size());
    if (weld_normals && !mesh.normals.empty()) new_normals.reserve(mesh.vertices.size());
    if (!mesh.texcoords.empty()) new_uvs.reserve(mesh.vertices.size());
    const bool use_uv_key =
        !mesh.texcoords.empty() && mesh.texcoords.size() == mesh.vertices.size();

    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto& p = mesh.vertices[i];
        std::string key =
            std::to_string(quant(p.x)) + "," + std::to_string(quant(p.y)) + "," + std::to_string(quant(p.z));
        if (use_uv_key) {
            const auto& uv = mesh.texcoords[i];
            key += "|uv=" + std::to_string(quant(uv.x)) + "," + std::to_string(quant(uv.y));
        }
        const auto it = map.find(key);
        if (it == map.end()) {
            const auto ni = static_cast<Index>(new_vertices.size());
            map.emplace(key, ni);
            new_vertices.push_back(p);
            if (weld_normals && !mesh.normals.empty()) {
                new_normals.push_back(mesh.normals[i]);
            }
            if (use_uv_key) {
                new_uvs.push_back(mesh.texcoords[i]);
            }
            remap[i] = ni;
        } else {
            remap[i] = it->second;
            if (weld_normals && !mesh.normals.empty()) {
                auto& acc = new_normals[it->second];
                acc.x += mesh.normals[i].x;
                acc.y += mesh.normals[i].y;
                acc.z += mesh.normals[i].z;
            }
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

MeshRecord tessellate_sphere(const BodyRecord& body, const TessellationOptions& options) {
    MeshRecord mesh;
    mesh.source_body = BodyId{0};
    mesh.label = "mesh_from_sphere";
    mesh.bbox = body.bbox;

    const auto center = body.origin;
    const auto r = std::max<Scalar>(body.a, std::numeric_limits<Scalar>::epsilon());
    const auto nu = segments_for_circle(r, options);
    const auto nv = std::max<std::size_t>(4, nu / 2);

    // UV sphere: u in [0,2pi], v in [0,pi]
    const auto base = static_cast<Index>(0);
    (void)base;
    for (std::size_t j = 0; j <= nv; ++j) {
        const auto v = (static_cast<Scalar>(j) / static_cast<Scalar>(nv)) * kPi;
        const auto sv = std::sin(v);
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
            add_quad(mesh.indices, i00, i10, i11, i01);
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
            add_quad(mesh.indices, i00, i10, i11, i01);
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
    const auto nu = segments_for_circle(R, options);
    const auto nv = segments_for_circle(r, options);

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

std::vector<Point3> face_outer_boundary_vertices(const KernelState& state, FaceId face_id) {
    std::vector<Point3> vertices;
    const auto face_it = state.faces.find(face_id.value);
    if (face_it == state.faces.end()) {
        return vertices;
    }
    const auto loop_it = state.loops.find(face_it->second.outer_loop.value);
    if (loop_it == state.loops.end() || loop_it->second.coedges.empty()) {
        return vertices;
    }
    vertices.reserve(loop_it->second.coedges.size());
    for (const auto coedge_id : loop_it->second.coedges) {
        const auto coedge_it = state.coedges.find(coedge_id.value);
        if (coedge_it == state.coedges.end()) {
            vertices.clear();
            return vertices;
        }
        const auto edge_it = state.edges.find(coedge_it->second.edge_id.value);
        if (edge_it == state.edges.end()) {
            vertices.clear();
            return vertices;
        }
        const auto v0 = edge_it->second.v0;
        const auto v1 = edge_it->second.v1;
        const auto v_it = state.vertices.find((coedge_it->second.reversed ? v1 : v0).value);
        if (v_it == state.vertices.end()) {
            vertices.clear();
            return vertices;
        }
        vertices.push_back(v_it->second.point);
    }
    // Remove duplicated last vertex if the loop explicitly repeats start.
    if (vertices.size() >= 2) {
        const auto& a = vertices.front();
        const auto& b = vertices.back();
        const auto dx = a.x - b.x;
        const auto dy = a.y - b.y;
        const auto dz = a.z - b.z;
        if ((dx*dx + dy*dy + dz*dz) <= 1e-24) {
            vertices.pop_back();
        }
    }
    return vertices;
}

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

std::array<Scalar, 3> to_local(Vec3 rel, const OrthoFrame& frame) {
    return {dot(rel, frame.u), dot(rel, frame.v), dot(rel, frame.w)};
}

bool eval_surface_grid_point(const SurfaceRecord& surface, Scalar u, Scalar v, Point3& p,
                             Vec3& n) {
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
    default:
        return false;
    }
}

struct UvBounds {
    bool ok {false};
    Scalar u0 {0.0};
    Scalar u1 {0.0};
    Scalar v0 {0.0};
    Scalar v1 {0.0};
};

UvBounds uv_bounds_from_boundary(const KernelState& state, FaceId face_id,
                                 const SurfaceRecord& surf) {
    UvBounds out;
    const auto boundary = face_outer_boundary_vertices(state, face_id);
    if (boundary.size() < 3) {
        return out;
    }
    const auto frame = make_frame_from_w(surf.normal);
    Scalar u_min = std::numeric_limits<Scalar>::infinity();
    Scalar u_max = -std::numeric_limits<Scalar>::infinity();
    Scalar v_min = std::numeric_limits<Scalar>::infinity();
    Scalar v_max = -std::numeric_limits<Scalar>::infinity();
    for (const auto& point : boundary) {
        const Vec3 rel {point.x - surf.origin.x, point.y - surf.origin.y, point.z - surf.origin.z};
        if (surf.kind == SurfaceKind::Sphere) {
            const auto R = std::max<Scalar>(surf.radius_a, std::numeric_limits<Scalar>::epsilon());
            const auto vv = safe_acos(clamp(rel.z / R, -1.0, 1.0));
            auto uu = std::atan2(rel.y, rel.x);
            if (uu < 0.0) {
                uu += 2.0 * kPi;
            }
            u_min = std::min(u_min, uu);
            u_max = std::max(u_max, uu);
            v_min = std::min(v_min, vv);
            v_max = std::max(v_max, vv);
        } else if (surf.kind == SurfaceKind::Cylinder) {
            const auto loc = to_local(rel, frame);
            auto uu = std::atan2(loc[1], loc[0]);
            if (uu < 0.0) {
                uu += 2.0 * kPi;
            }
            u_min = std::min(u_min, uu);
            u_max = std::max(u_max, uu);
            v_min = std::min(v_min, loc[2]);
            v_max = std::max(v_max, loc[2]);
        } else if (surf.kind == SurfaceKind::Torus) {
            const auto R = std::max<Scalar>(surf.radius_a, std::numeric_limits<Scalar>::epsilon());
            const auto loc = to_local(rel, frame);
            const auto planar = std::hypot(loc[0], loc[1]);
            auto uu = std::atan2(loc[1], loc[0]);
            if (uu < 0.0) {
                uu += 2.0 * kPi;
            }
            const auto vv = std::atan2(loc[2], planar - R);
            u_min = std::min(u_min, uu);
            u_max = std::max(u_max, uu);
            v_min = std::min(v_min, vv);
            v_max = std::max(v_max, vv);
        } else if (surf.kind == SurfaceKind::Cone) {
            const auto loc = to_local(rel, frame);
            auto uu = std::atan2(loc[1], loc[0]);
            if (uu < 0.0) {
                uu += 2.0 * kPi;
            }
            const auto vv = loc[2];
            u_min = std::min(u_min, uu);
            u_max = std::max(u_max, uu);
            v_min = std::min(v_min, vv);
            v_max = std::max(v_max, vv);
        } else {
            return out;
        }
    }
    if (!(u_min < u_max) || !(v_min < v_max)) {
        return out;
    }
    out.ok = true;
    out.u0 = u_min;
    out.u1 = u_max;
    out.v0 = v_min;
    out.v1 = v_max;
    return out;
}

MeshRecord tessellate_surface_patch(const SurfaceRecord& surf, Scalar u0, Scalar u1, Scalar v0,
                                  Scalar v1, const TessellationOptions& options, const char* label) {
    MeshRecord mesh;
    mesh.source_body = BodyId{0};
    mesh.label = label;
    if (!(u0 < u1) || !(v0 < v1)) {
        return mesh;
    }
    std::size_t nu = 2;
    std::size_t nv = 2;
    const auto span_u = u1 - u0;
    const auto span_v = v1 - v0;
    switch (surf.kind) {
    case SurfaceKind::Plane:
        nu = segments_for_length(span_u, options);
        nv = segments_for_length(span_v, options);
        break;
    case SurfaceKind::Sphere: {
        const auto R = std::max<Scalar>(surf.radius_a, std::numeric_limits<Scalar>::epsilon());
        const auto base = segments_for_circle(R, options);
        nu = std::max<std::size_t>(
            2, std::min<std::size_t>(2048, static_cast<std::size_t>(std::ceil(base * span_u / (2.0 * kPi)))));
        nv = std::max<std::size_t>(
            2, std::min<std::size_t>(2048, static_cast<std::size_t>(std::ceil(base * span_v / kPi))));
        break;
    }
    case SurfaceKind::Cylinder: {
        const auto R = std::max<Scalar>(surf.radius_a, std::numeric_limits<Scalar>::epsilon());
        const auto base = segments_for_circle(R, options);
        nu = std::max<std::size_t>(
            2, std::min<std::size_t>(2048, static_cast<std::size_t>(std::ceil(base * span_u / (2.0 * kPi)))));
        nv = std::max<std::size_t>(2, segments_for_length(span_v, options));
        break;
    }
    case SurfaceKind::Cone: {
        const auto apex_r = std::tan(surf.semi_angle) * v1;
        const auto base_r = std::max<Scalar>(apex_r, std::numeric_limits<Scalar>::epsilon());
        const auto base = segments_for_circle(base_r, options);
        nu = std::max<std::size_t>(
            2, std::min<std::size_t>(2048, static_cast<std::size_t>(std::ceil(base * span_u / (2.0 * kPi)))));
        nv = std::max<std::size_t>(2, segments_for_length(span_v, options));
        break;
    }
    case SurfaceKind::Torus: {
        const auto R = std::max<Scalar>(surf.radius_a, std::numeric_limits<Scalar>::epsilon());
        const auto r = std::max<Scalar>(surf.radius_b, std::numeric_limits<Scalar>::epsilon());
        const auto base_u = segments_for_circle(R + r, options);
        const auto base_v = segments_for_circle(r, options);
        nu = std::max<std::size_t>(
            2, std::min<std::size_t>(2048, static_cast<std::size_t>(std::ceil(base_u * span_u / (2.0 * kPi)))));
        nv = std::max<std::size_t>(
            2, std::min<std::size_t>(2048, static_cast<std::size_t>(std::ceil(base_v * span_v / (2.0 * kPi)))));
        break;
    }
    default:
        return mesh;
    }

    for (std::size_t j = 0; j <= nv; ++j) {
        const auto tv = static_cast<Scalar>(j) / static_cast<Scalar>(nv);
        const auto v = v0 + tv * (v1 - v0);
        for (std::size_t i = 0; i <= nu; ++i) {
            const auto tu = static_cast<Scalar>(i) / static_cast<Scalar>(nu);
            const auto u = u0 + tu * (u1 - u0);
            Point3 p;
            Vec3 n;
            if (!eval_surface_grid_point(surf, u, v, p, n)) {
                mesh.vertices.clear();
                mesh.indices.clear();
                mesh.normals.clear();
                mesh.texcoords.clear();
                return mesh;
            }
            mesh.vertices.push_back(p);
            if (options.compute_normals) {
                mesh.normals.push_back(n);
            }
            mesh.texcoords.push_back(Point2{tu, tv});
        }
    }
    mesh.bbox = mesh_bbox_from_vertices(mesh.vertices);
    for (std::size_t j = 0; j < nv; ++j) {
        for (std::size_t i = 0; i < nu; ++i) {
            const auto row0 = static_cast<Index>(j * (nu + 1));
            const auto row1 = static_cast<Index>((j + 1) * (nu + 1));
            const auto i00 = row0 + static_cast<Index>(i);
            const auto i10 = row0 + static_cast<Index>(i + 1);
            const auto i01 = row1 + static_cast<Index>(i);
            const auto i11 = row1 + static_cast<Index>(i + 1);
            mesh.indices.insert(mesh.indices.end(), {i00, i10, i11, i00, i11, i01});
        }
    }
    return mesh;
}

MeshRecord tessellate_face_planar_mesh(const KernelState& state, FaceId face_id, const TessellationOptions& options) {
    MeshRecord mesh;
    mesh.source_body = BodyId{0};
    mesh.label = "mesh_from_face_planar";

    const auto boundary = face_outer_boundary_vertices(state, face_id);
    if (boundary.size() < 3) {
        return mesh;
    }
    mesh.vertices = boundary;
    mesh.bbox = mesh_bbox_from_vertices(mesh.vertices);

    const auto n = newell_normal(mesh.vertices);
    if (options.compute_normals) {
        mesh.normals.assign(mesh.vertices.size(), n);
    }
    mesh.texcoords.clear();
    mesh.texcoords.reserve(mesh.vertices.size());
    const auto& p0 = mesh.vertices.front();
    const auto u_axis = pick_orthogonal_unit(n);
    const auto v_axis = normalize(cross(n, u_axis));
    for (const auto& p : mesh.vertices) {
        const Vec3 d {p.x - p0.x, p.y - p0.y, p.z - p0.z};
        mesh.texcoords.push_back(Point2{dot(d, u_axis), dot(d, v_axis)});
    }

    // Fan triangulation; suitable for convex faces (the current minimal owned topology uses rectangles).
    mesh.indices.reserve((mesh.vertices.size() - 2) * 3);
    for (std::size_t i = 1; i + 1 < mesh.vertices.size(); ++i) {
        mesh.indices.push_back(static_cast<Index>(0));
        mesh.indices.push_back(static_cast<Index>(i));
        mesh.indices.push_back(static_cast<Index>(i + 1));
    }
    return mesh;
}

MeshRecord tessellate_face(const KernelState& state, FaceId face_id, const TessellationOptions& options) {
    const auto face_it = state.faces.find(face_id.value);
    if (face_it == state.faces.end()) {
        return MeshRecord {};
    }
    const auto surf_it = state.surfaces.find(face_it->second.surface_id.value);
    if (surf_it == state.surfaces.end()) {
        return rep_internal::tessellate_face_planar_mesh(state, face_id, options);
    }
    const auto& surf = surf_it->second;
    if (surf.kind == SurfaceKind::Trimmed) {
        const auto base_it = state.surfaces.find(surf.base_surface_id.value);
        if (base_it == state.surfaces.end()) {
            return rep_internal::tessellate_face_planar_mesh(state, face_id, options);
        }
        auto patch = tessellate_surface_patch(base_it->second, surf.trim_u_min, surf.trim_u_max,
                                              surf.trim_v_min, surf.trim_v_max, options,
                                              "mesh_from_face_trimmed_patch");
        if (!patch.vertices.empty() && !patch.indices.empty()) {
            return patch;
        }
        return rep_internal::tessellate_face_planar_mesh(state, face_id, options);
    }
    if (surf.kind == SurfaceKind::Plane) {
        return rep_internal::tessellate_face_planar_mesh(state, face_id, options);
    }
    if (surf.kind == SurfaceKind::Sphere || surf.kind == SurfaceKind::Cylinder ||
        surf.kind == SurfaceKind::Torus || surf.kind == SurfaceKind::Cone) {
        const auto ub = uv_bounds_from_boundary(state, face_id, surf);
        if (ub.ok) {
            auto patch = tessellate_surface_patch(surf, ub.u0, ub.u1, ub.v0, ub.v1, options,
                                                  "mesh_from_face_analytic_patch");
            if (!patch.vertices.empty() && !patch.indices.empty()) {
                return patch;
            }
        }
        return rep_internal::tessellate_face_planar_mesh(state, face_id, options);
    }
    return rep_internal::tessellate_face_planar_mesh(state, face_id, options);
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

    constexpr Scalar kAreaEps = 1e-12;
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        const auto& p0 = vertices[indices[i]];
        const auto& p1 = vertices[indices[i + 1]];
        const auto& p2 = vertices[indices[i + 2]];
        const Vec3 e1 {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
        const Vec3 e2 {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
        const Vec3 cp {
            e1.y * e2.z - e1.z * e2.y,
            e1.z * e2.x - e1.x * e2.z,
            e1.x * e2.y - e1.y * e2.x
        };
        const auto area2 = cp.x * cp.x + cp.y * cp.y + cp.z * cp.z;
        if (area2 <= kAreaEps) {
            return true;
        }
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
