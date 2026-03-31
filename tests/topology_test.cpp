#include <array>
#include <iostream>

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
        auto rb = txn.rollback();
        if (rb.status != axiom::StatusCode::Ok) {
            std::cerr << "rollback failed for uv loop test\n";
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
    {
        auto nm_txn = kernel.topology().begin_transaction();
        auto f0 = nm_txn.create_face(*plane0.value, *loop.value, {});
        auto f1 = nm_txn.create_face(*plane0.value, *loop.value, {});
        auto f2 = nm_txn.create_face(*plane0.value, *loop.value, {});
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

    auto clear_txn = kernel.topology().begin_transaction();
    auto clear_v = clear_txn.create_vertex({2.0, 2.0, 2.0});
    if (clear_v.status != axiom::StatusCode::Ok || !clear_v.value.has_value()) {
        std::cerr << "failed to create vertex for clear tracking test\n";
        return 1;
    }
    auto clear_before = clear_txn.created_vertex_count();
    auto clear_ret = clear_txn.clear_tracking_records();
    auto clear_after = clear_txn.created_vertex_count();
    if (clear_before.status != axiom::StatusCode::Ok || !clear_before.value.has_value() || *clear_before.value != 1 ||
        clear_ret.status != axiom::StatusCode::Ok ||
        clear_after.status != axiom::StatusCode::Ok || !clear_after.value.has_value() || *clear_after.value != 0) {
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
