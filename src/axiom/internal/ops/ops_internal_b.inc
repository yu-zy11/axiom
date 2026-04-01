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

void combine_mass_properties_sum(const MassProperties& m0, const MassProperties& m1, MassProperties& out) {
    const auto M = m0.volume + m1.volume;
    out.volume = M;
    out.area = m0.area + m1.area;
    if (M <= 0.0) {
        out.centroid = Point3 {0.0, 0.0, 0.0};
        out.inertia = {};
        return;
    }
    out.centroid = Point3 {(m0.volume * m0.centroid.x + m1.volume * m1.centroid.x) / M,
                           (m0.volume * m0.centroid.y + m1.volume * m1.centroid.y) / M,
                           (m0.volume * m0.centroid.z + m1.volume * m1.centroid.z) / M};
    std::array<Scalar, 9> I {};
    for (int i = 0; i < 9; ++i) {
        I[static_cast<std::size_t>(i)] = m0.inertia[static_cast<std::size_t>(i)] + m1.inertia[static_cast<std::size_t>(i)];
    }
    add_inertia_parallel_axis(I, m0.volume, m0.centroid, out.centroid);
    add_inertia_parallel_axis(I, m1.volume, m1.centroid, out.centroid);
    out.inertia = I;
}

bool mass_properties_axis_aligned_bbox_solid(const BoundingBox& bb, MassProperties& out) {
    if (!bb.is_valid) {
        return false;
    }
    const Scalar dx = std::max<Scalar>(0.0, bb.max.x - bb.min.x);
    const Scalar dy = std::max<Scalar>(0.0, bb.max.y - bb.min.y);
    const Scalar dz = std::max<Scalar>(0.0, bb.max.z - bb.min.z);
    if (dx <= 0.0 || dy <= 0.0 || dz <= 0.0) {
        return false;
    }
    out.volume = dx * dy * dz;
    out.area = 2.0 * (dx * dy + dy * dz + dx * dz);
    out.centroid = Point3 {(bb.min.x + bb.max.x) * 0.5, (bb.min.y + bb.max.y) * 0.5, (bb.min.z + bb.max.z) * 0.5};
    set_box_inertia_about_centroid(out, dx, dy, dz);
    return true;
}

/// 轴对齐长方体「大减全含小」（ρ=1）：体积/质心/惯性差分；表面积取外+内腔占位和。
bool try_subtract_nested_axis_aligned_box_mass_properties(const MassProperties& m_big, const MassProperties& m_small,
                                                            MassProperties& out) {
    const Scalar M = m_big.volume - m_small.volume;
    if (M <= 1e-18) {
        return false;
    }
    out.volume = M;
    out.area = m_big.area + m_small.area;
    out.centroid = Point3 {(m_big.volume * m_big.centroid.x - m_small.volume * m_small.centroid.x) / M,
                           (m_big.volume * m_big.centroid.y - m_small.volume * m_small.centroid.y) / M,
                           (m_big.volume * m_big.centroid.z - m_small.volume * m_small.centroid.z) / M};
    std::array<Scalar, 9> T0 {};
    for (int i = 0; i < 9; ++i) {
        T0[static_cast<std::size_t>(i)] = m_big.inertia[static_cast<std::size_t>(i)];
    }
    add_inertia_parallel_axis(T0, m_big.volume, m_big.centroid, out.centroid);
    std::array<Scalar, 9> T1 {};
    for (int i = 0; i < 9; ++i) {
        T1[static_cast<std::size_t>(i)] = m_small.inertia[static_cast<std::size_t>(i)];
    }
    add_inertia_parallel_axis(T1, m_small.volume, m_small.centroid, out.centroid);
    for (int i = 0; i < 9; ++i) {
        out.inertia[static_cast<std::size_t>(i)] = T0[static_cast<std::size_t>(i)] - T1[static_cast<std::size_t>(i)];
    }
    if (out.inertia[0] <= 0.0 || out.inertia[4] <= 0.0 || out.inertia[8] <= 0.0) {
        return false;
    }
    return true;
}

