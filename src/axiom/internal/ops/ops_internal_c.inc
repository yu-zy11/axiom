std::optional<std::array<VertexId, 2>> oriented_vertices_for_coedge_local(const detail::KernelState& state, CoedgeId coedge_id) {
    const auto coedge_it = state.coedges.find(coedge_id.value);
    if (coedge_it == state.coedges.end()) {
        return std::nullopt;
    }
    const auto edge_it = state.edges.find(coedge_it->second.edge_id.value);
    if (edge_it == state.edges.end()) {
        return std::nullopt;
    }
    if (!detail::has_vertex(state, edge_it->second.v0) || !detail::has_vertex(state, edge_it->second.v1) ||
        edge_it->second.v0.value == edge_it->second.v1.value) {
        return std::nullopt;
    }
    if (coedge_it->second.reversed) {
        return std::array<VertexId, 2> {edge_it->second.v1, edge_it->second.v0};
    }
    return std::array<VertexId, 2> {edge_it->second.v0, edge_it->second.v1};
}

bool coedge_reversed_for_oriented_edge(const detail::KernelState& state, EdgeId edge_id, VertexId start, VertexId end, bool& out_reversed) {
    const auto edge_it = state.edges.find(edge_id.value);
    if (edge_it == state.edges.end()) {
        return false;
    }
    const auto v0 = edge_it->second.v0;
    const auto v1 = edge_it->second.v1;
    if (v0.value == start.value && v1.value == end.value) {
        out_reversed = false;
        return true;
    }
    if (v1.value == start.value && v0.value == end.value) {
        out_reversed = true;
        return true;
    }
    return false;
}


bool build_rect_frame_from_loop(const detail::KernelState& state,
                                LoopId loop_id,
                                std::array<VertexId, 4>& out_verts,
                                RectFrame& out_frame) {
    const auto loop_it = state.loops.find(loop_id.value);
    if (loop_it == state.loops.end() || loop_it->second.coedges.size() != 4) {
        return false;
    }
    for (std::size_t i = 0; i < 4; ++i) {
        const auto oriented = oriented_vertices_for_coedge_local(state, loop_it->second.coedges[i]);
        if (!oriented.has_value()) {
            return false;
        }
        out_verts[i] = (*oriented)[0];
    }
    const auto v0_it = state.vertices.find(out_verts[0].value);
    const auto v1_it = state.vertices.find(out_verts[1].value);
    const auto v3_it = state.vertices.find(out_verts[3].value);
    if (v0_it == state.vertices.end() || v1_it == state.vertices.end() || v3_it == state.vertices.end()) {
        return false;
    }
    const auto u_vec = detail::subtract(v1_it->second.point, v0_it->second.point);
    const auto v_vec = detail::subtract(v3_it->second.point, v0_it->second.point);
    const auto u_len = detail::norm(u_vec);
    const auto v_len = detail::norm(v_vec);
    if (!(u_len > 1e-12) || !(v_len > 1e-12)) {
        return false;
    }
    out_frame.origin = v0_it->second.point;
    out_frame.u_unit = detail::scale(u_vec, 1.0 / u_len);
    out_frame.v_unit = detail::scale(v_vec, 1.0 / v_len);
    out_frame.u_len = u_len;
    out_frame.v_len = v_len;
    return true;
}

std::array<Scalar, 2> to_uv(const RectFrame& f, const Point3& p) {
    const Vec3 op {p.x - f.origin.x, p.y - f.origin.y, p.z - f.origin.z};
    return {detail::dot(op, f.u_unit), detail::dot(op, f.v_unit)};
}

Point3 from_uv(const RectFrame& f, Scalar u, Scalar v) {
    const auto pu = detail::scale(f.u_unit, u);
    const auto pv = detail::scale(f.v_unit, v);
    return Point3 {f.origin.x + pu.x + pv.x, f.origin.y + pu.y + pv.y, f.origin.z + pu.z + pv.z};
}

bool uv_inside_rect(const RectFrame& f, Scalar u, Scalar v, Scalar eps) {
    return u >= -eps && u <= f.u_len + eps && v >= -eps && v <= f.v_len + eps;
}


