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
  if (!append_face_bbox(*state_, face_id, bbox) || !bbox.is_valid) {
    return detail::failed_result<BoundingBox>(
        *state_, StatusCode::InvalidTopology,
        diag_codes::kTopoFaceOuterLoopInvalid, "面包围盒查询失败：面拓扑不完整",
        "面包围盒查询失败");
  }
  return ok_result(bbox, state_->create_diagnostic("已查询面包围盒"));
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
  const auto shell_it = state_->shells.find(shell_id.value);
  if (shell_it == state_->shells.end()) {
    return detail::invalid_input_result<MassProperties>(
        *state_, diag_codes::kCoreInvalidHandle,
        "壳质量属性查询失败：目标壳不存在", "壳质量属性查询失败");
  }
  if (shell_it->second.faces.empty() ||
      !detail::validate_closed_shell_edge_usage_for_materialization(*state_, shell_id)) {
    return detail::failed_result<MassProperties>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "壳质量属性查询失败：壳为空、未闭合或存在非流形边", "壳质量属性查询失败",
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
      return detail::failed_result<MassProperties>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "壳质量属性查询失败：壳包含重复面引用", "壳质量属性查询失败",
          {shell_id.value, face_id.value});
    }
    const auto face_it = state_->faces.find(face_id.value);
    if (face_it == state_->faces.end()) {
      return detail::failed_result<MassProperties>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "壳质量属性查询失败：壳引用的面不存在", "壳质量属性查询失败",
          {shell_id.value, face_id.value});
    }
    const auto surface_it = state_->surfaces.find(face_it->second.surface_id.value);
    if (surface_it == state_->surfaces.end()) {
      return detail::failed_result<MassProperties>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
          "壳质量属性查询失败：面引用的曲面不存在", "壳质量属性查询失败",
          {shell_id.value, face_id.value, face_it->second.surface_id.value});
    }
    if (surface_it->second.kind != detail::SurfaceKind::Plane) {
      return detail::failed_result<MassProperties>(
          *state_, StatusCode::NotImplemented, diag_codes::kCoreOperationUnsupported,
          "壳质量属性查询暂仅支持平面、直线边闭壳", "壳质量属性查询失败",
          {shell_id.value, face_id.value});
    }
    const auto normal_length = detail::norm(surface_it->second.normal);
    if (!(normal_length > 0.0) || !std::isfinite(normal_length)) {
      return detail::failed_result<MassProperties>(
          *state_, StatusCode::DegenerateGeometry, diag_codes::kGeoDegenerateGeometry,
          "壳质量属性查询失败：平面法向退化", "壳质量属性查询失败",
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
        return detail::failed_result<MassProperties>(
            *state_, StatusCode::InvalidTopology,
            loop_index == 0 ? diag_codes::kTopoFaceOuterLoopInvalid
                            : diag_codes::kTopoFaceInnerLoopInvalid,
            "壳质量属性查询失败：面边界环无效" +
                (reason.empty() ? std::string{} : "（" + reason + "）"),
            "壳质量属性查询失败", {shell_id.value, face_id.value, loop_id.value});
      }
      for (const auto coedge_id : loop_it->second.coedges) {
        const auto coedge_it = state_->coedges.find(coedge_id.value);
        if (coedge_it == state_->coedges.end()) {
          return detail::failed_result<MassProperties>(
              *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
              "壳质量属性查询失败：边界环引用的定向边不存在", "壳质量属性查询失败",
              {shell_id.value, face_id.value, loop_id.value, coedge_id.value});
        }
        const auto edge_id = coedge_it->second.edge_id;
        const auto oriented = oriented_vertices(*state_, coedge_id);
        if (!oriented) {
          return detail::failed_result<MassProperties>(
              *state_, StatusCode::InvalidTopology, diag_codes::kTopoDanglingEdge,
              "壳质量属性查询失败：边界边端点无效", "壳质量属性查询失败",
              {shell_id.value, face_id.value, loop_id.value, edge_id.value});
        }
        const std::array<std::uint64_t, 2> direction{
            (*oriented)[0].value, (*oriented)[1].value};
        const auto [use_it, inserted] = first_edge_use.emplace(edge_id.value, direction);
        if (!inserted &&
            (use_it->second[0] != direction[1] || use_it->second[1] != direction[0])) {
          return detail::failed_result<MassProperties>(
              *state_, StatusCode::InvalidTopology,
              diag_codes::kTopoLoopOrientationMismatch,
              "壳质量属性查询失败：共享边在相邻面中的方向未相反",
              "壳质量属性查询失败", {shell_id.value, face_id.value, edge_id.value});
        }
        if (measured_edges.insert(edge_id.value).second) {
          const auto length = edge_length(edge_id);
          if (!length.value) {
            return error_result<MassProperties>(length.status, length.diagnostic_id);
          }
        }
      }
      std::vector<Point3> ring;
      if (!loop_vertex_chain_3d(*state_, loop_id, ring, reason) || ring.size() < 3) {
        return detail::failed_result<MassProperties>(
            *state_, StatusCode::InvalidTopology,
            loop_index == 0 ? diag_codes::kTopoFaceOuterLoopInvalid
                            : diag_codes::kTopoFaceInnerLoopInvalid,
            "壳质量属性查询失败：无法提取完整边界顶点", "壳质量属性查询失败",
            {shell_id.value, face_id.value, loop_id.value});
      }
      const auto raw_normal = newell_normal_unnormalized(ring);
      const auto ring_normal_length = detail::norm(raw_normal);
      if (!(ring_normal_length > linear_tol * linear_tol) ||
          !std::isfinite(ring_normal_length)) {
        return detail::failed_result<MassProperties>(
            *state_, StatusCode::DegenerateGeometry, diag_codes::kGeoDegenerateGeometry,
            "壳质量属性查询失败：面边界环面积退化", "壳质量属性查询失败",
            {shell_id.value, face_id.value, loop_id.value});
      }
      const Scalar alignment = detail::dot(raw_normal, normal);
      if ((loop_index == 0 && !(alignment > 0.0)) ||
          (loop_index != 0 && !(alignment < 0.0))) {
        return detail::failed_result<MassProperties>(
            *state_, StatusCode::InvalidTopology, diag_codes::kTopoLoopOrientationMismatch,
            "壳质量属性查询失败：外环/内环绕向与平面法向不一致", "壳质量属性查询失败",
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
            face_triangles) || face_triangles.empty()) {
      return detail::failed_result<MassProperties>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoFaceOuterLoopInvalid,
          "壳质量属性查询失败：面区域自交、孔无效、非共面或无法三角化",
          "壳质量属性查询失败", {shell_id.value, face_id.value});
    }
    if (!reference.has_value()) reference = face_points.front();
    if (positions.size() + face_points.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      return detail::invalid_input_result<MassProperties>(
          *state_, diag_codes::kCoreParameterOutOfRange,
          "壳质量属性查询失败：拓扑规模超出可处理范围", "壳质量属性查询失败");
    }
    const int base = static_cast<int>(positions.size());
    for (const auto &point : face_points) {
      const auto local = detail::subtract(point, *reference);
      if (!std::isfinite(local.x) || !std::isfinite(local.y) ||
          !std::isfinite(local.z)) {
        return detail::invalid_input_result<MassProperties>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "壳质量属性查询失败：顶点坐标非有限或超出数值范围", "壳质量属性查询失败");
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
    return detail::failed_result<MassProperties>(
        *state_, StatusCode::DegenerateGeometry, diag_codes::kGeoDegenerateGeometry,
        "壳质量属性查询失败：闭壳有向体积退化", "壳质量属性查询失败",
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
        !std::isfinite(properties.volume) || !std::isfinite(properties.area) ||
        !std::isfinite(properties.centroid.x) ||
        !std::isfinite(properties.centroid.y) ||
        !std::isfinite(properties.centroid.z)) return false;
    return std::all_of(properties.inertia.begin(), properties.inertia.end(),
                       [](Scalar value) { return std::isfinite(value); });
  };
  if (!finite_properties()) {
    return detail::failed_result<MassProperties>(
        *state_, StatusCode::NumericalInstability,
        diag_codes::kQueryMassPropertiesFailure,
        "壳质量属性查询失败：积分结果非有限或不满足正体积", "壳质量属性查询失败",
        {shell_id.value});
  }
  return ok_result(properties,
                   state_->create_diagnostic("已从闭合多面体壳查询质量属性"));
}

