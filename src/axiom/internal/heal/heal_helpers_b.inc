// Strict：用拓扑面 bbox 的两较大边长之积作为面积下界代理，拦截整体落在容差噪声尺度内的退化面片。
bool validate_strict_owned_faces_nondegenerate_area_proxy(const std::shared_ptr<detail::KernelState>& state,
                                                            BodyId body_id,
                                                            std::string& reason,
                                                            std::vector<std::uint64_t>& related) {
    related.clear();
    const auto& body = state->bodies.at(body_id.value);
    if (body.rep_kind != RepKind::ExactBRep || body.shells.empty()) {
        return true;
    }
    const Scalar lin = detail::resolve_linear_tolerance(0.0, state->config.tolerance);
    if (!std::isfinite(lin) || lin <= 0.0) {
        reason = "线性容差无效，无法进行面片尺度退化门控";
        related = {body_id.value};
        return false;
    }
    const Scalar char_scale = std::max(max_extent(body.bbox), lin);
    const Scalar thr_area =
        std::max<Scalar>(lin * lin * Scalar(1e-6),
                         std::numeric_limits<Scalar>::epsilon() * char_scale * char_scale * Scalar(1e6));
    TopologyQueryService topo(state);
    for (const auto shell_id : body.shells) {
        const auto shell_it = state->shells.find(shell_id.value);
        if (shell_it == state->shells.end()) {
            continue;
        }
        for (const auto face_id : shell_it->second.faces) {
            const auto bb = topo.bbox_of_face(face_id);
            if (bb.status != StatusCode::Ok || !bb.value.has_value()) {
                reason = "拓扑面包围盒查询失败";
                related = {body_id.value, face_id.value};
                return false;
            }
            const BoundingBox& b = *bb.value;
            if (!b.is_valid || b.max.x < b.min.x || b.max.y < b.min.y || b.max.z < b.min.z) {
                reason = "拓扑面包围盒无效或退化";
                related = {body_id.value, face_id.value};
                return false;
            }
            std::array<Scalar, 3> e {b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z};
            std::sort(e.begin(), e.end());
            const Scalar area_proxy = e[1] * e[2];
            if (!std::isfinite(area_proxy) || area_proxy < thr_area) {
                reason = "拓扑面整体尺度过小（面积代理低于相对线性容差平方门控）";
                related = {body_id.value, face_id.value};
                return false;
            }
        }
    }
    return true;
}

Vec3 newell_polygon_normal(const std::vector<Point3>& poly) {
    Vec3 n {0.0, 0.0, 0.0};
    const std::size_t m = poly.size();
    if (m < 3) {
        return n;
    }
    for (std::size_t i = 0; i < m; ++i) {
        const Point3& p0 = poly[i];
        const Point3& p1 = poly[(i + 1) % m];
        n.x += (p0.y - p1.y) * (p0.z + p1.z);
        n.y += (p0.z - p1.z) * (p0.x + p1.x);
        n.z += (p0.x - p1.x) * (p0.y + p1.y);
    }
    return n;
}

