bool boolean_finite_point(const Point3& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

// Read and validate the actual trimmed face, never the body's envelope. This
// also serves the compatibility run's line trimmer; all working data is local.
bool read_boolean_planar_face(const detail::KernelState& state, FaceId id, Scalar tolerance,
                              std::size_t edge_limit, BooleanPlanarFace& out, StatusCode& failure,
                              bool allow_proxy) {
    failure = StatusCode::InvalidTopology;
    const auto fit = state.faces.find(id.value);
    if (fit == state.faces.end()) return false;
    const auto& face = fit->second;
    const auto sit = state.surfaces.find(face.surface_id.value);
    if (sit == state.surfaces.end()) return false;
    if ((!allow_proxy && face.mass_boundary_proxy) || sit->second.kind != detail::SurfaceKind::Plane) {
        failure = StatusCode::NotImplemented;
        return false;
    }
    const auto& surface = sit->second;
    const Scalar length = detail::norm(surface.normal);
    if (!boolean_finite_point(surface.origin) || !std::isfinite(length)) {
        failure = StatusCode::NumericalInstability;
        return false;
    }
    if (!(length > 0)) { failure = StatusCode::DegenerateGeometry; return false; }
    out = {};
    out.id = id;
    out.normal = {surface.normal.x/length,surface.normal.y/length,surface.normal.z/length};
    std::vector<LoopId> loops {face.outer_loop};
    loops.insert(loops.end(), face.inner_loops.begin(), face.inner_loops.end());
    std::size_t count = 0;
    for (const auto loop : loops) {
        const auto lit = state.loops.find(loop.value);
        if (lit == state.loops.end() || lit->second.coedges.size() < 3) return false;
        if (lit->second.coedges.size() > edge_limit - count) {
            failure = StatusCode::OperationFailed;
            return false;
        }
        count += lit->second.coedges.size();
        std::vector<Point3> ring;
        std::vector<EdgeId> edges;
        VertexId first {}, previous {};
        for (const auto cid : lit->second.coedges) {
            const auto cit = state.coedges.find(cid.value);
            if (cit == state.coedges.end()) return false;
            const auto eit = state.edges.find(cit->second.edge_id.value);
            if (eit == state.edges.end()) return false;
            const auto& edge = eit->second;
            const auto a = state.vertices.find(edge.v0.value), b = state.vertices.find(edge.v1.value);
            const auto curve = state.curves.find(edge.curve_id.value);
            if (a == state.vertices.end() || b == state.vertices.end() || curve == state.curves.end()) return false;
            const auto& p = a->second.point;
            const auto& q = b->second.point;
            if (!boolean_finite_point(p) || !boolean_finite_point(q)) {
                failure = StatusCode::NumericalInstability;
                return false;
            }
            const Scalar chord = detail::norm(detail::subtract(q, p));
            if (!std::isfinite(chord)) { failure = StatusCode::NumericalInstability; return false; }
            if (!(chord > tolerance)) { failure = StatusCode::DegenerateGeometry; return false; }
            const auto& c = curve->second;
            if (c.kind == detail::CurveKind::Line) {
                const Scalar direction_length = detail::norm(c.direction);
                if (!boolean_finite_point(c.origin) || !std::isfinite(direction_length) || !(direction_length > 0)) {
                    failure = StatusCode::NumericalInstability;
                    return false;
                }
                const auto direction = detail::scale(c.direction, 1 / direction_length);
                if (detail::norm(detail::cross(detail::subtract(p, c.origin), direction)) > tolerance ||
                    detail::norm(detail::cross(detail::subtract(q, c.origin), direction)) > tolerance) return false;
            } else if (c.kind == detail::CurveKind::LineSegment && c.poles.size() == 2) {
                if (!boolean_finite_point(c.poles.front()) || !boolean_finite_point(c.poles.back())) {
                    failure = StatusCode::NumericalInstability;
                    return false;
                }
                if (detail::norm(detail::subtract(p, c.poles.front())) > tolerance ||
                    detail::norm(detail::subtract(q, c.poles.back())) > tolerance) return false;
            } else {
                failure = StatusCode::NotImplemented;
                return false;
            }
            const VertexId start = cit->second.reversed ? edge.v1 : edge.v0;
            const VertexId end = cit->second.reversed ? edge.v0 : edge.v1;
            if (ring.empty()) first = start;
            else if (previous != start) return false;
            previous = end;
            ring.push_back(cit->second.reversed ? q : p);
            edges.push_back(cit->second.edge_id);
            detail::extend_materialization_bbox(out.bbox, p);
            detail::extend_materialization_bbox(out.bbox, q);
            for (const auto point : {p, q}) {
                const Scalar magnitude = std::max({std::abs(point.x), std::abs(point.y), std::abs(point.z)});
                if (64 * std::numeric_limits<Scalar>::epsilon() * magnitude > tolerance) {
                    failure = StatusCode::NumericalInstability;
                    return false;
                }
                const Scalar residual = detail::dot(out.normal, detail::subtract(point, surface.origin));
                if (!std::isfinite(residual)) { failure = StatusCode::NumericalInstability; return false; }
                if (std::abs(residual) > tolerance) return false;
            }
        }
        if (previous != first) return false;
        out.rings.push_back(std::move(ring));
        out.edges.push_back(std::move(edges));
    }
    // A local anchor on the underlying analytic plane, rather than a snapped
    // boundary vertex defining a different parallel plane.
    const auto first = out.rings.front().front();
    out.origin = detail::add_point_vec(first,detail::scale(out.normal,
        -detail::dot(out.normal,detail::subtract(first,surface.origin))));
    out.u = detail::normalize(detail::subtract(out.rings.front()[1],first));
    out.v = detail::normalize(detail::cross(out.normal, out.u));
    out.u = detail::cross(out.v,out.normal);
    std::vector<std::array<int, 3>> triangles;
    if (out.rings.size() == 1) {
        if (!detail::triangulate_extrude_profile(out.rings.front(), out.normal, triangles, true)) return false;
    } else {
        const std::vector<std::vector<Point3>> holes(out.rings.begin() + 1, out.rings.end());
        std::vector<Point3> points;
        std::vector<std::pair<int, int>> boundary;
        if (!detail::triangulate_extrude_region(out.rings.front(), holes, out.normal, tolerance,
                                               points, boundary, triangles, true)) return false;
    }
    return true;
}

bool boolean_point_in_face(const BooleanPlanarFace& face, const Point3& point, Scalar tolerance) {
    const auto offset = detail::subtract(point, face.origin);
    const Point2 p {detail::dot(offset, face.u), detail::dot(offset, face.v)};
    bool in_outer = false;
    for (std::size_t r = 0; r < face.rings.size(); ++r) {
        const auto& ring = face.rings[r];
        bool inside = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const auto a3 = detail::subtract(ring[i], face.origin);
            const auto b3 = detail::subtract(ring[(i + 1) % ring.size()], face.origin);
            const Point2 a {detail::dot(a3, face.u), detail::dot(a3, face.v)};
            const Point2 b {detail::dot(b3, face.u), detail::dot(b3, face.v)};
            const Scalar dx = b.x - a.x, dy = b.y - a.y;
            const Scalar length2 = dx * dx + dy * dy;
            const Scalar fraction = std::clamp(((p.x-a.x)*dx + (p.y-a.y)*dy) / length2, Scalar(0), Scalar(1));
            if (std::hypot(p.x-a.x-fraction*dx, p.y-a.y-fraction*dy) <= tolerance) return true;
            if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (p.y-a.y)*dx/dy) inside = !inside;
        }
        if (r == 0) in_outer = inside;
        else if (inside) return false;
    }
    return in_outer;
}

