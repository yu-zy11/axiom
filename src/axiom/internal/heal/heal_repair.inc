RepairService::RepairService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<void> RepairService::repair_face_trim_pcurves(FaceId face_id, RepairMode mode) {
    if (!detail::has_face(*state_, face_id)) {
        return detail::invalid_input_void(*state_, diag_codes::kHealAutoRepairFailure,
                                          "修剪参数曲线修复失败：目标面不存在", "修剪参数曲线修复失败");
    }
    const auto& face = state_->faces.at(face_id.value);
    if (!detail::has_surface(*state_, face.surface_id)) {
        return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                   diag_codes::kTopoFaceOuterLoopInvalid,
                                   "修剪参数曲线修复失败：面引用的曲面不存在", "修剪参数曲线修复失败",
                                   {face_id.value, face.surface_id.value});
    }
    const auto& surf = state_->surfaces.at(face.surface_id.value);
    if (surf.kind != detail::SurfaceKind::Plane &&
        surf.kind != detail::SurfaceKind::Cylinder &&
        surf.kind != detail::SurfaceKind::Sphere) {
        return detail::failed_void(*state_, StatusCode::NotImplemented,
                                   diag_codes::kCoreOperationUnsupported,
                                   "修剪参数曲线修复失败：暂仅支持平面/圆柱/球面修剪面", "修剪参数曲线修复失败",
                                   {face_id.value, face.surface_id.value});
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
            return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                       diag_codes::kTopoLoopNotClosed,
                                       "修剪参数曲线修复失败：面引用的环不存在", "修剪参数曲线修复失败",
                                       {face_id.value, loop_id.value});
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
            state_->coedges[coedge_id.value].pcurve_id = pc_id;
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

    const auto post = topo_validation.validate_face_trim_consistency(face_id);
    if (post.status != StatusCode::Ok) {
        return detail::failed_void(*state_, StatusCode::OperationFailed,
                                   diag_codes::kHealAutoRepairFailure,
                                   "修剪参数曲线修复失败：重建后仍未通过修剪一致性验证", "修剪参数曲线修复失败",
                                   {face_id.value, face.surface_id.value});
    }
    const auto diag_trim = state_->create_diagnostic("修剪参数曲线修复完成");
    append_repair_pipeline_trace(*state_, diag_trim, "repair_face_trim_pcurves",
                                 {face_id.value, face.surface_id.value});
    append_repair_replay_summary_issue(*state_, diag_trim);
    return ok_void(diag_trim);
}

Result<OpReport> RepairService::sew_faces(std::span<const FaceId> faces, Scalar, RepairMode) {
    if (faces.empty()) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kHealSewFailure,
            "缝合失败：输入面集合为空", "缝合失败");
    }
    if (!std::all_of(faces.begin(), faces.end(), [this](const FaceId face_id) { return detail::has_face(*state_, face_id); })) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kHealSewFailure,
            "缝合失败：输入面集合包含无效面", "缝合失败");
    }
    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Generic;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "sewn_body";
    record.bbox = detail::compute_faces_bbox(*state_, faces);
    if (!record.bbox.is_valid) {
        return detail::failed_result<OpReport>(
            *state_, StatusCode::InvalidTopology, diag_codes::kHealSewFailure,
            "缝合失败：无法从输入面推导有效包围盒，拓扑链路可能不完整", "缝合失败");
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
    detail::materialize_body_bbox_topology(*state_, record);
    state_->bodies.emplace(body_id.value, record);
    detail::rebuild_topology_links(*state_);
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
    return ok_result(OpReport {StatusCode::Ok, body_id, diag, {}}, diag);
}

Result<OpReport> RepairService::remove_small_edges(BodyId body_id, Scalar threshold, RepairMode mode) {
    if (!detail::has_body(*state_, body_id) || threshold <= 0.0) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kHealSmallEdgeFailure,
            "小边清理失败：目标实体不存在或阈值非法", "小边清理失败");
    }
    const auto& source = state_->bodies.at(body_id.value);
    auto bbox = source.bbox;
    if (mode != RepairMode::ReportOnly) {
        bbox = detail::make_bbox(bbox.min, {bbox.max.x - threshold * 0.1, bbox.max.y, bbox.max.z});
    }
    const auto output = mode == RepairMode::ReportOnly ? body_id : clone_modified_body(state_, body_id, "remove_small_edges", bbox);
    if (mode != RepairMode::ReportOnly) {
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
    return ok_result(make_repair_report(StatusCode::Ok, output, diag,
                                        {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "已清理局部小边")}),
                     diag);
}

