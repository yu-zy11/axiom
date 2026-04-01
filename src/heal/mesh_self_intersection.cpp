#include "axiom/internal/heal/mesh_self_intersection.h"

#include "axiom/internal/rep/representation_internal_utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace axiom::detail {
namespace {

Vec3 vsub(const Point3& a, const Point3& b) {
    return Vec3 {a.x - b.x, a.y - b.y, a.z - b.z};
}

Scalar dotv(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 crossv(const Vec3& a, const Vec3& b) {
    return Vec3 {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Scalar length_squared(const Vec3& v) {
    return dotv(v, v);
}

void normalize_or_zero(Vec3* v) {
    const Scalar l2 = length_squared(*v);
    if (!(l2 > 0.0) || !std::isfinite(l2)) {
        *v = Vec3 {0.0, 0.0, 0.0};
        return;
    }
    const Scalar inv = 1.0 / std::sqrt(l2);
    v->x *= inv;
    v->y *= inv;
    v->z *= inv;
}

Scalar triangle_area_squared(const Point3& a, const Point3& b, const Point3& c) {
    const Vec3 ab = vsub(b, a);
    const Vec3 ac = vsub(c, a);
    const Vec3 cr = crossv(ab, ac);
    return dotv(cr, cr);
}

BoundingBox triangle_bbox(const Point3& a, const Point3& b, const Point3& c) {
    BoundingBox out;
    out.min.x = std::min({a.x, b.x, c.x});
    out.min.y = std::min({a.y, b.y, c.y});
    out.min.z = std::min({a.z, b.z, c.z});
    out.max.x = std::max({a.x, b.x, c.x});
    out.max.y = std::max({a.y, b.y, c.y});
    out.max.z = std::max({a.z, b.z, c.z});
    out.is_valid = true;
    return out;
}

bool bbox_overlap_eps(const BoundingBox& a, const BoundingBox& b, Scalar eps) {
    if (!a.is_valid || !b.is_valid) {
        return true;
    }
    return a.max.x >= b.min.x - eps && b.max.x >= a.min.x - eps && a.max.y >= b.min.y - eps &&
           b.max.y >= a.min.y - eps && a.max.z >= b.min.z - eps && b.max.z >= a.min.z - eps;
}

void project_interval(const Point3 tri[3], const Vec3& axis, Scalar* out_min, Scalar* out_max) {
    const Scalar p0 = tri[0].x * axis.x + tri[0].y * axis.y + tri[0].z * axis.z;
    const Scalar p1 = tri[1].x * axis.x + tri[1].y * axis.y + tri[1].z * axis.z;
    const Scalar p2 = tri[2].x * axis.x + tri[2].y * axis.y + tri[2].z * axis.z;
    *out_min = std::min({p0, p1, p2});
    *out_max = std::max({p0, p1, p2});
}

bool intervals_overlap(Scalar amin, Scalar amax, Scalar bmin, Scalar bmax, Scalar eps) {
    return amax >= bmin - eps && bmax >= amin - eps;
}

int count_shared_vertices(Index a0, Index a1, Index a2, Index b0, Index b1, Index b2) {
    int n = 0;
    const std::array<Index, 3> A {a0, a1, a2};
    const std::array<Index, 3> B {b0, b1, b2};
    for (const Index ia : A) {
        for (const Index ib : B) {
            if (ia == ib) {
                ++n;
            }
        }
    }
    return n;
}

bool triangles_intersect_sat(const Point3 ta[3], const Point3 tb[3], Scalar sat_eps) {
    const Vec3 n1 = crossv(vsub(ta[1], ta[0]), vsub(ta[2], ta[0]));
    const Vec3 n2 = crossv(vsub(tb[1], tb[0]), vsub(tb[2], tb[0]));

    std::array<Vec3, 11> axes {};
    std::size_t axis_count = 0;
    auto push_axis = [&axes, &axis_count](Vec3 a) {
        normalize_or_zero(&a);
        if (length_squared(a) < 1e-30) {
            return;
        }
        axes[axis_count++] = a;
    };

    push_axis(n1);
    push_axis(n2);

    const Vec3 a01 = vsub(ta[1], ta[0]);
    const Vec3 a12 = vsub(ta[2], ta[1]);
    const Vec3 a20 = vsub(ta[0], ta[2]);
    const Vec3 b01 = vsub(tb[1], tb[0]);
    const Vec3 b12 = vsub(tb[2], tb[1]);
    const Vec3 b20 = vsub(tb[0], tb[2]);

    const std::array<Vec3, 3> ea {a01, a12, a20};
    const std::array<Vec3, 3> eb {b01, b12, b20};
    for (const Vec3& u : ea) {
        for (const Vec3& v : eb) {
            push_axis(crossv(u, v));
        }
    }

    if (axis_count == 0) {
        return false;
    }

    for (std::size_t k = 0; k < axis_count; ++k) {
        const Vec3& ax = axes[k];
        Scalar amin = 0.0;
        Scalar amax = 0.0;
        Scalar bmin = 0.0;
        Scalar bmax = 0.0;
        project_interval(ta, ax, &amin, &amax);
        project_interval(tb, ax, &bmin, &bmax);
        if (!intervals_overlap(amin, amax, bmin, bmax, sat_eps)) {
            return false;
        }
    }
    return true;
}

}  // namespace

MeshSelfIntersectionResult analyze_mesh_self_intersection(const MeshRecord& mesh,
                                                          Scalar linear_tolerance,
                                                          std::size_t max_triangles) {
    MeshSelfIntersectionResult out;
    if (mesh.indices.size() % 3 != 0 || mesh.vertices.empty()) {
        out.status = MeshSelfIntersectionStatus::InvalidMesh;
        return out;
    }

    // 分析前按容差尺度重焊：避免共边三角形因数值缝隙未共享顶点而被 SAT 误判为相交。
    MeshRecord work = mesh;
    work.texcoords.clear();
    Scalar mesh_diag = Scalar(1.0);
    if (!mesh.vertices.empty()) {
        Point3 mn = mesh.vertices.front();
        Point3 mx = mesh.vertices.front();
        for (const Point3& p : mesh.vertices) {
            mn.x = std::min(mn.x, p.x);
            mn.y = std::min(mn.y, p.y);
            mn.z = std::min(mn.z, p.z);
            mx.x = std::max(mx.x, p.x);
            mx.y = std::max(mx.y, p.y);
            mx.z = std::max(mx.z, p.z);
        }
        const Scalar dx = mx.x - mn.x;
        const Scalar dy = mx.y - mn.y;
        const Scalar dz = mx.z - mn.z;
        mesh_diag = std::sqrt(std::max(Scalar(0.0), dx * dx + dy * dy + dz * dz));
    }
    const Scalar weld_q =
        std::max({linear_tolerance * Scalar(8.0), mesh_diag * Scalar(1e-7), Scalar(1e-9)});
    weld_mesh_vertices_quantized(work, !work.normals.empty() && work.normals.size() == work.vertices.size(),
                                 weld_q);

    const std::size_t n_tri = work.indices.size() / 3;
    if (n_tri > max_triangles) {
        out.status = MeshSelfIntersectionStatus::TooManyTriangles;
        return out;
    }
    if (n_tri < 2) {
        return out;
    }

    const Scalar sat_eps =
        std::max<Scalar>(linear_tolerance * 4.0, std::numeric_limits<Scalar>::epsilon() * 1e4);
    const Scalar bbox_eps = std::max<Scalar>(linear_tolerance * 8.0, 1e-12);
    const Scalar min_area2 =
        std::max<Scalar>(linear_tolerance * linear_tolerance * 1e-8, std::numeric_limits<Scalar>::min() * 1e6);

    std::vector<BoundingBox> tbbs;
    std::vector<Scalar> areas2;
    tbbs.resize(n_tri);
    areas2.resize(n_tri);

    for (std::size_t t = 0; t < n_tri; ++t) {
        const std::size_t base = t * 3;
        const Index i0 = work.indices[base];
        const Index i1 = work.indices[base + 1];
        const Index i2 = work.indices[base + 2];
        if (static_cast<std::size_t>(i0) >= work.vertices.size() ||
            static_cast<std::size_t>(i1) >= work.vertices.size() ||
            static_cast<std::size_t>(i2) >= work.vertices.size()) {
            out.status = MeshSelfIntersectionStatus::InvalidMesh;
            return out;
        }
        const Point3& p0 = work.vertices[static_cast<std::size_t>(i0)];
        const Point3& p1 = work.vertices[static_cast<std::size_t>(i1)];
        const Point3& p2 = work.vertices[static_cast<std::size_t>(i2)];
        tbbs[t] = triangle_bbox(p0, p1, p2);
        areas2[t] = triangle_area_squared(p0, p1, p2);
    }

    Point3 ta[3];
    Point3 tb[3];

    for (std::size_t i = 0; i + 1 < n_tri; ++i) {
        if (areas2[i] < min_area2) {
            continue;
        }
        const std::size_t ib0 = i * 3;
        const Index ia0 = work.indices[ib0];
        const Index ia1 = work.indices[ib0 + 1];
        const Index ia2 = work.indices[ib0 + 2];
        ta[0] = work.vertices[static_cast<std::size_t>(ia0)];
        ta[1] = work.vertices[static_cast<std::size_t>(ia1)];
        ta[2] = work.vertices[static_cast<std::size_t>(ia2)];

        for (std::size_t j = i + 1; j < n_tri; ++j) {
            if (areas2[j] < min_area2) {
                continue;
            }
            // 共享顶点：闭合流形上相邻面常在角点仅共 1 顶点；SAT 易将其判为相交，工业上属合法接触。
            if (count_shared_vertices(ia0, ia1, ia2, work.indices[j * 3], work.indices[j * 3 + 1],
                                      work.indices[j * 3 + 2]) >= 1) {
                continue;
            }
            if (!bbox_overlap_eps(tbbs[i], tbbs[j], bbox_eps)) {
                continue;
            }
            const std::size_t jb0 = j * 3;
            const Index ja0 = work.indices[jb0];
            const Index ja1 = work.indices[jb0 + 1];
            const Index ja2 = work.indices[jb0 + 2];
            tb[0] = work.vertices[static_cast<std::size_t>(ja0)];
            tb[1] = work.vertices[static_cast<std::size_t>(ja1)];
            tb[2] = work.vertices[static_cast<std::size_t>(ja2)];

            if (triangles_intersect_sat(ta, tb, sat_eps)) {
                out.status = MeshSelfIntersectionStatus::Hit;
                out.tri_i = i;
                out.tri_j = j;
                return out;
            }
        }
    }
    return out;
}

}  // namespace axiom::detail
