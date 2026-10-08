RepairService::RepairService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<void> RepairService::repair_face_trim_pcurves(FaceId face_id, RepairMode mode) {
    if (!detail::has_face(*state_, face_id)) {
        return heal_failed_void(*state_, StatusCode::InvalidInput, diag_codes::kHealAutoRepairFailure,
                                "修剪参数曲线修复失败：目标面不存在", "修剪参数曲线修复失败",
                                {face_id.value}, "heal.repair_trim.input",
                                {{"face_exists", 0.0, "bool"},
                                 {"repair_mode", static_cast<Scalar>(mode), "enum"}});
    }
    const auto& face = state_->faces.at(face_id.value);
    if (!detail::has_surface(*state_, face.surface_id)) {
        return heal_failed_void(*state_, StatusCode::InvalidTopology,
                                diag_codes::kTopoFaceOuterLoopInvalid,
                                "修剪参数曲线修复失败：面引用的曲面不存在", "修剪参数曲线修复失败",
                                {face_id.value, face.surface_id.value}, "heal.repair_trim.surface",
                                {{"surface_exists", 0.0, "bool"}});
    }
    const auto& surf = state_->surfaces.at(face.surface_id.value);
    if (surf.kind != detail::SurfaceKind::Plane &&
        surf.kind != detail::SurfaceKind::Cylinder &&
        surf.kind != detail::SurfaceKind::Sphere) {
        return heal_failed_void(*state_, StatusCode::NotImplemented,
                                diag_codes::kCoreOperationUnsupported,
                                "修剪参数曲线修复失败：暂仅支持平面/圆柱/球面修剪面", "修剪参数曲线修复失败",
                                {face_id.value, face.surface_id.value}, "heal.repair_trim.surface",
                                {{"surface_kind", static_cast<Scalar>(surf.kind), "enum"}});
    }
    if (mode == RepairMode::ReportOnly) {
        const auto diag_ro = state_->create_diagnostic("修剪参数曲线修复预检完成（ReportOnly）");
        append_repair_pipeline_trace(*state_, diag_ro, "repair_face_trim_pcurves",
                                     {face_id.value, face.surface_id.value});
        append_repair_replay_summary_issue(*state_, diag_ro);
        return ok_void(diag_ro);
    }

    SurfaceService surface_service{state_};
    CurveService curve_service{state_};
    TopologyValidationService topo_validation{state_};
    HealModelMutationRollback rollback {*state_};
    std::uint64_t rebuilt_coedge_count = 0;

    auto oriented_vertices_for_coedge = [&](CoedgeId coedge_id) -> std::optional<std::array<VertexId, 2>> {
        const auto ce_it = state_->coedges.find(coedge_id.value);
        if (ce_it == state_->coedges.end()) return std::nullopt;
        const auto e_it = state_->edges.find(ce_it->second.edge_id.value);
        if (e_it == state_->edges.end()) return std::nullopt;
        if (!detail::has_vertex(*state_, e_it->second.v0) || !detail::has_vertex(*state_, e_it->second.v1) ||
            e_it->second.v0.value == e_it->second.v1.value) return std::nullopt;
        if (ce_it->second.reversed) {
            return std::array<VertexId, 2>{e_it->second.v1, e_it->second.v0};
        }
        return std::array<VertexId, 2>{e_it->second.v0, e_it->second.v1};
    };

    auto rebuild_loop = [&](LoopId loop_id) -> Result<void> {
        const auto loop_it = state_->loops.find(loop_id.value);
        if (loop_it == state_->loops.end()) {
            return heal_failed_void(*state_, StatusCode::InvalidTopology,
                                    diag_codes::kTopoLoopNotClosed,
                                    "修剪参数曲线修复失败：面引用的环不存在", "修剪参数曲线修复失败",
                                    {face_id.value, loop_id.value}, "heal.repair_trim.loop",
                                    {{"loop_exists", 0.0, "bool"},
                                     {"rebuilt_coedge_count", static_cast<Scalar>(rebuilt_coedge_count), "count"}});
        }
        for (const auto coedge_id : loop_it->second.coedges) {
            const auto oriented = oriented_vertices_for_coedge(coedge_id);
            if (!oriented.has_value()) continue;
            const auto v0_it = state_->vertices.find((*oriented)[0].value);
            const auto v1_it = state_->vertices.find((*oriented)[1].value);
            if (v0_it == state_->vertices.end() || v1_it == state_->vertices.end()) continue;
            const auto ce_it = state_->coedges.find(coedge_id.value);
            if (ce_it == state_->coedges.end()) continue;
            const auto e_it = state_->edges.find(ce_it->second.edge_id.value);
            if (e_it == state_->edges.end() || e_it->second.curve_id.value == 0) continue;

            const auto curve_id = e_it->second.curve_id;
            if (!detail::has_curve(*state_, curve_id)) continue;

            // Estimate curve parameter interval between edge endpoints.
            const auto t0r = curve_service.closest_parameter(curve_id, v0_it->second.point);
            const auto t1r = curve_service.closest_parameter(curve_id, v1_it->second.point);
            if (t0r.status != StatusCode::Ok || t1r.status != StatusCode::Ok ||
                !t0r.value.has_value() || !t1r.value.has_value()) {
                continue;
            }
            const auto t0 = *t0r.value;
            const auto t1 = *t1r.value;

            struct TrimSample {
                Scalar t{};
                Point3 p{};
                Point2 uv{};
            };
            auto dist3_pt = [](const Point3& a, const Point3& b) -> Scalar {
                const auto dx = a.x - b.x;
                const auto dy = a.y - b.y;
                const auto dz = a.z - b.z;
                return std::sqrt(dx * dx + dy * dy + dz * dz);
            };
            auto lerp3 = [](const Point3& a, const Point3& b, Scalar s) -> Point3 {
                return Point3{a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s,
                              a.z + (b.z - a.z) * s};
            };

            std::vector<TrimSample> chain;
            auto append_endpoint = [&](Scalar tt) -> bool {
                const auto ev = curve_service.eval(curve_id, tt, 0);
                if (ev.status != StatusCode::Ok || !ev.value.has_value()) {
                    return false;
                }
                const auto uv = surface_service.closest_uv(face.surface_id, ev.value->point);
                if (uv.status != StatusCode::Ok || !uv.value.has_value()) {
                    return false;
                }
                chain.push_back(
                    TrimSample{tt, ev.value->point, Point2{uv.value->first, uv.value->second}});
                return true;
            };
            if (!append_endpoint(t0) || !append_endpoint(t1)) {
                continue;
            }

            const Scalar chord_tol = std::max(
                detail::resolve_linear_tolerance(0.0, state_->config.tolerance) * Scalar(0.32),
                Scalar(1e-10));
            const std::size_t max_pts =
                mode == RepairMode::Aggressive ? std::size_t{384} : std::size_t{192};
            bool changed = true;
            int guard = 0;
            while (changed && chain.size() < max_pts && guard < 64) {
                changed = false;
                ++guard;
                for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
                    const auto& sa = chain[i];
                    const auto& sb = chain[i + 1];
                    const auto span = std::abs(sb.t - sa.t);
                    if (span <= Scalar(1e-14)) {
                        continue;
                    }
                    const Scalar tm = (sa.t + sb.t) * Scalar(0.5);
                    const auto evm = curve_service.eval(curve_id, tm, 0);
                    if (evm.status != StatusCode::Ok || !evm.value.has_value()) {
                        continue;
                    }
                    const Point3& pm = evm.value->point;
                    const Scalar err_c = dist3_pt(pm, lerp3(sa.p, sb.p, Scalar(0.5)));
                    const Point2 uv_lin{(sa.uv.x + sb.uv.x) * Scalar(0.5),
                                        (sa.uv.y + sb.uv.y) * Scalar(0.5)};
                    const auto surf_mid =
                        surface_service.eval(face.surface_id, uv_lin.x, uv_lin.y, 0);
                    Scalar err_uv = Scalar(0);
                    if (surf_mid.status == StatusCode::Ok && surf_mid.value.has_value()) {
                        err_uv = dist3_pt(surf_mid.value->point, pm);
                    }
                    const Scalar err = std::max(err_c, err_uv);
                    if (err <= chord_tol) {
                        continue;
                    }
                    const auto uvm = surface_service.closest_uv(face.surface_id, pm);
                    if (uvm.status != StatusCode::Ok || !uvm.value.has_value()) {
                        continue;
                    }
                    TrimSample mid{tm, pm, Point2{uvm.value->first, uvm.value->second}};
                    chain.insert(chain.begin() + static_cast<std::ptrdiff_t>(i + 1), mid);
                    changed = true;
                    break;
                }
            }

            std::vector<Point2> uv_poly;
            uv_poly.reserve(chain.size());
            for (const auto& s : chain) {
                if (!uv_poly.empty()) {
                    const auto dx = uv_poly.back().x - s.uv.x;
                    const auto dy = uv_poly.back().y - s.uv.y;
                    if (std::sqrt(dx * dx + dy * dy) <=
                        detail::resolve_linear_tolerance(0.0, state_->config.tolerance)) {
                        continue;
                    }
                }
                uv_poly.push_back(s.uv);
            }
            if (uv_poly.size() < 2) {
                // Fall back to endpoints only.
                const auto uv0 = surface_service.closest_uv(face.surface_id, v0_it->second.point);
                const auto uv1 = surface_service.closest_uv(face.surface_id, v1_it->second.point);
                if (uv0.status != StatusCode::Ok || uv1.status != StatusCode::Ok ||
                    !uv0.value.has_value() || !uv1.value.has_value()) {
                    continue;
                }
                uv_poly = {
                    Point2{uv0.value->first, uv0.value->second},
                    Point2{uv1.value->first, uv1.value->second},
                };
            }

            detail::PCurveRecord pc;
            pc.kind = detail::PCurveKind::Polyline;
            pc.poles = std::move(uv_poly);
            const auto pc_id = PCurveId{state_->allocate_id()};
            state_->pcurves.emplace(pc_id.value, std::move(pc));
            rollback.remember_coedge_pcurve(coedge_id);
            state_->coedges[coedge_id.value].pcurve_id = pc_id;
            ++rebuilt_coedge_count;
        }
        return ok_void(state_->create_diagnostic("已重建环参数曲线"));
    };

    auto r0 = rebuild_loop(face.outer_loop);
    if (r0.status != StatusCode::Ok) return r0;
    for (const auto inner : face.inner_loops) {
        auto ri = rebuild_loop(inner);
        if (ri.status != StatusCode::Ok) return ri;
    }

    detail::rebuild_topology_links(*state_);
    if (rebuilt_coedge_count == 0) {
        return heal_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kHealAutoRepairFailure,
            "修剪参数曲线修复失败：未能从任何空间边生成有效参数曲线，已回滚本次尝试",
            "修剪参数曲线修复失败", {face_id.value, face.surface_id.value},
            "heal.repair_trim.rebuild",
            {{"rebuilt_coedge_count", 0.0, "count"},
             {"outer_loop_id", static_cast<Scalar>(face.outer_loop.value), "id"},
             {"rollback_applied", 1.0, "bool"}});
    }

    const auto post = topo_validation.validate_face_trim_consistency(face_id);
    if (post.status != StatusCode::Ok) {
        return heal_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kHealAutoRepairFailure,
            "修剪参数曲线修复失败：重建后仍未通过修剪一致性验证，已回滚全部新绑定",
            "修剪参数曲线修复失败", {face_id.value, face.surface_id.value},
            "heal.repair_trim.post_validate",
            {{"rebuilt_coedge_count", static_cast<Scalar>(rebuilt_coedge_count), "count"},
             {"rollback_applied", 1.0, "bool"},
             {"child_status_code", static_cast<Scalar>(post.status), "enum"}});
    }
    const auto diag_trim = state_->create_diagnostic("修剪参数曲线修复完成");
    append_repair_pipeline_trace(*state_, diag_trim, "repair_face_trim_pcurves",
                                 {face_id.value, face.surface_id.value});
    append_repair_replay_summary_issue(*state_, diag_trim);
    rollback.commit();
    return ok_void(diag_trim);
}