std::vector<RectIntersection> intersect_line_with_rect_uv(const RectFrame& f,
                                                          const std::array<Scalar, 2>& p0,
                                                          const std::array<Scalar, 2>& p1) {
    std::vector<RectIntersection> hits;
    const auto du = p1[0] - p0[0];
    const auto dv = p1[1] - p0[1];
    const auto eps = 1e-9 * std::max<Scalar>(1.0, std::max(f.u_len, f.v_len));

    auto add_hit = [&hits, eps](RectIntersection h) {
        for (const auto& e : hits) {
            if (std::abs(e.u - h.u) <= eps && std::abs(e.v - h.v) <= eps) {
                return;
            }
        }
        hits.push_back(h);
    };

    auto try_u = [&](Scalar u_target, int boundary) {
        if (std::abs(du) <= 1e-14) {
            return;
        }
        const auto t = (u_target - p0[0]) / du;
        const auto v = p0[1] + t * dv;
        if (uv_inside_rect(f, u_target, v, eps)) {
            add_hit(RectIntersection {u_target, v, boundary});
        }
    };
    auto try_v = [&](Scalar v_target, int boundary) {
        if (std::abs(dv) <= 1e-14) {
            return;
        }
        const auto t = (v_target - p0[1]) / dv;
        const auto u = p0[0] + t * du;
        if (uv_inside_rect(f, u, v_target, eps)) {
            add_hit(RectIntersection {u, v_target, boundary});
        }
    };

    try_u(0.0, 0);
    try_u(f.u_len, 1);
    try_v(0.0, 2);
    try_v(f.v_len, 3);
    return hits;
}

EdgeId create_line_edge(detail::KernelState& state, VertexId a, VertexId b) {
    const auto a_it = state.vertices.find(a.value);
    const auto b_it = state.vertices.find(b.value);
    detail::CurveRecord curve;
    curve.kind = detail::CurveKind::Line;
    curve.origin = a_it->second.point;
    curve.direction = detail::normalize(detail::subtract(b_it->second.point, a_it->second.point));
    const auto curve_id = CurveId {state.allocate_id()};
    state.curves.emplace(curve_id.value, std::move(curve));
    const auto edge_id = EdgeId {state.allocate_id()};
    state.edges.emplace(edge_id.value, detail::EdgeRecord {curve_id, a, b});
    return edge_id;
}


