ValidationService::ValidationService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<void> ValidationService::validate_geometry(BodyId body_id, ValidationMode mode) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_void(*state_, StatusCode::InvalidInput, diag_codes::kValDegenerateGeometry,
                                   "几何验证失败：目标实体不存在", "几何验证失败");
    }
    const auto& body = state_->bodies.at(body_id.value);
    if (body.bbox.is_valid &&
        (!point3_components_finite(body.bbox.min) || !point3_components_finite(body.bbox.max))) {
        return detail::failed_void(*state_, StatusCode::DegenerateGeometry, diag_codes::kValNonFiniteGeometry,
                                   "几何验证失败：包围盒坐标须为有限值", "几何验证失败", {body_id.value});
    }
    if (!has_valid_bbox(body) || min_extent(body.bbox) <= 0.0) {
        return detail::failed_void(*state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                                   "几何验证失败：目标实体包围盒退化或无效", "几何验证失败");
    }
    // owned B-Rep：句柄完整性（全模式），与 Strict 参数域/退化项解耦，便于 Standard 工作流尽早发现悬空引用。
    if (body.rep_kind == RepKind::ExactBRep && !body.shells.empty() &&
        !(body.kind == detail::BodyKind::Sweep && body.sweep_polyhedral_mass_valid)) {
        std::string ref_reason;
        std::vector<std::uint64_t> ref_related;
        if (!validate_owned_brep_reference_integrity(state_, body_id, ref_reason, ref_related)) {
            return detail::failed_void(*state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                                       "几何验证失败：" + ref_reason, "几何验证失败", std::move(ref_related));
        }
    }
    if (mode == ValidationMode::Strict) {
        // 分片旋转体：拓扑面为平面三角形片，当前曲面域与环 UV 尚未完全工业对齐，Strict 域检查易误报。
        if (!(body.kind == detail::BodyKind::Sweep && body.sweep_polyhedral_mass_valid)) {
            std::string dom_reason;
            std::vector<std::uint64_t> dom_related;
            if (!validate_strict_owned_surface_domains(state_, body_id, dom_reason, dom_related)) {
                return detail::failed_void(*state_, StatusCode::DegenerateGeometry, diag_codes::kValParameterDomainInvalid,
                                           "几何验证失败：" + dom_reason, "几何验证失败", std::move(dom_related));
            }
            if (!validate_strict_owned_curve_and_pcurve_domains(state_, body_id, dom_reason, dom_related)) {
                return detail::failed_void(*state_, StatusCode::DegenerateGeometry, diag_codes::kValParameterDomainInvalid,
                                           "几何验证失败：" + dom_reason, "几何验证失败", std::move(dom_related));
            }
            if (!validate_strict_owned_vertices_finite(state_, body_id, dom_reason, dom_related)) {
                return detail::failed_void(*state_, StatusCode::DegenerateGeometry, diag_codes::kValNonFiniteGeometry,
                                           "几何验证失败：" + dom_reason, "几何验证失败", std::move(dom_related));
            }
            if (!validate_strict_owned_near_duplicate_vertices(state_, body_id, dom_reason, dom_related)) {
                return detail::failed_void(*state_, StatusCode::DegenerateGeometry, diag_codes::kValNearDuplicateVertices,
                                           "几何验证失败：" + dom_reason, "几何验证失败", std::move(dom_related));
            }
            if (!validate_strict_owned_edges_nondegenerate(state_, body_id, dom_reason, dom_related)) {
                return detail::failed_void(*state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                                           "几何验证失败：" + dom_reason, "几何验证失败", std::move(dom_related));
            }
            if (!validate_strict_owned_faces_nondegenerate_area_proxy(state_, body_id, dom_reason, dom_related)) {
                return detail::failed_void(*state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                                           "几何验证失败：" + dom_reason, "几何验证失败", std::move(dom_related));
            }
            if (!validate_strict_owned_plane_face_loop_normals(state_, body_id, dom_reason, dom_related)) {
                return detail::failed_void(*state_, StatusCode::DegenerateGeometry, diag_codes::kValFaceNormalInconsistent,
                                           "几何验证失败：" + dom_reason, "几何验证失败", std::move(dom_related));
            }
        }
    }
    return ok_void(state_->create_diagnostic("几何验证通过"));
}