Result<OpReport> RepairService::sew_faces(std::span<const FaceId> faces, Scalar tolerance, RepairMode mode) {
    if (faces.empty() || !std::isfinite(tolerance) || tolerance <= 0.0) {
        return heal_failed_result<OpReport>(
            *state_, StatusCode::InvalidInput, diag_codes::kHealSewFailure,
            "缝合失败：输入面集合为空或缝合容差非法", "缝合失败",
            {faces.empty() ? 0 : faces.front().value}, "heal.sew_faces.input",
            {{"face_count", static_cast<Scalar>(faces.size()), "count"},
             {"tolerance_is_finite", std::isfinite(tolerance) ? 1.0 : 0.0, "bool"},
             {"tolerance_is_positive", tolerance > 0.0 ? 1.0 : 0.0, "bool"},
             {"repair_mode", static_cast<Scalar>(mode), "enum"}});
    }
    if (!std::all_of(faces.begin(), faces.end(), [this](const FaceId face_id) { return detail::has_face(*state_, face_id); })) {
        const auto invalid = std::find_if(
            faces.begin(), faces.end(),
            [this](FaceId face_id) { return !detail::has_face(*state_, face_id); });
        return heal_failed_result<OpReport>(
            *state_, StatusCode::InvalidInput, diag_codes::kHealSewFailure,
            "缝合失败：输入面集合包含无效面", "缝合失败",
            {invalid->value}, "heal.sew_faces.input",
            {{"face_count", static_cast<Scalar>(faces.size()), "count"},
             {"invalid_face_id", static_cast<Scalar>(invalid->value), "id"}});
    }
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Generic;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "sewn_body";
    record.bbox = detail::compute_faces_bbox(*state_, faces);
    if (!record.bbox.is_valid) {
        return heal_failed_result<OpReport>(
            *state_, StatusCode::InvalidTopology, diag_codes::kHealSewFailure,
            "缝合失败：无法从输入面推导有效包围盒，拓扑链路可能不完整", "缝合失败",
            {faces.front().value}, "heal.sew_faces.bounds",
            {{"face_count", static_cast<Scalar>(faces.size()), "count"},
             {"bbox_valid_flag", 0.0, "bool"}});
    }
    for (const auto face_id : faces) {
        append_unique_face(record.source_faces, face_id);
        const auto shell_it = state_->face_to_shells.find(face_id.value);
        if (shell_it == state_->face_to_shells.end()) {
            continue;
        }
        for (const auto shell_value : shell_it->second) {
            append_unique_shell(record.source_shells, ShellId {shell_value});
        }
    }
    HealModelMutationRollback rollback {*state_};
    const auto body_id = BodyId {state_->allocate_id()};
    detail::materialize_body_bbox_topology(*state_, record);
    state_->bodies.emplace(body_id.value, record);
    detail::rebuild_topology_links(*state_);
    ValidationService validation {state_};
    const auto post = validation.validate_all(body_id, ValidationMode::Standard);
    if (post.status != StatusCode::Ok) {
        return heal_failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kHealSewFailure,
            "缝合失败：派生体后验验证未通过，已回滚本次物化", "缝合失败",
            {faces.front().value, body_id.value}, "heal.sew_faces.post_validate",
            {{"face_count", static_cast<Scalar>(faces.size()), "count"},
             {"tolerance", tolerance, "model_unit"},
             {"allocated_object_count", static_cast<Scalar>(state_->next_id - rollback.first_allocated_id()), "count"},
             {"rollback_applied", 1.0, "bool"}});
    }
    detail::invalidate_eval_for_faces(*state_, faces);
    const auto diag = state_->create_diagnostic("已完成缝合");
    append_repair_pipeline_trace(*state_, diag, "sew_faces", {faces.front().value, body_id.value});
    append_repair_replay_summary_issue(*state_, diag);
    {
        auto info = detail::make_info_issue(diag_codes::kHealSewCompleted, "缝合已生成派生体");
        info.stage = "heal.sew_faces";
        info.related_entities = {faces.front().value, body_id.value};
        state_->append_diagnostic_issue(diag, std::move(info));
    }
    rollback.commit();
    return ok_result(OpReport {StatusCode::Ok, body_id, diag, {}}, diag);
}

Result<OpReport> RepairService::remove_small_edges(BodyId body_id, Scalar threshold, RepairMode mode) {
    if (!detail::has_body(*state_, body_id) || !std::isfinite(threshold) || threshold <= 0.0) {
        return heal_failed_result<OpReport>(
            *state_, StatusCode::InvalidInput, diag_codes::kHealSmallEdgeFailure,
            "小边清理失败：目标实体不存在或阈值非法", "小边清理失败", {body_id.value},
            "heal.remove_small_edges.input",
            {{"body_exists", detail::has_body(*state_, body_id) ? 1.0 : 0.0, "bool"},
             {"threshold_is_finite", std::isfinite(threshold) ? 1.0 : 0.0, "bool"},
             {"threshold_is_positive", threshold > 0.0 ? 1.0 : 0.0, "bool"}});
    }
    const auto& source = state_->bodies.at(body_id.value);
    const auto source_minimum_extent = min_extent(source.bbox);
    auto bbox = source.bbox;
    if (mode != RepairMode::ReportOnly) {
        bbox = detail::make_bbox(bbox.min, {bbox.max.x - threshold * 0.1, bbox.max.y, bbox.max.z});
    }
    HealModelMutationRollback rollback {*state_};
    const auto output = mode == RepairMode::ReportOnly ? body_id : clone_modified_body(state_, body_id, "remove_small_edges", bbox);
    if (mode != RepairMode::ReportOnly) {
        ValidationService validation {state_};
        const auto post = validation.validate_all(output, ValidationMode::Standard);
        if (post.status != StatusCode::Ok) {
            return heal_failed_result<OpReport>(
                *state_, StatusCode::OperationFailed, diag_codes::kHealSmallEdgeFailure,
                "小边清理失败：派生体后验验证未通过，已回滚本次修复", "小边清理失败",
                {body_id.value, output.value}, "heal.remove_small_edges.post_validate",
                {{"requested_threshold", threshold, "model_unit"},
                 {"source_minimum_extent", source_minimum_extent, "model_unit"},
                 {"allocated_object_count", static_cast<Scalar>(state_->next_id - rollback.first_allocated_id()), "count"},
                 {"rollback_applied", 1.0, "bool"}});
        }
        detail::invalidate_eval_for_bodies(*state_, {body_id});
    }
    const auto diag = state_->create_diagnostic("已完成小边清理");
    append_repair_pipeline_trace(*state_, diag, "remove_small_edges", {body_id.value, output.value});
    {
        auto w = detail::make_warning_issue(diag_codes::kHealFeatureRemovedWarning, "已清理局部小边，局部特征可能被简化");
        w.stage = "heal.remove_small_edges";
        state_->append_diagnostic_issue(diag, std::move(w));
    }
    append_repair_replay_summary_issue(*state_, diag);
    rollback.commit();
    return ok_result(make_repair_report(StatusCode::Ok, output, diag,
                                        {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "已清理局部小边")}),
                     diag);
}