// Partition the line at *all* polygon boundary crossings, then classify each
// open interval. Unlike clipping to a convex envelope this retains concavity
// and subtracts holes. Collinear boundary edges contribute their endpoints.
std::vector<Scalar> boolean_line_face_knots(const BooleanPlanarFace& face,
                                           const Point3& origin, const Vec3& direction, Scalar tolerance) {
    std::vector<Scalar> knots;
    const auto side = detail::normalize(detail::cross(face.normal, direction));
    for (const auto& ring : face.rings) {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const auto a = detail::subtract(ring[i], origin);
            const auto b = detail::subtract(ring[(i+1)%ring.size()], origin);
            const Scalar sa = detail::dot(a, side), sb = detail::dot(b, side);
            const Scalar ta = detail::dot(a, direction), tb = detail::dot(b, direction);
            if (std::abs(sa) <= tolerance) knots.push_back(ta);
            if (std::abs(sb) <= tolerance) knots.push_back(tb);
            if ((sa < -tolerance && sb > tolerance) || (sa > tolerance && sb < -tolerance)) {
                const Scalar fraction = sa / (sa-sb);
                knots.push_back(ta + fraction*(tb-ta));
            }
        }
    }
    return knots;
}

std::vector<BooleanBoundaryHit> boolean_boundary_hits(const detail::KernelState& state,
                                                      const BooleanPlanarFace& face,
                                                      const Point3& point, Scalar tolerance) {
    std::vector<BooleanBoundaryHit> hits;
    for (const auto& ring : face.edges) {
        for (const auto id : ring) {
            const auto& edge = state.edges.at(id.value);
            const auto& a = state.vertices.at(edge.v0.value).point;
            const auto delta = detail::subtract(state.vertices.at(edge.v1.value).point, a);
            const Scalar t = std::clamp(detail::dot(detail::subtract(point,a),delta) / detail::dot(delta,delta),
                                        Scalar(0), Scalar(1));
            if (detail::norm(detail::subtract(point,detail::add_point_vec(a,detail::scale(delta,t)))) <= tolerance)
                hits.push_back({face.id, id, t});
        }
    }
    return hits;
}