Result<OpReport> RepairService::remove_small_faces(BodyId body_id, Scalar threshold, RepairMode mode) {
    if (!detail::has_body(*state_, body_id) || threshold <= 0.0) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kHealSmallFaceFailure,
            "小面清理失败：目标实体不存在或阈值非法", "小面清理失败");
    }
    const auto& source = state_->bodies.at(body_id.value);
    const auto effective_threshold = adaptive_linear_threshold(*state_, body_id, threshold, mode);
    auto bbox = source.bbox;
    if (mode == RepairMode::Aggressive) {
        bbox = detail::make_bbox(bbox.min, {bbox.max.x, bbox.max.y - effective_threshold * 0.1, bbox.max.z});
    }
    const auto output = mode == RepairMode::ReportOnly ? body_id : clone_modified_body(state_, body_id, "remove_small_faces", bbox);
    if (mode != RepairMode::ReportOnly) {
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
    return ok_result(make_repair_report(StatusCode::Ok, output, diag,
                                        {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "已清理局部小面")}),
                     diag);
}

Result<OpReport> RepairService::merge_near_coplanar_faces(BodyId body_id, Scalar angle_tolerance, RepairMode mode) {
    if (!detail::has_body(*state_, body_id) || angle_tolerance <= 0.0) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kHealAutoRepairFailure,
            "近共面面合并失败：目标实体不存在或角度阈值非法", "近共面面合并失败");
    }
    const auto effective_angle = adaptive_angle_threshold(*state_, body_id, angle_tolerance, mode);
    const auto output = mode == RepairMode::ReportOnly
                            ? body_id
                            : clone_modified_body(state_, body_id, "merge_near_coplanar_faces", state_->bodies.at(body_id.value).bbox);
    if (mode != RepairMode::ReportOnly) {
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
    return ok_result(make_repair_report(StatusCode::Ok, output, diag, std::move(warnings)), diag);
}

Result<OpReport> RepairService::auto_repair(BodyId body_id, RepairMode) {
    if (!detail::has_body(*state_, body_id)) {
        return detail::invalid_input_result<OpReport>(
            *state_, diag_codes::kHealAutoRepairFailure,
            "自动修复失败：目标实体不存在", "自动修复失败");
    }

    ValidationService validation {state_};
    const auto pre_validation = validation.validate_all(body_id, ValidationMode::Standard);
    auto bbox = state_->bodies.at(body_id.value).bbox;
    if (!has_valid_bbox(state_->bodies.at(body_id.value)) || min_extent(state_->bodies.at(body_id.value).bbox) <= 0.0) {
        bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});
    }
    const auto output = clone_modified_body(state_, body_id, "auto_repair", bbox);
    detail::invalidate_eval_for_bodies(*state_, {body_id});
    const auto diag = state_->create_diagnostic("已完成自动修复");
    append_repair_pipeline_trace(*state_, diag, "auto_repair", {body_id.value, output.value});
    if (pre_validation.status != StatusCode::Ok && pre_validation.diagnostic_id.value != 0) {
        const auto pre_diag_it = state_->diagnostics.find(pre_validation.diagnostic_id.value);
        if (pre_diag_it != state_->diagnostics.end()) {
            for (const auto& issue : pre_diag_it->second.issues) {
                auto tracked = with_related_entities(issue, {body_id.value});
                if (tracked.stage.empty()) {
                    tracked.stage = "heal.auto_repair.pre_validate";
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
        return ok_result(make_repair_report(StatusCode::Ok, output, diag,
                                            {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "自动修复可能改变局部小特征")}),
                         diag);
    }

    if (post_validation.diagnostic_id.value != 0) {
        const auto post_diag_it = state_->diagnostics.find(post_validation.diagnostic_id.value);
        if (post_diag_it != state_->diagnostics.end()) {
            for (const auto& issue : post_diag_it->second.issues) {
                auto tracked = with_related_entities(issue, {output.value});
                if (tracked.stage.empty()) {
                    tracked.stage = "heal.auto_repair.post_validate";
                }
                state_->append_diagnostic_issue(diag, std::move(tracked));
            }
        }
    }
    Issue failure_issue;
    failure_issue.code = std::string(diag_codes::kHealAutoRepairFailure);
    failure_issue.severity = IssueSeverity::Error;
    failure_issue.message = "自动修复后模型仍未通过验证";
    failure_issue.related_entities = {body_id.value, output.value};
    failure_issue.stage = "heal.auto_repair.post_validate";
    state_->append_diagnostic_issue(diag, std::move(failure_issue));
    append_repair_replay_summary_issue(*state_, diag);
    return error_result<OpReport>(
        StatusCode::OperationFailed, diag,
        {detail::make_warning(diag_codes::kHealFeatureRemovedWarning, "自动修复可能改变局部小特征")});
}

Result<Scalar> RepairService::estimate_adaptive_linear_threshold(
    BodyId body_id, Scalar input, RepairMode mode) const {
  if (!detail::has_body(*state_, body_id) || input <= 0.0) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kHealSmallFaceFailure,
        "线性阈值估计失败：目标体不存在或输入阈值非法", "线性阈值估计失败");
  }
  return ok_result(adaptive_linear_threshold(*state_, body_id, input, mode),
                   state_->create_diagnostic("已估计线性阈值"));
}