Result<OpReport> RepairService::remove_small_faces(BodyId body_id, Scalar threshold, RepairMode mode) {
    if (!detail::has_body(*state_, body_id) || !std::isfinite(threshold) || threshold <= 0.0) {
        return heal_failed_result<OpReport>(
            *state_, StatusCode::InvalidInput, diag_codes::kHealSmallFaceFailure,
            "小面清理失败：目标实体不存在或阈值非法", "小面清理失败", {body_id.value},
            "heal.remove_small_faces.input",
            {{"body_exists", detail::has_body(*state_, body_id) ? 1.0 : 0.0, "bool"},
             {"threshold_is_finite", std::isfinite(threshold) ? 1.0 : 0.0, "bool"},
             {"threshold_is_positive", threshold > 0.0 ? 1.0 : 0.0, "bool"}});
    }
    const auto& source = state_->bodies.at(body_id.value);
    const auto source_minimum_extent = min_extent(source.bbox);
    const auto effective_threshold = adaptive_linear_threshold(*state_, body_id, threshold, mode);
    auto bbox = source.bbox;
    if (mode == RepairMode::Aggressive) {
        bbox = detail::make_bbox(bbox.min, {bbox.max.x, bbox.max.y - effective_threshold * 0.1, bbox.max.z});
    }
    HealModelMutationRollback rollback {*state_};
    const auto output = mode == RepairMode::ReportOnly ? body_id : clone_modified_body(state_, body_id, "remove_small_faces", bbox);
    if (mode != RepairMode::ReportOnly) {
        ValidationService validation {state_};
        const auto post = validation.validate_all(output, ValidationMode::Standard);
        if (post.status != StatusCode::Ok) {
            return heal_failed_result<OpReport>(
                *state_, StatusCode::OperationFailed, diag_codes::kHealSmallFaceFailure,
                "小面清理失败：派生体后验验证未通过，已回滚本次修复", "小面清理失败",
                {body_id.value, output.value}, "heal.remove_small_faces.post_validate",
                {{"requested_threshold", threshold, "model_unit"},
                 {"effective_threshold", effective_threshold, "model_unit"},
                 {"source_minimum_extent", source_minimum_extent, "model_unit"},
                 {"allocated_object_count", static_cast<Scalar>(state_->next_id - rollback.first_allocated_id()), "count"},
                 {"rollback_applied", 1.0, "bool"}});
        }
        detail::invalidate_eval_for_bodies(*state_, {body_id});
    }
    const auto diag = state_->create_diagnostic("已完成小面清理");
    append_repair_pipeline_trace(*state_, diag, "remove_small_faces", {body_id.value, output.value});
    if (effective_threshold > threshold) {
        auto w = detail::make_warning_issue(diag_codes::kHealFeatureRemovedWarning,
                                            "小面清理阈值已按体尺度与容差自适应放大");
        w.stage = "heal.remove_small_faces";
        state_->append_diagnostic_issue(diag, std::move(w));
    }
    {
        auto w = detail::make_warning_issue(diag_codes::kHealFeatureRemovedWarning, "已清理局部小面，局部几何可能被简化");
        w.stage = "heal.remove_small_faces";
        state_->append_diagnostic_issue(diag, std::move(w));
    }
    append_repair_replay_summary_issue(*state_, diag);
    rollback.commit();
    return ok_result(make_repair_report(StatusCode::Ok, output, diag,
                                        {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "已清理局部小面")}),
                     diag);
}

Result<OpReport> RepairService::merge_near_coplanar_faces(BodyId body_id, Scalar angle_tolerance, RepairMode mode) {
    if (!detail::has_body(*state_, body_id) || !std::isfinite(angle_tolerance) || angle_tolerance <= 0.0) {
        return heal_failed_result<OpReport>(
            *state_, StatusCode::InvalidInput, diag_codes::kHealAutoRepairFailure,
            "近共面面合并失败：目标实体不存在或角度阈值非法", "近共面面合并失败",
            {body_id.value}, "heal.merge_near_coplanar_faces.input",
            {{"body_exists", detail::has_body(*state_, body_id) ? 1.0 : 0.0, "bool"},
             {"angle_tolerance_is_finite", std::isfinite(angle_tolerance) ? 1.0 : 0.0, "bool"},
             {"angle_tolerance_is_positive", angle_tolerance > 0.0 ? 1.0 : 0.0, "bool"}});
    }
    const auto effective_angle = adaptive_angle_threshold(*state_, body_id, angle_tolerance, mode);
    HealModelMutationRollback rollback {*state_};
    const auto output = mode == RepairMode::ReportOnly
                            ? body_id
                            : clone_modified_body(state_, body_id, "merge_near_coplanar_faces", state_->bodies.at(body_id.value).bbox);
    if (mode != RepairMode::ReportOnly) {
        ValidationService validation {state_};
        const auto post = validation.validate_all(output, ValidationMode::Standard);
        if (post.status != StatusCode::Ok) {
            return heal_failed_result<OpReport>(
                *state_, StatusCode::OperationFailed, diag_codes::kHealAutoRepairFailure,
                "近共面面合并失败：派生体后验验证未通过，已回滚本次修复", "近共面面合并失败",
                {body_id.value, output.value}, "heal.merge_near_coplanar_faces.post_validate",
                {{"requested_angle_tolerance", angle_tolerance, "radian"},
                 {"effective_angle_tolerance", effective_angle, "radian"},
                 {"allocated_object_count", static_cast<Scalar>(state_->next_id - rollback.first_allocated_id()), "count"},
                 {"rollback_applied", 1.0, "bool"}});
        }
        detail::invalidate_eval_for_bodies(*state_, {body_id});
    }
    const auto diag = state_->create_diagnostic("已完成近共面面合并");
    append_repair_pipeline_trace(*state_, diag, "merge_near_coplanar_faces", {body_id.value, output.value});
    std::vector<Warning> warnings;
    if (effective_angle > angle_tolerance) {
        warnings.push_back(detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "近共面阈值已按体尺度与容差自适应放大"));
        auto w = detail::make_warning_issue(diag_codes::kHealFeatureRemovedWarning,
                                            "近共面阈值已按体尺度与容差自适应放大");
        w.stage = "heal.merge_near_coplanar_faces";
        state_->append_diagnostic_issue(diag, std::move(w));
    }
    append_repair_replay_summary_issue(*state_, diag);
    rollback.commit();
    return ok_result(make_repair_report(StatusCode::Ok, output, diag, std::move(warnings)), diag);
}

