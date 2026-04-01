bool is_primitive_body_kind(detail::BodyKind kind) {
    switch (kind) {
        case detail::BodyKind::Box:
        case detail::BodyKind::Sphere:
        case detail::BodyKind::Cylinder:
        case detail::BodyKind::Cone:
        case detail::BodyKind::Torus:
        case detail::BodyKind::Wedge:
            return true;
        default:
            return false;
    }
}

void clear_primitive_provenance_fields(detail::BodyRecord& record) {
    record.source_bodies.clear();
    record.source_shells.clear();
    record.source_faces.clear();
}


BodyId make_body(std::shared_ptr<detail::KernelState> state, detail::BodyRecord record, const std::string&) {
    const bool is_derived = record.kind == detail::BodyKind::BooleanResult || record.kind == detail::BodyKind::Modified ||
                           record.kind == detail::BodyKind::BlendResult || record.kind == detail::BodyKind::Sweep;
    if (is_derived) {
        detail::materialize_body_bbox_topology(*state, record);
    } else if (is_primitive_body_kind(record.kind) && record.rep_kind == RepKind::ExactBRep && record.bbox.is_valid &&
               record.shells.empty()) {
        clear_primitive_provenance_fields(record);
        if (record.kind == detail::BodyKind::Wedge) {
            detail::materialize_body_wedge_shell(*state, record);
        } else if (record.kind == detail::BodyKind::Cylinder) {
            detail::materialize_body_prism_cylinder_shell(*state, record);
        } else {
            detail::materialize_body_bbox_shell(*state, record);
        }
    }
    const auto id = BodyId {state->allocate_id()};
    state->bodies.emplace(id.value, std::move(record));
    detail::rebuild_topology_links(*state);
    return id;
}

void append_unique_body(std::vector<BodyId>& ids, BodyId id) {
    if (std::none_of(ids.begin(), ids.end(), [id](BodyId current) { return current.value == id.value; })) {
        ids.push_back(id);
    }
}

void append_unique_face(std::vector<FaceId>& ids, FaceId id) {
    if (std::none_of(ids.begin(), ids.end(), [id](FaceId current) { return current.value == id.value; })) {
        ids.push_back(id);
    }
}

void append_unique_shell(std::vector<ShellId>& ids, ShellId id) {
    if (std::none_of(ids.begin(), ids.end(), [id](ShellId current) { return current.value == id.value; })) {
        ids.push_back(id);
    }
}

void append_shells_for_face(const detail::KernelState& state, std::vector<ShellId>& ids, FaceId face_id) {
    const auto shell_it = state.face_to_shells.find(face_id.value);
    if (shell_it == state.face_to_shells.end()) {
        return;
    }
    for (const auto shell_value : shell_it->second) {
        append_unique_shell(ids, ShellId {shell_value});
    }
}

// When a Face is referenced by multiple shells, provenance for modify operations
// should prefer shells owned by the source body.
void append_shells_for_face_owned_by_body(const detail::KernelState& state, std::vector<ShellId>& ids,
                                         FaceId face_id, BodyId body_id) {
    const auto body_it = state.bodies.find(body_id.value);
    if (body_it == state.bodies.end()) {
        return;
    }
    for (const auto shell_id : body_it->second.shells) {
        const auto sh_it = state.shells.find(shell_id.value);
        if (sh_it == state.shells.end()) {
            continue;
        }
        const auto& faces = sh_it->second.faces;
        if (std::any_of(faces.begin(), faces.end(),
                        [face_id](FaceId f) { return f.value == face_id.value; })) {
            append_unique_shell(ids, shell_id);
        }
    }
}

void append_shell_provenance_for_body(const detail::KernelState& state, std::vector<ShellId>& ids, BodyId body_id) {
    const auto body_it = state.bodies.find(body_id.value);
    if (body_it == state.bodies.end()) {
        return;
    }
    if (!body_it->second.source_shells.empty()) {
        for (const auto shell_id : body_it->second.source_shells) {
            append_unique_shell(ids, shell_id);
        }
        return;
    }
    for (const auto shell_id : body_it->second.shells) {
        append_unique_shell(ids, shell_id);
    }
}

void append_face_provenance_for_body(const detail::KernelState& state, std::vector<FaceId>& ids, BodyId body_id) {
    const auto body_it = state.bodies.find(body_id.value);
    if (body_it == state.bodies.end()) {
        return;
    }
    for (const auto face_id : body_it->second.source_faces) {
        append_unique_face(ids, face_id);
    }
}

void detach_owned_topology(const detail::KernelState& state, detail::BodyRecord& record) {
    if (!is_primitive_body_kind(record.kind)) {
        detail::inherit_source_topology_from_owned_shells(state, record);
    }
    record.shells.clear();
}

void append_faces_for_edge(const detail::KernelState& state, std::vector<FaceId>& ids, EdgeId edge_id) {
    const auto coedge_it = state.edge_to_coedges.find(edge_id.value);
    if (coedge_it == state.edge_to_coedges.end()) {
        return;
    }
    for (const auto coedge_value : coedge_it->second) {
        const auto loop_it = state.coedge_to_loop.find(coedge_value);
        if (loop_it == state.coedge_to_loop.end()) {
            continue;
        }
        const auto face_it = state.loop_to_faces.find(loop_it->second);
        if (face_it == state.loop_to_faces.end()) {
            continue;
        }
        for (const auto face_value : face_it->second) {
            append_unique_face(ids, FaceId {face_value});
        }
    }
}