Result<void> ValidationService::validate_topology(BodyId body_id, ValidationMode mode) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_void(*state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
                                   "拓扑验证失败：目标实体不存在", "拓扑验证失败");
    }
    const auto& body = state_->bodies.at(body_id.value);
    if (body.rep_kind == RepKind::ExactBRep && body.kind == detail::BodyKind::Imported && !body.bbox.is_valid) {
        return detail::failed_void(*state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
                                   "拓扑验证失败：导入体缺少有效拓扑包围盒信息", "拓扑验证失败");
    }
    if (body.rep_kind == RepKind::ExactBRep && body.shells.empty() &&
        (!body.source_shells.empty() || !body.source_faces.empty())) {
        return detail::failed_void(*state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
                                   mode == ValidationMode::Strict
                                       ? "拓扑验证失败：结果体仅保留来源拓扑引用，尚未物化为可拥有的 B-Rep 拓扑"
                                       : "拓扑验证失败：结果体来源拓扑存在，但 owned topology 已丢失或未正确建立",
                                   "拓扑验证失败");
    }
    if (body.rep_kind == RepKind::ExactBRep && !body.shells.empty()) {
        TopologyValidationService topology_validation {state_};
        const auto topology_result = topology_validation.validate_body(body_id);
        if (topology_result.status != StatusCode::Ok) {
            return topology_result;
        }
    }
    if (mode == ValidationMode::Strict && body.rep_kind == RepKind::ExactBRep && !body.shells.empty()) {
        std::string dangle_reason;
        std::string_view dangle_code = diag_codes::kTopoDanglingEdge;
        std::uint64_t dangle_edge = 0;
        std::uint64_t dangle_shell = 0;
        if (!validate_strict_owned_topology_edge_coedge_index(*state_, body_id, dangle_reason, dangle_code,
                                                                dangle_edge, dangle_shell)) {
            std::vector<std::uint64_t> rel;
            rel.push_back(body_id.value);
            if (dangle_shell != 0) {
                rel.push_back(dangle_shell);
            }
            if (dangle_edge != 0) {
                rel.push_back(dangle_edge);
            }
            return detail::failed_void(*state_, StatusCode::InvalidTopology, dangle_code,
                                       "拓扑验证失败：" + dangle_reason, "拓扑验证失败",
                                       std::move(rel));
        }
    }
    if (mode == ValidationMode::Strict && body.rep_kind == RepKind::ExactBRep) {
        std::string reason;
        std::string_view issue_code = diag_codes::kTopoShellNotClosed;
        if (!validate_source_reference_consistency(*state_, body, reason, issue_code)) {
            return detail::failed_void(*state_, StatusCode::InvalidTopology, issue_code,
                                       "拓扑验证失败：" + reason, "拓扑验证失败");
        }
    }
    if (mode == ValidationMode::Strict && body.rep_kind == RepKind::ExactBRep) {
        // Stage 2 trim bridge: ensure face trim (UV pcurves) is consistent when present.
        if (!(body.kind == detail::BodyKind::Sweep && body.sweep_polyhedral_mass_valid)) {
            TopologyValidationService topology_validation {state_};
            const auto trim_batch = topology_validation.validate_body_trim_consistency(body_id);
            if (trim_batch.status != StatusCode::Ok) {
                return trim_batch;
            }
        }
    }
    if (body.rep_kind == RepKind::ExactBRep && !body.shells.empty()) {
        for (const auto shell_id : body.shells) {
            const auto shell_it = state_->shells.find(shell_id.value);
            if (shell_it == state_->shells.end()) {
                return detail::failed_void(*state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
                                           "拓扑验证失败：体引用了不存在的壳", "拓扑验证失败");
            }
            if (expects_materialized_closed_shell(body) && shell_it->second.faces.size() < 6) {
                return detail::failed_void(*state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
                                           "拓扑验证失败：最小物化结果壳骨架已被破坏，缺少必要闭合面", "拓扑验证失败");
            }
            // Standard/Fast/Strict：边级闭合与流形（每边恰为两侧不同拓扑面使用），对齐工业“可水密壳”门禁。
            std::string reason;
            std::string_view issue_code = diag_codes::kTopoShellNotClosed;
            std::uint64_t related_edge = 0;
            std::vector<std::uint64_t> edge_related_extra;
            if (!validate_closed_shell_edge_usage(*state_, shell_id, reason, issue_code, related_edge,
                                                  &edge_related_extra)) {
                std::vector<std::uint64_t> related;
                related.push_back(body_id.value);
                related.push_back(shell_id.value);
                if (related_edge != 0) {
                    related.push_back(related_edge);
                }
                related.insert(related.end(), edge_related_extra.begin(), edge_related_extra.end());
                return detail::failed_void(*state_, StatusCode::InvalidTopology, issue_code,
                                           "拓扑验证失败：" + reason, "拓扑验证失败",
                                           std::move(related));
            }
            if (mode == ValidationMode::Strict) {
                // Stage 2+ strict relation consistency (after closedness/manifold checks).
                std::string_view rel_code = diag_codes::kTopoShellNotClosed;
                std::string rel_reason;
                std::vector<std::uint64_t> rel_entities;
                if (!strict_check_shell_duplicate_faces_and_connectivity(*state_, shell_id, rel_code, rel_reason, rel_entities)) {
                    rel_entities.insert(rel_entities.begin(), body_id.value);
                    return detail::failed_void(*state_, StatusCode::InvalidTopology, rel_code,
                                               "拓扑验证失败：" + rel_reason, "拓扑验证失败",
                                               std::move(rel_entities));
                }
            }
        }
    }

    if (mode == ValidationMode::Strict && body.rep_kind == RepKind::ExactBRep) {
        // Stage 2+: body-level owned topology consistency:
        // Within one Body, a Face must not be shared by multiple owned Shells.
        // This prevents cross-shell self-gluing and makes manifold/closedness reasoning stable.
        std::unordered_map<std::uint64_t, std::uint64_t> face_owner_shell;
        for (const auto shell_id : body.shells) {
            const auto shell_it = state_->shells.find(shell_id.value);
            if (shell_it == state_->shells.end()) {
                continue;
            }
            for (const auto face_id : shell_it->second.faces) {
                const auto it = face_owner_shell.find(face_id.value);
                if (it == face_owner_shell.end()) {
                    face_owner_shell.emplace(face_id.value, shell_id.value);
                    continue;
                }
                if (it->second != shell_id.value) {
                    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                               diag_codes::kTopoRelationInconsistent,
                                               "拓扑验证失败：体内检测到跨壳共享面（face=" + std::to_string(face_id.value) +
                                                   ", shell_a=" + std::to_string(it->second) +
                                                   ", shell_b=" + std::to_string(shell_id.value) + ")",
                                               "拓扑验证失败",
                                               {body_id.value, face_id.value, it->second, shell_id.value});
                }
            }
        }
    }
    return ok_void(state_->create_diagnostic("拓扑验证通过"));
}

