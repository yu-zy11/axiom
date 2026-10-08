std::vector<FaceCandidatePair> build_face_candidates_for_boolean(const detail::KernelState& state,
                                                                 BodyId lhs,
                                                                 BodyId rhs) {
    std::vector<FaceCandidatePair> pairs;
    const auto lhs_faces = faces_for_body_boolean(state, lhs);
    const auto rhs_faces = faces_for_body_boolean(state, rhs);
    if (lhs_faces.empty() || rhs_faces.empty()) {
        return pairs;
    }

    std::vector<BoundingBox> lhs_boxes;
    lhs_boxes.reserve(lhs_faces.size());
    for (const auto face_id : lhs_faces) {
        lhs_boxes.push_back(face_bbox(state, face_id));
    }
    std::vector<BoundingBox> rhs_boxes;
    rhs_boxes.reserve(rhs_faces.size());
    for (const auto face_id : rhs_faces) {
        rhs_boxes.push_back(face_bbox(state, face_id));
    }

    for (std::size_t i = 0; i < lhs_faces.size(); ++i) {
        if (!lhs_boxes[i].is_valid) {
            continue;
        }
        for (std::size_t j = 0; j < rhs_faces.size(); ++j) {
            if (!rhs_boxes[j].is_valid) {
                continue;
            }
            if (!intersect_bbox(lhs_boxes[i], rhs_boxes[j]).is_valid) {
                continue;
            }
            pairs.push_back(FaceCandidatePair {lhs_faces[i], rhs_faces[j]});
        }
    }
    return pairs;
}

Vec3 safe_unit_normal_local(const Vec3& normal) {
    const auto n = detail::norm(normal);
    if (!(n > 0.0)) {
        return Vec3 {0.0, 0.0, 1.0};
    }
    return detail::scale(normal, 1.0 / n);
}

bool plane_plane_intersection_line(const detail::KernelState& state,
                                   FaceId lhs_face,
                                   FaceId rhs_face,
                                   Point3& out_origin,
                                   Vec3& out_dir) {
    const auto lhs_face_it = state.faces.find(lhs_face.value);
    const auto rhs_face_it = state.faces.find(rhs_face.value);
    if (lhs_face_it == state.faces.end() || rhs_face_it == state.faces.end()) {
        return false;
    }
    const auto lhs_surf_it = state.surfaces.find(lhs_face_it->second.surface_id.value);
    const auto rhs_surf_it = state.surfaces.find(rhs_face_it->second.surface_id.value);
    if (lhs_surf_it == state.surfaces.end() || rhs_surf_it == state.surfaces.end()) {
        return false;
    }
    const auto& s0 = lhs_surf_it->second;
    const auto& s1 = rhs_surf_it->second;
    if (s0.kind != detail::SurfaceKind::Plane || s1.kind != detail::SurfaceKind::Plane) {
        return false;
    }
    const auto n0 = safe_unit_normal_local(s0.normal);
    const auto n1 = safe_unit_normal_local(s1.normal);
    const auto dir = detail::cross(n0, n1);
    const auto dir_len2 = detail::dot(dir, dir);
    if (!(dir_len2 > 1e-14)) {
        return false;
    }
    const auto d0 = detail::dot(n0, Vec3 {s0.origin.x, s0.origin.y, s0.origin.z});
    const auto d1 = detail::dot(n1, Vec3 {s1.origin.x, s1.origin.y, s1.origin.z});
    // p = ((d0*n1 - d1*n0) x (n0 x n1)) / |n0 x n1|^2
    const Vec3 c {d0 * n1.x - d1 * n0.x, d0 * n1.y - d1 * n0.y, d0 * n1.z - d1 * n0.z};
    const auto p = detail::scale(detail::cross(c, dir), 1.0 / dir_len2);
    out_origin = Point3 {p.x, p.y, p.z};
    out_dir = detail::normalize(dir);
    return true;
}

