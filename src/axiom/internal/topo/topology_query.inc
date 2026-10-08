namespace {

thread_local int g_topo_query_audit_depth = 0;

/// 每个「顶层」TopologyQueryService 公共入口计一次读操作；内部再调其它查询方法时不重复计数。
struct TopoQueryAuditScope {
  explicit TopoQueryAuditScope(const std::shared_ptr<detail::KernelState> &state)
      : state_(state) {
    if (state_) {
      if (g_topo_query_audit_depth == 0) {
        ++state_->topology_query_op_count;
      }
      ++g_topo_query_audit_depth;
    }
  }
  ~TopoQueryAuditScope() {
    if (state_) {
      --g_topo_query_audit_depth;
    }
  }
  TopoQueryAuditScope(const TopoQueryAuditScope &) = delete;
  TopoQueryAuditScope &operator=(const TopoQueryAuditScope &) = delete;
  std::shared_ptr<detail::KernelState> state_;
};

struct AnalyticAreaSurface {
  detail::SurfaceKind kind{detail::SurfaceKind::Plane};
  const detail::SurfaceRecord *base{nullptr};
  Scalar offset{0.0};
  Range2D domain{{-std::numeric_limits<Scalar>::infinity(),
                  std::numeric_limits<Scalar>::infinity()},
                 {-std::numeric_limits<Scalar>::infinity(),
                  std::numeric_limits<Scalar>::infinity()}};
};

bool tighten_area_interval(Range1D &current, Scalar lo, Scalar hi) {
  if (!std::isfinite(lo) || !std::isfinite(hi) || !(lo < hi)) return false;
  current.min = std::max(current.min, lo);
  current.max = std::min(current.max, hi);
  return current.min < current.max;
}

bool resolve_analytic_area_surface(const detail::KernelState &state,
                                   SurfaceId surface_id,
                                   AnalyticAreaSurface &out) {
  std::unordered_set<std::uint64_t> visited;
  SurfaceId current = surface_id;
  while (visited.insert(current.value).second) {
    const auto it = state.surfaces.find(current.value);
    if (it == state.surfaces.end()) return false;
    const auto &surface = it->second;
    if (surface.kind == detail::SurfaceKind::Trimmed) {
      if (!tighten_area_interval(out.domain.u, surface.trim_u_min,
                                 surface.trim_u_max) ||
          !tighten_area_interval(out.domain.v, surface.trim_v_min,
                                 surface.trim_v_max) ||
          surface.base_surface_id.value == 0) {
        return false;
      }
      current = surface.base_surface_id;
      continue;
    }
    if (surface.kind == detail::SurfaceKind::Offset) {
      if (!std::isfinite(surface.offset_distance) ||
          surface.base_surface_id.value == 0) {
        return false;
      }
      out.offset += surface.offset_distance;
      if (!std::isfinite(out.offset)) return false;
      current = surface.base_surface_id;
      continue;
    }
    out.kind = surface.kind;
    out.base = &surface;
    const Scalar pi = std::acos(-1.0);
    switch (surface.kind) {
    case detail::SurfaceKind::Plane:
      return true;
    case detail::SurfaceKind::Cylinder:
      return tighten_area_interval(out.domain.u, 0.0, 2.0 * pi) &&
             surface.radius_a + out.offset > 0.0;
    case detail::SurfaceKind::Cone:
      out.domain.u.min = std::max(out.domain.u.min, 0.0);
      out.domain.u.max = std::min(out.domain.u.max, 2.0 * pi);
      out.domain.v.min = std::max(out.domain.v.min, 0.0);
      return out.domain.u.min < out.domain.u.max &&
             out.domain.v.min < out.domain.v.max;
    case detail::SurfaceKind::Sphere:
      return tighten_area_interval(out.domain.u, 0.0, 2.0 * pi) &&
             tighten_area_interval(out.domain.v, 0.0, pi) &&
             surface.radius_a + out.offset > 0.0;
    case detail::SurfaceKind::Torus:
      return tighten_area_interval(out.domain.u, 0.0, 2.0 * pi) &&
             tighten_area_interval(out.domain.v, 0.0, 2.0 * pi) &&
             surface.radius_b + out.offset > 0.0;
    default:
      return true; // Resolved successfully, but the caller reports unsupported kind.
    }
  }
  return false;
}

long double analytic_area_density(const AnalyticAreaSurface &surface,
                                  long double v) {
  const auto &base = *surface.base;
  const long double d = surface.offset;
  switch (surface.kind) {
  case detail::SurfaceKind::Plane:
    return 1.0L;
  case detail::SurfaceKind::Cylinder:
    return std::abs(static_cast<long double>(base.radius_a) + d);
  case detail::SurfaceKind::Cone: {
    const long double angle = base.semi_angle;
    const long double radial = std::tan(angle) * v + d * std::cos(angle);
    return std::abs(radial) / std::cos(angle);
  }
  case detail::SurfaceKind::Sphere: {
    const long double radius = static_cast<long double>(base.radius_a) + d;
    return radius * radius * std::abs(std::sin(v));
  }
  case detail::SurfaceKind::Torus: {
    const long double minor = static_cast<long double>(base.radius_b) + d;
    const long double ring = static_cast<long double>(base.radius_a) + minor * std::cos(v);
    return std::abs(minor * ring);
  }
  default:
    return std::numeric_limits<long double>::quiet_NaN();
  }
}

Point3 analytic_area_surface_point(const AnalyticAreaSurface &surface,
                                   const Point2 &uv) {
  const auto &base = *surface.base;
  const auto w = detail::normalize(base.normal);
  const auto reference = std::abs(w.z) < 0.9 ? Vec3{0.0, 0.0, 1.0}
                                             : Vec3{0.0, 1.0, 0.0};
  const auto axis_u = detail::normalize(detail::cross(reference, w));
  const auto axis_v = detail::normalize(detail::cross(w, axis_u));
  const auto from_local = [&](long double x, long double y, long double z) {
    return Point3{
        static_cast<Scalar>(base.origin.x + axis_u.x * x + axis_v.x * y + w.x * z),
        static_cast<Scalar>(base.origin.y + axis_u.y * x + axis_v.y * y + w.y * z),
        static_cast<Scalar>(base.origin.z + axis_u.z * x + axis_v.z * y + w.z * z)};
  };
  const long double u = uv.x;
  const long double v = uv.y;
  const long double d = surface.offset;
  switch (surface.kind) {
  case detail::SurfaceKind::Plane:
    return from_local(u, v, d);
  case detail::SurfaceKind::Cylinder: {
    const long double radius = static_cast<long double>(base.radius_a) + d;
    return from_local(radius * std::cos(u), radius * std::sin(u), v);
  }
  case detail::SurfaceKind::Cone: {
    const long double angle = base.semi_angle;
    const long double radius = std::tan(angle) * v + d * std::cos(angle);
    return from_local(radius * std::cos(u), radius * std::sin(u),
                      v - d * std::sin(angle));
  }
  case detail::SurfaceKind::Sphere: {
    const long double radius = static_cast<long double>(base.radius_a) + d;
    // SphereService intentionally uses the world XYZ spherical frame.
    return Point3{static_cast<Scalar>(base.origin.x + radius * std::cos(u) * std::sin(v)),
                  static_cast<Scalar>(base.origin.y + radius * std::sin(u) * std::sin(v)),
                  static_cast<Scalar>(base.origin.z + radius * std::cos(v))};
  }
  case detail::SurfaceKind::Torus: {
    const long double minor = static_cast<long double>(base.radius_b) + d;
    const long double ring = static_cast<long double>(base.radius_a) + minor * std::cos(v);
    return from_local(ring * std::cos(u), ring * std::sin(u), minor * std::sin(v));
  }
  default:
    return {};
  }
}

long double weighted_uv_ring_area(const std::vector<Point2> &ring,
                                  const AnalyticAreaSurface &surface) {
  // Green: integral_D J(v) du dv = integral_boundary u J(v) dv.  Eight-point
  // Gauss-Legendre is exact for constant metrics and stable for the trigonometric
  // analytic metrics over the bounded standard surface domains.
  constexpr std::array<long double, 4> nodes{
      0.18343464249564980494L, 0.52553240991632898582L,
      0.79666647741362673959L, 0.96028985649753623168L};
  constexpr std::array<long double, 4> weights{
      0.36268378337836198297L, 0.31370664587788728734L,
      0.22238103445337447054L, 0.10122853629037625915L};
  long double integral = 0.0L;
  for (std::size_t i = 0; i < ring.size(); ++i) {
    const auto &a = ring[i];
    const auto &b = ring[(i + 1) % ring.size()];
    const long double du = static_cast<long double>(b.x) - a.x;
    const long double dv = static_cast<long double>(b.y) - a.y;
    if (dv == 0.0L) continue;
    long double segment = 0.0L;
    for (std::size_t q = 0; q < nodes.size(); ++q) {
      for (const long double sign : {-1.0L, 1.0L}) {
        const long double t = 0.5L * (1.0L + sign * nodes[q]);
        const long double u = static_cast<long double>(a.x) + du * t;
        const long double v = static_cast<long double>(a.y) + dv * t;
        segment += weights[q] * u * analytic_area_density(surface, v);
      }
    }
    integral += 0.5L * dv * segment;
  }
  return integral;
}

long double uv_orientation(const Point2 &a, const Point2 &b, const Point2 &c) {
  return (static_cast<long double>(b.x) - a.x) *
             (static_cast<long double>(c.y) - a.y) -
         (static_cast<long double>(b.y) - a.y) *
             (static_cast<long double>(c.x) - a.x);
}

bool uv_point_on_segment(const Point2 &p, const Point2 &a, const Point2 &b,
                         long double tolerance) {
  const long double cross = std::abs(uv_orientation(a, b, p));
  const long double scale = std::hypot(static_cast<long double>(b.x) - a.x,
                                       static_cast<long double>(b.y) - a.y);
  if (cross > tolerance * std::max(1.0L, scale)) return false;
  return static_cast<long double>(p.x) >= std::min(a.x, b.x) - tolerance &&
         static_cast<long double>(p.x) <= std::max(a.x, b.x) + tolerance &&
         static_cast<long double>(p.y) >= std::min(a.y, b.y) - tolerance &&
         static_cast<long double>(p.y) <= std::max(a.y, b.y) + tolerance;
}

bool uv_segments_intersect(const Point2 &a, const Point2 &b, const Point2 &c,
                           const Point2 &d, long double tolerance) {
  const auto oa = uv_orientation(a, b, c);
  const auto ob = uv_orientation(a, b, d);
  const auto oc = uv_orientation(c, d, a);
  const auto od = uv_orientation(c, d, b);
  if (((oa > tolerance && ob < -tolerance) ||
       (oa < -tolerance && ob > tolerance)) &&
      ((oc > tolerance && od < -tolerance) ||
       (oc < -tolerance && od > tolerance))) return true;
  return (std::abs(oa) <= tolerance && uv_point_on_segment(c, a, b, tolerance)) ||
         (std::abs(ob) <= tolerance && uv_point_on_segment(d, a, b, tolerance)) ||
         (std::abs(oc) <= tolerance && uv_point_on_segment(a, c, d, tolerance)) ||
         (std::abs(od) <= tolerance && uv_point_on_segment(b, c, d, tolerance));
}

bool simple_uv_ring(const std::vector<Point2> &ring, long double tolerance) {
  if (ring.size() < 3) return false;
  for (std::size_t i = 0; i < ring.size(); ++i) {
    const std::size_t i1 = (i + 1) % ring.size();
    if (uv_point_on_segment(ring[i], ring[i], ring[i1], tolerance) &&
        std::hypot(static_cast<long double>(ring[i1].x) - ring[i].x,
                   static_cast<long double>(ring[i1].y) - ring[i].y) <= tolerance)
      return false;
    for (std::size_t j = i + 1; j < ring.size(); ++j) {
      const std::size_t j1 = (j + 1) % ring.size();
      if (j == i || j == i1 || j1 == i) continue;
      if (uv_segments_intersect(ring[i], ring[i1], ring[j], ring[j1], tolerance))
        return false;
    }
  }
  return true;
}

bool rings_touch_or_cross(const std::vector<Point2> &a,
                          const std::vector<Point2> &b,
                          long double tolerance) {
  for (std::size_t i = 0; i < a.size(); ++i)
    for (std::size_t j = 0; j < b.size(); ++j)
      if (uv_segments_intersect(a[i], a[(i + 1) % a.size()], b[j],
                                b[(j + 1) % b.size()], tolerance))
        return true;
  return false;
}

struct QueryShellMesh {
  ShellId shell{};
  std::vector<Point3> points;
  std::vector<std::array<int, 3>> triangles;
  std::vector<FaceId> triangle_faces;
  BoundingBox bbox{};
};

struct BodyShellAnalysis {
  std::vector<BodyShellRegion> regions;
  std::vector<MassProperties> properties;
  std::vector<QueryShellMesh> meshes;
};

void extend_query_bbox(BoundingBox &bbox, const Point3 &point) {
  if (!bbox.is_valid) {
    bbox.min = bbox.max = point;
    bbox.is_valid = true;
    return;
  }
  bbox.min.x = std::min(bbox.min.x, point.x);
  bbox.min.y = std::min(bbox.min.y, point.y);
  bbox.min.z = std::min(bbox.min.z, point.z);
  bbox.max.x = std::max(bbox.max.x, point.x);
  bbox.max.y = std::max(bbox.max.y, point.y);
  bbox.max.z = std::max(bbox.max.z, point.z);
}

/// `shell_mass_properties` has already performed the detailed topology,
/// straight-edge and face-orientation validation.  This extraction deliberately
/// rebuilds only the transient world-space triangles required for shell-to-shell
/// classification; it never publishes a MeshId or populates a tessellation cache.
bool extract_query_shell_mesh(const detail::KernelState &state, ShellId shell_id,
                              Scalar tolerance, QueryShellMesh &mesh) {
  const auto shell_it = state.shells.find(shell_id.value);
  if (shell_it == state.shells.end()) return false;
  mesh = {};
  mesh.shell = shell_id;
  for (const auto face_id : shell_it->second.faces) {
    const auto face_it = state.faces.find(face_id.value);
    if (face_it == state.faces.end()) return false;
    const auto surface_it = state.surfaces.find(face_it->second.surface_id.value);
    if (surface_it == state.surfaces.end() ||
        surface_it->second.kind != detail::SurfaceKind::Plane) return false;
    const auto normal = detail::normalize(surface_it->second.normal);
    if (!(detail::norm(normal) > 0.0)) return false;

    std::vector<LoopId> loops{face_it->second.outer_loop};
    loops.insert(loops.end(), face_it->second.inner_loops.begin(),
                 face_it->second.inner_loops.end());
    std::vector<std::vector<Point3>> rings;
    rings.reserve(loops.size());
    for (const auto loop_id : loops) {
      std::vector<Point3> ring;
      std::string reason;
      if (!loop_vertex_chain_3d(state, loop_id, ring, reason) || ring.size() < 3)
        return false;
      rings.push_back(std::move(ring));
    }
    if (rings.size() == 1 && rings.front().size() == 3) {
      if (mesh.points.size() + 3 >
          static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
      const int base = static_cast<int>(mesh.points.size());
      for (const auto &point : rings.front()) {
        extend_query_bbox(mesh.bbox, point);
        mesh.points.push_back(point);
      }
      mesh.triangles.push_back({base, base + 1, base + 2});
      mesh.triangle_faces.push_back(face_id);
      continue;
    }
    std::vector<Point3> face_points;
    std::vector<std::pair<int, int>> boundary;
    std::vector<std::array<int, 3>> face_triangles;
    std::vector<std::vector<Point3>> holes(rings.begin() + 1, rings.end());
    if (!detail::triangulate_extrude_region(
            rings.front(), holes, normal, tolerance, face_points, boundary,
            face_triangles, true) || face_triangles.empty() ||
        mesh.points.size() + face_points.size() >
            static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
    const int base = static_cast<int>(mesh.points.size());
    for (const auto &point : face_points) {
      extend_query_bbox(mesh.bbox, point);
      mesh.points.push_back(point);
    }
    for (const auto &triangle : face_triangles) {
      mesh.triangles.push_back(
          {triangle[0] + base, triangle[1] + base, triangle[2] + base});
      mesh.triangle_faces.push_back(face_id);
    }
  }
  return mesh.bbox.is_valid && !mesh.triangles.empty();
}

Vec3 query_vector(const Point3 &from, const Point3 &to) {
  return Vec3{to.x - from.x, to.y - from.y, to.z - from.z};
}

long double query_dot(const Vec3 &a, const Vec3 &b) {
  return static_cast<long double>(a.x) * b.x +
         static_cast<long double>(a.y) * b.y +
         static_cast<long double>(a.z) * b.z;
}

Vec3 query_cross(const Vec3 &a, const Vec3 &b) {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x};
}

long double query_length(const Vec3 &vector) {
  return std::sqrt(std::max(0.0L, query_dot(vector, vector)));
}

bool query_bbox_overlap(const BoundingBox &a, const BoundingBox &b,
                        long double tolerance) {
  return a.is_valid && b.is_valid &&
         static_cast<long double>(a.max.x) >= b.min.x - tolerance &&
         static_cast<long double>(b.max.x) >= a.min.x - tolerance &&
         static_cast<long double>(a.max.y) >= b.min.y - tolerance &&
         static_cast<long double>(b.max.y) >= a.min.y - tolerance &&
         static_cast<long double>(a.max.z) >= b.min.z - tolerance &&
         static_cast<long double>(b.max.z) >= a.min.z - tolerance;
}

BoundingBox query_triangle_bbox(const std::array<Point3, 3> &triangle) {
  BoundingBox bbox;
  for (const auto &point : triangle) extend_query_bbox(bbox, point);
  return bbox;
}

void query_project_triangle(const std::array<Point3, 3> &triangle,
                            const Vec3 &unit_axis, long double &minimum,
                            long double &maximum) {
  minimum = maximum = static_cast<long double>(triangle[0].x) * unit_axis.x +
                      static_cast<long double>(triangle[0].y) * unit_axis.y +
                      static_cast<long double>(triangle[0].z) * unit_axis.z;
  for (std::size_t i = 1; i < triangle.size(); ++i) {
    const long double value =
        static_cast<long double>(triangle[i].x) * unit_axis.x +
        static_cast<long double>(triangle[i].y) * unit_axis.y +
        static_cast<long double>(triangle[i].z) * unit_axis.z;
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
  }
}

/// Complete separating-axis test for two filled triangles.  In addition to
/// face normals and the nine edge cross-products, the six in-plane edge-normal
/// axes are retained so disjoint coplanar triangles are not reported as hits.
bool query_triangles_touch(const std::array<Point3, 3> &a,
                           const std::array<Point3, 3> &b,
                           long double tolerance) {
  if (!query_bbox_overlap(query_triangle_bbox(a), query_triangle_bbox(b), tolerance))
    return false;
  const std::array<Vec3, 3> a_edges{
      query_vector(a[0], a[1]), query_vector(a[1], a[2]),
      query_vector(a[2], a[0])};
  const std::array<Vec3, 3> b_edges{
      query_vector(b[0], b[1]), query_vector(b[1], b[2]),
      query_vector(b[2], b[0])};
  const auto a_normal = query_cross(a_edges[0], a_edges[1]);
  const auto b_normal = query_cross(b_edges[0], b_edges[1]);
  std::array<Vec3, 17> axes{};
  std::size_t count = 0;
  axes[count++] = a_normal;
  axes[count++] = b_normal;
  for (const auto &a_edge : a_edges)
    for (const auto &b_edge : b_edges)
      axes[count++] = query_cross(a_edge, b_edge);
  for (const auto &edge : a_edges) axes[count++] = query_cross(a_normal, edge);
  for (const auto &edge : b_edges) axes[count++] = query_cross(b_normal, edge);

  for (std::size_t i = 0; i < count; ++i) {
    const long double length = query_length(axes[i]);
    if (!(length > 1e-24L)) continue;
    const Vec3 unit{static_cast<Scalar>(axes[i].x / length),
                    static_cast<Scalar>(axes[i].y / length),
                    static_cast<Scalar>(axes[i].z / length)};
    long double a_min{}, a_max{}, b_min{}, b_max{};
    query_project_triangle(a, unit, a_min, a_max);
    query_project_triangle(b, unit, b_min, b_max);
    if (a_max < b_min - tolerance || b_max < a_min - tolerance) return false;
  }
  return true;
}

std::array<Point3, 3> query_triangle(const QueryShellMesh &mesh,
                                     const std::array<int, 3> &indices) {
  return {mesh.points[static_cast<std::size_t>(indices[0])],
          mesh.points[static_cast<std::size_t>(indices[1])],
          mesh.points[static_cast<std::size_t>(indices[2])]};
}

bool query_shell_boundaries_touch(const QueryShellMesh &a,
                                  const QueryShellMesh &b,
                                  long double tolerance) {
  if (!query_bbox_overlap(a.bbox, b.bbox, tolerance)) return false;
  std::vector<BoundingBox> b_boxes;
  b_boxes.reserve(b.triangles.size());
  for (const auto &indices : b.triangles)
    b_boxes.push_back(query_triangle_bbox(query_triangle(b, indices)));
  for (const auto &a_indices : a.triangles) {
    const auto a_triangle = query_triangle(a, a_indices);
    const auto a_bbox = query_triangle_bbox(a_triangle);
    for (std::size_t j = 0; j < b.triangles.size(); ++j) {
      if (!query_bbox_overlap(a_bbox, b_boxes[j], tolerance)) continue;
      if (query_triangles_touch(a_triangle, query_triangle(b, b.triangles[j]),
                                tolerance)) return true;
    }
  }
  return false;
}

/// Generalized winding number for an oriented closed triangle set.  The
/// absolute solid-angle sum makes classification independent of whether the
/// whole shell was authored inward or outward.  Pairwise boundary rejection is
/// performed first, so the representative point cannot lie on this shell.
bool query_point_inside_shell(const Point3 &point, const QueryShellMesh &mesh) {
  const long double pi = std::acos(-1.0L);
  long double solid_angle = 0.0L;
  for (const auto &indices : mesh.triangles) {
    const auto triangle = query_triangle(mesh, indices);
    const auto va = query_vector(point, triangle[0]);
    const auto vb = query_vector(point, triangle[1]);
    const auto vc = query_vector(point, triangle[2]);
    const long double la = query_length(va);
    const long double lb = query_length(vb);
    const long double lc = query_length(vc);
    if (!(la > 0.0L && lb > 0.0L && lc > 0.0L)) return true;
    const long double numerator = query_dot(va, query_cross(vb, vc));
    const long double denominator =
        la * lb * lc + query_dot(va, vb) * lc +
        query_dot(vb, vc) * la + query_dot(vc, va) * lb;
    solid_angle += 2.0L * std::atan2(numerator, denominator);
  }
  return std::abs(solid_angle) > 2.0L * pi;
}

template <class MeasureShell>
Result<BodyShellAnalysis> analyze_body_shells(detail::KernelState &state,
                                              BodyId body_id,
                                              MeasureShell measure_shell,
                                              bool need_single_mesh = false) {
  const auto body_it = state.bodies.find(body_id.value);
  if (body_it == state.bodies.end()) {
    return detail::invalid_input_result<BodyShellAnalysis>(
        state, diag_codes::kCoreInvalidHandle,
        "体闭壳空间关系查询失败：目标体不存在", "体闭壳空间关系查询失败");
  }
  // Several older body factories own a planar bbox shell even though their
  // represented geometry is curved or still a modelling placeholder. Such a
  // shell is not evidence of a real polyhedron. Never integrate or query it as
  // the represented solid. Generic and BlendResult topology is checked below;
  // shell measurement still rejects proxy faces, curved surfaces and curved
  // edges, so only real planar chamfers enter the polyhedron query path.
  const auto &body = body_it->second;
  const bool real_polyhedron = body.rep_kind == RepKind::ExactBRep &&
      (body.kind == detail::BodyKind::Generic || body.kind == detail::BodyKind::Box ||
       body.kind == detail::BodyKind::Wedge ||
       body.kind == detail::BodyKind::BlendResult ||
       (body.kind == detail::BodyKind::BooleanResult && body.boolean_rebuilt_boundary) ||
       (body.kind == detail::BodyKind::Sweep && body.sweep_polyhedral_mass_valid));
  if (!real_polyhedron) {
    auto issue = detail::make_error_issue(diag_codes::kCoreOperationUnsupported,
        "体闭壳查询失败：解析曲面或占位体不具有已声明的真实多面体边界", {body_id.value});
    issue.stage = "query.body.support_gate";
    return error_result<BodyShellAnalysis>(StatusCode::NotImplemented,
        state.create_diagnostic("体闭壳查询失败", {std::move(issue)}));
  }
  BodyShellAnalysis analysis;
  if (body_it->second.shells.empty()) {
    return ok_result(std::move(analysis),
                     state.create_diagnostic("已查询空实体闭壳空间关系"));
  }

  // A lone closed shell is necessarily a material region.  Measuring it is
  // sufficient; transient triangulation and pairwise spatial classification
  // only add work when another shell can establish a nesting relationship.
  if (body_it->second.shells.size() == 1 && !need_single_mesh) {
    const auto shell_id = body_it->second.shells.front();
    const auto properties = measure_shell(shell_id);
    if (!properties.value)
      return error_result<BodyShellAnalysis>(properties.status,
                                             properties.diagnostic_id);
    BodyShellRegion region;
    region.shell = shell_id;
    region.role = BodyShellRole::Material;
    analysis.regions.push_back(region);
    analysis.properties.push_back(*properties.value);
    return ok_result(std::move(analysis),
                     state.create_diagnostic("已查询单一材料闭壳空间关系"));
  }

  const Scalar linear_tolerance = std::max<Scalar>(
      1e-12, std::abs(detail::resolve_linear_tolerance(
                 0.0, state.config.tolerance)));
  std::unordered_set<std::uint64_t> unique_shells;
  auto &meshes = analysis.meshes;
  meshes.reserve(body_it->second.shells.size());
  analysis.properties.reserve(body_it->second.shells.size());
  for (const auto shell_id : body_it->second.shells) {
    if (!unique_shells.insert(shell_id.value).second) {
      return detail::failed_result<BodyShellAnalysis>(
          state, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "体闭壳空间关系查询失败：实体包含重复壳引用",
          "体闭壳空间关系查询失败", {body_id.value, shell_id.value});
    }
    const auto properties = measure_shell(shell_id);
    if (!properties.value)
      return error_result<BodyShellAnalysis>(properties.status,
                                             properties.diagnostic_id);
    QueryShellMesh mesh;
    if (!extract_query_shell_mesh(state, shell_id, linear_tolerance, mesh)) {
      return detail::failed_result<BodyShellAnalysis>(
          state, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "体闭壳空间关系查询失败：已验证壳无法重建临时三角边界",
          "体闭壳空间关系查询失败", {body_id.value, shell_id.value});
    }
    analysis.properties.push_back(*properties.value);
    meshes.push_back(std::move(mesh));
  }

  const std::size_t shell_count = meshes.size();
  std::vector<std::vector<bool>> inside(shell_count,
                                        std::vector<bool>(shell_count, false));
  for (std::size_t i = 0; i < shell_count; ++i) {
    for (std::size_t j = i + 1; j < shell_count; ++j) {
      if (query_shell_boundaries_touch(meshes[i], meshes[j], linear_tolerance)) {
        return detail::failed_result<BodyShellAnalysis>(
            state, StatusCode::InvalidTopology,
            diag_codes::kQueryShellArrangementInvalid,
            "体闭壳空间关系查询失败：两个壳相交、重叠或在建模容差内接触",
            "体闭壳空间关系查询失败",
            {body_id.value, meshes[i].shell.value, meshes[j].shell.value});
      }
      inside[i][j] = query_point_inside_shell(meshes[i].points.front(), meshes[j]);
      inside[j][i] = query_point_inside_shell(meshes[j].points.front(), meshes[i]);
      if (inside[i][j] && inside[j][i]) {
        return detail::failed_result<BodyShellAnalysis>(
            state, StatusCode::InvalidTopology,
            diag_codes::kQueryShellArrangementInvalid,
            "体闭壳空间关系查询失败：壳包含关系矛盾或边界重合",
            "体闭壳空间关系查询失败",
            {body_id.value, meshes[i].shell.value, meshes[j].shell.value});
      }
    }
  }

  analysis.regions.reserve(shell_count);
  for (std::size_t i = 0; i < shell_count; ++i) {
    std::uint32_t depth = 0;
    std::optional<std::size_t> parent;
    for (std::size_t j = 0; j < shell_count; ++j) {
      if (!inside[i][j]) continue;
      ++depth;
      if (!parent || analysis.properties[j].volume <
                         analysis.properties[*parent].volume) parent = j;
    }
    BodyShellRegion region;
    region.shell = meshes[i].shell;
    region.nesting_depth = depth;
    region.role = depth % 2 == 0 ? BodyShellRole::Material
                                 : BodyShellRole::Void;
    if (parent) region.parent_shell = meshes[*parent].shell;
    analysis.regions.push_back(region);
  }
  return ok_result(std::move(analysis),
                   state.create_diagnostic("已查询实体闭壳空间关系"));
}

// Local long-double coordinates keep distance, clipping and winding calculations
// on the same numerical model, including meshes far from the world origin.
using QuerySpatialVector = std::array<long double, 3>;

QuerySpatialVector spatial_delta(const Point3 &from, const Point3 &to) {
  return {static_cast<long double>(to.x) - from.x,
          static_cast<long double>(to.y) - from.y,
          static_cast<long double>(to.z) - from.z};
}

long double spatial_dot(const QuerySpatialVector &a,
                        const QuerySpatialVector &b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

QuerySpatialVector spatial_cross(const QuerySpatialVector &a,
                                 const QuerySpatialVector &b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}

Point3 spatial_interpolate(const Point3 &start, const Point3 &end,
                            long double parameter) {
  return {static_cast<Scalar>((1 - parameter) * start.x + parameter * end.x),
          static_cast<Scalar>((1 - parameter) * start.y + parameter * end.y),
          static_cast<Scalar>((1 - parameter) * start.z + parameter * end.z)};
}

bool spatial_finite_point(const Point3 &point) {
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

std::optional<Scalar> spatial_query_tolerance(
    const detail::KernelState &state, const BodySpatialQueryOptions &options) {
  if (!std::isfinite(options.position_tolerance) ||
      options.position_tolerance < 0 || options.max_triangle_tests == 0)
    return std::nullopt;
  const Scalar tolerance = options.position_tolerance == 0
      ? std::max<Scalar>(1e-12, std::abs(detail::resolve_linear_tolerance(
                                   0, state.config.tolerance)))
      : options.position_tolerance;
  if (!std::isfinite(tolerance) || !(tolerance > 0)) return std::nullopt;
  return tolerance;
}

struct QuerySpatialWork {
  std::uint64_t limit{};
  std::uint64_t count{};
  bool exhausted{};

  bool take() {
    if (count == limit) {
      exhausted = true;
      return false;
    }
    ++count;
    return true;
  }
};

// The closest point is either a clamped edge projection or the perpendicular
// projection into the filled triangle.  Testing all three edges also covers
// vertices without unstable region divisions near a triangle corner.
std::optional<BodyBoundaryPoint> spatial_triangle_closest(
    const Point3 &point, const std::array<Point3, 3> &triangle) {
  long double best_squared = std::numeric_limits<long double>::infinity();
  Point3 best_point{};
  for (std::size_t i = 0; i < 3; ++i) {
    const auto &a = triangle[i];
    const auto &b = triangle[(i + 1) % 3];
    const auto edge = spatial_delta(a, b);
    const auto to_point = spatial_delta(a, point);
    const long double edge_squared = spatial_dot(edge, edge);
    if (!(edge_squared > 0) || !std::isfinite(edge_squared)) return std::nullopt;
    const long double t = std::clamp(spatial_dot(to_point, edge) / edge_squared,
                                    0.0L, 1.0L);
    QuerySpatialVector residual{};
    // Project from the nearer endpoint to preserve a small positive endpoint
    // gap rather than subtracting two almost equal full edge lengths.
    const auto from_end = spatial_delta(b, point);
    for (std::size_t axis = 0; axis < 3; ++axis)
      residual[axis] = t <= .5L ? to_point[axis] - t * edge[axis]
                               : from_end[axis] + (1-t)*edge[axis];
    const long double squared = spatial_dot(residual, residual);
    if (squared < best_squared) {
      best_squared = squared;
      best_point = spatial_interpolate(a, b, t);
    }
  }
  const auto ab = spatial_delta(triangle[0], triangle[1]);
  const auto ac = spatial_delta(triangle[0], triangle[2]);
  const auto ap = spatial_delta(triangle[0], point);
  const auto normal = spatial_cross(ab, ac);
  const long double normal_squared = spatial_dot(normal, normal);
  if (!(normal_squared > 0) || !std::isfinite(normal_squared)) return std::nullopt;
  const long double height = spatial_dot(ap, normal) / normal_squared;
  QuerySpatialVector projection{};
  for (std::size_t axis = 0; axis < 3; ++axis)
    projection[axis] = ap[axis] - height * normal[axis];
  const long double u = spatial_dot(spatial_cross(projection, ac), normal) / normal_squared;
  const long double v = spatial_dot(spatial_cross(ab, projection), normal) / normal_squared;
  if (u >= 0 && v >= 0 && u + v <= 1) {
    const long double squared = height * height * normal_squared;
    if (squared < best_squared) {
      best_squared = squared;
      best_point = {
          static_cast<Scalar>(static_cast<long double>(triangle[0].x) + projection[0]),
          static_cast<Scalar>(static_cast<long double>(triangle[0].y) + projection[1]),
          static_cast<Scalar>(static_cast<long double>(triangle[0].z) + projection[2])};
    }
  }
  const auto witness_delta = spatial_delta(point,best_point);
  const Scalar distance = static_cast<Scalar>(std::sqrt(spatial_dot(witness_delta,witness_delta)));
  if (!std::isfinite(distance) || !spatial_finite_point(best_point)) return std::nullopt;
  BodyBoundaryPoint result;
  result.point = best_point;
  result.distance = distance;
  return result;
}

// Boundary points are handled before winding evaluation.  A certified cell
// must have winding near an integer (0 or +/-1); an unresolved solid-angle sum
// is a numerical failure, never an arbitrary Inside/Outside decision.
std::optional<bool> spatial_inside_material(const Point3 &point,
                                            const std::vector<QueryShellMesh> &meshes,
                                            QuerySpatialWork &work) {
  std::size_t containing_shells = 0;
  const long double pi = std::acos(-1.0L);
  for (const auto &mesh : meshes) {
    if (point.x < mesh.bbox.min.x || point.x > mesh.bbox.max.x ||
        point.y < mesh.bbox.min.y || point.y > mesh.bbox.max.y ||
        point.z < mesh.bbox.min.z || point.z > mesh.bbox.max.z) continue;
    long double angle = 0;
    long double compensation = 0;
    for (const auto &indices : mesh.triangles) {
      if (!work.take()) return std::nullopt;
      const auto triangle = query_triangle(mesh, indices);
      const auto a = spatial_delta(point, triangle[0]);
      const auto b = spatial_delta(point, triangle[1]);
      const auto c = spatial_delta(point, triangle[2]);
      const long double la = std::sqrt(spatial_dot(a, a));
      const long double lb = std::sqrt(spatial_dot(b, b));
      const long double lc = std::sqrt(spatial_dot(c, c));
      if (!(la > 0 && lb > 0 && lc > 0)) return std::nullopt;
      const long double numerator = spatial_dot(a, spatial_cross(b, c));
      const long double denominator = la * lb * lc + spatial_dot(a, b) * lc +
                                      spatial_dot(b, c) * la + spatial_dot(c, a) * lb;
      const long double term = 2 * std::atan2(numerator, denominator);
      if (!std::isfinite(term)) return std::nullopt;
      const long double corrected = term - compensation;
      const long double next = angle + corrected;
      compensation = (next - angle) - corrected;
      angle = next;
    }
    const long double winding = std::abs(angle) / (4 * pi);
    if (!std::isfinite(winding)) return std::nullopt;
    if (std::abs(winding - 1) <= 1e-7L) ++containing_shells;
    else if (winding > 1e-7L) return std::nullopt;
  }
  return containing_shells % 2 != 0;
}

struct QueryTriangleSegmentHit {
  std::array<long double, 2> parameters{};
  bool coplanar{};
};

// Clip in local coordinates against the triangle plane and the three inward
// half-planes.  Coplanar segments produce a span, including a zero-width span
// at a tangent vertex.  Shared edges/triangulation diagonals are deduplicated
// later in the common segment event partition.
std::optional<QueryTriangleSegmentHit> spatial_triangle_segment(
    const Point3 &start, const Point3 &end, const std::array<Point3, 3> &triangle,
    bool &numerical_failure, bool strict_contact = false) {
  const auto ab = spatial_delta(triangle[0], triangle[1]);
  const auto ac = spatial_delta(triangle[0], triangle[2]);
  auto normal = spatial_cross(ab, ac);
  const long double normal_length = std::sqrt(spatial_dot(normal, normal));
  if (!(normal_length > 0) || !std::isfinite(normal_length)) {
    numerical_failure = true;
    return std::nullopt;
  }
  for (auto &component : normal) component /= normal_length;
  const auto from_a = spatial_delta(triangle[0], start);
  const auto to_a = spatial_delta(triangle[0], end);
  const long double ds = spatial_dot(from_a, normal);
  const long double de = spatial_dot(to_a, normal);
  const long double local_scale = std::max({std::sqrt(spatial_dot(ab, ab)),
      std::sqrt(spatial_dot(ac, ac)), std::sqrt(spatial_dot(from_a, from_a)),
      std::sqrt(spatial_dot(to_a, to_a))});
  // Material distance cannot inherit the clipping roundoff band: even a small
  // representable positive gap must not acquire a zero-distance witness.
  const long double roundoff = strict_contact ? 0.0L :
      128 * std::numeric_limits<long double>::epsilon() * local_scale;
  if (!std::isfinite(ds) || !std::isfinite(de) || !std::isfinite(roundoff)) {
    numerical_failure = true;
    return std::nullopt;
  }
  long double lo = 0, hi = 1;
  const bool coplanar = std::abs(ds) <= roundoff && std::abs(de) <= roundoff;
  if (!coplanar) {
    if ((ds > roundoff && de > roundoff) || (ds < -roundoff && de < -roundoff))
      return std::nullopt;
    if (ds == de) return std::nullopt;
    lo = hi = ds / (ds - de);
    if (lo < 0 || lo > 1) return std::nullopt;
  }
  for (std::size_t i = 0; i < 3; ++i) {
    const auto inward = spatial_cross(normal, spatial_delta(triangle[i], triangle[(i + 1) % 3]));
    const long double edge_length = std::sqrt(spatial_dot(inward, inward));
    if (!(edge_length > 0) || !std::isfinite(edge_length)) {
      numerical_failure = true;
      return std::nullopt;
    }
    const long double a = spatial_dot(inward, spatial_delta(triangle[i], start)) / edge_length;
    const long double b = spatial_dot(inward, spatial_delta(triangle[i], end)) / edge_length;
    const long double slope = b - a;
    if (coplanar) {
      if (a < -roundoff && b < -roundoff) return std::nullopt;
      if (slope > 0) lo = std::max(lo, -a / slope);
      else if (slope < 0) hi = std::min(hi, -a / slope);
      else if (a < -roundoff) return std::nullopt;
      if (lo > hi) return std::nullopt;
    } else if (a + lo * slope < -roundoff) return std::nullopt;
  }
  lo = std::clamp(lo, 0.0L, 1.0L);
  hi = std::clamp(hi, 0.0L, 1.0L);
  if (lo > hi) return std::nullopt;
  return QueryTriangleSegmentHit{{lo, hi}, coplanar};
}

template <class T>
Result<T> spatial_failure(detail::KernelState &state, StatusCode status,
                          std::string_view code, std::string message,
                          std::string_view stage, std::vector<std::uint64_t> entities) {
  auto issue = detail::make_error_issue(code, std::move(message), std::move(entities));
  issue.stage = std::string(stage);
  return error_result<T>(status, state.create_diagnostic("实体几何查询失败", {std::move(issue)}));
}

template <class T, class U>
Result<T> spatial_preflight_failure(detail::KernelState &state, const Result<U> &source,
                                    std::string_view operation) {
  std::vector<Issue> issues;
  const auto diagnostic = state.diagnostics.find(source.diagnostic_id.value);
  if (diagnostic != state.diagnostics.end()) issues = diagnostic->second.issues;
  const std::string stage = std::string(operation) +
      (source.status == StatusCode::NotImplemented ? ".support_gate" :
       operation == "query.mass_properties" &&
           (source.status == StatusCode::NumericalInstability || source.status == StatusCode::DegenerateGeometry)
           ? ".numeric" : ".preflight");
  for (auto &issue : issues) issue.stage = stage;
  if (issues.empty()) {
    auto issue = detail::make_error_issue(diag_codes::kTopoInvariantBroken,
                                         "实体查询前置检查失败");
    issue.stage = stage;
    issues.push_back(std::move(issue));
  }
  return error_result<T>(source.status,
      state.create_diagnostic("实体查询前置检查失败", std::move(issues)));
}

// Complete triangle distance: crossing edges, six vertex/face projections and
// nine interior edge/edge projections. End-clamped edge minima are already
// included by vertex/face projections. All divisions use local long doubles.
std::optional<BodyDistanceQuery> spatial_triangle_distance(
    const std::array<Point3, 3> &a, const std::array<Point3, 3> &b) {
  BodyDistanceQuery best;
  best.distance = std::numeric_limits<Scalar>::infinity();
  const auto consider = [&](const Point3 &first, const Point3 &second) {
    // The public distance must measure the returned world-space witnesses;
    // local projection residuals can lose a tiny endpoint gap by cancellation.
    const auto delta = spatial_delta(first,second);
    const long double distance = std::sqrt(spatial_dot(delta,delta));
    if (!spatial_finite_point(first) || !spatial_finite_point(second) ||
        !std::isfinite(distance) || distance > std::numeric_limits<Scalar>::max()) return false;
    if (distance < best.distance) {
      best.distance = static_cast<Scalar>(distance);
      best.first_point = first;
      best.second_point = second;
    }
    return true;
  };
  for (std::size_t i = 0; i < 3; ++i) {
    bool failed = false;
    const auto ab = spatial_triangle_segment(a[i], a[(i+1)%3], b, failed, true);
    const auto ba = spatial_triangle_segment(b[i], b[(i+1)%3], a, failed, true);
    if (failed) return std::nullopt;
    if (ab || ba) {
      best.distance = 0;
      best.first_point = best.second_point = ab
          ? spatial_interpolate(a[i], a[(i+1)%3], ab->parameters[0])
          : spatial_interpolate(b[i], b[(i+1)%3], ba->parameters[0]);
      return spatial_finite_point(best.first_point) ? std::optional{best} : std::nullopt;
    }
    const auto from_a = spatial_triangle_closest(a[i], b);
    const auto from_b = spatial_triangle_closest(b[i], a);
    if (!from_a || !from_b ||
        !consider(a[i], from_a->point) ||
        !consider(from_b->point, b[i])) return std::nullopt;
  }
  for (std::size_t i = 0; i < 3; ++i) {
    const auto u = spatial_delta(a[i], a[(i+1)%3]);
    for (std::size_t j = 0; j < 3; ++j) {
      const auto v = spatial_delta(b[j], b[(j+1)%3]);
      const auto w = spatial_delta(b[j], a[i]);
      const auto cross = spatial_cross(u, v);
      const long double determinant = spatial_dot(cross, cross);
      if (!(determinant > 0)) continue; // parallel: endpoint projections suffice
      const long double uu = spatial_dot(u,u), uv = spatial_dot(u,v), vv = spatial_dot(v,v);
      const long double uw = spatial_dot(u,w), vw = spatial_dot(v,w);
      const long double s = (uv*vw-vv*uw)/determinant;
      const long double t = (uu*vw-uv*uw)/determinant;
      if (s < 0 || s > 1 || t < 0 || t > 1) continue;
      if (!consider(spatial_interpolate(a[i],a[(i+1)%3],s),
                    spatial_interpolate(b[j],b[(j+1)%3],t))) return std::nullopt;
    }
  }
  return std::isfinite(best.distance) ? std::optional{best} : std::nullopt;
}

}  // namespace

TopologyQueryService::TopologyQueryService(
    std::shared_ptr<detail::KernelState> state)
    : state_(std::move(state)) {}

Result<std::array<VertexId, 2>>
TopologyQueryService::vertices_of_edge(EdgeId edge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->edges.find(edge_id.value);
  if (it == state_->edges.end()) {
    return detail::invalid_input_result<std::array<VertexId, 2>>(
        *state_, diag_codes::kCoreInvalidHandle, "边查询失败：目标边不存在",
        "边端点查询失败");
  }
  return ok_result(std::array<VertexId, 2>{it->second.v0, it->second.v1},
                   state_->create_diagnostic("已查询边端点"));
}

Result<std::vector<CoedgeId>>
TopologyQueryService::coedges_of_edge(EdgeId edge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  if (!detail::has_edge(*state_, edge_id)) {
    return detail::invalid_input_result<std::vector<CoedgeId>>(
        *state_, diag_codes::kCoreInvalidHandle, "边查询失败：目标边不存在",
        "边定向边集合查询失败");
  }

  std::vector<CoedgeId> coedges;
  const auto index_it = state_->edge_to_coedges.find(edge_id.value);
  if (index_it != state_->edge_to_coedges.end()) {
    coedges.reserve(index_it->second.size());
    for (const auto coedge_value : index_it->second) {
      coedges.push_back(CoedgeId{coedge_value});
    }
  }
  return ok_result(coedges, state_->create_diagnostic("已查询边定向边集合"));
}

Result<std::vector<LoopId>>
TopologyQueryService::loops_of_edge(EdgeId edge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  if (!detail::has_edge(*state_, edge_id)) {
    return detail::invalid_input_result<std::vector<LoopId>>(
        *state_, diag_codes::kCoreInvalidHandle, "边查询失败：目标边不存在",
        "边所属环集合查询失败");
  }

  std::vector<LoopId> loops;
  const auto coedges_result = coedges_of_edge(edge_id);
  if (coedges_result.status != StatusCode::Ok ||
      !coedges_result.value.has_value()) {
    return detail::failed_result<std::vector<LoopId>>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoCurveTopologyMismatch,
        "边查询失败：无法稳定解析边定向边集合", "边所属环集合查询失败");
  }
  for (const auto coedge_id : *coedges_result.value) {
    const auto loop_it = state_->coedge_to_loop.find(coedge_id.value);
    if (loop_it != state_->coedge_to_loop.end()) {
      append_unique(loops, LoopId{loop_it->second});
    }
  }
  return ok_result(loops, state_->create_diagnostic("已查询边所属环集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::faces_of_edge(EdgeId edge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  if (!detail::has_edge(*state_, edge_id)) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kCoreInvalidHandle, "边查询失败：目标边不存在",
        "边所属面集合查询失败");
  }

  const auto loops_result = loops_of_edge(edge_id);
  if (loops_result.status != StatusCode::Ok ||
      !loops_result.value.has_value()) {
    return detail::failed_result<std::vector<FaceId>>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoCurveTopologyMismatch,
        "边查询失败：无法稳定解析边所属环", "边所属面集合查询失败");
  }

  std::vector<FaceId> faces;
  for (const auto loop_id : *loops_result.value) {
    const auto face_it = state_->loop_to_faces.find(loop_id.value);
    if (face_it == state_->loop_to_faces.end()) {
      continue;
    }
    for (const auto face_value : face_it->second) {
      append_unique(faces, FaceId{face_value});
    }
  }
  return ok_result(faces, state_->create_diagnostic("已查询边所属面集合"));
}

Result<std::vector<ShellId>>
TopologyQueryService::shells_of_edge(EdgeId edge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto faces_result = faces_of_edge(edge_id);
  if (faces_result.status != StatusCode::Ok ||
      !faces_result.value.has_value()) {
    return detail::failed_result<std::vector<ShellId>>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoCurveTopologyMismatch,
        "边查询失败：无法稳定解析边所属面集合", "边所属壳集合查询失败");
  }

  std::vector<ShellId> shells;
  for (const auto face_id : *faces_result.value) {
    const auto shell_it = state_->face_to_shells.find(face_id.value);
    if (shell_it == state_->face_to_shells.end()) {
      continue;
    }
    for (const auto shell_value : shell_it->second) {
      append_unique(shells, ShellId{shell_value});
    }
  }
  return ok_result(shells, state_->create_diagnostic("已查询边所属壳集合"));
}