// Strict：支撑为解析平面时，用外环定向边序列构造顶点多边形，校验其 Newell 法向与平面记录法向共线（允许反向，受角度容差约束）。
bool validate_strict_owned_plane_face_loop_normals(const std::shared_ptr<detail::KernelState>& state,
                                                   BodyId body_id,
                                                   std::string& reason,
                                                   std::vector<std::uint64_t>& related) {
    related.clear();
    const auto& body = state->bodies.at(body_id.value);
    if (body.rep_kind != RepKind::ExactBRep || body.shells.empty()) {
        return true;
    }
    const Scalar ang = detail::resolve_angular_tolerance(0.0, state->config.tolerance);
    if (!std::isfinite(ang) || ang < 0.0) {
        reason = "角度容差无效，无法进行平面-环法向一致性门控";
        related = {body_id.value};
        return false;
    }
    constexpr Scalar kPiHalf = 1.57079632679489661923;
    const Scalar ang_clamped = std::min(ang, kPiHalf);
    const Scalar cos_needed = std::cos(ang_clamped);
    const Scalar lin = detail::resolve_linear_tolerance(0.0, state->config.tolerance);
    const Scalar char_scale = std::max(max_extent(body.bbox), lin);
    const Scalar n_floor = std::max(lin * Scalar(1e-9),
                                    std::numeric_limits<Scalar>::epsilon() * char_scale * Scalar(100.0));

    for (const auto shell_id : body.shells) {
        const auto shell_it = state->shells.find(shell_id.value);
        if (shell_it == state->shells.end()) {
            continue;
        }
        for (const auto face_id : shell_it->second.faces) {
            const auto face_it = state->faces.find(face_id.value);
            if (face_it == state->faces.end()) {
                continue;
            }
            const SurfaceId surface_id = face_it->second.surface_id;
            if (!detail::has_surface(*state, surface_id)) {
                reason = "面引用了不存在的支撑曲面";
                related = {body_id.value, face_id.value};
                return false;
            }
            const auto& surf = state->surfaces.at(surface_id.value);
            if (surf.kind != detail::SurfaceKind::Plane) {
                continue;
            }
            const Vec3 n_geo = detail::normalize(surf.normal);
            if (!std::isfinite(n_geo.x) || !std::isfinite(n_geo.y) || !std::isfinite(n_geo.z) ||
                detail::norm(n_geo) <= 0.0) {
                reason = "平面支撑曲面法向无效或为零";
                related = {body_id.value, face_id.value, surface_id.value};
                return false;
            }

            const LoopId outer = face_it->second.outer_loop;
            if (!detail::has_loop(*state, outer)) {
                reason = "拓扑面缺少有效外环";
                related = {body_id.value, face_id.value};
                return false;
            }
            const auto loop_it = state->loops.find(outer.value);
            if (loop_it == state->loops.end()) {
                reason = "拓扑面引用了不存在的外环";
                related = {body_id.value, face_id.value, outer.value};
                return false;
            }

            std::vector<Point3> poly;
            poly.reserve(loop_it->second.coedges.size() + 1);
            for (const auto coedge_id : loop_it->second.coedges) {
                const auto co_it = state->coedges.find(coedge_id.value);
                if (co_it == state->coedges.end()) {
                    reason = "外环引用了不存在的共边";
                    related = {body_id.value, face_id.value, coedge_id.value};
                    return false;
                }
                const auto e_it = state->edges.find(co_it->second.edge_id.value);
                if (e_it == state->edges.end()) {
                    reason = "外环引用了不存在的边";
                    related = {body_id.value, face_id.value, co_it->second.edge_id.value};
                    return false;
                }
                const VertexId vs = co_it->second.reversed ? e_it->second.v1 : e_it->second.v0;
                const VertexId ve = co_it->second.reversed ? e_it->second.v0 : e_it->second.v1;
                if (!detail::has_vertex(*state, vs) || !detail::has_vertex(*state, ve)) {
                    reason = "外环边引用了不存在的顶点";
                    related = {body_id.value, face_id.value, co_it->second.edge_id.value};
                    return false;
                }
                const Point3& ps = state->vertices.at(vs.value).point;
                const Point3& pe = state->vertices.at(ve.value).point;
                if (poly.empty()) {
                    poly.push_back(ps);
                    poly.push_back(pe);
                } else {
                    poly.push_back(pe);
                }
            }
            if (poly.size() >= 2) {
                const Point3& a = poly.front();
                const Point3& b = poly.back();
                const Scalar dx = b.x - a.x;
                const Scalar dy = b.y - a.y;
                const Scalar dz = b.z - a.z;
                if (dx * dx + dy * dy + dz * dz <= n_floor * n_floor) {
                    poly.pop_back();
                }
            }
            if (poly.size() < 3) {
                continue;
            }

            const Vec3 n_raw = newell_polygon_normal(poly);
            const Scalar n_raw_len = detail::norm(n_raw);
            if (!std::isfinite(n_raw_len) || n_raw_len < n_floor) {
                continue;
            }
            const Vec3 n_topo = detail::scale(n_raw, 1.0 / n_raw_len);
            const Scalar abs_dot = std::abs(detail::dot(n_topo, n_geo));
            if (!std::isfinite(abs_dot)) {
                reason = "环法向与平面法向点积非有限值";
                related = {body_id.value, face_id.value, surface_id.value};
                return false;
            }
            if (abs_dot + Scalar(4e-15) < cos_needed) {
                reason = "平面拓扑面外环法向与支撑平面几何法向不一致（超出角度容差）";
                related = {body_id.value, face_id.value, surface_id.value, outer.value};
                return false;
            }
        }
    }
    return true;
}