std::vector<IntersectionCurve> compute_intersection_curves_for_candidates(detail::KernelState& state,
                                                                          std::span<const FaceCandidatePair> candidates) {
    std::vector<IntersectionCurve> curves;
    curves.reserve(candidates.size());
    for (const auto& pair : candidates) {
        Point3 origin {};
        Vec3 dir {};
        if (!plane_plane_intersection_line(state, pair.lhs_face, pair.rhs_face, origin, dir)) {
            continue;
        }
        detail::CurveRecord curve;
        curve.kind = detail::CurveKind::Line;
        curve.origin = origin;
        curve.direction = dir;
        const auto curve_id = CurveId {state.allocate_id()};
        state.curves.emplace(curve_id.value, std::move(curve));
        curves.push_back(IntersectionCurve {curve_id, pair.lhs_face, pair.rhs_face});
    }
    return curves;
}

std::vector<IntersectionSegment> clip_intersection_lines_to_face_overlap(detail::KernelState& state,
                                                                         std::span<const IntersectionCurve> curves) {
    std::vector<IntersectionSegment> segments;
    segments.reserve(curves.size());
    for (const auto& item : curves) {
        const auto lhs_box = face_bbox(state, item.lhs_face);
        const auto rhs_box = face_bbox(state, item.rhs_face);
        const auto overlap = intersect_bbox(lhs_box, rhs_box);
        if (!overlap.is_valid) {
            continue;
        }
        const auto curve_it = state.curves.find(item.curve.value);
        if (curve_it == state.curves.end() || curve_it->second.kind != detail::CurveKind::Line) {
            continue;
        }
        const auto o = curve_it->second.origin;
        const auto d = curve_it->second.direction;
        const auto d_len2 = detail::dot(d, d);
        if (!(d_len2 > 1e-14)) {
            continue;
        }

        const std::array<Point3, 8> corners {
            Point3 {overlap.min.x, overlap.min.y, overlap.min.z},
            Point3 {overlap.max.x, overlap.min.y, overlap.min.z},
            Point3 {overlap.max.x, overlap.max.y, overlap.min.z},
            Point3 {overlap.min.x, overlap.max.y, overlap.min.z},
            Point3 {overlap.min.x, overlap.min.y, overlap.max.z},
            Point3 {overlap.max.x, overlap.min.y, overlap.max.z},
            Point3 {overlap.max.x, overlap.max.y, overlap.max.z},
            Point3 {overlap.min.x, overlap.max.y, overlap.max.z},
        };

        Scalar tmin = 0.0;
        Scalar tmax = 0.0;
        bool initialized = false;
        for (const auto& p : corners) {
            const Vec3 op {p.x - o.x, p.y - o.y, p.z - o.z};
            const auto t = detail::dot(op, d) / d_len2;
            if (!initialized) {
                tmin = t;
                tmax = t;
                initialized = true;
            } else {
                tmin = std::min(tmin, t);
                tmax = std::max(tmax, t);
            }
        }
        if (!initialized || !(tmax - tmin > 1e-9)) {
            continue;
        }

        const Point3 p0 {o.x + d.x * tmin, o.y + d.y * tmin, o.z + d.z * tmin};
        const Point3 p1 {o.x + d.x * tmax, o.y + d.y * tmax, o.z + d.z * tmax};

        detail::CurveRecord seg;
        seg.kind = detail::CurveKind::LineSegment;
        seg.poles = {p0, p1};
        const auto seg_id = CurveId {state.allocate_id()};
        state.curves.emplace(seg_id.value, std::move(seg));
        segments.push_back(IntersectionSegment {seg_id, item.lhs_face, item.rhs_face});
    }
    return segments;
}

CurveId longest_intersection_segment_curve(const detail::KernelState& state,
                                           std::span<const IntersectionSegment> segments) {
    CurveId best {};
    Scalar best_len2 = -1.0;
    for (const auto& s : segments) {
        const auto it = state.curves.find(s.curve.value);
        if (it == state.curves.end() || it->second.kind != detail::CurveKind::LineSegment ||
            it->second.poles.size() < 2) {
            continue;
        }
        const auto dv = detail::subtract(it->second.poles.back(), it->second.poles.front());
        const auto l2 = detail::dot(dv, dv);
        if (l2 > best_len2) {
            best_len2 = l2;
            best = s.curve;
        }
    }
    return best;
}

std::vector<BoundingBox> body_regions_for_boolean(const detail::KernelState& state, BodyId body_id) {
    std::vector<BoundingBox> regions;
    if (!detail::has_body(state, body_id)) {
        return regions;
    }
    const auto& body = state.bodies.at(body_id.value);
    const auto shells = shells_for_body(body);
    for (const auto shell_id : shells) {
        const auto bbox = shell_bbox(state, shell_id);
        if (bbox.is_valid) {
            regions.push_back(bbox);
        }
    }
    if (regions.empty() && body.bbox.is_valid) {
        regions.push_back(body.bbox);
    }
    return regions;
}