std::vector<BooleanIntersectionSegment> trim_boolean_planar_line(const detail::KernelState& state,
    const BooleanPlanarFace& lhs, const BooleanPlanarFace& rhs,
    const Point3& origin, const Vec3& direction, Scalar tolerance) {
    auto knots = boolean_line_face_knots(lhs, origin, direction, tolerance);
    const auto rhs_knots = boolean_line_face_knots(rhs, origin, direction, tolerance);
    knots.insert(knots.end(), rhs_knots.begin(), rhs_knots.end());
    std::sort(knots.begin(), knots.end());
    std::vector<Scalar> distinct;
    for (const Scalar t : knots)
        if (distinct.empty() || t-distinct.back() > tolerance) distinct.push_back(t);
    const auto point_at = [&](Scalar t) { return detail::add_point_vec(origin,detail::scale(direction,t)); };
    const auto in_both = [&](Scalar t) {
        const auto point = point_at(t);
        return boolean_point_in_face(lhs,point,tolerance) && boolean_point_in_face(rhs,point,tolerance);
    };
    std::vector<Range1D> intervals;
    for (std::size_t i = 1; i < distinct.size(); ++i) {
        const Scalar a = distinct[i-1], b = distinct[i];
        if (in_both(a + (b-a)*0.5)) {
            if (!intervals.empty() && intervals.back().max == a) intervals.back().max = b;
            else intervals.push_back({a,b});
        }
    }
    for (const Scalar t : distinct) {
        if (in_both(t) && std::none_of(intervals.begin(),intervals.end(),[&](const Range1D& interval) {
            return t >= interval.min-tolerance && t <= interval.max+tolerance;
        })) intervals.push_back({t,t});
    }
    std::sort(intervals.begin(),intervals.end(),[](const Range1D& a,const Range1D& b) { return a.min < b.min; });
    std::vector<BooleanIntersectionSegment> segments;
    for (const auto interval : intervals) {
        BooleanIntersectionSegment segment;
        segment.lhs_face = lhs.id;
        segment.rhs_face = rhs.id;
        segment.begin = point_at(interval.min);
        segment.end = point_at(interval.max);
        segment.point_contact = interval.min == interval.max;
        segment.begin_hits = boolean_boundary_hits(state,lhs,segment.begin,tolerance);
        auto hits = boolean_boundary_hits(state,rhs,segment.begin,tolerance);
        segment.begin_hits.insert(segment.begin_hits.end(),hits.begin(),hits.end());
        segment.end_hits = boolean_boundary_hits(state,lhs,segment.end,tolerance);
        hits = boolean_boundary_hits(state,rhs,segment.end,tolerance);
        segment.end_hits.insert(segment.end_hits.end(),hits.begin(),hits.end());
        segments.push_back(std::move(segment));
    }
    return segments;
}

