#include <array>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <string_view>
#include <unordered_set>

#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

namespace {

bool has_issue_code(const axiom::DiagnosticReport& report, std::string_view code) {
    for (const auto& issue : report.issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

bool issue_links_entities(const axiom::DiagnosticReport& report, std::string_view code,
                          std::initializer_list<std::uint64_t> ids) {
    for (const auto& issue : report.issues) {
        if (issue.code != code) {
            continue;
        }
        const std::unordered_set<std::uint64_t> s(issue.related_entities.begin(),
                                                  issue.related_entities.end());
        bool all = true;
        for (const auto id : ids) {
            if (s.count(id) == 0) {
                all = false;
                break;
            }
        }
        if (all) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    axiom::Kernel kernel;

    // ---- Stage 2: Coedge can bind PCurveId (trim bridge foundation) ----
    {
        const std::array<axiom::Point2, 3> uv_poly {{ {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0} }};
        auto pc = kernel.pcurves().make_polyline(uv_poly);
        if (pc.status != axiom::StatusCode::Ok || !pc.value.has_value()) {
            std::cerr << "failed to create pcurve for coedge binding\n";
            return 1;
        }
        auto line_x = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        if (line_x.status != axiom::StatusCode::Ok || !line_x.value.has_value()) {
            std::cerr << "failed to create curve for coedge binding\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
        auto e0 = txn.create_edge(*line_x.value, *v0.value, *v1.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        if (v0.status != axiom::StatusCode::Ok || v1.status != axiom::StatusCode::Ok ||
            e0.status != axiom::StatusCode::Ok || c0.status != axiom::StatusCode::Ok ||
            !v0.value.has_value() || !v1.value.has_value() || !e0.value.has_value() || !c0.value.has_value()) {
            std::cerr << "failed to create topology for coedge binding\n";
            return 1;
        }
        auto bind = txn.set_coedge_pcurve(*c0.value, *pc.value);
        if (bind.status != axiom::StatusCode::Ok) {
            std::cerr << "failed to bind pcurve to coedge\n";
            return 1;
        }
        auto bind_n = txn.coedge_pcurve_bind_count();
        if (bind_n.status != axiom::StatusCode::Ok || !bind_n.value.has_value() ||
            *bind_n.value != 1) {
            std::cerr << "expected coedge_pcurve_bind_count 1 after set_coedge_pcurve\n";
            return 1;
        }
        auto q = kernel.topology().query().pcurve_of_coedge(*c0.value);
        if (q.status != axiom::StatusCode::Ok || !q.value.has_value() ||
            q.value->value != pc.value->value) {
            std::cerr << "unexpected pcurve_of_coedge result\n";
            return 1;
        }
        auto coedge_valid = kernel.topology().validate().validate_coedge(*c0.value);
        if (coedge_valid.status != axiom::StatusCode::Ok) {
            std::cerr << "coedge should validate after binding pcurve\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for coedge binding test\n";
            return 1;
        }
        auto bind_after_rb = txn.coedge_pcurve_bind_count();
        if (bind_after_rb.status != axiom::StatusCode::Ok || !bind_after_rb.value.has_value() ||
            *bind_after_rb.value != 0) {
            std::cerr << "expected coedge_pcurve_bind_count 0 after rollback\n";
            return 1;
        }
    }

    // ---- TopoCore (7.2): read-query audit — nested topology queries count as one top-level op ----
    {
        axiom::Kernel k_read;
        auto q0 = k_read.topology().query().query_operation_count();
        if (q0.status != axiom::StatusCode::Ok || !q0.value.has_value() || *q0.value != 0) {
            std::cerr << "expected initial topology query_operation_count 0\n";
            return 1;
        }
        (void)k_read.topology().query().has_vertex(axiom::VertexId{404});
        auto q1 = k_read.topology().query().query_operation_count();
        if (q1.status != axiom::StatusCode::Ok || !q1.value.has_value() || *q1.value != 1) {
            std::cerr << "expected query_operation_count 1 after has_vertex\n";
            return 1;
        }
        (void)k_read.topology().query().is_edge_boundary(axiom::EdgeId{404});
        auto q2 = k_read.topology().query().query_operation_count();
        if (q2.status != axiom::StatusCode::Ok || !q2.value.has_value() || *q2.value != 2) {
            std::cerr << "expected query_operation_count 2 after is_edge_boundary (nested queries not double-counted)\n";
            return 1;
        }
    }

    // ---- TopoCore (7.2): trim bridge audit — bind count accumulates & survives commit ----
    {
        axiom::Kernel k_bind_audit;
        const std::array<axiom::Point2, 2> uv_a {{{0.0, 0.0}, {1.0, 0.0}}};
        const std::array<axiom::Point2, 2> uv_b {{{1.0, 0.0}, {1.0, 1.0}}};
        auto pca = k_bind_audit.pcurves().make_polyline(uv_a);
        auto pcb = k_bind_audit.pcurves().make_polyline(uv_b);
        auto line_x = k_bind_audit.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        if (pca.status != axiom::StatusCode::Ok || !pca.value.has_value() ||
            pcb.status != axiom::StatusCode::Ok || !pcb.value.has_value() ||
            line_x.status != axiom::StatusCode::Ok || !line_x.value.has_value()) {
            std::cerr << "failed to create geometry for bind-count audit test\n";
            return 1;
        }
        auto txn = k_bind_audit.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
        auto e0 = txn.create_edge(*line_x.value, *v0.value, *v1.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        if (v0.status != axiom::StatusCode::Ok || v1.status != axiom::StatusCode::Ok ||
            e0.status != axiom::StatusCode::Ok || c0.status != axiom::StatusCode::Ok ||
            !v0.value.has_value() || !v1.value.has_value() || !e0.value.has_value() ||
            !c0.value.has_value()) {
            std::cerr << "failed to create topology for bind-count audit test\n";
            return 1;
        }
        if (txn.set_coedge_pcurve(*c0.value, *pca.value).status != axiom::StatusCode::Ok ||
            txn.set_coedge_pcurve(*c0.value, *pcb.value).status != axiom::StatusCode::Ok) {
            std::cerr << "set_coedge_pcurve failed in bind-count audit test\n";
            return 1;
        }
        auto mid = txn.coedge_pcurve_bind_count();
        if (mid.status != axiom::StatusCode::Ok || !mid.value.has_value() || *mid.value != 2) {
            std::cerr << "expected coedge_pcurve_bind_count 2 after two successful binds\n";
            return 1;
        }
        if (txn.commit().status != axiom::StatusCode::Ok) {
            std::cerr << "commit failed in bind-count audit test\n";
            return 1;
        }
        auto after_commit = txn.coedge_pcurve_bind_count();
        if (after_commit.status != axiom::StatusCode::Ok || !after_commit.value.has_value() ||
            *after_commit.value != 2) {
            std::cerr << "expected coedge_pcurve_bind_count 2 preserved after commit\n";
            return 1;
        }
    }

    // ---- TopoCore (7.2): clearing PCurve (id 0) does not increment bind audit counter ----
    {
        axiom::Kernel k_clear_pc;
        const std::array<axiom::Point2, 2> uv {{{0.0, 0.0}, {1.0, 0.0}}};
        auto pc = k_clear_pc.pcurves().make_polyline(uv);
        auto line_x = k_clear_pc.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        if (pc.status != axiom::StatusCode::Ok || !pc.value.has_value() ||
            line_x.status != axiom::StatusCode::Ok || !line_x.value.has_value()) {
            std::cerr << "failed to create geometry for pcurve clear audit test\n";
            return 1;
        }
        auto txn = k_clear_pc.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
        auto e0 = txn.create_edge(*line_x.value, *v0.value, *v1.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        if (v0.status != axiom::StatusCode::Ok || v1.status != axiom::StatusCode::Ok ||
            e0.status != axiom::StatusCode::Ok || c0.status != axiom::StatusCode::Ok ||
            !v0.value.has_value() || !v1.value.has_value() || !e0.value.has_value() ||
            !c0.value.has_value()) {
            std::cerr << "failed to create topology for pcurve clear audit test\n";
            return 1;
        }
        if (txn.set_coedge_pcurve(*c0.value, *pc.value).status != axiom::StatusCode::Ok) {
            std::cerr << "set_coedge_pcurve bind failed in clear audit test\n";
            return 1;
        }
        auto n1 = txn.coedge_pcurve_bind_count();
        if (n1.status != axiom::StatusCode::Ok || !n1.value.has_value() || *n1.value != 1) {
            std::cerr << "expected coedge_pcurve_bind_count 1 after first bind\n";
            return 1;
        }
        if (txn.set_coedge_pcurve(*c0.value, axiom::PCurveId{}).status != axiom::StatusCode::Ok) {
            std::cerr << "set_coedge_pcurve clear failed in clear audit test\n";
            return 1;
        }
        auto n2 = txn.coedge_pcurve_bind_count();
        if (n2.status != axiom::StatusCode::Ok || !n2.value.has_value() || *n2.value != 1) {
            std::cerr << "expected coedge_pcurve_bind_count unchanged after clear (PCurveId 0)\n";
            return 1;
        }
        auto clr = txn.coedge_pcurve_clear_count();
        if (clr.status != axiom::StatusCode::Ok || !clr.value.has_value() || *clr.value != 1) {
            std::cerr << "expected coedge_pcurve_clear_count 1 after PCurveId 0 clear\n";
            return 1;
        }
        if (txn.rollback().status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for pcurve clear audit test\n";
            return 1;
        }
        auto clr_rb = txn.coedge_pcurve_clear_count();
        if (clr_rb.status != axiom::StatusCode::Ok || !clr_rb.value.has_value() ||
            *clr_rb.value != 0) {
            std::cerr << "expected coedge_pcurve_clear_count 0 after rollback\n";
            return 1;
        }
    }

    // ---- TopoCore (7.2): dangling vertex should fail validate_vertex ----
    {
        auto txn = kernel.topology().begin_transaction();
        auto v = txn.create_vertex({99.0, 99.0, 99.0});
        if (v.status != axiom::StatusCode::Ok || !v.value.has_value()) {
            std::cerr << "failed to create isolated vertex for dangling test\n";
            return 1;
        }
        auto bad = kernel.topology().validate().validate_vertex(*v.value);
        if (bad.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_vertex to fail for dangling vertex\n";
            return 1;
        }
        auto d = kernel.diagnostics().get(bad.diagnostic_id);
        if (d.status != axiom::StatusCode::Ok || !d.value.has_value() ||
            !has_issue_code(*d.value, axiom::diag_codes::kTopoDanglingVertex)) {
            std::cerr << "expected kTopoDanglingVertex for vertex unused by edges\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for dangling vertex test\n";
            return 1;
        }
    }

    // ---- TopoCore (7.2): validate_indices_consistency — reverse edge index (edge without coedge) ----
    {
        axiom::Kernel k_edge_index;
        auto line = k_edge_index.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        if (line.status != axiom::StatusCode::Ok || !line.value.has_value()) {
            std::cerr << "failed to create curve for edge reverse-index test\n";
            return 1;
        }
        auto txn = k_edge_index.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
        auto e = txn.create_edge(*line.value, *v0.value, *v1.value);
        if (v0.status != axiom::StatusCode::Ok || v1.status != axiom::StatusCode::Ok ||
            e.status != axiom::StatusCode::Ok || !v0.value.has_value() ||
            !v1.value.has_value() || !e.value.has_value()) {
            std::cerr << "failed to create edge-only topology for reverse-index test\n";
            return 1;
        }
        auto cm = txn.commit();
        if (cm.status != axiom::StatusCode::Ok) {
            std::cerr << "commit failed for edge reverse-index test\n";
            return 1;
        }
        auto idx_bad = k_edge_index.topology().validate().validate_indices_consistency();
        if (idx_bad.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_indices_consistency to fail for edge without coedge\n";
            return 1;
        }
        auto diag = k_edge_index.diagnostics().get(idx_bad.diagnostic_id);
        if (diag.status != axiom::StatusCode::Ok || !diag.value.has_value() ||
            !has_issue_code(*diag.value, axiom::diag_codes::kTopoDanglingEdge)) {
            std::cerr << "expected kTopoDanglingEdge for reverse edge_to_coedges check\n";
            return 1;
        }
    }

    // ---- TopoCore (7.2): validate_indices_consistency — vertex without incident edge ----
    {
        axiom::Kernel k_vertex_index;
        auto txn = k_vertex_index.topology().begin_transaction();
        auto v = txn.create_vertex({3.0, 3.0, 3.0});
        if (v.status != axiom::StatusCode::Ok || !v.value.has_value()) {
            std::cerr << "failed to create lone vertex for index test\n";
            return 1;
        }
        if (txn.commit().status != axiom::StatusCode::Ok) {
            std::cerr << "commit failed for vertex index test\n";
            return 1;
        }
        auto idx_bad = k_vertex_index.topology().validate().validate_indices_consistency();
        if (idx_bad.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_indices_consistency to fail for dangling vertex\n";
            return 1;
        }
        auto diag = k_vertex_index.diagnostics().get(idx_bad.diagnostic_id);
        if (diag.status != axiom::StatusCode::Ok || !diag.value.has_value() ||
            !has_issue_code(*diag.value, axiom::diag_codes::kTopoDanglingVertex)) {
            std::cerr << "expected kTopoDanglingVertex in validate_indices_consistency\n";
            return 1;
        }
    }

    // ---- TopoCore (7.2): validate_indices_consistency — face not in any shell (orphan face) ----
    {
        axiom::Kernel k_face_index;
        auto plane = k_face_index.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto l0 = k_face_index.curves().make_line({0.0, 10.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = k_face_index.curves().make_line({1.0, 10.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = k_face_index.curves().make_line({1.0, 11.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = k_face_index.curves().make_line({0.0, 11.0, 0.0}, {0.0, -1.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            l0.status != axiom::StatusCode::Ok || !l0.value.has_value() ||
            l1.status != axiom::StatusCode::Ok || !l1.value.has_value() ||
            l2.status != axiom::StatusCode::Ok || !l2.value.has_value() ||
            l3.status != axiom::StatusCode::Ok || !l3.value.has_value()) {
            std::cerr << "failed to create geometry for orphan face index test\n";
            return 1;
        }
        auto txn = k_face_index.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 10.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 10.0, 0.0});
        auto v2 = txn.create_vertex({1.0, 11.0, 0.0});
        auto v3 = txn.create_vertex({0.0, 11.0, 0.0});
        auto e0 = txn.create_edge(*l0.value, *v0.value, *v1.value);
        auto e1 = txn.create_edge(*l1.value, *v1.value, *v2.value);
        auto e2 = txn.create_edge(*l2.value, *v2.value, *v3.value);
        auto e3 = txn.create_edge(*l3.value, *v3.value, *v0.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        auto c1 = txn.create_coedge(*e1.value, false);
        auto c2 = txn.create_coedge(*e2.value, false);
        auto c3 = txn.create_coedge(*e3.value, false);
        const std::array<axiom::CoedgeId, 4> co {{*c0.value, *c1.value, *c2.value, *c3.value}};
        auto loop = txn.create_loop(co);
        auto face = txn.create_face(*plane.value, *loop.value, {});
        if (v0.status != axiom::StatusCode::Ok || !v0.value.has_value() ||
            v1.status != axiom::StatusCode::Ok || !v1.value.has_value() ||
            v2.status != axiom::StatusCode::Ok || !v2.value.has_value() ||
            v3.status != axiom::StatusCode::Ok || !v3.value.has_value() ||
            e0.status != axiom::StatusCode::Ok || !e0.value.has_value() ||
            e1.status != axiom::StatusCode::Ok || !e1.value.has_value() ||
            e2.status != axiom::StatusCode::Ok || !e2.value.has_value() ||
            e3.status != axiom::StatusCode::Ok || !e3.value.has_value() ||
            c0.status != axiom::StatusCode::Ok || !c0.value.has_value() ||
            c1.status != axiom::StatusCode::Ok || !c1.value.has_value() ||
            c2.status != axiom::StatusCode::Ok || !c2.value.has_value() ||
            c3.status != axiom::StatusCode::Ok || !c3.value.has_value() ||
            loop.status != axiom::StatusCode::Ok || !loop.value.has_value() ||
            face.status != axiom::StatusCode::Ok || !face.value.has_value()) {
            std::cerr << "failed to build face-without-shell topology for index test\n";
            return 1;
        }
        if (txn.commit().status != axiom::StatusCode::Ok) {
            std::cerr << "commit failed for orphan face index test\n";
            return 1;
        }
        auto idx_bad = k_face_index.topology().validate().validate_indices_consistency();
        if (idx_bad.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_indices_consistency to fail for face without shell\n";
            return 1;
        }
        auto diag = k_face_index.diagnostics().get(idx_bad.diagnostic_id);
        if (diag.status != axiom::StatusCode::Ok || !diag.value.has_value() ||
            !has_issue_code(*diag.value, axiom::diag_codes::kTopoOrphanFace) ||
            !issue_links_entities(*diag.value, axiom::diag_codes::kTopoOrphanFace,
                                  {face.value->value})) {
            std::cerr << "expected kTopoOrphanFace with face id in related_entities\n";
            return 1;
        }
    }

    // ---- TopoCore (7.2): loop single-owner — second create_face with same outer loop must fail ----
    {
        axiom::Kernel k_loop_owner;
        auto plane = k_loop_owner.surfaces().make_plane({0.0, 0.0, -5.0}, {0.0, 0.0, 1.0});
        auto la0 = k_loop_owner.curves().make_line({0.0, -5.0, 0.0}, {1.0, 0.0, 0.0});
        auto la1 = k_loop_owner.curves().make_line({1.0, -5.0, 0.0}, {0.0, 1.0, 0.0});
        auto la2 = k_loop_owner.curves().make_line({1.0, -4.0, 0.0}, {-1.0, 0.0, 0.0});
        auto la3 = k_loop_owner.curves().make_line({0.0, -4.0, 0.0}, {0.0, -1.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            la0.status != axiom::StatusCode::Ok || !la0.value.has_value() ||
            la1.status != axiom::StatusCode::Ok || !la1.value.has_value() ||
            la2.status != axiom::StatusCode::Ok || !la2.value.has_value() ||
            la3.status != axiom::StatusCode::Ok || !la3.value.has_value()) {
            std::cerr << "failed to init geometry for loop single-owner test\n";
            return 1;
        }
        auto txn = k_loop_owner.topology().begin_transaction();
        auto va0 = txn.create_vertex({0.0, -5.0, 0.0});
        auto va1 = txn.create_vertex({1.0, -5.0, 0.0});
        auto va2 = txn.create_vertex({1.0, -4.0, 0.0});
        auto va3 = txn.create_vertex({0.0, -4.0, 0.0});
        auto ea0 = txn.create_edge(*la0.value, *va0.value, *va1.value);
        auto ea1 = txn.create_edge(*la1.value, *va1.value, *va2.value);
        auto ea2 = txn.create_edge(*la2.value, *va2.value, *va3.value);
        auto ea3 = txn.create_edge(*la3.value, *va3.value, *va0.value);
        auto ca0 = txn.create_coedge(*ea0.value, false);
        auto ca1 = txn.create_coedge(*ea1.value, false);
        auto ca2 = txn.create_coedge(*ea2.value, false);
        auto ca3 = txn.create_coedge(*ea3.value, false);
        const std::array<axiom::CoedgeId, 4> coa {
            *ca0.value, *ca1.value, *ca2.value, *ca3.value};
        auto shared_loop = txn.create_loop(coa);
        auto face_a = txn.create_face(*plane.value, *shared_loop.value, {});
        auto face_b = txn.create_face(*plane.value, *shared_loop.value, {});
        if (shared_loop.status != axiom::StatusCode::Ok || !shared_loop.value.has_value() ||
            face_a.status != axiom::StatusCode::Ok || !face_a.value.has_value()) {
            std::cerr << "failed to create first face for loop single-owner test\n";
            return 1;
        }
        if (face_b.status == axiom::StatusCode::Ok) {
            std::cerr << "expected second create_face with shared outer loop to fail\n";
            return 1;
        }
        auto d = k_loop_owner.diagnostics().get(face_b.diagnostic_id);
        if (d.status != axiom::StatusCode::Ok || !d.value.has_value() ||
            !has_issue_code(*d.value, axiom::diag_codes::kTopoFaceOuterLoopInvalid)) {
            std::cerr << "expected kTopoFaceOuterLoopInvalid for shared outer loop\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for loop single-owner test\n";
            return 1;
        }
    }

    // ---- TopoCore (7.2): validate_face checks loop_to_faces ↔ face (happy path + indices) ----
    {
        axiom::Kernel k_ltf;
        auto plane = k_ltf.surfaces().make_plane({0.0, 0.0, -2.0}, {0.0, 0.0, 1.0});
        auto la0 = k_ltf.curves().make_line({0.0, -2.0, 0.0}, {1.0, 0.0, 0.0});
        auto la1 = k_ltf.curves().make_line({1.0, -2.0, 0.0}, {0.0, 1.0, 0.0});
        auto la2 = k_ltf.curves().make_line({1.0, -1.0, 0.0}, {-1.0, 0.0, 0.0});
        auto la3 = k_ltf.curves().make_line({0.0, -1.0, 0.0}, {0.0, -2.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            la0.status != axiom::StatusCode::Ok || !la0.value.has_value() ||
            la1.status != axiom::StatusCode::Ok || !la1.value.has_value() ||
            la2.status != axiom::StatusCode::Ok || !la2.value.has_value() ||
            la3.status != axiom::StatusCode::Ok || !la3.value.has_value()) {
            std::cerr << "failed to init geometry for loop_to_faces face validate test\n";
            return 1;
        }
        auto txn = k_ltf.topology().begin_transaction();
        auto va0 = txn.create_vertex({0.0, -2.0, 0.0});
        auto va1 = txn.create_vertex({1.0, -2.0, 0.0});
        auto va2 = txn.create_vertex({1.0, -1.0, 0.0});
        auto va3 = txn.create_vertex({0.0, -1.0, 0.0});
        auto ea0 = txn.create_edge(*la0.value, *va0.value, *va1.value);
        auto ea1 = txn.create_edge(*la1.value, *va1.value, *va2.value);
        auto ea2 = txn.create_edge(*la2.value, *va2.value, *va3.value);
        auto ea3 = txn.create_edge(*la3.value, *va3.value, *va0.value);
        auto ca0 = txn.create_coedge(*ea0.value, false);
        auto ca1 = txn.create_coedge(*ea1.value, false);
        auto ca2 = txn.create_coedge(*ea2.value, false);
        auto ca3 = txn.create_coedge(*ea3.value, false);
        const std::array<axiom::CoedgeId, 4> coa {
            *ca0.value, *ca1.value, *ca2.value, *ca3.value};
        auto loop_f = txn.create_loop(coa);
        auto face_f = txn.create_face(*plane.value, *loop_f.value, {});
        auto shell_f = txn.create_shell(std::array<axiom::FaceId, 1>{*face_f.value});
        auto body_f = txn.create_body(std::array<axiom::ShellId, 1>{*shell_f.value});
        if (loop_f.status != axiom::StatusCode::Ok || face_f.status != axiom::StatusCode::Ok ||
            shell_f.status != axiom::StatusCode::Ok || body_f.status != axiom::StatusCode::Ok ||
            !face_f.value.has_value()) {
            std::cerr << "failed to build face for loop_to_faces validate test\n";
            return 1;
        }
        auto vf = k_ltf.topology().validate().validate_face(*face_f.value);
        if (vf.status != axiom::StatusCode::Ok) {
            std::cerr << "expected validate_face ok when loop_to_faces matches face\n";
            return 1;
        }
        auto vl = k_ltf.topology().validate().validate_loop(*loop_f.value);
        if (vl.status != axiom::StatusCode::Ok) {
            std::cerr << "expected validate_loop ok when loop_to_faces matches face (scoped index check)\n";
            return 1;
        }
        auto idx = k_ltf.topology().validate().validate_indices_consistency();
        if (idx.status != axiom::StatusCode::Ok) {
            std::cerr << "expected validate_indices_consistency ok in loop_to_faces face test\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for loop_to_faces face validate test\n";
            return 1;
        }
    }

    // ---- TopoCore (7.2): delete_face leaves orphan loop; indices fail; txn rollback restores ----
    {
        auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto l0 = kernel.curves().make_line({0.0, 30.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = kernel.curves().make_line({1.0, 30.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = kernel.curves().make_line({1.0, 31.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = kernel.curves().make_line({0.0, 31.0, 0.0}, {0.0, -1.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            l0.status != axiom::StatusCode::Ok || !l0.value.has_value() ||
            l1.status != axiom::StatusCode::Ok || !l1.value.has_value() ||
            l2.status != axiom::StatusCode::Ok || !l2.value.has_value() ||
            l3.status != axiom::StatusCode::Ok || !l3.value.has_value()) {
            std::cerr << "failed to create geometry for orphan loop index test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 30.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 30.0, 0.0});
        auto v2 = txn.create_vertex({1.0, 31.0, 0.0});
        auto v3 = txn.create_vertex({0.0, 31.0, 0.0});
        auto e0 = txn.create_edge(*l0.value, *v0.value, *v1.value);
        auto e1 = txn.create_edge(*l1.value, *v1.value, *v2.value);
        auto e2 = txn.create_edge(*l2.value, *v2.value, *v3.value);
        auto e3 = txn.create_edge(*l3.value, *v3.value, *v0.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        auto c1 = txn.create_coedge(*e1.value, false);
        auto c2 = txn.create_coedge(*e2.value, false);
        auto c3 = txn.create_coedge(*e3.value, false);
        const std::array<axiom::CoedgeId, 4> co {{ *c0.value, *c1.value, *c2.value, *c3.value }};
        auto loop = txn.create_loop(co);
        auto face = txn.create_face(*plane.value, *loop.value, {});
        auto shell = txn.create_shell(std::array<axiom::FaceId, 1> {*face.value});
        auto body = txn.create_body(std::array<axiom::ShellId, 1> {*shell.value});
        if (loop.status != axiom::StatusCode::Ok || !loop.value.has_value() ||
            face.status != axiom::StatusCode::Ok || !face.value.has_value() ||
            shell.status != axiom::StatusCode::Ok || !shell.value.has_value() ||
            body.status != axiom::StatusCode::Ok || !body.value.has_value()) {
            std::cerr << "failed to build topology for orphan loop index test\n";
            return 1;
        }
        auto del = txn.delete_face(*face.value);
        if (del.status != axiom::StatusCode::Ok) {
            std::cerr << "delete_face failed in orphan loop index test\n";
            return 1;
        }
        auto del_face_n = txn.deleted_face_count();
        if (del_face_n.status != axiom::StatusCode::Ok || !del_face_n.value.has_value() ||
            *del_face_n.value != 1) {
            std::cerr << "expected deleted_face_count 1 after delete_face\n";
            return 1;
        }
        auto idx_bad = kernel.topology().validate().validate_indices_consistency();
        if (idx_bad.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_indices_consistency to fail after orphaning loops\n";
            return 1;
        }
        auto d = kernel.diagnostics().get(idx_bad.diagnostic_id);
        if (d.status != axiom::StatusCode::Ok || !d.value.has_value() ||
            !has_issue_code(*d.value, axiom::diag_codes::kTopoOrphanLoop)) {
            std::cerr << "expected kTopoOrphanLoop in diagnostics after delete_face\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for orphan loop index test\n";
            return 1;
        }
        auto del_face_after_rb = txn.deleted_face_count();
        if (del_face_after_rb.status != axiom::StatusCode::Ok ||
            !del_face_after_rb.value.has_value() || *del_face_after_rb.value != 0) {
            std::cerr << "expected deleted_face_count 0 after rollback\n";
            return 1;
        }
    }

    // ---- TopoCore (7.2): deleted_face_count survives commit (isolated kernel) ----
    {
        axiom::Kernel k_commit_del;
        auto plane = k_commit_del.surfaces().make_plane({0.0, 0.0, 60.0}, {0.0, 0.0, 1.0});
        auto l0 = k_commit_del.curves().make_line({0.0, 60.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = k_commit_del.curves().make_line({1.0, 60.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = k_commit_del.curves().make_line({1.0, 61.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = k_commit_del.curves().make_line({0.0, 61.0, 0.0}, {0.0, -1.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            l0.status != axiom::StatusCode::Ok || !l0.value.has_value() ||
            l1.status != axiom::StatusCode::Ok || !l1.value.has_value() ||
            l2.status != axiom::StatusCode::Ok || !l2.value.has_value() ||
            l3.status != axiom::StatusCode::Ok || !l3.value.has_value()) {
            std::cerr << "failed to create geometry for deleted_face_count commit test\n";
            return 1;
        }
        axiom::FaceId committed_face{0};
        {
            auto txn = k_commit_del.topology().begin_transaction();
            auto v0 = txn.create_vertex({0.0, 60.0, 0.0});
            auto v1 = txn.create_vertex({1.0, 60.0, 0.0});
            auto v2 = txn.create_vertex({1.0, 61.0, 0.0});
            auto v3 = txn.create_vertex({0.0, 61.0, 0.0});
            auto e0 = txn.create_edge(*l0.value, *v0.value, *v1.value);
            auto e1 = txn.create_edge(*l1.value, *v1.value, *v2.value);
            auto e2 = txn.create_edge(*l2.value, *v2.value, *v3.value);
            auto e3 = txn.create_edge(*l3.value, *v3.value, *v0.value);
            auto c0 = txn.create_coedge(*e0.value, false);
            auto c1 = txn.create_coedge(*e1.value, false);
            auto c2 = txn.create_coedge(*e2.value, false);
            auto c3 = txn.create_coedge(*e3.value, false);
            const std::array<axiom::CoedgeId, 4> co {
                {*c0.value, *c1.value, *c2.value, *c3.value}};
            auto loop = txn.create_loop(co);
            auto face = txn.create_face(*plane.value, *loop.value, {});
            auto shell = txn.create_shell(std::array<axiom::FaceId, 1>{*face.value});
            auto body = txn.create_body(std::array<axiom::ShellId, 1>{*shell.value});
            if (loop.status != axiom::StatusCode::Ok || !loop.value.has_value() ||
                face.status != axiom::StatusCode::Ok || !face.value.has_value() ||
                shell.status != axiom::StatusCode::Ok || !shell.value.has_value() ||
                body.status != axiom::StatusCode::Ok || !body.value.has_value()) {
                std::cerr << "failed to build topology for deleted_face_count commit test\n";
                return 1;
            }
            committed_face = *face.value;
            auto cm = txn.commit();
            if (cm.status != axiom::StatusCode::Ok) {
                std::cerr << "commit failed in deleted_face_count commit test\n";
                return 1;
            }
        }
        auto txn2 = k_commit_del.topology().begin_transaction();
        auto del = txn2.delete_face(committed_face);
        if (del.status != axiom::StatusCode::Ok) {
            std::cerr << "delete_face failed after commit in deleted_face_count test\n";
            return 1;
        }
        auto n1 = txn2.deleted_face_count();
        if (n1.status != axiom::StatusCode::Ok || !n1.value.has_value() || *n1.value != 1) {
            std::cerr << "expected deleted_face_count 1 before second commit\n";
            return 1;
        }
        auto cm2 = txn2.commit();
        if (cm2.status != axiom::StatusCode::Ok) {
            std::cerr << "second commit failed in deleted_face_count test\n";
            return 1;
        }
        auto n2 = txn2.deleted_face_count();
        if (n2.status != axiom::StatusCode::Ok || !n2.value.has_value() || *n2.value != 1) {
            std::cerr << "expected deleted_face_count still 1 after commit\n";
            return 1;
        }
    }

    // ---- Stage 2: Loop PCurve closedness validation (UV trim ring) ----
    {
        // square in 3D (z=0), and matching square in UV
        auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto l0 = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = kernel.curves().make_line({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = kernel.curves().make_line({1.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = kernel.curves().make_line({0.0, 1.0, 0.0}, {0.0, -1.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            l0.status != axiom::StatusCode::Ok || !l0.value.has_value() ||
            l1.status != axiom::StatusCode::Ok || !l1.value.has_value() ||
            l2.status != axiom::StatusCode::Ok || !l2.value.has_value() ||
            l3.status != axiom::StatusCode::Ok || !l3.value.has_value()) {
            std::cerr << "failed to create geometry prerequisites for uv loop\n";
            return 1;
        }
        const std::array<axiom::Point2, 2> uv01 {{ {0.0, 0.0}, {1.0, 0.0} }};
        const std::array<axiom::Point2, 2> uv12 {{ {1.0, 0.0}, {1.0, 1.0} }};
        const std::array<axiom::Point2, 2> uv23 {{ {1.0, 1.0}, {0.0, 1.0} }};
        const std::array<axiom::Point2, 2> uv30 {{ {0.0, 1.0}, {0.0, 0.0} }};
        auto pc01 = kernel.pcurves().make_polyline(uv01);
        auto pc12 = kernel.pcurves().make_polyline(uv12);
        auto pc23 = kernel.pcurves().make_polyline(uv23);
        auto pc30 = kernel.pcurves().make_polyline(uv30);
        if (pc01.status != axiom::StatusCode::Ok || !pc01.value.has_value() ||
            pc12.status != axiom::StatusCode::Ok || !pc12.value.has_value() ||
            pc23.status != axiom::StatusCode::Ok || !pc23.value.has_value() ||
            pc30.status != axiom::StatusCode::Ok || !pc30.value.has_value()) {
            std::cerr << "failed to create pcurves for uv loop\n";
            return 1;
        }

        auto txn = kernel.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
        auto v2 = txn.create_vertex({1.0, 1.0, 0.0});
        auto v3 = txn.create_vertex({0.0, 1.0, 0.0});
        auto e0 = txn.create_edge(*l0.value, *v0.value, *v1.value);
        auto e1 = txn.create_edge(*l1.value, *v1.value, *v2.value);
        auto e2 = txn.create_edge(*l2.value, *v2.value, *v3.value);
        auto e3 = txn.create_edge(*l3.value, *v3.value, *v0.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        auto c1 = txn.create_coedge(*e1.value, false);
        auto c2 = txn.create_coedge(*e2.value, false);
        auto c3 = txn.create_coedge(*e3.value, false);
        if (v0.status != axiom::StatusCode::Ok || v1.status != axiom::StatusCode::Ok ||
            v2.status != axiom::StatusCode::Ok || v3.status != axiom::StatusCode::Ok ||
            e0.status != axiom::StatusCode::Ok || e1.status != axiom::StatusCode::Ok ||
            e2.status != axiom::StatusCode::Ok || e3.status != axiom::StatusCode::Ok ||
            c0.status != axiom::StatusCode::Ok || c1.status != axiom::StatusCode::Ok ||
            c2.status != axiom::StatusCode::Ok || c3.status != axiom::StatusCode::Ok ||
            !v0.value.has_value() || !v1.value.has_value() || !v2.value.has_value() || !v3.value.has_value() ||
            !e0.value.has_value() || !e1.value.has_value() || !e2.value.has_value() || !e3.value.has_value() ||
            !c0.value.has_value() || !c1.value.has_value() || !c2.value.has_value() || !c3.value.has_value()) {
            std::cerr << "failed to build uv loop topology\n";
            return 1;
        }
        txn.set_coedge_pcurve(*c0.value, *pc01.value);
        txn.set_coedge_pcurve(*c1.value, *pc12.value);
        txn.set_coedge_pcurve(*c2.value, *pc23.value);
        txn.set_coedge_pcurve(*c3.value, *pc30.value);

        const std::array<axiom::CoedgeId, 4> coedges {{ *c0.value, *c1.value, *c2.value, *c3.value }};
        auto loop = txn.create_loop(coedges);
        if (loop.status != axiom::StatusCode::Ok || !loop.value.has_value()) {
            std::cerr << "failed to create uv loop\n";
            return 1;
        }
        auto loop_pcurve_valid = kernel.topology().validate().validate_loop_pcurve_closedness(*loop.value);
        if (loop_pcurve_valid.status != axiom::StatusCode::Ok) {
            std::cerr << "expected uv loop pcurve closedness validation ok\n";
            return 1;
        }
        auto face = txn.create_face(*plane.value, *loop.value, {});
        if (face.status != axiom::StatusCode::Ok || !face.value.has_value()) {
            std::cerr << "failed to create face for trim consistency test\n";
            return 1;
        }
        auto face_trim_ok = kernel.topology().validate().validate_face_trim_consistency(*face.value);
        if (face_trim_ok.status != axiom::StatusCode::Ok) {
            std::cerr << "expected face trim consistency ok\n";
            return 1;
        }

        // Trim bridge: outer-loop PCurves -> UV bounds -> Geo Trimmed (underlying plane).
        auto uv_bounds = kernel.topology().query().face_outer_loop_uv_bounds(*face.value);
        if (uv_bounds.status != axiom::StatusCode::Ok || !uv_bounds.value.has_value()) {
            std::cerr << "expected face_outer_loop_uv_bounds ok\n";
            return 1;
        }
        if (std::abs(uv_bounds.value->u.min) > 1e-9 || std::abs(uv_bounds.value->u.max - 1.0) > 1e-9 ||
            std::abs(uv_bounds.value->v.min) > 1e-9 || std::abs(uv_bounds.value->v.max - 1.0) > 1e-9) {
            std::cerr << "unexpected face_outer_loop_uv_bounds range\n";
            return 1;
        }
        auto under_s = kernel.topology().query().underlying_surface_for_face_trim(*face.value);
        if (under_s.status != axiom::StatusCode::Ok || !under_s.value.has_value() ||
            under_s.value->value != plane.value->value) {
            std::cerr << "expected underlying_surface_for_face_trim to match plane surface\n";
            return 1;
        }
        auto trim_from_topo = kernel.create_trimmed_surface_from_face_outer_loop_pcurves(*face.value);
        if (trim_from_topo.status != axiom::StatusCode::Ok || !trim_from_topo.value.has_value()) {
            std::cerr << "create_trimmed_surface_from_face_outer_loop_pcurves failed\n";
            return 1;
        }
        auto tev = kernel.surface_service().eval(*trim_from_topo.value, 0.5, 0.5, 0);
        if (tev.status != axiom::StatusCode::Ok || !tev.value.has_value() ||
            std::abs(tev.value->point.x - 0.5) > 1e-6 || std::abs(tev.value->point.y - 0.5) > 1e-6 ||
            std::abs(tev.value->point.z) > 1e-6) {
            std::cerr << "unexpected eval on topology-derived trimmed surface\n";
            return 1;
        }

        auto bad_lp = kernel.topology().query().face_loop_uv_polyline(*face.value, axiom::LoopId{999999});
        if (bad_lp.status == axiom::StatusCode::Ok) {
            std::cerr << "expected face_loop_uv_polyline to reject loop not on face\n";
            return 1;
        }

        // Break closure by binding wrong pcurve on one coedge.
        txn.set_coedge_pcurve(*c3.value, *pc23.value);
        auto broken = kernel.topology().validate().validate_loop_pcurve_closedness(*loop.value);
        if (broken.status == axiom::StatusCode::Ok) {
            std::cerr << "expected uv loop pcurve closedness validation to fail\n";
            return 1;
        }
        auto broken_face_trim = kernel.topology().validate().validate_face_trim_consistency(*face.value);
        if (broken_face_trim.status == axiom::StatusCode::Ok) {
            std::cerr << "expected face trim consistency to fail\n";
            return 1;
        }
        auto broken_diag = kernel.diagnostics().get(broken.diagnostic_id);
        auto broken_face_diag = kernel.diagnostics().get(broken_face_trim.diagnostic_id);
        if (broken_diag.status != axiom::StatusCode::Ok || !broken_diag.value.has_value() ||
            broken_face_diag.status != axiom::StatusCode::Ok || !broken_face_diag.value.has_value() ||
            !has_issue_code(*broken_diag.value, axiom::diag_codes::kTopoLoopNotClosed) ||
            !has_issue_code(*broken_face_diag.value, axiom::diag_codes::kTopoLoopNotClosed)) {
            std::cerr << "expected validate_face_trim_consistency to forward loop pcurve closedness diagnostics\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for uv loop test\n";
            return 1;
        }
    }

    // ---- Stage 2: full trim outer-only — UV signed area must not be degenerate vs linear tol ----
    {
        const double s = 3e-7;  // s^2 = 9e-14 < default linear_tol^2 * 10 (1e-11 @ 1e-6)
        auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto seg01 = kernel.curves().make_line_segment({0.0, 0.0, 0.0}, {s, 0.0, 0.0});
        auto seg12 = kernel.curves().make_line_segment({s, 0.0, 0.0}, {s, s, 0.0});
        auto seg23 = kernel.curves().make_line_segment({s, s, 0.0}, {0.0, s, 0.0});
        auto seg30 = kernel.curves().make_line_segment({0.0, s, 0.0}, {0.0, 0.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || seg01.status != axiom::StatusCode::Ok ||
            seg12.status != axiom::StatusCode::Ok || seg23.status != axiom::StatusCode::Ok ||
            seg30.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            !seg01.value.has_value() || !seg12.value.has_value() || !seg23.value.has_value() ||
            !seg30.value.has_value()) {
            std::cerr << "failed to create geometry for micro-uv trim area test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v1 = txn.create_vertex({s, 0.0, 0.0});
        auto v2 = txn.create_vertex({s, s, 0.0});
        auto v3 = txn.create_vertex({0.0, s, 0.0});
        auto e01 = txn.create_edge(*seg01.value, *v0.value, *v1.value);
        auto e12 = txn.create_edge(*seg12.value, *v1.value, *v2.value);
        auto e23 = txn.create_edge(*seg23.value, *v2.value, *v3.value);
        auto e30 = txn.create_edge(*seg30.value, *v3.value, *v0.value);
        auto c01 = txn.create_coedge(*e01.value, false);
        auto c12 = txn.create_coedge(*e12.value, false);
        auto c23 = txn.create_coedge(*e23.value, false);
        auto c30 = txn.create_coedge(*e30.value, false);
        if (v0.status != axiom::StatusCode::Ok || !v0.value.has_value() ||
            v1.status != axiom::StatusCode::Ok || !v1.value.has_value() ||
            v2.status != axiom::StatusCode::Ok || !v2.value.has_value() ||
            v3.status != axiom::StatusCode::Ok || !v3.value.has_value() ||
            e01.status != axiom::StatusCode::Ok || e12.status != axiom::StatusCode::Ok ||
            e23.status != axiom::StatusCode::Ok || e30.status != axiom::StatusCode::Ok ||
            c01.status != axiom::StatusCode::Ok || c12.status != axiom::StatusCode::Ok ||
            c23.status != axiom::StatusCode::Ok || c30.status != axiom::StatusCode::Ok ||
            !e01.value.has_value() || !e12.value.has_value() || !e23.value.has_value() ||
            !e30.value.has_value() || !c01.value.has_value() || !c12.value.has_value() ||
            !c23.value.has_value() || !c30.value.has_value()) {
            std::cerr << "failed to build micro-uv trim topology\n";
            return 1;
        }
        const std::array<axiom::Point2, 2> uv01{{{0.0, 0.0}, {s, 0.0}}};
        const std::array<axiom::Point2, 2> uv12{{{s, 0.0}, {s, s}}};
        const std::array<axiom::Point2, 2> uv23{{{s, s}, {0.0, s}}};
        const std::array<axiom::Point2, 2> uv30{{{0.0, s}, {0.0, 0.0}}};
        auto pc01 = kernel.pcurves().make_polyline(uv01);
        auto pc12 = kernel.pcurves().make_polyline(uv12);
        auto pc23 = kernel.pcurves().make_polyline(uv23);
        auto pc30 = kernel.pcurves().make_polyline(uv30);
        if (pc01.status != axiom::StatusCode::Ok || pc12.status != axiom::StatusCode::Ok ||
            pc23.status != axiom::StatusCode::Ok || pc30.status != axiom::StatusCode::Ok ||
            !pc01.value.has_value() || !pc12.value.has_value() || !pc23.value.has_value() ||
            !pc30.value.has_value()) {
            std::cerr << "failed to create pcurves for micro-uv trim test\n";
            return 1;
        }
        txn.set_coedge_pcurve(*c01.value, *pc01.value);
        txn.set_coedge_pcurve(*c12.value, *pc12.value);
        txn.set_coedge_pcurve(*c23.value, *pc23.value);
        txn.set_coedge_pcurve(*c30.value, *pc30.value);
        const std::array<axiom::CoedgeId, 4> coedges{{*c01.value, *c12.value, *c23.value, *c30.value}};
        auto loop = txn.create_loop(coedges);
        auto face = txn.create_face(*plane.value, *loop.value, {});
        if (loop.status != axiom::StatusCode::Ok || face.status != axiom::StatusCode::Ok ||
            !loop.value.has_value() || !face.value.has_value()) {
            std::cerr << "failed to create face for micro-uv trim test\n";
            return 1;
        }
        auto tiny_uv = kernel.topology().validate().validate_face_trim_consistency(*face.value);
        if (tiny_uv.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_face_trim_consistency to reject degenerate UV signed area\n";
            return 1;
        }
        auto tiny_rep = kernel.diagnostics().get(tiny_uv.diagnostic_id);
        if (tiny_rep.status != axiom::StatusCode::Ok || !tiny_rep.value.has_value() ||
            !has_issue_code(*tiny_rep.value, axiom::diag_codes::kTopoFaceOuterLoopInvalid)) {
            std::cerr << "expected kTopoFaceOuterLoopInvalid for micro UV trim area\n";
            return 1;
        }
        if (txn.rollback().status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for micro-uv trim test\n";
            return 1;
        }
    }

    // ---- Stage 2+: full trim on plane — 3D 外环顺时针时须与平面法向冲突（kTopoLoopOrientationMismatch）----
    {
        auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto s03 = kernel.curves().make_line_segment({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        auto s32 = kernel.curves().make_line_segment({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0});
        auto s21 = kernel.curves().make_line_segment({1.0, 1.0, 0.0}, {1.0, 0.0, 0.0});
        auto s10 = kernel.curves().make_line_segment({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || s03.status != axiom::StatusCode::Ok ||
            s32.status != axiom::StatusCode::Ok || s21.status != axiom::StatusCode::Ok ||
            s10.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            !s03.value.has_value() || !s32.value.has_value() || !s21.value.has_value() ||
            !s10.value.has_value()) {
            std::cerr << "failed to create geometry for cw trim orientation test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v3 = txn.create_vertex({0.0, 1.0, 0.0});
        auto v2 = txn.create_vertex({1.0, 1.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
        auto e03 = txn.create_edge(*s03.value, *v0.value, *v3.value);
        auto e32 = txn.create_edge(*s32.value, *v3.value, *v2.value);
        auto e21 = txn.create_edge(*s21.value, *v2.value, *v1.value);
        auto e10 = txn.create_edge(*s10.value, *v1.value, *v0.value);
        auto c03 = txn.create_coedge(*e03.value, false);
        auto c32 = txn.create_coedge(*e32.value, false);
        auto c21 = txn.create_coedge(*e21.value, false);
        auto c10 = txn.create_coedge(*e10.value, false);
        if (v0.status != axiom::StatusCode::Ok || !v0.value.has_value() ||
            v3.status != axiom::StatusCode::Ok || !v3.value.has_value() ||
            v2.status != axiom::StatusCode::Ok || !v2.value.has_value() ||
            v1.status != axiom::StatusCode::Ok || !v1.value.has_value() ||
            e03.status != axiom::StatusCode::Ok || e32.status != axiom::StatusCode::Ok ||
            e21.status != axiom::StatusCode::Ok || e10.status != axiom::StatusCode::Ok ||
            c03.status != axiom::StatusCode::Ok || c32.status != axiom::StatusCode::Ok ||
            c21.status != axiom::StatusCode::Ok || c10.status != axiom::StatusCode::Ok ||
            !e03.value.has_value() || !e32.value.has_value() || !e21.value.has_value() ||
            !e10.value.has_value() || !c03.value.has_value() || !c32.value.has_value() ||
            !c21.value.has_value() || !c10.value.has_value()) {
            std::cerr << "failed to build cw trim topology\n";
            return 1;
        }
        const std::array<axiom::Point2, 2> uv03{{{0.0, 0.0}, {0.0, 1.0}}};
        const std::array<axiom::Point2, 2> uv32{{{0.0, 1.0}, {1.0, 1.0}}};
        const std::array<axiom::Point2, 2> uv21{{{1.0, 1.0}, {1.0, 0.0}}};
        const std::array<axiom::Point2, 2> uv10{{{1.0, 0.0}, {0.0, 0.0}}};
        auto pc03 = kernel.pcurves().make_polyline(uv03);
        auto pc32 = kernel.pcurves().make_polyline(uv32);
        auto pc21 = kernel.pcurves().make_polyline(uv21);
        auto pc10 = kernel.pcurves().make_polyline(uv10);
        if (pc03.status != axiom::StatusCode::Ok || pc32.status != axiom::StatusCode::Ok ||
            pc21.status != axiom::StatusCode::Ok || pc10.status != axiom::StatusCode::Ok ||
            !pc03.value.has_value() || !pc32.value.has_value() || !pc21.value.has_value() ||
            !pc10.value.has_value()) {
            std::cerr << "failed to create pcurves for cw trim test\n";
            return 1;
        }
        txn.set_coedge_pcurve(*c03.value, *pc03.value);
        txn.set_coedge_pcurve(*c32.value, *pc32.value);
        txn.set_coedge_pcurve(*c21.value, *pc21.value);
        txn.set_coedge_pcurve(*c10.value, *pc10.value);
        const std::array<axiom::CoedgeId, 4> cw_ring{{*c03.value, *c32.value, *c21.value, *c10.value}};
        auto loop = txn.create_loop(cw_ring);
        auto face = txn.create_face(*plane.value, *loop.value, {});
        if (loop.status != axiom::StatusCode::Ok || face.status != axiom::StatusCode::Ok ||
            !loop.value.has_value() || !face.value.has_value()) {
            std::cerr << "failed to create face for cw trim test\n";
            return 1;
        }
        auto bad_trim = kernel.topology().validate().validate_face_trim_consistency(*face.value);
        if (bad_trim.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_face_trim_consistency to fail for CW 3D outer with +Z plane\n";
            return 1;
        }
        auto bad_rep = kernel.diagnostics().get(bad_trim.diagnostic_id);
        if (bad_rep.status != axiom::StatusCode::Ok || !bad_rep.value.has_value() ||
            !has_issue_code(*bad_rep.value, axiom::diag_codes::kTopoLoopOrientationMismatch)) {
            std::cerr << "expected kTopoLoopOrientationMismatch for CW full trim on plane\n";
            return 1;
        }
        if (txn.rollback().status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for cw trim orientation test\n";
            return 1;
        }
    }

    // ---- Stage 2: trim bridge — 外环+内环 PCurve -> Kernel 物化 `trim_uv_loop` + `trim_uv_holes` ----
    {
        auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto o0 = kernel.curves().make_line_segment({0.0, 0.0, 0.0}, {5.0, 0.0, 0.0});
        auto o1 = kernel.curves().make_line_segment({5.0, 0.0, 0.0}, {5.0, 5.0, 0.0});
        auto o2 = kernel.curves().make_line_segment({5.0, 5.0, 0.0}, {0.0, 5.0, 0.0});
        auto o3 = kernel.curves().make_line_segment({0.0, 5.0, 0.0}, {0.0, 0.0, 0.0});
        // 内环走向与外环相反（Strict trim：有符号 UV 面积异号）
        auto i0 = kernel.curves().make_line_segment({2.0, 2.0, 0.0}, {2.0, 3.0, 0.0});
        auto i1 = kernel.curves().make_line_segment({2.0, 3.0, 0.0}, {3.0, 3.0, 0.0});
        auto i2 = kernel.curves().make_line_segment({3.0, 3.0, 0.0}, {3.0, 2.0, 0.0});
        auto i3 = kernel.curves().make_line_segment({3.0, 2.0, 0.0}, {2.0, 2.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            o0.status != axiom::StatusCode::Ok || !o0.value.has_value() ||
            o1.status != axiom::StatusCode::Ok || !o1.value.has_value() ||
            o2.status != axiom::StatusCode::Ok || !o2.value.has_value() ||
            o3.status != axiom::StatusCode::Ok || !o3.value.has_value() ||
            i0.status != axiom::StatusCode::Ok || !i0.value.has_value() ||
            i1.status != axiom::StatusCode::Ok || !i1.value.has_value() ||
            i2.status != axiom::StatusCode::Ok || !i2.value.has_value() ||
            i3.status != axiom::StatusCode::Ok || !i3.value.has_value()) {
            std::cerr << "failed geometry for trim bridge polygon+holes test\n";
            return 1;
        }
        const std::array<axiom::Point2, 2> ouv0{{{0.0, 0.0}, {5.0, 0.0}}};
        const std::array<axiom::Point2, 2> ouv1{{{5.0, 0.0}, {5.0, 5.0}}};
        const std::array<axiom::Point2, 2> ouv2{{{5.0, 5.0}, {0.0, 5.0}}};
        const std::array<axiom::Point2, 2> ouv3{{{0.0, 5.0}, {0.0, 0.0}}};
        const std::array<axiom::Point2, 2> iuv0{{{2.0, 2.0}, {2.0, 3.0}}};
        const std::array<axiom::Point2, 2> iuv1{{{2.0, 3.0}, {3.0, 3.0}}};
        const std::array<axiom::Point2, 2> iuv2{{{3.0, 3.0}, {3.0, 2.0}}};
        const std::array<axiom::Point2, 2> iuv3{{{3.0, 2.0}, {2.0, 2.0}}};
        auto po0 = kernel.pcurves().make_polyline(ouv0);
        auto po1 = kernel.pcurves().make_polyline(ouv1);
        auto po2 = kernel.pcurves().make_polyline(ouv2);
        auto po3 = kernel.pcurves().make_polyline(ouv3);
        auto pi0 = kernel.pcurves().make_polyline(iuv0);
        auto pi1 = kernel.pcurves().make_polyline(iuv1);
        auto pi2 = kernel.pcurves().make_polyline(iuv2);
        auto pi3 = kernel.pcurves().make_polyline(iuv3);
        if (po0.status != axiom::StatusCode::Ok || !po0.value.has_value() ||
            po1.status != axiom::StatusCode::Ok || !po1.value.has_value() ||
            po2.status != axiom::StatusCode::Ok || !po2.value.has_value() ||
            po3.status != axiom::StatusCode::Ok || !po3.value.has_value() ||
            pi0.status != axiom::StatusCode::Ok || !pi0.value.has_value() ||
            pi1.status != axiom::StatusCode::Ok || !pi1.value.has_value() ||
            pi2.status != axiom::StatusCode::Ok || !pi2.value.has_value() ||
            pi3.status != axiom::StatusCode::Ok || !pi3.value.has_value()) {
            std::cerr << "failed pcurves for trim bridge polygon+holes test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto v00 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v50 = txn.create_vertex({5.0, 0.0, 0.0});
        auto v55 = txn.create_vertex({5.0, 5.0, 0.0});
        auto v05 = txn.create_vertex({0.0, 5.0, 0.0});
        auto v22 = txn.create_vertex({2.0, 2.0, 0.0});
        auto v32 = txn.create_vertex({3.0, 2.0, 0.0});
        auto v33 = txn.create_vertex({3.0, 3.0, 0.0});
        auto v23 = txn.create_vertex({2.0, 3.0, 0.0});
        auto e_o0 = txn.create_edge(*o0.value, *v00.value, *v50.value);
        auto e_o1 = txn.create_edge(*o1.value, *v50.value, *v55.value);
        auto e_o2 = txn.create_edge(*o2.value, *v55.value, *v05.value);
        auto e_o3 = txn.create_edge(*o3.value, *v05.value, *v00.value);
        auto e_i0 = txn.create_edge(*i0.value, *v22.value, *v23.value);
        auto e_i1 = txn.create_edge(*i1.value, *v23.value, *v33.value);
        auto e_i2 = txn.create_edge(*i2.value, *v33.value, *v32.value);
        auto e_i3 = txn.create_edge(*i3.value, *v32.value, *v22.value);
        auto c_o0 = txn.create_coedge(*e_o0.value, false);
        auto c_o1 = txn.create_coedge(*e_o1.value, false);
        auto c_o2 = txn.create_coedge(*e_o2.value, false);
        auto c_o3 = txn.create_coedge(*e_o3.value, false);
        auto c_i0 = txn.create_coedge(*e_i0.value, false);
        auto c_i1 = txn.create_coedge(*e_i1.value, false);
        auto c_i2 = txn.create_coedge(*e_i2.value, false);
        auto c_i3 = txn.create_coedge(*e_i3.value, false);
        if (v00.status != axiom::StatusCode::Ok || !v00.value.has_value() ||
            v50.status != axiom::StatusCode::Ok || !v50.value.has_value() ||
            v55.status != axiom::StatusCode::Ok || !v55.value.has_value() ||
            v05.status != axiom::StatusCode::Ok || !v05.value.has_value() ||
            v22.status != axiom::StatusCode::Ok || !v22.value.has_value() ||
            v32.status != axiom::StatusCode::Ok || !v32.value.has_value() ||
            v33.status != axiom::StatusCode::Ok || !v33.value.has_value() ||
            v23.status != axiom::StatusCode::Ok || !v23.value.has_value() ||
            e_o0.status != axiom::StatusCode::Ok || !e_o0.value.has_value() ||
            e_o1.status != axiom::StatusCode::Ok || !e_o1.value.has_value() ||
            e_o2.status != axiom::StatusCode::Ok || !e_o2.value.has_value() ||
            e_o3.status != axiom::StatusCode::Ok || !e_o3.value.has_value() ||
            e_i0.status != axiom::StatusCode::Ok || !e_i0.value.has_value() ||
            e_i1.status != axiom::StatusCode::Ok || !e_i1.value.has_value() ||
            e_i2.status != axiom::StatusCode::Ok || !e_i2.value.has_value() ||
            e_i3.status != axiom::StatusCode::Ok || !e_i3.value.has_value() ||
            c_o0.status != axiom::StatusCode::Ok || !c_o0.value.has_value() ||
            c_o1.status != axiom::StatusCode::Ok || !c_o1.value.has_value() ||
            c_o2.status != axiom::StatusCode::Ok || !c_o2.value.has_value() ||
            c_o3.status != axiom::StatusCode::Ok || !c_o3.value.has_value() ||
            c_i0.status != axiom::StatusCode::Ok || !c_i0.value.has_value() ||
            c_i1.status != axiom::StatusCode::Ok || !c_i1.value.has_value() ||
            c_i2.status != axiom::StatusCode::Ok || !c_i2.value.has_value() ||
            c_i3.status != axiom::StatusCode::Ok || !c_i3.value.has_value()) {
            std::cerr << "failed topology txn for trim bridge polygon+holes test\n";
            return 1;
        }
        txn.set_coedge_pcurve(*c_o0.value, *po0.value);
        txn.set_coedge_pcurve(*c_o1.value, *po1.value);
        txn.set_coedge_pcurve(*c_o2.value, *po2.value);
        txn.set_coedge_pcurve(*c_o3.value, *po3.value);
        txn.set_coedge_pcurve(*c_i0.value, *pi0.value);
        txn.set_coedge_pcurve(*c_i1.value, *pi1.value);
        txn.set_coedge_pcurve(*c_i2.value, *pi2.value);
        txn.set_coedge_pcurve(*c_i3.value, *pi3.value);
        const std::array<axiom::CoedgeId, 4> co_outer{{*c_o0.value, *c_o1.value, *c_o2.value, *c_o3.value}};
        const std::array<axiom::CoedgeId, 4> co_inner{{*c_i0.value, *c_i1.value, *c_i2.value, *c_i3.value}};
        auto outer_loop = txn.create_loop(co_outer);
        auto inner_loop = txn.create_loop(co_inner);
        if (outer_loop.status != axiom::StatusCode::Ok || !outer_loop.value.has_value() ||
            inner_loop.status != axiom::StatusCode::Ok || !inner_loop.value.has_value()) {
            std::cerr << "failed create loops for trim bridge polygon+holes test\n";
            return 1;
        }
        auto face_h = txn.create_face(*plane.value, *outer_loop.value,
                                      std::array<axiom::LoopId, 1>{*inner_loop.value});
        if (face_h.status != axiom::StatusCode::Ok || !face_h.value.has_value()) {
            std::cerr << "failed create face with hole for trim bridge test\n";
            return 1;
        }
        auto trim_ok_h = kernel.topology().validate().validate_face_trim_consistency(*face_h.value);
        if (trim_ok_h.status != axiom::StatusCode::Ok) {
            std::cerr << "expected face with outer+inner PCurve loops to pass trim consistency\n";
            return 1;
        }
        auto ts_h = kernel.create_trimmed_surface_from_face_outer_loop_pcurves(*face_h.value);
        if (ts_h.status != axiom::StatusCode::Ok || !ts_h.value.has_value()) {
            std::cerr << "create_trimmed_surface_from_face_outer_loop_pcurves (with holes) failed\n";
            return 1;
        }
        auto ev_mid = kernel.surface_service().eval(*ts_h.value, 2.5, 2.5, 0);
        if (ev_mid.status != axiom::StatusCode::Ok || !ev_mid.value.has_value()) {
            std::cerr << "eval on trim-with-holes from topo failed\n";
            return 1;
        }
        if (std::abs(ev_mid.value->point.x - 2.5) < 0.08 && std::abs(ev_mid.value->point.y - 2.5) < 0.08) {
            std::cerr << "expected UV in hole to project off hole interior (trim bridge)\n";
            return 1;
        }
        // 单面带孔非闭合壳，此处只验证 trim bridge + Geo 物化；Strict 全体验证见 `validate_face_trim_consistency` 与其它闭合体回归。
        if (txn.rollback().status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for trim bridge polygon+holes test\n";
            return 1;
        }
    }

    // ---- Trim bridge：内环 UV 整体落在外环 UV 域外时须在 PCurve↔3D 之前判为 kTopoFaceInnerLoopInvalid ----
    {
        auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto o0 = kernel.curves().make_line_segment({0.0, 0.0, 0.0}, {5.0, 0.0, 0.0});
        auto o1 = kernel.curves().make_line_segment({5.0, 0.0, 0.0}, {5.0, 5.0, 0.0});
        auto o2 = kernel.curves().make_line_segment({5.0, 5.0, 0.0}, {0.0, 5.0, 0.0});
        auto o3 = kernel.curves().make_line_segment({0.0, 5.0, 0.0}, {0.0, 0.0, 0.0});
        auto i0 = kernel.curves().make_line_segment({2.0, 2.0, 0.0}, {2.0, 3.0, 0.0});
        auto i1 = kernel.curves().make_line_segment({2.0, 3.0, 0.0}, {3.0, 3.0, 0.0});
        auto i2 = kernel.curves().make_line_segment({3.0, 3.0, 0.0}, {3.0, 2.0, 0.0});
        auto i3 = kernel.curves().make_line_segment({3.0, 2.0, 0.0}, {2.0, 2.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            o0.status != axiom::StatusCode::Ok || !o0.value.has_value() ||
            o1.status != axiom::StatusCode::Ok || !o1.value.has_value() ||
            o2.status != axiom::StatusCode::Ok || !o2.value.has_value() ||
            o3.status != axiom::StatusCode::Ok || !o3.value.has_value() ||
            i0.status != axiom::StatusCode::Ok || !i0.value.has_value() ||
            i1.status != axiom::StatusCode::Ok || !i1.value.has_value() ||
            i2.status != axiom::StatusCode::Ok || !i2.value.has_value() ||
            i3.status != axiom::StatusCode::Ok || !i3.value.has_value()) {
            std::cerr << "failed geometry for inner UV outside outer trim test\n";
            return 1;
        }
        const std::array<axiom::Point2, 2> ouv0{{{0.0, 0.0}, {5.0, 0.0}}};
        const std::array<axiom::Point2, 2> ouv1{{{5.0, 0.0}, {5.0, 5.0}}};
        const std::array<axiom::Point2, 2> ouv2{{{5.0, 5.0}, {0.0, 5.0}}};
        const std::array<axiom::Point2, 2> ouv3{{{0.0, 5.0}, {0.0, 0.0}}};
        // 与合法孔相同的绕向，但整体平移 (+4,+4) 使质心落在外环 [0,5]² 之外
        const std::array<axiom::Point2, 2> iuv0{{{6.0, 6.0}, {6.0, 7.0}}};
        const std::array<axiom::Point2, 2> iuv1{{{6.0, 7.0}, {7.0, 7.0}}};
        const std::array<axiom::Point2, 2> iuv2{{{7.0, 7.0}, {7.0, 6.0}}};
        const std::array<axiom::Point2, 2> iuv3{{{7.0, 6.0}, {6.0, 6.0}}};
        auto po0 = kernel.pcurves().make_polyline(ouv0);
        auto po1 = kernel.pcurves().make_polyline(ouv1);
        auto po2 = kernel.pcurves().make_polyline(ouv2);
        auto po3 = kernel.pcurves().make_polyline(ouv3);
        auto pi0 = kernel.pcurves().make_polyline(iuv0);
        auto pi1 = kernel.pcurves().make_polyline(iuv1);
        auto pi2 = kernel.pcurves().make_polyline(iuv2);
        auto pi3 = kernel.pcurves().make_polyline(iuv3);
        if (po0.status != axiom::StatusCode::Ok || !po0.value.has_value() ||
            po1.status != axiom::StatusCode::Ok || !po1.value.has_value() ||
            po2.status != axiom::StatusCode::Ok || !po2.value.has_value() ||
            po3.status != axiom::StatusCode::Ok || !po3.value.has_value() ||
            pi0.status != axiom::StatusCode::Ok || !pi0.value.has_value() ||
            pi1.status != axiom::StatusCode::Ok || !pi1.value.has_value() ||
            pi2.status != axiom::StatusCode::Ok || !pi2.value.has_value() ||
            pi3.status != axiom::StatusCode::Ok || !pi3.value.has_value()) {
            std::cerr << "failed pcurves for inner UV outside outer test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto v00 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v50 = txn.create_vertex({5.0, 0.0, 0.0});
        auto v55 = txn.create_vertex({5.0, 5.0, 0.0});
        auto v05 = txn.create_vertex({0.0, 5.0, 0.0});
        auto v22 = txn.create_vertex({2.0, 2.0, 0.0});
        auto v32 = txn.create_vertex({3.0, 2.0, 0.0});
        auto v33 = txn.create_vertex({3.0, 3.0, 0.0});
        auto v23 = txn.create_vertex({2.0, 3.0, 0.0});
        auto e_o0 = txn.create_edge(*o0.value, *v00.value, *v50.value);
        auto e_o1 = txn.create_edge(*o1.value, *v50.value, *v55.value);
        auto e_o2 = txn.create_edge(*o2.value, *v55.value, *v05.value);
        auto e_o3 = txn.create_edge(*o3.value, *v05.value, *v00.value);
        auto e_i0 = txn.create_edge(*i0.value, *v22.value, *v23.value);
        auto e_i1 = txn.create_edge(*i1.value, *v23.value, *v33.value);
        auto e_i2 = txn.create_edge(*i2.value, *v33.value, *v32.value);
        auto e_i3 = txn.create_edge(*i3.value, *v32.value, *v22.value);
        auto c_o0 = txn.create_coedge(*e_o0.value, false);
        auto c_o1 = txn.create_coedge(*e_o1.value, false);
        auto c_o2 = txn.create_coedge(*e_o2.value, false);
        auto c_o3 = txn.create_coedge(*e_o3.value, false);
        auto c_i0 = txn.create_coedge(*e_i0.value, false);
        auto c_i1 = txn.create_coedge(*e_i1.value, false);
        auto c_i2 = txn.create_coedge(*e_i2.value, false);
        auto c_i3 = txn.create_coedge(*e_i3.value, false);
        if (v00.status != axiom::StatusCode::Ok || !v00.value.has_value() ||
            v50.status != axiom::StatusCode::Ok || !v50.value.has_value() ||
            v55.status != axiom::StatusCode::Ok || !v55.value.has_value() ||
            v05.status != axiom::StatusCode::Ok || !v05.value.has_value() ||
            v22.status != axiom::StatusCode::Ok || !v22.value.has_value() ||
            v32.status != axiom::StatusCode::Ok || !v32.value.has_value() ||
            v33.status != axiom::StatusCode::Ok || !v33.value.has_value() ||
            v23.status != axiom::StatusCode::Ok || !v23.value.has_value() ||
            e_o0.status != axiom::StatusCode::Ok || !e_o0.value.has_value() ||
            e_o1.status != axiom::StatusCode::Ok || !e_o1.value.has_value() ||
            e_o2.status != axiom::StatusCode::Ok || !e_o2.value.has_value() ||
            e_o3.status != axiom::StatusCode::Ok || !e_o3.value.has_value() ||
            e_i0.status != axiom::StatusCode::Ok || !e_i0.value.has_value() ||
            e_i1.status != axiom::StatusCode::Ok || !e_i1.value.has_value() ||
            e_i2.status != axiom::StatusCode::Ok || !e_i2.value.has_value() ||
            e_i3.status != axiom::StatusCode::Ok || !e_i3.value.has_value() ||
            c_o0.status != axiom::StatusCode::Ok || !c_o0.value.has_value() ||
            c_o1.status != axiom::StatusCode::Ok || !c_o1.value.has_value() ||
            c_o2.status != axiom::StatusCode::Ok || !c_o2.value.has_value() ||
            c_o3.status != axiom::StatusCode::Ok || !c_o3.value.has_value() ||
            c_i0.status != axiom::StatusCode::Ok || !c_i0.value.has_value() ||
            c_i1.status != axiom::StatusCode::Ok || !c_i1.value.has_value() ||
            c_i2.status != axiom::StatusCode::Ok || !c_i2.value.has_value() ||
            c_i3.status != axiom::StatusCode::Ok || !c_i3.value.has_value()) {
            std::cerr << "failed topology txn for inner UV outside outer test\n";
            return 1;
        }
        txn.set_coedge_pcurve(*c_o0.value, *po0.value);
        txn.set_coedge_pcurve(*c_o1.value, *po1.value);
        txn.set_coedge_pcurve(*c_o2.value, *po2.value);
        txn.set_coedge_pcurve(*c_o3.value, *po3.value);
        txn.set_coedge_pcurve(*c_i0.value, *pi0.value);
        txn.set_coedge_pcurve(*c_i1.value, *pi1.value);
        txn.set_coedge_pcurve(*c_i2.value, *pi2.value);
        txn.set_coedge_pcurve(*c_i3.value, *pi3.value);
        const std::array<axiom::CoedgeId, 4> co_outer{{*c_o0.value, *c_o1.value, *c_o2.value, *c_o3.value}};
        const std::array<axiom::CoedgeId, 4> co_inner{{*c_i0.value, *c_i1.value, *c_i2.value, *c_i3.value}};
        auto outer_loop = txn.create_loop(co_outer);
        auto inner_loop = txn.create_loop(co_inner);
        auto face_bad = txn.create_face(*plane.value, *outer_loop.value,
                                        std::array<axiom::LoopId, 1>{*inner_loop.value});
        if (outer_loop.status != axiom::StatusCode::Ok || !outer_loop.value.has_value() ||
            inner_loop.status != axiom::StatusCode::Ok || !inner_loop.value.has_value() ||
            face_bad.status != axiom::StatusCode::Ok || !face_bad.value.has_value()) {
            std::cerr << "failed create face for inner UV outside outer test\n";
            return 1;
        }
        auto trim_bad_uv = kernel.topology().validate().validate_face_trim_consistency(*face_bad.value);
        if (trim_bad_uv.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_face_trim_consistency to reject inner UV outside outer\n";
            return 1;
        }
        auto trim_bad_rep = kernel.diagnostics().get(trim_bad_uv.diagnostic_id);
        if (trim_bad_rep.status != axiom::StatusCode::Ok || !trim_bad_rep.value.has_value() ||
            !has_issue_code(*trim_bad_rep.value, axiom::diag_codes::kTopoFaceInnerLoopInvalid)) {
            std::cerr << "expected kTopoFaceInnerLoopInvalid for inner UV outside outer\n";
            return 1;
        }
        if (txn.rollback().status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for inner UV outside outer test\n";
            return 1;
        }
    }

    // ---- Stage 2: PCurve <-> 3D edge endpoint UV consistency (plane face) ----
    {
        auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto l0 = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = kernel.curves().make_line({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = kernel.curves().make_line({1.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = kernel.curves().make_line({0.0, 1.0, 0.0}, {0.0, -1.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            l0.status != axiom::StatusCode::Ok || !l0.value.has_value() ||
            l1.status != axiom::StatusCode::Ok || !l1.value.has_value() ||
            l2.status != axiom::StatusCode::Ok || !l2.value.has_value() ||
            l3.status != axiom::StatusCode::Ok || !l3.value.has_value()) {
            std::cerr << "failed to create prerequisites for pcurve-edge consistency test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
        auto v2 = txn.create_vertex({1.0, 1.0, 0.0});
        auto v3 = txn.create_vertex({0.0, 1.0, 0.0});
        auto e0 = txn.create_edge(*l0.value, *v0.value, *v1.value);
        auto e1 = txn.create_edge(*l1.value, *v1.value, *v2.value);
        auto e2 = txn.create_edge(*l2.value, *v2.value, *v3.value);
        auto e3 = txn.create_edge(*l3.value, *v3.value, *v0.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        auto c1 = txn.create_coedge(*e1.value, false);
        auto c2 = txn.create_coedge(*e2.value, false);
        auto c3 = txn.create_coedge(*e3.value, false);
        if (v0.status != axiom::StatusCode::Ok || v1.status != axiom::StatusCode::Ok ||
            v2.status != axiom::StatusCode::Ok || v3.status != axiom::StatusCode::Ok ||
            e0.status != axiom::StatusCode::Ok || e1.status != axiom::StatusCode::Ok ||
            e2.status != axiom::StatusCode::Ok || e3.status != axiom::StatusCode::Ok ||
            c0.status != axiom::StatusCode::Ok || c1.status != axiom::StatusCode::Ok ||
            c2.status != axiom::StatusCode::Ok || c3.status != axiom::StatusCode::Ok ||
            !v0.value.has_value() || !v1.value.has_value() || !v2.value.has_value() || !v3.value.has_value() ||
            !e0.value.has_value() || !e1.value.has_value() || !e2.value.has_value() || !e3.value.has_value() ||
            !c0.value.has_value() || !c1.value.has_value() || !c2.value.has_value() || !c3.value.has_value()) {
            std::cerr << "failed to build topology for pcurve-edge consistency test\n";
            return 1;
        }
        const std::array<axiom::Point2, 2> uv01 {{ {0.0, 0.0}, {1.0, 0.0} }};
        const std::array<axiom::Point2, 2> uv12 {{ {1.0, 0.0}, {1.0, 1.0} }};
        const std::array<axiom::Point2, 2> uv23 {{ {1.0, 1.0}, {0.0, 1.0} }};
        const std::array<axiom::Point2, 2> uv30 {{ {0.0, 1.0}, {0.0, 0.0} }};
        auto pc01 = kernel.pcurves().make_polyline(uv01);
        auto pc12 = kernel.pcurves().make_polyline(uv12);
        auto pc23 = kernel.pcurves().make_polyline(uv23);
        auto pc30 = kernel.pcurves().make_polyline(uv30);
        if (pc01.status != axiom::StatusCode::Ok || !pc01.value.has_value() ||
            pc12.status != axiom::StatusCode::Ok || !pc12.value.has_value() ||
            pc23.status != axiom::StatusCode::Ok || !pc23.value.has_value() ||
            pc30.status != axiom::StatusCode::Ok || !pc30.value.has_value()) {
            std::cerr << "failed to create pcurves for pcurve-edge consistency test\n";
            return 1;
        }
        txn.set_coedge_pcurve(*c0.value, *pc01.value);
        txn.set_coedge_pcurve(*c1.value, *pc12.value);
        txn.set_coedge_pcurve(*c2.value, *pc23.value);
        txn.set_coedge_pcurve(*c3.value, *pc30.value);
        const std::array<axiom::CoedgeId, 4> coedges {{ *c0.value, *c1.value, *c2.value, *c3.value }};
        auto loop = txn.create_loop(coedges);
        auto face = txn.create_face(*plane.value, *loop.value, {});
        if (loop.status != axiom::StatusCode::Ok || !loop.value.has_value() ||
            face.status != axiom::StatusCode::Ok || !face.value.has_value()) {
            std::cerr << "failed to create face for pcurve-edge consistency test\n";
            return 1;
        }
        auto ok = kernel.topology().validate().validate_face_trim_consistency(*face.value);
        if (ok.status != axiom::StatusCode::Ok) {
            std::cerr << "expected pcurve-edge consistency ok\n";
            return 1;
        }

        // New: endpoints can be UV-consistent, but the 3D edge curve can still disagree with pcurve->surface mapping.
        // Build an edge whose 3D curve is a circle arc, while the pcurve is a straight chord in UV.
        {
            auto circ = kernel.curves().make_circle({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 1.0);
            if (circ.status != axiom::StatusCode::Ok || !circ.value.has_value()) {
                std::cerr << "failed to create circle curve for trim consistency regression\n";
                return 1;
            }
            auto txn2 = kernel.topology().begin_transaction();
            auto a = txn2.create_vertex({1.0, 0.0, 0.0});
            auto b = txn2.create_vertex({0.0, 1.0, 0.0});
            auto e_arc = txn2.create_edge(*circ.value, *a.value, *b.value);
            auto e_chord = txn2.create_edge(*l1.value, *b.value, *a.value);
            auto c_arc = txn2.create_coedge(*e_arc.value, false);
            auto c_chord = txn2.create_coedge(*e_chord.value, false);
            if (a.status != axiom::StatusCode::Ok || b.status != axiom::StatusCode::Ok ||
                e_arc.status != axiom::StatusCode::Ok || e_chord.status != axiom::StatusCode::Ok ||
                c_arc.status != axiom::StatusCode::Ok || c_chord.status != axiom::StatusCode::Ok ||
                !a.value.has_value() || !b.value.has_value() || !e_arc.value.has_value() || !e_chord.value.has_value() ||
                !c_arc.value.has_value() || !c_chord.value.has_value()) {
                std::cerr << "failed to build arc/chord topology for trim consistency regression\n";
                return 1;
            }
            // UV endpoints match vertex UVs on plane, but middle deviates from circle arc.
            const std::array<axiom::Point2, 2> uv_ab {{ {1.0, 0.0}, {0.0, 1.0} }};
            const std::array<axiom::Point2, 2> uv_ba {{ {0.0, 1.0}, {1.0, 0.0} }};
            auto pc_ab = kernel.pcurves().make_polyline(uv_ab);
            auto pc_ba = kernel.pcurves().make_polyline(uv_ba);
            if (pc_ab.status != axiom::StatusCode::Ok || pc_ba.status != axiom::StatusCode::Ok ||
                !pc_ab.value.has_value() || !pc_ba.value.has_value()) {
                std::cerr << "failed to create pcurves for arc/chord trim consistency regression\n";
                return 1;
            }
            txn2.set_coedge_pcurve(*c_arc.value, *pc_ab.value);
            txn2.set_coedge_pcurve(*c_chord.value, *pc_ba.value);
            const std::array<axiom::CoedgeId, 2> ring {{ *c_arc.value, *c_chord.value }};
            auto loop2 = txn2.create_loop(ring);
            auto face2 = txn2.create_face(*plane.value, *loop2.value, {});
            if (loop2.status != axiom::StatusCode::Ok || face2.status != axiom::StatusCode::Ok ||
                !loop2.value.has_value() || !face2.value.has_value()) {
                std::cerr << "failed to create face for arc/chord trim consistency regression\n";
                return 1;
            }
            auto bad_arc = kernel.topology().validate().validate_face_trim_consistency(*face2.value);
            if (bad_arc.status == axiom::StatusCode::Ok) {
                std::cerr << "expected pcurve-surface mapping to disagree with 3D edge curve\n";
                return 1;
            }
            txn2.rollback();
        }

        // Endpoints still match, but the middle point deviates: should fail sampling consistency.
        const std::array<axiom::Point2, 3> uv01_bulge {{ {0.0, 0.0}, {0.5, 0.2}, {1.0, 0.0} }};
        auto pc01_bulge = kernel.pcurves().make_polyline(uv01_bulge);
        if (pc01_bulge.status != axiom::StatusCode::Ok || !pc01_bulge.value.has_value()) {
            std::cerr << "failed to create bulged pcurve\n";
            return 1;
        }
        txn.set_coedge_pcurve(*c0.value, *pc01_bulge.value);
        auto bad_mid = kernel.topology().validate().validate_face_trim_consistency(*face.value);
        if (bad_mid.status == axiom::StatusCode::Ok) {
            std::cerr << "expected pcurve-edge sampling consistency to fail\n";
            return 1;
        }

        // Repair: rebuild pcurves from 3D edges on plane, then it should pass.
        auto repaired = kernel.repair().repair_face_trim_pcurves(*face.value, axiom::RepairMode::Safe);
        if (repaired.status != axiom::StatusCode::Ok) {
            std::cerr << "expected trim pcurve repair ok\n";
            return 1;
        }
        auto ok2 = kernel.topology().validate().validate_face_trim_consistency(*face.value);
        if (ok2.status != axiom::StatusCode::Ok) {
            std::cerr << "expected trim consistency ok after repair\n";
            return 1;
        }
        // Restore for next checks.
        txn.set_coedge_pcurve(*c0.value, *pc01.value);

        // Now bind a still-closed but shifted UV ring: should fail endpoint consistency.
        const std::array<axiom::Point2, 2> uv01s {{ {0.1, 0.0}, {1.1, 0.0} }};
        const std::array<axiom::Point2, 2> uv12s {{ {1.1, 0.0}, {1.1, 1.0} }};
        const std::array<axiom::Point2, 2> uv23s {{ {1.1, 1.0}, {0.1, 1.0} }};
        const std::array<axiom::Point2, 2> uv30s {{ {0.1, 1.0}, {0.1, 0.0} }};
        auto pc01s = kernel.pcurves().make_polyline(uv01s);
        auto pc12s = kernel.pcurves().make_polyline(uv12s);
        auto pc23s = kernel.pcurves().make_polyline(uv23s);
        auto pc30s = kernel.pcurves().make_polyline(uv30s);
        txn.set_coedge_pcurve(*c0.value, *pc01s.value);
        txn.set_coedge_pcurve(*c1.value, *pc12s.value);
        txn.set_coedge_pcurve(*c2.value, *pc23s.value);
        txn.set_coedge_pcurve(*c3.value, *pc30s.value);
        auto bad = kernel.topology().validate().validate_face_trim_consistency(*face.value);
        if (bad.status == axiom::StatusCode::Ok) {
            std::cerr << "expected pcurve-edge endpoint consistency to fail\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for pcurve-edge consistency test\n";
            return 1;
        }
    }

    // ---- Stage 3: Trim repair on cylinder face (rebuild pcurves from 3D edges) ----
    {
        const double pi = std::acos(-1.0);
        auto cyl = kernel.surfaces().make_cylinder({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 1.0);
        auto circ = kernel.curves().make_circle({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 1.0);
        if (cyl.status != axiom::StatusCode::Ok || circ.status != axiom::StatusCode::Ok ||
            !cyl.value.has_value() || !circ.value.has_value()) {
            std::cerr << "failed to create cylinder/circle for trim repair test\n";
            return 1;
        }

        auto txn = kernel.topology().begin_transaction();
        auto va = txn.create_vertex({1.0, 0.0, 0.0});
        auto vb = txn.create_vertex({0.0, 1.0, 0.0});
        auto e_ab = txn.create_edge(*circ.value, *va.value, *vb.value);
        auto e_ba = txn.create_edge(*circ.value, *vb.value, *va.value);
        auto c_ab = txn.create_coedge(*e_ab.value, false);
        auto c_ba = txn.create_coedge(*e_ba.value, false);
        if (va.status != axiom::StatusCode::Ok || vb.status != axiom::StatusCode::Ok ||
            e_ab.status != axiom::StatusCode::Ok || e_ba.status != axiom::StatusCode::Ok ||
            c_ab.status != axiom::StatusCode::Ok || c_ba.status != axiom::StatusCode::Ok ||
            !va.value.has_value() || !vb.value.has_value() ||
            !e_ab.value.has_value() || !e_ba.value.has_value() ||
            !c_ab.value.has_value() || !c_ba.value.has_value()) {
            std::cerr << "failed to build cylinder face topology\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 2> ring{{*c_ab.value, *c_ba.value}};
        auto loop = txn.create_loop(ring);
        auto face = txn.create_face(*cyl.value, *loop.value, {});
        if (loop.status != axiom::StatusCode::Ok || face.status != axiom::StatusCode::Ok ||
            !loop.value.has_value() || !face.value.has_value()) {
            std::cerr << "failed to create cylinder face\n";
            return 1;
        }

        // Bind an obviously wrong pcurve for one coedge: v should remain 0 on this circle, but we bulge it.
        const std::array<axiom::Point2, 3> wrong{{{0.0, 0.0}, {pi * 0.25, 0.3}, {pi * 0.5, 0.0}}};
        auto pc_wrong = kernel.pcurves().make_polyline(wrong);
        if (pc_wrong.status != axiom::StatusCode::Ok || !pc_wrong.value.has_value()) {
            std::cerr << "failed to create wrong pcurve for cylinder repair test\n";
            return 1;
        }
        txn.set_coedge_pcurve(*c_ab.value, *pc_wrong.value);

        auto bad = kernel.topology().validate().validate_face_trim_consistency(*face.value);
        if (bad.status == axiom::StatusCode::Ok) {
            std::cerr << "expected cylinder trim consistency to fail before repair\n";
            return 1;
        }
        auto bad_rep = kernel.diagnostics().get(bad.diagnostic_id);
        if (bad_rep.status != axiom::StatusCode::Ok || !bad_rep.value.has_value() ||
            !issue_links_entities(*bad_rep.value, axiom::diag_codes::kTopoCurveTopologyMismatch,
                                  {face.value->value, c_ba.value->value})) {
            std::cerr << "expected incomplete-trim failure with related_entities face + missing-pcurve coedge\n";
            return 1;
        }
        auto fixed = kernel.repair().repair_face_trim_pcurves(*face.value, axiom::RepairMode::Safe);
        if (fixed.status != axiom::StatusCode::Ok) {
            std::cerr << "expected cylinder trim pcurve repair ok\n";
            return 1;
        }
        auto ok = kernel.topology().validate().validate_face_trim_consistency(*face.value);
        if (ok.status != axiom::StatusCode::Ok) {
            std::cerr << "expected cylinder trim consistency ok after repair\n";
            return 1;
        }
        txn.rollback();
    }

    // ---- Stage 3b: Trim repair on sphere face (same pattern as cylinder) ----
    {
        const double pi = std::acos(-1.0);
        auto sph = kernel.surfaces().make_sphere({0.0, 0.0, 0.0}, 1.0);
        auto circ = kernel.curves().make_circle({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 1.0);
        if (sph.status != axiom::StatusCode::Ok || circ.status != axiom::StatusCode::Ok ||
            !sph.value.has_value() || !circ.value.has_value()) {
            std::cerr << "failed to create sphere/circle for trim repair test\n";
            return 1;
        }

        auto txn = kernel.topology().begin_transaction();
        auto va = txn.create_vertex({1.0, 0.0, 0.0});
        auto vb = txn.create_vertex({0.0, 1.0, 0.0});
        auto e_ab = txn.create_edge(*circ.value, *va.value, *vb.value);
        auto e_ba = txn.create_edge(*circ.value, *vb.value, *va.value);
        auto c_ab = txn.create_coedge(*e_ab.value, false);
        auto c_ba = txn.create_coedge(*e_ba.value, false);
        if (va.status != axiom::StatusCode::Ok || vb.status != axiom::StatusCode::Ok ||
            e_ab.status != axiom::StatusCode::Ok || e_ba.status != axiom::StatusCode::Ok ||
            c_ab.status != axiom::StatusCode::Ok || c_ba.status != axiom::StatusCode::Ok ||
            !va.value.has_value() || !vb.value.has_value() ||
            !e_ab.value.has_value() || !e_ba.value.has_value() ||
            !c_ab.value.has_value() || !c_ba.value.has_value()) {
            std::cerr << "failed to build sphere face topology\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 2> ring{{*c_ab.value, *c_ba.value}};
        auto loop = txn.create_loop(ring);
        auto face = txn.create_face(*sph.value, *loop.value, {});
        if (loop.status != axiom::StatusCode::Ok || face.status != axiom::StatusCode::Ok ||
            !loop.value.has_value() || !face.value.has_value()) {
            std::cerr << "failed to create sphere face\n";
            return 1;
        }

        const std::array<axiom::Point2, 3> wrong{{{0.0, pi * 0.5}, {pi * 0.3, pi * 0.35}, {pi * 0.5, pi * 0.5}}};
        auto pc_wrong = kernel.pcurves().make_polyline(wrong);
        if (pc_wrong.status != axiom::StatusCode::Ok || !pc_wrong.value.has_value()) {
            std::cerr << "failed to create wrong pcurve for sphere repair test\n";
            return 1;
        }
        txn.set_coedge_pcurve(*c_ab.value, *pc_wrong.value);

        auto bad = kernel.topology().validate().validate_face_trim_consistency(*face.value);
        if (bad.status == axiom::StatusCode::Ok) {
            std::cerr << "expected sphere trim consistency to fail before repair\n";
            return 1;
        }
        auto bad_rep = kernel.diagnostics().get(bad.diagnostic_id);
        if (bad_rep.status != axiom::StatusCode::Ok || !bad_rep.value.has_value() ||
            !issue_links_entities(*bad_rep.value, axiom::diag_codes::kTopoCurveTopologyMismatch,
                                  {face.value->value, c_ba.value->value})) {
            std::cerr << "expected sphere incomplete-trim failure with face + missing-pcurve coedge\n";
            return 1;
        }
        auto fixed = kernel.repair().repair_face_trim_pcurves(*face.value, axiom::RepairMode::Safe);
        if (fixed.status != axiom::StatusCode::Ok) {
            std::cerr << "expected sphere trim pcurve repair ok\n";
            return 1;
        }
        auto ok = kernel.topology().validate().validate_face_trim_consistency(*face.value);
        if (ok.status != axiom::StatusCode::Ok) {
            std::cerr << "expected sphere trim consistency ok after repair\n";
            return 1;
        }
        txn.rollback();
    }

    // ---- Stage 2: duplicate underlying edge in one loop (diagnostic code) ----
    {
        auto line01 = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        if (line01.status != axiom::StatusCode::Ok || !line01.value.has_value()) {
            std::cerr << "failed to create curve for duplicate-edge loop test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto va = txn.create_vertex({0.0, 0.0, 0.0});
        auto vb = txn.create_vertex({1.0, 0.0, 0.0});
        auto e01 = txn.create_edge(*line01.value, *va.value, *vb.value);
        auto c_fwd = txn.create_coedge(*e01.value, false);
        auto c_rev = txn.create_coedge(*e01.value, true);
        if (va.status != axiom::StatusCode::Ok || vb.status != axiom::StatusCode::Ok ||
            e01.status != axiom::StatusCode::Ok || c_fwd.status != axiom::StatusCode::Ok ||
            c_rev.status != axiom::StatusCode::Ok || !c_fwd.value.has_value() || !c_rev.value.has_value()) {
            std::cerr << "failed to build topology for duplicate-edge loop test\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 2> dup_loop {{ *c_fwd.value, *c_rev.value }};
        auto bad_loop = txn.create_loop(dup_loop);
        if (bad_loop.status == axiom::StatusCode::Ok) {
            std::cerr << "expected invalid topology for duplicate edge within a loop\n";
            return 1;
        }
        auto dup_diag = kernel.diagnostics().get(bad_loop.diagnostic_id);
        if (dup_diag.status != axiom::StatusCode::Ok || !dup_diag.value.has_value() ||
            !has_issue_code(*dup_diag.value, axiom::diag_codes::kTopoLoopDuplicateEdge)) {
            std::cerr << "expected kTopoLoopDuplicateEdge in diagnostics for duplicate edge loop\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for duplicate-edge loop test\n";
            return 1;
        }
    }

    // ---- Stage 2: duplicate coedge id in loop sequence (kTopoLoopDuplicateEdge) ----
    {
        auto line01 = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        if (line01.status != axiom::StatusCode::Ok || !line01.value.has_value()) {
            std::cerr << "failed to create curve for duplicate-coedge loop test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto va = txn.create_vertex({0.0, 0.0, 0.0});
        auto vb = txn.create_vertex({1.0, 0.0, 0.0});
        auto e01 = txn.create_edge(*line01.value, *va.value, *vb.value);
        auto c0 = txn.create_coedge(*e01.value, false);
        if (va.status != axiom::StatusCode::Ok || vb.status != axiom::StatusCode::Ok ||
            e01.status != axiom::StatusCode::Ok || c0.status != axiom::StatusCode::Ok ||
            !c0.value.has_value()) {
            std::cerr << "failed to build topology for duplicate-coedge loop test\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 2> dup_co {{ *c0.value, *c0.value }};
        auto bad = txn.create_loop(dup_co);
        if (bad.status == axiom::StatusCode::Ok) {
            std::cerr << "expected invalid topology for duplicate coedge in loop\n";
            return 1;
        }
        auto dup_co_diag = kernel.diagnostics().get(bad.diagnostic_id);
        if (dup_co_diag.status != axiom::StatusCode::Ok || !dup_co_diag.value.has_value() ||
            !has_issue_code(*dup_co_diag.value, axiom::diag_codes::kTopoLoopDuplicateEdge)) {
            std::cerr << "expected kTopoLoopDuplicateEdge for duplicate coedge in create_loop\n";
            return 1;
        }
        if (txn.rollback().status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for duplicate-coedge loop test\n";
            return 1;
        }
    }

    // ---- Stage 2: outer/inner loop UV orientation (hole must oppose outer) ----
    {
        auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto l0 = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = kernel.curves().make_line({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = kernel.curves().make_line({1.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = kernel.curves().make_line({0.0, 1.0, 0.0}, {0.0, -1.0, 0.0});
        auto li0 = kernel.curves().make_line({0.25, 0.25, 0.0}, {0.75, 0.25, 0.0});
        auto li1 = kernel.curves().make_line({0.75, 0.25, 0.0}, {0.75, 0.75, 0.0});
        auto li2 = kernel.curves().make_line({0.75, 0.75, 0.0}, {0.25, 0.75, 0.0});
        auto li3 = kernel.curves().make_line({0.25, 0.75, 0.0}, {0.25, 0.25, 0.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            l0.status != axiom::StatusCode::Ok || !l0.value.has_value() ||
            l1.status != axiom::StatusCode::Ok || !l1.value.has_value() ||
            l2.status != axiom::StatusCode::Ok || !l2.value.has_value() ||
            l3.status != axiom::StatusCode::Ok || !l3.value.has_value() ||
            li0.status != axiom::StatusCode::Ok || !li0.value.has_value() ||
            li1.status != axiom::StatusCode::Ok || !li1.value.has_value() ||
            li2.status != axiom::StatusCode::Ok || !li2.value.has_value() ||
            li3.status != axiom::StatusCode::Ok || !li3.value.has_value()) {
            std::cerr << "failed to create geometry for loop orientation test\n";
            return 1;
        }

        auto txn = kernel.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
        auto v2 = txn.create_vertex({1.0, 1.0, 0.0});
        auto v3 = txn.create_vertex({0.0, 1.0, 0.0});
        auto e0 = txn.create_edge(*l0.value, *v0.value, *v1.value);
        auto e1 = txn.create_edge(*l1.value, *v1.value, *v2.value);
        auto e2 = txn.create_edge(*l2.value, *v2.value, *v3.value);
        auto e3 = txn.create_edge(*l3.value, *v3.value, *v0.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        auto c1 = txn.create_coedge(*e1.value, false);
        auto c2 = txn.create_coedge(*e2.value, false);
        auto c3 = txn.create_coedge(*e3.value, false);

        auto iv0 = txn.create_vertex({0.25, 0.25, 0.0});
        auto iv1 = txn.create_vertex({0.75, 0.25, 0.0});
        auto iv2 = txn.create_vertex({0.75, 0.75, 0.0});
        auto iv3 = txn.create_vertex({0.25, 0.75, 0.0});
        auto ie0 = txn.create_edge(*li0.value, *iv0.value, *iv1.value);
        auto ie1 = txn.create_edge(*li1.value, *iv1.value, *iv2.value);
        auto ie2 = txn.create_edge(*li2.value, *iv2.value, *iv3.value);
        auto ie3 = txn.create_edge(*li3.value, *iv3.value, *iv0.value);
        auto ic0 = txn.create_coedge(*ie0.value, false);
        auto ic1 = txn.create_coedge(*ie1.value, false);
        auto ic2 = txn.create_coedge(*ie2.value, false);
        auto ic3 = txn.create_coedge(*ie3.value, false);

        if (c0.status != axiom::StatusCode::Ok || ic0.status != axiom::StatusCode::Ok ||
            !c0.value.has_value() || !ic0.value.has_value()) {
            std::cerr << "failed to build topology for loop orientation test\n";
            return 1;
        }

        const std::array<axiom::Point2, 2> uvo01 {{ {0.0, 0.0}, {1.0, 0.0} }};
        const std::array<axiom::Point2, 2> uvo12 {{ {1.0, 0.0}, {1.0, 1.0} }};
        const std::array<axiom::Point2, 2> uvo23 {{ {1.0, 1.0}, {0.0, 1.0} }};
        const std::array<axiom::Point2, 2> uvo30 {{ {0.0, 1.0}, {0.0, 0.0} }};
        const std::array<axiom::Point2, 2> uvi01 {{ {0.25, 0.25}, {0.75, 0.25} }};
        const std::array<axiom::Point2, 2> uvi12 {{ {0.75, 0.25}, {0.75, 0.75} }};
        const std::array<axiom::Point2, 2> uvi23 {{ {0.75, 0.75}, {0.25, 0.75} }};
        const std::array<axiom::Point2, 2> uvi30 {{ {0.25, 0.75}, {0.25, 0.25} }};
        auto pc_o01 = kernel.pcurves().make_polyline(uvo01);
        auto pc_o12 = kernel.pcurves().make_polyline(uvo12);
        auto pc_o23 = kernel.pcurves().make_polyline(uvo23);
        auto pc_o30 = kernel.pcurves().make_polyline(uvo30);
        auto pc_i01 = kernel.pcurves().make_polyline(uvi01);
        auto pc_i12 = kernel.pcurves().make_polyline(uvi12);
        auto pc_i23 = kernel.pcurves().make_polyline(uvi23);
        auto pc_i30 = kernel.pcurves().make_polyline(uvi30);
        if (pc_o01.status != axiom::StatusCode::Ok || !pc_o01.value.has_value() ||
            pc_o12.status != axiom::StatusCode::Ok || !pc_o12.value.has_value() ||
            pc_o23.status != axiom::StatusCode::Ok || !pc_o23.value.has_value() ||
            pc_o30.status != axiom::StatusCode::Ok || !pc_o30.value.has_value() ||
            pc_i01.status != axiom::StatusCode::Ok || !pc_i01.value.has_value() ||
            pc_i12.status != axiom::StatusCode::Ok || !pc_i12.value.has_value() ||
            pc_i23.status != axiom::StatusCode::Ok || !pc_i23.value.has_value() ||
            pc_i30.status != axiom::StatusCode::Ok || !pc_i30.value.has_value()) {
            std::cerr << "failed to create pcurves for loop orientation test\n";
            return 1;
        }
        txn.set_coedge_pcurve(*c0.value, *pc_o01.value);
        txn.set_coedge_pcurve(*c1.value, *pc_o12.value);
        txn.set_coedge_pcurve(*c2.value, *pc_o23.value);
        txn.set_coedge_pcurve(*c3.value, *pc_o30.value);
        txn.set_coedge_pcurve(*ic0.value, *pc_i01.value);
        txn.set_coedge_pcurve(*ic1.value, *pc_i12.value);
        txn.set_coedge_pcurve(*ic2.value, *pc_i23.value);
        txn.set_coedge_pcurve(*ic3.value, *pc_i30.value);

        const std::array<axiom::CoedgeId, 4> oco {{ *c0.value, *c1.value, *c2.value, *c3.value }};
        const std::array<axiom::CoedgeId, 4> ico {{ *ic0.value, *ic1.value, *ic2.value, *ic3.value }};
        auto outer_loop = txn.create_loop(oco);
        auto inner_loop = txn.create_loop(ico);
        const std::array<axiom::LoopId, 1> inners {{ *inner_loop.value }};
        auto face_bad = txn.create_face(*plane.value, *outer_loop.value, inners);
        if (outer_loop.status != axiom::StatusCode::Ok || inner_loop.status != axiom::StatusCode::Ok ||
            face_bad.status != axiom::StatusCode::Ok || !face_bad.value.has_value()) {
            std::cerr << "failed to create face with inner loop for orientation test\n";
            return 1;
        }
        auto trim_bad = kernel.topology().validate().validate_face_trim_consistency(*face_bad.value);
        if (trim_bad.status == axiom::StatusCode::Ok) {
            std::cerr << "expected trim strict orientation check to fail (inner same winding as outer)\n";
            return 1;
        }
        auto ori_diag = kernel.diagnostics().get(trim_bad.diagnostic_id);
        if (ori_diag.status != axiom::StatusCode::Ok || !ori_diag.value.has_value() ||
            !has_issue_code(*ori_diag.value, axiom::diag_codes::kTopoLoopOrientationMismatch)) {
            std::cerr << "expected kTopoLoopOrientationMismatch for bad inner loop winding\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for loop orientation test\n";
            return 1;
        }
    }

    // ---- Stage 2+: planar face loop orientation (no PCurve): outer/inner must be opposite ----
    {
        auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value()) {
            std::cerr << "failed to create plane for planar loop orientation test\n";
            return 1;
        }

        auto l0 = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = kernel.curves().make_line({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = kernel.curves().make_line({1.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = kernel.curves().make_line({0.0, 1.0, 0.0}, {0.0, -1.0, 0.0});
        auto li0 = kernel.curves().make_line({0.25, 0.25, 0.0}, {0.5, 0.0, 0.0});
        auto li1 = kernel.curves().make_line({0.75, 0.25, 0.0}, {0.0, 0.5, 0.0});
        auto li2 = kernel.curves().make_line({0.75, 0.75, 0.0}, {-0.5, 0.0, 0.0});
        auto li3 = kernel.curves().make_line({0.25, 0.75, 0.0}, {0.0, -0.5, 0.0});
        if (l0.status != axiom::StatusCode::Ok || !l0.value.has_value() ||
            l1.status != axiom::StatusCode::Ok || !l1.value.has_value() ||
            l2.status != axiom::StatusCode::Ok || !l2.value.has_value() ||
            l3.status != axiom::StatusCode::Ok || !l3.value.has_value() ||
            li0.status != axiom::StatusCode::Ok || !li0.value.has_value() ||
            li1.status != axiom::StatusCode::Ok || !li1.value.has_value() ||
            li2.status != axiom::StatusCode::Ok || !li2.value.has_value() ||
            li3.status != axiom::StatusCode::Ok || !li3.value.has_value()) {
            std::cerr << "failed to create curves for planar loop orientation test\n";
            return 1;
        }

        auto txn = kernel.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
        auto v2 = txn.create_vertex({1.0, 1.0, 0.0});
        auto v3 = txn.create_vertex({0.0, 1.0, 0.0});
        auto iv0 = txn.create_vertex({0.25, 0.25, 0.0});
        auto iv1 = txn.create_vertex({0.75, 0.25, 0.0});
        auto iv2 = txn.create_vertex({0.75, 0.75, 0.0});
        auto iv3 = txn.create_vertex({0.25, 0.75, 0.0});
        auto e0 = txn.create_edge(*l0.value, *v0.value, *v1.value);
        auto e1 = txn.create_edge(*l1.value, *v1.value, *v2.value);
        auto e2 = txn.create_edge(*l2.value, *v2.value, *v3.value);
        auto e3 = txn.create_edge(*l3.value, *v3.value, *v0.value);
        auto ie0 = txn.create_edge(*li0.value, *iv0.value, *iv1.value);
        auto ie1 = txn.create_edge(*li1.value, *iv1.value, *iv2.value);
        auto ie2 = txn.create_edge(*li2.value, *iv2.value, *iv3.value);
        auto ie3 = txn.create_edge(*li3.value, *iv3.value, *iv0.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        auto c1 = txn.create_coedge(*e1.value, false);
        auto c2 = txn.create_coedge(*e2.value, false);
        auto c3 = txn.create_coedge(*e3.value, false);
        auto ic0 = txn.create_coedge(*ie0.value, false);
        auto ic1 = txn.create_coedge(*ie1.value, false);
        auto ic2 = txn.create_coedge(*ie2.value, false);
        auto ic3 = txn.create_coedge(*ie3.value, false);
        if (c0.status != axiom::StatusCode::Ok || ic0.status != axiom::StatusCode::Ok ||
            !c0.value.has_value() || !ic0.value.has_value()) {
            std::cerr << "failed to build topology for planar loop orientation test\n";
            return 1;
        }

        auto outer_loop = txn.create_loop(std::array<axiom::CoedgeId, 4>{*c0.value, *c1.value, *c2.value, *c3.value});
        auto inner_loop = txn.create_loop(std::array<axiom::CoedgeId, 4>{*ic0.value, *ic1.value, *ic2.value, *ic3.value});
        if (outer_loop.status != axiom::StatusCode::Ok || inner_loop.status != axiom::StatusCode::Ok ||
            !outer_loop.value.has_value() || !inner_loop.value.has_value()) {
            std::cerr << "failed to create loops for planar loop orientation test\n";
            return 1;
        }
        const std::array<axiom::LoopId, 1> inners {{ *inner_loop.value }};
        auto face = txn.create_face(*plane.value, *outer_loop.value, inners);
        auto shell = txn.create_shell(std::array<axiom::FaceId, 1>{*face.value});
        auto body = txn.create_body(std::array<axiom::ShellId, 1>{*shell.value});
        if (face.status != axiom::StatusCode::Ok || shell.status != axiom::StatusCode::Ok ||
            body.status != axiom::StatusCode::Ok || !body.value.has_value()) {
            std::cerr << "failed to create face/shell/body for planar loop orientation test\n";
            return 1;
        }
        auto r = kernel.validate().validate_topology(*body.value, axiom::ValidationMode::Standard);
        if (r.status == axiom::StatusCode::Ok) {
            std::cerr << "expected planar face loop orientation to fail (inner same winding as outer)\n";
            return 1;
        }
        auto diag = kernel.diagnostics().get(r.diagnostic_id);
        if (diag.status != axiom::StatusCode::Ok || !diag.value.has_value() ||
            !has_issue_code(*diag.value, axiom::diag_codes::kTopoLoopOrientationMismatch)) {
            std::cerr << "expected kTopoLoopOrientationMismatch for planar face loop winding\n";
            return 1;
        }

        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for planar loop orientation test\n";
            return 1;
        }
    }

    // ---- Stage 2+: sphere outer loop orientation (no PCurve), single outer ring ----
    {
        axiom::Kernel k_sph;
        auto sph = k_sph.surfaces().make_sphere({0.0, 0.0, 0.0}, 10.0);
        auto s0 = k_sph.curves().make_line_segment({8.0, 0.0, 6.0}, {0.0, 8.0, 6.0});
        auto s1 = k_sph.curves().make_line_segment({0.0, 8.0, 6.0}, {-8.0, 0.0, 6.0});
        auto s2 = k_sph.curves().make_line_segment({-8.0, 0.0, 6.0}, {0.0, -8.0, 6.0});
        auto s3 = k_sph.curves().make_line_segment({0.0, -8.0, 6.0}, {8.0, 0.0, 6.0});
        if (sph.status != axiom::StatusCode::Ok || !sph.value.has_value() ||
            s0.status != axiom::StatusCode::Ok || !s0.value.has_value() ||
            s1.status != axiom::StatusCode::Ok || !s1.value.has_value() ||
            s2.status != axiom::StatusCode::Ok || !s2.value.has_value() ||
            s3.status != axiom::StatusCode::Ok || !s3.value.has_value()) {
            std::cerr << "failed to init geometry for sphere loop orientation test\n";
            return 1;
        }
        auto txn = k_sph.topology().begin_transaction();
        auto v0 = txn.create_vertex({8.0, 0.0, 6.0});
        auto v1 = txn.create_vertex({0.0, 8.0, 6.0});
        auto v2 = txn.create_vertex({-8.0, 0.0, 6.0});
        auto v3 = txn.create_vertex({0.0, -8.0, 6.0});
        auto e0 = txn.create_edge(*s0.value, *v0.value, *v1.value);
        auto e1 = txn.create_edge(*s1.value, *v1.value, *v2.value);
        auto e2 = txn.create_edge(*s2.value, *v2.value, *v3.value);
        auto e3 = txn.create_edge(*s3.value, *v3.value, *v0.value);
        auto c0 = txn.create_coedge(*e0.value, true);
        auto c1 = txn.create_coedge(*e1.value, true);
        auto c2 = txn.create_coedge(*e2.value, true);
        auto c3 = txn.create_coedge(*e3.value, true);
        if (v0.status != axiom::StatusCode::Ok || !v0.value.has_value() ||
            v1.status != axiom::StatusCode::Ok || !v1.value.has_value() ||
            v2.status != axiom::StatusCode::Ok || !v2.value.has_value() ||
            v3.status != axiom::StatusCode::Ok || !v3.value.has_value() ||
            e0.status != axiom::StatusCode::Ok || !e0.value.has_value() ||
            e1.status != axiom::StatusCode::Ok || !e1.value.has_value() ||
            e2.status != axiom::StatusCode::Ok || !e2.value.has_value() ||
            e3.status != axiom::StatusCode::Ok || !e3.value.has_value() ||
            c0.status != axiom::StatusCode::Ok || !c0.value.has_value() ||
            c1.status != axiom::StatusCode::Ok || !c1.value.has_value() ||
            c2.status != axiom::StatusCode::Ok || !c2.value.has_value() ||
            c3.status != axiom::StatusCode::Ok || !c3.value.has_value()) {
            std::cerr << "failed to build topology for sphere loop orientation test\n";
            return 1;
        }
        // v0 -> v3 -> v2 -> v1 -> v0 (inner direction vs outward Newell for the patch).
        const std::array<axiom::CoedgeId, 4> co_bad {{*c3.value, *c2.value, *c1.value, *c0.value}};
        auto loop_bad = txn.create_loop(co_bad);
        auto face_bad = txn.create_face(*sph.value, *loop_bad.value, {});
        if (loop_bad.status != axiom::StatusCode::Ok || face_bad.status != axiom::StatusCode::Ok ||
            !loop_bad.value.has_value() || !face_bad.value.has_value()) {
            std::cerr << "failed to create sphere face for orientation test\n";
            return 1;
        }
        auto bad_val = k_sph.topology().validate().validate_face(*face_bad.value);
        if (bad_val.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_face to fail for bad sphere outer winding\n";
            return 1;
        }
        auto bad_diag = k_sph.diagnostics().get(bad_val.diagnostic_id);
        if (bad_diag.status != axiom::StatusCode::Ok || !bad_diag.value.has_value() ||
            !has_issue_code(*bad_diag.value, axiom::diag_codes::kTopoLoopOrientationMismatch)) {
            std::cerr << "expected kTopoLoopOrientationMismatch for sphere outer winding\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for sphere loop orientation test\n";
            return 1;
        }
    }

    // ---- Stage 2+: cylinder outer loop orientation (no PCurve), non-planar quad on wall ----
    {
        axiom::Kernel k_cyl;
        auto cyl = k_cyl.surfaces().make_cylinder({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 1.0);
        auto s01 = k_cyl.curves().make_line_segment({1.0, 0.0, 0.0}, {0.0, 1.0, 0.5});
        auto s12 = k_cyl.curves().make_line_segment({0.0, 1.0, 0.5}, {-1.0, 0.0, 1.0});
        auto s23 = k_cyl.curves().make_line_segment({-1.0, 0.0, 1.0}, {0.0, -1.0, 0.5});
        auto s30 = k_cyl.curves().make_line_segment({0.0, -1.0, 0.5}, {1.0, 0.0, 0.0});
        if (cyl.status != axiom::StatusCode::Ok || !cyl.value.has_value() ||
            s01.status != axiom::StatusCode::Ok || !s01.value.has_value() ||
            s12.status != axiom::StatusCode::Ok || !s12.value.has_value() ||
            s23.status != axiom::StatusCode::Ok || !s23.value.has_value() ||
            s30.status != axiom::StatusCode::Ok || !s30.value.has_value()) {
            std::cerr << "failed to init geometry for cylinder loop orientation test\n";
            return 1;
        }
        auto txn = k_cyl.topology().begin_transaction();
        auto v0 = txn.create_vertex({1.0, 0.0, 0.0});
        auto v1 = txn.create_vertex({0.0, 1.0, 0.5});
        auto v2 = txn.create_vertex({-1.0, 0.0, 1.0});
        auto v3 = txn.create_vertex({0.0, -1.0, 0.5});
        auto e01 = txn.create_edge(*s01.value, *v0.value, *v1.value);
        auto e12 = txn.create_edge(*s12.value, *v1.value, *v2.value);
        auto e23 = txn.create_edge(*s23.value, *v2.value, *v3.value);
        auto e30 = txn.create_edge(*s30.value, *v3.value, *v0.value);
        auto c01 = txn.create_coedge(*e01.value, false);
        auto c12 = txn.create_coedge(*e12.value, false);
        auto c23 = txn.create_coedge(*e23.value, false);
        auto c30 = txn.create_coedge(*e30.value, false);
        // Opposite winding: same edges, reversed coedges so the loop is still closed (v0->v3->v2->v1->v0).
        auto c30r = txn.create_coedge(*e30.value, true);
        auto c23r = txn.create_coedge(*e23.value, true);
        auto c12r = txn.create_coedge(*e12.value, true);
        auto c01r = txn.create_coedge(*e01.value, true);
        if (v0.status != axiom::StatusCode::Ok || !v0.value.has_value() ||
            c01.status != axiom::StatusCode::Ok || !c01.value.has_value() ||
            c30r.status != axiom::StatusCode::Ok || !c30r.value.has_value()) {
            std::cerr << "failed to build topology for cylinder loop orientation test\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 4> co_good{{*c01.value, *c12.value, *c23.value, *c30.value}};
        auto loop_good = txn.create_loop(co_good);
        auto face_good = txn.create_face(*cyl.value, *loop_good.value, {});
        if (loop_good.status != axiom::StatusCode::Ok || face_good.status != axiom::StatusCode::Ok ||
            !face_good.value.has_value()) {
            std::cerr << "failed to create good cylinder face for orientation test\n";
            return 1;
        }
        auto ok_val = k_cyl.topology().validate().validate_face(*face_good.value);
        if (ok_val.status != axiom::StatusCode::Ok) {
            std::cerr << "expected validate_face ok for good cylinder outer winding\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 4> co_bad{{*c30r.value, *c23r.value, *c12r.value, *c01r.value}};
        auto loop_bad = txn.create_loop(co_bad);
        auto face_bad = txn.create_face(*cyl.value, *loop_bad.value, {});
        if (loop_bad.status != axiom::StatusCode::Ok || face_bad.status != axiom::StatusCode::Ok ||
            !face_bad.value.has_value()) {
            std::cerr << "failed to create bad cylinder face for orientation test\n";
            return 1;
        }
        auto bad_val = k_cyl.topology().validate().validate_face(*face_bad.value);
        if (bad_val.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_face to fail for reversed cylinder outer winding\n";
            return 1;
        }
        auto bad_diag = k_cyl.diagnostics().get(bad_val.diagnostic_id);
        if (bad_diag.status != axiom::StatusCode::Ok || !bad_diag.value.has_value() ||
            !has_issue_code(*bad_diag.value, axiom::diag_codes::kTopoLoopOrientationMismatch)) {
            std::cerr << "expected kTopoLoopOrientationMismatch for cylinder outer winding\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for cylinder loop orientation test\n";
            return 1;
        }
    }

    // ---- Stage 2+: cone outer loop orientation (no PCurve), same axis-radial rule as cylinder ----
    {
        const double pi = std::acos(-1.0);
        axiom::Kernel k_cone;
        auto cone = k_cone.surfaces().make_cone({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, pi * 0.25);
        // 非共面四边形顶点在锥面上（r=z）：避免 Newell 与轴平行导致与径向点积退化。
        auto s01 = k_cone.curves().make_line_segment({1.0, 0.0, 1.0}, {0.0, 2.0, 2.0});
        auto s12 = k_cone.curves().make_line_segment({0.0, 2.0, 2.0}, {-2.0, 0.0, 2.0});
        auto s23 = k_cone.curves().make_line_segment({-2.0, 0.0, 2.0}, {0.0, -1.0, 1.0});
        auto s30 = k_cone.curves().make_line_segment({0.0, -1.0, 1.0}, {1.0, 0.0, 1.0});
        if (cone.status != axiom::StatusCode::Ok || !cone.value.has_value() ||
            s01.status != axiom::StatusCode::Ok || !s01.value.has_value() ||
            s12.status != axiom::StatusCode::Ok || !s12.value.has_value() ||
            s23.status != axiom::StatusCode::Ok || !s23.value.has_value() ||
            s30.status != axiom::StatusCode::Ok || !s30.value.has_value()) {
            std::cerr << "failed to init geometry for cone loop orientation test\n";
            return 1;
        }
        auto txn = k_cone.topology().begin_transaction();
        auto v0 = txn.create_vertex({1.0, 0.0, 1.0});
        auto v1 = txn.create_vertex({0.0, 2.0, 2.0});
        auto v2 = txn.create_vertex({-2.0, 0.0, 2.0});
        auto v3 = txn.create_vertex({0.0, -1.0, 1.0});
        auto e01 = txn.create_edge(*s01.value, *v0.value, *v1.value);
        auto e12 = txn.create_edge(*s12.value, *v1.value, *v2.value);
        auto e23 = txn.create_edge(*s23.value, *v2.value, *v3.value);
        auto e30 = txn.create_edge(*s30.value, *v3.value, *v0.value);
        auto c01 = txn.create_coedge(*e01.value, false);
        auto c12 = txn.create_coedge(*e12.value, false);
        auto c23 = txn.create_coedge(*e23.value, false);
        auto c30 = txn.create_coedge(*e30.value, false);
        auto c30r = txn.create_coedge(*e30.value, true);
        auto c23r = txn.create_coedge(*e23.value, true);
        auto c12r = txn.create_coedge(*e12.value, true);
        auto c01r = txn.create_coedge(*e01.value, true);
        if (c01.status != axiom::StatusCode::Ok || !c01.value.has_value() ||
            c30r.status != axiom::StatusCode::Ok || !c30r.value.has_value()) {
            std::cerr << "failed to build topology for cone loop orientation test\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 4> co_good{{*c01.value, *c12.value, *c23.value, *c30.value}};
        auto loop_good = txn.create_loop(co_good);
        auto face_good = txn.create_face(*cone.value, *loop_good.value, {});
        if (loop_good.status != axiom::StatusCode::Ok || face_good.status != axiom::StatusCode::Ok ||
            !face_good.value.has_value()) {
            std::cerr << "failed to create good cone face for orientation test\n";
            return 1;
        }
        if (k_cone.topology().validate().validate_face(*face_good.value).status != axiom::StatusCode::Ok) {
            std::cerr << "expected validate_face ok for good cone outer winding\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 4> co_bad{{*c30r.value, *c23r.value, *c12r.value, *c01r.value}};
        auto loop_bad = txn.create_loop(co_bad);
        auto face_bad = txn.create_face(*cone.value, *loop_bad.value, {});
        if (loop_bad.status != axiom::StatusCode::Ok || face_bad.status != axiom::StatusCode::Ok ||
            !face_bad.value.has_value()) {
            std::cerr << "failed to create bad cone face for orientation test\n";
            return 1;
        }
        auto bad_val = k_cone.topology().validate().validate_face(*face_bad.value);
        if (bad_val.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_face to fail for reversed cone outer winding\n";
            return 1;
        }
        auto bad_diag = k_cone.diagnostics().get(bad_val.diagnostic_id);
        if (bad_diag.status != axiom::StatusCode::Ok || !bad_diag.value.has_value() ||
            !has_issue_code(*bad_diag.value, axiom::diag_codes::kTopoLoopOrientationMismatch)) {
            std::cerr << "expected kTopoLoopOrientationMismatch for cone outer winding\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for cone loop orientation test\n";
            return 1;
        }
    }

    // ---- Stage 2+: torus outer loop orientation (no PCurve), quad from surface eval ----
    {
        axiom::Kernel k_tor;
        auto tor = k_tor.surfaces().make_torus({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 3.0, 0.5);
        if (tor.status != axiom::StatusCode::Ok || !tor.value.has_value()) {
            std::cerr << "failed to create torus for orientation test\n";
            return 1;
        }
        const axiom::SurfaceId sid = *tor.value;
        const struct {
            double u;
            double v;
        } uvs[4] = {{0.0, 0.12}, {0.9, 0.18}, {1.8, 0.14}, {2.7, 0.16}};
        axiom::Point3 pts[4]{};
        for (int i = 0; i < 4; ++i) {
            auto ev = k_tor.surface_service().eval(sid, uvs[i].u, uvs[i].v, 0);
            if (ev.status != axiom::StatusCode::Ok || !ev.value.has_value()) {
                std::cerr << "failed eval torus point for orientation test\n";
                return 1;
            }
            pts[i] = ev.value->point;
        }
        auto s01 = k_tor.curves().make_line_segment(pts[0], pts[1]);
        auto s12 = k_tor.curves().make_line_segment(pts[1], pts[2]);
        auto s23 = k_tor.curves().make_line_segment(pts[2], pts[3]);
        auto s30 = k_tor.curves().make_line_segment(pts[3], pts[0]);
        if (s01.status != axiom::StatusCode::Ok || !s01.value.has_value() ||
            s12.status != axiom::StatusCode::Ok || !s12.value.has_value() ||
            s23.status != axiom::StatusCode::Ok || !s23.value.has_value() ||
            s30.status != axiom::StatusCode::Ok || !s30.value.has_value()) {
            std::cerr << "failed to init curves for torus loop orientation test\n";
            return 1;
        }
        auto txn = k_tor.topology().begin_transaction();
        auto v0 = txn.create_vertex(pts[0]);
        auto v1 = txn.create_vertex(pts[1]);
        auto v2 = txn.create_vertex(pts[2]);
        auto v3 = txn.create_vertex(pts[3]);
        auto e01 = txn.create_edge(*s01.value, *v0.value, *v1.value);
        auto e12 = txn.create_edge(*s12.value, *v1.value, *v2.value);
        auto e23 = txn.create_edge(*s23.value, *v2.value, *v3.value);
        auto e30 = txn.create_edge(*s30.value, *v3.value, *v0.value);
        auto c01 = txn.create_coedge(*e01.value, false);
        auto c12 = txn.create_coedge(*e12.value, false);
        auto c23 = txn.create_coedge(*e23.value, false);
        auto c30 = txn.create_coedge(*e30.value, false);
        auto c30r = txn.create_coedge(*e30.value, true);
        auto c23r = txn.create_coedge(*e23.value, true);
        auto c12r = txn.create_coedge(*e12.value, true);
        auto c01r = txn.create_coedge(*e01.value, true);
        if (v0.status != axiom::StatusCode::Ok || !v0.value.has_value() ||
            c01.status != axiom::StatusCode::Ok || !c01.value.has_value() ||
            c30r.status != axiom::StatusCode::Ok || !c30r.value.has_value()) {
            std::cerr << "failed to build topology for torus loop orientation test\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 4> co_good{{*c01.value, *c12.value, *c23.value, *c30.value}};
        auto loop_good = txn.create_loop(co_good);
        auto face_good = txn.create_face(sid, *loop_good.value, {});
        if (loop_good.status != axiom::StatusCode::Ok || face_good.status != axiom::StatusCode::Ok ||
            !face_good.value.has_value()) {
            std::cerr << "failed to create good torus face for orientation test\n";
            return 1;
        }
        if (k_tor.topology().validate().validate_face(*face_good.value).status != axiom::StatusCode::Ok) {
            std::cerr << "expected validate_face ok for good torus outer winding\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 4> co_bad{{*c30r.value, *c23r.value, *c12r.value, *c01r.value}};
        auto loop_bad = txn.create_loop(co_bad);
        auto face_bad = txn.create_face(sid, *loop_bad.value, {});
        if (loop_bad.status != axiom::StatusCode::Ok || face_bad.status != axiom::StatusCode::Ok ||
            !face_bad.value.has_value()) {
            std::cerr << "failed to create bad torus face for orientation test\n";
            return 1;
        }
        auto bad_val = k_tor.topology().validate().validate_face(*face_bad.value);
        if (bad_val.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_face to fail for reversed torus outer winding\n";
            return 1;
        }
        auto bad_diag = k_tor.diagnostics().get(bad_val.diagnostic_id);
        if (bad_diag.status != axiom::StatusCode::Ok || !bad_diag.value.has_value() ||
            !has_issue_code(*bad_diag.value, axiom::diag_codes::kTopoLoopOrientationMismatch)) {
            std::cerr << "expected kTopoLoopOrientationMismatch for torus outer winding\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for torus loop orientation test\n";
            return 1;
        }
    }

    // ---- Stage 2: dangling edge (no coedge) ----
    {
        auto line_d = kernel.curves().make_line({2.0, 0.0, 0.0}, {3.0, 0.0, 0.0});
        if (line_d.status != axiom::StatusCode::Ok || !line_d.value.has_value()) {
            std::cerr << "failed to create curve for dangling-edge test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto p0 = txn.create_vertex({2.0, 0.0, 0.0});
        auto p1 = txn.create_vertex({3.0, 0.0, 0.0});
        auto e_d = txn.create_edge(*line_d.value, *p0.value, *p1.value);
        if (e_d.status != axiom::StatusCode::Ok || !e_d.value.has_value()) {
            std::cerr << "failed to create dangling edge\n";
            return 1;
        }
        auto ev = kernel.topology().validate().validate_edge(*e_d.value);
        if (ev.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_edge to fail for dangling edge\n";
            return 1;
        }
        auto ev_diag = kernel.diagnostics().get(ev.diagnostic_id);
        if (ev_diag.status != axiom::StatusCode::Ok || !ev_diag.value.has_value() ||
            !has_issue_code(*ev_diag.value, axiom::diag_codes::kTopoDanglingEdge)) {
            std::cerr << "expected kTopoDanglingEdge diagnostic for dangling edge\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for dangling-edge test\n";
            return 1;
        }
    }

    // ---- TopoCore (7.2): face-bound loop must have at least three coedges (kTopoLoopNotClosed) ----
    {
        auto la = kernel.curves().make_line({5.0, 0.0, 0.0}, {6.0, 0.0, 0.0});
        auto lb = kernel.curves().make_line({6.0, 0.0, 0.0}, {5.0, 0.0, 0.0});
        auto plane = kernel.surfaces().make_plane({5.5, 0.0, 0.0}, {0.0, 0.0, 1.0});
        if (la.status != axiom::StatusCode::Ok || !la.value || lb.status != axiom::StatusCode::Ok || !lb.value ||
            plane.status != axiom::StatusCode::Ok || !plane.value) {
            std::cerr << "failed to create geometry for two-edge face loop test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto v0 = txn.create_vertex({5.0, 0.0, 0.0});
        auto v1 = txn.create_vertex({6.0, 0.0, 0.0});
        auto e0 = txn.create_edge(*la.value, *v0.value, *v1.value);
        auto e1 = txn.create_edge(*lb.value, *v1.value, *v0.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        auto c1 = txn.create_coedge(*e1.value, false);
        if (v0.status != axiom::StatusCode::Ok || !v0.value || v1.status != axiom::StatusCode::Ok || !v1.value ||
            e0.status != axiom::StatusCode::Ok || !e0.value || e1.status != axiom::StatusCode::Ok || !e1.value ||
            c0.status != axiom::StatusCode::Ok || !c0.value || c1.status != axiom::StatusCode::Ok || !c1.value) {
            std::cerr << "failed to build two-coedge loop topology\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 2> two {{ *c0.value, *c1.value }};
        auto lp = txn.create_loop(two);
        auto face = txn.create_face(*plane.value, *lp.value, {});
        if (lp.status != axiom::StatusCode::Ok || !lp.value || face.status != axiom::StatusCode::Ok || !face.value) {
            std::cerr << "failed to create two-edge loop face\n";
            return 1;
        }
        auto lv = kernel.topology().validate().validate_loop(*lp.value);
        if (lv.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_loop to fail for two-coedge face-bound loop\n";
            return 1;
        }
        auto ld = kernel.diagnostics().get(lv.diagnostic_id);
        if (ld.status != axiom::StatusCode::Ok || !ld.value ||
            !has_issue_code(*ld.value, axiom::diag_codes::kTopoLoopNotClosed)) {
            std::cerr << "expected kTopoLoopNotClosed on validate_loop for two-line digon\n";
            return 1;
        }
        auto fv = kernel.topology().validate().validate_face(*face.value);
        if (fv.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_face to fail for two-coedge outer loop on a face\n";
            return 1;
        }
        auto fd = kernel.diagnostics().get(fv.diagnostic_id);
        // validate_face maps outer validate_loop failures to kTopoFaceOuterLoopInvalid.
        if (fd.status != axiom::StatusCode::Ok || !fd.value ||
            !has_issue_code(*fd.value, axiom::diag_codes::kTopoFaceOuterLoopInvalid)) {
            std::cerr << "expected kTopoFaceOuterLoopInvalid for face with only two boundary coedges\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for two-edge face loop test\n";
            return 1;
        }
    }

    // ---- Stage 2: coedge must belong to at most one loop (relation inconsistent) ----
    {
        auto l0 = kernel.curves().make_line({0.0, 2.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = kernel.curves().make_line({1.0, 2.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = kernel.curves().make_line({1.0, 3.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = kernel.curves().make_line({0.0, 3.0, 0.0}, {0.0, -1.0, 0.0});
        if (l0.status != axiom::StatusCode::Ok || !l0.value.has_value() ||
            l1.status != axiom::StatusCode::Ok || !l1.value.has_value() ||
            l2.status != axiom::StatusCode::Ok || !l2.value.has_value() ||
            l3.status != axiom::StatusCode::Ok || !l3.value.has_value()) {
            std::cerr << "failed to create curves for coedge ownership test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 2.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 2.0, 0.0});
        auto v2 = txn.create_vertex({1.0, 3.0, 0.0});
        auto v3 = txn.create_vertex({0.0, 3.0, 0.0});
        auto e0 = txn.create_edge(*l0.value, *v0.value, *v1.value);
        auto e1 = txn.create_edge(*l1.value, *v1.value, *v2.value);
        auto e2 = txn.create_edge(*l2.value, *v2.value, *v3.value);
        auto e3 = txn.create_edge(*l3.value, *v3.value, *v0.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        auto c1 = txn.create_coedge(*e1.value, false);
        auto c2 = txn.create_coedge(*e2.value, false);
        auto c3 = txn.create_coedge(*e3.value, false);
        const std::array<axiom::CoedgeId, 4> coedges {{ *c0.value, *c1.value, *c2.value, *c3.value }};
        auto loop1 = txn.create_loop(coedges);
        if (loop1.status != axiom::StatusCode::Ok || !loop1.value.has_value()) {
            std::cerr << "failed to create first loop for coedge ownership test\n";
            return 1;
        }
        auto loop2 = txn.create_loop(coedges);
        if (loop2.status == axiom::StatusCode::Ok) {
            std::cerr << "expected create_loop to fail when reusing coedges across loops\n";
            return 1;
        }
        auto loop2_diag = kernel.diagnostics().get(loop2.diagnostic_id);
        if (loop2_diag.status != axiom::StatusCode::Ok || !loop2_diag.value.has_value() ||
            !has_issue_code(*loop2_diag.value, axiom::diag_codes::kTopoCoedgeAlreadyOwned)) {
            std::cerr << "expected kTopoCoedgeAlreadyOwned for coedge reused across loops\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for coedge ownership test\n";
            return 1;
        }
    }

    // ---- Stage 2: duplicate FaceId in shell face list rejected (7.2: loop single-owner forbids two faces sharing one loop) ----
    {
        auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto l0 = kernel.curves().make_line({0.0, 4.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = kernel.curves().make_line({1.0, 4.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = kernel.curves().make_line({1.0, 5.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = kernel.curves().make_line({0.0, 5.0, 0.0}, {0.0, -1.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            l0.status != axiom::StatusCode::Ok || !l0.value.has_value() ||
            l1.status != axiom::StatusCode::Ok || !l1.value.has_value() ||
            l2.status != axiom::StatusCode::Ok || !l2.value.has_value() ||
            l3.status != axiom::StatusCode::Ok || !l3.value.has_value()) {
            std::cerr << "failed to create geometry for duplicate face test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 4.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 4.0, 0.0});
        auto v2 = txn.create_vertex({1.0, 5.0, 0.0});
        auto v3 = txn.create_vertex({0.0, 5.0, 0.0});
        auto e0 = txn.create_edge(*l0.value, *v0.value, *v1.value);
        auto e1 = txn.create_edge(*l1.value, *v1.value, *v2.value);
        auto e2 = txn.create_edge(*l2.value, *v2.value, *v3.value);
        auto e3 = txn.create_edge(*l3.value, *v3.value, *v0.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        auto c1 = txn.create_coedge(*e1.value, false);
        auto c2 = txn.create_coedge(*e2.value, false);
        auto c3 = txn.create_coedge(*e3.value, false);
        const std::array<axiom::CoedgeId, 4> coedges {{ *c0.value, *c1.value, *c2.value, *c3.value }};
        auto loop = txn.create_loop(coedges);
        auto f0 = txn.create_face(*plane.value, *loop.value, {});
        if (loop.status != axiom::StatusCode::Ok || !loop.value.has_value() ||
            f0.status != axiom::StatusCode::Ok || !f0.value.has_value()) {
            std::cerr << "failed to create face for duplicate-in-shell list test\n";
            return 1;
        }
        const std::array<axiom::FaceId, 2> dup_list {{ *f0.value, *f0.value }};
        auto shell = txn.create_shell(dup_list);
        if (shell.status == axiom::StatusCode::Ok) {
            std::cerr << "expected create_shell to fail when face list repeats same FaceId\n";
            return 1;
        }
        auto shell_diag = kernel.diagnostics().get(shell.diagnostic_id);
        if (shell_diag.status != axiom::StatusCode::Ok || !shell_diag.value.has_value() ||
            !has_issue_code(*shell_diag.value, axiom::diag_codes::kTopoDuplicateFaceInShell)) {
            std::cerr << "expected kTopoDuplicateFaceInShell for repeated FaceId in shell list\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for duplicate face test\n";
            return 1;
        }
    }

    // ---- Stage 2: disconnected shell should be reported (relation inconsistent warning) ----
    {
        auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto l0 = kernel.curves().make_line({0.0, 6.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = kernel.curves().make_line({1.0, 6.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = kernel.curves().make_line({1.0, 7.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = kernel.curves().make_line({0.0, 7.0, 0.0}, {0.0, -1.0, 0.0});
        auto r0 = kernel.curves().make_line({3.0, 6.0, 0.0}, {1.0, 0.0, 0.0});
        auto r1 = kernel.curves().make_line({4.0, 6.0, 0.0}, {0.0, 1.0, 0.0});
        auto r2 = kernel.curves().make_line({4.0, 7.0, 0.0}, {-1.0, 0.0, 0.0});
        auto r3 = kernel.curves().make_line({3.0, 7.0, 0.0}, {0.0, -1.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value.has_value() ||
            l0.status != axiom::StatusCode::Ok || !l0.value.has_value() ||
            l1.status != axiom::StatusCode::Ok || !l1.value.has_value() ||
            l2.status != axiom::StatusCode::Ok || !l2.value.has_value() ||
            l3.status != axiom::StatusCode::Ok || !l3.value.has_value() ||
            r0.status != axiom::StatusCode::Ok || !r0.value.has_value() ||
            r1.status != axiom::StatusCode::Ok || !r1.value.has_value() ||
            r2.status != axiom::StatusCode::Ok || !r2.value.has_value() ||
            r3.status != axiom::StatusCode::Ok || !r3.value.has_value()) {
            std::cerr << "failed to create geometry for disconnected shell test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto a0 = txn.create_vertex({0.0, 6.0, 0.0});
        auto a1 = txn.create_vertex({1.0, 6.0, 0.0});
        auto a2 = txn.create_vertex({1.0, 7.0, 0.0});
        auto a3 = txn.create_vertex({0.0, 7.0, 0.0});
        auto e0 = txn.create_edge(*l0.value, *a0.value, *a1.value);
        auto e1 = txn.create_edge(*l1.value, *a1.value, *a2.value);
        auto e2 = txn.create_edge(*l2.value, *a2.value, *a3.value);
        auto e3 = txn.create_edge(*l3.value, *a3.value, *a0.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        auto c1 = txn.create_coedge(*e1.value, false);
        auto c2 = txn.create_coedge(*e2.value, false);
        auto c3 = txn.create_coedge(*e3.value, false);
        const std::array<axiom::CoedgeId, 4> co_a {{ *c0.value, *c1.value, *c2.value, *c3.value }};
        auto loop_a = txn.create_loop(co_a);
        auto f_a = txn.create_face(*plane.value, *loop_a.value, {});

        auto b0 = txn.create_vertex({3.0, 6.0, 0.0});
        auto b1 = txn.create_vertex({4.0, 6.0, 0.0});
        auto b2 = txn.create_vertex({4.0, 7.0, 0.0});
        auto b3 = txn.create_vertex({3.0, 7.0, 0.0});
        auto f0e = txn.create_edge(*r0.value, *b0.value, *b1.value);
        auto f1e = txn.create_edge(*r1.value, *b1.value, *b2.value);
        auto f2e = txn.create_edge(*r2.value, *b2.value, *b3.value);
        auto f3e = txn.create_edge(*r3.value, *b3.value, *b0.value);
        auto d0 = txn.create_coedge(*f0e.value, false);
        auto d1 = txn.create_coedge(*f1e.value, false);
        auto d2 = txn.create_coedge(*f2e.value, false);
        auto d3 = txn.create_coedge(*f3e.value, false);
        const std::array<axiom::CoedgeId, 4> co_b {{ *d0.value, *d1.value, *d2.value, *d3.value }};
        auto loop_b = txn.create_loop(co_b);
        auto f_b = txn.create_face(*plane.value, *loop_b.value, {});

        if (f_a.status != axiom::StatusCode::Ok || f_b.status != axiom::StatusCode::Ok ||
            !f_a.value.has_value() || !f_b.value.has_value()) {
            std::cerr << "failed to create faces for disconnected shell test\n";
            return 1;
        }
        auto shell = txn.create_shell(std::array<axiom::FaceId, 2>{*f_a.value, *f_b.value});
        if (shell.status != axiom::StatusCode::Ok || !shell.value.has_value()) {
            std::cerr << "failed to create shell for disconnected shell test\n";
            return 1;
        }
        auto shell_valid = kernel.topology().validate().validate_shell(*shell.value);
        if (shell_valid.status != axiom::StatusCode::Ok) {
            std::cerr << "expected validate_shell ok (disconnected reported as warning)\n";
            return 1;
        }
        auto shell_diag = kernel.diagnostics().get(shell_valid.diagnostic_id);
        if (shell_diag.status != axiom::StatusCode::Ok || !shell_diag.value.has_value() ||
            !has_issue_code(*shell_diag.value, axiom::diag_codes::kTopoShellDisconnected)) {
            std::cerr << "expected disconnected shell warning as kTopoShellDisconnected\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for disconnected shell test\n";
            return 1;
        }
    }

    auto plane0 = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    auto plane1 = kernel.surfaces().make_plane({0.0, 0.0, 1.0}, {0.0, 0.0, 1.0});
    auto line_x = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    auto line_y = kernel.curves().make_line({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    auto line_neg_x = kernel.curves().make_line({1.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
    auto line_neg_y = kernel.curves().make_line({0.0, 1.0, 0.0}, {0.0, -1.0, 0.0});
    if (plane0.status != axiom::StatusCode::Ok || plane1.status != axiom::StatusCode::Ok ||
        line_x.status != axiom::StatusCode::Ok || line_y.status != axiom::StatusCode::Ok ||
        line_neg_x.status != axiom::StatusCode::Ok || line_neg_y.status != axiom::StatusCode::Ok ||
        !plane0.value.has_value() || !plane1.value.has_value() ||
        !line_x.value.has_value() || !line_y.value.has_value() ||
        !line_neg_x.value.has_value() || !line_neg_y.value.has_value()) {
        std::cerr << "failed to create topology prerequisites\n";
        return 1;
    }

    auto txn = kernel.topology().begin_transaction();
    auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
    auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
    auto v2 = txn.create_vertex({1.0, 1.0, 0.0});
    auto v3 = txn.create_vertex({0.0, 1.0, 0.0});
    if (v0.status != axiom::StatusCode::Ok || v1.status != axiom::StatusCode::Ok ||
        v2.status != axiom::StatusCode::Ok || v3.status != axiom::StatusCode::Ok ||
        !v0.value.has_value() || !v1.value.has_value() ||
        !v2.value.has_value() || !v3.value.has_value()) {
        std::cerr << "failed to create vertices\n";
        return 1;
    }

    auto e0 = txn.create_edge(*line_x.value, *v0.value, *v1.value);
    auto e1 = txn.create_edge(*line_y.value, *v1.value, *v2.value);
    auto e2 = txn.create_edge(*line_neg_x.value, *v2.value, *v3.value);
    auto e3 = txn.create_edge(*line_neg_y.value, *v3.value, *v0.value);
    if (e0.status != axiom::StatusCode::Ok || e1.status != axiom::StatusCode::Ok ||
        e2.status != axiom::StatusCode::Ok || e3.status != axiom::StatusCode::Ok ||
        !e0.value.has_value() || !e1.value.has_value() ||
        !e2.value.has_value() || !e3.value.has_value()) {
        std::cerr << "failed to create edges\n";
        return 1;
    }

    auto bad_edge = txn.create_edge(*line_x.value, *v0.value, *v0.value);
    if (bad_edge.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected invalid input for degenerate edge\n";
        return 1;
    }

    auto c0 = txn.create_coedge(*e0.value, false);
    auto c1 = txn.create_coedge(*e1.value, false);
    auto c2 = txn.create_coedge(*e2.value, false);
    auto c3 = txn.create_coedge(*e3.value, false);
    if (c0.status != axiom::StatusCode::Ok || c1.status != axiom::StatusCode::Ok ||
        c2.status != axiom::StatusCode::Ok || c3.status != axiom::StatusCode::Ok ||
        !c0.value.has_value() || !c1.value.has_value() ||
        !c2.value.has_value() || !c3.value.has_value()) {
        std::cerr << "failed to create coedges\n";
        return 1;
    }

    const std::array<axiom::CoedgeId, 4> bad_coedges {*c0.value, *c2.value, *c1.value, *c3.value};
    auto bad_loop = txn.create_loop(bad_coedges);
    if (bad_loop.status != axiom::StatusCode::InvalidTopology) {
        std::cerr << "expected invalid topology for disconnected loop order\n";
        return 1;
    }

    const std::array<axiom::CoedgeId, 4> coedges {*c0.value, *c1.value, *c2.value, *c3.value};
    auto loop = txn.create_loop(coedges);
    if (loop.status != axiom::StatusCode::Ok || !loop.value.has_value()) {
        std::cerr << "failed to create loop\n";
        return 1;
    }

    auto face = txn.create_face(*plane0.value, *loop.value, {});
    if (face.status != axiom::StatusCode::Ok || !face.value.has_value()) {
        std::cerr << "failed to create face\n";
        return 1;
    }

    auto shell = txn.create_shell(std::array<axiom::FaceId, 1> {*face.value});
    if (shell.status != axiom::StatusCode::Ok || !shell.value.has_value()) {
        std::cerr << "failed to create shell\n";
        return 1;
    }

    auto body = txn.create_body(std::array<axiom::ShellId, 1> {*shell.value});
    if (body.status != axiom::StatusCode::Ok || !body.value.has_value()) {
        std::cerr << "failed to create body\n";
        return 1;
    }

    auto shell_fail = txn.create_shell({});
    if (shell_fail.status == axiom::StatusCode::Ok) {
        std::cerr << "expected shell creation failure for empty face set\n";
        return 1;
    }

    auto txn_active = txn.is_active();
    auto txn_can_commit = txn.can_commit();
    auto txn_preview_version = txn.preview_commit_version();
    auto txn_created_vertex_count = txn.created_vertex_count();
    auto txn_created_edge_count = txn.created_edge_count();
    auto txn_created_coedge_count = txn.created_coedge_count();
    auto txn_created_loop_count = txn.created_loop_count();
    auto txn_created_face_count = txn.created_face_count();
    auto txn_created_shell_count = txn.created_shell_count();
    auto txn_created_body_count = txn.created_body_count();
    auto txn_created_total = txn.created_entity_count_total();
    auto txn_touched_face_count = txn.touched_face_count();
    auto txn_touched_shell_count = txn.touched_shell_count();
    auto txn_touched_body_count = txn.touched_body_count();
    auto txn_write_ops = txn.write_operation_count();
    auto txn_iso = txn.effective_isolation_level();
    auto txn_has_created_v0 = txn.has_created_vertex(*v0.value);
    auto txn_has_created_e0 = txn.has_created_edge(*e0.value);
    auto txn_has_created_c0 = txn.has_created_coedge(*c0.value);
    auto txn_has_created_loop = txn.has_created_loop(*loop.value);
    auto txn_has_created_face = txn.has_created_face(*face.value);
    auto txn_has_created_shell = txn.has_created_shell(*shell.value);
    auto txn_has_created_body = txn.has_created_body(*body.value);
    auto txn_has_snapshot_face = txn.has_snapshot_face(*face.value);
    auto txn_has_snapshot_shell = txn.has_snapshot_shell(*shell.value);
    auto txn_has_snapshot_body = txn.has_snapshot_body(*body.value);
    auto txn_created_vertices = txn.created_vertices();
    auto txn_created_edges = txn.created_edges();
    auto txn_created_coedges_list = txn.created_coedges();
    auto txn_created_loops_list = txn.created_loops();
    auto txn_created_faces = txn.created_faces();
    auto txn_created_shells = txn.created_shells();
    auto txn_created_bodies = txn.created_bodies();
    if (txn_active.status != axiom::StatusCode::Ok || !txn_active.value.has_value() || !*txn_active.value ||
        txn_can_commit.status != axiom::StatusCode::Ok || !txn_can_commit.value.has_value() || !*txn_can_commit.value ||
        txn_preview_version.status != axiom::StatusCode::Ok || !txn_preview_version.value.has_value() ||
        txn_created_vertex_count.status != axiom::StatusCode::Ok || !txn_created_vertex_count.value.has_value() || *txn_created_vertex_count.value != 4 ||
        txn_created_edge_count.status != axiom::StatusCode::Ok || !txn_created_edge_count.value.has_value() || *txn_created_edge_count.value != 4 ||
        txn_created_coedge_count.status != axiom::StatusCode::Ok || !txn_created_coedge_count.value.has_value() || *txn_created_coedge_count.value != 4 ||
        txn_created_loop_count.status != axiom::StatusCode::Ok || !txn_created_loop_count.value.has_value() || *txn_created_loop_count.value != 1 ||
        txn_created_face_count.status != axiom::StatusCode::Ok || !txn_created_face_count.value.has_value() || *txn_created_face_count.value != 1 ||
        txn_created_shell_count.status != axiom::StatusCode::Ok || !txn_created_shell_count.value.has_value() || *txn_created_shell_count.value != 1 ||
        txn_created_body_count.status != axiom::StatusCode::Ok || !txn_created_body_count.value.has_value() || *txn_created_body_count.value != 1 ||
        txn_created_total.status != axiom::StatusCode::Ok || !txn_created_total.value.has_value() || *txn_created_total.value != 16 ||
        txn_touched_face_count.status != axiom::StatusCode::Ok || !txn_touched_face_count.value.has_value() || *txn_touched_face_count.value != 0 ||
        txn_touched_shell_count.status != axiom::StatusCode::Ok || !txn_touched_shell_count.value.has_value() || *txn_touched_shell_count.value != 0 ||
        txn_touched_body_count.status != axiom::StatusCode::Ok || !txn_touched_body_count.value.has_value() || *txn_touched_body_count.value != 0 ||
        txn_write_ops.status != axiom::StatusCode::Ok || !txn_write_ops.value.has_value() || *txn_write_ops.value != 16 ||
        txn_iso.status != axiom::StatusCode::Ok || !txn_iso.value.has_value() ||
        *txn_iso.value != axiom::TopologyIsolationLevel::SnapshotSerializable ||
        txn_has_created_v0.status != axiom::StatusCode::Ok || !txn_has_created_v0.value.has_value() || !*txn_has_created_v0.value ||
        txn_has_created_e0.status != axiom::StatusCode::Ok || !txn_has_created_e0.value.has_value() || !*txn_has_created_e0.value ||
        txn_has_created_c0.status != axiom::StatusCode::Ok || !txn_has_created_c0.value.has_value() || !*txn_has_created_c0.value ||
        txn_has_created_loop.status != axiom::StatusCode::Ok || !txn_has_created_loop.value.has_value() || !*txn_has_created_loop.value ||
        txn_has_created_face.status != axiom::StatusCode::Ok || !txn_has_created_face.value.has_value() || !*txn_has_created_face.value ||
        txn_has_created_shell.status != axiom::StatusCode::Ok || !txn_has_created_shell.value.has_value() || !*txn_has_created_shell.value ||
        txn_has_created_body.status != axiom::StatusCode::Ok || !txn_has_created_body.value.has_value() || !*txn_has_created_body.value ||
        txn_has_snapshot_face.status != axiom::StatusCode::Ok || !txn_has_snapshot_face.value.has_value() || *txn_has_snapshot_face.value ||
        txn_has_snapshot_shell.status != axiom::StatusCode::Ok || !txn_has_snapshot_shell.value.has_value() || *txn_has_snapshot_shell.value ||
        txn_has_snapshot_body.status != axiom::StatusCode::Ok || !txn_has_snapshot_body.value.has_value() || *txn_has_snapshot_body.value ||
        txn_created_vertices.status != axiom::StatusCode::Ok || !txn_created_vertices.value.has_value() || txn_created_vertices.value->size() != 4 ||
        txn_created_edges.status != axiom::StatusCode::Ok || !txn_created_edges.value.has_value() || txn_created_edges.value->size() != 4 ||
        txn_created_coedges_list.status != axiom::StatusCode::Ok || !txn_created_coedges_list.value.has_value() ||
        txn_created_coedges_list.value->size() != 4 ||
        txn_created_coedges_list.value->at(0).value != c0.value->value ||
        txn_created_loops_list.status != axiom::StatusCode::Ok || !txn_created_loops_list.value.has_value() ||
        txn_created_loops_list.value->size() != 1 ||
        txn_created_loops_list.value->front().value != loop.value->value ||
        txn_created_faces.status != axiom::StatusCode::Ok || !txn_created_faces.value.has_value() || txn_created_faces.value->size() != 1 ||
        txn_created_shells.status != axiom::StatusCode::Ok || !txn_created_shells.value.has_value() || txn_created_shells.value->size() != 1 ||
        txn_created_bodies.status != axiom::StatusCode::Ok || !txn_created_bodies.value.has_value() || txn_created_bodies.value->size() != 1) {
        std::cerr << "unexpected topology transaction observability behavior\n";
        return 1;
    }

    auto commit = txn.commit();
    if (commit.status != axiom::StatusCode::Ok || !commit.value.has_value()) {
        std::cerr << "commit failed\n";
        return 1;
    }

    auto edge_valid = kernel.topology().validate().validate_edge(*e0.value);
    auto face_valid = kernel.topology().validate().validate_face(*face.value);
    auto shell_valid = kernel.topology().validate().validate_shell(*shell.value);
    auto body_valid = kernel.topology().validate().validate_body(*body.value);
    if (edge_valid.status != axiom::StatusCode::Ok || face_valid.status != axiom::StatusCode::Ok ||
        shell_valid.status != axiom::StatusCode::Ok || body_valid.status != axiom::StatusCode::Ok) {
        std::cerr << "expected committed topology to validate\n";
        return 1;
    }

    auto vertices = kernel.topology().query().vertices_of_edge(*e0.value);
    auto coedges_of_edge = kernel.topology().query().coedges_of_edge(*e0.value);
    auto loops_of_edge = kernel.topology().query().loops_of_edge(*e0.value);
    auto faces_of_edge = kernel.topology().query().faces_of_edge(*e0.value);
    auto shells_of_edge = kernel.topology().query().shells_of_edge(*e0.value);
    auto edges = kernel.topology().query().edges_of_loop(*loop.value);
    auto loop_vertices = kernel.topology().query().vertices_of_loop(*loop.value);
    auto loops = kernel.topology().query().loops_of_face(*face.value);
    auto surface = kernel.topology().query().surface_of_face(*face.value);
    auto shells_of_face = kernel.topology().query().shells_of_face(*face.value);
    auto bodies_of_face = kernel.topology().query().bodies_of_face(*face.value);
    auto source_faces_of_face = kernel.topology().query().source_faces_of_face(*face.value);
    auto faces = kernel.topology().query().faces_of_shell(*shell.value);
    auto bodies_of_shell = kernel.topology().query().bodies_of_shell(*shell.value);
    auto source_shells_of_shell = kernel.topology().query().source_shells_of_shell(*shell.value);
    auto source_faces_of_shell = kernel.topology().query().source_faces_of_shell(*shell.value);
    auto shells = kernel.topology().query().shells_of_body(*body.value);
    auto source_bodies = kernel.topology().query().source_bodies_of_body(*body.value);
    auto source_shells = kernel.topology().query().source_shells_of_body(*body.value);
    auto source_faces = kernel.topology().query().source_faces_of_body(*body.value);
    auto shell_summary = kernel.topology().query().summary_of_shell(*shell.value);
    auto body_summary = kernel.topology().query().summary_of_body(*body.value);
    auto edge_count_of_loop = kernel.topology().query().edge_count_of_loop(*loop.value);
    auto loop_count_of_face = kernel.topology().query().loop_count_of_face(*face.value);
    auto face_count_of_shell = kernel.topology().query().face_count_of_shell(*shell.value);
    auto shell_count_of_body = kernel.topology().query().shell_count_of_body(*body.value);
    auto coedge_count_of_edge = kernel.topology().query().coedge_count_of_edge(*e0.value);
    auto owner_count_of_edge = kernel.topology().query().owner_count_of_edge(*e0.value);
    auto owner_count_of_face = kernel.topology().query().owner_count_of_face(*face.value);
    auto owner_count_of_shell = kernel.topology().query().owner_count_of_shell(*shell.value);
    auto has_vertex = kernel.topology().query().has_vertex(*v0.value);
    auto has_edge = kernel.topology().query().has_edge(*e0.value);
    auto has_loop = kernel.topology().query().has_loop(*loop.value);
    auto has_face = kernel.topology().query().has_face(*face.value);
    auto has_shell = kernel.topology().query().has_shell(*shell.value);
    auto has_body = kernel.topology().query().has_body(*body.value);
    auto is_edge_boundary = kernel.topology().query().is_edge_boundary(*e0.value);
    auto is_edge_non_manifold = kernel.topology().query().is_edge_non_manifold(*e0.value);
    auto is_face_orphan = kernel.topology().query().is_face_orphan(*face.value);
    auto is_shell_orphan = kernel.topology().query().is_shell_orphan(*shell.value);
    auto is_body_derived = kernel.topology().query().is_body_derived(*body.value);
    auto face_bbox = kernel.topology().query().bbox_of_face(*face.value);
    auto shell_bbox = kernel.topology().query().bbox_of_shell(*shell.value);
    auto body_topo_bbox = kernel.topology().query().bbox_of_body_from_topology(*body.value);
    auto faces_of_body = kernel.topology().query().faces_of_body(*body.value);
    auto loops_of_body = kernel.topology().query().loops_of_body(*body.value);
    auto edges_of_body = kernel.topology().query().edges_of_body(*body.value);
    auto vertices_of_body = kernel.topology().query().vertices_of_body(*body.value);
    auto face_count_of_body = kernel.topology().query().face_count_of_body(*body.value);
    auto loop_count_of_body = kernel.topology().query().loop_count_of_body(*body.value);
    auto edge_count_of_body = kernel.topology().query().edge_count_of_body(*body.value);
    auto vertex_count_of_body = kernel.topology().query().vertex_count_of_body(*body.value);
    auto body_has_face = kernel.topology().query().body_has_face(*body.value, *face.value);
    auto shell_has_face = kernel.topology().query().shell_has_face(*shell.value, *face.value);
    auto face_has_loop = kernel.topology().query().face_has_loop(*face.value, *loop.value);
    auto loop_has_edge = kernel.topology().query().loop_has_edge(*loop.value, *e0.value);
    auto edge_has_vertex = kernel.topology().query().edge_has_vertex(*e0.value, *v0.value);
    auto shared_face_count_of_body = kernel.topology().query().shared_face_count_of_body(*body.value);
    auto shared_edge_count_of_body = kernel.topology().query().shared_edge_count_of_body(*body.value);
    auto boundary_edge_count_of_body = kernel.topology().query().boundary_edge_count_of_body(*body.value);
    auto non_manifold_edge_count_of_body = kernel.topology().query().non_manifold_edge_count_of_body(*body.value);
    auto is_body_topology_empty = kernel.topology().query().is_body_topology_empty(*body.value);
    if (vertices.status != axiom::StatusCode::Ok || !vertices.value.has_value() ||
        vertices.value->at(0).value != v0.value->value || vertices.value->at(1).value != v1.value->value ||
        coedges_of_edge.status != axiom::StatusCode::Ok || !coedges_of_edge.value.has_value() || coedges_of_edge.value->size() != 1 ||
        coedges_of_edge.value->front().value != c0.value->value ||
        loops_of_edge.status != axiom::StatusCode::Ok || !loops_of_edge.value.has_value() || loops_of_edge.value->size() != 1 ||
        loops_of_edge.value->front().value != loop.value->value ||
        faces_of_edge.status != axiom::StatusCode::Ok || !faces_of_edge.value.has_value() || faces_of_edge.value->size() != 1 ||
        faces_of_edge.value->front().value != face.value->value ||
        shells_of_edge.status != axiom::StatusCode::Ok || !shells_of_edge.value.has_value() || shells_of_edge.value->size() != 1 ||
        shells_of_edge.value->front().value != shell.value->value ||
        edges.status != axiom::StatusCode::Ok || !edges.value.has_value() || edges.value->size() != 4 ||
        loop_vertices.status != axiom::StatusCode::Ok || !loop_vertices.value.has_value() ||
        loop_vertices.value->size() != 4 ||
        loop_vertices.value->at(0).value != v0.value->value ||
        loop_vertices.value->at(1).value != v1.value->value ||
        loop_vertices.value->at(2).value != v2.value->value ||
        loop_vertices.value->at(3).value != v3.value->value ||
        loops.status != axiom::StatusCode::Ok || !loops.value.has_value() || loops.value->size() != 1 ||
        surface.status != axiom::StatusCode::Ok || !surface.value.has_value() || surface.value->value != plane0.value->value ||
        shells_of_face.status != axiom::StatusCode::Ok || !shells_of_face.value.has_value() || shells_of_face.value->size() != 1 ||
        shells_of_face.value->front().value != shell.value->value ||
        bodies_of_face.status != axiom::StatusCode::Ok || !bodies_of_face.value.has_value() || bodies_of_face.value->size() != 1 ||
        bodies_of_face.value->front().value != body.value->value ||
        source_faces_of_face.status != axiom::StatusCode::Ok || !source_faces_of_face.value.has_value() ||
        source_faces_of_face.value->size() != 1 || source_faces_of_face.value->front().value != face.value->value ||
        faces.status != axiom::StatusCode::Ok || !faces.value.has_value() || faces.value->size() != 1 ||
        bodies_of_shell.status != axiom::StatusCode::Ok || !bodies_of_shell.value.has_value() || bodies_of_shell.value->size() != 1 ||
        bodies_of_shell.value->front().value != body.value->value ||
        source_shells_of_shell.status != axiom::StatusCode::Ok || !source_shells_of_shell.value.has_value() ||
        !source_shells_of_shell.value->empty() ||
        source_faces_of_shell.status != axiom::StatusCode::Ok || !source_faces_of_shell.value.has_value() ||
        source_faces_of_shell.value->size() != 1 || source_faces_of_shell.value->front().value != face.value->value ||
        shells.status != axiom::StatusCode::Ok || !shells.value.has_value() || shells.value->size() != 1 ||
        source_bodies.status != axiom::StatusCode::Ok || !source_bodies.value.has_value() || !source_bodies.value->empty() ||
        source_shells.status != axiom::StatusCode::Ok || !source_shells.value.has_value() || source_shells.value->size() != 1 ||
        source_shells.value->front().value != shell.value->value ||
        source_faces.status != axiom::StatusCode::Ok || !source_faces.value.has_value() || source_faces.value->size() != 1 ||
        source_faces.value->front().value != face.value->value ||
        shell_summary.status != axiom::StatusCode::Ok || !shell_summary.value.has_value() ||
        shell_summary.value->shell_count != 1 || shell_summary.value->face_count != 1 ||
        body_summary.status != axiom::StatusCode::Ok || !body_summary.value.has_value() ||
        body_summary.value->shell_count != 1 || body_summary.value->face_count != 1 ||
        edge_count_of_loop.status != axiom::StatusCode::Ok || !edge_count_of_loop.value.has_value() || *edge_count_of_loop.value != 4 ||
        loop_count_of_face.status != axiom::StatusCode::Ok || !loop_count_of_face.value.has_value() || *loop_count_of_face.value != 1 ||
        face_count_of_shell.status != axiom::StatusCode::Ok || !face_count_of_shell.value.has_value() || *face_count_of_shell.value != 1 ||
        shell_count_of_body.status != axiom::StatusCode::Ok || !shell_count_of_body.value.has_value() || *shell_count_of_body.value != 1 ||
        coedge_count_of_edge.status != axiom::StatusCode::Ok || !coedge_count_of_edge.value.has_value() || *coedge_count_of_edge.value != 1 ||
        owner_count_of_edge.status != axiom::StatusCode::Ok || !owner_count_of_edge.value.has_value() || *owner_count_of_edge.value != 1 ||
        owner_count_of_face.status != axiom::StatusCode::Ok || !owner_count_of_face.value.has_value() || *owner_count_of_face.value != 1 ||
        owner_count_of_shell.status != axiom::StatusCode::Ok || !owner_count_of_shell.value.has_value() || *owner_count_of_shell.value != 1 ||
        has_vertex.status != axiom::StatusCode::Ok || !has_vertex.value.has_value() || !*has_vertex.value ||
        has_edge.status != axiom::StatusCode::Ok || !has_edge.value.has_value() || !*has_edge.value ||
        has_loop.status != axiom::StatusCode::Ok || !has_loop.value.has_value() || !*has_loop.value ||
        has_face.status != axiom::StatusCode::Ok || !has_face.value.has_value() || !*has_face.value ||
        has_shell.status != axiom::StatusCode::Ok || !has_shell.value.has_value() || !*has_shell.value ||
        has_body.status != axiom::StatusCode::Ok || !has_body.value.has_value() || !*has_body.value ||
        is_edge_boundary.status != axiom::StatusCode::Ok || !is_edge_boundary.value.has_value() || !*is_edge_boundary.value ||
        is_edge_non_manifold.status != axiom::StatusCode::Ok || !is_edge_non_manifold.value.has_value() || *is_edge_non_manifold.value ||
        is_face_orphan.status != axiom::StatusCode::Ok || !is_face_orphan.value.has_value() || *is_face_orphan.value ||
        is_shell_orphan.status != axiom::StatusCode::Ok || !is_shell_orphan.value.has_value() || *is_shell_orphan.value ||
        is_body_derived.status != axiom::StatusCode::Ok || !is_body_derived.value.has_value() || !*is_body_derived.value ||
        face_bbox.status != axiom::StatusCode::Ok || !face_bbox.value.has_value() || !face_bbox.value->is_valid ||
        shell_bbox.status != axiom::StatusCode::Ok || !shell_bbox.value.has_value() || !shell_bbox.value->is_valid ||
        body_topo_bbox.status != axiom::StatusCode::Ok || !body_topo_bbox.value.has_value() || !body_topo_bbox.value->is_valid ||
        faces_of_body.status != axiom::StatusCode::Ok || !faces_of_body.value.has_value() || faces_of_body.value->size() != 1 ||
        loops_of_body.status != axiom::StatusCode::Ok || !loops_of_body.value.has_value() || loops_of_body.value->size() != 1 ||
        edges_of_body.status != axiom::StatusCode::Ok || !edges_of_body.value.has_value() || edges_of_body.value->size() != 4 ||
        vertices_of_body.status != axiom::StatusCode::Ok || !vertices_of_body.value.has_value() || vertices_of_body.value->size() != 4 ||
        face_count_of_body.status != axiom::StatusCode::Ok || !face_count_of_body.value.has_value() || *face_count_of_body.value != 1 ||
        loop_count_of_body.status != axiom::StatusCode::Ok || !loop_count_of_body.value.has_value() || *loop_count_of_body.value != 1 ||
        edge_count_of_body.status != axiom::StatusCode::Ok || !edge_count_of_body.value.has_value() || *edge_count_of_body.value != 4 ||
        vertex_count_of_body.status != axiom::StatusCode::Ok || !vertex_count_of_body.value.has_value() || *vertex_count_of_body.value != 4 ||
        body_has_face.status != axiom::StatusCode::Ok || !body_has_face.value.has_value() || !*body_has_face.value ||
        shell_has_face.status != axiom::StatusCode::Ok || !shell_has_face.value.has_value() || !*shell_has_face.value ||
        face_has_loop.status != axiom::StatusCode::Ok || !face_has_loop.value.has_value() || !*face_has_loop.value ||
        loop_has_edge.status != axiom::StatusCode::Ok || !loop_has_edge.value.has_value() || !*loop_has_edge.value ||
        edge_has_vertex.status != axiom::StatusCode::Ok || !edge_has_vertex.value.has_value() || !*edge_has_vertex.value ||
        shared_face_count_of_body.status != axiom::StatusCode::Ok || !shared_face_count_of_body.value.has_value() || *shared_face_count_of_body.value != 0 ||
        shared_edge_count_of_body.status != axiom::StatusCode::Ok || !shared_edge_count_of_body.value.has_value() || *shared_edge_count_of_body.value != 0 ||
        boundary_edge_count_of_body.status != axiom::StatusCode::Ok || !boundary_edge_count_of_body.value.has_value() || *boundary_edge_count_of_body.value != 4 ||
        non_manifold_edge_count_of_body.status != axiom::StatusCode::Ok || !non_manifold_edge_count_of_body.value.has_value() || *non_manifold_edge_count_of_body.value != 0 ||
        is_body_topology_empty.status != axiom::StatusCode::Ok || !is_body_topology_empty.value.has_value() || *is_body_topology_empty.value) {
        std::cerr << "query service returned unexpected topology data\n";
        return 1;
    }

    auto vtx_valid = kernel.topology().validate().validate_vertex(*v0.value);
    auto coedge_valid = kernel.topology().validate().validate_coedge(*c0.value);
    auto loop_valid = kernel.topology().validate().validate_loop(*loop.value);
    auto face_source_valid = kernel.topology().validate().validate_face_sources(*face.value);
    auto shell_source_valid = kernel.topology().validate().validate_shell_sources(*shell.value);
    auto body_source_valid = kernel.topology().validate().validate_body_sources(*body.value);
    auto body_bbox_valid = kernel.topology().validate().validate_body_bbox(*body.value);
    auto indices_valid = kernel.topology().validate().validate_indices_consistency();
    auto body_topology_indices_valid =
        kernel.topology().validate().validate_body_topology_indices(*body.value);
    std::array<axiom::FaceId, 1> face_ids {*face.value};
    std::array<axiom::ShellId, 1> shell_ids {*shell.value};
    std::array<axiom::BodyId, 1> body_ids {*body.value};
    auto face_many_valid = kernel.topology().validate().validate_face_many(face_ids);
    auto shell_many_valid = kernel.topology().validate().validate_shell_many(shell_ids);
    auto body_many_valid = kernel.topology().validate().validate_body_many(body_ids);
    auto is_face_valid = kernel.topology().validate().is_face_valid(*face.value);
    auto is_shell_valid = kernel.topology().validate().is_shell_valid(*shell.value);
    auto is_body_valid = kernel.topology().validate().is_body_valid(*body.value);
    auto invalid_faces = kernel.topology().validate().count_invalid_faces(face_ids);
    auto invalid_shells = kernel.topology().validate().count_invalid_shells(shell_ids);
    auto invalid_bodies = kernel.topology().validate().count_invalid_bodies(body_ids);
    auto first_invalid_face = kernel.topology().validate().first_invalid_face(face_ids);
    auto first_invalid_shell = kernel.topology().validate().first_invalid_shell(shell_ids);
    auto first_invalid_body = kernel.topology().validate().first_invalid_body(body_ids);
    if (vtx_valid.status != axiom::StatusCode::Ok || coedge_valid.status != axiom::StatusCode::Ok ||
        loop_valid.status != axiom::StatusCode::Ok || face_source_valid.status != axiom::StatusCode::Ok ||
        shell_source_valid.status != axiom::StatusCode::Ok || body_source_valid.status != axiom::StatusCode::Ok ||
        body_bbox_valid.status != axiom::StatusCode::Ok || indices_valid.status != axiom::StatusCode::Ok ||
        body_topology_indices_valid.status != axiom::StatusCode::Ok ||
        face_many_valid.status != axiom::StatusCode::Ok || shell_many_valid.status != axiom::StatusCode::Ok ||
        body_many_valid.status != axiom::StatusCode::Ok ||
        is_face_valid.status != axiom::StatusCode::Ok || !is_face_valid.value.has_value() || !*is_face_valid.value ||
        is_shell_valid.status != axiom::StatusCode::Ok || !is_shell_valid.value.has_value() || !*is_shell_valid.value ||
        is_body_valid.status != axiom::StatusCode::Ok || !is_body_valid.value.has_value() || !*is_body_valid.value ||
        invalid_faces.status != axiom::StatusCode::Ok || !invalid_faces.value.has_value() || *invalid_faces.value != 0 ||
        invalid_shells.status != axiom::StatusCode::Ok || !invalid_shells.value.has_value() || *invalid_shells.value != 0 ||
        invalid_bodies.status != axiom::StatusCode::Ok || !invalid_bodies.value.has_value() || *invalid_bodies.value != 0 ||
        first_invalid_face.status != axiom::StatusCode::OperationFailed ||
        first_invalid_shell.status != axiom::StatusCode::OperationFailed ||
        first_invalid_body.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "extended topology validation checks failed\n";
        return 1;
    }

    auto modify_txn = kernel.topology().begin_transaction();
    auto replace_surface = modify_txn.replace_surface(*face.value, *plane1.value);
    if (replace_surface.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to replace face surface in transaction\n";
        return 1;
    }
    auto rep_cnt = modify_txn.replaced_surface_count();
    if (rep_cnt.status != axiom::StatusCode::Ok || !rep_cnt.value.has_value() ||
        *rep_cnt.value != 1) {
        std::cerr << "expected replaced_surface_count 1 after replace_surface\n";
        return 1;
    }

    auto changed_surface = kernel.topology().query().surface_of_face(*face.value);
    if (changed_surface.status != axiom::StatusCode::Ok || !changed_surface.value.has_value() ||
        changed_surface.value->value != plane1.value->value) {
        std::cerr << "surface replacement did not take effect before rollback\n";
        return 1;
    }

    auto rollback = modify_txn.rollback();
    if (rollback.status != axiom::StatusCode::Ok) {
        std::cerr << "rollback failed\n";
        return 1;
    }
    auto rep_after_rb = modify_txn.replaced_surface_count();
    if (rep_after_rb.status != axiom::StatusCode::Ok || !rep_after_rb.value.has_value() ||
        *rep_after_rb.value != 0) {
        std::cerr << "expected replaced_surface_count 0 after rollback\n";
        return 1;
    }
    auto modify_txn_active = modify_txn.is_active();
    if (modify_txn_active.status != axiom::StatusCode::Ok || !modify_txn_active.value.has_value() || *modify_txn_active.value) {
        std::cerr << "transaction should be inactive after rollback\n";
        return 1;
    }

    auto restored_surface = kernel.topology().query().surface_of_face(*face.value);
    if (restored_surface.status != axiom::StatusCode::Ok || !restored_surface.value.has_value() ||
        restored_surface.value->value != plane0.value->value) {
        std::cerr << "rollback did not restore original face surface\n";
        return 1;
    }

    auto replace_face_feature = kernel.modify().replace_face(*body.value, *face.value, *plane1.value);
    auto repair_feature = kernel.repair().auto_repair(*body.value, axiom::RepairMode::Safe);
    if (replace_face_feature.status != axiom::StatusCode::Ok || !replace_face_feature.value.has_value() ||
        repair_feature.status != axiom::StatusCode::Ok || !repair_feature.value.has_value()) {
        std::cerr << "expected feature operations on topology body to succeed\n";
        return 1;
    }

    auto replace_feature_shells = kernel.topology().query().source_shells_of_body(replace_face_feature.value->output);
    auto repair_feature_shells = kernel.topology().query().source_shells_of_body(repair_feature.value->output);
    auto replace_feature_owned_shells = kernel.topology().query().shells_of_body(replace_face_feature.value->output);
    auto repair_feature_owned_shells = kernel.topology().query().shells_of_body(repair_feature.value->output);
    auto replace_feature_owned_faces = replace_feature_owned_shells.status == axiom::StatusCode::Ok &&
                                               replace_feature_owned_shells.value.has_value() &&
                                               replace_feature_owned_shells.value->size() == 1
                                           ? kernel.topology().query().faces_of_shell(replace_feature_owned_shells.value->front())
                                           : axiom::Result<std::vector<axiom::FaceId>> {};
    auto repair_feature_owned_faces = repair_feature_owned_shells.status == axiom::StatusCode::Ok &&
                                              repair_feature_owned_shells.value.has_value() &&
                                              repair_feature_owned_shells.value->size() == 1
                                          ? kernel.topology().query().faces_of_shell(repair_feature_owned_shells.value->front())
                                          : axiom::Result<std::vector<axiom::FaceId>> {};
    auto replace_feature_strict_valid = kernel.validate().validate_topology(replace_face_feature.value->output, axiom::ValidationMode::Strict);
    auto repair_feature_strict_valid = kernel.validate().validate_topology(repair_feature.value->output, axiom::ValidationMode::Strict);
    auto replace_feature_topo_valid = kernel.topology().validate().validate_body(replace_face_feature.value->output);
    auto repair_feature_topo_valid = kernel.topology().validate().validate_body(repair_feature.value->output);
    if (replace_feature_shells.status != axiom::StatusCode::Ok || !replace_feature_shells.value.has_value() ||
        replace_feature_shells.value->size() != 1 || replace_feature_shells.value->front().value != shell.value->value ||
        repair_feature_shells.status != axiom::StatusCode::Ok || !repair_feature_shells.value.has_value() ||
        repair_feature_shells.value->size() != 1 || repair_feature_shells.value->front().value != shell.value->value ||
        replace_feature_owned_shells.status != axiom::StatusCode::Ok || !replace_feature_owned_shells.value.has_value() ||
        repair_feature_owned_shells.status != axiom::StatusCode::Ok || !repair_feature_owned_shells.value.has_value() ||
        replace_feature_owned_faces.status != axiom::StatusCode::Ok || !replace_feature_owned_faces.value.has_value() ||
        repair_feature_owned_faces.status != axiom::StatusCode::Ok || !repair_feature_owned_faces.value.has_value() ||
        replace_feature_owned_shells.value->size() != 1 ||
        repair_feature_owned_shells.value->size() != 1 ||
        replace_feature_owned_faces.value->size() != 6 ||
        repair_feature_owned_faces.value->size() != 6 ||
        replace_feature_strict_valid.status != axiom::StatusCode::Ok ||
        repair_feature_strict_valid.status != axiom::StatusCode::Ok ||
        replace_feature_topo_valid.status != axiom::StatusCode::Ok ||
        repair_feature_topo_valid.status != axiom::StatusCode::Ok) {
        std::cerr << "derived body source shells are unexpected\n";
        return 1;
    }

    const std::array<axiom::FaceId, 1> sew_faces {*face.value};
    auto sewn_feature = kernel.repair().sew_faces(sew_faces, 1e-3, axiom::RepairMode::Safe);
    if (sewn_feature.status != axiom::StatusCode::Ok || !sewn_feature.value.has_value()) {
        std::cerr << "expected sew_faces on topology face to succeed\n";
        return 1;
    }
    auto sewn_feature_shells = kernel.topology().query().source_shells_of_body(sewn_feature.value->output);
    auto sewn_feature_faces = kernel.topology().query().source_faces_of_body(sewn_feature.value->output);
    auto sewn_feature_owned_shells = kernel.topology().query().shells_of_body(sewn_feature.value->output);
    auto sewn_feature_owned_faces = sewn_feature_owned_shells.status == axiom::StatusCode::Ok &&
                                            sewn_feature_owned_shells.value.has_value() &&
                                            sewn_feature_owned_shells.value->size() == 1
                                        ? kernel.topology().query().faces_of_shell(sewn_feature_owned_shells.value->front())
                                        : axiom::Result<std::vector<axiom::FaceId>> {};
    auto sewn_feature_strict_valid = kernel.validate().validate_topology(sewn_feature.value->output, axiom::ValidationMode::Strict);
    auto sewn_feature_topo_valid = kernel.topology().validate().validate_body(sewn_feature.value->output);
    if (sewn_feature_shells.status != axiom::StatusCode::Ok || !sewn_feature_shells.value.has_value() ||
        sewn_feature_shells.value->size() != 1 || sewn_feature_shells.value->front().value != shell.value->value ||
        sewn_feature_faces.status != axiom::StatusCode::Ok || !sewn_feature_faces.value.has_value() ||
        sewn_feature_faces.value->size() != 1 || sewn_feature_faces.value->front().value != face.value->value ||
        sewn_feature_owned_shells.status != axiom::StatusCode::Ok || !sewn_feature_owned_shells.value.has_value() ||
        sewn_feature_owned_faces.status != axiom::StatusCode::Ok || !sewn_feature_owned_faces.value.has_value() ||
        sewn_feature_owned_shells.value->size() != 1 ||
        sewn_feature_owned_faces.value->size() != 6 ||
        sewn_feature_strict_valid.status != axiom::StatusCode::Ok ||
        sewn_feature_topo_valid.status != axiom::StatusCode::Ok) {
        std::cerr << "sewn body topology/provenance is unexpected\n";
        return 1;
    }

    auto derived_delete_txn = kernel.topology().begin_transaction();
    auto delete_derived_face = derived_delete_txn.delete_face(sewn_feature_owned_faces.value->front());
    if (delete_derived_face.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to delete owned face from sewn derived body\n";
        return 1;
    }
    auto broken_sewn_valid = kernel.validate().validate_topology(sewn_feature.value->output, axiom::ValidationMode::Standard);
    if (broken_sewn_valid.status == axiom::StatusCode::Ok) {
        std::cerr << "expected validate_topology to fail after breaking materialized sewn shell\n";
        return 1;
    }
    auto derived_delete_rollback = derived_delete_txn.rollback();
    if (derived_delete_rollback.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to rollback derived sewn face deletion\n";
        return 1;
    }
    auto restored_sewn_valid = kernel.validate().validate_topology(sewn_feature.value->output, axiom::ValidationMode::Standard);
    if (restored_sewn_valid.status != axiom::StatusCode::Ok) {
        std::cerr << "expected sewn body validation to recover after rollback\n";
        return 1;
    }

    auto topo_txn_2 = kernel.topology().begin_transaction();
    auto shell2 = topo_txn_2.create_shell(std::array<axiom::FaceId, 1> {*face.value});
    if (shell2.status != axiom::StatusCode::Ok || !shell2.value.has_value()) {
        std::cerr << "failed to create second shell\n";
        return 1;
    }
    auto body2 = topo_txn_2.create_body(std::array<axiom::ShellId, 1> {*shell2.value});
    if (body2.status != axiom::StatusCode::Ok || !body2.value.has_value()) {
        std::cerr << "failed to create second body\n";
        return 1;
    }
    auto commit2 = topo_txn_2.commit();
    if (commit2.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to commit second topology body\n";
        return 1;
    }

    auto replace_face_after_shared_shell = kernel.modify().replace_face(*body.value, *face.value, *plane1.value);
    if (replace_face_after_shared_shell.status != axiom::StatusCode::Ok || !replace_face_after_shared_shell.value.has_value()) {
        std::cerr << "replace_face should succeed when source face is shared by multiple shells\n";
        return 1;
    }
    auto replace_after_shared_source_shells = kernel.topology().query().source_shells_of_body(replace_face_after_shared_shell.value->output);
    if (replace_after_shared_source_shells.status != axiom::StatusCode::Ok || !replace_after_shared_source_shells.value.has_value() ||
        replace_after_shared_source_shells.value->size() != 1 ||
        replace_after_shared_source_shells.value->front().value != shell.value->value) {
        std::cerr << "replace_face should prefer source shells owned by source body in shared-face scenario\n";
        return 1;
    }

    auto topo_boolean = kernel.booleans().run(axiom::BooleanOp::Union, *body.value, *body2.value, {});
    if (topo_boolean.status != axiom::StatusCode::Ok || !topo_boolean.value.has_value()) {
        std::cerr << "boolean on topology bodies failed\n";
        return 1;
    }
    auto topo_boolean_shells = kernel.topology().query().source_shells_of_body(topo_boolean.value->output);
    auto topo_boolean_faces = kernel.topology().query().source_faces_of_body(topo_boolean.value->output);
    auto topo_boolean_owned_shells = kernel.topology().query().shells_of_body(topo_boolean.value->output);
    auto topo_boolean_owned_faces = topo_boolean_owned_shells.status == axiom::StatusCode::Ok &&
                                            topo_boolean_owned_shells.value.has_value() &&
                                            topo_boolean_owned_shells.value->size() == 1
                                        ? kernel.topology().query().faces_of_shell(topo_boolean_owned_shells.value->front())
                                        : axiom::Result<std::vector<axiom::FaceId>> {};
    auto topo_boolean_strict_valid = kernel.validate().validate_topology(topo_boolean.value->output, axiom::ValidationMode::Strict);
    auto topo_boolean_topo_valid = kernel.topology().validate().validate_body(topo_boolean.value->output);
    if (topo_boolean_shells.status != axiom::StatusCode::Ok || !topo_boolean_shells.value.has_value() ||
        topo_boolean_shells.value->size() != 2 ||
        topo_boolean_faces.status != axiom::StatusCode::Ok || !topo_boolean_faces.value.has_value() ||
        topo_boolean_faces.value->size() != 1 || topo_boolean_faces.value->front().value != face.value->value ||
        topo_boolean_owned_shells.status != axiom::StatusCode::Ok || !topo_boolean_owned_shells.value.has_value() ||
        topo_boolean_owned_faces.status != axiom::StatusCode::Ok || !topo_boolean_owned_faces.value.has_value() ||
        topo_boolean_owned_shells.value->size() != 1 ||
        topo_boolean_owned_faces.value->size() != 6 ||
        topo_boolean_strict_valid.status != axiom::StatusCode::Ok ||
        topo_boolean_topo_valid.status != axiom::StatusCode::Ok) {
        std::cerr << "boolean source shells are unexpected\n";
        return 1;
    }
    const bool has_shell_1 = topo_boolean_shells.value->at(0).value == shell.value->value ||
                             topo_boolean_shells.value->at(1).value == shell.value->value;
    const bool has_shell_2 = topo_boolean_shells.value->at(0).value == shell2.value->value ||
                             topo_boolean_shells.value->at(1).value == shell2.value->value;
    if (!has_shell_1 || !has_shell_2) {
        std::cerr << "boolean source shells do not include both topology inputs\n";
        return 1;
    }

    auto delete_txn = kernel.topology().begin_transaction();
    auto deleted = delete_txn.delete_face(*face.value);
    if (deleted.status != axiom::StatusCode::Ok) {
        std::cerr << "delete face failed\n";
        return 1;
    }

    auto body_after_delete = kernel.topology().validate().validate_body(*body.value);
    if (body_after_delete.status == axiom::StatusCode::Ok) {
        std::cerr << "body should not validate after deleting its only face\n";
        return 1;
    }

    auto delete_rollback = delete_txn.rollback();
    if (delete_rollback.status != axiom::StatusCode::Ok) {
        std::cerr << "delete transaction rollback failed\n";
        return 1;
    }

    auto body_restored = kernel.topology().validate().validate_body(*body.value);
    if (body_restored.status != axiom::StatusCode::Ok) {
        std::cerr << "body validation should recover after rollback\n";
        return 1;
    }

    auto restored_shells_of_edge = kernel.topology().query().shells_of_edge(*e0.value);
    auto restored_bodies_of_face = kernel.topology().query().bodies_of_face(*face.value);
    if (restored_shells_of_edge.status != axiom::StatusCode::Ok || !restored_shells_of_edge.value.has_value() ||
        restored_shells_of_edge.value->size() != 2 ||
        restored_bodies_of_face.status != axiom::StatusCode::Ok || !restored_bodies_of_face.value.has_value() ||
        restored_bodies_of_face.value->size() != 2) {
        std::cerr << "reverse adjacency indexes were not restored after rollback\n";
        return 1;
    }

    const bool restored_has_shell_1 = restored_shells_of_edge.value->at(0).value == shell.value->value ||
                                      restored_shells_of_edge.value->at(1).value == shell.value->value;
    const bool restored_has_shell_2 = restored_shells_of_edge.value->at(0).value == shell2.value->value ||
                                      restored_shells_of_edge.value->at(1).value == shell2.value->value;
    const bool restored_has_body_1 = std::any_of(
        restored_bodies_of_face.value->begin(), restored_bodies_of_face.value->end(),
        [body](axiom::BodyId current) { return current.value == body.value->value; });
    const bool restored_has_body_2 = std::any_of(
        restored_bodies_of_face.value->begin(), restored_bodies_of_face.value->end(),
        [body2](axiom::BodyId current) { return current.value == body2.value->value; });
    if (!restored_has_shell_1 || !restored_has_shell_2 || !restored_has_body_1 || !restored_has_body_2) {
        std::cerr << "restored reverse adjacency indexes do not include all expected owners\n";
        return 1;
    }

    auto break_owned_shell_txn = kernel.topology().begin_transaction();
    auto broken_owned_face = break_owned_shell_txn.delete_face(topo_boolean_owned_faces.value->front());
    if (broken_owned_face.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to delete derived owned face\n";
        return 1;
    }
    auto broken_owned_strict = kernel.validate().validate_topology(topo_boolean.value->output, axiom::ValidationMode::Strict);
    if (broken_owned_strict.status == axiom::StatusCode::Ok) {
        std::cerr << "strict validation should fail for opened derived shell\n";
        return 1;
    }
    auto broken_owned_diag = kernel.diagnostics().get(broken_owned_strict.diagnostic_id);
    if (broken_owned_diag.status != axiom::StatusCode::Ok || !broken_owned_diag.value.has_value() ||
        (!has_issue_code(*broken_owned_diag.value, axiom::diag_codes::kTopoOpenBoundary) &&
         !has_issue_code(*broken_owned_diag.value, axiom::diag_codes::kTopoShellNotClosed))) {
        std::cerr << "strict validation should expose open-boundary diagnostic code\n";
        return 1;
    }
    auto break_owned_shell_rollback = break_owned_shell_txn.rollback();
    if (break_owned_shell_rollback.status != axiom::StatusCode::Ok) {
        std::cerr << "rollback failed after breaking derived owned shell\n";
        return 1;
    }
    auto restored_owned_strict = kernel.validate().validate_topology(topo_boolean.value->output, axiom::ValidationMode::Strict);
    if (restored_owned_strict.status != axiom::StatusCode::Ok) {
        std::cerr << "strict validation should recover after restoring derived owned shell\n";
        return 1;
    }

    auto break_source_face_txn = kernel.topology().begin_transaction();
    auto broken_source_face = break_source_face_txn.delete_face(*face.value);
    if (broken_source_face.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to delete source face for provenance consistency check\n";
        return 1;
    }
    auto broken_source_strict = kernel.validate().validate_topology(topo_boolean.value->output, axiom::ValidationMode::Strict);
    auto broken_source_topo = kernel.topology().validate().validate_body(topo_boolean.value->output);
    if (broken_source_strict.status == axiom::StatusCode::Ok) {
        std::cerr << "strict validation should fail for dangling source face references\n";
        return 1;
    }
    auto broken_source_diag = kernel.diagnostics().get(broken_source_strict.diagnostic_id);
    if (broken_source_diag.status != axiom::StatusCode::Ok || !broken_source_diag.value.has_value() ||
        (!has_issue_code(*broken_source_diag.value, axiom::diag_codes::kTopoSourceRefInvalid) &&
         !has_issue_code(*broken_source_diag.value, axiom::diag_codes::kTopoSourceRefMismatch) &&
         !has_issue_code(*broken_source_diag.value, axiom::diag_codes::kTopoShellNotClosed))) {
        std::cerr << "strict validation should expose source-ref diagnostic code\n";
        return 1;
    }
    if (broken_source_topo.status == axiom::StatusCode::Ok) {
        std::cerr << "topology validation should fail for dangling source face references\n";
        return 1;
    }
    auto break_source_face_rollback = break_source_face_txn.rollback();
    if (break_source_face_rollback.status != axiom::StatusCode::Ok) {
        std::cerr << "rollback failed after breaking source face references\n";
        return 1;
    }
    auto restored_source_strict = kernel.validate().validate_topology(topo_boolean.value->output, axiom::ValidationMode::Strict);
    auto restored_source_topo = kernel.topology().validate().validate_body(topo_boolean.value->output);
    if (restored_source_strict.status != axiom::StatusCode::Ok ||
        restored_source_topo.status != axiom::StatusCode::Ok) {
        std::cerr << "validation should recover after restoring source face references\n";
        return 1;
    }

    // ---- Stage 2 regression: delete_body / delete_shell should be transactional and rollback-safe ----
    {
        auto del_txn = kernel.topology().begin_transaction();
        auto del = del_txn.delete_body(*body.value);
        if (del.status != axiom::StatusCode::Ok) {
            std::cerr << "delete body failed\n";
            return 1;
        }
        auto after_del = kernel.topology().validate().validate_body(*body.value);
        if (after_del.status == axiom::StatusCode::Ok) {
            std::cerr << "body should not validate after deletion\n";
            return 1;
        }
        auto rb = del_txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed after delete body\n";
            return 1;
        }
        auto restored = kernel.topology().validate().validate_body(*body.value);
        if (restored.status != axiom::StatusCode::Ok) {
            std::cerr << "body should validate after rollback of delete\n";
            return 1;
        }
    }

    {
        auto del_shell_txn = kernel.topology().begin_transaction();
        auto del = del_shell_txn.delete_shell(*shell.value);
        if (del.status != axiom::StatusCode::Ok) {
            std::cerr << "delete shell failed\n";
            return 1;
        }
        auto after_del_shell = kernel.topology().validate().validate_shell(*shell.value);
        if (after_del_shell.status == axiom::StatusCode::Ok) {
            std::cerr << "shell should not validate after deletion\n";
            return 1;
        }
        auto rb = del_shell_txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed after delete shell\n";
            return 1;
        }
        auto restored_shell = kernel.topology().validate().validate_shell(*shell.value);
        if (restored_shell.status != axiom::StatusCode::Ok) {
            std::cerr << "shell should validate after rollback of delete\n";
            return 1;
        }
    }

    // ---- Stage 2 regression: Strict topology should fail for open shells (boundary edges) ----
    {
        auto open_txn = kernel.topology().begin_transaction();
        auto open_shell = open_txn.create_shell(std::array<axiom::FaceId, 1>{*face.value});
        auto open_body = open_txn.create_body(std::array<axiom::ShellId, 1>{*open_shell.value});
        if (open_shell.status != axiom::StatusCode::Ok || open_body.status != axiom::StatusCode::Ok ||
            !open_shell.value.has_value() || !open_body.value.has_value()) {
            std::cerr << "failed to create open shell body for strict validation\n";
            return 1;
        }
        auto open_strict = kernel.validate().validate_topology(*open_body.value, axiom::ValidationMode::Strict);
        if (open_strict.status == axiom::StatusCode::Ok) {
            std::cerr << "expected strict topology validation to fail for open shells\n";
            return 1;
        }
        auto open_shell_closed = kernel.topology().validate().validate_shell_closedness(*open_shell.value);
        if (open_shell_closed.status == axiom::StatusCode::Ok) {
            std::cerr << "expected TopoCore shell closedness validation to fail for open shells\n";
            return 1;
        }
        auto open_topo_shell = kernel.topology().validate().validate_shell(*open_shell.value);
        if (open_topo_shell.status != axiom::StatusCode::Ok) {
            std::cerr << "expected validate_shell Ok with warnings for open shell\n";
            return 1;
        }
        auto loops_open = kernel.topology().query().loops_of_face(*face.value);
        if (loops_open.status != axiom::StatusCode::Ok || !loops_open.value.has_value() ||
            loops_open.value->empty()) {
            std::cerr << "failed to query loops for open-shell related_entities test\n";
            return 1;
        }
        auto edges_open = kernel.topology().query().edges_of_loop(loops_open.value->front());
        if (edges_open.status != axiom::StatusCode::Ok || !edges_open.value.has_value() ||
            edges_open.value->empty()) {
            std::cerr << "failed to query edges for open-shell related_entities test\n";
            return 1;
        }
        auto open_topo_shell_diag = kernel.diagnostics().get(open_topo_shell.diagnostic_id);
        if (open_topo_shell_diag.status != axiom::StatusCode::Ok || !open_topo_shell_diag.value.has_value() ||
            !issue_links_entities(*open_topo_shell_diag.value, axiom::diag_codes::kTopoOpenBoundary,
                                  {open_shell.value->value, edges_open.value->front().value})) {
            std::cerr << "expected validate_shell open-boundary warning with shell+edge related_entities\n";
            return 1;
        }
        auto open_diag = kernel.diagnostics().get(open_strict.diagnostic_id);
        if (open_diag.status != axiom::StatusCode::Ok || !open_diag.value.has_value() ||
            !has_issue_code(*open_diag.value, axiom::diag_codes::kTopoOpenBoundary)) {
            std::cerr << "expected strict validation to expose open-boundary diagnostic code\n";
            return 1;
        }
        auto open_rollback = open_txn.rollback();
        if (open_rollback.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed after open shell strict test\n";
            return 1;
        }
    }

    // ---- Stage 2 regression: Strict topology should fail for non-manifold shells (edge use > 2) ----
    // Three distinct loops reusing the same four EdgeIds (each face owns its coedges) so each edge is used 3x in the shell.
    {
        auto edges_q = kernel.topology().query().edges_of_loop(*loop.value);
        if (edges_q.status != axiom::StatusCode::Ok || !edges_q.value.has_value() ||
            edges_q.value->size() != 4) {
            std::cerr << "failed to query edges for non-manifold shell test\n";
            return 1;
        }
        const auto& E = *edges_q.value;
        auto nm_txn = kernel.topology().begin_transaction();
        auto nm_a0 = nm_txn.create_coedge(E[0], false);
        auto nm_a1 = nm_txn.create_coedge(E[1], false);
        auto nm_a2 = nm_txn.create_coedge(E[2], false);
        auto nm_a3 = nm_txn.create_coedge(E[3], false);
        auto nm_b0 = nm_txn.create_coedge(E[0], false);
        auto nm_b1 = nm_txn.create_coedge(E[1], false);
        auto nm_b2 = nm_txn.create_coedge(E[2], false);
        auto nm_b3 = nm_txn.create_coedge(E[3], false);
        auto nm_c0 = nm_txn.create_coedge(E[0], false);
        auto nm_c1 = nm_txn.create_coedge(E[1], false);
        auto nm_c2 = nm_txn.create_coedge(E[2], false);
        auto nm_c3 = nm_txn.create_coedge(E[3], false);
        if (nm_a0.status != axiom::StatusCode::Ok || nm_a1.status != axiom::StatusCode::Ok ||
            nm_a2.status != axiom::StatusCode::Ok || nm_a3.status != axiom::StatusCode::Ok ||
            nm_b0.status != axiom::StatusCode::Ok || nm_b1.status != axiom::StatusCode::Ok ||
            nm_b2.status != axiom::StatusCode::Ok || nm_b3.status != axiom::StatusCode::Ok ||
            nm_c0.status != axiom::StatusCode::Ok || nm_c1.status != axiom::StatusCode::Ok ||
            nm_c2.status != axiom::StatusCode::Ok || nm_c3.status != axiom::StatusCode::Ok ||
            !nm_a0.value.has_value() || !nm_a1.value.has_value() || !nm_a2.value.has_value() ||
            !nm_a3.value.has_value() || !nm_b0.value.has_value() || !nm_b1.value.has_value() ||
            !nm_b2.value.has_value() || !nm_b3.value.has_value() || !nm_c0.value.has_value() ||
            !nm_c1.value.has_value() || !nm_c2.value.has_value() || !nm_c3.value.has_value()) {
            std::cerr << "failed to create coedges for non-manifold shell test\n";
            return 1;
        }
        const std::array<axiom::CoedgeId, 4> ring0 {
            *nm_a0.value, *nm_a1.value, *nm_a2.value, *nm_a3.value};
        const std::array<axiom::CoedgeId, 4> ring1 {
            *nm_b0.value, *nm_b1.value, *nm_b2.value, *nm_b3.value};
        const std::array<axiom::CoedgeId, 4> ring2 {
            *nm_c0.value, *nm_c1.value, *nm_c2.value, *nm_c3.value};
        auto lp0 = nm_txn.create_loop(ring0);
        auto lp1 = nm_txn.create_loop(ring1);
        auto lp2 = nm_txn.create_loop(ring2);
        if (lp0.status != axiom::StatusCode::Ok || lp1.status != axiom::StatusCode::Ok ||
            lp2.status != axiom::StatusCode::Ok || !lp0.value.has_value() ||
            !lp1.value.has_value() || !lp2.value.has_value()) {
            std::cerr << "failed to create parallel loops for non-manifold shell test\n";
            return 1;
        }
        auto f0 = nm_txn.create_face(*plane0.value, *lp0.value, {});
        auto f1 = nm_txn.create_face(*plane0.value, *lp1.value, {});
        auto f2 = nm_txn.create_face(*plane0.value, *lp2.value, {});
        if (f0.status != axiom::StatusCode::Ok || f1.status != axiom::StatusCode::Ok || f2.status != axiom::StatusCode::Ok ||
            !f0.value.has_value() || !f1.value.has_value() || !f2.value.has_value()) {
            std::cerr << "failed to create non-manifold faces\n";
            return 1;
        }
        auto nm_shell = nm_txn.create_shell(std::array<axiom::FaceId, 3>{*f0.value, *f1.value, *f2.value});
        auto nm_body = nm_txn.create_body(std::array<axiom::ShellId, 1>{*nm_shell.value});
        if (nm_shell.status != axiom::StatusCode::Ok || nm_body.status != axiom::StatusCode::Ok ||
            !nm_shell.value.has_value() || !nm_body.value.has_value()) {
            std::cerr << "failed to create non-manifold shell body for strict validation\n";
            return 1;
        }
        auto nm_topo_shell = kernel.topology().validate().validate_shell(*nm_shell.value);
        if (nm_topo_shell.status != axiom::StatusCode::Ok) {
            std::cerr << "expected validate_shell Ok with warnings for non-manifold shell\n";
            return 1;
        }
        auto nm_topo_shell_diag = kernel.diagnostics().get(nm_topo_shell.diagnostic_id);
        if (nm_topo_shell_diag.status != axiom::StatusCode::Ok || !nm_topo_shell_diag.value.has_value() ||
            !issue_links_entities(*nm_topo_shell_diag.value, axiom::diag_codes::kTopoNonManifoldEdge,
                                  {nm_shell.value->value, E[0].value})) {
            std::cerr << "expected validate_shell non-manifold warning with shell+edge related_entities\n";
            return 1;
        }
        auto nm_strict = kernel.validate().validate_topology(*nm_body.value, axiom::ValidationMode::Strict);
        if (nm_strict.status == axiom::StatusCode::Ok) {
            std::cerr << "expected strict topology validation to fail for non-manifold shells\n";
            return 1;
        }
        auto nm_shell_closed = kernel.topology().validate().validate_shell_closedness(*nm_shell.value);
        if (nm_shell_closed.status == axiom::StatusCode::Ok) {
            std::cerr << "expected TopoCore shell closedness validation to fail for non-manifold shells\n";
            return 1;
        }
        auto nm_diag = kernel.diagnostics().get(nm_strict.diagnostic_id);
        if (nm_diag.status != axiom::StatusCode::Ok || !nm_diag.value.has_value() ||
            !has_issue_code(*nm_diag.value, axiom::diag_codes::kTopoNonManifoldEdge)) {
            std::cerr << "expected strict validation to expose non-manifold diagnostic code\n";
            return 1;
        }
        auto nm_rollback = nm_txn.rollback();
        if (nm_rollback.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed after non-manifold strict test\n";
            return 1;
        }
    }

    // ---- Stage 2+: same-surface same-loop-id duplicate shell signature is blocked at create_face (7.2); Strict duplicate-signature path is covered by Heal strict_check when data is corrupted. ----

    // ---- Stage 2+: Strict topology should fail for disconnected (but closed) shells ----
    {
        auto w0 = kernel.primitives().wedge({10.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
        auto w1 = kernel.primitives().wedge({20.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
        if (w0.status != axiom::StatusCode::Ok || w1.status != axiom::StatusCode::Ok ||
            !w0.value.has_value() || !w1.value.has_value()) {
            std::cerr << "failed to create wedges for strict disconnected-shell test\n";
            return 1;
        }
        auto shells0 = kernel.topology().query().shells_of_body(*w0.value);
        auto shells1 = kernel.topology().query().shells_of_body(*w1.value);
        if (shells0.status != axiom::StatusCode::Ok || shells1.status != axiom::StatusCode::Ok ||
            !shells0.value.has_value() || !shells1.value.has_value() ||
            shells0.value->empty() || shells1.value->empty()) {
            std::cerr << "failed to query wedge shells for strict disconnected-shell test\n";
            return 1;
        }
        auto faces0 = kernel.topology().query().faces_of_shell(shells0.value->front());
        auto faces1 = kernel.topology().query().faces_of_shell(shells1.value->front());
        if (faces0.status != axiom::StatusCode::Ok || faces1.status != axiom::StatusCode::Ok ||
            !faces0.value.has_value() || !faces1.value.has_value() ||
            faces0.value->empty() || faces1.value->empty()) {
            std::cerr << "failed to query wedge faces for strict disconnected-shell test\n";
            return 1;
        }

        std::vector<axiom::FaceId> combined;
        combined.reserve(faces0.value->size() + faces1.value->size());
        combined.insert(combined.end(), faces0.value->begin(), faces0.value->end());
        combined.insert(combined.end(), faces1.value->begin(), faces1.value->end());

        auto txn = kernel.topology().begin_transaction();
        auto shell = txn.create_shell(combined);
        auto body = txn.create_body(std::array<axiom::ShellId, 1>{*shell.value});
        if (shell.status != axiom::StatusCode::Ok || body.status != axiom::StatusCode::Ok ||
            !shell.value.has_value() || !body.value.has_value()) {
            std::cerr << "failed to create combined shell/body for strict disconnected-shell test\n";
            return 1;
        }
        auto strict = kernel.validate().validate_topology(*body.value, axiom::ValidationMode::Strict);
        if (strict.status == axiom::StatusCode::Ok) {
            std::cerr << "expected strict topology validation to fail for disconnected shell\n";
            return 1;
        }
        auto diag = kernel.diagnostics().get(strict.diagnostic_id);
        if (diag.status != axiom::StatusCode::Ok || !diag.value.has_value() ||
            !has_issue_code(*diag.value, axiom::diag_codes::kTopoShellDisconnected)) {
            std::cerr << "expected strict validation to expose shell-disconnected diagnostic code\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed after strict disconnected-shell test\n";
            return 1;
        }
    }

    // ---- Stage 2+: Strict topology should fail when a Body shares one owned Face across multiple owned Shells ----
    {
        auto box = kernel.primitives().box({0.0, 0.0, 0.0}, 1.0, 2.0, 3.0);
        if (box.status != axiom::StatusCode::Ok || !box.value.has_value()) {
            std::cerr << "failed to create box for shared-face-across-shells test\n";
            return 1;
        }
        auto shells = kernel.topology().query().shells_of_body(*box.value);
        if (shells.status != axiom::StatusCode::Ok || !shells.value.has_value() || shells.value->empty()) {
            std::cerr << "failed to query box shells for shared-face-across-shells test\n";
            return 1;
        }
        auto faces = kernel.topology().query().faces_of_shell(shells.value->front());
        if (faces.status != axiom::StatusCode::Ok || !faces.value.has_value() || faces.value->size() != 6) {
            std::cerr << "expected 6 faces on box shell for shared-face-across-shells test\n";
            return 1;
        }
        const auto shared_face = faces.value->front();

        auto txn = kernel.topology().begin_transaction();
        auto sh1 = txn.create_shell(std::array<axiom::FaceId, 6>{
            (*faces.value)[0], (*faces.value)[1], (*faces.value)[2],
            (*faces.value)[3], (*faces.value)[4], (*faces.value)[5]
        });
        if (sh1.status != axiom::StatusCode::Ok || !sh1.value.has_value()) {
            std::cerr << "failed to create first shell clone for shared-face-across-shells test\n";
            return 1;
        }
        // Second shell shares one face with the first shell but is otherwise a closed box shell.
        std::array<axiom::FaceId, 6> faces2{
            shared_face, (*faces.value)[1], (*faces.value)[2],
            (*faces.value)[3], (*faces.value)[4], (*faces.value)[5]
        };
        auto sh2 = txn.create_shell(faces2);
        if (sh2.status != axiom::StatusCode::Ok || !sh2.value.has_value()) {
            std::cerr << "failed to create second shell for shared-face-across-shells test\n";
            return 1;
        }
        auto body = txn.create_body(std::array<axiom::ShellId, 2>{*sh1.value, *sh2.value});
        if (body.status != axiom::StatusCode::Ok || !body.value.has_value()) {
            std::cerr << "failed to create body for shared-face-across-shells test\n";
            return 1;
        }

        auto strict = kernel.validate().validate_topology(*body.value, axiom::ValidationMode::Strict);
        if (strict.status == axiom::StatusCode::Ok) {
            std::cerr << "expected Strict to fail for shared face across shells\n";
            return 1;
        }
        auto diag = kernel.diagnostics().get(strict.diagnostic_id);
        if (diag.status != axiom::StatusCode::Ok || !diag.value.has_value() ||
            !has_issue_code(*diag.value, axiom::diag_codes::kTopoRelationInconsistent)) {
            std::cerr << "expected kTopoRelationInconsistent for shared face across shells\n";
            return 1;
        }

        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for shared-face-across-shells test\n";
            return 1;
        }
    }

    // ---- Stage 2: Face must not reuse the same Edge across outer/inner loops ----
    {
        auto l01 = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto l12 = kernel.curves().make_line({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        auto l20 = kernel.curves().make_line({0.0, 1.0, 0.0}, {-1.0, -1.0, 0.0});
        auto l13 = kernel.curves().make_line({1.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto l34 = kernel.curves().make_line({2.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        auto l40 = kernel.curves().make_line({2.0, 1.0, 0.0}, {-2.0, -1.0, 0.0});
        if (l01.status != axiom::StatusCode::Ok || l12.status != axiom::StatusCode::Ok ||
            l20.status != axiom::StatusCode::Ok || l13.status != axiom::StatusCode::Ok ||
            l34.status != axiom::StatusCode::Ok || l40.status != axiom::StatusCode::Ok ||
            !l01.value.has_value() || !l12.value.has_value() || !l20.value.has_value() ||
            !l13.value.has_value() || !l34.value.has_value() || !l40.value.has_value()) {
            std::cerr << "failed to create curve prerequisites for face cross-loop duplicate-edge test\n";
            return 1;
        }

        auto txn = kernel.topology().begin_transaction();

        auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
        auto v2 = txn.create_vertex({0.0, 1.0, 0.0});
        auto v3 = txn.create_vertex({2.0, 0.0, 0.0});
        auto v4 = txn.create_vertex({2.0, 1.0, 0.0});

        if (v0.status != axiom::StatusCode::Ok || v1.status != axiom::StatusCode::Ok ||
            v2.status != axiom::StatusCode::Ok || v3.status != axiom::StatusCode::Ok ||
            v4.status != axiom::StatusCode::Ok || !v0.value || !v1.value || !v2.value ||
            !v3.value || !v4.value) {
            std::cerr << "failed to create vertices for face cross-loop duplicate-edge test\n";
            return 1;
        }

        auto e01 = txn.create_edge(*l01.value, *v0.value, *v1.value);
        auto e12 = txn.create_edge(*l12.value, *v1.value, *v2.value);
        auto e20 = txn.create_edge(*l20.value, *v2.value, *v0.value);
        auto e13 = txn.create_edge(*l13.value, *v1.value, *v3.value);
        auto e34 = txn.create_edge(*l34.value, *v3.value, *v4.value);
        auto e40 = txn.create_edge(*l40.value, *v4.value, *v0.value);
        if (e01.status != axiom::StatusCode::Ok || e12.status != axiom::StatusCode::Ok ||
            e20.status != axiom::StatusCode::Ok || e13.status != axiom::StatusCode::Ok ||
            e34.status != axiom::StatusCode::Ok || e40.status != axiom::StatusCode::Ok ||
            !e01.value || !e12.value || !e20.value || !e13.value || !e34.value || !e40.value) {
            std::cerr << "failed to create edges for face cross-loop duplicate-edge test\n";
            return 1;
        }

        auto c01 = txn.create_coedge(*e01.value, false);
        auto c12 = txn.create_coedge(*e12.value, false);
        auto c20 = txn.create_coedge(*e20.value, false);

        auto c01b = txn.create_coedge(*e01.value, false); // same underlying edge, different coedge
        auto c13 = txn.create_coedge(*e13.value, false);
        auto c34 = txn.create_coedge(*e34.value, false);
        auto c40 = txn.create_coedge(*e40.value, false);

        if (c01.status != axiom::StatusCode::Ok || c12.status != axiom::StatusCode::Ok ||
            c20.status != axiom::StatusCode::Ok || c01b.status != axiom::StatusCode::Ok ||
            c13.status != axiom::StatusCode::Ok || c34.status != axiom::StatusCode::Ok ||
            c40.status != axiom::StatusCode::Ok || !c01.value || !c12.value || !c20.value ||
            !c01b.value || !c13.value || !c34.value || !c40.value) {
            std::cerr << "failed to create coedges for face cross-loop duplicate-edge test\n";
            return 1;
        }

        auto outer = txn.create_loop(std::array<axiom::CoedgeId, 3>{*c01.value, *c12.value, *c20.value});
        auto inner = txn.create_loop(std::array<axiom::CoedgeId, 4>{*c01b.value, *c13.value, *c34.value, *c40.value});
        if (outer.status != axiom::StatusCode::Ok || inner.status != axiom::StatusCode::Ok ||
            !outer.value || !inner.value) {
            std::cerr << "failed to create loops for face cross-loop duplicate-edge test\n";
            return 1;
        }

        auto face = txn.create_face(*plane0.value, *outer.value, std::array<axiom::LoopId, 1>{*inner.value});
        if (face.status != axiom::StatusCode::Ok || !face.value) {
            std::cerr << "failed to create face for face cross-loop duplicate-edge test\n";
            return 1;
        }

        auto shell = txn.create_shell(std::array<axiom::FaceId, 1>{*face.value});
        auto body = txn.create_body(std::array<axiom::ShellId, 1>{*shell.value});
        if (shell.status != axiom::StatusCode::Ok || body.status != axiom::StatusCode::Ok ||
            !shell.value.has_value() || !body.value.has_value()) {
            std::cerr << "failed to create shell/body for face cross-loop duplicate-edge test\n";
            return 1;
        }

        auto r = kernel.validate().validate_topology(*body.value, axiom::ValidationMode::Standard);
        if (r.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_topology to fail for cross-loop duplicate edge in one face\n";
            return 1;
        }
        auto diag = kernel.diagnostics().get(r.diagnostic_id);
        if (diag.status != axiom::StatusCode::Ok || !diag.value) {
            std::cerr << "expected diagnostics for cross-loop duplicate edge validation failure\n";
            return 1;
        }
        const bool has_code = has_issue_code(*diag.value, axiom::diag_codes::kTopoLoopDuplicateEdge);
        if (!has_code) {
            std::cerr << "expected kTopoLoopDuplicateEdge for cross-loop duplicate edge in face\n";
            return 1;
        }

        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed after face cross-loop duplicate-edge test\n";
            return 1;
        }
    }

    // ---- Stage 2+: PCurve trim -> underlying surface + Trimmed materialization (nested face surface) ----
    {
        auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto outer_trim = kernel.surfaces().make_trimmed(*plane.value, -1.0, 12.0, -1.0, 12.0);
        if (plane.status != axiom::StatusCode::Ok || !plane.value ||
            outer_trim.status != axiom::StatusCode::Ok || !outer_trim.value) {
            std::cerr << "failed to create plane/outer trim for nested topo trim test\n";
            return 1;
        }
        auto l0 = kernel.curves().make_line({2.0, 2.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = kernel.curves().make_line({4.0, 2.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = kernel.curves().make_line({4.0, 4.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = kernel.curves().make_line({2.0, 4.0, 0.0}, {0.0, -1.0, 0.0});
        const std::array<axiom::Point2, 2> uva {{ {2.0, 2.0}, {4.0, 2.0} }};
        const std::array<axiom::Point2, 2> uvb {{ {4.0, 2.0}, {4.0, 4.0} }};
        const std::array<axiom::Point2, 2> uvc {{ {4.0, 4.0}, {2.0, 4.0} }};
        const std::array<axiom::Point2, 2> uvd {{ {2.0, 4.0}, {2.0, 2.0} }};
        auto pca = kernel.pcurves().make_polyline(uva);
        auto pcb = kernel.pcurves().make_polyline(uvb);
        auto pcc = kernel.pcurves().make_polyline(uvc);
        auto pcd = kernel.pcurves().make_polyline(uvd);
        if (l0.status != axiom::StatusCode::Ok || !l0.value || l1.status != axiom::StatusCode::Ok || !l1.value ||
            l2.status != axiom::StatusCode::Ok || !l2.value || l3.status != axiom::StatusCode::Ok || !l3.value ||
            pca.status != axiom::StatusCode::Ok || !pca.value || pcb.status != axiom::StatusCode::Ok || !pcb.value ||
            pcc.status != axiom::StatusCode::Ok || !pcc.value || pcd.status != axiom::StatusCode::Ok || !pcd.value) {
            std::cerr << "failed to create geometry for nested topo trim test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto v0 = txn.create_vertex({2.0, 2.0, 0.0});
        auto v1 = txn.create_vertex({4.0, 2.0, 0.0});
        auto v2 = txn.create_vertex({4.0, 4.0, 0.0});
        auto v3 = txn.create_vertex({2.0, 4.0, 0.0});
        auto e0 = txn.create_edge(*l0.value, *v0.value, *v1.value);
        auto e1 = txn.create_edge(*l1.value, *v1.value, *v2.value);
        auto e2 = txn.create_edge(*l2.value, *v2.value, *v3.value);
        auto e3 = txn.create_edge(*l3.value, *v3.value, *v0.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        auto c1 = txn.create_coedge(*e1.value, false);
        auto c2 = txn.create_coedge(*e2.value, false);
        auto c3 = txn.create_coedge(*e3.value, false);
        if (v0.status != axiom::StatusCode::Ok || !v0.value || v1.status != axiom::StatusCode::Ok || !v1.value ||
            v2.status != axiom::StatusCode::Ok || !v2.value || v3.status != axiom::StatusCode::Ok || !v3.value ||
            e0.status != axiom::StatusCode::Ok || !e0.value || e1.status != axiom::StatusCode::Ok || !e1.value ||
            e2.status != axiom::StatusCode::Ok || !e2.value || e3.status != axiom::StatusCode::Ok || !e3.value ||
            c0.status != axiom::StatusCode::Ok || !c0.value || c1.status != axiom::StatusCode::Ok || !c1.value ||
            c2.status != axiom::StatusCode::Ok || !c2.value || c3.status != axiom::StatusCode::Ok || !c3.value) {
            std::cerr << "failed to build topology for nested topo trim test\n";
            return 1;
        }
        txn.set_coedge_pcurve(*c0.value, *pca.value);
        txn.set_coedge_pcurve(*c1.value, *pcb.value);
        txn.set_coedge_pcurve(*c2.value, *pcc.value);
        txn.set_coedge_pcurve(*c3.value, *pcd.value);
        const std::array<axiom::CoedgeId, 4> coeds {{ *c0.value, *c1.value, *c2.value, *c3.value }};
        auto loop = txn.create_loop(coeds);
        auto face = txn.create_face(*outer_trim.value, *loop.value, {});
        if (loop.status != axiom::StatusCode::Ok || !loop.value || face.status != axiom::StatusCode::Ok || !face.value) {
            std::cerr << "failed to create face on outer_trim for nested topo trim test\n";
            return 1;
        }
        auto under = kernel.topology().query().underlying_surface_for_face_trim(*face.value);
        if (under.status != axiom::StatusCode::Ok || !under.value || under.value->value != plane.value->value) {
            std::cerr << "nested face should unwrap to plane as trim base\n";
            return 1;
        }
        auto ub = kernel.topology().query().face_outer_loop_uv_bounds(*face.value);
        if (ub.status != axiom::StatusCode::Ok || !ub.value ||
            std::abs(ub.value->u.min - 2.0) > 1e-9 || std::abs(ub.value->u.max - 4.0) > 1e-9 ||
            std::abs(ub.value->v.min - 2.0) > 1e-9 || std::abs(ub.value->v.max - 4.0) > 1e-9) {
            std::cerr << "unexpected UV bounds for nested trim face\n";
            return 1;
        }
        auto ts = kernel.create_trimmed_surface_from_face_outer_loop_pcurves(*face.value);
        if (ts.status != axiom::StatusCode::Ok || !ts.value) {
            std::cerr << "materialize trimmed from nested face failed\n";
            return 1;
        }
        auto ev = kernel.surface_service().eval(*ts.value, 3.0, 3.0, 0);
        if (ev.status != axiom::StatusCode::Ok || !ev.value ||
            std::abs(ev.value->point.x - 3.0) > 1e-6 || std::abs(ev.value->point.y - 3.0) > 1e-6 ||
            std::abs(ev.value->point.z) > 1e-6) {
            std::cerr << "unexpected eval on nested-topology-derived trim\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed after nested topo trim test\n";
            return 1;
        }
    }

    // face_outer_loop_uv_bounds should fail when a coedge has no PCurve.
    {
        auto plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto l0 = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = kernel.curves().make_line({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = kernel.curves().make_line({1.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = kernel.curves().make_line({0.0, 1.0, 0.0}, {0.0, -1.0, 0.0});
        if (plane.status != axiom::StatusCode::Ok || !plane.value ||
            l0.status != axiom::StatusCode::Ok || !l0.value || l1.status != axiom::StatusCode::Ok || !l1.value ||
            l2.status != axiom::StatusCode::Ok || !l2.value || l3.status != axiom::StatusCode::Ok || !l3.value) {
            std::cerr << "failed to create geometry for missing-pcurve bounds test\n";
            return 1;
        }
        auto txn = kernel.topology().begin_transaction();
        auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
        auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
        auto v2 = txn.create_vertex({1.0, 1.0, 0.0});
        auto v3 = txn.create_vertex({0.0, 1.0, 0.0});
        auto e0 = txn.create_edge(*l0.value, *v0.value, *v1.value);
        auto e1 = txn.create_edge(*l1.value, *v1.value, *v2.value);
        auto e2 = txn.create_edge(*l2.value, *v2.value, *v3.value);
        auto e3 = txn.create_edge(*l3.value, *v3.value, *v0.value);
        auto c0 = txn.create_coedge(*e0.value, false);
        auto c1 = txn.create_coedge(*e1.value, false);
        auto c2 = txn.create_coedge(*e2.value, false);
        auto c3 = txn.create_coedge(*e3.value, false);
        if (e0.status != axiom::StatusCode::Ok || !e0.value || e1.status != axiom::StatusCode::Ok || !e1.value ||
            e2.status != axiom::StatusCode::Ok || !e2.value || e3.status != axiom::StatusCode::Ok || !e3.value ||
            c0.status != axiom::StatusCode::Ok || !c0.value || c1.status != axiom::StatusCode::Ok || !c1.value ||
            c2.status != axiom::StatusCode::Ok || !c2.value || c3.status != axiom::StatusCode::Ok || !c3.value) {
            std::cerr << "failed to build topology for missing-pcurve bounds test\n";
            return 1;
        }
        const std::array<axiom::Point2, 2> uv01 {{ {0.0, 0.0}, {1.0, 0.0} }};
        const std::array<axiom::Point2, 2> uv12 {{ {1.0, 0.0}, {1.0, 1.0} }};
        const std::array<axiom::Point2, 2> uv23 {{ {1.0, 1.0}, {0.0, 1.0} }};
        auto pc01 = kernel.pcurves().make_polyline(uv01);
        auto pc12 = kernel.pcurves().make_polyline(uv12);
        auto pc23 = kernel.pcurves().make_polyline(uv23);
        if (pc01.status != axiom::StatusCode::Ok || !pc01.value || pc12.status != axiom::StatusCode::Ok || !pc12.value ||
            pc23.status != axiom::StatusCode::Ok || !pc23.value) {
            std::cerr << "failed to create pcurves for missing-pcurve bounds test\n";
            return 1;
        }
        txn.set_coedge_pcurve(*c0.value, *pc01.value);
        txn.set_coedge_pcurve(*c1.value, *pc12.value);
        txn.set_coedge_pcurve(*c2.value, *pc23.value);
        // c3 intentionally unbound
        const std::array<axiom::CoedgeId, 4> coeds {{ *c0.value, *c1.value, *c2.value, *c3.value }};
        auto loop = txn.create_loop(coeds);
        auto face = txn.create_face(*plane.value, *loop.value, {});
        if (loop.status != axiom::StatusCode::Ok || !loop.value || face.status != axiom::StatusCode::Ok || !face.value) {
            std::cerr << "failed to create face for missing-pcurve bounds test\n";
            return 1;
        }
        auto bad_bounds = kernel.topology().query().face_outer_loop_uv_bounds(*face.value);
        if (bad_bounds.status == axiom::StatusCode::Ok) {
            std::cerr << "expected face_outer_loop_uv_bounds to fail without full pcurve bind\n";
            return 1;
        }
        auto diag = kernel.diagnostics().get(bad_bounds.diagnostic_id);
        if (diag.status != axiom::StatusCode::Ok || !diag.value ||
            !has_issue_code(*diag.value, axiom::diag_codes::kTopoRelationInconsistent)) {
            std::cerr << "expected kTopoRelationInconsistent for missing pcurve on loop\n";
            return 1;
        }
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed after missing-pcurve bounds test\n";
            return 1;
        }
    }

    auto clear_txn = kernel.topology().begin_transaction();
    auto clear_v = clear_txn.create_vertex({2.0, 2.0, 2.0});
    if (clear_v.status != axiom::StatusCode::Ok || !clear_v.value.has_value()) {
        std::cerr << "failed to create vertex for clear tracking test\n";
        return 1;
    }
    auto clear_before = clear_txn.created_vertex_count();
    auto clear_w0 = clear_txn.write_operation_count();
    auto clear_ret = clear_txn.clear_tracking_records();
    auto clear_after = clear_txn.created_vertex_count();
    auto clear_w1 = clear_txn.write_operation_count();
    if (clear_before.status != axiom::StatusCode::Ok || !clear_before.value.has_value() || *clear_before.value != 1 ||
        clear_w0.status != axiom::StatusCode::Ok || !clear_w0.value.has_value() || *clear_w0.value != 1 ||
        clear_ret.status != axiom::StatusCode::Ok ||
        clear_after.status != axiom::StatusCode::Ok || !clear_after.value.has_value() || *clear_after.value != 0 ||
        clear_w1.status != axiom::StatusCode::Ok || !clear_w1.value.has_value() || *clear_w1.value != 0) {
        std::cerr << "clear_tracking_records behavior is unexpected\n";
        return 1;
    }
    auto clear_rollback = clear_txn.rollback();
    if (clear_rollback.status != axiom::StatusCode::Ok) {
        std::cerr << "rollback failed for clear tracking transaction\n";
        return 1;
    }

    return 0;
}