// Split an existing edge inside a shell: replace each coedge in that shell by two coedges referencing new edges.
std::optional<SplitEdgeResult> split_edge_in_shell(detail::KernelState& state, ShellId shell_id, EdgeId edge_id, VertexId mid_vertex) {
    const auto edge_it = state.edges.find(edge_id.value);
    if (edge_it == state.edges.end()) {
        return std::nullopt;
    }
    const auto v0 = edge_it->second.v0;
    const auto v1 = edge_it->second.v1;
    if (v0.value == mid_vertex.value || v1.value == mid_vertex.value) {
        return std::nullopt;
    }
    if (!detail::has_vertex(state, v0) || !detail::has_vertex(state, v1) || !detail::has_vertex(state, mid_vertex)) {
        return std::nullopt;
    }

    const auto e0 = create_line_edge(state, v0, mid_vertex);
    const auto e1 = create_line_edge(state, mid_vertex, v1);

    // Prefer scanning the shell topology directly. This is more robust than relying on
    // `edge_to_coedges`, which may be stale/partial during in-place imprint operations.
    std::vector<std::uint64_t> target_coedges;
    {
        const auto shell_it = state.shells.find(shell_id.value);
        if (shell_it != state.shells.end()) {
            for (const auto face_id : shell_it->second.faces) {
                const auto face_it = state.faces.find(face_id.value);
                if (face_it == state.faces.end()) continue;
                std::vector<LoopId> loops;
                loops.push_back(face_it->second.outer_loop);
                loops.insert(loops.end(), face_it->second.inner_loops.begin(), face_it->second.inner_loops.end());
                for (const auto loop_id : loops) {
                    const auto loop_it = state.loops.find(loop_id.value);
                    if (loop_it == state.loops.end()) continue;
                    for (const auto coedge_id : loop_it->second.coedges) {
                        const auto coedge_it = state.coedges.find(coedge_id.value);
                        if (coedge_it == state.coedges.end()) continue;
                        if (coedge_it->second.edge_id.value == edge_id.value) {
                            target_coedges.push_back(coedge_id.value);
                        }
                    }
                }
            }
        }
    }

    // Fall back to the edge index if the scan couldn't find any coedges.
    if (target_coedges.empty()) {
        const auto it = state.edge_to_coedges.find(edge_id.value);
        if (it == state.edge_to_coedges.end()) {
            return SplitEdgeResult {e0, e1, v0, v1};
        }
        target_coedges.assign(it->second.begin(), it->second.end());
    }

    for (const auto coedge_value : target_coedges) {
        const auto loop_map_it = state.coedge_to_loop.find(coedge_value);
        if (loop_map_it == state.coedge_to_loop.end()) {
            continue;
        }
        const auto loop_id = LoopId {loop_map_it->second};
        const auto loop_it = state.loops.find(loop_id.value);
        if (loop_it == state.loops.end()) {
            continue;
        }
        const auto coedge_it = state.coedges.find(coedge_value);
        if (coedge_it == state.coedges.end()) {
            continue;
        }
        const bool reversed = coedge_it->second.reversed;

        // Create two new coedges, ordering depends on the original coedge orientation.
        const auto c0 = CoedgeId {state.allocate_id()};
        const auto c1 = CoedgeId {state.allocate_id()};
        if (!reversed) {
            state.coedges.emplace(c0.value, detail::CoedgeRecord {e0, false});
            state.coedges.emplace(c1.value, detail::CoedgeRecord {e1, false});
        } else {
            state.coedges.emplace(c0.value, detail::CoedgeRecord {e1, true});
            state.coedges.emplace(c1.value, detail::CoedgeRecord {e0, true});
        }

        auto& coedges = loop_it->second.coedges;
        for (std::size_t i = 0; i < coedges.size(); ++i) {
            if (coedges[i].value == coedge_value) {
                coedges[i] = c0;
                coedges.insert(coedges.begin() + static_cast<std::ptrdiff_t>(i + 1), c1);
                break;
            }
        }
    }
    return SplitEdgeResult {e0, e1, v0, v1};
}