bool boolean_boxes_overlap(const BoundingBox& a, const BoundingBox& b, Scalar tolerance) {
    return a.is_valid && b.is_valid && a.min.x <= b.max.x+tolerance && b.min.x <= a.max.x+tolerance &&
        a.min.y <= b.max.y+tolerance && b.min.y <= a.max.y+tolerance &&
        a.min.z <= b.max.z+tolerance && b.min.z <= a.max.z+tolerance;
}

Result<BooleanIntersectionPreparation> prepare_planar_boolean_intersections(
    detail::KernelState& state, BodyId lhs, BodyId rhs, const BooleanIntersectionOptions& options,
    bool resolve_coplanar) {
    const auto fail = [&](StatusCode status, std::string_view code, std::string_view stage,
                          const char* message, FaceId lhs_face = {}, FaceId rhs_face = {}) {
        auto issue = detail::make_error_issue(code,message,{lhs.value,rhs.value});
        if (lhs_face.value) issue.related_entities.push_back(lhs_face.value);
        if (rhs_face.value) issue.related_entities.push_back(rhs_face.value);
        issue.stage = std::string(stage);
        issue.numeric_evidence = {{"linear_tolerance",std::isfinite(options.tolerance.linear) ? options.tolerance.linear : 0,"model_unit"},
            {"angular_tolerance",std::isfinite(options.tolerance.angular) ? options.tolerance.angular : 0,"radian"},
            {"linear_tolerance_finite",std::isfinite(options.tolerance.linear) ? 1.0 : 0.0,"bool"},
            {"angular_tolerance_finite",std::isfinite(options.tolerance.angular) ? 1.0 : 0.0,"bool"},
            {"max_face_pairs",static_cast<Scalar>(options.max_face_pairs),"count"},
            {"max_segments",static_cast<Scalar>(options.max_segments),"count"},
            {"max_edges_per_face",static_cast<Scalar>(options.max_edges_per_face),"count"}};
        return error_result<BooleanIntersectionPreparation>(status,
            state.create_diagnostic("布尔几何求交准备失败",{std::move(issue)}));
    };
    const auto& tolerance = options.tolerance;
    if (!detail::valid_tolerance_policy(tolerance) || !std::isfinite(tolerance.linear) ||
        !std::isfinite(tolerance.angular) || !std::isfinite(tolerance.min_local) ||
        !std::isfinite(tolerance.max_local) || !(tolerance.linear > 0) ||
        !(tolerance.angular > 0) || tolerance.angular >= 1 ||
        tolerance.linear < tolerance.min_local || tolerance.linear > tolerance.max_local ||
        (tolerance.precision_mode != PrecisionMode::FastFloat &&
         tolerance.precision_mode != PrecisionMode::AdaptiveCertified &&
         tolerance.precision_mode != PrecisionMode::ExactCritical) ||
        options.max_face_pairs == 0 || options.max_face_pairs > 2000000 ||
        options.max_segments == 0 || options.max_segments > 100000 ||
        options.max_edges_per_face < 3 || options.max_edges_per_face > 256 ||
        !detail::has_body(state,lhs) || !detail::has_body(state,rhs))
        return fail(StatusCode::InvalidInput,diag_codes::kBoolInvalidInput,"bool.prep.candidates",
                    "求交准备输入、容差或预算无效");
    if (tolerance.precision_mode == PrecisionMode::ExactCritical)
        return fail(StatusCode::NotImplemented,diag_codes::kBoolUnsupportedInput,"bool.prep.candidates",
                    "求交准备尚不支持精确算术模式");
    std::array<std::vector<BooleanPlanarFace>,2> faces;
    const std::array<BodyId,2> bodies {lhs,rhs};
    const auto lhs_count = faces_for_body_boolean(state,lhs).size();
    const auto rhs_count = faces_for_body_boolean(state,rhs).size();
    if (lhs_count && rhs_count > options.max_face_pairs / lhs_count)
        return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"bool.prep.candidates",
                    "求交准备超过面候选比较预算");
    for (std::size_t side = 0; side < bodies.size(); ++side) {
        const auto& body = state.bodies.at(bodies[side].value);
        if (body.rep_kind != RepKind::ExactBRep || body.shells.empty())
            return fail(StatusCode::NotImplemented,diag_codes::kBoolUnsupportedInput,"bool.prep.candidates",
                        "求交准备要求具有真实闭壳的 ExactBRep 输入");
        std::vector<FaceId> seen;
        for (const auto shell_id : body.shells) {
            const auto shell = state.shells.find(shell_id.value);
            if (shell == state.shells.end() || shell->second.faces.empty())
                return fail(StatusCode::InvalidTopology,diag_codes::kBoolInvalidInput,"bool.prep.candidates",
                            "求交准备输入壳缺失或为空");
            std::unordered_map<std::uint64_t,std::array<int,2>> uses;
            for (const auto id : shell->second.faces) {
                if (std::find(seen.begin(),seen.end(),id) != seen.end())
                    return fail(StatusCode::InvalidTopology,diag_codes::kBoolInvalidInput,"bool.prep.candidates",
                                "求交准备输入重复引用面",id);
                seen.push_back(id);
                BooleanPlanarFace face;
                StatusCode status {};
                if (!read_boolean_planar_face(state,id,tolerance.linear,options.max_edges_per_face,face,status)) {
                    const auto code = status == StatusCode::NotImplemented ? diag_codes::kBoolUnsupportedInput :
                        status == StatusCode::OperationFailed ? diag_codes::kBoolPreparationBudgetExceeded :
                        status == StatusCode::NumericalInstability ? diag_codes::kBoolNumericalFailure : diag_codes::kBoolInvalidInput;
                    return fail(status,code,"bool.prep.candidates","求交准备面结构、边界、数值或支持范围检查失败",id);
                }
                const auto& record = state.faces.at(id.value);
                std::vector<LoopId> loops {record.outer_loop};
                loops.insert(loops.end(),record.inner_loops.begin(),record.inner_loops.end());
                for (const auto loop : loops) for (const auto cid : state.loops.at(loop.value).coedges) {
                    const auto& coedge = state.coedges.at(cid.value);
                    auto& use = uses[coedge.edge_id.value];
                    ++use[0];
                    use[1] += coedge.reversed ? -1 : 1;
                }
                faces[side].push_back(std::move(face));
            }
            if (std::any_of(uses.begin(),uses.end(),[](const auto& use) {
                return use.second[0] != 2 || use.second[1] != 0;
            })) return fail(StatusCode::InvalidTopology,diag_codes::kBoolInvalidInput,"bool.prep.candidates",
                            "求交准备要求闭合且一致定向的壳边界");
        }
        std::sort(faces[side].begin(),faces[side].end(),[](const auto& a,const auto& b) { return a.id.value < b.id.value; });
    }
    BooleanIntersectionPreparation result;
    std::size_t coplanar_work = lhs_count * rhs_count;
    const auto consume_coplanar = [&](std::size_t count) {
        if (count > options.max_face_pairs-coplanar_work) return false;
        coplanar_work += count;
        return true;
    };
    for (const auto& a : faces[0]) for (const auto& b : faces[1]) {
        if (!boolean_boxes_overlap(a.bbox,b.bbox,tolerance.linear)) continue;
        const auto pair_segment_begin = result.segments.size();
        BooleanFaceCandidate candidate;
        candidate.lhs_face = a.id;
        candidate.rhs_face = b.id;
        for (const auto& ring : a.edges) candidate.lhs_edges.insert(candidate.lhs_edges.end(),ring.begin(),ring.end());
        for (const auto& ring : b.edges) candidate.rhs_edges.insert(candidate.rhs_edges.end(),ring.begin(),ring.end());
        result.candidates.push_back(std::move(candidate));
        const auto cross = detail::cross(a.normal,b.normal);
        const Scalar sine = detail::norm(cross);
        const Scalar distance = detail::dot(b.normal,detail::subtract(b.origin,a.origin));
        if (sine <= tolerance.angular) {
            if (sine > 64*std::numeric_limits<Scalar>::epsilon())
                return fail(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,"bool.intersect",
                            "候选面近平行，交线在角容差内无法可靠确定",a.id,b.id);
            if (std::abs(distance) <= tolerance.linear) {
                if (!resolve_coplanar)
                    return fail(StatusCode::NotImplemented,diag_codes::kBoolCoplanarUnsupported,"bool.intersect",
                                "共面候选需要二维区域求交，禁止用包围盒产生交线",a.id,b.id);
                Scalar resolution = 64 * std::numeric_limits<Scalar>::epsilon();
                for (const auto* face : {&a,&b}) for (const auto& ring : face->rings)
                    for (const auto& point : ring)
                        resolution = std::max(resolution,64 * std::numeric_limits<Scalar>::epsilon() *
                            std::max({std::abs(point.x),std::abs(point.y),std::abs(point.z)}));
                for (const auto* face : {&a,&b}) for (const auto& ring : face->rings)
                    for (const auto& point : ring)
                        if (std::abs(detail::dot(a.normal,detail::subtract(point,a.origin))) > resolution ||
                            std::abs(detail::dot(b.normal,detail::subtract(point,b.origin))) > resolution)
                            return fail(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,"bool.intersect",
                                        "近共面间隙不能按线容差吸附为二维区域",a.id,b.id);
                // Every real ring edge contributes a support line. Partitioning
                // both trimmed regions by these lines resolves overlap and holes
                // independently of either face's original triangulation.
                for (const auto* face : {&a,&b}) for (const auto& ring : face->rings)
                    for (std::size_t i = 0; i < ring.size(); ++i) {
                        const auto origin = ring[i];
                        const auto direction = detail::normalize(detail::subtract(ring[(i+1)%ring.size()],origin));
                        const std::size_t ring_edges = result.candidates.back().lhs_edges.size() +
                            result.candidates.back().rhs_edges.size();
                        // Knot construction scans every real ring; interval and
                        // endpoint membership each scan both trimmed regions.
                        if (!consume_coplanar(ring_edges) || ring_edges > options.max_face_pairs /
                            std::max<std::size_t>(1,8*ring_edges) ||
                            !consume_coplanar(8*ring_edges*ring_edges))
                            return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"bool.intersect",
                                        "共面二维区域约束超过累计几何工作预算",a.id,b.id);
                        auto segments = trim_boolean_planar_line(state,a,b,origin,direction,resolution);
                        for (auto& segment : segments) {
                            for (const auto point : {segment.begin,segment.end})
                                if (!boolean_finite_point(point) ||
                                    std::abs(detail::dot(a.normal,detail::subtract(point,a.origin))) > resolution ||
                                    std::abs(detail::dot(b.normal,detail::subtract(point,b.origin))) > resolution)
                                    return fail(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,"bool.intersect",
                                                "共面约束未通过真实支撑平面残差检查",a.id,b.id);
                            const auto length = detail::norm(detail::subtract(segment.end,segment.begin));
                            if (!segment.point_contact && length <= tolerance.linear)
                                return fail(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,"bool.intersect",
                                            "共面区域交段处于未解析的线容差带",a.id,b.id);
                            // Earlier pairs cannot be duplicates: the equality
                            // contract includes both source faces. Scan only this pair.
                            if (!consume_coplanar(result.segments.size()-pair_segment_begin))
                                return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"bool.intersect",
                                            "共面约束去重超过累计几何工作预算",a.id,b.id);
                            const bool duplicate = std::any_of(result.segments.begin()+
                                static_cast<std::ptrdiff_t>(pair_segment_begin),result.segments.end(),[&](const auto& old) {
                                if (old.lhs_face != a.id || old.rhs_face != b.id) return false;
                                const auto equal = [&](const auto& p,const auto& q) {
                                    return detail::norm(detail::subtract(p,q)) <= resolution;
                                };
                                return (equal(old.begin,segment.begin) && equal(old.end,segment.end)) ||
                                       (equal(old.begin,segment.end) && equal(old.end,segment.begin));
                            });
                            if (duplicate) continue;
                            if (result.segments.size() == options.max_segments)
                                return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"bool.intersect",
                                            "共面二维边界约束超过交段预算",a.id,b.id);
                            if (segment.begin_hits.empty() || segment.end_hits.empty())
                                return fail(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,"bool.intersect",
                                            "共面区域约束端点无法关联真实环边",a.id,b.id);
                            result.segments.push_back(std::move(segment));
                        }
                    }
            }
            continue;
        }
        // Anchor the solve at a real face vertex to avoid subtracting large
        // absolute plane constants. n1 dot (cross(n0,n1) x n0) = sin^2.
        const auto origin = detail::add_point_vec(a.origin,
            detail::scale(detail::cross(cross,a.normal),distance/(sine*sine)));
        const auto direction = detail::scale(cross,1/sine);
        // Check the solve before trimming, including pairs whose line lies far
        // outside both polygons. Otherwise cancellation can erase every knot
        // and turn an unresolved solve into an apparently empty intersection.
        Scalar solve_scale = std::max({std::abs(origin.x),std::abs(origin.y),std::abs(origin.z)});
        for (const auto* face : {&a,&b}) for (const auto& ring : face->rings)
            for (const auto& point : ring)
                solve_scale = std::max(solve_scale,detail::norm(detail::subtract(point,origin)));
        const Scalar lhs_residual = detail::dot(a.normal,detail::subtract(origin,a.origin));
        const Scalar rhs_residual = detail::dot(b.normal,detail::subtract(origin,b.origin));
        if (!boolean_finite_point(origin) || !std::isfinite(solve_scale) ||
            64*std::numeric_limits<Scalar>::epsilon()*solve_scale > tolerance.linear ||
            !std::isfinite(lhs_residual) || !std::isfinite(rhs_residual) ||
            std::abs(lhs_residual) > tolerance.linear || std::abs(rhs_residual) > tolerance.linear)
            return fail(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,"bool.intersect",
                        "候选面解析交线无法在声明的线容差内可靠分辨",a.id,b.id);
        auto segments = trim_boolean_planar_line(state,a,b,origin,direction,tolerance.linear);
        if (segments.size() > options.max_segments-result.segments.size())
            return fail(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,"bool.intersect",
                        "交段输出超过预算",a.id,b.id);
        for (auto& segment : segments) {
            for (const auto point : {segment.begin,segment.end}) {
                if (!boolean_finite_point(point) ||
                    std::abs(detail::dot(a.normal,detail::subtract(point,a.origin))) > tolerance.linear ||
                    std::abs(detail::dot(b.normal,detail::subtract(point,b.origin))) > tolerance.linear)
                    return fail(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,"bool.intersect",
                                "交段未通过独立平面残差检查",a.id,b.id);
            }
            if (segment.begin_hits.empty() || segment.end_hits.empty())
                return fail(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,"bool.intersect",
                            "交段端点无法关联真实边界",a.id,b.id);
            result.segments.push_back(std::move(segment));
        }
    }
    return ok_result(std::move(result));
}

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