bool same_unordered_face_ids(const std::vector<FaceId>& a, const std::vector<FaceId>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    std::vector<std::uint64_t> av;
    std::vector<std::uint64_t> bv;
    av.reserve(a.size());
    bv.reserve(b.size());
    for (const auto f : a) {
        av.push_back(f.value);
    }
    for (const auto f : b) {
        bv.push_back(f.value);
    }
    std::sort(av.begin(), av.end());
    std::sort(bv.begin(), bv.end());
    return av == bv;
}

OpReport make_report(StatusCode status, BodyId output, DiagnosticId diagnostic_id, std::vector<Warning> warnings) {
    OpReport report;
    report.status = status;
    report.output = output;
    report.diagnostic_id = diagnostic_id;
    report.warnings = std::move(warnings);
    return report;
}

BoundingBox box_bbox(const Point3& origin, Scalar dx, Scalar dy, Scalar dz) {
    return detail::make_bbox(origin, Point3 {origin.x + dx, origin.y + dy, origin.z + dz});
}

BoundingBox cylinder_bbox(const Point3& center, const Vec3& axis_unit, Scalar radius, Scalar height) {
    const auto hx = std::abs(axis_unit.x) * height * 0.5;
    const auto hy = std::abs(axis_unit.y) * height * 0.5;
    const auto hz = std::abs(axis_unit.z) * height * 0.5;
    return detail::make_bbox(
        {center.x - radius - hx, center.y - radius - hy, center.z - radius - hz},
        {center.x + radius + hx, center.y + radius + hy, center.z + radius + hz});
}

BoundingBox cone_bbox(const Point3& apex, const Vec3& axis_unit, Scalar semi_angle, Scalar height) {
    const auto radius = std::max<Scalar>(0.0, height * std::tan(semi_angle));
    const auto mid = detail::add_point_vec(apex, detail::scale(axis_unit, height * 0.5));
    const auto hx = std::abs(axis_unit.x) * height * 0.5;
    const auto hy = std::abs(axis_unit.y) * height * 0.5;
    const auto hz = std::abs(axis_unit.z) * height * 0.5;
    return detail::make_bbox(
        {mid.x - radius - hx, mid.y - radius - hy, mid.z - radius - hz},
        {mid.x + radius + hx, mid.y + radius + hy, mid.z + radius + hz});
}

MassProperties bbox_mass_properties(const BoundingBox& bbox) {
    MassProperties props {};
    if (!bbox.is_valid) {
        return props;
    }

    const auto dx = std::max(0.0, bbox.max.x - bbox.min.x);
    const auto dy = std::max(0.0, bbox.max.y - bbox.min.y);
    const auto dz = std::max(0.0, bbox.max.z - bbox.min.z);

    props.volume = dx * dy * dz;
    props.area = 2.0 * (dx * dy + dy * dz + dx * dz);
    props.centroid = Point3 {
        (bbox.min.x + bbox.max.x) * 0.5,
        (bbox.min.y + bbox.max.y) * 0.5,
        (bbox.min.z + bbox.max.z) * 0.5
    };
    return props;
}

/// 均质密度 rho=1（m 与 volume 数值相同）。惯性张量关于质心，世界坐标系行主序 3x3。
void set_box_inertia_about_centroid(MassProperties& props, Scalar dx, Scalar dy, Scalar dz) {
    const auto m = props.volume;
    if (m <= 0.0 || dx <= 0.0 || dy <= 0.0 || dz <= 0.0) {
        return;
    }
    const auto ixx = m / 12.0 * (dy * dy + dz * dz);
    const auto iyy = m / 12.0 * (dx * dx + dz * dz);
    const auto izz = m / 12.0 * (dx * dx + dy * dy);
    props.inertia = {
        ixx, 0.0, 0.0,
        0.0, iyy, 0.0,
        0.0, 0.0, izz};
}

bool bbox_corners_almost_equal(const BoundingBox& a, const BoundingBox& b, Scalar eps) {
    if (!a.is_valid || !b.is_valid) {
        return false;
    }
    return std::abs(a.min.x - b.min.x) <= eps && std::abs(a.min.y - b.min.y) <= eps &&
           std::abs(a.min.z - b.min.z) <= eps && std::abs(a.max.x - b.max.x) <= eps &&
           std::abs(a.max.y - b.max.y) <= eps && std::abs(a.max.z - b.max.z) <= eps;
}

MassProperties mass_properties_from_box_body_record(const detail::BodyRecord& b) {
    MassProperties props {};
    props.volume = b.a * b.b * b.c;
    props.area = 2.0 * (b.a * b.b + b.b * b.c + b.a * b.c);
    props.centroid =
        Point3 {b.origin.x + b.a * 0.5, b.origin.y + b.b * 0.5, b.origin.z + b.c * 0.5};
    set_box_inertia_about_centroid(props, b.a, b.b, b.c);
    return props;
}

/// 将关于 `cm` 的惯性张量平移到参考点 `ref`（均质 m，世界坐标、行主序 3x3）：I_ref += I_cm + m((d·d)I - d⊗d)，d = cm - ref。
void add_inertia_parallel_axis(std::array<Scalar, 9>& I, Scalar m, const Point3& cm, const Point3& ref) {
    if (m <= 0.0) {
        return;
    }
    const Scalar dx = cm.x - ref.x;
    const Scalar dy = cm.y - ref.y;
    const Scalar dz = cm.z - ref.z;
    const Scalar r2 = dx * dx + dy * dy + dz * dz;
    I[0] += m * (r2 - dx * dx);
    I[1] += m * (-dx * dy);
    I[2] += m * (-dx * dz);
    I[3] += m * (-dx * dy);
    I[4] += m * (r2 - dy * dy);
    I[5] += m * (-dy * dz);
    I[6] += m * (-dx * dz);
    I[7] += m * (-dy * dz);
    I[8] += m * (r2 - dz * dz);
}