// Segment-driven imprint: split a rectangular plane face using the intersection segment direction.
// Supports the common "opposite edges" cut (u=0 with u=u_len) or (v=0 with v=v_len).
bool imprint_split_rect_face_by_segment(detail::KernelState& state,
                                        ShellId shell_id,
                                        FaceId face_id,
                                        CurveId segment_curve_id) {
    const auto shell_it = state.shells.find(shell_id.value);
    const auto face_it = state.faces.find(face_id.value);
    if (shell_it == state.shells.end() || face_it == state.faces.end()) {
        return false;
    }
    const auto surf_it = state.surfaces.find(face_it->second.surface_id.value);
    if (surf_it == state.surfaces.end() || surf_it->second.kind != detail::SurfaceKind::Plane) {
        return false;
    }
    const auto loop_id = face_it->second.outer_loop;

    std::array<VertexId, 4> verts {};
    RectFrame frame {};
    if (!build_rect_frame_from_loop(state, loop_id, verts, frame)) {
        return false;
    }

    const auto seg_it = state.curves.find(segment_curve_id.value);
    if (seg_it == state.curves.end() || seg_it->second.kind != detail::CurveKind::LineSegment || seg_it->second.poles.size() < 2) {
        return false;
    }
    const auto uv0 = to_uv(frame, seg_it->second.poles.front());
    const auto uv1 = to_uv(frame, seg_it->second.poles.back());
    auto hits = intersect_line_with_rect_uv(frame, uv0, uv1);
    if (hits.size() < 2) {
        return false;
    }

    // Pick two hits on opposite boundaries.
    std::optional<RectIntersection> h0;
    std::optional<RectIntersection> h1;
    auto pick = [&](int b0, int b1) -> bool {
        std::optional<RectIntersection> a;
        std::optional<RectIntersection> b;
        for (const auto& h : hits) {
            if (h.boundary == b0 && !a.has_value()) a = h;
            if (h.boundary == b1 && !b.has_value()) b = h;
        }
        if (a.has_value() && b.has_value()) {
            h0 = a;
            h1 = b;
            return true;
        }
        return false;
    };
    const bool ok_lr = pick(0, 1);
    const bool ok_bt = ok_lr ? false : pick(2, 3);
    if (!ok_lr && !ok_bt) {
        return false;
    }

    // Map boundaries to rectangle edges indices (from loop order v0->v1->v2->v3->v0):
    // boundary 0(u=0)   => edge v3->v0 (edges[3])
    // boundary 1(u=uLen)=> edge v1->v2 (edges[1])
    // boundary 2(v=0)   => edge v0->v1 (edges[0])
    // boundary 3(v=vLen)=> edge v2->v3 (edges[2])
    const auto loop_it = state.loops.find(loop_id.value);
    std::array<std::uint64_t, 4> eids {};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto coe_it = state.coedges.find(loop_it->second.coedges[i].value);
        if (coe_it == state.coedges.end()) return false;
        eids[i] = coe_it->second.edge_id.value;
    }

    auto make_split_vertex = [&](const RectIntersection& h) -> VertexId {
        const auto p = from_uv(frame, h.u, h.v);
        const auto vid = VertexId {state.allocate_id()};
        state.vertices.emplace(vid.value, detail::VertexRecord {p});
        return vid;
    };
    const auto va = make_split_vertex(*h0);
    const auto vb = make_split_vertex(*h1);

    // Split edges and build two quad loops.
    // For left/right: cut between edges[3] (v3-v0) and edges[1] (v1-v2)
    // For bottom/top: cut between edges[0] (v0-v1) and edges[2] (v2-v3)
    std::array<EdgeId, 2> split_edges {};
    std::array<VertexId, 2> split_end_a {};
    std::array<VertexId, 2> split_end_b {};
    if (ok_lr) {
        split_edges = {EdgeId {eids[3]}, EdgeId {eids[1]}};
        split_end_a = {verts[3], verts[0]};
        split_end_b = {verts[1], verts[2]};
    } else {
        split_edges = {EdgeId {eids[0]}, EdgeId {eids[2]}};
        split_end_a = {verts[0], verts[1]};
        split_end_b = {verts[2], verts[3]};
    }

    // Split the two hit edges in-place across the whole shell to preserve closedness.
    const auto split_a = split_edge_in_shell(state, shell_id, split_edges[0], va);
    const auto split_b = split_edge_in_shell(state, shell_id, split_edges[1], vb);
    if (!split_a.has_value() || !split_b.has_value()) {
        return false;
    }
    const auto cut = create_line_edge(state, va, vb);

    // Pick correct segment edge ids for the target loops by oriented endpoints.
    auto pick_segment = [&](const SplitEdgeResult& s, VertexId start, VertexId end) -> EdgeId {
        bool rev = false;
        if (coedge_reversed_for_oriented_edge(state, s.e0, start, end, rev)) {
            return s.e0;
        }
        if (coedge_reversed_for_oriented_edge(state, s.e1, start, end, rev)) {
            return s.e1;
        }
        return {};
    };
    // For loop construction we need four boundary segments:
    EdgeId seg_a_to_0 {};
    EdgeId seg_3_to_a {};
    EdgeId seg_1_to_b {};
    EdgeId seg_b_to_2 {};
    EdgeId seg_0_to_a {};
    EdgeId seg_a_to_1 {};
    EdgeId seg_2_to_b {};
    EdgeId seg_b_to_3 {};
    if (ok_lr) {
        // left edge between v3 and v0 split at va
        seg_3_to_a = pick_segment(*split_a, verts[3], va);
        seg_a_to_0 = pick_segment(*split_a, va, verts[0]);
        // right edge between v1 and v2 split at vb
        seg_1_to_b = pick_segment(*split_b, verts[1], vb);
        seg_b_to_2 = pick_segment(*split_b, vb, verts[2]);
        if (seg_3_to_a.value == 0 || seg_a_to_0.value == 0 || seg_1_to_b.value == 0 || seg_b_to_2.value == 0) {
            return false;
        }
    } else {
        // bottom edge between v0 and v1 split at va
        seg_0_to_a = pick_segment(*split_a, verts[0], va);
        seg_a_to_1 = pick_segment(*split_a, va, verts[1]);
        // top edge between v2 and v3 split at vb
        seg_2_to_b = pick_segment(*split_b, verts[2], vb);
        seg_b_to_3 = pick_segment(*split_b, vb, verts[3]);
        if (seg_0_to_a.value == 0 || seg_a_to_1.value == 0 || seg_2_to_b.value == 0 || seg_b_to_3.value == 0) {
            return false;
        }
    }

    auto make_coedge = [&](EdgeId e, VertexId s, VertexId t) -> CoedgeId {
        bool rev = false;
        if (!coedge_reversed_for_oriented_edge(state, e, s, t, rev)) {
            return {};
        }
        const auto cid = CoedgeId {state.allocate_id()};
        state.coedges.emplace(cid.value, detail::CoedgeRecord {e, rev});
        return cid;
    };

    // Build two loops depending on cut orientation.
    LoopId loop_a {};
    LoopId loop_b {};
    if (ok_lr) {
        // loop_a: v0 -> v1 -> vb -> va -> v0
        const auto c01 = make_coedge(EdgeId {eids[0]}, verts[0], verts[1]);
        const auto c1b = make_coedge(seg_1_to_b, verts[1], vb);
        const auto cba = make_coedge(cut, vb, va);
        const auto ca0 = make_coedge(seg_a_to_0, va, verts[0]);
        if (c01.value == 0 || c1b.value == 0 || cba.value == 0 || ca0.value == 0) return false;
        loop_a = LoopId {state.allocate_id()};
        state.loops.emplace(loop_a.value, detail::LoopRecord {std::vector<CoedgeId> {c01, c1b, cba, ca0}});

        // loop_b: va -> vb -> v2 -> v3 -> va
        const auto cab = make_coedge(cut, va, vb);
        const auto cb2 = make_coedge(seg_b_to_2, vb, verts[2]);
        const auto c23 = make_coedge(EdgeId {eids[2]}, verts[2], verts[3]);
        const auto c3a = make_coedge(seg_3_to_a, verts[3], va);
        if (cab.value == 0 || cb2.value == 0 || c23.value == 0 || c3a.value == 0) return false;
        loop_b = LoopId {state.allocate_id()};
        state.loops.emplace(loop_b.value, detail::LoopRecord {std::vector<CoedgeId> {cab, cb2, c23, c3a}});
    } else {
        // bottom/top cut:
        // loop_a: v0 -> va -> vb -> v3 -> v0 (uses eA0, cut, eB1, edge v3->v0)
        const auto c0a = make_coedge(seg_0_to_a, verts[0], va);
        const auto cab = make_coedge(cut, va, vb);
        const auto cb3 = make_coedge(seg_b_to_3, vb, verts[3]);
        const auto c30 = make_coedge(EdgeId {eids[3]}, verts[3], verts[0]);
        if (c0a.value == 0 || cab.value == 0 || cb3.value == 0 || c30.value == 0) return false;
        loop_a = LoopId {state.allocate_id()};
        state.loops.emplace(loop_a.value, detail::LoopRecord {std::vector<CoedgeId> {c0a, cab, cb3, c30}});

        // loop_b: va -> v1 -> v2 -> vb -> va
        const auto ca1 = make_coedge(seg_a_to_1, va, verts[1]);
        const auto c12 = make_coedge(EdgeId {eids[1]}, verts[1], verts[2]);
        const auto c2b = make_coedge(seg_2_to_b, verts[2], vb);
        const auto cba = make_coedge(cut, vb, va);
        if (ca1.value == 0 || c12.value == 0 || c2b.value == 0 || cba.value == 0) return false;
        loop_b = LoopId {state.allocate_id()};
        state.loops.emplace(loop_b.value, detail::LoopRecord {std::vector<CoedgeId> {ca1, c12, c2b, cba}});
    }

    // Create two new faces on same surface.
    const auto face_a = FaceId {state.allocate_id()};
    const auto face_b = FaceId {state.allocate_id()};
    detail::FaceRecord fa;
    fa.surface_id = face_it->second.surface_id;
    fa.outer_loop = loop_a;
    fa.source_faces = face_it->second.source_faces.empty() ? std::vector<FaceId> {face_id} : face_it->second.source_faces;
    detail::FaceRecord fb;
    fb.surface_id = face_it->second.surface_id;
    fb.outer_loop = loop_b;
    fb.source_faces = face_it->second.source_faces.empty() ? std::vector<FaceId> {face_id} : face_it->second.source_faces;
    state.faces.emplace(face_a.value, std::move(fa));
    state.faces.emplace(face_b.value, std::move(fb));

    // Replace face in shell with the two faces.
    auto& faces = shell_it->second.faces;
    const auto it = std::find_if(faces.begin(), faces.end(), [face_id](FaceId f) { return f.value == face_id.value; });
    if (it == faces.end()) {
        return false;
    }
    *it = face_a;
    faces.push_back(face_b);
    return true;
}