Result<void> ValidationService::validate_manifold(BodyId body_id, ValidationMode mode) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_void(*state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
                                   "流形验证失败：目标实体不存在", "流形验证失败");
    }
    const auto& body = state_->bodies.at(body_id.value);
    if (body.rep_kind != RepKind::ExactBRep || body.shells.empty()) {
        return ok_void(state_->create_diagnostic("流形验证跳过（无 owned B-Rep 壳）"));
    }
    TopologyValidationService topology_validation {state_};
    const auto topology_result = topology_validation.validate_body(body_id);
    if (topology_result.status != StatusCode::Ok) {
        return topology_result;
    }
    for (const auto shell_id : body.shells) {
        const auto shell_it = state_->shells.find(shell_id.value);
        if (shell_it == state_->shells.end()) {
            return detail::failed_void(*state_, StatusCode::InvalidTopology, diag_codes::kTopoShellNotClosed,
                                       "流形验证失败：体引用了不存在的壳", "流形验证失败");
        }
        std::string reason;
        std::string_view issue_code = diag_codes::kTopoShellNotClosed;
        std::uint64_t related_edge = 0;
        std::vector<std::uint64_t> edge_related_extra;
        if (!validate_closed_shell_edge_usage(*state_, shell_id, reason, issue_code, related_edge,
                                              &edge_related_extra)) {
            std::vector<std::uint64_t> related;
            related.push_back(body_id.value);
            related.push_back(shell_id.value);
            if (related_edge != 0) {
                related.push_back(related_edge);
            }
            related.insert(related.end(), edge_related_extra.begin(), edge_related_extra.end());
            return detail::failed_void(*state_, StatusCode::InvalidTopology, issue_code,
                                       "流形验证失败：" + reason, "流形验证失败",
                                       std::move(related));
        }
        if (mode == ValidationMode::Strict) {
            std::string_view rel_code = diag_codes::kTopoShellNotClosed;
            std::string rel_reason;
            std::vector<std::uint64_t> rel_entities;
            if (!strict_check_shell_duplicate_faces_and_connectivity(*state_, shell_id, rel_code, rel_reason, rel_entities)) {
                rel_entities.insert(rel_entities.begin(), body_id.value);
                return detail::failed_void(*state_, StatusCode::InvalidTopology, rel_code,
                                           "流形验证失败：" + rel_reason, "流形验证失败",
                                           std::move(rel_entities));
            }
        }
    }
    if (mode == ValidationMode::Strict) {
        std::unordered_map<std::uint64_t, std::uint64_t> face_owner_shell;
        for (const auto shell_id : body.shells) {
            const auto shell_it = state_->shells.find(shell_id.value);
            if (shell_it == state_->shells.end()) {
                continue;
            }
            for (const auto face_id : shell_it->second.faces) {
                const auto it = face_owner_shell.find(face_id.value);
                if (it == face_owner_shell.end()) {
                    face_owner_shell.emplace(face_id.value, shell_id.value);
                    continue;
                }
                if (it->second != shell_id.value) {
                    return detail::failed_void(*state_, StatusCode::InvalidTopology,
                                               diag_codes::kTopoRelationInconsistent,
                                               "流形验证失败：体内检测到跨壳共享面（face=" + std::to_string(face_id.value) +
                                                   ", shell_a=" + std::to_string(it->second) +
                                                   ", shell_b=" + std::to_string(shell_id.value) + ")",
                                               "流形验证失败",
                                               {body_id.value, face_id.value, it->second, shell_id.value});
                }
            }
        }
    }
    return ok_void(state_->create_diagnostic("流形验证通过"));
}