Result<OpReport> RepairService::auto_repair(BodyId body_id, RepairMode mode) {
    if (!detail::has_body(*state_, body_id)) {
        return heal_failed_result<OpReport>(
            *state_, StatusCode::InvalidInput, diag_codes::kHealAutoRepairFailure,
            "自动修复失败：目标实体不存在", "自动修复失败", {body_id.value},
            "heal.auto_repair.input",
            {{"body_exists", 0.0, "bool"},
             {"repair_mode", static_cast<Scalar>(mode), "enum"}});
    }

    HealModelMutationRollback rollback {*state_};
    ValidationService validation {state_};
    const auto pre_validation = validation.validate_all(body_id, ValidationMode::Standard);
    if (mode == RepairMode::ReportOnly || mode == RepairMode::SuggestOnly) {
        // Observation modes expose the actual validation result without allocating a
        // replacement model. The report remains usable for inspecting dirty imports.
        return ok_result(make_repair_report(pre_validation.status, body_id,
                                            pre_validation.diagnostic_id, {}),
                         pre_validation.diagnostic_id);
    }
    const auto& source = state_->bodies.at(body_id.value);
    if (pre_validation.status != StatusCode::Ok && source.rep_kind == RepKind::ExactBRep &&
        !source.shells.empty()) {
        // First-generation repair deliberately rebuilds the real polygon boundary.
        // It never routes an unsupported or failed owned shell through bbox topology.
        const auto& policy = state_->config.tolerance;
        const Scalar tolerance = policy.linear;
        auto fail = [&](std::string_view stage, std::string message,
                        std::vector<std::uint64_t> entities,
                        std::vector<NumericEvidence> evidence = {}) -> Result<OpReport> {
            evidence.push_back({"repair_mode", static_cast<Scalar>(mode), "enum"});
            evidence.push_back({"allocated_object_count",
                                static_cast<Scalar>(state_->next_id - rollback.first_allocated_id()), "count"});
            evidence.push_back({"rollback_applied", 1.0, "bool"});
            auto result = heal_failed_result<OpReport>(
                *state_, StatusCode::OperationFailed, diag_codes::kHealAutoRepairFailure,
                std::move(message), "平面壳自动修复失败", std::move(entities), stage,
                std::move(evidence));
            const auto pre_diag = state_->diagnostics.find(pre_validation.diagnostic_id.value);
            if (pre_diag != state_->diagnostics.end()) {
                for (const auto& issue : pre_diag->second.issues) {
                    auto tracked = with_related_entities(issue, {body_id.value});
                    if (tracked.stage.empty()) tracked.stage = "heal.auto_repair.pre_validate";
                    if ((tracked.severity == IssueSeverity::Error || tracked.severity == IssueSeverity::Fatal) &&
                        tracked.numeric_evidence.empty()) {
                        tracked.numeric_evidence = heal_failure_evidence(pre_validation.status, tracked.related_entities.size());
                    }
                    state_->append_diagnostic_issue(result.diagnostic_id, std::move(tracked));
                }
            }
            return result;
        };
        if (!std::isfinite(tolerance) || !std::isfinite(policy.min_local) ||
            !std::isfinite(policy.max_local) || tolerance <= 0.0 ||
            policy.min_local <= 0.0 || policy.max_local < policy.min_local ||
            tolerance < policy.min_local || tolerance > policy.max_local || source.shells.size() != 1) {
            return fail("heal.auto_repair.planar.input", "平面壳修复要求单壳与有限且位于本地边界内的线性容差",
                        {body_id.value}, {{"owned_shell_count", static_cast<Scalar>(source.shells.size()), "count"},
                                          {"tolerance_is_finite", std::isfinite(tolerance) ? 1.0 : 0.0, "bool"}});
        }
        const auto shell_id = source.shells.front();
        const auto shell_it = state_->shells.find(shell_id.value);
        if (shell_it == state_->shells.end() || shell_it->second.faces.empty()) {
            return fail("heal.auto_repair.planar.extract", "平面壳修复输入壳不存在或无面",
                        {body_id.value, shell_id.value});
        }
        struct Polygon {
            FaceId source_face;
            Point3 plane_origin;
            Vec3 plane_normal;
            std::vector<std::pair<VertexId, VertexId>> segments;
            std::vector<std::size_t> nodes;
        };
        std::vector<Polygon> polygons;
        std::map<std::uint64_t, Point3> input_points;
        std::unordered_set<std::uint64_t> seen_faces;
        std::size_t duplicate_faces = 0;
        auto distance = [](const Point3& a, const Point3& b) {
            return static_cast<Scalar>(std::hypot(static_cast<long double>(a.x) - b.x,
                                                   static_cast<long double>(a.y) - b.y,
                                                   static_cast<long double>(a.z) - b.z));
        };
        for (const auto face_id : shell_it->second.faces) {
            if (!seen_faces.insert(face_id.value).second) {
                ++duplicate_faces;
                continue;
            }
            const auto face_it = state_->faces.find(face_id.value);
            if (face_it == state_->faces.end() || !face_it->second.inner_loops.empty()) {
                return fail("heal.auto_repair.planar.extract", "平面壳修复不支持缺失面或内孔环",
                            {body_id.value, shell_id.value, face_id.value});
            }
            const auto& face = face_it->second;
            const auto surface_it = state_->surfaces.find(face.surface_id.value);
            const auto loop_it = state_->loops.find(face.outer_loop.value);
            if (surface_it == state_->surfaces.end() || surface_it->second.kind != detail::SurfaceKind::Plane ||
                !surface_it->second.trim_uv_holes.empty() || face.mass_boundary_proxy ||
                loop_it == state_->loops.end() || loop_it->second.coedges.size() < 3) {
                return fail("heal.auto_repair.planar.extract", "平面壳修复仅支持真实平面直线外环",
                            {body_id.value, face_id.value, face.surface_id.value, face.outer_loop.value});
            }
            const auto& surface = surface_it->second;
            const Scalar normal_length = std::hypot(surface.normal.x, surface.normal.y, surface.normal.z);
            if (!point3_components_finite(surface.origin) || !std::isfinite(normal_length) || normal_length <= 0.0) {
                return fail("heal.auto_repair.planar.extract", "平面壳修复遇到非有限或退化支撑平面",
                            {body_id.value, face_id.value, face.surface_id.value});
            }
            Polygon polygon {face_id, surface.origin, detail::scale(surface.normal, 1.0 / normal_length), {}, {}};
            for (const auto coedge_id : loop_it->second.coedges) {
                const auto coedge_it = state_->coedges.find(coedge_id.value);
                if (coedge_it == state_->coedges.end()) {
                    return fail("heal.auto_repair.planar.extract", "平面壳外环引用不存在的共边",
                                {body_id.value, face_id.value, coedge_id.value});
                }
                const auto edge_it = state_->edges.find(coedge_it->second.edge_id.value);
                if (edge_it == state_->edges.end()) {
                    return fail("heal.auto_repair.planar.extract", "平面壳外环引用不存在的边",
                                {body_id.value, face_id.value, coedge_it->second.edge_id.value});
                }
                const auto& edge = edge_it->second;
                const auto curve_it = state_->curves.find(edge.curve_id.value);
                const auto v0_it = state_->vertices.find(edge.v0.value);
                const auto v1_it = state_->vertices.find(edge.v1.value);
                if (curve_it == state_->curves.end() || v0_it == state_->vertices.end() || v1_it == state_->vertices.end() ||
                    (curve_it->second.kind != detail::CurveKind::Line &&
                     curve_it->second.kind != detail::CurveKind::LineSegment)) {
                    return fail("heal.auto_repair.planar.extract", "平面壳修复遇到缺失顶点或非直线边",
                                {body_id.value, face_id.value, coedge_it->second.edge_id.value, edge.curve_id.value});
                }
                const auto& curve = curve_it->second;
                const bool zero_edge = distance(v0_it->second.point, v1_it->second.point) == 0.0;
                Point3 line_origin = curve.origin;
                Vec3 line_direction = curve.direction;
                if (curve.kind == detail::CurveKind::LineSegment && !zero_edge) {
                    if (curve.poles.size() != 2 || !point3_components_finite(curve.poles.front()) ||
                        !point3_components_finite(curve.poles.back())) {
                        return fail("heal.auto_repair.planar.extract", "平面壳线段支撑端点缺失或非有限",
                                    {body_id.value, face_id.value, edge.curve_id.value});
                    }
                    line_origin = curve.poles.front();
                    line_direction = detail::subtract(curve.poles.back(), curve.poles.front());
                }
                const Scalar direction_length = std::hypot(line_direction.x, line_direction.y, line_direction.z);
                if (!point3_components_finite(v0_it->second.point) || !point3_components_finite(v1_it->second.point) ||
                    !point3_components_finite(line_origin) || !std::isfinite(direction_length) || (!zero_edge && direction_length <= 0.0) ||
                    (edge.has_parameter_interval && (!std::isfinite(edge.start_parameter) || !std::isfinite(edge.end_parameter)))) {
                    return fail("heal.auto_repair.planar.extract", "平面壳修复遇到非有限几何或退化直线",
                                {body_id.value, face_id.value, edge.curve_id.value, edge.v0.value, edge.v1.value});
                }
                const Vec3 direction = zero_edge ? Vec3 {} : detail::scale(line_direction, 1.0 / direction_length);
                if (!zero_edge && (curve.kind == detail::CurveKind::LineSegment || edge.has_parameter_interval)) {
                    const Scalar t0 = edge.has_parameter_interval ? edge.start_parameter : 0.0;
                    const Scalar t1 = edge.has_parameter_interval ? edge.end_parameter : 1.0;
                    if (t0 == t1 || (curve.kind == detail::CurveKind::LineSegment &&
                        (t0 < 0.0 || t0 > 1.0 || t1 < 0.0 || t1 > 1.0))) {
                        return fail("heal.auto_repair.planar.extract", "平面壳边参数范围退化或超出直线段支撑域",
                                    {body_id.value, face_id.value, edge.curve_id.value});
                    }
                    const auto p0 = detail::add_point_vec(line_origin, detail::scale(line_direction, t0));
                    const auto p1 = detail::add_point_vec(line_origin, detail::scale(line_direction, t1));
                    if (!point3_components_finite(p0) || !point3_components_finite(p1) ||
                        distance(p0, v0_it->second.point) > tolerance || distance(p1, v1_it->second.point) > tolerance) {
                        return fail("heal.auto_repair.planar.extract", "平面壳边参数端点与拓扑顶点不一致",
                                    {body_id.value, face_id.value, edge.curve_id.value, edge.v0.value, edge.v1.value},
                                    {{"linear_tolerance", tolerance, "model_unit"}});
                    }
                }
                for (const auto vertex_id : {edge.v0, edge.v1}) {
                    const auto point = state_->vertices.at(vertex_id.value).point;
                    const Scalar line_residual = zero_edge ? 0.0 : detail::norm(detail::cross(detail::subtract(point, line_origin), direction));
                    const Scalar plane_residual = std::abs(detail::dot(detail::subtract(point, surface.origin), polygon.plane_normal));
                    if (!std::isfinite(line_residual) || !std::isfinite(plane_residual) ||
                        line_residual > tolerance || plane_residual > tolerance) {
                        return fail("heal.auto_repair.planar.extract", "平面壳修复输入边或顶点偏离支撑几何超过容差",
                                    {body_id.value, face_id.value, vertex_id.value},
                                    {{"linear_tolerance", tolerance, "model_unit"}});
                    }
                    input_points.emplace(vertex_id.value, point);
                }
                polygon.segments.emplace_back(coedge_it->second.reversed ? edge.v1 : edge.v0,
                                              coedge_it->second.reversed ? edge.v0 : edge.v1);
            }
            polygons.push_back(std::move(polygon));
        }
        if (polygons.size() < 6) {
            return fail("heal.auto_repair.planar.extract", "第一代平面壳修复要求至少六个唯一面",
                        {body_id.value, shell_id.value},
                        {{"unique_face_count", static_cast<Scalar>(polygons.size()), "count"},
                         {"minimum_face_count", 6.0, "count"}});
        }
        // A fixed first representative bounds every move by tolerance. Checking all
        // cluster members rejects chain welding and order-dependent growing gaps.
        std::vector<Point3> points;
        std::vector<std::vector<Point3>> clusters;
        std::unordered_map<std::uint64_t, std::size_t> vertex_node;
        Scalar maximum_displacement = 0.0;
        for (const auto& [vertex_id, point] : input_points) {
            std::size_t candidate = points.size();
            for (std::size_t i = 0; i < points.size(); ++i) {
                const Scalar d = distance(point, points[i]);
                if (!std::isfinite(d)) {
                    return fail("heal.auto_repair.planar.weld", "平面壳焊接坐标差溢出", {body_id.value, vertex_id});
                }
                if (d > tolerance) continue;
                if (candidate != points.size() || std::any_of(clusters[i].begin(), clusters[i].end(),
                        [&](const Point3& member) { return distance(point, member) > tolerance; })) {
                    return fail("heal.auto_repair.planar.weld", "平面壳焊接存在多候选或链式容差漂移",
                                {body_id.value, vertex_id}, {{"linear_tolerance", tolerance, "model_unit"}});
                }
                candidate = i;
            }
            if (candidate == points.size()) {
                points.push_back(point);
                clusters.push_back({});
            }
            maximum_displacement = std::max(maximum_displacement, distance(point, points[candidate]));
            clusters[candidate].push_back(point);
            vertex_node.emplace(vertex_id, candidate);
        }
        std::size_t zero_nodes = 0;
        for (auto& polygon : polygons) {
            for (std::size_t i = 0; i < polygon.segments.size(); ++i) {
                const auto segment = polygon.segments[i];
                const auto next = polygon.segments[(i + 1) % polygon.segments.size()];
                const Scalar gap = distance(input_points.at(segment.second.value), input_points.at(next.first.value));
                if (!std::isfinite(gap) || gap > tolerance ||
                    vertex_node.at(segment.second.value) != vertex_node.at(next.first.value)) {
                    return fail("heal.auto_repair.planar.weld", "平面壳外环间隙超过容差或无法唯一焊接",
                                {body_id.value, polygon.source_face.value, segment.second.value, next.first.value},
                                {{"linear_tolerance", tolerance, "model_unit"}});
                }
                const auto start = vertex_node.at(segment.first.value);
                const auto end = vertex_node.at(segment.second.value);
                if (start == end) {
                    ++zero_nodes;
                    continue;
                }
                polygon.nodes.push_back(start);
            }
            std::unordered_set<std::size_t> unique_nodes(polygon.nodes.begin(), polygon.nodes.end());
            if (polygon.nodes.size() < 3 || unique_nodes.size() != polygon.nodes.size()) {
                return fail("heal.auto_repair.planar.weld", "平面壳焊接后外环退化或重复经过非连续顶点",
                            {body_id.value, polygon.source_face.value},
                            {{"remaining_node_count", static_cast<Scalar>(polygon.nodes.size()), "count"}});
            }
        }
        using EdgeKey = std::pair<std::size_t, std::size_t>;
        struct EdgeUse { std::size_t face; bool forward; };
        std::map<EdgeKey, std::vector<EdgeUse>> uses;
        for (std::size_t f = 0; f < polygons.size(); ++f) {
            const auto& nodes = polygons[f].nodes;
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                const auto a = nodes[i];
                const auto b = nodes[(i + 1) % nodes.size()];
                uses[{std::min(a, b), std::max(a, b)}].push_back({f, a < b});
            }
        }
        std::vector<std::vector<std::pair<std::size_t, bool>>> adjacency(polygons.size());
        for (const auto& [key, edge_uses] : uses) {
            (void)key;
            if (edge_uses.size() != 2 || edge_uses[0].face == edge_uses[1].face) {
                return fail("heal.auto_repair.planar.orient", "平面壳焊接后仍存在开放或非流形边",
                            {body_id.value, shell_id.value, polygons[edge_uses.front().face].source_face.value},
                            {{"edge_use_count", static_cast<Scalar>(edge_uses.size()), "count"},
                             {"linear_tolerance", tolerance, "model_unit"}});
            }
            const bool opposite_flip = edge_uses[0].forward == edge_uses[1].forward;
            adjacency[edge_uses[0].face].push_back({edge_uses[1].face, opposite_flip});
            adjacency[edge_uses[1].face].push_back({edge_uses[0].face, opposite_flip});
        }
        std::vector<int> flips(polygons.size(), -1);
        std::vector<std::size_t> queue {0};
        flips[0] = 0;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const auto face = queue[head];
            for (const auto& [neighbor, opposite_flip] : adjacency[face]) {
                const int needed = flips[face] ^ static_cast<int>(opposite_flip);
                if (flips[neighbor] == -1) {
                    flips[neighbor] = needed;
                    queue.push_back(neighbor);
                } else if (flips[neighbor] != needed) {
                    return fail("heal.auto_repair.planar.orient", "平面壳存在矛盾的定向约束",
                                {body_id.value, polygons[face].source_face.value, polygons[neighbor].source_face.value});
                }
            }
        }
        if (queue.size() != polygons.size()) {
            return fail("heal.auto_repair.planar.orient", "平面壳包含互不连接的面组件", {body_id.value, shell_id.value});
        }
        const Point3 volume_origin = points.front();
        long double volume_six = 0.0L;
        for (std::size_t f = 0; f < polygons.size(); ++f) {
            if (flips[f]) std::reverse(polygons[f].nodes.begin(), polygons[f].nodes.end());
            const auto& nodes = polygons[f].nodes;
            const Vec3 a = detail::subtract(points[nodes.front()], volume_origin);
            for (std::size_t i = 1; i + 1 < nodes.size(); ++i) {
                const Vec3 b = detail::subtract(points[nodes[i]], volume_origin);
                const Vec3 c = detail::subtract(points[nodes[i + 1]], volume_origin);
                volume_six += static_cast<long double>(a.x) * (static_cast<long double>(b.y) * c.z - static_cast<long double>(b.z) * c.y)
                            + static_cast<long double>(a.y) * (static_cast<long double>(b.z) * c.x - static_cast<long double>(b.x) * c.z)
                            + static_cast<long double>(a.z) * (static_cast<long double>(b.x) * c.y - static_cast<long double>(b.y) * c.x);
            }
        }
        const Scalar signed_volume = static_cast<Scalar>(std::abs(volume_six) / 6.0L);
        if (!std::isfinite(signed_volume) || signed_volume <= 0.0) {
            return fail("heal.auto_repair.planar.orient", "平面壳定向体积非有限或退化", {body_id.value, shell_id.value});
        }
        if (volume_six < 0.0L) {
            for (std::size_t f = 0; f < polygons.size(); ++f) {
                std::reverse(polygons[f].nodes.begin(), polygons[f].nodes.end());
                flips[f] ^= 1;
            }
        }
        // Do not flatten a warped ring into a replacement plane. Restrict rebuild
        // to planar polygons, with a scale-relative floating-point residual only.
        std::vector<Vec3> normals;
        for (const auto& polygon : polygons) {
            const Point3 origin = points[polygon.nodes.front()];
            Vec3 normal {};
            Scalar scale = 0.0;
            for (std::size_t i = 1; i + 1 < polygon.nodes.size(); ++i) {
                const auto triangle_normal = detail::cross(detail::subtract(points[polygon.nodes[i]], origin),
                                                           detail::subtract(points[polygon.nodes[i + 1]], origin));
                normal.x += triangle_normal.x;
                normal.y += triangle_normal.y;
                normal.z += triangle_normal.z;
            }
            const Scalar n_length = std::hypot(normal.x, normal.y, normal.z);
            if (!std::isfinite(n_length) || n_length <= 0.0) {
                return fail("heal.auto_repair.planar.rebuild", "平面壳焊接后面面积退化", {body_id.value, polygon.source_face.value});
            }
            normal = detail::scale(normal, 1.0 / n_length);
            for (const auto node : polygon.nodes) scale = std::max(scale, distance(points[node], origin));
            const Scalar residual_limit = std::numeric_limits<Scalar>::epsilon() * scale * 128.0;
            for (const auto node : polygon.nodes) {
                const Scalar residual = std::abs(detail::dot(detail::subtract(points[node], origin), normal));
                const Scalar old_plane_residual = std::abs(detail::dot(detail::subtract(points[node], polygon.plane_origin), polygon.plane_normal));
                if (!std::isfinite(residual) || !std::isfinite(old_plane_residual) || residual > residual_limit || old_plane_residual > tolerance) {
                    return fail("heal.auto_repair.planar.rebuild", "平面壳焊接后面不共面或偏离原支撑面超过容差",
                                {body_id.value, polygon.source_face.value}, {{"linear_tolerance", tolerance, "model_unit"}});
                }
            }
            // A nonconsecutive repeated vertex is already rejected above; also
            // reject crossed or overlapping edges instead of triangulating a bowtie.
            const int drop_axis = std::abs(normal.x) >= std::abs(normal.y) && std::abs(normal.x) >= std::abs(normal.z)
                                ? 0 : (std::abs(normal.y) >= std::abs(normal.z) ? 1 : 2);
            auto project = [&](std::size_t node) {
                const Vec3 p = detail::subtract(points[node], origin);
                return drop_axis == 0 ? std::array<long double, 2> {p.y, p.z}
                     : drop_axis == 1 ? std::array<long double, 2> {p.x, p.z}
                                      : std::array<long double, 2> {p.x, p.y};
            };
            auto turn = [](const std::array<long double, 2>& p, const std::array<long double, 2>& q,
                           const std::array<long double, 2>& r) {
                return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
            };
            const long double area_noise = static_cast<long double>(residual_limit) * scale;
            for (std::size_t i = 0; i < polygon.nodes.size(); ++i) {
                const auto p = project(polygon.nodes[i]);
                const auto q = project(polygon.nodes[(i + 1) % polygon.nodes.size()]);
                for (std::size_t j = i + 1; j < polygon.nodes.size(); ++j) {
                    if (j == i + 1 || (i == 0 && j + 1 == polygon.nodes.size())) continue;
                    const auto r = project(polygon.nodes[j]);
                    const auto t = project(polygon.nodes[(j + 1) % polygon.nodes.size()]);
                    if (std::max(std::min(p[0], q[0]), std::min(r[0], t[0])) > std::min(std::max(p[0], q[0]), std::max(r[0], t[0])) + residual_limit ||
                        std::max(std::min(p[1], q[1]), std::min(r[1], t[1])) > std::min(std::max(p[1], q[1]), std::max(r[1], t[1])) + residual_limit) continue;
                    const auto o1 = turn(p, q, r);
                    const auto o2 = turn(p, q, t);
                    const auto o3 = turn(r, t, p);
                    const auto o4 = turn(r, t, q);
                    const bool same_side_1 = (o1 > area_noise && o2 > area_noise) || (o1 < -area_noise && o2 < -area_noise);
                    const bool same_side_2 = (o3 > area_noise && o4 > area_noise) || (o3 < -area_noise && o4 < -area_noise);
                    if (!same_side_1 && !same_side_2) {
                        return fail("heal.auto_repair.planar.rebuild", "平面壳外环存在非相邻边交叉、接触或重叠",
                                    {body_id.value, polygon.source_face.value},
                                    {{"first_segment", static_cast<Scalar>(i), "index"},
                                     {"second_segment", static_cast<Scalar>(j), "index"}});
                    }
                }
            }
            normals.push_back(normal);
        }
        std::vector<VertexId> vertices;
        for (const auto& point : points) {
            const auto id = VertexId {state_->allocate_id()};
            state_->vertices.emplace(id.value, detail::VertexRecord {point});
            vertices.push_back(id);
        }
        std::vector<EdgeId> edges;
        std::map<EdgeKey, int> edge_indices;
        for (const auto& [key, edge_uses] : uses) {
            (void)edge_uses;
            const auto curve = detail::create_materialized_line(*state_, points[key.first], points[key.second]);
            const auto edge = EdgeId {state_->allocate_id()};
            state_->edges.emplace(edge.value, detail::EdgeRecord {curve, vertices[key.first], vertices[key.second]});
            edge_indices.emplace(key, static_cast<int>(edges.size()));
            edges.push_back(edge);
        }
        detail::ShellRecord rebuilt_shell;
        rebuilt_shell.source_shells = {shell_id};
        SurfaceService surface_service {state_};
        for (std::size_t f = 0; f < polygons.size(); ++f) {
            const auto& polygon = polygons[f];
            std::vector<std::pair<int, bool>> refs;
            for (std::size_t i = 0; i < polygon.nodes.size(); ++i) {
                const auto a = polygon.nodes[i];
                const auto b = polygon.nodes[(i + 1) % polygon.nodes.size()];
                refs.emplace_back(edge_indices.at({std::min(a, b), std::max(a, b)}), a > b);
            }
            const std::array<FaceId, 1> sources {polygon.source_face};
            const auto face = detail::create_materialized_polygon_face(*state_, edges, refs, normals[f], sources);
            if (face.value == 0) {
                return fail("heal.auto_repair.planar.rebuild", "平面壳无法重建派生面", {body_id.value, polygon.source_face.value});
            }
            const auto& rebuilt_face = state_->faces.at(face.value);
            for (const auto coedge_id : state_->loops.at(rebuilt_face.outer_loop.value).coedges) {
                auto& coedge = state_->coedges.at(coedge_id.value);
                const auto& edge = state_->edges.at(coedge.edge_id.value);
                // PCurve follows the underlying edge; loop consumers apply the
                // coedge direction when walking this canonical endpoint pair.
                const auto uv0 = surface_service.closest_uv(rebuilt_face.surface_id,
                                                            state_->vertices.at(edge.v0.value).point);
                const auto uv1 = surface_service.closest_uv(rebuilt_face.surface_id,
                                                            state_->vertices.at(edge.v1.value).point);
                if (!uv0.value || !uv1.value || uv0.status != StatusCode::Ok || uv1.status != StatusCode::Ok ||
                    !std::isfinite(uv0.value->first) || !std::isfinite(uv0.value->second) ||
                    !std::isfinite(uv1.value->first) || !std::isfinite(uv1.value->second)) {
                    return fail("heal.auto_repair.planar.rebuild", "平面壳派生边无法生成有限参数曲线",
                                {body_id.value, polygon.source_face.value, face.value, coedge_id.value});
                }
                detail::PCurveRecord pcurve;
                pcurve.poles = {{uv0.value->first, uv0.value->second}, {uv1.value->first, uv1.value->second}};
                const auto pcurve_id = PCurveId {state_->allocate_id()};
                state_->pcurves.emplace(pcurve_id.value, std::move(pcurve));
                coedge.pcurve_id = pcurve_id;
            }
            rebuilt_shell.faces.push_back(face);
            rebuilt_shell.source_faces.push_back(polygon.source_face);
        }
        const auto output_shell = ShellId {state_->allocate_id()};
        state_->shells.emplace(output_shell.value, std::move(rebuilt_shell));
        detail::BodyRecord rebuilt_body;
        // This shell is rebuilt from real planar rings, rather than the bbox
        // placeholder associated with legacy Modified bodies.
        rebuilt_body.kind = detail::BodyKind::Generic;
        rebuilt_body.rep_kind = RepKind::ExactBRep;
        rebuilt_body.label = "auto_repair_planar";
        rebuilt_body.shells = {output_shell};
        rebuilt_body.source_bodies = {body_id};
        rebuilt_body.source_shells = {shell_id};
        for (const auto& polygon : polygons) rebuilt_body.source_faces.push_back(polygon.source_face);
        rebuilt_body.bbox = detail::compute_faces_bbox(*state_, state_->shells.at(output_shell.value).faces);
        const auto output = BodyId {state_->allocate_id()};
        state_->bodies.emplace(output.value, std::move(rebuilt_body));
        detail::rebuild_topology_links(*state_);
        const auto post = validation.validate_all(output, ValidationMode::Strict);
        if (post.status != StatusCode::Ok) {
            auto rejected = fail("heal.auto_repair.planar.post_validate", "平面壳修复结果未通过Strict验证，已撤销全部派生对象",
                                 {body_id.value, output.value}, {{"child_status_code", static_cast<Scalar>(post.status), "enum"},
                                                                {"child_diagnostic_id", static_cast<Scalar>(post.diagnostic_id.value), "id"}});
            const auto child = state_->diagnostics.find(post.diagnostic_id.value);
            if (child != state_->diagnostics.end()) {
                const auto issues = child->second.issues;
                for (auto issue : issues) {
                    if (issue.related_entities.empty()) issue.related_entities = {body_id.value, output.value};
                    if (issue.stage.empty()) issue.stage = "heal.auto_repair.planar.post_validate";
                    if ((issue.severity == IssueSeverity::Error || issue.severity == IssueSeverity::Fatal) &&
                        issue.numeric_evidence.empty()) {
                        issue.numeric_evidence = heal_failure_evidence(post.status, issue.related_entities.size());
                    }
                    state_->append_diagnostic_issue(rejected.diagnostic_id, std::move(issue));
                }
            }
            return rejected;
        }
        const auto diag = state_->create_diagnostic("真实平面壳自动修复与Strict再验证完成");
        append_repair_pipeline_trace(*state_, diag, "auto_repair", {body_id.value, output.value});
        auto issue = detail::make_info_issue(diag_codes::kHealRepairValidated, "间隙焊接、方向传播与重复/零长节点清理后Strict验证通过");
        issue.stage = "heal.auto_repair.planar.post_validate";
        issue.related_entities = {body_id.value, shell_id.value, output.value};
        issue.numeric_evidence = {{"linear_tolerance", tolerance, "model_unit"},
                                  {"maximum_vertex_displacement", maximum_displacement, "model_unit"},
                                  {"welded_vertex_count", static_cast<Scalar>(input_points.size() - points.size()), "count"},
                                  {"removed_duplicate_face_count", static_cast<Scalar>(duplicate_faces), "count"},
                                  {"removed_zero_length_node_count", static_cast<Scalar>(zero_nodes), "count"},
                                  {"reoriented_face_count", static_cast<Scalar>(std::count(flips.begin(), flips.end(), 1)), "count"},
                                  {"signed_volume", signed_volume, "model_unit^3"},
                                  {"pre_validation_diagnostic_id", static_cast<Scalar>(pre_validation.diagnostic_id.value), "id"}};
        state_->append_diagnostic_issue(diag, std::move(issue));
        append_repair_replay_summary_issue(*state_, diag);
        detail::invalidate_eval_for_bodies(*state_, {body_id});
        rollback.commit();
        return ok_result(make_repair_report(StatusCode::Ok, output, diag, {}), diag);
    }
    if ((!has_valid_bbox(source) || min_extent(source.bbox) <= 0.0) && mode != RepairMode::Aggressive) {
        return heal_failed_result<OpReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kHealAutoRepairFailure,
            "安全自动修复不能以合成包围盒替代缺失的真实模型", "自动修复失败", {body_id.value},
            "heal.auto_repair.planar.input", {{"owned_shell_count", static_cast<Scalar>(source.shells.size()), "count"},
                                              {"repair_mode", static_cast<Scalar>(mode), "enum"}});
    }
    auto bbox = state_->bodies.at(body_id.value).bbox;
    if (!has_valid_bbox(state_->bodies.at(body_id.value)) || min_extent(state_->bodies.at(body_id.value).bbox) <= 0.0) {
        bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});
    }
    const auto output = clone_modified_body(state_, body_id, "auto_repair", bbox);
    const auto diag = state_->create_diagnostic("自动修复执行中");
    append_repair_pipeline_trace(*state_, diag, "auto_repair", {body_id.value, output.value});
    if (!has_valid_bbox(source) || min_extent(source.bbox) <= 0.0) {
        auto compatibility = detail::make_warning_issue(diag_codes::kHealFeatureRemovedWarning,
            "Aggressive仅对无owned拓扑的历史导入元数据使用合成单位包围盒，此结果不证明原几何已修复");
        compatibility.stage = "heal.auto_repair.metadata_bbox";
        compatibility.related_entities = {body_id.value, output.value};
        compatibility.numeric_evidence = {{"synthetic_bbox", 1.0, "bool"}};
        state_->append_diagnostic_issue(diag, std::move(compatibility));
    }
    if (pre_validation.status != StatusCode::Ok && pre_validation.diagnostic_id.value != 0) {
        const auto pre_diag_it = state_->diagnostics.find(pre_validation.diagnostic_id.value);
        if (pre_diag_it != state_->diagnostics.end()) {
            for (const auto& issue : pre_diag_it->second.issues) {
                auto tracked = with_related_entities(issue, {body_id.value});
                if (tracked.stage.empty()) {
                    tracked.stage = "heal.auto_repair.pre_validate";
                }
                if ((tracked.severity == IssueSeverity::Error || tracked.severity == IssueSeverity::Fatal) &&
                    tracked.numeric_evidence.empty()) {
                    tracked.numeric_evidence = heal_failure_evidence(
                        pre_validation.status, tracked.related_entities.size(),
                        {{"repair_mode", static_cast<Scalar>(mode), "enum"}});
                }
                state_->append_diagnostic_issue(diag, std::move(tracked));
            }
        }
    }
    auto feature_removed_issue = detail::make_warning_issue(diag_codes::kHealFeatureRemovedWarning, "自动修复可能改变局部小特征");
    feature_removed_issue.related_entities = {body_id.value, output.value};
    feature_removed_issue.stage = "heal.auto_repair";
    state_->append_diagnostic_issue(diag, std::move(feature_removed_issue));
    const auto post_validation = validation.validate_all(output, ValidationMode::Standard);
    if (post_validation.status == StatusCode::Ok) {
        Issue validated_issue;
        validated_issue.code = std::string(diag_codes::kHealRepairValidated);
        validated_issue.severity = IssueSeverity::Info;
        validated_issue.message = "自动修复后验证通过";
        validated_issue.related_entities = {body_id.value, output.value};
        validated_issue.stage = "heal.auto_repair.post_validate";
        state_->append_diagnostic_issue(diag, std::move(validated_issue));
        append_repair_replay_summary_issue(*state_, diag);
        detail::invalidate_eval_for_bodies(*state_, {body_id});
        state_->diagnostics.at(diag.value).summary = "已完成自动修复";
        rollback.commit();
        return ok_result(make_repair_report(StatusCode::Ok, output, diag,
                                            {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "自动修复可能改变局部小特征")}),
                         diag);
    }

    if (post_validation.diagnostic_id.value != 0) {
        const auto post_diag_it = state_->diagnostics.find(post_validation.diagnostic_id.value);
        if (post_diag_it != state_->diagnostics.end()) {
            for (const auto& issue : post_diag_it->second.issues) {
                auto tracked = with_related_entities(issue, {output.value});
                if (tracked.severity == IssueSeverity::Error || tracked.severity == IssueSeverity::Fatal) {
                    tracked.stage = "heal.auto_repair.post_validate";
                    if (tracked.numeric_evidence.empty()) {
                        tracked.numeric_evidence = heal_failure_evidence(
                            post_validation.status, tracked.related_entities.size(),
                            {{"repair_mode", static_cast<Scalar>(mode), "enum"},
                             {"rollback_applied", 1.0, "bool"}});
                    }
                }
                state_->append_diagnostic_issue(diag, std::move(tracked));
            }
        }
    }
    Issue failure_issue = make_heal_failure_issue(
        diag_codes::kHealAutoRepairFailure,
        "自动修复后模型仍未通过验证，已回滚派生体及其物化拓扑",
        {body_id.value, output.value}, "heal.auto_repair.post_validate",
        StatusCode::OperationFailed,
        {{"repair_mode", static_cast<Scalar>(mode), "enum"},
         {"allocated_object_count", static_cast<Scalar>(state_->next_id - rollback.first_allocated_id()), "count"},
         {"rollback_applied", 1.0, "bool"},
         {"post_validation_status", static_cast<Scalar>(post_validation.status), "enum"}});
    state_->append_diagnostic_issue(diag, std::move(failure_issue));
    append_repair_replay_summary_issue(*state_, diag);
    state_->diagnostics.at(diag.value).summary = "自动修复失败并已回滚";
    return error_result<OpReport>(
        StatusCode::OperationFailed, diag,
        {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "自动修复可能改变局部小特征")});
}