std::vector<IntersectionCurve> compute_intersection_curves_for_candidates(const detail::KernelState& state,
                                                                          std::span<const FaceCandidatePair> candidates) {
    std::vector<IntersectionCurve> curves;
    curves.reserve(candidates.size());
    for (const auto& pair : candidates) {
        Point3 origin {};
        Vec3 dir {};
        if (!plane_plane_intersection_line(state, pair.lhs_face, pair.rhs_face, origin, dir)) {
            continue;
        }
        curves.push_back(IntersectionCurve {{}, pair.lhs_face, pair.rhs_face, origin, dir});
    }
    return curves;
}

Result<std::vector<IntersectionSegment>> clip_intersection_lines_to_face_boundaries(
    detail::KernelState& state, std::span<IntersectionCurve> curves, BodyId lhs_body, BodyId rhs_body) {
    std::vector<BooleanIntersectionSegment> trimmed_segments;
    const Scalar tolerance = detail::resolve_linear_tolerance(0.0, state.config.tolerance);
    for (const auto& item : curves) {
        BooleanPlanarFace lhs, rhs;
        StatusCode failure {};
        // Compatibility run may operate on sampled/proxy faces. Trim their
        // actual polygon boundaries, but do not certify analytic curved solids.
        if (!read_boolean_planar_face(state,item.lhs_face,tolerance,256,lhs,failure,true) ||
            !read_boolean_planar_face(state,item.rhs_face,tolerance,256,rhs,failure,true)) {
            const auto code = failure == StatusCode::NotImplemented ? diag_codes::kBoolUnsupportedInput :
                failure == StatusCode::OperationFailed ? diag_codes::kBoolPreparationBudgetExceeded :
                failure == StatusCode::NumericalInstability ? diag_codes::kBoolNumericalFailure : diag_codes::kBoolInvalidInput;
            auto issue = detail::make_error_issue(code,"布尔交线裁剪失败：真实面边界、数值或预算检查失败",
                {lhs_body.value,rhs_body.value,item.lhs_face.value,item.rhs_face.value});
            issue.stage = "bool.intersect.trim";
            issue.numeric_evidence = {{"linear_tolerance",std::isfinite(tolerance) ? tolerance : 0,"model_unit"},
                {"max_edges_per_face",256,"count"},
                {"intersection_curves",static_cast<Scalar>(curves.size()),"count"}};
            return error_result<std::vector<IntersectionSegment>>(failure,
                state.create_diagnostic("布尔交线裁剪失败",{std::move(issue)}));
        }
        auto trimmed = trim_boolean_planar_line(state,lhs,rhs,item.origin,item.direction,tolerance);
        for (const auto& segment : trimmed) {
            if (segment.point_contact) continue;
            trimmed_segments.push_back(segment);
        }
    }
    // Complete all fallible boundary reads before allocating geometry. The run
    // caller likewise waits for this result before creating or invalidating a body.
    for (auto& item : curves) {
        detail::CurveRecord record;
        record.kind = detail::CurveKind::Line;
        record.origin = item.origin;
        record.direction = item.direction;
        item.curve = CurveId {state.allocate_id()};
        state.curves.emplace(item.curve.value,std::move(record));
    }
    std::vector<IntersectionSegment> segments;
    segments.reserve(trimmed_segments.size());
    for (const auto& segment : trimmed_segments) {
        detail::CurveRecord record;
        record.kind = detail::CurveKind::LineSegment;
        record.poles = {segment.begin,segment.end};
        const auto id = CurveId {state.allocate_id()};
        state.curves.emplace(id.value,std::move(record));
        segments.push_back({id,segment.lhs_face,segment.rhs_face});
    }
    return ok_result(std::move(segments));
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