Scalar bbox_volume(const BoundingBox& bbox) {
    if (!bbox.is_valid) {
        return 0.0;
    }
    const auto dx = std::max(0.0, bbox.max.x - bbox.min.x);
    const auto dy = std::max(0.0, bbox.max.y - bbox.min.y);
    const auto dz = std::max(0.0, bbox.max.z - bbox.min.z);
    return dx * dy * dz;
}

BooleanPrepStats compute_boolean_prep_stats(const detail::KernelState& state, BodyId lhs, BodyId rhs) {
    BooleanPrepStats stats;
    const auto lhs_regions = body_regions_for_boolean(state, lhs);
    const auto rhs_regions = body_regions_for_boolean(state, rhs);
    stats.lhs_regions = lhs_regions.size();
    stats.rhs_regions = rhs_regions.size();

    for (const auto& lhs_bbox : lhs_regions) {
        for (const auto& rhs_bbox : rhs_regions) {
            const auto overlap = intersect_bbox(lhs_bbox, rhs_bbox);
            if (!overlap.is_valid) {
                continue;
            }
            ++stats.overlap_candidates;
            stats.overlap_volume_sum += bbox_volume(overlap);
            if (!stats.local_overlap_bbox.is_valid) {
                stats.local_overlap_bbox = overlap;
            } else {
                stats.local_overlap_bbox = union_bbox(stats.local_overlap_bbox, overlap);
            }
        }
    }
    stats.local_clip_applied = stats.local_overlap_bbox.is_valid;
    return stats;
}

/// 为布尔工作流诊断问题绑定稳定阶段标签，供 JSON 导出与 CI 聚合（与 error_codes 中 BOOL 码一致）。
void set_boolean_diagnostic_stage(Issue& issue, std::string_view code) {
    using namespace diag_codes;
    if (code == kBoolPrepCandidatesBuilt || code == kBoolLocalClipApplied || code == kBoolFaceCandidatesBuilt ||
        code == kBoolNearDegenerateWarning || code == kBoolPrepNoCandidateWarning) {
        issue.stage = "bool.prep";
    } else if (code == kBoolRunStageSummary) {
        issue.stage = "bool.summary";
    } else if (code == kBoolIntersectionCurvesBuilt) {
        issue.stage = "bool.intersect";
    } else if (code == kBoolIntersectionSegmentsBuilt) {
        issue.stage = "bool.intersect.trim";
    } else if (code == kBoolIntersectionWiresStored) {
        issue.stage = "bool.intersect.store";
    } else if (code == kBoolImprintApplied || code == kBoolImprintSegmentApplied) {
        issue.stage = "bool.split.imprint";
    } else if (code == kBoolClassificationCompleted) {
        issue.stage = "bool.classify.result";
    } else if (code == kBoolRebuildCompleted) {
        issue.stage = "bool.rebuild.result";
    } else if (code == kBoolStageCandidates) {
        issue.stage = "bool.prep.candidates";
    } else if (code == kBoolStageSplit) {
        issue.stage = "bool.split";
    } else if (code == kBoolStageOutputMaterialized) {
        issue.stage = "bool.output_materialized";
    } else if (code == kBoolStageClassify) {
        issue.stage = "bool.classify";
    } else if (code == kBoolStageRebuild) {
        issue.stage = "bool.rebuild";
    } else if (code == kBoolStageValidate) {
        issue.stage = "bool.validate";
    } else if (code == kBoolStageRepair) {
        issue.stage = "bool.repair";
    } else if (code == kBoolStrictValidationResidual) {
        issue.stage = "bool.validate.residual";
    } else if (code == kBoolInvalidInput) {
        issue.stage = "bool.input";
    } else if (code == kBoolIntersectionFailure) {
        issue.stage = "bool.abort.intersect";
    } else if (code == kBoolClassificationFailure) {
        issue.stage = "bool.abort.classify";
    }
}