Result<std::vector<EdgeId>>
TopologyQueryService::edges_of_loop(LoopId loop_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->loops.find(loop_id.value);
  if (it == state_->loops.end()) {
    return detail::invalid_input_result<std::vector<EdgeId>>(
        *state_, diag_codes::kTopoLoopNotClosed, "环查询失败：目标环不存在",
        "环边集合查询失败");
  }
  std::vector<EdgeId> edges;
  edges.reserve(it->second.coedges.size());
  for (const auto coedge_id : it->second.coedges) {
    const auto coedge_it = state_->coedges.find(coedge_id.value);
    if (coedge_it == state_->coedges.end() ||
        !detail::has_edge(*state_, coedge_it->second.edge_id)) {
      return detail::failed_result<std::vector<EdgeId>>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoLoopNotClosed,
          "环查询失败：环包含无效定向边或边引用", "环边集合查询失败");
    }
    edges.push_back(coedge_it->second.edge_id);
  }
  return ok_result(edges, state_->create_diagnostic("已查询环边集合"));
}

Result<std::vector<VertexId>>
TopologyQueryService::vertices_of_loop(LoopId loop_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto lit = state_->loops.find(loop_id.value);
  if (lit == state_->loops.end()) {
    return detail::invalid_input_result<std::vector<VertexId>>(
        *state_, diag_codes::kTopoLoopNotClosed, "环查询失败：目标环不存在",
        "环顶点序列查询失败");
  }
  std::vector<VertexId> vertices;
  vertices.reserve(lit->second.coedges.size());
  for (const auto coedge_id : lit->second.coedges) {
    const auto ov = oriented_vertices(*state_, coedge_id);
    if (!ov.has_value()) {
      return detail::failed_result<std::vector<VertexId>>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoLoopNotClosed,
          "环查询失败：无法解析定向边的端点顺序", "环顶点序列查询失败");
    }
    vertices.push_back((*ov)[0]);
  }
  return ok_result(vertices, state_->create_diagnostic("已查询环顶点序列"));
}