Result<MassProperties>
TopologyQueryService::body_mass_properties(BodyId body_id) const {
  TopoQueryAuditScope _topo_query_audit{state_};
  const auto body_it = state_->bodies.find(body_id.value);
  if (body_it == state_->bodies.end()) {
    return detail::invalid_input_result<MassProperties>(
        *state_, diag_codes::kCoreInvalidHandle,
        "体拓扑质量属性查询失败：目标体不存在", "体拓扑质量属性查询失败");
  }
  if (body_it->second.shells.empty()) {
    return detail::failed_result<MassProperties>(
        *state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
        "体拓扑质量属性查询失败：实体不拥有可计算的拓扑壳",
        "体拓扑质量属性查询失败", {body_id.value});
  }

  std::vector<MassProperties> components;
  components.reserve(body_it->second.shells.size());
  std::unordered_set<std::uint64_t> unique_shells;
  long double total_volume = 0.0L;
  long double total_area = 0.0L;
  long double first_x = 0.0L, first_y = 0.0L, first_z = 0.0L;
  for (const auto shell_id : body_it->second.shells) {
    if (!unique_shells.insert(shell_id.value).second) {
      return detail::failed_result<MassProperties>(
          *state_, StatusCode::InvalidTopology, diag_codes::kTopoInvariantBroken,
          "体拓扑质量属性查询失败：实体包含重复壳引用",
          "体拓扑质量属性查询失败", {body_id.value, shell_id.value});
    }
    const auto component = shell_mass_properties(shell_id);
    if (!component.value)
      return error_result<MassProperties>(component.status, component.diagnostic_id);
    components.push_back(*component.value);
    total_volume += component.value->volume;
    total_area += component.value->area;
    first_x += static_cast<long double>(component.value->volume) *
               component.value->centroid.x;
    first_y += static_cast<long double>(component.value->volume) *
               component.value->centroid.y;
    first_z += static_cast<long double>(component.value->volume) *
               component.value->centroid.z;
  }
  if (!(total_volume > 0.0L) || !std::isfinite(total_volume) ||
      !std::isfinite(total_area) ||
      total_volume > std::numeric_limits<Scalar>::max() ||
      total_area > std::numeric_limits<Scalar>::max()) {
    return detail::failed_result<MassProperties>(
        *state_, StatusCode::NumericalInstability,
        diag_codes::kQueryMassPropertiesFailure,
        "体拓扑质量属性查询失败：多壳汇总超出数值范围",
        "体拓扑质量属性查询失败", {body_id.value});
  }

  MassProperties properties{};
  properties.volume = static_cast<Scalar>(total_volume);
  properties.area = static_cast<Scalar>(total_area);
  properties.centroid = Point3{static_cast<Scalar>(first_x / total_volume),
                               static_cast<Scalar>(first_y / total_volume),
                               static_cast<Scalar>(first_z / total_volume)};
  for (const auto &component : components) {
    for (std::size_t i = 0; i < properties.inertia.size(); ++i)
      properties.inertia[i] += component.inertia[i];
    const long double dx =
        static_cast<long double>(component.centroid.x) - properties.centroid.x;
    const long double dy =
        static_cast<long double>(component.centroid.y) - properties.centroid.y;
    const long double dz =
        static_cast<long double>(component.centroid.z) - properties.centroid.z;
    const long double mass = component.volume;
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
  if (!std::isfinite(properties.centroid.x) ||
      !std::isfinite(properties.centroid.y) ||
      !std::isfinite(properties.centroid.z) ||
      !std::all_of(properties.inertia.begin(), properties.inertia.end(),
                   [](Scalar value) { return std::isfinite(value); })) {
    return detail::failed_result<MassProperties>(
        *state_, StatusCode::NumericalInstability,
        diag_codes::kQueryMassPropertiesFailure,
        "体拓扑质量属性查询失败：质心或惯性张量非有限",
        "体拓扑质量属性查询失败", {body_id.value});
  }
  return ok_result(properties,
                   state_->create_diagnostic("已从实体闭合拓扑查询质量属性"));
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
    if (!append_face_bbox(*state_, face_id, bbox)) {
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
  const auto bbox = compute_body_bbox(*state_, body_it->second.shells);
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