/// 布尔结果体：两操作数均有可解释占位/解析质量时，在包围盒语义与 AABB 关系一致的前提下组合或退化为左/大实体积。
/// - 若 `has_boolean_op`：`Intersect`+双盒且结果 bbox≈AABB 交 → 交叠长方体解析量；`Subtract`+左含右双盒 → 体积差与差分惯性占位。
/// - `Union`/`Split`：`Disjoint` + 结果 bbox≈union → 体积/面积相加；`Touching`/`Overlapping` + union 体积≈体积之和时相加。
/// - `LhsContainsRhs`/`RhsContainsLhs` + 结果 bbox 与较大体一致：**仅 Union/Split**（或无缓存字段时的兼容路径）取大包络体。
/// - `Disjoint` + 结果 bbox≈左操作数 → 左体（减法语义或兼容）。
bool try_boolean_two_operand_mass_properties(const detail::KernelState& state, const detail::BodyRecord& body,
                                             MassProperties& out) {
    if (body.source_bodies.size() != 2) {
        return false;
    }
    const auto it0 = state.bodies.find(body.source_bodies[0].value);
    const auto it1 = state.bodies.find(body.source_bodies[1].value);
    if (it0 == state.bodies.end() || it1 == state.bodies.end()) {
        return false;
    }
    const auto& b0 = it0->second;
    const auto& b1 = it1->second;
    MassProperties m0 {};
    MassProperties m1 {};
    if (!try_boolean_operand_mass_properties(b0, m0) || !try_boolean_operand_mass_properties(b1, m1)) {
        return false;
    }
    if (!(m0.volume > 0.0) || !(m1.volume > 0.0)) {
        return false;
    }
    const auto rel = classify_bbox_relation(b0.bbox, b1.bbox);
    const Scalar lin = detail::resolve_linear_tolerance(0.0, state.config.tolerance);
    const Scalar eps = std::max(Scalar {1e-7}, lin * Scalar {128});
    const auto merged = union_bbox(b0.bbox, b1.bbox);
    const bool use_op = body.has_boolean_op;
    const auto bop = body.boolean_op;

    if (use_op && bop == BooleanOp::Intersect && b0.kind == detail::BodyKind::Box && b1.kind == detail::BodyKind::Box) {
        const auto ix = intersect_bbox(b0.bbox, b1.bbox);
        if (ix.is_valid && bbox_corners_almost_equal(body.bbox, ix, eps)) {
            return mass_properties_axis_aligned_bbox_solid(ix, out);
        }
        return false;
    }

    if (use_op && bop == BooleanOp::Subtract && rel == BBoxRelation::LhsContainsRhs &&
        b0.kind == detail::BodyKind::Box && b1.kind == detail::BodyKind::Box &&
        bbox_corners_almost_equal(body.bbox, b0.bbox, eps) && m0.volume > m1.volume * (1.0 + 1e-12)) {
        if (try_subtract_nested_axis_aligned_box_mass_properties(m0, m1, out)) {
            return true;
        }
    }

    if (rel == BBoxRelation::LhsContainsRhs && bbox_corners_almost_equal(body.bbox, b0.bbox, eps)) {
        if (!use_op || bop == BooleanOp::Union || bop == BooleanOp::Split) {
            out = m0;
            return true;
        }
    }
    if (rel == BBoxRelation::RhsContainsLhs && bbox_corners_almost_equal(body.bbox, b1.bbox, eps)) {
        if (!use_op || bop == BooleanOp::Union || bop == BooleanOp::Split) {
            out = m1;
            return true;
        }
    }
    if (rel == BBoxRelation::Disjoint && bbox_corners_almost_equal(body.bbox, b0.bbox, eps)) {
        out = m0;
        return true;
    }
    if (!bbox_corners_almost_equal(body.bbox, merged, eps)) {
        return false;
    }
    if (!use_op || bop == BooleanOp::Union || bop == BooleanOp::Split) {
        if (rel == BBoxRelation::Disjoint) {
            combine_mass_properties_sum(m0, m1, out);
            return true;
        }
        if (rel == BBoxRelation::Touching || rel == BBoxRelation::Overlapping) {
            const Scalar vsum = m0.volume + m1.volume;
            const Scalar vb = bbox_volume(merged);
            const Scalar tol = std::max(Scalar {1e-9}, lin * Scalar {4096}) +
                               Scalar {1e-9} * std::max(vsum, vb);
            if (std::abs(vb - vsum) > tol) {
                return false;
            }
            combine_mass_properties_sum(m0, m1, out);
            return true;
        }
    }
    return false;
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
    if (code == kBoolPrepCandidatesBuilt || code == kBoolLocalClipApplied || code == kBoolFaceCandidatesBuilt) {
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
    set_boolean_diagnostic_stage(issue, diag_codes::kBoolPrepCandidatesBuilt);
    state.append_diagnostic_issue(diag, std::move(issue));

    if (prep.local_clip_applied) {
        auto clip_issue = detail::make_info_issue(
            diag_codes::kBoolLocalClipApplied,
            "布尔局部裁剪已应用：结果 bbox 已按候选重叠区域收缩");
        clip_issue.related_entities = {lhs.value, rhs.value};
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
    set_boolean_diagnostic_stage(issue, diag_codes::kBoolIntersectionWiresStored);
    state.append_diagnostic_issue(diag, std::move(issue));
}