Result<std::vector<LoopId>>
TopologyQueryService::loops_of_face(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->faces.find(face_id.value);
  if (it == state_->faces.end()) {
    return detail::invalid_input_result<std::vector<LoopId>>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "面查询失败：目标面不存在", "面环集合查询失败");
  }
  auto loops = it->second.inner_loops;
  loops.insert(loops.begin(), it->second.outer_loop);
  return ok_result(loops, state_->create_diagnostic("已查询面环集合"));
}

Result<SurfaceId> TopologyQueryService::surface_of_face(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->faces.find(face_id.value);
  if (it == state_->faces.end()) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kTopoCurveTopologyMismatch,
        "面曲面查询失败：目标面不存在", "面曲面映射查询失败");
  }
  return ok_result(it->second.surface_id,
                   state_->create_diagnostic("已查询面曲面映射"));
}

Result<std::vector<ShellId>>
TopologyQueryService::shells_of_face(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<std::vector<ShellId>>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "面查询失败：目标面不存在", "面所属壳集合查询失败");
  }

  std::vector<ShellId> shells;
  const auto shell_it = state_->face_to_shells.find(face_id.value);
  if (shell_it != state_->face_to_shells.end()) {
    for (const auto shell_value : shell_it->second) {
      shells.push_back(ShellId{shell_value});
    }
  }
  return ok_result(shells, state_->create_diagnostic("已查询面所属壳集合"));
}

Result<std::vector<BodyId>>
TopologyQueryService::bodies_of_face(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<std::vector<BodyId>>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "面查询失败：目标面不存在", "面所属体集合查询失败");
  }

  std::vector<BodyId> bodies;
  const auto shells_result = shells_of_face(face_id);
  if (shells_result.status != StatusCode::Ok ||
      !shells_result.value.has_value()) {
    return detail::failed_result<std::vector<BodyId>>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "面查询失败：无法稳定解析面所属壳集合", "面所属体集合查询失败");
  }
  for (const auto shell_id : *shells_result.value) {
    const auto body_it = state_->shell_to_bodies.find(shell_id.value);
    if (body_it == state_->shell_to_bodies.end()) {
      continue;
    }
    for (const auto body_value : body_it->second) {
      append_unique(bodies, BodyId{body_value});
    }
  }
  return ok_result(bodies, state_->create_diagnostic("已查询面所属体集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::source_faces_of_face(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->faces.find(face_id.value);
  if (it == state_->faces.end()) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kTopoFaceOuterLoopInvalid,
        "面来源查询失败：目标面不存在", "面来源面集合查询失败");
  }
  return ok_result(it->second.source_faces,
                   state_->create_diagnostic("已查询面来源面集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::faces_of_shell(ShellId shell_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->shells.find(shell_id.value);
  if (it == state_->shells.end()) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kTopoShellNotClosed, "壳查询失败：目标壳不存在",
        "壳面集合查询失败");
  }
  return ok_result(it->second.faces,
                   state_->create_diagnostic("已查询壳面集合"));
}

Result<std::vector<BodyId>>
TopologyQueryService::bodies_of_shell(ShellId shell_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  if (!detail::has_shell(*state_, shell_id)) {
    return detail::invalid_input_result<std::vector<BodyId>>(
        *state_, diag_codes::kTopoShellNotClosed, "壳查询失败：目标壳不存在",
        "壳所属体集合查询失败");
  }

  std::vector<BodyId> bodies;
  const auto body_it = state_->shell_to_bodies.find(shell_id.value);
  if (body_it != state_->shell_to_bodies.end()) {
    for (const auto body_value : body_it->second) {
      bodies.push_back(BodyId{body_value});
    }
  }
  return ok_result(bodies, state_->create_diagnostic("已查询壳所属体集合"));
}

Result<std::vector<ShellId>>
TopologyQueryService::source_shells_of_shell(ShellId shell_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->shells.find(shell_id.value);
  if (it == state_->shells.end()) {
    return detail::invalid_input_result<std::vector<ShellId>>(
        *state_, diag_codes::kTopoShellNotClosed,
        "壳来源查询失败：目标壳不存在", "壳来源壳集合查询失败");
  }
  return ok_result(it->second.source_shells,
                   state_->create_diagnostic("已查询壳来源壳集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::source_faces_of_shell(ShellId shell_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->shells.find(shell_id.value);
  if (it == state_->shells.end()) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kTopoShellNotClosed,
        "壳来源查询失败：目标壳不存在", "壳来源面集合查询失败");
  }
  return ok_result(it->second.source_faces,
                   state_->create_diagnostic("已查询壳来源面集合"));
}

Result<std::vector<ShellId>>
TopologyQueryService::shells_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<std::vector<ShellId>>(
        *state_, diag_codes::kCoreInvalidHandle, "体查询失败：目标实体不存在",
        "体壳集合查询失败");
  }
  return ok_result(it->second.shells,
                   state_->create_diagnostic("已查询体壳集合"));
}

Result<std::vector<BodyId>>
TopologyQueryService::source_bodies_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<std::vector<BodyId>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体来源查询失败：目标实体不存在", "体来源体集合查询失败");
  }
  return ok_result(it->second.source_bodies,
                   state_->create_diagnostic("已查询体来源体集合"));
}

Result<std::vector<ShellId>>
TopologyQueryService::source_shells_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<std::vector<ShellId>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体来源查询失败：目标实体不存在", "体来源壳集合查询失败");
  }
  return ok_result(it->second.source_shells,
                   state_->create_diagnostic("已查询体来源壳集合"));
}

Result<std::vector<FaceId>>
TopologyQueryService::source_faces_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<std::vector<FaceId>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体来源查询失败：目标实体不存在", "体来源面集合查询失败");
  }
  return ok_result(it->second.source_faces,
                   state_->create_diagnostic("已查询体来源面集合"));
}

Result<TopologySummary>
TopologyQueryService::summary_of_shell(ShellId shell_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::invalid_input_result<TopologySummary>(
        *state_, diag_codes::kTopoShellNotClosed,
        "壳拓扑摘要查询失败：目标壳不存在", "壳拓扑摘要查询失败");
  }
  TopologySummary summary{};
  summary.shell_count = 1;
  summary.face_count =
      static_cast<std::uint64_t>(shell_it->second.faces.size());
  std::unordered_set<std::uint64_t> loops;
  std::unordered_set<std::uint64_t> edges;
  std::unordered_set<std::uint64_t> vertices;
  for (const auto face_id : shell_it->second.faces) {
    const auto face_it = state_->faces.find(face_id.value);
    if (face_it == state_->faces.end()) {
      continue;
    }
    loops.insert(face_it->second.outer_loop.value);
    for (const auto loop_id : face_it->second.inner_loops)
      loops.insert(loop_id.value);
  }
  for (const auto loop_value : loops) {
    const auto loop_it = state_->loops.find(loop_value);
    if (loop_it == state_->loops.end())
      continue;
    for (const auto coedge_id : loop_it->second.coedges) {
      const auto coedge_it = state_->coedges.find(coedge_id.value);
      if (coedge_it == state_->coedges.end())
        continue;
      const auto edge_id = coedge_it->second.edge_id.value;
      edges.insert(edge_id);
      const auto edge_it = state_->edges.find(edge_id);
      if (edge_it == state_->edges.end())
        continue;
      vertices.insert(edge_it->second.v0.value);
      vertices.insert(edge_it->second.v1.value);
    }
  }
  summary.loop_count = static_cast<std::uint64_t>(loops.size());
  summary.edge_count = static_cast<std::uint64_t>(edges.size());
  summary.vertex_count = static_cast<std::uint64_t>(vertices.size());
  return ok_result(summary, state_->create_diagnostic("已查询壳拓扑摘要"));
}