/// 直角三棱柱：XY 内直角三角形 (0,0),(+dx,0),(0,+dy)，沿 +Z 拉伸 dz；均质 rho=1，张量关于质心，与世界轴对齐。
void set_wedge_inertia_about_centroid(MassProperties& props, Scalar dx, Scalar dy, Scalar dz) {
    const auto m = props.volume;
    if (m <= 0.0 || dx <= 0.0 || dy <= 0.0 || dz <= 0.0) {
        return;
    }
    const auto dx2 = dx * dx;
    const auto dy2 = dy * dy;
    const auto dz2 = dz * dz;
    const auto i_o_xx = m * dy2 / 6.0 + m * dz2 / 3.0;
    const auto i_o_yy = m * dx2 / 6.0 + m * dz2 / 3.0;
    const auto i_o_zz = m * (dx2 + dy2) / 6.0;
    const auto i_o_xy = -m * dx * dy / 12.0;
    const auto i_o_xz = -m * dx * dz / 6.0;
    const auto i_o_yz = -m * dy * dz / 6.0;
    const auto cx = dx / 3.0;
    const auto cy = dy / 3.0;
    const auto cz = dz * 0.5;
    const auto i_c_xx = i_o_xx - m * (cy * cy + cz * cz);
    const auto i_c_yy = i_o_yy - m * (cx * cx + cz * cz);
    const auto i_c_zz = i_o_zz - m * (cx * cx + cy * cy);
    const auto i_c_xy = i_o_xy + m * cx * cy;
    const auto i_c_xz = i_o_xz + m * cx * cz;
    const auto i_c_yz = i_o_yz + m * cy * cz;
    props.inertia = {
        i_c_xx, i_c_xy, i_c_xz,
        i_c_xy, i_c_yy, i_c_yz,
        i_c_xz, i_c_yz, i_c_zz};
}

void set_sphere_inertia_about_center(MassProperties& props, Scalar radius) {
    const auto m = props.volume;
    if (m <= 0.0 || radius <= 0.0) {
        return;
    }
    const auto ii = 2.0 / 5.0 * m * radius * radius;
    props.inertia = {ii, 0.0, 0.0, 0.0, ii, 0.0, 0.0, 0.0, ii};
}

/// 回转体：局部惯性 diag(`i_transverse`,`i_transverse`,`i_axial`)，局部 z 与 `axis`（单位向量）对齐。
void set_axisymmetric_inertia_world(MassProperties& props, const Vec3& axis, Scalar i_transverse, Scalar i_axial) {
    Vec3 t {1.0, 0.0, 0.0};
    if (std::abs(axis.x) > 0.85) {
        t = {0.0, 1.0, 0.0};
    }
    auto v = detail::cross(t, axis);
    const auto vn = detail::norm(v);
    if (vn <= 1e-24) {
        return;
    }
    v = detail::scale(v, 1.0 / vn);
    const auto w = detail::cross(axis, v);
    const std::array<Scalar, 3> e0 {v.x, v.y, v.z};
    const std::array<Scalar, 3> e1 {w.x, w.y, w.z};
    const std::array<Scalar, 3> e2 {axis.x, axis.y, axis.z};
    const std::array<Scalar, 3> diag {i_transverse, i_transverse, i_axial};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            Scalar s = 0.0;
            for (int k = 0; k < 3; ++k) {
                const std::array<Scalar, 3>* ek = (k == 0) ? &e0 : (k == 1) ? &e1 : &e2;
                s += diag[static_cast<std::size_t>(k)] * (*ek)[static_cast<std::size_t>(i)] * (*ek)[static_cast<std::size_t>(j)];
            }
            props.inertia[static_cast<std::size_t>(i * 3 + j)] = s;
        }
    }
}

void set_cylinder_inertia_about_center(MassProperties& props, const Vec3& axis, Scalar radius, Scalar height) {
    const auto m = props.volume;
    if (m <= 0.0 || radius <= 0.0 || height <= 0.0) {
        return;
    }
    const auto r2 = radius * radius;
    const auto h2 = height * height;
    const auto i_axial = 0.5 * m * r2;
    const auto i_perp = m * (3.0 * r2 + h2) / 12.0;
    set_axisymmetric_inertia_world(props, axis, i_perp, i_axial);
}

/// 实心圆锥关于质心：对称轴与 `axis`（单位，由顶点指向底）一致；`semi_angle` 为半角，`height` 为沿轴高度。
void set_cone_inertia_about_centroid(MassProperties& props, const Vec3& axis, Scalar semi_angle, Scalar height) {
    const auto m = props.volume;
    if (m <= 0.0 || height <= 0.0) {
        return;
    }
    const auto radius = height * std::tan(semi_angle);
    const auto r2 = radius * radius;
    const auto h2 = height * height;
    const auto i_axial = 0.3 * m * r2;
    const auto i_perp = m * (0.15 * r2 + 0.0125 * h2);
    set_axisymmetric_inertia_world(props, axis, i_perp, i_axial);
}