Result<void> ValidationService::validate_self_intersection(BodyId body_id, ValidationMode mode) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_void(*state_, StatusCode::InvalidInput, diag_codes::kValSelfIntersection,
                                   "自交检查失败：目标实体不存在", "自交检查失败");
    }
    const auto& body = state_->bodies.at(body_id.value);
    if (body.label == "offset" && min_extent(body.bbox) <= 0.0) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kValSelfIntersection,
                                   "自交检查失败：偏置结果已退化，疑似发生自交", "自交检查失败",
                                   {body_id.value});
    }
    // 子午面旋转体：分片线性侧壁 + 平面封盖；Strict 网格 SAT 对窄二面角/共顶点接触易数值误报，与 Sweep 占位棱柱同类。
    if (body.kind == detail::BodyKind::Sweep && body.sweep_polyhedral_mass_valid) {
        return ok_void(state_->create_diagnostic("自交检查通过（跳过分片旋转体网格 SAT）"));
    }

    // Strict：BRep 三角化近似 + 三角形对 SAT；分析前重焊，并跳过共享顶点的三角形对（合法流形接触）。
    // Fast/Standard 不跑网格扫描，降低 validate_all 成本并避免 Standard 门禁过噪。
    if (mode == ValidationMode::Strict && body.rep_kind == RepKind::ExactBRep) {
        RepresentationConversionService rep {state_};
        return strict_owned_brep_mesh_self_intersection_with_builder(
            state_, body_id, body,
            [&](const TessellationOptions& opts) { return rep.brep_to_mesh(body_id, opts); },
            {body_id.value}, "自交检查失败", "自交检查通过", "");
    }

    return ok_void(state_->create_diagnostic("自交检查通过"));
}