Result<Scalar> RepairService::estimate_adaptive_linear_threshold(
    BodyId body_id, Scalar input, RepairMode mode) const {
  if (!detail::has_body(*state_, body_id) || !std::isfinite(input) || input <= 0.0) {
    return heal_failed_result<Scalar>(
        *state_, StatusCode::InvalidInput, diag_codes::kHealSmallFaceFailure,
        "线性阈值估计失败：目标体不存在或输入阈值非法", "线性阈值估计失败",
        {body_id.value}, "heal.estimate_linear_threshold.input",
        {{"body_exists", detail::has_body(*state_, body_id) ? 1.0 : 0.0, "bool"},
         {"input_is_finite", std::isfinite(input) ? 1.0 : 0.0, "bool"},
         {"input_is_positive", input > 0.0 ? 1.0 : 0.0, "bool"}});
  }
  return ok_result(adaptive_linear_threshold(*state_, body_id, input, mode),
                   state_->create_diagnostic("已估计线性阈值"));
}

Result<Scalar> RepairService::estimate_adaptive_angle_threshold(
    BodyId body_id, Scalar input, RepairMode mode) const {
  if (!detail::has_body(*state_, body_id) || !std::isfinite(input) || input <= 0.0) {
    return heal_failed_result<Scalar>(
        *state_, StatusCode::InvalidInput, diag_codes::kHealAutoRepairFailure,
        "角度阈值估计失败：目标体不存在或输入阈值非法", "角度阈值估计失败",
        {body_id.value}, "heal.estimate_angle_threshold.input",
        {{"body_exists", detail::has_body(*state_, body_id) ? 1.0 : 0.0, "bool"},
         {"input_is_finite", std::isfinite(input) ? 1.0 : 0.0, "bool"},
         {"input_is_positive", input > 0.0 ? 1.0 : 0.0, "bool"}});
  }
  return ok_result(adaptive_angle_threshold(*state_, body_id, input, mode),
                   state_->create_diagnostic("已估计角度阈值"));
}