/// 实心环体：主半径 `major_r`（管中心到回转轴）、副半径 `minor_r`（截面圆半径），对称轴为 `axis`（单位向量）。
void set_torus_inertia_about_center(MassProperties& props, const Vec3& axis, Scalar major_r, Scalar minor_r) {
    const auto m = props.volume;
    if (m <= 0.0 || major_r <= 0.0 || minor_r <= 0.0) {
        return;
    }
    const auto R = major_r;
    const auto r = minor_r;
    const auto i_axial = m * (R * R + 0.75 * r * r);
    const auto i_transverse = m * (0.5 * R * R + 0.625 * r * r);
    set_axisymmetric_inertia_world(props, axis, i_transverse, i_axial);
}

/// `Box`/`Wedge`/`Sphere`/`Cylinder`/`Cone`/`Torus` 的解析质量属性（ρ=1）；失败时返回 false（调用方回退 bbox）。
bool try_primitive_analytic_mass_properties(const detail::BodyRecord& body, MassProperties& props) {
    props = MassProperties {};
    switch (body.kind) {
        case detail::BodyKind::Box:
            if (body.a <= 0.0 || body.b <= 0.0 || body.c <= 0.0) {
                return false;
            }
            props = mass_properties_from_box_body_record(body);
            return true;
        case detail::BodyKind::Wedge: {
            const auto dx = body.a;
            const auto dy = body.b;
            const auto dz = body.c;
            if (dx <= 0.0 || dy <= 0.0 || dz <= 0.0) {
                return false;
            }
            props.volume = 0.5 * dx * dy * dz;
            const auto hyp = std::sqrt(dx * dx + dy * dy);
            props.area = dx * dy + dx * dz + dy * dz + hyp * dz;
            props.centroid =
                Point3 {body.origin.x + dx / 3.0, body.origin.y + dy / 3.0, body.origin.z + dz * 0.5};
            set_wedge_inertia_about_centroid(props, dx, dy, dz);
            return true;
        }
        case detail::BodyKind::Sphere:
            if (body.a <= 0.0) {
                return false;
            }
            props.volume = 4.0 / 3.0 * kPi * body.a * body.a * body.a;
            props.area = 4.0 * kPi * body.a * body.a;
            props.centroid = body.origin;
            set_sphere_inertia_about_center(props, body.a);
            return true;
        case detail::BodyKind::Cylinder:
            if (body.a <= 0.0 || body.b <= 0.0) {
                return false;
            }
            props.volume = kPi * body.a * body.a * body.b;
            props.area = 2.0 * kPi * body.a * (body.b + body.a);
            props.centroid = body.origin;
            set_cylinder_inertia_about_center(props, body.axis, body.a, body.b);
            return true;
        case detail::BodyKind::Cone: {
            if (body.b <= 0.0) {
                return false;
            }
            const auto radius = body.b * std::tan(body.a);
            if (!(radius > 0.0)) {
                return false;
            }
            const auto slant = std::sqrt(radius * radius + body.b * body.b);
            props.volume = kPi * radius * radius * body.b / 3.0;
            props.area = kPi * radius * (radius + slant);
            props.centroid = detail::add_point_vec(body.origin, detail::scale(body.axis, body.b * 0.75));
            set_cone_inertia_about_centroid(props, body.axis, body.a, body.b);
            return true;
        }
        case detail::BodyKind::Torus:
            if (body.a <= 0.0 || body.b <= 0.0) {
                return false;
            }
            props.volume = 2.0 * kPi * kPi * body.a * body.b * body.b;
            props.area = 4.0 * kPi * kPi * body.a * body.b;
            props.centroid = body.origin;
            set_torus_inertia_about_center(props, body.axis, body.a, body.b);
            return true;
        default:
            return false;
    }
}

Scalar interval_gap(Scalar a_min, Scalar a_max, Scalar b_min, Scalar b_max) {
    if (a_max < b_min) {
        return b_min - a_max;
    }
    if (b_max < a_min) {
        return a_min - b_max;
    }
    return 0.0;
}

bool contains_bbox(const BoundingBox& outer, const BoundingBox& inner) {
    return outer.is_valid && inner.is_valid &&
           outer.min.x <= inner.min.x && outer.max.x >= inner.max.x &&
           outer.min.y <= inner.min.y && outer.max.y >= inner.max.y &&
           outer.min.z <= inner.min.z && outer.max.z >= inner.max.z;
}

Scalar overlap_extent(Scalar a_min, Scalar a_max, Scalar b_min, Scalar b_max) {
    return std::min(a_max, b_max) - std::max(a_min, b_min);
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

BoundingBox curve_bbox_for_query(const detail::CurveRecord& curve) {
    switch (curve.kind) {
        case detail::CurveKind::Line:
            return detail::bbox_from_center_radius(curve.origin, 1000.0, 1000.0, 1000.0);
        case detail::CurveKind::LineSegment: {
            if (curve.poles.size() >= 2) {
                const auto& a = curve.poles[0];
                const auto& b = curve.poles[1];
                return detail::make_bbox(
                    {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)},
                    {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)});
            }
            return detail::bbox_from_center_radius(curve.origin, 10.0, 10.0, 10.0);
        }
        case detail::CurveKind::Circle:
            return detail::bbox_from_center_radius(curve.origin, curve.radius, curve.radius, curve.radius);
        case detail::CurveKind::Ellipse:
            return detail::bbox_from_center_radius(curve.origin, detail::norm(curve.axis_u), detail::norm(curve.axis_v), 1.0);
        case detail::CurveKind::Parabola:
        case detail::CurveKind::Hyperbola:
        case detail::CurveKind::CompositePolyline:
            if (!curve.poles.empty()) {
                auto min = curve.poles.front();
                auto max = curve.poles.front();
                for (const auto& p : curve.poles) {
                    min.x = std::min(min.x, p.x);
                    min.y = std::min(min.y, p.y);
                    min.z = std::min(min.z, p.z);
                    max.x = std::max(max.x, p.x);
                    max.y = std::max(max.y, p.y);
                    max.z = std::max(max.z, p.z);
                }
                return detail::make_bbox(min, max);
            }
            return detail::bbox_from_center_radius(curve.origin, 10.0, 10.0, 10.0);
        default:
            return detail::bbox_from_center_radius(curve.origin, 10.0, 10.0, 10.0);
    }
}