Result<void> ValidationService::validate_self_intersection_shell(BodyId body_id, ShellId shell_id,
                                                               ValidationMode mode) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_void(*state_, StatusCode::InvalidInput, diag_codes::kValSelfIntersection,
                                   "壳级自交检查失败：目标实体不存在", "壳级自交检查失败");
    }
    if (!detail::has_shell(*state_, shell_id)) {
        return detail::invalid_input_void(*state_, diag_codes::kValSelfIntersection,
                                          "壳级自交检查失败：目标壳不存在", "壳级自交检查失败");
    }
    const auto& body = state_->bodies.at(body_id.value);
    const bool shell_owned =
        std::any_of(body.shells.begin(), body.shells.end(),
                    [shell_id](ShellId sid) { return sid.value == shell_id.value; });
    if (!shell_owned) {
        return detail::invalid_input_void(*state_, diag_codes::kValSelfIntersection,
                                          "壳级自交检查失败：壳不属于该体的 owned 壳集合", "壳级自交检查失败");
    }
    if (body.label == "offset" && min_extent(body.bbox) <= 0.0) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kValSelfIntersection,
                                   "壳级自交检查失败：偏置结果已退化，疑似发生自交", "壳级自交检查失败",
                                   {body_id.value, shell_id.value});
    }
    if (body.kind == detail::BodyKind::Sweep && body.sweep_polyhedral_mass_valid) {
        return ok_void(state_->create_diagnostic("壳级自交检查通过（跳过分片旋转体网格 SAT）"));
    }
    if (mode != ValidationMode::Strict || body.rep_kind != RepKind::ExactBRep) {
        return ok_void(state_->create_diagnostic("壳级自交检查跳过（非 Strict 或无 owned B-Rep）"));
    }
    RepresentationConversionService rep {state_};
    return strict_owned_brep_mesh_self_intersection_with_builder(
        state_, body_id, body,
        [&](const TessellationOptions& opts) { return rep.brep_to_mesh_shell(body_id, shell_id, opts); },
        {body_id.value, shell_id.value}, "壳级自交检查失败", "壳级自交检查通过", "（壳级三角化近似）");
}

Result<void> ValidationService::validate_self_intersection_shell_many(BodyId body_id,
                                                                    std::span<const ShellId> shell_ids,
                                                                    ValidationMode mode) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_void(*state_, StatusCode::InvalidInput, diag_codes::kValSelfIntersection,
                                   "壳级批量自交检查失败：目标实体不存在", "壳级批量自交检查失败");
    }
    if (shell_ids.empty()) {
        return detail::invalid_input_void(*state_, diag_codes::kValSelfIntersection,
                                          "壳级批量自交检查失败：壳列表为空", "壳级批量自交检查失败");
    }
    for (const auto shell_id : shell_ids) {
        const auto r = validate_self_intersection_shell(body_id, shell_id, mode);
        if (r.status != StatusCode::Ok) {
            return r;
        }
    }
    return ok_void(state_->create_diagnostic("壳级自交批量检查通过"));
}

Result<void> ValidationService::validate_self_intersection_all_shells(BodyId body_id,
                                                                    ValidationMode mode) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_void(*state_, StatusCode::InvalidInput, diag_codes::kValSelfIntersection,
                                   "壳级自交检查失败：目标实体不存在", "壳级自交检查失败");
    }
    const auto& body = state_->bodies.at(body_id.value);
    if (body.rep_kind != RepKind::ExactBRep || body.shells.empty()) {
        return ok_void(state_->create_diagnostic("壳级自交检查跳过（无 owned B-Rep 壳）"));
    }
    return validate_self_intersection_shell_many(
        body_id, std::span<const ShellId>(body.shells.data(), body.shells.size()), mode);
}