Result<OpReport> RepairService::sew_faces_default(std::span<const FaceId> faces) {
  return sew_faces(faces, detail::resolve_linear_tolerance(0.0, state_->config.tolerance),
                    RepairMode::Safe);
}

Result<OpReport> RepairService::remove_small_edges_default(BodyId body_id,
                                                           Scalar threshold) {
  return remove_small_edges(body_id, threshold, RepairMode::Safe);
}

Result<OpReport> RepairService::remove_small_faces_default(BodyId body_id,
                                                           Scalar threshold) {
  return remove_small_faces(body_id, threshold, RepairMode::Safe);
}

Result<OpReport> RepairService::merge_near_coplanar_faces_default(
    BodyId body_id, Scalar angle_tolerance) {
  return merge_near_coplanar_faces(body_id, angle_tolerance, RepairMode::Safe);
}

Result<OpReport> RepairService::auto_repair_default(BodyId body_id) {
  return auto_repair(body_id, RepairMode::Safe);
}

Result<std::vector<OpReport>> RepairService::repair_many_auto(
    std::span<const BodyId> body_ids, RepairMode mode) {
  if (body_ids.empty()) {
    return heal_failed_result<std::vector<OpReport>>(
        *state_, StatusCode::InvalidInput, diag_codes::kHealAutoRepairFailure,
        "批量自动修复失败：输入为空", "批量自动修复失败", {0},
        "heal.repair_many_auto.input", {{"body_count", 0.0, "count"}});
  }
  HealModelMutationRollback rollback {*state_, true};
  std::vector<OpReport> out;
  out.reserve(body_ids.size());
  for (const auto body_id : body_ids) {
    const auto r = auto_repair(body_id, mode);
    if (r.status != StatusCode::Ok || !r.value.has_value()) {
      auto rejected = heal_failed_result<std::vector<OpReport>>(
          *state_, r.status, diag_codes::kHealAutoRepairFailure,
          "批量自动修复失败：子项失败，已回滚此前全部派生结果", "批量自动修复失败",
          {body_id.value}, "heal.repair_many_auto.rollback",
          {{"completed_item_count", static_cast<Scalar>(out.size()), "count"},
           {"requested_item_count", static_cast<Scalar>(body_ids.size()), "count"},
           {"allocated_object_count", static_cast<Scalar>(state_->next_id - rollback.first_allocated_id()), "count"},
           {"rollback_applied", 1.0, "bool"}});
      const auto child = state_->diagnostics.find(r.diagnostic_id.value);
      if (child != state_->diagnostics.end()) {
        // Keep the item failure stage alongside the batch rollback evidence.
        // Replacing it with the batch stage would conceal the actual repair rule.
        const auto issues = child->second.issues;
        for (auto issue : issues) {
          if (issue.related_entities.empty()) issue.related_entities = {body_id.value};
          if (issue.stage.empty()) issue.stage = "heal.repair_many_auto.item";
          if ((issue.severity == IssueSeverity::Error || issue.severity == IssueSeverity::Fatal) &&
              issue.numeric_evidence.empty()) {
            issue.numeric_evidence = heal_failure_evidence(r.status, issue.related_entities.size());
          }
          state_->append_diagnostic_issue(rejected.diagnostic_id, std::move(issue));
        }
      }
      return rejected;
    }
    out.push_back(*r.value);
  }
  rollback.commit();
  return ok_result(std::move(out), state_->create_diagnostic("已批量自动修复"));
}