void append_boolean_prep_candidate_issue(detail::KernelState& state,
                                         DiagnosticId diag,
                                         BodyId lhs,
                                         BodyId rhs,
                                         const BooleanPrepStats& prep) {
    if (diag.value == 0) {
        return;
    }
    std::ostringstream message;
    message << "布尔预处理候选片段构建完成: lhs_regions=" << prep.lhs_regions
            << ", rhs_regions=" << prep.rhs_regions
            << ", overlap_candidates=" << prep.overlap_candidates
            << ", overlap_volume_sum=" << prep.overlap_volume_sum;
    auto issue = detail::make_info_issue(diag_codes::kBoolPrepCandidatesBuilt, message.str());
    issue.related_entities = {lhs.value, rhs.value};
    issue.numeric_evidence = {
        {"lhs_regions", static_cast<Scalar>(prep.lhs_regions), "count"},
        {"rhs_regions", static_cast<Scalar>(prep.rhs_regions), "count"},
        {"overlap_candidates", static_cast<Scalar>(prep.overlap_candidates), "count"},
        {"overlap_volume_sum", prep.overlap_volume_sum, "model_unit^3"},
    };
    set_boolean_diagnostic_stage(issue, diag_codes::kBoolPrepCandidatesBuilt);
    state.append_diagnostic_issue(diag, std::move(issue));

    if (prep.local_clip_applied) {
        auto clip_issue = detail::make_info_issue(
            diag_codes::kBoolLocalClipApplied,
            "布尔局部裁剪已应用：结果 bbox 已按候选重叠区域收缩");
        clip_issue.related_entities = {lhs.value, rhs.value};
        clip_issue.numeric_evidence = {{"local_clip_applied", 1.0, "bool"}};
        set_boolean_diagnostic_stage(clip_issue, diag_codes::kBoolLocalClipApplied);
        state.append_diagnostic_issue(diag, std::move(clip_issue));
    }
}

const char* bbox_relation_name(BBoxRelation relation) {
    switch (relation) {
        case BBoxRelation::Disjoint:
            return "Disjoint";
        case BBoxRelation::Touching:
            return "Touching";
        case BBoxRelation::Overlapping:
            return "Overlapping";
        case BBoxRelation::LhsContainsRhs:
            return "LhsContainsRhs";
        case BBoxRelation::RhsContainsLhs:
            return "RhsContainsLhs";
    }
    return "Unknown";
}

const char* boolean_op_name(BooleanOp op) {
    switch (op) {
        case BooleanOp::Union:
            return "Union";
        case BooleanOp::Intersect:
            return "Intersect";
        case BooleanOp::Subtract:
            return "Subtract";
        case BooleanOp::Split:
            return "Split";
    }
    return "Unknown";
}

void append_boolean_run_stage_issue(detail::KernelState& state,
                                    DiagnosticId diag,
                                    BooleanOp op,
                                    BBoxRelation relation,
                                    const BooleanPrepStats& prep,
                                    BodyId lhs,
                                    BodyId rhs,
                                    BodyId output) {
    if (diag.value == 0) {
        return;
    }
    std::ostringstream msg;
    msg << "布尔阶段摘要 op=" << boolean_op_name(op) << " bbox_relation=" << bbox_relation_name(relation)
        << " lhs_regions=" << prep.lhs_regions << " rhs_regions=" << prep.rhs_regions
        << " overlap_candidates=" << prep.overlap_candidates
        << " local_clip=" << (prep.local_clip_applied ? "true" : "false");
    auto issue = detail::make_info_issue(diag_codes::kBoolRunStageSummary, msg.str());
    issue.related_entities = {lhs.value, rhs.value, output.value};
    issue.numeric_evidence = {
        {"operation", static_cast<Scalar>(op), "enum"},
        {"bbox_relation", static_cast<Scalar>(relation), "enum"},
        {"lhs_regions", static_cast<Scalar>(prep.lhs_regions), "count"},
        {"rhs_regions", static_cast<Scalar>(prep.rhs_regions), "count"},
        {"overlap_candidates", static_cast<Scalar>(prep.overlap_candidates), "count"},
        {"local_clip_applied", prep.local_clip_applied ? 1.0 : 0.0, "bool"},
    };
    set_boolean_diagnostic_stage(issue, diag_codes::kBoolRunStageSummary);
    state.append_diagnostic_issue(diag, std::move(issue));
}