Result<TopologySummary>
TopologyQueryService::summary_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::invalid_input_result<TopologySummary>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体拓扑摘要查询失败：目标体不存在", "体拓扑摘要查询失败");
  }
  TopologySummary summary{};
  summary.shell_count =
      static_cast<std::uint64_t>(body_it->second.shells.size());
  for (const auto shell_id : body_it->second.shells) {
    const auto shell_summary = summary_of_shell(shell_id);
    if (shell_summary.status != StatusCode::Ok ||
        !shell_summary.value.has_value()) {
      return error_result<TopologySummary>(shell_summary.status,
                                           shell_summary.diagnostic_id);
    }
    summary.face_count += shell_summary.value->face_count;
    summary.loop_count += shell_summary.value->loop_count;
    summary.edge_count += shell_summary.value->edge_count;
    summary.vertex_count += shell_summary.value->vertex_count;
  }
  return ok_result(summary, state_->create_diagnostic("已查询体拓扑摘要"));
}

Result<std::uint64_t>
TopologyQueryService::edge_count_of_loop(LoopId loop_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto edges = edges_of_loop(loop_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(edges.value->size()),
      state_->create_diagnostic("已查询环边数量"));
}

Result<std::uint64_t>
TopologyQueryService::loop_count_of_face(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto loops = loops_of_face(face_id);
  if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
    return error_result<std::uint64_t>(loops.status, loops.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(loops.value->size()),
      state_->create_diagnostic("已查询面环数量"));
}

Result<std::uint64_t>
TopologyQueryService::face_count_of_shell(ShellId shell_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto faces = faces_of_shell(shell_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<std::uint64_t>(faces.status, faces.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(faces.value->size()),
      state_->create_diagnostic("已查询壳面数量"));
}

Result<std::uint64_t>
TopologyQueryService::shell_count_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto shells = shells_of_body(body_id);
  if (shells.status != StatusCode::Ok || !shells.value.has_value()) {
    return error_result<std::uint64_t>(shells.status, shells.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(shells.value->size()),
      state_->create_diagnostic("已查询体壳数量"));
}

Result<std::uint64_t>
TopologyQueryService::coedge_count_of_edge(EdgeId edge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto coedges = coedges_of_edge(edge_id);
  if (coedges.status != StatusCode::Ok || !coedges.value.has_value()) {
    return error_result<std::uint64_t>(coedges.status, coedges.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(coedges.value->size()),
      state_->create_diagnostic("已查询边定向边数量"));
}

Result<std::uint64_t>
TopologyQueryService::owner_count_of_edge(EdgeId edge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto shells = shells_of_edge(edge_id);
  if (shells.status != StatusCode::Ok || !shells.value.has_value()) {
    return error_result<std::uint64_t>(shells.status, shells.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(shells.value->size()),
      state_->create_diagnostic("已查询边壳归属数量"));
}

Result<std::uint64_t>
TopologyQueryService::owner_count_of_face(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto shells = shells_of_face(face_id);
  if (shells.status != StatusCode::Ok || !shells.value.has_value()) {
    return error_result<std::uint64_t>(shells.status, shells.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(shells.value->size()),
      state_->create_diagnostic("已查询面壳归属数量"));
}

Result<std::uint64_t>
TopologyQueryService::owner_count_of_shell(ShellId shell_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto bodies = bodies_of_shell(shell_id);
  if (bodies.status != StatusCode::Ok || !bodies.value.has_value()) {
    return error_result<std::uint64_t>(bodies.status, bodies.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(bodies.value->size()),
      state_->create_diagnostic("已查询壳体归属数量"));
}

Result<bool> TopologyQueryService::has_vertex(VertexId vertex_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  return ok_result(detail::has_vertex(*state_, vertex_id),
                   state_->create_diagnostic("已查询顶点存在性"));
}

Result<bool> TopologyQueryService::has_edge(EdgeId edge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  return ok_result(detail::has_edge(*state_, edge_id),
                   state_->create_diagnostic("已查询边存在性"));
}

Result<bool> TopologyQueryService::has_loop(LoopId loop_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  return ok_result(detail::has_loop(*state_, loop_id),
                   state_->create_diagnostic("已查询环存在性"));
}

Result<bool> TopologyQueryService::has_face(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  return ok_result(detail::has_face(*state_, face_id),
                   state_->create_diagnostic("已查询面存在性"));
}

Result<bool> TopologyQueryService::has_shell(ShellId shell_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  return ok_result(detail::has_shell(*state_, shell_id),
                   state_->create_diagnostic("已查询壳存在性"));
}

Result<bool> TopologyQueryService::has_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  return ok_result(detail::has_body(*state_, body_id),
                   state_->create_diagnostic("已查询体存在性"));
}

Result<bool> TopologyQueryService::is_edge_boundary(EdgeId edge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto count = owner_count_of_edge(edge_id);
  if (count.status != StatusCode::Ok || !count.value.has_value()) {
    return error_result<bool>(count.status, count.diagnostic_id);
  }
  return ok_result(*count.value == 1,
                   state_->create_diagnostic("已查询边是否边界边"));
}

Result<bool> TopologyQueryService::is_edge_non_manifold(EdgeId edge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto count = owner_count_of_edge(edge_id);
  if (count.status != StatusCode::Ok || !count.value.has_value()) {
    return error_result<bool>(count.status, count.diagnostic_id);
  }
  return ok_result(*count.value > 2,
                   state_->create_diagnostic("已查询边是否非流形"));
}

Result<bool> TopologyQueryService::is_face_orphan(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto owners = owner_count_of_face(face_id);
  if (owners.status != StatusCode::Ok || !owners.value.has_value()) {
    return error_result<bool>(owners.status, owners.diagnostic_id);
  }
  return ok_result(*owners.value == 0,
                   state_->create_diagnostic("已查询面是否孤立"));
}

Result<bool> TopologyQueryService::is_shell_orphan(ShellId shell_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto owners = owner_count_of_shell(shell_id);
  if (owners.status != StatusCode::Ok || !owners.value.has_value()) {
    return error_result<bool>(owners.status, owners.diagnostic_id);
  }
  return ok_result(*owners.value == 0,
                   state_->create_diagnostic("已查询壳是否孤立"));
}

Result<bool> TopologyQueryService::is_body_derived(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->bodies.find(body_id.value);
  if (it == state_->bodies.end()) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体派生查询失败：目标实体不存在", "体派生查询失败");
  }
  const bool derived = !it->second.source_bodies.empty() ||
                       !it->second.source_shells.empty() ||
                       !it->second.source_faces.empty();
  return ok_result(derived, state_->create_diagnostic("已查询体派生属性"));
}

Result<BoundingBox> TopologyQueryService::bbox_of_face(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<BoundingBox>(
        *state_, diag_codes::kCoreInvalidHandle,
        "面包围盒查询失败：目标面不存在", "面包围盒查询失败");
  }
  BoundingBox bbox{};
  if (!topo_internal::append_face_bbox(state_, face_id, bbox) || !bbox.is_valid) {
    return detail::failed_result<BoundingBox>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoFaceOuterLoopInvalid, "面包围盒查询失败：面拓扑不完整",
        "面包围盒查询失败");
  }
  return ok_result(bbox, state_->create_diagnostic("已查询面包围盒"));
}

Result<std::optional<EdgeCurveInterval>>
TopologyQueryService::edge_curve_interval(EdgeId edge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto edge = state_->edges.find(edge_id.value);
  if (edge == state_->edges.end()) {
    return detail::invalid_input_result<std::optional<EdgeCurveInterval>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "边裁剪区间查询失败：目标边不存在", "边裁剪区间查询失败");
  }
  if (!edge->second.has_parameter_interval) {
    return ok_result(std::optional<EdgeCurveInterval>{},
                     state_->create_diagnostic("目标边未携带显式曲线裁剪区间"));
  }
  return ok_result(
      std::optional<EdgeCurveInterval>{EdgeCurveInterval{
          edge->second.start_parameter, edge->second.end_parameter}},
      state_->create_diagnostic("已查询边曲线裁剪区间"));
}

Result<Scalar> TopologyQueryService::edge_length(EdgeId edge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto edge = state_->edges.find(edge_id.value);
  if (edge == state_->edges.end()) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreInvalidHandle, "边长度查询失败：目标边不存在", "边长度查询失败");
  }
  const auto curve = state_->curves.find(edge->second.curve_id.value);
  const auto v0 = state_->vertices.find(edge->second.v0.value);
  const auto v1 = state_->vertices.find(edge->second.v1.value);
  if (curve == state_->curves.end() || v0 == state_->vertices.end() || v1 == state_->vertices.end()) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoDanglingEdge,
        "边长度查询失败：曲线或顶点引用无效", "边长度查询失败");
  }
  const auto &c = curve->second;
  if (edge->second.has_parameter_interval) {
    Point3 expected_start{};
    Point3 expected_end{};
    const auto invalid_trim = [&]() {
      return detail::failed_result<Scalar>(
          *state_, StatusCode::InvalidTopology,
          diag_codes::kTopoCurveTopologyMismatch,
          "边长度查询失败：显式裁剪区间、支撑曲线或拓扑端点不一致",
          "边长度查询失败", {edge_id.value, edge->second.curve_id.value});
    };
    const CurveService curve_service(state_);
    const auto domain = curve_service.domain(edge->second.curve_id);
    if (!domain.value || edge->second.start_parameter < domain.value->min ||
        edge->second.start_parameter > domain.value->max ||
        edge->second.end_parameter < domain.value->min ||
        edge->second.end_parameter > domain.value->max ||
        edge->second.start_parameter == edge->second.end_parameter) {
      return invalid_trim();
    }
    const auto start_point = curve_service.point_at_parameter(
        edge->second.curve_id, edge->second.start_parameter);
    const auto end_point = curve_service.point_at_parameter(
        edge->second.curve_id, edge->second.end_parameter);
    if (!start_point.value || !end_point.value) return invalid_trim();
    expected_start = *start_point.value;
    expected_end = *end_point.value;
    const auto point_distance = [](const Point3 &a, const Point3 &b) {
      return std::hypot(static_cast<long double>(a.x) - b.x,
                        static_cast<long double>(a.y) - b.y,
                        static_cast<long double>(a.z) - b.z);
    };
    const auto tolerance = detail::resolve_linear_tolerance(
        0.0, state_->config.tolerance);
    if (point_distance(expected_start, v0->second.point) > tolerance ||
        point_distance(expected_end, v1->second.point) > tolerance) {
      return invalid_trim();
    }
    const auto measured = curve_service.length(
        edge->second.curve_id, edge->second.start_parameter,
        edge->second.end_parameter);
    if (!measured.value) {
      return error_result<Scalar>(measured.status, measured.diagnostic_id);
    }
    if (!std::isfinite(*measured.value) || *measured.value <= 0.0) {
      return invalid_trim();
    }
    return ok_result(*measured.value,
                     state_->create_diagnostic("已查询显式裁剪边弧长"));
  }
  if (c.kind != detail::CurveKind::Line && c.kind != detail::CurveKind::LineSegment) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::NotImplemented, diag_codes::kCoreOperationUnsupported,
        "边长度查询失败：曲边尚无明确裁剪区间", "边长度查询失败");
  }
  const auto invalid_geometry = [&]() {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoCurveTopologyMismatch,
        "边长度查询失败：端点退化或与支撑曲线不一致", "边长度查询失败");
  };
  Point3 origin = c.origin;
  long double dx = c.direction.x, dy = c.direction.y, dz = c.direction.z;
  if (c.kind == detail::CurveKind::LineSegment) {
    if (c.poles.size() != 2) return invalid_geometry();
    origin = c.poles.front();
    dx = static_cast<long double>(c.poles[1].x) - origin.x;
    dy = static_cast<long double>(c.poles[1].y) - origin.y;
    dz = static_cast<long double>(c.poles[1].z) - origin.z;
  }
  const auto norm = std::hypot(dx, dy, dz);
  if (!(norm > 0.0L) || !std::isfinite(norm)) return invalid_geometry();
  dx /= norm;
  dy /= norm;
  dz /= norm;
  const long double tolerance = state_->config.tolerance.linear;
  for (const auto &point : {v0->second.point, v1->second.point}) {
    const long double x = static_cast<long double>(point.x) - origin.x;
    const long double y = static_cast<long double>(point.y) - origin.y;
    const long double z = static_cast<long double>(point.z) - origin.z;
    const auto distance = std::hypot(y * dz - z * dy, z * dx - x * dz, x * dy - y * dx);
    const auto along = x * dx + y * dy + z * dz;
    if (!std::isfinite(distance) || !std::isfinite(along) || distance > tolerance ||
        (c.kind == detail::CurveKind::LineSegment &&
         (along < -tolerance || along > norm + tolerance))) return invalid_geometry();
  }
  const auto &a = v0->second.point;
  const auto &b = v1->second.point;
  const auto length = std::hypot(static_cast<long double>(b.x) - a.x,
                               static_cast<long double>(b.y) - a.y,
                               static_cast<long double>(b.z) - a.z);
  if (!(length > 0.0L)) return invalid_geometry();
  if (!std::isfinite(length) || length > std::numeric_limits<Scalar>::max()) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "边长度查询失败：结果超出数值范围", "边长度查询失败");
  }
  return ok_result(static_cast<Scalar>(length), state_->create_diagnostic("已查询直线边长度"));
}

Result<Scalar> TopologyQueryService::loop_length(LoopId loop_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto loop = state_->loops.find(loop_id.value);
  if (loop == state_->loops.end()) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreInvalidHandle, "环长度查询失败：目标环不存在", "环长度查询失败");
  }
  std::string reason;
  if (!validate_loop_record(*state_, loop->second, reason)) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoLoopNotClosed,
        "环长度查询失败：" + reason, "环长度查询失败");
  }
  long double total = 0.0L;
  for (const auto coedge_id : loop->second.coedges) {
    const auto measured = edge_length(state_->coedges.at(coedge_id.value).edge_id);
    if (!measured.value) return error_result<Scalar>(measured.status, measured.diagnostic_id);
    total += *measured.value;
  }
  if (!std::isfinite(total) || total > std::numeric_limits<Scalar>::max()) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "环长度查询失败：结果超出数值范围", "环长度查询失败");
  }
  return ok_result(static_cast<Scalar>(total), state_->create_diagnostic("已查询环长度"));
}

Result<Scalar> TopologyQueryService::face_boundary_length(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto face = state_->faces.find(face_id.value);
  if (face == state_->faces.end()) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreInvalidHandle, "面边界长度查询失败：目标面不存在", "面边界长度查询失败");
  }
  std::vector<LoopId> loops{face->second.outer_loop};
  loops.insert(loops.end(), face->second.inner_loops.begin(), face->second.inner_loops.end());
  std::unordered_set<std::uint64_t> seen;
  long double total = 0.0L;
  for (std::size_t i = 0; i < loops.size(); ++i) {
    if (!state_->loops.contains(loops[i].value) || !seen.insert(loops[i].value).second) {
      return detail::failed_result<Scalar>(
          *state_, StatusCode::InvalidTopology,
          i == 0 ? diag_codes::kTopoFaceOuterLoopInvalid : diag_codes::kTopoFaceInnerLoopInvalid,
          "面边界长度查询失败：边界环缺失或重复", "面边界长度查询失败");
    }
    const auto measured = loop_length(loops[i]);
    if (!measured.value) return error_result<Scalar>(measured.status, measured.diagnostic_id);
    total += *measured.value;
  }
  if (!std::isfinite(total) || total > std::numeric_limits<Scalar>::max()) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "面边界长度查询失败：结果超出数值范围", "面边界长度查询失败");
  }
  return ok_result(static_cast<Scalar>(total), state_->create_diagnostic("已查询面边界长度"));
}

Result<Scalar> TopologyQueryService::planar_face_area(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto face = state_->faces.find(face_id.value);
  if (face == state_->faces.end()) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreInvalidHandle, "面面积查询失败：目标面不存在",
        "面面积查询失败");
  }
  const auto surface = state_->surfaces.find(face->second.surface_id.value);
  if (surface == state_->surfaces.end()) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoFaceOuterLoopInvalid,
        "面面积查询失败：曲面引用无效", "面面积查询失败");
  }
  if (surface->second.kind != detail::SurfaceKind::Plane) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::NotImplemented, diag_codes::kCoreOperationUnsupported,
        "面面积查询仅支持平面直线边面片", "面面积查询失败");
  }

  const auto &origin = surface->second.origin;
  const auto &normal = surface->second.normal;
  const long double nx = normal.x, ny = normal.y, nz = normal.z;
  const long double norm = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (!(norm > 0.0L) || !std::isfinite(norm)) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoFaceOuterLoopInvalid,
        "面面积查询失败：平面法向无效", "面面积查询失败");
  }

  long double area = 0.0L;
  bool has_curved_edge = false;
  const auto accumulate_loop = [&](LoopId loop_id, bool inner) -> bool {
    const auto loop = state_->loops.find(loop_id.value);
    if (loop == state_->loops.end() || loop->second.coedges.size() < 3) return false;
    std::vector<Point3> points;
    points.reserve(loop->second.coedges.size());
    for (const auto coedge_id : loop->second.coedges) {
      const auto coedge = state_->coedges.find(coedge_id.value);
      if (coedge == state_->coedges.end()) return false;
      const auto edge = state_->edges.find(coedge->second.edge_id.value);
      if (edge == state_->edges.end()) return false;
      const auto curve = state_->curves.find(edge->second.curve_id.value);
      if (curve == state_->curves.end()) return false;
      if (curve->second.kind != detail::CurveKind::Line &&
          curve->second.kind != detail::CurveKind::LineSegment) {
        has_curved_edge = true;
        return false;
      }
      const auto vertex_id = coedge->second.reversed ? edge->second.v1 : edge->second.v0;
      const auto vertex = state_->vertices.find(vertex_id.value);
      if (vertex == state_->vertices.end()) return false;
      const auto &p = vertex->second.point;
      const long double dx = static_cast<long double>(p.x) - origin.x;
      const long double dy = static_cast<long double>(p.y) - origin.y;
      const long double dz = static_cast<long double>(p.z) - origin.z;
      if (std::abs(dx * nx + dy * ny + dz * nz) / norm > state_->config.tolerance.linear)
        return false;
      points.push_back(p);
    }
    long double projected_twice_area = 0.0L;
    const auto &base = points.front();
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
      const long double ax = static_cast<long double>(points[i].x) - base.x;
      const long double ay = static_cast<long double>(points[i].y) - base.y;
      const long double az = static_cast<long double>(points[i].z) - base.z;
      const long double bx = static_cast<long double>(points[i + 1].x) - base.x;
      const long double by = static_cast<long double>(points[i + 1].y) - base.y;
      const long double bz = static_cast<long double>(points[i + 1].z) - base.z;
      projected_twice_area += ((ay * bz - az * by) * nx +
                               (az * bx - ax * bz) * ny +
                               (ax * by - ay * bx) * nz) / norm;
    }
    const long double loop_area = std::abs(projected_twice_area) * 0.5L;
    area += inner ? -loop_area : loop_area;
    return true;
  };
  if (!accumulate_loop(face->second.outer_loop, false)) {
    return detail::failed_result<Scalar>(
        *state_, has_curved_edge ? StatusCode::NotImplemented : StatusCode::InvalidTopology,
        has_curved_edge ? diag_codes::kCoreOperationUnsupported : diag_codes::kTopoFaceOuterLoopInvalid,
        "面面积查询失败：外环不完整、非平面或包含曲边", "面面积查询失败");
  }
  for (const auto loop_id : face->second.inner_loops) {
    if (!accumulate_loop(loop_id, true)) {
      return detail::failed_result<Scalar>(
          *state_, has_curved_edge ? StatusCode::NotImplemented : StatusCode::InvalidTopology,
          has_curved_edge ? diag_codes::kCoreOperationUnsupported : diag_codes::kTopoFaceInnerLoopInvalid,
          "面面积查询失败：内环不完整、非平面或包含曲边", "面面积查询失败");
    }
  }
  if (area < 0.0L || !std::isfinite(area) ||
      area > std::numeric_limits<Scalar>::max()) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoFaceOuterLoopInvalid,
        "面面积查询失败：边界面积无效或超出数值范围", "面面积查询失败");
  }
  return ok_result(static_cast<Scalar>(area), state_->create_diagnostic("已查询平面面片面积"));
}