Result<std::vector<OpReport>> RepairService::repair_many_remove_small_edges(
    std::span<const BodyId> body_ids, Scalar threshold, RepairMode mode) {
  if (body_ids.empty()) {
    return heal_failed_result<std::vector<OpReport>>(
        *state_, StatusCode::InvalidInput, diag_codes::kHealSmallEdgeFailure,
        "批量小边清理失败：输入为空", "批量小边清理失败", {0},
        "heal.repair_many_remove_small_edges.input", {{"body_count", 0.0, "count"}});
  }
  HealModelMutationRollback rollback {*state_, true};
  std::vector<OpReport> out;
  out.reserve(body_ids.size());
  for (const auto body_id : body_ids) {
    const auto r = remove_small_edges(body_id, threshold, mode);
    if (r.status != StatusCode::Ok || !r.value.has_value()) {
      return heal_failed_result<std::vector<OpReport>>(
          *state_, r.status, diag_codes::kHealSmallEdgeFailure,
          "批量小边清理失败：子项失败，已回滚此前全部派生结果", "批量小边清理失败",
          {body_id.value}, "heal.repair_many_remove_small_edges.rollback",
          {{"completed_item_count", static_cast<Scalar>(out.size()), "count"},
           {"requested_item_count", static_cast<Scalar>(body_ids.size()), "count"},
           {"rollback_applied", 1.0, "bool"}});
    }
    out.push_back(*r.value);
  }
  rollback.commit();
  return ok_result(std::move(out),
                   state_->create_diagnostic("已批量执行小边清理"));
}