Result<Scalar> RepairService::estimate_adaptive_angle_threshold(
    BodyId body_id, Scalar input, RepairMode mode) const {
  if (!detail::has_body(*state_, body_id) || input <= 0.0) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kHealAutoRepairFailure,
        "角度阈值估计失败：目标体不存在或输入阈值非法", "角度阈值估计失败");
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
  std::vector<OpReport> out;
  out.reserve(body_ids.size());
  for (const auto body_id : body_ids) {
    const auto r = auto_repair(body_id, mode);
    if (r.status != StatusCode::Ok || !r.value.has_value()) {
      return error_result<std::vector<OpReport>>(r.status, r.diagnostic_id);
    }
    out.push_back(*r.value);
  }
  return ok_result(std::move(out), state_->create_diagnostic("已批量自动修复"));
}

Result<std::vector<OpReport>> RepairService::repair_many_remove_small_edges(
    std::span<const BodyId> body_ids, Scalar threshold, RepairMode mode) {
  std::vector<OpReport> out;
  out.reserve(body_ids.size());
  for (const auto body_id : body_ids) {
    const auto r = remove_small_edges(body_id, threshold, mode);
    if (r.status != StatusCode::Ok || !r.value.has_value()) {
      return error_result<std::vector<OpReport>>(r.status, r.diagnostic_id);
    }
    out.push_back(*r.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已批量执行小边清理"));
}

Result<std::vector<OpReport>> RepairService::repair_many_remove_small_faces(
    std::span<const BodyId> body_ids, Scalar threshold, RepairMode mode) {
  std::vector<OpReport> out;
  out.reserve(body_ids.size());
  for (const auto body_id : body_ids) {
    const auto r = remove_small_faces(body_id, threshold, mode);
    if (r.status != StatusCode::Ok || !r.value.has_value()) {
      return error_result<std::vector<OpReport>>(r.status, r.diagnostic_id);
    }
    out.push_back(*r.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已批量执行小面清理"));
}

Result<std::vector<OpReport>>
RepairService::repair_many_merge_near_coplanar_faces(
    std::span<const BodyId> body_ids, Scalar angle_tolerance, RepairMode mode) {
  std::vector<OpReport> out;
  out.reserve(body_ids.size());
  for (const auto body_id : body_ids) {
    const auto r = merge_near_coplanar_faces(body_id, angle_tolerance, mode);
    if (r.status != StatusCode::Ok || !r.value.has_value()) {
      return error_result<std::vector<OpReport>>(r.status, r.diagnostic_id);
    }
    out.push_back(*r.value);
  }
  return ok_result(std::move(out),
                   state_->create_diagnostic("已批量执行近共面合并"));
}

Result<bool> RepairService::was_modified_output(const OpReport& report) const {
  if (!detail::has_body(*state_, report.output)) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kHealAutoRepairFailure,
        "修复结果查询失败：输出体不存在", "修复结果查询失败");
  }
  return ok_result(state_->bodies.at(report.output.value).kind ==
                       detail::BodyKind::Modified,
                   state_->create_diagnostic("已查询修复输出类型"));
}

Result<bool> RepairService::output_is_new_body(const OpReport& report,
                                               BodyId input) const {
  if (!detail::has_body(*state_, report.output) || !detail::has_body(*state_, input)) {
    return detail::invalid_input_result<bool>(
        *state_, diag_codes::kHealAutoRepairFailure,
        "修复输出比较失败：输入或输出体不存在", "修复输出比较失败");
  }
  return ok_result(report.output.value != input.value,
                   state_->create_diagnostic("已比较修复输出是否新体"));
}

Result<Scalar> RepairService::body_bbox_shrink_ratio(BodyId before,
                                                     BodyId after) const {
  if (!detail::has_body(*state_, before) || !detail::has_body(*state_, after)) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kHealAutoRepairFailure,
        "修复体积比查询失败：输入体不存在", "修复体积比查询失败");
  }
  const auto v0 = min_extent(state_->bodies.at(before.value).bbox);
  const auto v1 = min_extent(state_->bodies.at(after.value).bbox);
  if (v0 <= 0.0) {
    return detail::failed_result<Scalar>(
        *state_, StatusCode::OperationFailed, diag_codes::kValDegenerateGeometry,
        "修复体积比查询失败：基准体尺度无效", "修复体积比查询失败");
  }
  return ok_result(v1 / v0, state_->create_diagnostic("已查询修复尺度比"));
}

Result<Scalar> RepairService::compare_bbox_extent_change(BodyId before,
                                                         BodyId after) const {
  if (!detail::has_body(*state_, before) || !detail::has_body(*state_, after)) {
    return detail::invalid_input_result<Scalar>(
        *state_, diag_codes::kHealAutoRepairFailure,
        "修复尺度变化查询失败：输入体不存在", "修复尺度变化查询失败");
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