Result<Scalar> TopologyQueryService::face_area(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto face_it = state_->faces.find(face_id.value);
  if (face_it == state_->faces.end()) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreInvalidHandle,
        "通用面积查询失败：目标面不存在", "通用面积查询失败");
  }
  const auto surface_it = state_->surfaces.find(face_it->second.surface_id.value);
  if (surface_it == state_->surfaces.end()) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoFaceOuterLoopInvalid,
        "通用面积查询失败：面引用的曲面不存在", "通用面积查询失败",
        {face_id.value, face_it->second.surface_id.value});
  }

  std::vector<LoopId> loop_ids{face_it->second.outer_loop};
  loop_ids.insert(loop_ids.end(), face_it->second.inner_loops.begin(),
                  face_it->second.inner_loops.end());
  std::size_t with_pcurve = 0;
  std::size_t without_pcurve = 0;
  for (const auto loop_id : loop_ids) {
    const auto loop_it = state_->loops.find(loop_id.value);
    if (loop_it == state_->loops.end() || loop_it->second.coedges.empty()) {
      return detail::failed_result<Scalar>(
          *state_, StatusCode::InvalidTopology,
          loop_id == face_it->second.outer_loop ? diag_codes::kTopoFaceOuterLoopInvalid
                                                 : diag_codes::kTopoFaceInnerLoopInvalid,
          "通用面积查询失败：边界环不存在或为空", "通用面积查询失败",
          {face_id.value, loop_id.value});
    }
    for (const auto coedge_id : loop_it->second.coedges) {
      const auto coedge_it = state_->coedges.find(coedge_id.value);
      if (coedge_it == state_->coedges.end()) {
        return detail::failed_result<Scalar>(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
            "通用面积查询失败：边界环引用的定向边不存在", "通用面积查询失败",
            {face_id.value, loop_id.value, coedge_id.value});
      }
      if (coedge_it->second.pcurve_id.value == 0) ++without_pcurve;
      else ++with_pcurve;
    }
  }

  if (with_pcurve == 0 && surface_it->second.kind == detail::SurfaceKind::Plane) {
    return planar_face_area(face_id);
  }
  if (with_pcurve == 0 || without_pcurve != 0) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoCurveTopologyMismatch,
        with_pcurve == 0
            ? "通用面积查询失败：非平面面片未提供 PCurve 修剪环"
            : "通用面积查询失败：PCurve 修剪环绑定不完整",
        "通用面积查询失败", {face_id.value});
  }

  AnalyticAreaSurface area_surface;
  if (!resolve_analytic_area_surface(*state_, face_it->second.surface_id,
                                     area_surface) || !area_surface.base) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoCurveTopologyMismatch,
        "通用面积查询失败：曲面包装链、参数域或偏置无效", "通用面积查询失败",
        {face_id.value, face_it->second.surface_id.value});
  }
  if (area_surface.kind != detail::SurfaceKind::Plane &&
      area_surface.kind != detail::SurfaceKind::Cylinder &&
      area_surface.kind != detail::SurfaceKind::Cone &&
      area_surface.kind != detail::SurfaceKind::Sphere &&
      area_surface.kind != detail::SurfaceKind::Torus) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::NotImplemented, diag_codes::kCoreOperationUnsupported,
        "通用面积查询暂仅支持解析平面/柱面/锥面/球面/环面及其修剪或偏置包装",
        "通用面积查询失败", {face_id.value, face_it->second.surface_id.value});
  }

  const long double uv_tol = std::max<long double>(
      1e-12L, std::abs(static_cast<long double>(state_->config.tolerance.linear)));
  const auto in_domain = [&](const Point2 &p) {
    const auto in_one = [uv_tol](Scalar value, const Range1D &range) {
      return std::isfinite(value) &&
             (!std::isfinite(range.min) || value >= range.min - uv_tol) &&
             (!std::isfinite(range.max) || value <= range.max + uv_tol);
    };
    return in_one(p.x, area_surface.domain.u) && in_one(p.y, area_surface.domain.v);
  };
  const auto build_ring = [&](LoopId loop_id, std::vector<Point2> &ring,
                              std::string &reason) -> bool {
    const auto &loop = state_->loops.at(loop_id.value);
    ring.clear();
    for (const auto coedge_id : loop.coedges) {
      const auto &coedge = state_->coedges.at(coedge_id.value);
      const auto pc_it = state_->pcurves.find(coedge.pcurve_id.value);
      if (pc_it == state_->pcurves.end() ||
          pc_it->second.kind != detail::PCurveKind::Polyline ||
          pc_it->second.poles.size() < 2) {
        reason = "PCurve 不存在、类型不支持或已退化";
        return false;
      }
      const auto &poles = pc_it->second.poles;
      const auto oriented = [&](std::size_t i) -> const Point2 & {
        return coedge.reversed ? poles[poles.size() - 1 - i] : poles[i];
      };
      const auto topology_vertices = oriented_vertices(*state_, coedge_id);
      if (!topology_vertices) {
        reason = "定向边的拓扑端点无效";
        return false;
      }
      const auto vertex0 = state_->vertices.find((*topology_vertices)[0].value);
      const auto vertex1 = state_->vertices.find((*topology_vertices)[1].value);
      const auto expected0 = analytic_area_surface_point(area_surface, oriented(0));
      const auto expected1 =
          analytic_area_surface_point(area_surface, oriented(poles.size() - 1));
      const auto endpoint_distance = [](const Point3 &a, const Point3 &b) {
        return std::hypot(std::hypot(static_cast<long double>(a.x) - b.x,
                                    static_cast<long double>(a.y) - b.y),
                          static_cast<long double>(a.z) - b.z);
      };
      const long double xyz_tol = std::max<long double>(
          1e-9L, std::abs(static_cast<long double>(state_->config.tolerance.linear)));
      if (vertex0 == state_->vertices.end() || vertex1 == state_->vertices.end() ||
          endpoint_distance(vertex0->second.point, expected0) > xyz_tol ||
          endpoint_distance(vertex1->second.point, expected1) > xyz_tol) {
        reason = "PCurve 端点在支撑曲面上与拓扑顶点不一致";
        return false;
      }
      if (!ring.empty()) {
        const auto &start = oriented(0);
        if (std::hypot(static_cast<long double>(ring.back().x) - start.x,
                       static_cast<long double>(ring.back().y) - start.y) > uv_tol) {
          reason = "PCurve 片段不连续";
          return false;
        }
      }
      for (std::size_t i = ring.empty() ? 0 : 1; i < poles.size(); ++i) {
        const auto &p = oriented(i);
        if (!in_domain(p)) {
          reason = "PCurve 顶点非有限或超出当前曲面参数域";
          return false;
        }
        if (ring.empty() ||
            std::hypot(static_cast<long double>(ring.back().x) - p.x,
                       static_cast<long double>(ring.back().y) - p.y) > uv_tol)
          ring.push_back(p);
      }
    }
    if (ring.size() < 4 ||
        std::hypot(static_cast<long double>(ring.front().x) - ring.back().x,
                   static_cast<long double>(ring.front().y) - ring.back().y) > uv_tol) {
      reason = "PCurve 环未闭合或有效顶点不足";
      return false;
    }
    ring.pop_back();
    if (!simple_uv_ring(ring, uv_tol)) {
      reason = "PCurve 环退化或自交";
      return false;
    }
    long double uv_area2 = 0.0L;
    for (std::size_t i = 0; i < ring.size(); ++i) {
      const auto &a = ring[i];
      const auto &b = ring[(i + 1) % ring.size()];
      uv_area2 += static_cast<long double>(a.x) * b.y -
                  static_cast<long double>(b.x) * a.y;
    }
    if (std::abs(uv_area2) <= uv_tol * uv_tol * 2.0L) {
      reason = "PCurve 环参数面积退化";
      return false;
    }
    return true;
  };

  std::vector<std::vector<Point2>> rings(loop_ids.size());
  for (std::size_t i = 0; i < loop_ids.size(); ++i) {
    std::string reason;
    if (!build_ring(loop_ids[i], rings[i], reason)) {
      return detail::failed_result<Scalar>(
          *state_, StatusCode::InvalidTopology,
          i == 0 ? diag_codes::kTopoFaceOuterLoopInvalid : diag_codes::kTopoFaceInnerLoopInvalid,
          "通用面积查询失败：" + reason, "通用面积查询失败",
          {face_id.value, loop_ids[i].value});
    }
  }
  const auto strictly_inside = [&](const Point2 &p, const std::vector<Point2> &ring) {
    for (std::size_t i = 0; i < ring.size(); ++i)
      if (uv_point_on_segment(p, ring[i], ring[(i + 1) % ring.size()], uv_tol))
        return false;
    return detail::point_in_polygon_2d_raycast(
        p, std::span<const Point2>(ring.data(), ring.size()));
  };
  for (std::size_t i = 1; i < rings.size(); ++i) {
    if (rings_touch_or_cross(rings[0], rings[i], uv_tol) ||
        !std::all_of(rings[i].begin(), rings[i].end(),
                     [&](const Point2 &p) { return strictly_inside(p, rings[0]); })) {
      return detail::failed_result<Scalar>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoFaceInnerLoopInvalid,
          "通用面积查询失败：内环不严格位于外环内或与外环相交", "通用面积查询失败",
          {face_id.value, loop_ids[i].value});
    }
    for (std::size_t j = 1; j < i; ++j) {
      if (rings_touch_or_cross(rings[j], rings[i], uv_tol) ||
          strictly_inside(rings[i].front(), rings[j]) ||
          strictly_inside(rings[j].front(), rings[i])) {
        return detail::failed_result<Scalar>(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoFaceInnerLoopInvalid,
            "通用面积查询失败：内环之间相交、重叠或嵌套", "通用面积查询失败",
            {face_id.value, loop_ids[j].value, loop_ids[i].value});
      }
    }
  }

  long double area = std::abs(weighted_uv_ring_area(rings.front(), area_surface));
  if (!std::isfinite(area)) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::NumericalInstability, diag_codes::kQueryMassPropertiesFailure,
        "通用面积查询失败：解析面积密度产生非有限结果", "通用面积查询失败",
        {face_id.value});
  }
  for (std::size_t i = 1; i < rings.size(); ++i)
    area -= std::abs(weighted_uv_ring_area(rings[i], area_surface));
  if (!(area > uv_tol * uv_tol) || !std::isfinite(area)) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::DegenerateGeometry, diag_codes::kGeoDegenerateGeometry,
        "通用面积查询失败：扣除内环后面积非正或数值退化", "通用面积查询失败",
        {face_id.value});
  }
  if (area > std::numeric_limits<Scalar>::max()) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "通用面积查询失败：结果超出数值范围", "通用面积查询失败");
  }
  return ok_result(static_cast<Scalar>(area),
                   state_->create_diagnostic("已查询解析曲面修剪面积"));
}

Result<MassProperties>
TopologyQueryService::shell_mass_properties(ShellId shell_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto failure = [&](StatusCode status, std::string_view code, std::string message,
                            std::vector<std::uint64_t> entities = {}) {
    if (entities.empty()) entities.push_back(shell_id.value);
    const auto stage = status == StatusCode::NotImplemented ? "query.mass_properties.support_gate" :
        status == StatusCode::NumericalInstability || status == StatusCode::DegenerateGeometry
            ? "query.mass_properties.numeric" : "query.mass_properties.preflight";
    return spatial_failure<MassProperties>(*state_, status, code, std::move(message), stage, std::move(entities));
  };
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return failure(StatusCode::InvalidInput, diag_codes::kCoreInvalidHandle,
        "壳质量属性查询失败：目标壳不存在");
  }
  if (shell_it->second.faces.empty() ||
      !detail::validate_closed_shell_edge_usage_for_materialization(*state_, shell_id)) {
    return failure(StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "壳质量属性查询失败：壳为空、未闭合或存在非流形边",
        {shell_id.value});
  }

  std::vector<Point3> positions;
  std::vector<std::array<int, 3>> triangles;
  std::optional<Point3> reference;
  const Scalar linear_tol = std::max<Scalar>(
      1e-12, std::abs(detail::resolve_linear_tolerance(
                 0.0, state_->config.tolerance)));
  std::unordered_set<std::uint64_t> unique_faces;
  std::unordered_set<std::uint64_t> measured_edges;
  std::unordered_map<std::uint64_t, std::array<std::uint64_t, 2>> first_edge_use;

  for (const auto face_id : shell_it->second.faces) {
    if (!unique_faces.insert(face_id.value).second) {
      return failure(StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "壳质量属性查询失败：壳包含重复面引用",
          {shell_id.value, face_id.value});
    }
    const auto face_it = state_->faces.find(face_id.value);
    if (face_it == state_->faces.end()) {
      return failure(StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "壳质量属性查询失败：壳引用的面不存在",
          {shell_id.value, face_id.value});
    }
    if (face_it->second.mass_boundary_proxy) {
      return failure(StatusCode::NotImplemented, diag_codes::kCoreOperationUnsupported,
          "壳质量属性查询失败：兼容占位面不是真实材料边界", {shell_id.value, face_id.value});
    }
    const auto surface_it = state_->surfaces.find(face_it->second.surface_id.value);
    if (surface_it == state_->surfaces.end()) {
      return failure(StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "壳质量属性查询失败：面引用的曲面不存在",
          {shell_id.value, face_id.value, face_it->second.surface_id.value});
    }
    if (surface_it->second.kind != detail::SurfaceKind::Plane) {
      return failure(StatusCode::NotImplemented, diag_codes::kCoreOperationUnsupported,
          "壳质量属性查询暂仅支持平面、直线边闭壳",
          {shell_id.value, face_id.value});
    }
    const auto normal_length = detail::norm(surface_it->second.normal);
    if (!(normal_length > 0.0) || !std::isfinite(normal_length)) {
      return failure(StatusCode::DegenerateGeometry, diag_codes::kGeoDegenerateGeometry,
          "壳质量属性查询失败：平面法向退化",
          {shell_id.value, face_id.value});
    }
    const auto normal = detail::scale(surface_it->second.normal, 1.0 / normal_length);

    std::vector<LoopId> loop_ids{face_it->second.outer_loop};
    loop_ids.insert(loop_ids.end(), face_it->second.inner_loops.begin(),
                    face_it->second.inner_loops.end());
    std::vector<std::vector<Point3>> rings;
    rings.reserve(loop_ids.size());
    for (std::size_t loop_index = 0; loop_index < loop_ids.size(); ++loop_index) {
      const auto loop_id = loop_ids[loop_index];
      const auto loop_it = state_->loops.find(loop_id.value);
      std::string reason;
      if (loop_it == state_->loops.end() ||
          !validate_loop_record(*state_, loop_it->second, reason) ||
          loop_it->second.coedges.size() < 3) {
        return failure(StatusCode::InvalidTopology,
            loop_index == 0 ? diag_codes::kTopoFaceOuterLoopInvalid
                            : diag_codes::kTopoFaceInnerLoopInvalid,
            "壳质量属性查询失败：面边界环无效" +
                (reason.empty() ? std::string{} : "（" + reason + "）"), {shell_id.value, face_id.value, loop_id.value});
      }
      for (const auto coedge_id : loop_it->second.coedges) {
        const auto coedge_it = state_->coedges.find(coedge_id.value);
        if (coedge_it == state_->coedges.end()) {
          return failure(StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
              "壳质量属性查询失败：边界环引用的定向边不存在",
              {shell_id.value, face_id.value, loop_id.value, coedge_id.value});
        }
        const auto edge_id = coedge_it->second.edge_id;
        const auto oriented = oriented_vertices(*state_, coedge_id);
        if (!oriented) {
          return failure(StatusCode::InvalidTopology, diag_codes::kTopoDanglingEdge,
              "壳质量属性查询失败：边界边端点无效",
              {shell_id.value, face_id.value, loop_id.value, edge_id.value});
        }
        const std::array<std::uint64_t, 2> direction{
            (*oriented)[0].value, (*oriented)[1].value};
        const auto [use_it, inserted] = first_edge_use.emplace(edge_id.value, direction);
        if (!inserted &&
            (use_it->second[0] != direction[1] || use_it->second[1] != direction[0])) {
          return failure(StatusCode::InvalidTopology,
              diag_codes::kTopoLoopOrientationMismatch,
              "壳质量属性查询失败：共享边在相邻面中的方向未相反", {shell_id.value, face_id.value, edge_id.value});
        }
        if (measured_edges.insert(edge_id.value).second) {
          const auto &edge = state_->edges.at(edge_id.value);
          const auto curve = state_->curves.find(edge.curve_id.value);
          if (curve != state_->curves.end() &&
              curve->second.kind != detail::CurveKind::Line &&
              curve->second.kind != detail::CurveKind::LineSegment) {
            return failure(StatusCode::NotImplemented, diag_codes::kCoreOperationUnsupported,
                "壳质量属性查询失败：曲边不能用端点弦替代为多面体边界", {shell_id.value, face_id.value, edge_id.value});
          }
          const auto length = edge_length(edge_id);
          if (!length.value) {
            return spatial_preflight_failure<MassProperties>(*state_, length, "query.mass_properties");
          }
        }
      }
      std::vector<Point3> ring;
      if (!loop_vertex_chain_3d(*state_, loop_id, ring, reason) || ring.size() < 3) {
        return failure(StatusCode::InvalidTopology,
            loop_index == 0 ? diag_codes::kTopoFaceOuterLoopInvalid
                            : diag_codes::kTopoFaceInnerLoopInvalid,
            "壳质量属性查询失败：无法提取完整边界顶点",
            {shell_id.value, face_id.value, loop_id.value});
      }
      for (const auto &point : ring) {
        const long double plane_distance = std::abs(
            (static_cast<long double>(point.x) - surface_it->second.origin.x) * normal.x +
            (static_cast<long double>(point.y) - surface_it->second.origin.y) * normal.y +
            (static_cast<long double>(point.z) - surface_it->second.origin.z) * normal.z);
        if (!std::isfinite(plane_distance) || plane_distance > linear_tol) {
          return failure(StatusCode::InvalidTopology, diag_codes::kTopoCurveTopologyMismatch,
              "壳质量属性查询失败：面边界不在支撑平面上",
              {shell_id.value, face_id.value, loop_id.value});
        }
      }
      const auto raw_normal = newell_normal_unnormalized(ring);
      const auto ring_normal_length = detail::norm(raw_normal);
      if (!(ring_normal_length > linear_tol * linear_tol) ||
          !std::isfinite(ring_normal_length)) {
        return failure(StatusCode::DegenerateGeometry, diag_codes::kGeoDegenerateGeometry,
            "壳质量属性查询失败：面边界环面积退化",
            {shell_id.value, face_id.value, loop_id.value});
      }
      const Scalar alignment = detail::dot(raw_normal, normal);
      if ((loop_index == 0 && !(alignment > 0.0)) ||
          (loop_index != 0 && !(alignment < 0.0))) {
        return failure(StatusCode::InvalidTopology, diag_codes::kTopoLoopOrientationMismatch,
            "壳质量属性查询失败：外环/内环绕向与平面法向不一致",
            {shell_id.value, face_id.value, loop_id.value});
      }
      rings.push_back(std::move(ring));
    }

    std::vector<Point3> face_points;
    std::vector<std::pair<int, int>> boundary;
    std::vector<std::array<int, 3>> face_triangles;
    std::vector<std::vector<Point3>> holes;
    holes.assign(rings.begin() + 1, rings.end());
    if (!detail::triangulate_extrude_region(
            rings.front(), holes, normal, linear_tol, face_points, boundary,
            face_triangles, true) || face_triangles.empty()) {
      return failure(StatusCode::InvalidTopology, diag_codes::kTopoFaceOuterLoopInvalid,
          "壳质量属性查询失败：面区域自交、孔无效、非共面或无法三角化", {shell_id.value, face_id.value});
    }
    if (!reference.has_value()) reference = face_points.front();
    if (positions.size() + face_points.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      return failure(StatusCode::InvalidInput, diag_codes::kCoreParameterOutOfRange,
          "壳质量属性查询失败：拓扑规模超出可处理范围");
    }
    const int base = static_cast<int>(positions.size());
    for (const auto &point : face_points) {
      const auto local = detail::subtract(point, *reference);
      if (!std::isfinite(local.x) || !std::isfinite(local.y) ||
          !std::isfinite(local.z)) {
        return failure(StatusCode::InvalidInput, diag_codes::kCoreParameterOutOfRange,
            "壳质量属性查询失败：顶点坐标非有限或超出数值范围");
      }
      positions.push_back(Point3{local.x, local.y, local.z});
    }
    for (const auto &triangle : face_triangles) {
      triangles.push_back(
          {triangle[0] + base, triangle[1] + base, triangle[2] + base});
    }
  }

  long double signed_volume6 = 0.0L;
  for (const auto &triangle : triangles) {
    const auto &a = positions[static_cast<std::size_t>(triangle[0])];
    const auto &b = positions[static_cast<std::size_t>(triangle[1])];
    const auto &c = positions[static_cast<std::size_t>(triangle[2])];
    signed_volume6 += static_cast<long double>(a.x) *
                          (static_cast<long double>(b.y) * c.z -
                           static_cast<long double>(b.z) * c.y) +
                      static_cast<long double>(a.y) *
                          (static_cast<long double>(b.z) * c.x -
                           static_cast<long double>(b.x) * c.z) +
                      static_cast<long double>(a.z) *
                          (static_cast<long double>(b.x) * c.y -
                           static_cast<long double>(b.y) * c.x);
  }
  const long double volume_tol = std::max<long double>(
      1e-24L, static_cast<long double>(linear_tol) * linear_tol * linear_tol);
  if (!std::isfinite(signed_volume6) ||
      std::abs(signed_volume6) <= 6.0L * volume_tol) {
    return failure(StatusCode::DegenerateGeometry, diag_codes::kGeoDegenerateGeometry,
        "壳质量属性查询失败：闭壳有向体积退化",
        {shell_id.value});
  }
  if (signed_volume6 < 0.0L) {
    for (auto &triangle : triangles) std::swap(triangle[1], triangle[2]);
  }

  MassProperties properties{};
  detail::polyhedral_mass_properties_from_triangles(
      positions, triangles, properties.volume, properties.centroid,
      properties.inertia, properties.area);
  properties.centroid.x += reference->x;
  properties.centroid.y += reference->y;
  properties.centroid.z += reference->z;
  const auto finite_properties = [&]() {
    if (!(properties.volume > 0.0) || !(properties.area > 0.0) ||
        !(properties.inertia[0] > 0.0) || !(properties.inertia[4] > 0.0) ||
        !(properties.inertia[8] > 0.0) ||
        !std::isfinite(properties.volume) || !std::isfinite(properties.area) ||
        !std::isfinite(properties.centroid.x) ||
        !std::isfinite(properties.centroid.y) ||
        !std::isfinite(properties.centroid.z)) return false;
    return std::all_of(properties.inertia.begin(), properties.inertia.end(),
                       [](Scalar value) { return std::isfinite(value); });
  };
  if (!finite_properties()) {
    return failure(StatusCode::NumericalInstability,
        diag_codes::kQueryMassPropertiesFailure,
        "壳质量属性查询失败：积分结果非有限或体积/惯性对角项非正",
        {shell_id.value});
  }
  return ok_result(properties,
                   state_->create_diagnostic("已从闭合多面体壳查询质量属性"));
}