Result<void> ValidationService::validate_tolerance(BodyId body_id, ValidationMode mode) const {
    if (!detail::has_body(*state_, body_id)) {
        return detail::failed_void(*state_, StatusCode::ToleranceConflict, diag_codes::kValToleranceConflict,
                                   "容差验证失败：目标实体不存在", "容差验证失败");
    }
    const auto& tol = state_->config.tolerance;
    if (!std::isfinite(tol.linear) || tol.linear <= 0.0) {
        return detail::failed_void(*state_, StatusCode::ToleranceConflict, diag_codes::kValToleranceConflict,
                                   "容差验证失败：工作线性容差须为有限正数", "容差验证失败");
    }
    if (!std::isfinite(tol.angular) || tol.angular <= 0.0) {
        return detail::failed_void(*state_, StatusCode::ToleranceConflict, diag_codes::kValToleranceConflict,
                                   "容差验证失败：角度容差须为有限正数", "容差验证失败");
    }
    const auto& body = state_->bodies.at(body_id.value);
    if (!has_valid_bbox(body)) {
        return detail::failed_void(*state_, StatusCode::ToleranceConflict, diag_codes::kValToleranceConflict,
                                   "容差验证失败：目标实体无有效尺寸范围", "容差验证失败");
    }
    if (mode == ValidationMode::Strict) {
        if (std::isfinite(tol.min_local) && std::isfinite(tol.max_local) && tol.max_local > 0.0 &&
            tol.min_local > tol.max_local) {
            return detail::failed_void(*state_, StatusCode::ToleranceConflict, diag_codes::kValToleranceConflict,
                                       "容差验证失败：线性容差策略 min_local 大于 max_local", "容差验证失败",
                                       {body_id.value});
        }
        if (std::isfinite(tol.max_local) && tol.max_local > 0.0 && tol.linear > tol.max_local) {
            return detail::failed_void(*state_, StatusCode::ToleranceConflict, diag_codes::kValToleranceConflict,
                                       "容差验证失败：线性容差超过策略上限 max_local", "容差验证失败",
                                       {body_id.value});
        }
        const auto ext = min_extent(body.bbox);
        const bool skip_linear_vs_min_extent =
            body.kind == detail::BodyKind::Sweep && std::isfinite(ext) && ext > 0.0 &&
            ext <= tol.linear * Scalar(1e-6);
        if (!skip_linear_vs_min_extent && std::isfinite(ext) && ext > 0.0 && tol.linear > 0.25 * ext) {
            return detail::failed_void(*state_, StatusCode::ToleranceConflict, diag_codes::kValToleranceConflict,
                                       "容差验证失败：线性容差相对模型尺度过大", "容差验证失败",
                                       {body_id.value});
        }
        constexpr Scalar k_pi = 3.14159265358979323846;
        if (tol.angular > k_pi) {
            return detail::failed_void(*state_, StatusCode::ToleranceConflict, diag_codes::kValToleranceConflict,
                                       "容差验证失败：角度容差超过 π", "容差验证失败", {body_id.value});
        }
        // 导入体：高纵横比包围盒 + 最小厚度落在相对线性容差的工程薄片带内（与 revolve/sweep 等本地建模体解耦，避免误报）。
        if (body.kind == detail::BodyKind::Imported) {
            const auto ext_max = max_extent(body.bbox);
            constexpr Scalar k_high_aspect_ratio = 100.0;
            const Scalar thin_low = tol.linear * Scalar(0.05);
            const Scalar thin_high = tol.linear * Scalar(8.0);
            if (std::isfinite(ext) && ext > 0.0 && std::isfinite(ext_max) &&
                ext_max >= ext * k_high_aspect_ratio && ext > thin_low && ext < thin_high) {
                return detail::failed_void(*state_, StatusCode::ToleranceConflict,
                                           diag_codes::kValModelFinerThanTolerance,
                                           "容差验证失败：导入包围盒呈高纵横比薄片且最小厚度相对线性容差过薄",
                                           "容差验证失败", {body_id.value});
            }
        }
    }
    return ok_void(state_->create_diagnostic("容差验证通过"));
}

Result<void> ValidationService::validate_tolerance_many(std::span<const BodyId> body_ids,
                                                        ValidationMode mode) const {
    if (body_ids.empty()) {
        return detail::invalid_input_void(*state_, diag_codes::kValToleranceConflict,
                                            "容差批量验证失败：输入为空", "容差批量验证失败");
    }
    for (const auto body_id : body_ids) {
        const auto r = validate_tolerance(body_id, mode);
        if (r.status != StatusCode::Ok) {
            return r;
        }
    }
    return ok_void(state_->create_diagnostic("容差批量验证通过"));
}