BoundingBox surface_bbox_for_query(const detail::SurfaceRecord& surface) {
    switch (surface.kind) {
        case detail::SurfaceKind::Plane:
            return detail::bbox_from_center_radius(surface.origin, 1000.0, 1000.0, 0.0);
        case detail::SurfaceKind::Sphere:
            return detail::bbox_from_center_radius(surface.origin, surface.radius_a, surface.radius_a, surface.radius_a);
        case detail::SurfaceKind::Cylinder:
            return detail::bbox_from_center_radius(surface.origin, surface.radius_a, surface.radius_a, 1000.0);
        case detail::SurfaceKind::Cone:
            return detail::bbox_from_center_radius(surface.origin, 1000.0, 1000.0, 1000.0);
        case detail::SurfaceKind::Torus:
            return detail::bbox_from_center_radius(surface.origin, surface.radius_a + surface.radius_b,
                                                   surface.radius_a + surface.radius_b, surface.radius_b);
        case detail::SurfaceKind::Bezier:
        case detail::SurfaceKind::BSpline:
        case detail::SurfaceKind::Nurbs:
            if (!surface.poles.empty()) {
                auto min = surface.poles.front();
                auto max = surface.poles.front();
                for (const auto& p : surface.poles) {
                    min.x = std::min(min.x, p.x);
                    min.y = std::min(min.y, p.y);
                    min.z = std::min(min.z, p.z);
                    max.x = std::max(max.x, p.x);
                    max.y = std::max(max.y, p.y);
                    max.z = std::max(max.z, p.z);
                }
                return detail::make_bbox(min, max);
            }
            return detail::bbox_from_center_radius(surface.origin, 10.0, 10.0, 10.0);
        default:
            return detail::bbox_from_center_radius(surface.origin, 10.0, 10.0, 10.0);
    }
}

Scalar plane_signed_distance(const Plane& plane, const Point3& point) {
    const auto dx = point.x - plane.origin.x;
    const auto dy = point.y - plane.origin.y;
    const auto dz = point.z - plane.origin.z;
    return dx * plane.normal.x + dy * plane.normal.y + dz * plane.normal.z;
}

bool intersects_plane(const BoundingBox& bbox, const Plane& plane) {
    if (!bbox.is_valid) {
        return false;
    }

    bool has_positive = false;
    bool has_negative = false;
    for (const auto& corner : bbox_corners(bbox)) {
        const auto distance = plane_signed_distance(plane, corner);
        has_positive = has_positive || distance >= 0.0;
        has_negative = has_negative || distance <= 0.0;
    }
    return has_positive && has_negative;
}

BoundingBox union_bbox(const BoundingBox& lhs, const BoundingBox& rhs) {
    return detail::make_bbox(
        {std::min(lhs.min.x, rhs.min.x), std::min(lhs.min.y, rhs.min.y), std::min(lhs.min.z, rhs.min.z)},
        {std::max(lhs.max.x, rhs.max.x), std::max(lhs.max.y, rhs.max.y), std::max(lhs.max.z, rhs.max.z)});
}

BoundingBox intersect_bbox(const BoundingBox& lhs, const BoundingBox& rhs) {
    const Point3 min {
        std::max(lhs.min.x, rhs.min.x),
        std::max(lhs.min.y, rhs.min.y),
        std::max(lhs.min.z, rhs.min.z),
    };
    const Point3 max {
        std::min(lhs.max.x, rhs.max.x),
        std::min(lhs.max.y, rhs.max.y),
        std::min(lhs.max.z, rhs.max.z),
    };
    if (min.x > max.x || min.y > max.y || min.z > max.z) {
        return {};
    }
    return detail::make_bbox(min, max);
}

BBoxRelation classify_bbox_relation(const BoundingBox& lhs, const BoundingBox& rhs) {
    const auto ix = overlap_extent(lhs.min.x, lhs.max.x, rhs.min.x, rhs.max.x);
    const auto iy = overlap_extent(lhs.min.y, lhs.max.y, rhs.min.y, rhs.max.y);
    const auto iz = overlap_extent(lhs.min.z, lhs.max.z, rhs.min.z, rhs.max.z);
    if (ix < 0.0 || iy < 0.0 || iz < 0.0) {
        return BBoxRelation::Disjoint;
    }
    if (contains_bbox(lhs, rhs)) {
        return BBoxRelation::LhsContainsRhs;
    }
    if (contains_bbox(rhs, lhs)) {
        return BBoxRelation::RhsContainsLhs;
    }
    if (ix == 0.0 || iy == 0.0 || iz == 0.0) {
        return BBoxRelation::Touching;
    }
    return BBoxRelation::Overlapping;
}

Scalar bbox_min_extent(const BoundingBox& bbox) {
    return std::min({bbox.max.x - bbox.min.x, bbox.max.y - bbox.min.y, bbox.max.z - bbox.min.z});
}