Result<std::vector<BodyShellRegion>>
TopologyQueryService::body_shell_regions(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto analysis = analyze_body_shells(
      *state_, body_id,
      [this](ShellId shell_id) { return shell_mass_properties(shell_id); });
  if (!analysis.value)
    return error_result<std::vector<BodyShellRegion>>(analysis.status,
                                                      analysis.diagnostic_id);
  return ok_result(analysis.value->regions, analysis.diagnostic_id);
}

Result<MassProperties>
TopologyQueryService::body_mass_properties(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto analysis = analyze_body_shells(
      *state_, body_id,
      [this](ShellId shell_id) { return shell_mass_properties(shell_id); });
  if (!analysis.value)
    return spatial_preflight_failure<MassProperties>(*state_, analysis, "query.mass_properties");
  if (analysis.value->regions.empty()) {
    return spatial_failure<MassProperties>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "体拓扑质量属性查询失败：实体不拥有可计算的拓扑壳",
        "query.mass_properties.empty_gate", {body_id.value});
  }

  long double total_volume = 0.0L;
  long double total_area = 0.0L;
  long double first_x = 0.0L, first_y = 0.0L, first_z = 0.0L;
  for (std::size_t i = 0; i < analysis.value->properties.size(); ++i) {
    const auto &component = analysis.value->properties[i];
    const long double sign = analysis.value->regions[i].role ==
                                     BodyShellRole::Material
                                 ? 1.0L
                                 : -1.0L;
    const long double signed_volume = sign * component.volume;
    total_volume += signed_volume;
    total_area += component.area;
    first_x += signed_volume * component.centroid.x;
    first_y += signed_volume * component.centroid.y;
    first_z += signed_volume * component.centroid.z;
  }
  if (!(total_volume > 0.0L) || !std::isfinite(total_volume) ||
      !std::isfinite(total_area) ||
      total_volume > std::numeric_limits<Scalar>::max() ||
      total_area > std::numeric_limits<Scalar>::max()) {
    return spatial_failure<MassProperties>(
        *state_, StatusCode::NumericalInstability,
        diag_codes::kQueryMassPropertiesFailure,
        "体拓扑质量属性查询失败：多壳汇总超出数值范围",
        "query.mass_properties.numeric", {body_id.value});
  }

  MassProperties properties{};
  properties.volume = static_cast<Scalar>(total_volume);
  properties.area = static_cast<Scalar>(total_area);
  properties.centroid = Point3{static_cast<Scalar>(first_x / total_volume),
                               static_cast<Scalar>(first_y / total_volume),
                               static_cast<Scalar>(first_z / total_volume)};
  for (std::size_t component_index = 0;
       component_index < analysis.value->properties.size(); ++component_index) {
    const auto &component = analysis.value->properties[component_index];
    const long double sign = analysis.value->regions[component_index].role ==
                                     BodyShellRole::Material
                                 ? 1.0L
                                 : -1.0L;
    for (std::size_t i = 0; i < properties.inertia.size(); ++i)
      properties.inertia[i] += static_cast<Scalar>(sign * component.inertia[i]);
    const long double dx =
        static_cast<long double>(component.centroid.x) - properties.centroid.x;
    const long double dy =
        static_cast<long double>(component.centroid.y) - properties.centroid.y;
    const long double dz =
        static_cast<long double>(component.centroid.z) - properties.centroid.z;
    const long double mass = sign * component.volume;
    const long double diagonal[3]{mass * (dy * dy + dz * dz),
                                  mass * (dx * dx + dz * dz),
                                  mass * (dx * dx + dy * dy)};
    properties.inertia[0] += static_cast<Scalar>(diagonal[0]);
    properties.inertia[4] += static_cast<Scalar>(diagonal[1]);
    properties.inertia[8] += static_cast<Scalar>(diagonal[2]);
    const Scalar ixy = static_cast<Scalar>(-mass * dx * dy);
    const Scalar ixz = static_cast<Scalar>(-mass * dx * dz);
    const Scalar iyz = static_cast<Scalar>(-mass * dy * dz);
    properties.inertia[1] += ixy; properties.inertia[3] += ixy;
    properties.inertia[2] += ixz; properties.inertia[6] += ixz;
    properties.inertia[5] += iyz; properties.inertia[7] += iyz;
  }
  if (!(properties.inertia[0] > 0.0) || !(properties.inertia[4] > 0.0) ||
      !(properties.inertia[8] > 0.0) ||
      !std::isfinite(properties.centroid.x) ||
      !std::isfinite(properties.centroid.y) ||
      !std::isfinite(properties.centroid.z) ||
      !std::all_of(properties.inertia.begin(), properties.inertia.end(),
                   [](Scalar value) { return std::isfinite(value); })) {
    return spatial_failure<MassProperties>(
        *state_, StatusCode::NumericalInstability,
        diag_codes::kQueryMassPropertiesFailure,
        "体拓扑质量属性查询失败：质心/惯性张量非有限或惯性对角项非正",
        "query.mass_properties.numeric", {body_id.value});
  }
  return ok_result(properties,
                   state_->create_diagnostic("已按材料/空腔闭壳层级查询实体质量属性"));
}

Result<BodyPointQuery> TopologyQueryService::locate_point(
    BodyId body_id, const Point3 &point, const BodySpatialQueryOptions &options) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto tolerance = spatial_query_tolerance(*state_, options);
  if (!tolerance || !spatial_finite_point(point)) {
    return spatial_failure<BodyPointQuery>(
        *state_, StatusCode::InvalidInput, diag_codes::kCoreParameterOutOfRange,
        "实体点定位失败：坐标、位置容差或三角形预算无效", "query.closest_point.input_gate", {body_id.value});
  }
  const auto analysis = analyze_body_shells(
      *state_, body_id,
      [this](ShellId shell_id) { return shell_mass_properties(shell_id); }, true);
  if (!analysis.value)
    return spatial_preflight_failure<BodyPointQuery>(*state_, analysis, "query.closest_point");
  BodyPointQuery result;
  result.position_tolerance = *tolerance;
  QuerySpatialWork work{options.max_triangle_tests};
  for (const auto &mesh : analysis.value->meshes) {
    for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
      if (!work.take()) {
        return spatial_failure<BodyPointQuery>(
            *state_, StatusCode::InvalidInput, diag_codes::kCoreParameterOutOfRange,
            "实体点定位失败：三角形计算预算耗尽，未返回部分最近点", "query.closest_point.budget", {body_id.value});
      }
      auto nearest = spatial_triangle_closest(point, query_triangle(mesh, mesh.triangles[i]));
      if (!nearest) {
        return spatial_failure<BodyPointQuery>(
            *state_, StatusCode::NumericalInstability, diag_codes::kQueryClosestPointFailure,
            "实体点定位失败：三角形距离计算退化或溢出", "query.closest_point.numeric",
            {body_id.value, mesh.shell.value, mesh.triangle_faces[i].value});
      }
      nearest->shell = mesh.shell;
      nearest->face = mesh.triangle_faces[i];
      const bool stable_tie = result.nearest_boundary &&
          nearest->distance == result.nearest_boundary->distance &&
          (nearest->shell.value < result.nearest_boundary->shell.value ||
           (nearest->shell.value == result.nearest_boundary->shell.value &&
            nearest->face.value < result.nearest_boundary->face.value));
      if (!result.nearest_boundary ||
          nearest->distance < result.nearest_boundary->distance || stable_tie)
        result.nearest_boundary = *nearest;
    }
  }
  if (result.nearest_boundary && result.nearest_boundary->distance <= *tolerance) {
    result.location = BodyPointLocation::Boundary;
  } else {
    const auto inside = spatial_inside_material(point, analysis.value->meshes, work);
    if (!inside) {
      return spatial_failure<BodyPointQuery>(
          *state_, work.exhausted ? StatusCode::InvalidInput : StatusCode::NumericalInstability,
          work.exhausted ? diag_codes::kCoreParameterOutOfRange : diag_codes::kQueryClosestPointFailure,
          work.exhausted ? "实体点定位失败：绕数计算预算耗尽"
                         : "实体点定位失败：闭壳绕数无法可靠判定",
          work.exhausted ? "query.closest_point.budget" : "query.closest_point.numeric", {body_id.value});
    }
    result.location = *inside ? BodyPointLocation::Inside : BodyPointLocation::Outside;
  }
  result.triangle_tests = work.count;
  return ok_result(std::move(result), state_->create_diagnostic("已查询实体材料位置与最近边界"));
}

Result<BodySegmentQuery> TopologyQueryService::clip_segment(
    BodyId body_id, const Point3 &start, const Point3 &end,
    const BodySpatialQueryOptions &options) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto tolerance = spatial_query_tolerance(*state_, options);
  if (!tolerance || !spatial_finite_point(start) || !spatial_finite_point(end)) {
    return detail::invalid_input_result<BodySegmentQuery>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "实体线段裁剪失败：坐标、位置容差或三角形预算无效", "实体线段裁剪失败");
  }
  const auto direction = spatial_delta(start, end);
  const long double length = std::sqrt(spatial_dot(direction, direction));
  if (!std::isfinite(length) || length > std::numeric_limits<Scalar>::max()) {
    return detail::invalid_input_result<BodySegmentQuery>(
        *state_, diag_codes::kCoreParameterOutOfRange,
        "实体线段裁剪失败：线段长度超出有限数值范围", "实体线段裁剪失败");
  }
  if (length <= *tolerance) {
    return detail::failed_result<BodySegmentQuery>(
        *state_, StatusCode::DegenerateGeometry, diag_codes::kGeoDegenerateGeometry,
        "实体线段裁剪失败：线段长度不大于位置容差", "实体线段裁剪失败",
        {body_id.value});
  }
  const auto analysis = analyze_body_shells(
      *state_, body_id,
      [this](ShellId shell_id) { return shell_mass_properties(shell_id); }, true);
  if (!analysis.value)
    return error_result<BodySegmentQuery>(analysis.status, analysis.diagnostic_id);
  BodySegmentQuery result;
  result.position_tolerance = *tolerance;
  QuerySpatialWork work{options.max_triangle_tests};
  std::vector<long double> cuts{0, 1};
  std::vector<std::array<long double, 2>> coplanar_spans;
  std::vector<long double> contact_parameters;
  long double boundary_scale = 0;
  bool numerical_failure = false;
  for (const auto &mesh : analysis.value->meshes) {
    for (const auto &indices : mesh.triangles) {
      if (!work.take()) {
        return detail::invalid_input_result<BodySegmentQuery>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "实体线段裁剪失败：三角形求交预算耗尽，未返回部分区间", "实体线段裁剪失败");
      }
      const auto triangle = query_triangle(mesh, indices);
      const auto edge = spatial_delta(triangle[0], triangle[1]);
      boundary_scale = std::max(boundary_scale, std::sqrt(spatial_dot(edge, edge)));
      const auto hit = spatial_triangle_segment(start, end, triangle, numerical_failure);
      if (numerical_failure) {
        return detail::failed_result<BodySegmentQuery>(
            *state_, StatusCode::NumericalInstability, diag_codes::kQuerySectionFailure,
            "实体线段裁剪失败：局部面片求交退化或溢出", "实体线段裁剪失败",
            {body_id.value, mesh.shell.value});
      }
      if (!hit) continue;
      cuts.push_back(hit->parameters[0]);
      cuts.push_back(hit->parameters[1]);
      contact_parameters.push_back(hit->parameters[0]);
      contact_parameters.push_back(hit->parameters[1]);
      if (hit->coplanar && hit->parameters[0] < hit->parameters[1])
        coplanar_spans.push_back(hit->parameters);
    }
  }
  // Only representational roundoff is merged, never the modelling tolerance.
  // This removes duplicate hits on shared edges/diagonals while preserving
  // narrow cavities that remain resolvable in the normalized parameter domain.
  const Scalar parameter_roundoff = 32 * std::numeric_limits<Scalar>::epsilon();
  std::sort(cuts.begin(), cuts.end());
  const long double boundary_roundoff =
      256 * std::numeric_limits<Scalar>::epsilon() * boundary_scale;
  std::vector<long double> unique_cuts;
  for (const long double cut : cuts) {
    if (unique_cuts.empty() || cut - unique_cuts.back() > parameter_roundoff) {
      unique_cuts.push_back(cut);
    } else {
      if ((cut - unique_cuts.back()) * length > boundary_roundoff) {
        return detail::failed_result<BodySegmentQuery>(
            *state_, StatusCode::NumericalInstability, diag_codes::kQuerySectionFailure,
            "实体线段裁剪失败：不同边界事件在归一化参数中无法可靠分离",
            "实体线段裁剪失败", {body_id.value});
      }
      if (cut == 1) unique_cuts.back() = 1;
    }
  }
  long double material_length = 0;
  for (std::size_t i = 1; i < unique_cuts.size(); ++i) {
    const long double cell_lo = unique_cuts[i - 1], cell_hi = unique_cuts[i];
    const Scalar lo = static_cast<Scalar>(cell_lo), hi = static_cast<Scalar>(cell_hi);
    const long double midpoint = (cell_lo + cell_hi) / 2;
    BodyPointLocation location = BodyPointLocation::Outside;
    const bool on_boundary = std::any_of(coplanar_spans.begin(), coplanar_spans.end(),
        [midpoint](const auto &span) { return span[0] <= midpoint && midpoint <= span[1]; });
    if (on_boundary) {
      location = BodyPointLocation::Boundary;
    } else {
      const auto inside = spatial_inside_material(spatial_interpolate(start, end, midpoint),
                                                  analysis.value->meshes, work);
      if (!inside) {
        return detail::failed_result<BodySegmentQuery>(
            *state_, work.exhausted ? StatusCode::InvalidInput : StatusCode::NumericalInstability,
            work.exhausted ? diag_codes::kCoreParameterOutOfRange : diag_codes::kQuerySectionFailure,
            work.exhausted ? "实体线段裁剪失败：材料分类预算耗尽"
                           : "实体线段裁剪失败：区间绕数无法可靠判定",
            "实体线段裁剪失败", {body_id.value});
      }
      if (*inside) location = BodyPointLocation::Inside;
    }
    if (location == BodyPointLocation::Outside) continue;
    if (location == BodyPointLocation::Inside)
      material_length += (cell_hi - cell_lo) * length;
    if (!result.intervals.empty() && result.intervals.back().location == location &&
        result.intervals.back().parameters.max == lo) {
      result.intervals.back().parameters.max = hi;
    } else {
      result.intervals.push_back({{lo, hi}, location});
    }
  }
  std::sort(contact_parameters.begin(), contact_parameters.end());
  for (const long double contact : contact_parameters) {
    const Scalar parameter = static_cast<Scalar>(contact);
    const bool covered = std::any_of(result.intervals.begin(), result.intervals.end(),
        [parameter, parameter_roundoff](const BodySegmentInterval &interval) {
          return interval.parameters.min - parameter_roundoff <= parameter &&
                 parameter <= interval.parameters.max + parameter_roundoff;
        });
    if (!covered)
      result.intervals.push_back({{parameter, parameter}, BodyPointLocation::Boundary});
  }
  std::sort(result.intervals.begin(), result.intervals.end(),
      [](const BodySegmentInterval &a, const BodySegmentInterval &b) {
        return a.parameters.min < b.parameters.min;
      });
  result.material_length = static_cast<Scalar>(material_length);
  result.triangle_tests = work.count;
  if (!std::isfinite(result.material_length)) {
    return detail::failed_result<BodySegmentQuery>(
        *state_, StatusCode::NumericalInstability, diag_codes::kQuerySectionFailure,
        "实体线段裁剪失败：材料总长度溢出", "实体线段裁剪失败", {body_id.value});
  }
  return ok_result(std::move(result), state_->create_diagnostic("已查询有限线段材料与边界区间"));
}