Result<void> ValidationService::validate_all(BodyId body_id, ValidationMode mode) const {
    auto geometry = validate_geometry(body_id, mode);
    if (geometry.status != StatusCode::Ok) {
        return geometry;
    }
    auto tolerance = validate_tolerance(body_id, mode);
    if (tolerance.status != StatusCode::Ok) {
        return tolerance;
    }
    auto topology = validate_topology(body_id, mode);
    if (topology.status != StatusCode::Ok) {
        return topology;
    }
    auto manifold = validate_manifold(body_id, mode);
    if (manifold.status != StatusCode::Ok) {
        return manifold;
    }
    auto self_intersection = validate_self_intersection(body_id, mode);
    if (self_intersection.status != StatusCode::Ok) {
        return self_intersection;
    }
    return ok_void(state_->create_diagnostic("全量验证通过"));
}

Result<void> ValidationService::validate_geometry_many(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  if (body_ids.empty()) {
    return detail::invalid_input_void(*state_, diag_codes::kValDegenerateGeometry,
                                      "几何批量验证失败：输入为空", "几何批量验证失败");
  }
  for (const auto body_id : body_ids) {
    const auto r = validate_geometry(body_id, mode);
    if (r.status != StatusCode::Ok) return r;
  }
  return ok_void(state_->create_diagnostic("几何批量验证通过"));
}

Result<void> ValidationService::validate_topology_many(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  if (body_ids.empty()) {
    return detail::invalid_input_void(*state_, diag_codes::kTopoShellNotClosed,
                                      "拓扑批量验证失败：输入为空", "拓扑批量验证失败");
  }
  for (const auto body_id : body_ids) {
    const auto r = validate_topology(body_id, mode);
    if (r.status != StatusCode::Ok) return r;
  }
  return ok_void(state_->create_diagnostic("拓扑批量验证通过"));
}

Result<void> ValidationService::validate_all_many(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  if (body_ids.empty()) {
    return detail::invalid_input_void(*state_, diag_codes::kValDegenerateGeometry,
                                      "全量批量验证失败：输入为空", "全量批量验证失败");
  }
  for (const auto body_id : body_ids) {
    const auto r = validate_all(body_id, mode);
    if (r.status != StatusCode::Ok) return r;
  }
  return ok_void(state_->create_diagnostic("全量批量验证通过"));
}

Result<void> ValidationService::validate_manifold_many(std::span<const BodyId> body_ids,
                                                       ValidationMode mode) const {
  if (body_ids.empty()) {
    return detail::invalid_input_void(*state_, diag_codes::kTopoShellNotClosed,
                                      "流形批量验证失败：输入为空", "流形批量验证失败");
  }
  for (const auto body_id : body_ids) {
    const auto r = validate_manifold(body_id, mode);
    if (r.status != StatusCode::Ok) {
      return r;
    }
  }
  return ok_void(state_->create_diagnostic("流形批量验证通过"));
}

Result<void> ValidationService::validate_self_intersection_many(std::span<const BodyId> body_ids,
                                                                ValidationMode mode) const {
  if (body_ids.empty()) {
    return detail::invalid_input_void(*state_, diag_codes::kValSelfIntersection,
                                      "自交批量检查失败：输入为空", "自交批量检查失败");
  }
  for (const auto body_id : body_ids) {
    const auto r = validate_self_intersection(body_id, mode);
    if (r.status != StatusCode::Ok) {
      return r;
    }
  }
  return ok_void(state_->create_diagnostic("自交批量检查通过"));
}

Result<bool> ValidationService::is_geometry_valid(BodyId body_id,
                                                  ValidationMode mode) const {
  const auto r = validate_geometry(body_id, mode);
  if (r.status == StatusCode::InvalidInput) {
    return error_result<bool>(r.status, r.diagnostic_id);
  }
  return ok_result(r.status == StatusCode::Ok,
                   state_->create_diagnostic("已查询几何有效性"));
}