// Minimal imprint: split a rectangular plane face into two triangle faces along a diagonal.
// If prefer_diag_02 is true, split along (v0 -> v2); otherwise split along (v1 -> v3).
bool imprint_split_rect_face_diagonal(detail::KernelState& state, ShellId shell_id, FaceId face_id, bool prefer_diag_02) {
    const auto shell_it = state.shells.find(shell_id.value);
    const auto face_it = state.faces.find(face_id.value);
    if (shell_it == state.shells.end() || face_it == state.faces.end()) {
        return false;
    }
    const auto loop_it = state.loops.find(face_it->second.outer_loop.value);
    if (loop_it == state.loops.end() || loop_it->second.coedges.size() != 4) {
        return false;
    }
    const auto surf_it = state.surfaces.find(face_it->second.surface_id.value);
    if (surf_it == state.surfaces.end() || surf_it->second.kind != detail::SurfaceKind::Plane) {
        return false;
    }

    std::array<CoedgeId, 4> coedges {
        loop_it->second.coedges[0],
        loop_it->second.coedges[1],
        loop_it->second.coedges[2],
        loop_it->second.coedges[3],
    };
    std::array<std::uint64_t, 4> edges {};
    std::array<VertexId, 4> verts {};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto coe_it = state.coedges.find(coedges[i].value);
        if (coe_it == state.coedges.end()) {
            return false;
        }
        edges[i] = coe_it->second.edge_id.value;
        const auto oriented = oriented_vertices_for_coedge_local(state, coedges[i]);
        if (!oriented.has_value()) {
            return false;
        }
        verts[i] = (*oriented)[0];
    }
    const auto v0 = verts[0];
    const auto v1 = verts[1];
    const auto v2 = verts[2];
    const auto v3 = verts[3];

    const auto diag_start = prefer_diag_02 ? v0 : v1;
    const auto diag_end = prefer_diag_02 ? v2 : v3;

    const auto v0_it = state.vertices.find(diag_start.value);
    const auto v2_it = state.vertices.find(diag_end.value);
    if (v0_it == state.vertices.end() || v2_it == state.vertices.end()) {
        return false;
    }
    // Create diagonal edge.
    detail::CurveRecord diag_curve;
    diag_curve.kind = detail::CurveKind::Line;
    diag_curve.origin = v0_it->second.point;
    diag_curve.direction = detail::normalize(detail::subtract(v2_it->second.point, v0_it->second.point));
    const auto diag_curve_id = CurveId {state.allocate_id()};
    state.curves.emplace(diag_curve_id.value, std::move(diag_curve));

    const auto diag_edge_id = EdgeId {state.allocate_id()};
    state.edges.emplace(diag_edge_id.value, detail::EdgeRecord {diag_curve_id, diag_start, diag_end});

    auto make_tri_loop = [&state](std::array<EdgeId, 3> tri_edges, std::array<bool, 3> tri_rev) -> LoopId {
        std::vector<CoedgeId> coeds;
        coeds.reserve(3);
        for (int i = 0; i < 3; ++i) {
            const auto cid = CoedgeId {state.allocate_id()};
            state.coedges.emplace(cid.value, detail::CoedgeRecord {tri_edges[static_cast<std::size_t>(i)], tri_rev[static_cast<std::size_t>(i)]});
            coeds.push_back(cid);
        }
        const auto lid = LoopId {state.allocate_id()};
        state.loops.emplace(lid.value, detail::LoopRecord {std::move(coeds)});
        return lid;
    };

    LoopId loop_a {};
    LoopId loop_b {};
    if (prefer_diag_02) {
        // Diagonal v0 -> v2 (original behavior).
        bool rev01 = false, rev12 = false, rev20 = false;
        if (!coedge_reversed_for_oriented_edge(state, EdgeId {edges[0]}, v0, v1, rev01) ||
            !coedge_reversed_for_oriented_edge(state, EdgeId {edges[1]}, v1, v2, rev12) ||
            !coedge_reversed_for_oriented_edge(state, diag_edge_id, v2, v0, rev20)) {
            return false;
        }
        loop_a = make_tri_loop({EdgeId {edges[0]}, EdgeId {edges[1]}, diag_edge_id}, {rev01, rev12, rev20});

        bool rev02 = false, rev23 = false, rev30 = false;
        if (!coedge_reversed_for_oriented_edge(state, diag_edge_id, v0, v2, rev02) ||
            !coedge_reversed_for_oriented_edge(state, EdgeId {edges[2]}, v2, v3, rev23) ||
            !coedge_reversed_for_oriented_edge(state, EdgeId {edges[3]}, v3, v0, rev30)) {
            return false;
        }
        loop_b = make_tri_loop({diag_edge_id, EdgeId {edges[2]}, EdgeId {edges[3]}}, {rev02, rev23, rev30});
    } else {
        // Diagonal v1 -> v3.
        bool rev12 = false, rev23 = false, rev31 = false;
        if (!coedge_reversed_for_oriented_edge(state, EdgeId {edges[1]}, v1, v2, rev12) ||
            !coedge_reversed_for_oriented_edge(state, EdgeId {edges[2]}, v2, v3, rev23) ||
            !coedge_reversed_for_oriented_edge(state, diag_edge_id, v3, v1, rev31)) {
            return false;
        }
        loop_a = make_tri_loop({EdgeId {edges[1]}, EdgeId {edges[2]}, diag_edge_id}, {rev12, rev23, rev31});

        bool rev13 = false, rev30 = false, rev01 = false;
        if (!coedge_reversed_for_oriented_edge(state, diag_edge_id, v1, v3, rev13) ||
            !coedge_reversed_for_oriented_edge(state, EdgeId {edges[3]}, v3, v0, rev30) ||
            !coedge_reversed_for_oriented_edge(state, EdgeId {edges[0]}, v0, v1, rev01)) {
            return false;
        }
        loop_b = make_tri_loop({diag_edge_id, EdgeId {edges[3]}, EdgeId {edges[0]}}, {rev13, rev30, rev01});
    }

    // Create two new faces on same surface.
    const auto face_a = FaceId {state.allocate_id()};
    const auto face_b = FaceId {state.allocate_id()};
    detail::FaceRecord fa;
    fa.surface_id = face_it->second.surface_id;
    fa.outer_loop = loop_a;
    fa.source_faces = face_it->second.source_faces.empty() ? std::vector<FaceId> {face_id} : face_it->second.source_faces;
    detail::FaceRecord fb;
    fb.surface_id = face_it->second.surface_id;
    fb.outer_loop = loop_b;
    fb.source_faces = face_it->second.source_faces.empty() ? std::vector<FaceId> {face_id} : face_it->second.source_faces;
    state.faces.emplace(face_a.value, std::move(fa));
    state.faces.emplace(face_b.value, std::move(fb));

    // Replace face in shell with the two faces.
    auto& faces = shell_it->second.faces;
    const auto it = std::find_if(faces.begin(), faces.end(), [face_id](FaceId f) { return f.value == face_id.value; });
    if (it == faces.end()) {
        return false;
    }
    *it = face_a;
    faces.push_back(face_b);
    return true;
}

IntersectionId store_intersection(std::shared_ptr<detail::KernelState> state,
                                  std::string label,
                                  BoundingBox bbox,
                                  std::vector<CurveId> curves,
                                  std::vector<SurfaceId> surfaces) {
    const auto id = IntersectionId {state->allocate_id()};
    detail::IntersectionRecord record;
    record.label = std::move(label);
    record.bbox = bbox;
    record.curves = std::move(curves);
    record.surfaces = std::move(surfaces);
    state->intersections.emplace(id.value, std::move(record));
    return id;
}