Result<std::vector<OpReport>> RepairService::repair_many_remove_small_faces(
    std::span<const BodyId> body_ids, Scalar threshold, RepairMode mode) {
  if (body_ids.empty()) {
    return heal_failed_result<std::vector<OpReport>>(
        *state_, StatusCode::InvalidInput, diag_codes::kHealSmallFaceFailure,
        "批量小面清理失败：输入为空", "批量小面清理失败", {0},
        "heal.repair_many_remove_small_faces.input", {{"body_count", 0.0, "count"}});
  }
  HealModelMutationRollback rollback {*state_, true};
  std::vector<OpReport> out;
  out.reserve(body_ids.size());
  for (const auto body_id : body_ids) {
    const auto r = remove_small_faces(body_id, threshold, mode);
    if (r.status != StatusCode::Ok || !r.value.has_value()) {
      return heal_failed_result<std::vector<OpReport>>(
          *state_, r.status, diag_codes::kHealSmallFaceFailure,
          "批量小面清理失败：子项失败，已回滚此前全部派生结果", "批量小面清理失败",
          {body_id.value}, "heal.repair_many_remove_small_faces.rollback",
          {{"completed_item_count", static_cast<Scalar>(out.size()), "count"},
           {"requested_item_count", static_cast<Scalar>(body_ids.size()), "count"},
           {"rollback_applied", 1.0, "bool"}});
    }
    out.push_back(*r.value);
  }
  rollback.commit();
  return ok_result(std::move(out),
                   state_->create_diagnostic("已批量执行小面清理"));
}

Result<std::vector<OpReport>>
RepairService::repair_many_merge_near_coplanar_faces(
    std::span<const BodyId> body_ids, Scalar angle_tolerance, RepairMode mode) {
  if (body_ids.empty()) {
    return heal_failed_result<std::vector<OpReport>>(
        *state_, StatusCode::InvalidInput, diag_codes::kHealAutoRepairFailure,
        "批量近共面合并失败：输入为空", "批量近共面合并失败", {0},
        "heal.repair_many_merge_near_coplanar_faces.input", {{"body_count", 0.0, "count"}});
  }
  HealModelMutationRollback rollback {*state_, true};
  std::vector<OpReport> out;
  out.reserve(body_ids.size());
  for (const auto body_id : body_ids) {
    const auto r = merge_near_coplanar_faces(body_id, angle_tolerance, mode);
    if (r.status != StatusCode::Ok || !r.value.has_value()) {
      return heal_failed_result<std::vector<OpReport>>(
          *state_, r.status, diag_codes::kHealAutoRepairFailure,
          "批量近共面合并失败：子项失败，已回滚此前全部派生结果", "批量近共面合并失败",
          {body_id.value}, "heal.repair_many_merge_near_coplanar_faces.rollback",
          {{"completed_item_count", static_cast<Scalar>(out.size()), "count"},
           {"requested_item_count", static_cast<Scalar>(body_ids.size()), "count"},
           {"rollback_applied", 1.0, "bool"}});
    }
    out.push_back(*r.value);
  }
  rollback.commit();
  return ok_result(std::move(out),
                   state_->create_diagnostic("已批量执行近共面合并"));
}

Result<bool> RepairService::was_modified_output(const OpReport& report) const {
  if (!detail::has_body(*state_, report.output)) {
    return heal_failed_result<bool>(
        *state_, StatusCode::InvalidInput, diag_codes::kHealAutoRepairFailure,
        "修复结果查询失败：输出体不存在", "修复结果查询失败", {report.output.value},
        "heal.query.modified_output", {{"output_exists", 0.0, "bool"}});
  }
  return ok_result(state_->bodies.at(report.output.value).kind ==
                       detail::BodyKind::Modified,
                   state_->create_diagnostic("已查询修复输出类型"));
}

Result<bool> RepairService::output_is_new_body(const OpReport& report,
                                               BodyId input) const {
  if (!detail::has_body(*state_, report.output) || !detail::has_body(*state_, input)) {
    return heal_failed_result<bool>(
        *state_, StatusCode::InvalidInput, diag_codes::kHealAutoRepairFailure,
        "修复输出比较失败：输入或输出体不存在", "修复输出比较失败",
        {input.value, report.output.value}, "heal.query.output_identity",
        {{"input_exists", detail::has_body(*state_, input) ? 1.0 : 0.0, "bool"},
         {"output_exists", detail::has_body(*state_, report.output) ? 1.0 : 0.0, "bool"}});
  }
  return ok_result(report.output.value != input.value,
                   state_->create_diagnostic("已比较修复输出是否新体"));
}

Result<Scalar> RepairService::body_bbox_shrink_ratio(BodyId before,
                                                     BodyId after) const {
  if (!detail::has_body(*state_, before) || !detail::has_body(*state_, after)) {
    return heal_failed_result<Scalar>(
        *state_, StatusCode::InvalidInput, diag_codes::kHealAutoRepairFailure,
        "修复体积比查询失败：输入体不存在", "修复体积比查询失败", {before.value, after.value},
        "heal.query.bbox_shrink_ratio",
        {{"before_exists", detail::has_body(*state_, before) ? 1.0 : 0.0, "bool"},
         {"after_exists", detail::has_body(*state_, after) ? 1.0 : 0.0, "bool"}});
  }
  const auto v0 = min_extent(state_->bodies.at(before.value).bbox);
  const auto v1 = min_extent(state_->bodies.at(after.value).bbox);
  if (v0 <= 0.0) {
    return heal_failed_result<Scalar>(
        *state_, StatusCode::OperationFailed, diag_codes::kValDegenerateGeometry,
        "修复体积比查询失败：基准体尺度无效", "修复体积比查询失败", {before.value, after.value},
        "heal.query.bbox_shrink_ratio", {{"before_minimum_extent", v0, "model_unit"},
                                          {"after_minimum_extent", v1, "model_unit"}});
  }
  return ok_result(v1 / v0, state_->create_diagnostic("已查询修复尺度比"));
}

Result<Scalar> RepairService::compare_bbox_extent_change(BodyId before,
                                                         BodyId after) const {
  if (!detail::has_body(*state_, before) || !detail::has_body(*state_, after)) {
    return heal_failed_result<Scalar>(
        *state_, StatusCode::InvalidInput, diag_codes::kHealAutoRepairFailure,
        "修复尺度变化查询失败：输入体不存在", "修复尺度变化查询失败", {before.value, after.value},
        "heal.query.bbox_extent_change",
        {{"before_exists", detail::has_body(*state_, before) ? 1.0 : 0.0, "bool"},
         {"after_exists", detail::has_body(*state_, after) ? 1.0 : 0.0, "bool"}});
  }
  const auto v0 = min_extent(state_->bodies.at(before.value).bbox);
  const auto v1 = min_extent(state_->bodies.at(after.value).bbox);
  return ok_result(v1 - v0, state_->create_diagnostic("已查询修复尺度变化"));
}

Result<void> RepairService::ensure_valid_after_repair(const OpReport& report,
                                                      ValidationMode mode) const {
  ValidationService validation {state_};
  return validation.validate_all(report.output, mode);
}

Result<std::string> RepairService::summarize_repair(const OpReport& report) const {
  std::ostringstream os;
  os << "status=" << static_cast<int>(report.status) << ", output="
     << report.output.value << ", warnings=" << report.warnings.size();
  const auto dit = state_->diagnostics.find(report.diagnostic_id.value);
  if (dit != state_->diagnostics.end() && !dit->second.issues.empty()) {
    os << ", issue_codes=";
    for (std::size_t i = 0; i < dit->second.issues.size(); ++i) {
      if (i > 0) {
        os << ';';
      }
      os << dit->second.issues[i].code;
    }
    const std::string pipeline_ops = collect_repair_pipeline_ops_ordered(dit->second.issues);
    if (!pipeline_ops.empty()) {
      os << ", pipeline_ops=" << pipeline_ops;
    }
  }
  return ok_result(os.str(), state_->create_diagnostic("已生成修复摘要"));
}