Result<bool> ValidationService::is_topology_valid(BodyId body_id,
                                                  ValidationMode mode) const {
  const auto r = validate_topology(body_id, mode);
  if (r.status == StatusCode::InvalidInput) {
    return error_result<bool>(r.status, r.diagnostic_id);
  }
  return ok_result(r.status == StatusCode::Ok,
                   state_->create_diagnostic("已查询拓扑有效性"));
}

Result<bool> ValidationService::is_manifold_valid(BodyId body_id,
                                                  ValidationMode mode) const {
  const auto r = validate_manifold(body_id, mode);
  if (r.status == StatusCode::InvalidInput) {
    return error_result<bool>(r.status, r.diagnostic_id);
  }
  return ok_result(r.status == StatusCode::Ok,
                   state_->create_diagnostic("已查询流形有效性"));
}

Result<bool> ValidationService::is_valid(BodyId body_id, ValidationMode mode) const {
  const auto r = validate_all(body_id, mode);
  if (r.status == StatusCode::InvalidInput) {
    return error_result<bool>(r.status, r.diagnostic_id);
  }
  return ok_result(r.status == StatusCode::Ok,
                   state_->create_diagnostic("已查询体有效性"));
}

Result<void> ValidationService::validate_bbox(BodyId body_id) const {
  if (!detail::has_body(*state_, body_id)) {
    return detail::invalid_input_void(*state_, diag_codes::kValDegenerateGeometry,
                                      "包围盒验证失败：目标实体不存在", "包围盒验证失败");
  }
  const auto& body = state_->bodies.at(body_id.value);
  if (!has_valid_bbox(body)) {
    return detail::failed_void(*state_, StatusCode::DegenerateGeometry,
                               diag_codes::kValDegenerateGeometry,
                               "包围盒验证失败：目标实体包围盒无效", "包围盒验证失败");
  }
  return ok_void(state_->create_diagnostic("包围盒验证通过"));
}

Result<void> ValidationService::validate_bbox_many(
    std::span<const BodyId> body_ids) const {
  if (body_ids.empty()) {
    return detail::invalid_input_void(*state_, diag_codes::kValDegenerateGeometry,
                                      "包围盒批量验证失败：输入为空", "包围盒批量验证失败");
  }
  for (const auto body_id : body_ids) {
    const auto r = validate_bbox(body_id);
    if (r.status != StatusCode::Ok) return r;
  }
  return ok_void(state_->create_diagnostic("包围盒批量验证通过"));
}

Result<std::uint64_t> ValidationService::count_invalid_in(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  std::uint64_t count = 0;
  for (const auto body_id : body_ids) {
    const auto r = validate_all(body_id, mode);
    if (r.status != StatusCode::Ok) ++count;
  }
  return ok_result(count, state_->create_diagnostic("已统计无效体数量"));
}

Result<BodyId> ValidationService::first_invalid_in(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  for (const auto body_id : body_ids) {
    const auto r = validate_all(body_id, mode);
    if (r.status != StatusCode::Ok) {
      return ok_result(body_id, state_->create_diagnostic("已找到首个无效体"));
    }
  }
  return detail::failed_result<BodyId>(
      *state_, StatusCode::OperationFailed, diag_codes::kValDegenerateGeometry,
      "无效体查询失败：输入集合中不存在无效体", "无效体查询失败");
}

Result<std::vector<BodyId>> ValidationService::filter_valid_bodies(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  std::vector<BodyId> out;
  for (const auto body_id : body_ids) {
    const auto r = validate_all(body_id, mode);
    if (r.status == StatusCode::Ok) out.push_back(body_id);
  }
  return ok_result(std::move(out), state_->create_diagnostic("已过滤有效体"));
}

Result<std::vector<BodyId>> ValidationService::filter_invalid_bodies(
    std::span<const BodyId> body_ids, ValidationMode mode) const {
  std::vector<BodyId> out;
  for (const auto body_id : body_ids) {
    const auto r = validate_all(body_id, mode);
    if (r.status != StatusCode::Ok) out.push_back(body_id);
  }
  return ok_result(std::move(out), state_->create_diagnostic("已过滤无效体"));
}