void append_boolean_stage_issue(detail::KernelState& state,
                                DiagnosticId diag,
                                std::string_view code,
                                std::string message,
                                std::vector<std::uint64_t> related_entities) {
    if (diag.value == 0) {
        return;
    }
    auto issue = detail::make_info_issue(code, std::move(message));
    issue.related_entities = std::move(related_entities);
    set_boolean_diagnostic_stage(issue, code);
    state.append_diagnostic_issue(diag, std::move(issue));
}

void append_boolean_face_candidate_issue(detail::KernelState& state,
                                        DiagnosticId diag,
                                        BodyId lhs,
                                        BodyId rhs,
                                        std::size_t count) {
    if (diag.value == 0) {
        return;
    }
    std::ostringstream msg;
    msg << "布尔面级候选对已生成: face_candidates=" << count;
    auto issue = detail::make_info_issue(diag_codes::kBoolFaceCandidatesBuilt, msg.str());
    issue.related_entities = {lhs.value, rhs.value};
    issue.numeric_evidence = {{"face_candidates", static_cast<Scalar>(count), "count"}};
    set_boolean_diagnostic_stage(issue, diag_codes::kBoolFaceCandidatesBuilt);
    state.append_diagnostic_issue(diag, std::move(issue));
}

void append_boolean_intersection_curve_issue(detail::KernelState& state,
                                             DiagnosticId diag,
                                             BodyId lhs,
                                             BodyId rhs,
                                             std::span<const IntersectionCurve> curves) {
    if (diag.value == 0) {
        return;
    }
    std::ostringstream msg;
    msg << "布尔精确求交（解析）已生成交线: curves=" << curves.size();
    auto issue = detail::make_info_issue(diag_codes::kBoolIntersectionCurvesBuilt, msg.str());
    issue.related_entities = {lhs.value, rhs.value};
    issue.numeric_evidence = {{"curves", static_cast<Scalar>(curves.size()), "count"}};
    set_boolean_diagnostic_stage(issue, diag_codes::kBoolIntersectionCurvesBuilt);
    const std::size_t kMaxCurves = 8;
    for (std::size_t i = 0; i < std::min(kMaxCurves, curves.size()); ++i) {
        issue.related_entities.push_back(curves[i].curve.value);
    }
    state.append_diagnostic_issue(diag, std::move(issue));
}

void append_boolean_intersection_segment_issue(detail::KernelState& state,
                                               DiagnosticId diag,
                                               BodyId lhs,
                                               BodyId rhs,
                                               std::span<const IntersectionSegment> segments) {
    if (diag.value == 0) {
        return;
    }
    std::ostringstream msg;
    msg << "布尔交线裁剪完成: segments=" << segments.size();
    auto issue = detail::make_info_issue(diag_codes::kBoolIntersectionSegmentsBuilt, msg.str());
    issue.related_entities = {lhs.value, rhs.value};
    issue.numeric_evidence = {{"segments", static_cast<Scalar>(segments.size()), "count"}};
    set_boolean_diagnostic_stage(issue, diag_codes::kBoolIntersectionSegmentsBuilt);
    const std::size_t kMaxSeg = 8;
    for (std::size_t i = 0; i < std::min(kMaxSeg, segments.size()); ++i) {
        issue.related_entities.push_back(segments[i].curve.value);
    }
    state.append_diagnostic_issue(diag, std::move(issue));
}

void append_boolean_intersection_stored_issue(detail::KernelState& state,
                                              DiagnosticId diag,
                                              BodyId lhs,
                                              BodyId rhs,
                                              IntersectionId intersection_id,
                                              std::size_t curve_count) {
    if (diag.value == 0) {
        return;
    }
    std::ostringstream msg;
    msg << "布尔交线集合已保存: intersection_id=" << intersection_id.value
        << " curve_count=" << curve_count;
    auto issue = detail::make_info_issue(diag_codes::kBoolIntersectionWiresStored, msg.str());
    issue.related_entities = {lhs.value, rhs.value, intersection_id.value};
    issue.numeric_evidence = {{"curve_count", static_cast<Scalar>(curve_count), "count"}};
    set_boolean_diagnostic_stage(issue, diag_codes::kBoolIntersectionWiresStored);
    state.append_diagnostic_issue(diag, std::move(issue));
}