BoundingBox offset_bbox(const BoundingBox& bbox, Scalar distance) {
    return detail::make_bbox(
        {bbox.min.x - distance, bbox.min.y - distance, bbox.min.z - distance},
        {bbox.max.x + distance, bbox.max.y + distance, bbox.max.z + distance});
}

/// 由面顶点 AABB 估计平面片在轴对齐投影上的最大矩形面积（Stage-2 占位；轴对齐矩形面精确）。
Scalar bbox_max_axis_rectangle_area(const BoundingBox& fb) {
    if (!fb.is_valid) {
        return 0.0;
    }
    const Scalar dx = std::max<Scalar>(0.0, fb.max.x - fb.min.x);
    const Scalar dy = std::max<Scalar>(0.0, fb.max.y - fb.min.y);
    const Scalar dz = std::max<Scalar>(0.0, fb.max.z - fb.min.z);
    return std::max({dx * dy, dy * dz, dz * dx});
}

/// 点到无限直线（`origin` + t * `u`，`u` 单位）的最短距离。
Scalar distance_point_to_unit_axis(const Point3& p, const Point3& origin, const Vec3& u) {
    const Vec3 v {p.x - origin.x, p.y - origin.y, p.z - origin.z};
    const auto t = detail::dot(u, v);
    const auto px = v.x - u.x * t;
    const auto py = v.y - u.y * t;
    const auto pz = v.z - u.z * t;
    return std::sqrt(px * px + py * py + pz * pz);
}


std::optional<PappusRevolveMass> try_pappus_revolve_mass(std::span<const Point3> poly, const Point3& O,
                                                         const Vec3& axis_u, Scalar angle) {
    if (poly.size() < 3 || !(angle > 0.0)) {
        return std::nullopt;
    }
    const auto u = detail::normalize(axis_u);
    const auto& v0p = poly.front();
    Vec3 accn {0.0, 0.0, 0.0};
    const auto nv = poly.size();
    for (std::size_t i = 0; i < nv; ++i) {
        const auto& vi = poly[i];
        const auto& vj = poly[(i + 1) % nv];
        const auto ei = detail::subtract(vi, v0p);
        const auto ej = detail::subtract(vj, v0p);
        const auto cr = detail::cross(ei, ej);
        accn = Vec3 {accn.x + cr.x, accn.y + cr.y, accn.z + cr.z};
    }
    const auto acc_len = detail::norm(accn);
    const auto poly_area = 0.5 * acc_len;
    if (poly_area <= 1e-18 || acc_len <= 1e-18) {
        return std::nullopt;
    }
    Point3 csum {0.0, 0.0, 0.0};
    Scalar wsum = 0.0;
    if (nv == 3) {
        const auto e1 = detail::subtract(poly[1], poly[0]);
        const auto e2 = detail::subtract(poly[2], poly[0]);
        const auto cp = detail::cross(e1, e2);
        wsum = 0.5 * detail::norm(cp);
        if (wsum <= 1e-30) {
            return std::nullopt;
        }
        csum.x = (poly[0].x + poly[1].x + poly[2].x) / 3.0;
        csum.y = (poly[0].y + poly[1].y + poly[2].y) / 3.0;
        csum.z = (poly[0].z + poly[1].z + poly[2].z) / 3.0;
    } else {
        for (std::size_t i = 1; i + 1 < static_cast<std::size_t>(nv); ++i) {
            const auto& vi = poly[i];
            const auto& vj = poly[i + 1];
            const auto e1 = detail::subtract(vi, v0p);
            const auto e2 = detail::subtract(vj, v0p);
            const auto cp = detail::cross(e1, e2);
            const auto ta = 0.5 * detail::norm(cp);
            if (ta <= 1e-30) {
                continue;
            }
            csum.x += (v0p.x + vi.x + vj.x) * ta / 3.0;
            csum.y += (v0p.y + vi.y + vj.y) * ta / 3.0;
            csum.z += (v0p.z + vi.z + vj.z) * ta / 3.0;
            wsum += ta;
        }
        if (wsum <= 1e-30) {
            return std::nullopt;
        }
        csum.x /= wsum;
        csum.y /= wsum;
        csum.z /= wsum;
    }
    const Point3 c_poly {csum.x, csum.y, csum.z};
    const auto r_bar = distance_point_to_unit_axis(c_poly, O, u);
    if (r_bar <= 1e-30 || !std::isfinite(r_bar)) {
        return std::nullopt;
    }
    return PappusRevolveMass {poly_area * r_bar * angle, poly_area};
}