Result<BodyPlaneSection> TopologyQueryService::section(
    BodyId body_id, const Plane &plane, const BodySpatialQueryOptions &options) const {
  TopoQueryAuditScope audit{state_};
  const auto failure = [&](StatusCode status, std::string_view code, std::string message,
                           std::string_view stage) {
    return spatial_failure<BodyPlaneSection>(*state_, status, code, std::move(message),
                                             stage, {body_id.value});
  };
  QuerySpatialVector normal{plane.normal.x, plane.normal.y, plane.normal.z};
  const long double normal_length = std::sqrt(spatial_dot(normal,normal));
  if (!spatial_query_tolerance(*state_,options) || !spatial_finite_point(plane.origin) ||
      !std::isfinite(normal_length) || !(normal_length > 0))
    return failure(StatusCode::InvalidInput, diag_codes::kCoreParameterOutOfRange,
                   "截面平面、位置容差或预算无效", "query.section.input_gate");
  for (auto &component : normal) component /= normal_length;
  const auto analysis = analyze_body_shells(*state_,body_id,
      [this](ShellId shell) { return shell_mass_properties(shell); },true);
  if (!analysis.value)
    return spatial_preflight_failure<BodyPlaneSection>(*state_,analysis,"query.section");
  BodyPlaneSection result;
  if (analysis.value->meshes.empty()) return ok_result(std::move(result),analysis.diagnostic_id);

  // A local orthonormal plane frame avoids global-coordinate cancellation.
  const auto reference = analysis.value->meshes.front().points.front();
  const long double height = spatial_dot(spatial_delta(plane.origin,reference),normal);
  QuerySpatialVector origin{static_cast<long double>(reference.x)-height*normal[0],
                             static_cast<long double>(reference.y)-height*normal[1],
                             static_cast<long double>(reference.z)-height*normal[2]};
  QuerySpatialVector seed{};
  const auto axis = static_cast<std::size_t>(std::min_element(normal.begin(),normal.end(),
      [](long double a, long double b) { return std::abs(a) < std::abs(b); })-normal.begin());
  seed[axis] = 1;
  auto u = spatial_cross(seed,normal);
  const long double u_length = std::sqrt(spatial_dot(u,u));
  for (auto &component : u) component /= u_length;
  auto v = spatial_cross(normal,u);
  // Avoid aligning the decomposition direction with common polygon edges or
  // sampled revolution symmetries. Distinct near-coincident events below still
  // fail explicitly instead of silently deleting thin material cells.
  const long double cosine = std::cos(1.23456789L), sine = std::sin(1.23456789L);
  const auto original_u = u;
  for (std::size_t i = 0; i < 3; ++i) {
    u[i] = cosine*original_u[i]+sine*v[i];
    v[i] = cosine*v[i]-sine*original_u[i];
  }
  using PlanePoint = std::array<long double,2>;
  const auto project = [&](const Point3 &point) {
    const QuerySpatialVector delta{static_cast<long double>(point.x)-origin[0],
                                    static_cast<long double>(point.y)-origin[1],
                                    static_cast<long double>(point.z)-origin[2]};
    return PlanePoint{spatial_dot(delta,u),spatial_dot(delta,v)};
  };
  const auto world = [&](const PlanePoint &point) {
    return Point3{static_cast<Scalar>(origin[0]+point[0]*u[0]+point[1]*v[0]),
                  static_cast<Scalar>(origin[1]+point[0]*u[1]+point[1]*v[1]),
                  static_cast<Scalar>(origin[2]+point[0]*u[2]+point[1]*v[2])};
  };
  long double scale = 0;
  for (const auto &mesh : analysis.value->meshes)
    for (const auto &point : mesh.points) {
      const auto delta = spatial_delta(reference,point);
      scale = std::max(scale,std::sqrt(spatial_dot(delta,delta)));
    }
  // Caller modelling tolerance is deliberately absent from plane intersection:
  // it must not turn a near miss into material or fill a resolvable cavity.
  const long double roundoff = 64*std::numeric_limits<Scalar>::epsilon()*scale;
  QuerySpatialWork work{options.max_triangle_tests};
  std::vector<std::array<PlanePoint,2>> segments;
  std::vector<std::array<Point3,3>> coplanar;
  std::vector<long double> cuts;
  const auto add_segment = [&](const Point3 &first, const Point3 &second) {
    auto a = project(first), b = project(second);
    if (b < a) std::swap(a,b);
    segments.push_back({a,b});
    cuts.push_back(a[0]); cuts.push_back(b[0]);
    const auto p = world(a), q = world(b);
    result.boundary_segments.push_back({p,q});
    extend_query_bbox(result.bbox,p); extend_query_bbox(result.bbox,q);
  };
  for (const auto &mesh : analysis.value->meshes) {
    for (const auto &indices : mesh.triangles) {
      if (!work.take())
        return failure(StatusCode::InvalidInput,diag_codes::kCoreParameterOutOfRange,
                       "截面三角形预算耗尽", "query.section.budget");
      const auto triangle = query_triangle(mesh,indices);
      std::array<long double,3> distances{};
      std::size_t zeros = 0;
      for (std::size_t i = 0; i < 3; ++i) {
        // Use the supplied normal for the intersection predicate, avoiding an
        // extra normalization rounding. Do not snap a nearby exterior plane
        // onto a face. Cancellation below arithmetic resolution is a failure,
        // whereas an exact zero retains face/edge/vertex contact semantics.
        const auto delta = spatial_delta(plane.origin,triangle[i]);
        const QuerySpatialVector terms{delta[0]*plane.normal.x,
                                        delta[1]*plane.normal.y,
                                        delta[2]*plane.normal.z};
        distances[i] = terms[0]+terms[1]+terms[2];
        if (!std::isfinite(distances[i]))
          return failure(StatusCode::NumericalInstability,diag_codes::kQuerySectionFailure,
                         "截面有向平面距离溢出", "query.section.numeric");
        const long double predicate_roundoff = 8*std::numeric_limits<long double>::epsilon()*
            (std::abs(terms[0])+std::abs(terms[1])+std::abs(terms[2]));
        if (distances[i] == 0) ++zeros;
        else if (std::abs(distances[i]) <= predicate_roundoff)
          return failure(StatusCode::NumericalInstability,diag_codes::kQuerySectionFailure,
                         "截面平面距离符号在舍入尺度内不可可靠分辨", "query.section.numeric");
      }
      if (zeros == 3) {
        coplanar.push_back(triangle);
        for (std::size_t i = 0; i < 3; ++i) add_segment(triangle[i],triangle[(i+1)%3]);
        continue;
      }
      std::vector<Point3> hits;
      for (std::size_t i = 0; i < 3; ++i) {
        const auto j = (i+1)%3;
        if (distances[i] == 0) hits.push_back(triangle[i]);
        if ((distances[i] < 0 && distances[j] > 0) ||
            (distances[i] > 0 && distances[j] < 0)) {
          // Canonical edge direction gives bit-identical shared-edge events,
          // independent of the orientation of its two adjacent triangles.
          const auto key = [](const Point3 &p) { return std::array{p.x,p.y,p.z}; };
          const bool forward = key(triangle[i]) < key(triangle[j]);
          const auto first = forward ? i : j, second = forward ? j : i;
          hits.push_back(spatial_interpolate(triangle[first],triangle[second],
                                            distances[first]/(distances[first]-distances[second])));
        }
      }
      if (hits.size() == 2) add_segment(hits[0],hits[1]);
      else if (hits.size() == 1) {
        const auto point = world(project(hits.front()));
        result.contact_points.push_back(point);
        extend_query_bbox(result.bbox,point);
      }
    }
  }

  // Vertical decomposition of the actual intersection segments. Between two
  // vertex abscissae their order cannot change for an embedded closed boundary.
  // Classifying each open cell by the same shell parity as locate_point handles
  // concavity, holes, disjoint material components and nested material islands.
  // Coplanar triangles contribute filled cells independently of shell winding.
  std::sort(cuts.begin(),cuts.end());
  std::vector<long double> unique_cuts;
  for (const auto cut : cuts) {
    if (!unique_cuts.empty() && cut != unique_cuts.back() && cut-unique_cuts.back() <= roundoff)
      return failure(StatusCode::NumericalInstability,diag_codes::kQuerySectionFailure,
                     "截面不同边界事件在平面坐标中不可可靠分辨", "query.section.numeric");
    if (unique_cuts.empty() || cut != unique_cuts.back()) unique_cuts.push_back(cut);
  }
  long double area = 0;
  const auto append_triangle = [&](const PlanePoint &a, const PlanePoint &b, const PlanePoint &c) {
    const long double twice_area = (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
    if (twice_area == 0) return true;
    if (!std::isfinite(twice_area) || twice_area < 0 || result.vertices.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max()-3)) return false;
    const auto p = world(a), q = world(b), r = world(c);
    if (!spatial_finite_point(p) || !spatial_finite_point(q) || !spatial_finite_point(r)) return false;
    const auto pq = spatial_delta(p,q), pr = spatial_delta(p,r);
    const auto cross = spatial_cross(pq,pr);
    // If casting world positions collapsed a real cell, fail without partial mesh.
    if (!(spatial_dot(cross,cross) > 0)) return false;
    const int base = static_cast<int>(result.vertices.size());
    result.vertices.insert(result.vertices.end(),{p,q,r});
    result.triangles.push_back({base,base+1,base+2});
    area += twice_area/2;
    return true;
  };
  for (std::size_t slab = 1; slab < unique_cuts.size(); ++slab) {
    const long double left = unique_cuts[slab-1], right = unique_cuts[slab];
    const long double middle = (left+right)/2;
    struct Crossing { long double y, left_y, right_y; };
    std::vector<Crossing> crossings;
    for (const auto &segment : segments) {
      if (!work.take())
        return failure(StatusCode::InvalidInput,diag_codes::kCoreParameterOutOfRange,
                       "截面区域划分预算耗尽", "query.section.budget");
      const auto &a = segment[0], &b = segment[1];
      if (!(std::min(a[0],b[0]) < middle && middle < std::max(a[0],b[0]))) continue;
      const auto ordinate = [&](long double x) { return a[1]+(x-a[0])*(b[1]-a[1])/(b[0]-a[0]); };
      crossings.push_back({ordinate(middle),ordinate(left),ordinate(right)});
    }
    std::sort(crossings.begin(),crossings.end(),
              [](const Crossing &a, const Crossing &b) { return a.y < b.y; });
    std::vector<Crossing> unique;
    for (const auto &crossing : crossings) {
      if (!unique.empty() && crossing.y != unique.back().y && crossing.y-unique.back().y <= roundoff)
        return failure(StatusCode::NumericalInstability,diag_codes::kQuerySectionFailure,
                       "截面不同交段在平面坐标中不可可靠分辨", "query.section.numeric");
      if (unique.empty() || crossing.y != unique.back().y) unique.push_back(crossing);
    }
    for (std::size_t cell = 1; cell < unique.size(); ++cell) {
      const auto &lower = unique[cell-1], &upper = unique[cell];
      const auto midpoint = world({middle,(lower.y+upper.y)/2});
      bool on_boundary = false;
      for (const auto &triangle : coplanar) {
        if (!work.take())
          return failure(StatusCode::InvalidInput,diag_codes::kCoreParameterOutOfRange,
                         "共面截面分类预算耗尽", "query.section.budget");
        const auto closest = spatial_triangle_closest(midpoint,triangle);
        if (!closest)
          return failure(StatusCode::NumericalInstability,diag_codes::kQuerySectionFailure,
                         "共面截面距离计算失败", "query.section.numeric");
        if (closest->distance <= roundoff) { on_boundary = true; break; }
      }
      if (!on_boundary) {
        const auto inside = spatial_inside_material(midpoint,analysis.value->meshes,work);
        if (!inside)
          return failure(work.exhausted ? StatusCode::InvalidInput : StatusCode::NumericalInstability,
                         work.exhausted ? diag_codes::kCoreParameterOutOfRange : diag_codes::kQuerySectionFailure,
                         "截面材料分类失败或预算耗尽",
                         work.exhausted ? "query.section.budget" : "query.section.numeric");
        if (!*inside) continue;
      }
      PlanePoint a{left,lower.left_y}, b{right,lower.right_y};
      PlanePoint c{right,upper.right_y}, d{left,upper.left_y};
      // Shared-vertex roundoff can reverse a collapsed end of a trapezoid.
      if (std::abs(a[1]-d[1]) <= roundoff) a[1] = d[1] = (a[1]+d[1])/2;
      if (std::abs(b[1]-c[1]) <= roundoff) b[1] = c[1] = (b[1]+c[1])/2;
      if (!append_triangle(a,b,c) || !append_triangle(a,c,d))
        return failure(StatusCode::NumericalInstability,diag_codes::kQuerySectionFailure,
                       "截面单元交叉、溢出或世界坐标不可分辨", "query.section.numeric");
    }
  }
  result.area = static_cast<Scalar>(area);
  result.triangle_tests = work.count;
  if (!std::isfinite(result.area))
    return failure(StatusCode::NumericalInstability,diag_codes::kQuerySectionFailure,
                   "截面面积溢出", "query.section.numeric");
  return ok_result(std::move(result),state_->create_diagnostic("已查询真实多面体平面截面"));
}

Result<BodyDistanceQuery> TopologyQueryService::closest_points(
    BodyId lhs, BodyId rhs, const BodySpatialQueryOptions &options) const {
  TopoQueryAuditScope audit{state_};
  const auto failure = [&](StatusCode status, std::string_view code, std::string message,
                           std::string_view stage) {
    return spatial_failure<BodyDistanceQuery>(*state_,status,code,std::move(message),
                                              stage,{lhs.value,rhs.value});
  };
  if (!spatial_query_tolerance(*state_,options))
    return failure(StatusCode::InvalidInput,diag_codes::kCoreParameterOutOfRange,
                   "实体距离位置容差或预算无效", "query.distance.input_gate");
  const auto measure = [this](ShellId shell) { return shell_mass_properties(shell); };
  const auto first = analyze_body_shells(*state_,lhs,measure,true);
  if (!first.value) return spatial_preflight_failure<BodyDistanceQuery>(*state_,first,"query.distance");
  const auto second = analyze_body_shells(*state_,rhs,measure,true);
  if (!second.value) return spatial_preflight_failure<BodyDistanceQuery>(*state_,second,"query.distance");
  if (first.value->meshes.empty() || second.value->meshes.empty())
    return failure(StatusCode::DegenerateGeometry,diag_codes::kQueryClosestPointFailure,
                   "空实体没有有限最近位置", "query.distance.empty_gate");
  QuerySpatialWork work{options.max_triangle_tests};
  BodyDistanceQuery best;
  best.distance = std::numeric_limits<Scalar>::infinity();
  for (const auto &a : first.value->meshes) {
    for (const auto &b : second.value->meshes) {
      for (std::size_t i = 0; i < a.triangles.size(); ++i) {
        for (std::size_t j = 0; j < b.triangles.size(); ++j) {
          if (!work.take())
            return failure(StatusCode::InvalidInput,diag_codes::kCoreParameterOutOfRange,
                           "实体距离三角形对预算耗尽", "query.distance.budget");
          auto candidate = spatial_triangle_distance(query_triangle(a,a.triangles[i]),
                                                       query_triangle(b,b.triangles[j]));
          if (!candidate)
            return failure(StatusCode::NumericalInstability,diag_codes::kQueryClosestPointFailure,
                           "实体距离面/边距离退化或溢出", "query.distance.numeric");
          candidate->first_face = a.triangle_faces[i]; candidate->first_shell = a.shell;
          candidate->second_face = b.triangle_faces[j]; candidate->second_shell = b.shell;
          const std::array key{a.shell.value,a.triangle_faces[i].value,b.shell.value,b.triangle_faces[j].value};
          const std::array best_key{best.first_shell.value,best.first_face.value,
                                    best.second_shell.value,best.second_face.value};
          if (candidate->distance < best.distance ||
              (candidate->distance == best.distance && key < best_key)) best = *candidate;
        }
      }
    }
  }
  // Nonintersecting boundaries do not exclude solid containment. Testing one
  // boundary representative per shell is sufficient after all boundary pairs
  // have a strictly positive distance; parity excludes cavities automatically.
  if (best.distance > 0) {
    for (int side = 0; side < 2; ++side) {
      const auto &source = side == 0 ? first.value->meshes : second.value->meshes;
      const auto &target = side == 0 ? second.value->meshes : first.value->meshes;
      for (const auto &mesh : source) {
        const auto point = mesh.points.front();
        const auto inside = spatial_inside_material(point,target,work);
        if (!inside)
          return failure(work.exhausted ? StatusCode::InvalidInput : StatusCode::NumericalInstability,
                         work.exhausted ? diag_codes::kCoreParameterOutOfRange : diag_codes::kQueryClosestPointFailure,
                         "实体距离包含分类失败或预算耗尽",
                         work.exhausted ? "query.distance.budget" : "query.distance.numeric");
        const auto selected_shell = side == 0 ? best.first_shell : best.second_shell;
        if (*inside && (best.distance > 0 || mesh.shell.value < selected_shell.value)) {
          best = {};
          best.first_point = best.second_point = point;
          if (side == 0) { best.first_face = mesh.triangle_faces.front(); best.first_shell = mesh.shell; }
          else { best.second_face = mesh.triangle_faces.front(); best.second_shell = mesh.shell; }
        }
      }
      if (best.distance == 0) break;
    }
  }
  best.triangle_tests = work.count;
  return ok_result(best,state_->create_diagnostic("已查询真实实体材料最近位置与距离"));
}