Result<void> strict_owned_brep_mesh_self_intersection_with_builder(
    const std::shared_ptr<detail::KernelState>& state,
    BodyId body_id,
    const detail::BodyRecord& body,
    const std::function<Result<MeshId>(const TessellationOptions&)>& build_mesh,
    std::vector<std::uint64_t> related_entities,
    std::string_view fail_title,
    std::string_view ok_summary,
    std::string_view extra_hit_suffix) {
    constexpr std::size_t k_self_intersection_max_tris = 25000;
    const auto ext = std::max(0.0, min_extent(body.bbox));
    TessellationOptions tess_opts;
    const Scalar rlin = detail::resolve_linear_tolerance(0.0, state->config.tolerance);
    tess_opts.chordal_error = std::max(rlin * 5.0, ext * 0.12);
    tess_opts.angular_error = 25.0;
    tess_opts.compute_normals = true;
    tess_opts.generate_texcoords = false;
    auto run_si = [&](const TessellationOptions& opts) {
        const auto mesh_res = build_mesh(opts);
        if (mesh_res.status != StatusCode::Ok || !mesh_res.value.has_value()) {
            return std::make_pair(detail::MeshSelfIntersectionStatus::InvalidMesh,
                                std::make_pair(std::size_t {0}, std::size_t {0}));
        }
        const auto it = state->meshes.find(mesh_res.value->value);
        if (it == state->meshes.end()) {
            return std::make_pair(detail::MeshSelfIntersectionStatus::InvalidMesh,
                                std::make_pair(std::size_t {0}, std::size_t {0}));
        }
        const auto si = detail::analyze_mesh_self_intersection(it->second, rlin, k_self_intersection_max_tris);
        return std::make_pair(si.status, std::make_pair(si.tri_i, si.tri_j));
    };
    auto si_first = run_si(tess_opts);
    if (si_first.first == detail::MeshSelfIntersectionStatus::InvalidMesh) {
        return detail::failed_void(*state, StatusCode::DegenerateGeometry, diag_codes::kTesFailure,
                                   std::string(fail_title) + "：分析网格无效或索引越界", std::string(fail_title),
                                   std::move(related_entities));
    }
    if (si_first.first == detail::MeshSelfIntersectionStatus::TooManyTriangles) {
        return detail::failed_void(*state, StatusCode::OperationFailed, diag_codes::kValSelfIntersectionMeshBudget,
                                   std::string(fail_title) + "：三角化三角形数量超过检测预算", std::string(fail_title),
                                   std::move(related_entities));
    }
    if (si_first.first == detail::MeshSelfIntersectionStatus::Hit) {
        TessellationOptions coarse = tess_opts;
        coarse.chordal_error = std::max(coarse.chordal_error * 8.0, ext * 0.15);
        coarse.angular_error = std::min(90.0, coarse.angular_error * 2.0);
        auto si_second = run_si(coarse);
        if (si_second.first == detail::MeshSelfIntersectionStatus::Hit) {
            TessellationOptions very_coarse = coarse;
            very_coarse.chordal_error = std::max(very_coarse.chordal_error * 4.0, ext * 0.35);
            very_coarse.angular_error = std::min(90.0, very_coarse.angular_error * 1.5);
            si_second = run_si(very_coarse);
        }
        if (si_second.first == detail::MeshSelfIntersectionStatus::Hit) {
            std::string msg = std::string(fail_title) + "：三角化近似检测到相交三角形对 tri=" +
                              std::to_string(si_second.second.first) + " 与 tri=" +
                              std::to_string(si_second.second.second) +
                              "（结果为网格近似，可调整 tessellation 复核）";
            if (!extra_hit_suffix.empty()) {
                msg.append(extra_hit_suffix.data(), extra_hit_suffix.size());
            }
            return detail::failed_void(*state, StatusCode::OperationFailed, diag_codes::kValSelfIntersection,
                                       std::move(msg), std::string(fail_title), std::move(related_entities));
        }
        if (si_second.first == detail::MeshSelfIntersectionStatus::InvalidMesh ||
            si_second.first == detail::MeshSelfIntersectionStatus::TooManyTriangles) {
            return detail::failed_void(*state, StatusCode::OperationFailed, diag_codes::kValSelfIntersection,
                                       std::string(fail_title) + "：粗化重试后仍无法完成自交分析", std::string(fail_title),
                                       std::move(related_entities));
        }
    }
    return ok_void(state->create_diagnostic(std::string(ok_summary)));
}