/// `BodyKind::Sweep` 的占位/解析质量口径（与 `QueryService::mass_properties` 一致），供布尔操作数等复用。
bool try_sweep_body_mass_properties(const detail::BodyRecord& body, MassProperties& props) {
    if (body.kind != detail::BodyKind::Sweep) {
        return false;
    }
    if (body.label == "thicken" && body.a > 0.0) {
        props.volume = body.a;
        if (body.bbox.is_valid) {
            props.centroid = Point3 {
                (body.bbox.min.x + body.bbox.max.x) * 0.5,
                (body.bbox.min.y + body.bbox.max.y) * 0.5,
                (body.bbox.min.z + body.bbox.max.z) * 0.5};
            const auto bboxp = bbox_mass_properties(body.bbox);
            props.area = bboxp.area;
        }
        return true;
    }
    if (body.label.size() >= 8 && body.label.compare(0, 8, "revolve:") == 0) {
        if (body.sweep_polyhedral_mass_valid) {
            props.volume = body.sweep_polyhedral_volume;
            props.area = body.sweep_cached_surface_area;
            props.centroid = body.sweep_polyhedral_centroid;
            props.inertia = body.sweep_inertia_about_centroid;
            if (body.b > 0.0 && !body.revolve_profile_xyz.empty()) {
                if (const auto pm = try_pappus_revolve_mass(
                        std::span<const Point3>(body.revolve_profile_xyz.data(), body.revolve_profile_xyz.size()),
                        body.origin, body.axis, body.b)) {
                    const auto pp = std::abs(pm->volume);
                    const auto pv = std::abs(props.volume);
                    if (pp > 1e-12 && pv < 1e-4 * pp) {
                        props.volume = pm->volume;
                        if (body.bbox.is_valid &&
                            (props.area <= 0.0 || props.inertia[0] <= 0.0 || props.inertia[4] <= 0.0 ||
                             props.inertia[8] <= 0.0)) {
                            const auto bboxp = bbox_mass_properties(body.bbox);
                            props.area = bboxp.area;
                            props.centroid = bboxp.centroid;
                            const auto dx = std::max<Scalar>(0.0, body.bbox.max.x - body.bbox.min.x);
                            const auto dy = std::max<Scalar>(0.0, body.bbox.max.y - body.bbox.min.y);
                            const auto dz = std::max<Scalar>(0.0, body.bbox.max.z - body.bbox.min.z);
                            set_box_inertia_about_centroid(props, dx, dy, dz);
                        }
                    }
                }
            }
            return true;
        }
        if (body.a > 0.0) {
            props.volume = body.a;
            if (body.bbox.is_valid) {
                props.centroid = Point3 {
                    (body.bbox.min.x + body.bbox.max.x) * 0.5,
                    (body.bbox.min.y + body.bbox.max.y) * 0.5,
                    (body.bbox.min.z + body.bbox.max.z) * 0.5};
                const auto bboxp = bbox_mass_properties(body.bbox);
                props.area = bboxp.area;
            }
            return true;
        }
    }
    if (body.label == "loft" && body.a > 0.0) {
        props.volume = body.a;
        if (body.bbox.is_valid) {
            props.centroid = Point3 {
                (body.bbox.min.x + body.bbox.max.x) * 0.5,
                (body.bbox.min.y + body.bbox.max.y) * 0.5,
                (body.bbox.min.z + body.bbox.max.z) * 0.5};
            const auto bboxp = bbox_mass_properties(body.bbox);
            props.area = bboxp.area;
        }
        return true;
    }
    if (body.label.size() >= 6 && body.label.compare(0, 6, "sweep:") == 0 && body.a > 0.0) {
        props.volume = body.a;
        if (body.bbox.is_valid) {
            props.centroid = Point3 {
                (body.bbox.min.x + body.bbox.max.x) * 0.5,
                (body.bbox.min.y + body.bbox.max.y) * 0.5,
                (body.bbox.min.z + body.bbox.max.z) * 0.5};
            const auto bboxp = bbox_mass_properties(body.bbox);
            props.area = bboxp.area;
        }
        return true;
    }
    if (body.label.size() >= 8 && body.label.compare(0, 8, "extrude:") == 0 && body.a > 0.0) {
        props.volume = body.a;
        if (body.extrude_poly_cap_area > 0.0) {
            props.area = 2.0 * body.extrude_poly_cap_area + body.extrude_lateral_area;
            props.centroid = body.extrude_mass_centroid;
        } else if (body.bbox.is_valid) {
            props.centroid = Point3 {
                (body.bbox.min.x + body.bbox.max.x) * 0.5,
                (body.bbox.min.y + body.bbox.max.y) * 0.5,
                (body.bbox.min.z + body.bbox.max.z) * 0.5};
            const auto bboxp = bbox_mass_properties(body.bbox);
            props.area = bboxp.area;
        }
        return true;
    }
    props = bbox_mass_properties(body.bbox);
    if (body.label.size() >= 8 && body.label.compare(0, 8, "revolve:") == 0 && body.b > 0.0 &&
        !body.revolve_profile_xyz.empty()) {
        if (const auto pm = try_pappus_revolve_mass(
                std::span<const Point3>(body.revolve_profile_xyz.data(), body.revolve_profile_xyz.size()),
                body.origin, body.axis, body.b)) {
            if (pm->volume > 1e-18) {
                props.volume = pm->volume;
            }
        }
    }
    return true;
}

bool try_boolean_operand_mass_properties(const detail::BodyRecord& body, MassProperties& m) {
    if (try_primitive_analytic_mass_properties(body, m)) {
        return true;
    }
    return try_sweep_body_mass_properties(body, m);
}

/// Rodrigues：绕单位轴 `u` 过 `origin` 旋转角度 θ，`cos_t=cos(θ)`，`sin_t=sin(θ)`。
Point3 rotate_point_around_unit_axis(const Point3& p, const Point3& origin, const Vec3& u, Scalar cos_t,
                                     Scalar sin_t) {
    const Vec3 v {p.x - origin.x, p.y - origin.y, p.z - origin.z};
    const auto uxv = detail::cross(u, v);
    const auto udotv = detail::dot(u, v);
    const auto omc = 1.0 - cos_t;
    const Vec3 vr {v.x * cos_t + uxv.x * sin_t + u.x * udotv * omc,
                   v.y * cos_t + uxv.y * sin_t + u.y * udotv * omc,
                   v.z * cos_t + uxv.z * sin_t + u.z * udotv * omc};
    return detail::add_point_vec(origin, vr);
}