Result<BoundingBox>
TopologyQueryService::bbox_of_shell(ShellId shell_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::invalid_input_result<BoundingBox>(
        *state_, diag_codes::kCoreInvalidHandle,
        "壳包围盒查询失败：目标壳不存在", "壳包围盒查询失败");
  }
  BoundingBox bbox{};
  for (const auto face_id : shell_it->second.faces) {
    if (!topo_internal::append_face_bbox(state_, face_id, bbox)) {
      return detail::failed_result<BoundingBox>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "壳包围盒查询失败：壳面拓扑不完整", "壳包围盒查询失败");
    }
  }
  if (!bbox.is_valid) {
    return detail::failed_result<BoundingBox>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "壳包围盒查询失败：壳不包含可用拓扑", "壳包围盒查询失败");
  }
  return ok_result(bbox, state_->create_diagnostic("已查询壳包围盒"));
}

Result<BoundingBox>
TopologyQueryService::bbox_of_body_from_topology(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::invalid_input_result<BoundingBox>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体拓扑包围盒查询失败：目标体不存在", "体拓扑包围盒查询失败");
  }
  const auto bbox = compute_body_bbox(state_, body_it->second.shells);
  if (!bbox.is_valid) {
    return detail::failed_result<BoundingBox>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "体拓扑包围盒查询失败：体拓扑不完整", "体拓扑包围盒查询失败");
  }
  return ok_result(bbox, state_->create_diagnostic("已查询体拓扑包围盒"));
}

Result<std::vector<FaceId>>
TopologyQueryService::faces_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto shells = shells_of_body(body_id);
  if (shells.status != StatusCode::Ok || !shells.value.has_value()) {
    return error_result<std::vector<FaceId>>(shells.status,
                                             shells.diagnostic_id);
  }
  std::vector<FaceId> out;
  for (const auto shell_id : *shells.value) {
    const auto faces = faces_of_shell(shell_id);
    if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
      return error_result<std::vector<FaceId>>(faces.status,
                                               faces.diagnostic_id);
    }
    for (const auto face_id : *faces.value)
      append_unique(out, face_id);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询体的全部面"));
}

Result<std::vector<LoopId>>
TopologyQueryService::loops_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto faces = faces_of_body(body_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<std::vector<LoopId>>(faces.status, faces.diagnostic_id);
  }
  std::vector<LoopId> out;
  for (const auto face_id : *faces.value) {
    const auto loops = loops_of_face(face_id);
    if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
      return error_result<std::vector<LoopId>>(loops.status,
                                               loops.diagnostic_id);
    }
    for (const auto loop_id : *loops.value)
      append_unique(out, loop_id);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询体的全部环"));
}

Result<std::vector<EdgeId>>
TopologyQueryService::edges_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto loops = loops_of_body(body_id);
  if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
    return error_result<std::vector<EdgeId>>(loops.status, loops.diagnostic_id);
  }
  std::vector<EdgeId> out;
  for (const auto loop_id : *loops.value) {
    const auto edges = edges_of_loop(loop_id);
    if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
      return error_result<std::vector<EdgeId>>(edges.status,
                                               edges.diagnostic_id);
    }
    for (const auto edge_id : *edges.value)
      append_unique(out, edge_id);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询体的全部边"));
}

Result<std::vector<VertexId>>
TopologyQueryService::vertices_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::vector<VertexId>>(edges.status,
                                               edges.diagnostic_id);
  }
  std::vector<VertexId> out;
  for (const auto edge_id : *edges.value) {
    const auto vertices = vertices_of_edge(edge_id);
    if (vertices.status != StatusCode::Ok || !vertices.value.has_value()) {
      return error_result<std::vector<VertexId>>(vertices.status,
                                                 vertices.diagnostic_id);
    }
    append_unique(out, vertices.value->at(0));
    append_unique(out, vertices.value->at(1));
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已查询体的全部顶点"));
}

Result<std::uint64_t>
TopologyQueryService::face_count_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto faces = faces_of_body(body_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<std::uint64_t>(faces.status, faces.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(faces.value->size()),
      state_->create_diagnostic("已查询体面数量"));
}

Result<std::uint64_t>
TopologyQueryService::loop_count_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto loops = loops_of_body(body_id);
  if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
    return error_result<std::uint64_t>(loops.status, loops.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(loops.value->size()),
      state_->create_diagnostic("已查询体环数量"));
}

Result<std::uint64_t>
TopologyQueryService::edge_count_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(edges.value->size()),
      state_->create_diagnostic("已查询体边数量"));
}

Result<std::uint64_t>
TopologyQueryService::vertex_count_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto vertices = vertices_of_body(body_id);
  if (vertices.status != StatusCode::Ok || !vertices.value.has_value()) {
    return error_result<std::uint64_t>(vertices.status, vertices.diagnostic_id);
  }
  return ok_result<std::uint64_t>(
      static_cast<std::uint64_t>(vertices.value->size()),
      state_->create_diagnostic("已查询体顶点数量"));
}

Result<bool> TopologyQueryService::body_has_face(BodyId body_id,
                                                 FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto faces = faces_of_body(body_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<bool>(faces.status, faces.diagnostic_id);
  }
  return ok_result(
      std::any_of(faces.value->begin(), faces.value->end(),
                  [face_id](FaceId id) { return id.value == face_id.value; }),
      state_->create_diagnostic("已查询体是否包含面"));
}

Result<bool> TopologyQueryService::shell_has_face(ShellId shell_id,
                                                  FaceId face_id) const {
  const auto faces = faces_of_shell(shell_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<bool>(faces.status, faces.diagnostic_id);
  }
  return ok_result(
      std::any_of(faces.value->begin(), faces.value->end(),
                  [face_id](FaceId id) { return id.value == face_id.value; }),
      state_->create_diagnostic("已查询壳是否包含面"));
}

Result<bool> TopologyQueryService::face_has_loop(FaceId face_id,
                                                 LoopId loop_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto loops = loops_of_face(face_id);
  if (loops.status != StatusCode::Ok || !loops.value.has_value()) {
    return error_result<bool>(loops.status, loops.diagnostic_id);
  }
  return ok_result(
      std::any_of(loops.value->begin(), loops.value->end(),
                  [loop_id](LoopId id) { return id.value == loop_id.value; }),
      state_->create_diagnostic("已查询面是否包含环"));
}

Result<bool> TopologyQueryService::loop_has_edge(LoopId loop_id,
                                                 EdgeId edge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto edges = edges_of_loop(loop_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<bool>(edges.status, edges.diagnostic_id);
  }
  return ok_result(
      std::any_of(edges.value->begin(), edges.value->end(),
                  [edge_id](EdgeId id) { return id.value == edge_id.value; }),
      state_->create_diagnostic("已查询环是否包含边"));
}

Result<bool> TopologyQueryService::edge_has_vertex(EdgeId edge_id,
                                                   VertexId vertex_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto vertices = vertices_of_edge(edge_id);
  if (vertices.status != StatusCode::Ok || !vertices.value.has_value()) {
    return error_result<bool>(vertices.status, vertices.diagnostic_id);
  }
  return ok_result(vertices.value->at(0).value == vertex_id.value ||
                       vertices.value->at(1).value == vertex_id.value,
                   state_->create_diagnostic("已查询边是否包含顶点"));
}

Result<std::uint64_t>
TopologyQueryService::shared_face_count_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto faces = faces_of_body(body_id);
  if (faces.status != StatusCode::Ok || !faces.value.has_value()) {
    return error_result<std::uint64_t>(faces.status, faces.diagnostic_id);
  }
  std::uint64_t count = 0;
  for (const auto face_id : *faces.value) {
    const auto owners = owner_count_of_face(face_id);
    if (owners.status != StatusCode::Ok || !owners.value.has_value()) {
      return error_result<std::uint64_t>(owners.status, owners.diagnostic_id);
    }
    if (*owners.value > 1)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计体共享面数量"));
}

Result<std::uint64_t>
TopologyQueryService::shared_edge_count_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  std::uint64_t count = 0;
  for (const auto edge_id : *edges.value) {
    const auto owners = owner_count_of_edge(edge_id);
    if (owners.status != StatusCode::Ok || !owners.value.has_value()) {
      return error_result<std::uint64_t>(owners.status, owners.diagnostic_id);
    }
    if (*owners.value > 1)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计体共享边数量"));
}

Result<std::uint64_t>
TopologyQueryService::boundary_edge_count_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  std::uint64_t count = 0;
  for (const auto edge_id : *edges.value) {
    const auto is_boundary = is_edge_boundary(edge_id);
    if (is_boundary.status != StatusCode::Ok ||
        !is_boundary.value.has_value()) {
      return error_result<std::uint64_t>(is_boundary.status,
                                         is_boundary.diagnostic_id);
    }
    if (*is_boundary.value)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计体边界边数量"));
}

Result<std::uint64_t>
TopologyQueryService::non_manifold_edge_count_of_body(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto edges = edges_of_body(body_id);
  if (edges.status != StatusCode::Ok || !edges.value.has_value()) {
    return error_result<std::uint64_t>(edges.status, edges.diagnostic_id);
  }
  std::uint64_t count = 0;
  for (const auto edge_id : *edges.value) {
    const auto is_non_manifold = is_edge_non_manifold(edge_id);
    if (is_non_manifold.status != StatusCode::Ok ||
        !is_non_manifold.value.has_value()) {
      return error_result<std::uint64_t>(is_non_manifold.status,
                                         is_non_manifold.diagnostic_id);
    }
    if (*is_non_manifold.value)
      ++count;
  }
  return ok_result<std::uint64_t>(
      count, state_->create_diagnostic("已统计体非流形边数量"));
}

Result<bool>
TopologyQueryService::is_body_topology_empty(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体拓扑空性查询失败：目标体不存在", "体拓扑空性查询失败");
  }
  return ok_result(body_it->second.shells.empty(),
                   state_->create_diagnostic("已查询体拓扑是否为空"));
}

Result<PCurveId> TopologyQueryService::pcurve_of_coedge(CoedgeId coedge_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto it = state_->coedges.find(coedge_id.value);
  if (it == state_->coedges.end()) {
    return detail::invalid_input_result<PCurveId>(
        *state_, diag_codes::kCoreInvalidHandle,
        "共边参数曲线查询失败：目标定向边不存在", "共边参数曲线查询失败");
  }
  return ok_result(it->second.pcurve_id,
                   state_->create_diagnostic("已查询共边参数曲线"));
}


Result<SurfaceId>
TopologyQueryService::underlying_surface_for_face_trim(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<SurfaceId>(
        *state_, diag_codes::kCoreInvalidHandle,
        "面修剪基曲面查询失败：目标面不存在", "面修剪基曲面查询失败");
  }
  const auto &face = state_->faces.at(face_id.value);
  if (!detail::has_surface(*state_, face.surface_id)) {
    return error_result<SurfaceId>(
        StatusCode::InvalidTopology,
        detail::error_diag(
            *state_, "面修剪基曲面查询失败", diag_codes::kTopoFaceOuterLoopInvalid,
            "面修剪基曲面查询失败：面引用的曲面不存在",
            std::vector<std::uint64_t>{face_id.value, face.surface_id.value}));
  }
  const SurfaceId base =
      underlying_trim_base_surface_id(*state_, face.surface_id);
  return ok_result(base, state_->create_diagnostic("已查询面修剪用底层曲面"));
}

Result<Range2D>
TopologyQueryService::face_outer_loop_uv_bounds(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<Range2D>(
        *state_, diag_codes::kCoreInvalidHandle,
        "面外环 UV 包围查询失败：目标面不存在", "面外环 UV 包围查询失败");
  }
  const auto &face = state_->faces.at(face_id.value);
  const auto loop_it = state_->loops.find(face.outer_loop.value);
  if (loop_it == state_->loops.end() || loop_it->second.coedges.empty()) {
    return error_result<Range2D>(
        StatusCode::InvalidTopology,
        detail::error_diag(
            *state_, "面外环 UV 包围查询失败", diag_codes::kTopoFaceOuterLoopInvalid,
            "面外环 UV 包围查询失败：外环不存在或为空",
            std::vector<std::uint64_t>{face_id.value, face.outer_loop.value}));
  }
  bool initialized = false;
  Scalar u_min = 0.0;
  Scalar u_max = 0.0;
  Scalar v_min = 0.0;
  Scalar v_max = 0.0;
  for (const auto coedge_id : loop_it->second.coedges) {
    const auto ce_it = state_->coedges.find(coedge_id.value);
    if (ce_it == state_->coedges.end()) {
      return error_result<Range2D>(
          StatusCode::InvalidTopology,
          detail::error_diag(
              *state_, "面外环 UV 包围查询失败", diag_codes::kTopoFaceOuterLoopInvalid,
              "面外环 UV 包围查询失败：共边记录缺失",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value}));
    }
    const PCurveId pc_id = ce_it->second.pcurve_id;
    if (pc_id.value == 0 || !detail::has_pcurve(*state_, pc_id)) {
      return error_result<Range2D>(
          StatusCode::InvalidTopology,
          detail::error_diag(
              *state_, "面外环 UV 包围查询失败", diag_codes::kTopoRelationInconsistent,
              "面外环 UV 包围查询失败：共边未绑定有效 PCurve",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value,
                                           pc_id.value}));
    }
    const auto &pc = state_->pcurves.at(pc_id.value);
    if (!accumulate_polyline_pcurve_uv_bounds(pc, initialized, u_min, u_max,
                                               v_min, v_max)) {
      return error_result<Range2D>(
          StatusCode::NotImplemented,
          detail::error_diag(
              *state_, "面外环 UV 包围查询失败", diag_codes::kCoreOperationUnsupported,
              "面外环 UV 包围查询失败：暂仅支持折线型 PCurve 且须含有效顶点",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value,
                                           pc_id.value}));
    }
  }
  if (!initialized) {
    return error_result<Range2D>(
        StatusCode::DegenerateGeometry,
        detail::error_diag(
            *state_, "面外环 UV 包围查询失败", diag_codes::kGeoDegenerateGeometry,
            "面外环 UV 包围查询失败：未能从 PCurve 得到有效范围",
            std::vector<std::uint64_t>{face_id.value}));
  }
  ensure_strict_increasing_range(u_min, u_max);
  ensure_strict_increasing_range(v_min, v_max);
  return ok_result(Range2D{Range1D{u_min, u_max}, Range1D{v_min, v_max}},
                   state_->create_diagnostic("已计算面外环 UV 轴对齐包围"));
}

Result<std::vector<Point2>>
TopologyQueryService::face_loop_uv_polyline(FaceId face_id, LoopId loop_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<std::vector<Point2>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "面环 UV 折线查询失败：目标面不存在", "面环 UV 折线查询失败");
  }
  const auto &face = state_->faces.at(face_id.value);
  bool loop_ok = (loop_id.value == face.outer_loop.value);
  if (!loop_ok) {
    for (const auto &il : face.inner_loops) {
      if (il.value == loop_id.value) {
        loop_ok = true;
        break;
      }
    }
  }
  if (!loop_ok) {
    return detail::failed_result<std::vector<Point2>>(
        *state_, StatusCode::InvalidInput, diag_codes::kCoreInvalidHandle,
        "面环 UV 折线查询失败：环不属于该面", "面环 UV 折线查询失败",
        std::vector<std::uint64_t>{face_id.value, loop_id.value});
  }
  const auto loop_it = state_->loops.find(loop_id.value);
  if (loop_it == state_->loops.end() || loop_it->second.coedges.empty()) {
    return error_result<std::vector<Point2>>(
        StatusCode::InvalidTopology,
        detail::error_diag(
            *state_, "面环 UV 折线查询失败", diag_codes::kTopoFaceOuterLoopInvalid,
            "面环 UV 折线查询失败：环不存在或为空",
            std::vector<std::uint64_t>{face_id.value, loop_id.value}));
  }
  std::vector<Point2> out;
  out.reserve(32);
  for (const auto coedge_id : loop_it->second.coedges) {
    const auto ce_it = state_->coedges.find(coedge_id.value);
    if (ce_it == state_->coedges.end()) {
      return error_result<std::vector<Point2>>(
          StatusCode::InvalidTopology,
          detail::error_diag(
              *state_, "面环 UV 折线查询失败", diag_codes::kTopoFaceOuterLoopInvalid,
              "面环 UV 折线查询失败：共边记录缺失",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value}));
    }
    const PCurveId pc_id = ce_it->second.pcurve_id;
    if (pc_id.value == 0 || !detail::has_pcurve(*state_, pc_id)) {
      return error_result<std::vector<Point2>>(
          StatusCode::InvalidTopology,
          detail::error_diag(
              *state_, "面环 UV 折线查询失败", diag_codes::kTopoRelationInconsistent,
              "面环 UV 折线查询失败：共边未绑定有效 PCurve",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value, pc_id.value}));
    }
    const auto &pc = state_->pcurves.at(pc_id.value);
    if (pc.kind != detail::PCurveKind::Polyline || pc.poles.empty()) {
      return error_result<std::vector<Point2>>(
          StatusCode::NotImplemented,
          detail::error_diag(
              *state_, "面环 UV 折线查询失败", diag_codes::kCoreOperationUnsupported,
              "面环 UV 折线查询失败：暂仅支持折线型 PCurve",
              std::vector<std::uint64_t>{face_id.value, coedge_id.value, pc_id.value}));
    }
    for (const auto &q : pc.poles) {
      if (!std::isfinite(q.x) || !std::isfinite(q.y)) {
        return error_result<std::vector<Point2>>(
            StatusCode::DegenerateGeometry,
            detail::error_diag(
                *state_, "面环 UV 折线查询失败", diag_codes::kGeoDegenerateGeometry,
                "面环 UV 折线查询失败：PCurve 顶点含非法数值",
                std::vector<std::uint64_t>{face_id.value, pc_id.value}));
      }
    }
    append_coedge_polyline_to_uv_path(pc, ce_it->second.reversed, out);
  }
  if (out.size() >= 2) {
    const auto &a = out.front();
    const auto &b = out.back();
    if (std::hypot(a.x - b.x, a.y - b.y) < 1e-12) {
      out.pop_back();
    }
  }
  if (out.size() < 3) {
    return error_result<std::vector<Point2>>(
        StatusCode::DegenerateGeometry,
        detail::error_diag(
            *state_, "面环 UV 折线查询失败", diag_codes::kGeoDegenerateGeometry,
            "面环 UV 折线查询失败：有效顶点不足 3 个",
            std::vector<std::uint64_t>{face_id.value, loop_id.value}));
  }
  return ok_result(std::move(out), state_->create_diagnostic("已串联面环 UV 折线"));
}

Result<std::vector<Point2>>
TopologyQueryService::face_outer_loop_uv_polyline(FaceId face_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  if (!detail::has_face(*state_, face_id)) {
    return detail::invalid_input_result<std::vector<Point2>>(
        *state_, diag_codes::kCoreInvalidHandle,
        "面外环 UV 折线查询失败：目标面不存在", "面外环 UV 折线查询失败");
  }
  const auto &face = state_->faces.at(face_id.value);
  return face_loop_uv_polyline(face_id, face.outer_loop);
}

Result<std::uint64_t>
TopologyQueryService::query_operation_count() const {
  return ok_result(
      state_->topology_query_op_count,
      state_->create_diagnostic("已查询拓扑只读操作累计次数"));
}