bool valid_axis(const Vec3& axis) {
    return detail::norm(axis) > 0.0;
}

bool valid_face_ids(const detail::KernelState& state, std::span<const FaceId> faces) {
    return std::all_of(faces.begin(), faces.end(), [&state](const FaceId face_id) {
        return detail::has_face(state, face_id);
    });
}

bool valid_edge_ids(const detail::KernelState& state, std::span<const EdgeId> edges) {
    return std::all_of(edges.begin(), edges.end(), [&state](const EdgeId edge_id) {
        return detail::has_edge(state, edge_id);
    });
}

BoundingBox face_bbox(const detail::KernelState& state, FaceId face_id) {
    const auto face_it = state.faces.find(face_id.value);
    if (face_it == state.faces.end()) {
        return {};
    }
    std::vector<LoopId> loops;
    loops.push_back(face_it->second.outer_loop);
    loops.insert(loops.end(), face_it->second.inner_loops.begin(), face_it->second.inner_loops.end());

    BoundingBox bbox;
    bool initialized = false;
    for (const auto loop_id : loops) {
        const auto loop_it = state.loops.find(loop_id.value);
        if (loop_it == state.loops.end()) {
            return {};
        }
        for (const auto coedge_id : loop_it->second.coedges) {
            const auto coedge_it = state.coedges.find(coedge_id.value);
            if (coedge_it == state.coedges.end()) {
                return {};
            }
            const auto edge_it = state.edges.find(coedge_it->second.edge_id.value);
            if (edge_it == state.edges.end()) {
                return {};
            }
            const auto v0_it = state.vertices.find(edge_it->second.v0.value);
            const auto v1_it = state.vertices.find(edge_it->second.v1.value);
            if (v0_it == state.vertices.end() || v1_it == state.vertices.end()) {
                return {};
            }
            for (const auto& point : {v0_it->second.point, v1_it->second.point}) {
                if (!initialized) {
                    bbox = detail::make_bbox(point, point);
                    initialized = true;
                } else {
                    bbox.min.x = std::min(bbox.min.x, point.x);
                    bbox.min.y = std::min(bbox.min.y, point.y);
                    bbox.min.z = std::min(bbox.min.z, point.z);
                    bbox.max.x = std::max(bbox.max.x, point.x);
                    bbox.max.y = std::max(bbox.max.y, point.y);
                    bbox.max.z = std::max(bbox.max.z, point.z);
                }
            }
        }
    }
    return initialized ? bbox : BoundingBox {};
}

BoundingBox shell_bbox(const detail::KernelState& state, ShellId shell_id) {
    const auto shell_it = state.shells.find(shell_id.value);
    if (shell_it == state.shells.end()) {
        return {};
    }
    BoundingBox bbox;
    bool initialized = false;
    for (const auto face_id : shell_it->second.faces) {
        const auto face_it = state.faces.find(face_id.value);
        if (face_it == state.faces.end()) {
            return {};
        }
        std::vector<LoopId> loops;
        loops.push_back(face_it->second.outer_loop);
        loops.insert(loops.end(), face_it->second.inner_loops.begin(), face_it->second.inner_loops.end());
        for (const auto loop_id : loops) {
            const auto loop_it = state.loops.find(loop_id.value);
            if (loop_it == state.loops.end()) {
                return {};
            }
            for (const auto coedge_id : loop_it->second.coedges) {
                const auto coedge_it = state.coedges.find(coedge_id.value);
                if (coedge_it == state.coedges.end()) {
                    return {};
                }
                const auto edge_it = state.edges.find(coedge_it->second.edge_id.value);
                if (edge_it == state.edges.end()) {
                    return {};
                }
                const auto v0_it = state.vertices.find(edge_it->second.v0.value);
                const auto v1_it = state.vertices.find(edge_it->second.v1.value);
                if (v0_it == state.vertices.end() || v1_it == state.vertices.end()) {
                    return {};
                }
                for (const auto& point : {v0_it->second.point, v1_it->second.point}) {
                    if (!initialized) {
                        bbox = detail::make_bbox(point, point);
                        initialized = true;
                    } else {
                        bbox.min.x = std::min(bbox.min.x, point.x);
                        bbox.min.y = std::min(bbox.min.y, point.y);
                        bbox.min.z = std::min(bbox.min.z, point.z);
                        bbox.max.x = std::max(bbox.max.x, point.x);
                        bbox.max.y = std::max(bbox.max.y, point.y);
                        bbox.max.z = std::max(bbox.max.z, point.z);
                    }
                }
            }
        }
    }
    return initialized ? bbox : BoundingBox {};
}

std::vector<ShellId> shells_for_body(const detail::BodyRecord& body) {
    if (!body.shells.empty()) {
        return body.shells;
    }
    return body.source_shells;
}





std::vector<FaceId> faces_for_body_boolean(const detail::KernelState& state, BodyId body_id) {
    std::vector<FaceId> faces;
    if (!detail::has_body(state, body_id)) {
        return faces;
    }
    const auto& body = state.bodies.at(body_id.value);
    const auto shells = shells_for_body(body);
    for (const auto shell_id : shells) {
        const auto shell_it = state.shells.find(shell_id.value);
        if (shell_it == state.shells.end()) {
            continue;
        }
        for (const auto face_id : shell_it->second.faces) {
            faces.push_back(face_id);
        }
    }
    return faces;
}

