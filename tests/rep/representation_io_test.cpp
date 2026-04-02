#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

namespace {

bool approx(double lhs, double rhs, double eps = 1e-6) {
    return std::abs(lhs - rhs) <= eps;
}

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

    auto box = kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 20.0, 30.0);
    if (box.status != axiom::StatusCode::Ok || !box.value.has_value()) {
        std::cerr << "failed to create box\n";
        return 1;
    }

    auto bbox = kernel.representation().bbox_of_body(*box.value);
    if (bbox.status != axiom::StatusCode::Ok || !bbox.value.has_value()) {
        std::cerr << "failed to query bbox\n";
        return 1;
    }

    auto inside_distance = kernel.representation().distance_to_body(*box.value, {5.0, 5.0, 5.0});
    auto outside_distance = kernel.representation().distance_to_body(*box.value, {15.0, 20.0, 30.0});
    if (inside_distance.status != axiom::StatusCode::Ok || outside_distance.status != axiom::StatusCode::Ok ||
        !inside_distance.value.has_value() || !outside_distance.value.has_value()) {
        std::cerr << "failed to query representation distance\n";
        return 1;
    }

    if (!approx(*inside_distance.value, 0.0) || !approx(*outside_distance.value, 5.0)) {
        std::cerr << "unexpected representation distance result\n";
        return 1;
    }

    auto mesh = kernel.convert().brep_to_mesh(*box.value, {});
    if (mesh.status != axiom::StatusCode::Ok || !mesh.value.has_value()) {
        std::cerr << "failed to convert brep to mesh\n";
        return 1;
    }
    auto coarse_mesh = kernel.convert().brep_to_mesh(*box.value, {0.5, 45.0, true});
    auto fine_mesh = kernel.convert().brep_to_mesh(*box.value, {0.05, 5.0, true});
    if (coarse_mesh.status != axiom::StatusCode::Ok || fine_mesh.status != axiom::StatusCode::Ok ||
        !coarse_mesh.value.has_value() || !fine_mesh.value.has_value()) {
        std::cerr << "failed to build coarse/fine mesh variants\n";
        return 1;
    }
    auto coarse_triangles = kernel.convert().mesh_triangle_count(*coarse_mesh.value);
    auto fine_triangles = kernel.convert().mesh_triangle_count(*fine_mesh.value);
    auto coarse_vertices = kernel.convert().mesh_vertex_count(*coarse_mesh.value);
    auto fine_vertices = kernel.convert().mesh_vertex_count(*fine_mesh.value);
    auto fine_indices = kernel.convert().mesh_index_count(*fine_mesh.value);
    auto fine_components = kernel.convert().mesh_connected_components(*fine_mesh.value);
    auto fine_out_of_range = kernel.convert().mesh_has_out_of_range_indices(*fine_mesh.value);
    auto fine_degenerate = kernel.convert().mesh_has_degenerate_triangles(*fine_mesh.value);
    auto fine_report = kernel.convert().inspect_mesh(*fine_mesh.value);
    if (coarse_triangles.status != axiom::StatusCode::Ok || fine_triangles.status != axiom::StatusCode::Ok ||
        coarse_vertices.status != axiom::StatusCode::Ok ||
        fine_vertices.status != axiom::StatusCode::Ok || fine_indices.status != axiom::StatusCode::Ok ||
        fine_components.status != axiom::StatusCode::Ok || fine_out_of_range.status != axiom::StatusCode::Ok ||
        fine_degenerate.status != axiom::StatusCode::Ok || fine_report.status != axiom::StatusCode::Ok ||
        !coarse_triangles.value.has_value() || !fine_triangles.value.has_value() ||
        !coarse_vertices.value.has_value() ||
        !fine_vertices.value.has_value() || !fine_indices.value.has_value() ||
        !fine_components.value.has_value() || !fine_out_of_range.value.has_value() ||
        !fine_degenerate.value.has_value() || !fine_report.value.has_value()) {
        std::cerr << "failed to query mesh statistics\n";
        return 1;
    }
    if (*fine_triangles.value <= *coarse_triangles.value ||
        *fine_vertices.value <= *coarse_vertices.value ||
        *fine_indices.value != (*fine_triangles.value * 3) || *fine_components.value != 1 ||
        *fine_out_of_range.value || *fine_degenerate.value ||
        fine_report.value->triangle_count != *fine_triangles.value ||
        fine_report.value->vertex_count != *fine_vertices.value ||
        !fine_report.value->is_indexed || fine_report.value->connected_components != 1 ||
        fine_report.value->has_out_of_range_indices || fine_report.value->has_degenerate_triangles) {
        std::cerr << "unexpected tessellation density behavior\n";
        std::cerr << "coarse: tris=" << *coarse_triangles.value << " verts=" << *coarse_vertices.value << "\n";
        std::cerr << "fine: tris=" << *fine_triangles.value << " verts=" << *fine_vertices.value
                  << " indices=" << *fine_indices.value << " comps=" << *fine_components.value
                  << " oor=" << (*fine_out_of_range.value ? "true" : "false")
                  << " deg=" << (*fine_degenerate.value ? "true" : "false") << "\n";
        std::cerr << "fine_report: tris=" << fine_report.value->triangle_count
                  << " verts=" << fine_report.value->vertex_count
                  << " comps=" << fine_report.value->connected_components
                  << " indexed=" << (fine_report.value->is_indexed ? "true" : "false")
                  << " oor=" << (fine_report.value->has_out_of_range_indices ? "true" : "false")
                  << " deg=" << (fine_report.value->has_degenerate_triangles ? "true" : "false")
                  << "\n";
        return 1;
    }
    // `weld_shading_split_angle_deg` < 180：在盒体棱边处按法向折边保留更多顶点；默认 180 与历史「按位置合并」一致。
    {
        axiom::TessellationOptions weld_merge {0.05, 5.0, true, false, 180.0};
        axiom::TessellationOptions weld_crease {0.05, 5.0, true, false, 35.0};
        auto m_m = kernel.convert().brep_to_mesh(*box.value, weld_merge);
        auto m_c = kernel.convert().brep_to_mesh(*box.value, weld_crease);
        auto v_m = kernel.convert().mesh_vertex_count(*m_m.value);
        auto v_c = kernel.convert().mesh_vertex_count(*m_c.value);
        if (m_m.status != axiom::StatusCode::Ok || m_c.status != axiom::StatusCode::Ok ||
            !m_m.value.has_value() || !m_c.value.has_value() ||
            v_m.status != axiom::StatusCode::Ok || v_c.status != axiom::StatusCode::Ok ||
            !v_m.value.has_value() || !v_c.value.has_value() ||
            *v_c.value <= *v_m.value) {
            std::cerr << "expected crease-preserving weld to retain more vertices than merge-all on box\n";
            return 1;
        }
    }
    const auto mesh_report_path = std::filesystem::temp_directory_path() / "axiom_mesh_report.json";
    auto export_mesh_report = kernel.convert().export_mesh_report_json(*fine_mesh.value, mesh_report_path.string());
    if (export_mesh_report.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to export mesh report json\n";
        return 1;
    }
    std::ifstream mesh_report_in {mesh_report_path};
    std::string mesh_report_text((std::istreambuf_iterator<char>(mesh_report_in)),
                                 std::istreambuf_iterator<char>());
    if (mesh_report_text.find("\"triangle_count\":" + std::to_string(*fine_triangles.value)) == std::string::npos ||
        mesh_report_text.find("\"has_out_of_range_indices\":false") == std::string::npos ||
        mesh_report_text.find("\"mesh_label\":\"mesh_from_box\"") == std::string::npos ||
        mesh_report_text.find("\"tessellation_strategy\":\"primitive_box\"") == std::string::npos ||
        mesh_report_text.find("\"tessellation_budget_digest\"") == std::string::npos ||
        mesh_report_text.find("\"has_texcoords\":false") == std::string::npos) {
        std::cerr << "mesh report json content is unexpected\n";
        std::filesystem::remove(mesh_report_path);
        return 1;
    }

    // Optional display seam support: allow generating UVs for export pipelines.
    // This is a display-oriented option and may intentionally keep seams.
    auto uv_mesh = kernel.convert().brep_to_mesh(*box.value, {0.05, 5.0, true, true});
    if (uv_mesh.status != axiom::StatusCode::Ok || !uv_mesh.value.has_value()) {
        std::cerr << "failed to convert brep to mesh with generated texcoords\n";
        std::filesystem::remove(mesh_report_path);
        return 1;
    }
    const auto uv_report_path = std::filesystem::temp_directory_path() / "axiom_mesh_report_uv.json";
    auto export_uv_report = kernel.convert().export_mesh_report_json(*uv_mesh.value, uv_report_path.string());
    if (export_uv_report.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to export uv mesh report json\n";
        std::filesystem::remove(mesh_report_path);
        return 1;
    }
    std::ifstream uv_report_in {uv_report_path};
    std::string uv_report_text((std::istreambuf_iterator<char>(uv_report_in)),
                               std::istreambuf_iterator<char>());
    if (uv_report_text.find("\"has_texcoords\":true") == std::string::npos) {
        std::cerr << "uv mesh report should indicate has_texcoords=true\n";
        std::filesystem::remove(mesh_report_path);
        std::filesystem::remove(uv_report_path);
        return 1;
    }
    {
        axiom::TessellationOptions seam_opts {0.05, 5.0, true, true};
        seam_opts.uv_parametric_seam = true;
        auto seam_mesh = kernel.convert().brep_to_mesh(*box.value, seam_opts);
        if (seam_mesh.status != axiom::StatusCode::Ok || !seam_mesh.value.has_value()) {
            std::cerr << "brep_to_mesh with uv_parametric_seam failed\n";
            std::filesystem::remove(mesh_report_path);
            std::filesystem::remove(uv_report_path);
            return 1;
        }
        const auto seam_report_path = std::filesystem::temp_directory_path() / "axiom_mesh_report_seam.json";
        auto exp_seam = kernel.convert().export_mesh_report_json(*seam_mesh.value, seam_report_path.string());
        if (exp_seam.status != axiom::StatusCode::Ok) {
            std::cerr << "export seam mesh report failed\n";
            std::filesystem::remove(mesh_report_path);
            std::filesystem::remove(uv_report_path);
            return 1;
        }
        std::ifstream seam_in {seam_report_path};
        std::string seam_text((std::istreambuf_iterator<char>(seam_in)), std::istreambuf_iterator<char>());
        if (seam_text.find("\"uv_parametric_seam\":true") == std::string::npos) {
            std::cerr << "mesh report digest should record uv_parametric_seam\n";
            std::filesystem::remove(mesh_report_path);
            std::filesystem::remove(uv_report_path);
            std::filesystem::remove(seam_report_path);
            return 1;
        }
        std::filesystem::remove(seam_report_path);
    }
    std::filesystem::remove(uv_report_path);

    {
        auto pl = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto trimmed_plane = kernel.surfaces().make_trimmed(*pl.value, 0.0, 10.0, 0.0, 20.0);
        auto l0 = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = kernel.curves().make_line({10.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = kernel.curves().make_line({10.0, 20.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = kernel.curves().make_line({0.0, 20.0, 0.0}, {0.0, -1.0, 0.0});
        if (pl.status != axiom::StatusCode::Ok || !pl.value.has_value() ||
            trimmed_plane.status != axiom::StatusCode::Ok || !trimmed_plane.value.has_value() ||
            l0.status != axiom::StatusCode::Ok || !l0.value.has_value() ||
            l1.status != axiom::StatusCode::Ok || !l1.value.has_value() ||
            l2.status != axiom::StatusCode::Ok || !l2.value.has_value() ||
            l3.status != axiom::StatusCode::Ok || !l3.value.has_value()) {
            std::cerr << "trimmed-plane tessellation: geometry init failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto ttx = kernel.topology().begin_transaction();
        auto tv0 = ttx.create_vertex({0.0, 0.0, 0.0});
        auto tv1 = ttx.create_vertex({10.0, 0.0, 0.0});
        auto tv2 = ttx.create_vertex({10.0, 20.0, 0.0});
        auto tv3 = ttx.create_vertex({0.0, 20.0, 0.0});
        auto te0 = ttx.create_edge(*l0.value, *tv0.value, *tv1.value);
        auto te1 = ttx.create_edge(*l1.value, *tv1.value, *tv2.value);
        auto te2 = ttx.create_edge(*l2.value, *tv2.value, *tv3.value);
        auto te3 = ttx.create_edge(*l3.value, *tv3.value, *tv0.value);
        auto tc0 = ttx.create_coedge(*te0.value, false);
        auto tc1 = ttx.create_coedge(*te1.value, false);
        auto tc2 = ttx.create_coedge(*te2.value, false);
        auto tc3 = ttx.create_coedge(*te3.value, false);
        if (tv0.status != axiom::StatusCode::Ok || tv1.status != axiom::StatusCode::Ok ||
            tv2.status != axiom::StatusCode::Ok || tv3.status != axiom::StatusCode::Ok ||
            te0.status != axiom::StatusCode::Ok || te1.status != axiom::StatusCode::Ok ||
            te2.status != axiom::StatusCode::Ok || te3.status != axiom::StatusCode::Ok ||
            tc0.status != axiom::StatusCode::Ok || tc1.status != axiom::StatusCode::Ok ||
            tc2.status != axiom::StatusCode::Ok || tc3.status != axiom::StatusCode::Ok ||
            !tv0.value.has_value() || !tv1.value.has_value() || !tv2.value.has_value() || !tv3.value.has_value() ||
            !te0.value.has_value() || !te1.value.has_value() || !te2.value.has_value() || !te3.value.has_value() ||
            !tc0.value.has_value() || !tc1.value.has_value() || !tc2.value.has_value() || !tc3.value.has_value()) {
            std::cerr << "trimmed-plane tessellation: topology txn failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        const std::array<axiom::CoedgeId, 4> trim_coedges {
            *tc0.value, *tc1.value, *tc2.value, *tc3.value};
        auto tloop = ttx.create_loop(trim_coedges);
        auto tface = ttx.create_face(*trimmed_plane.value, *tloop.value, {});
        const std::array<axiom::FaceId, 1> trim_faces {*tface.value};
        auto tshell = ttx.create_shell(trim_faces);
        const std::array<axiom::ShellId, 1> trim_shells {*tshell.value};
        auto tbody = ttx.create_body(trim_shells);
        if (tloop.status != axiom::StatusCode::Ok || tface.status != axiom::StatusCode::Ok ||
            tshell.status != axiom::StatusCode::Ok || tbody.status != axiom::StatusCode::Ok ||
            !tloop.value.has_value() || !tface.value.has_value() || !tshell.value.has_value() ||
            !tbody.value.has_value()) {
            std::cerr << "trimmed-plane tessellation: face/shell/body creation failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto trim_commit = ttx.commit();
        if (trim_commit.status != axiom::StatusCode::Ok) {
            std::cerr << "trimmed-plane tessellation: commit failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto trim_mesh =
            kernel.convert().brep_to_mesh(*tbody.value, {0.4, 25.0, true});
        if (trim_mesh.status != axiom::StatusCode::Ok || !trim_mesh.value.has_value()) {
            std::cerr << "trimmed-plane brep_to_mesh failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto trim_tris = kernel.convert().mesh_triangle_count(*trim_mesh.value);
        auto trim_insp = kernel.convert().inspect_mesh(*trim_mesh.value);
        if (trim_tris.status != axiom::StatusCode::Ok || !trim_tris.value.has_value() ||
            trim_insp.status != axiom::StatusCode::Ok || !trim_insp.value.has_value() ||
            *trim_tris.value < 32 ||
            trim_insp.value->tessellation_strategy != "owned_topo_welded") {
            std::cerr << "trimmed-plane should use topo patch tessellation with strategy metadata\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
    }

    // BSpline 拓扑面：参数域网格三角化（误差敏感分段）应优于平面扇三角的三角形数量。
    {
        auto bsurf = kernel.surfaces().make_bspline(
            {{{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}}});
        auto l0 = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto l1 = kernel.curves().make_line({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        auto l2 = kernel.curves().make_line({1.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
        auto l3 = kernel.curves().make_line({0.0, 1.0, 0.0}, {0.0, -1.0, 0.0});
        if (bsurf.status != axiom::StatusCode::Ok || !bsurf.value.has_value() ||
            l0.status != axiom::StatusCode::Ok || !l0.value.has_value() ||
            l1.status != axiom::StatusCode::Ok || !l1.value.has_value() ||
            l2.status != axiom::StatusCode::Ok || !l2.value.has_value() ||
            l3.status != axiom::StatusCode::Ok || !l3.value.has_value()) {
            std::cerr << "bspline-surface tessellation: geometry init failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto btx = kernel.topology().begin_transaction();
        auto bv0 = btx.create_vertex({0.0, 0.0, 0.0});
        auto bv1 = btx.create_vertex({1.0, 0.0, 0.0});
        auto bv2 = btx.create_vertex({1.0, 1.0, 0.0});
        auto bv3 = btx.create_vertex({0.0, 1.0, 0.0});
        auto be0 = btx.create_edge(*l0.value, *bv0.value, *bv1.value);
        auto be1 = btx.create_edge(*l1.value, *bv1.value, *bv2.value);
        auto be2 = btx.create_edge(*l2.value, *bv2.value, *bv3.value);
        auto be3 = btx.create_edge(*l3.value, *bv3.value, *bv0.value);
        auto bc0 = btx.create_coedge(*be0.value, false);
        auto bc1 = btx.create_coedge(*be1.value, false);
        auto bc2 = btx.create_coedge(*be2.value, false);
        auto bc3 = btx.create_coedge(*be3.value, false);
        if (bv0.status != axiom::StatusCode::Ok || !bv0.value.has_value() ||
            bv1.status != axiom::StatusCode::Ok || !bv1.value.has_value() ||
            bv2.status != axiom::StatusCode::Ok || !bv2.value.has_value() ||
            bv3.status != axiom::StatusCode::Ok || !bv3.value.has_value() ||
            be0.status != axiom::StatusCode::Ok || !be0.value.has_value() ||
            be1.status != axiom::StatusCode::Ok || !be1.value.has_value() ||
            be2.status != axiom::StatusCode::Ok || !be2.value.has_value() ||
            be3.status != axiom::StatusCode::Ok || !be3.value.has_value() ||
            bc0.status != axiom::StatusCode::Ok || !bc0.value.has_value() ||
            bc1.status != axiom::StatusCode::Ok || !bc1.value.has_value() ||
            bc2.status != axiom::StatusCode::Ok || !bc2.value.has_value() ||
            bc3.status != axiom::StatusCode::Ok || !bc3.value.has_value()) {
            std::cerr << "bspline-surface tessellation: topology txn failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        const std::array<axiom::CoedgeId, 4> bcoedges {*bc0.value, *bc1.value, *bc2.value, *bc3.value};
        auto bloop = btx.create_loop(bcoedges);
        auto bface = btx.create_face(*bsurf.value, *bloop.value, {});
        const std::array<axiom::FaceId, 1> bfaces {*bface.value};
        auto bshell = btx.create_shell(bfaces);
        const std::array<axiom::ShellId, 1> bshells {*bshell.value};
        auto bbody = btx.create_body(bshells);
        if (bloop.status != axiom::StatusCode::Ok || bface.status != axiom::StatusCode::Ok ||
            bshell.status != axiom::StatusCode::Ok || bbody.status != axiom::StatusCode::Ok ||
            !bloop.value.has_value() || !bface.value.has_value() || !bshell.value.has_value() ||
            !bbody.value.has_value()) {
            std::cerr << "bspline-surface tessellation: body creation failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto bcommit = btx.commit();
        if (bcommit.status != axiom::StatusCode::Ok) {
            std::cerr << "bspline-surface tessellation: commit failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        axiom::TessellationOptions bs_tes {0.12, 18.0, true};
        auto bs_mesh = kernel.convert().brep_to_mesh(*bbody.value, bs_tes);
        if (bs_mesh.status != axiom::StatusCode::Ok || !bs_mesh.value.has_value()) {
            std::cerr << "bspline-surface brep_to_mesh failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto bs_tris = kernel.convert().mesh_triangle_count(*bs_mesh.value);
        auto bs_insp = kernel.convert().inspect_mesh(*bs_mesh.value);
        if (bs_tris.status != axiom::StatusCode::Ok || !bs_tris.value.has_value() ||
            bs_insp.status != axiom::StatusCode::Ok || !bs_insp.value.has_value() ||
            *bs_tris.value < 8 || bs_insp.value->tessellation_strategy != "owned_topo_welded") {
            std::cerr << "bspline face should tessellate with parametric grid (more than fan)\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        // 张量积 patch：`angular_error` 收紧应使网格更密（与 `segments_for_tensor_direction` 一致）。
        auto bs_ang_coarse = kernel.convert().brep_to_mesh(*bbody.value, {0.12, 48.0, true});
        auto bs_ang_fine = kernel.convert().brep_to_mesh(*bbody.value, {0.12, 4.0, true});
        if (bs_ang_coarse.status != axiom::StatusCode::Ok || !bs_ang_coarse.value.has_value() ||
            bs_ang_fine.status != axiom::StatusCode::Ok || !bs_ang_fine.value.has_value()) {
            std::cerr << "bspline angular tessellation probe failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto bs_tc = kernel.convert().mesh_triangle_count(*bs_ang_coarse.value);
        auto bs_tf = kernel.convert().mesh_triangle_count(*bs_ang_fine.value);
        if (bs_tc.status != axiom::StatusCode::Ok || !bs_tc.value.has_value() ||
            bs_tf.status != axiom::StatusCode::Ok || !bs_tf.value.has_value() ||
            *bs_tf.value <= *bs_tc.value) {
            std::cerr << "bspline tessellation should refine when angular_error tightens (same chordal)\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        // Patch 弦高/曲率选项写入网格 digest（与缓存键一致）；盒体路径三角形数可能相同，不断言增量。
        axiom::TessellationOptions dig0 {0.08, 12.0, true};
        dig0.refine_patch_chordal_max_passes = 0;
        dig0.use_principal_curvature_refinement = false;
        axiom::TessellationOptions dig1 = dig0;
        dig1.refine_patch_chordal_max_passes = 5;
        dig1.use_principal_curvature_refinement = true;
        auto dig_mesh0 = kernel.convert().brep_to_mesh(*box.value, dig0);
        auto dig_mesh1 = kernel.convert().brep_to_mesh(*box.value, dig1);
        if (dig_mesh0.status != axiom::StatusCode::Ok || dig_mesh1.status != axiom::StatusCode::Ok ||
            !dig_mesh0.value.has_value() || !dig_mesh1.value.has_value()) {
            std::cerr << "brep_to_mesh with patch refine digest options failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        const auto dig_path0 = std::filesystem::temp_directory_path() / "axiom_mesh_digest0.json";
        const auto dig_path1 = std::filesystem::temp_directory_path() / "axiom_mesh_digest1.json";
        if (kernel.convert().export_mesh_report_json(*dig_mesh0.value, dig_path0.string()).status !=
                axiom::StatusCode::Ok ||
            kernel.convert().export_mesh_report_json(*dig_mesh1.value, dig_path1.string()).status !=
                axiom::StatusCode::Ok) {
            std::cerr << "export mesh report for digest options failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        std::ifstream din0 {dig_path0};
        std::ifstream din1 {dig_path1};
        std::string dtext0((std::istreambuf_iterator<char>(din0)), std::istreambuf_iterator<char>());
        std::string dtext1((std::istreambuf_iterator<char>(din1)), std::istreambuf_iterator<char>());
        if (dtext0.find("\"refine_patch_chordal_max_passes\":0") == std::string::npos ||
            dtext0.find("\"use_principal_curvature_refinement\":false") == std::string::npos ||
            dtext1.find("\"refine_patch_chordal_max_passes\":5") == std::string::npos ||
            dtext1.find("\"use_principal_curvature_refinement\":true") == std::string::npos) {
            std::cerr << "mesh report should embed patch chordal/curvature flags in tessellation_budget_digest\n";
            std::filesystem::remove(mesh_report_path);
            std::filesystem::remove(dig_path0);
            std::filesystem::remove(dig_path1);
            return 1;
        }
        std::filesystem::remove(dig_path0);
        std::filesystem::remove(dig_path1);
    }

    // 线性扫掠曲面：派生面参数域网格（mesh_from_face_derived_patch）应优于平面扇。
    {
        auto prof = kernel.curves().make_line_segment({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto sw_surf = kernel.surfaces().make_swept_linear(*prof.value, {0.0, 0.0, 1.0}, 2.0);
        auto e0 = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        auto e1 = kernel.curves().make_line({1.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        auto e2 = kernel.curves().make_line({1.0, 0.0, 2.0}, {-1.0, 0.0, 0.0});
        auto e3 = kernel.curves().make_line({0.0, 0.0, 2.0}, {0.0, 0.0, -1.0});
        if (prof.status != axiom::StatusCode::Ok || !prof.value.has_value() ||
            sw_surf.status != axiom::StatusCode::Ok || !sw_surf.value.has_value() ||
            e0.status != axiom::StatusCode::Ok || !e0.value.has_value() ||
            e1.status != axiom::StatusCode::Ok || !e1.value.has_value() ||
            e2.status != axiom::StatusCode::Ok || !e2.value.has_value() ||
            e3.status != axiom::StatusCode::Ok || !e3.value.has_value()) {
            std::cerr << "swept-surface tessellation: geometry init failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto sw_tx = kernel.topology().begin_transaction();
        auto sv0 = sw_tx.create_vertex({0.0, 0.0, 0.0});
        auto sv1 = sw_tx.create_vertex({1.0, 0.0, 0.0});
        auto sv2 = sw_tx.create_vertex({1.0, 0.0, 2.0});
        auto sv3 = sw_tx.create_vertex({0.0, 0.0, 2.0});
        auto se0 = sw_tx.create_edge(*e0.value, *sv0.value, *sv1.value);
        auto se1 = sw_tx.create_edge(*e1.value, *sv1.value, *sv2.value);
        auto se2 = sw_tx.create_edge(*e2.value, *sv2.value, *sv3.value);
        auto se3 = sw_tx.create_edge(*e3.value, *sv3.value, *sv0.value);
        auto sc0 = sw_tx.create_coedge(*se0.value, false);
        auto sc1 = sw_tx.create_coedge(*se1.value, false);
        auto sc2 = sw_tx.create_coedge(*se2.value, false);
        auto sc3 = sw_tx.create_coedge(*se3.value, false);
        if (sv0.status != axiom::StatusCode::Ok || !sv0.value.has_value() ||
            sv1.status != axiom::StatusCode::Ok || !sv1.value.has_value() ||
            sv2.status != axiom::StatusCode::Ok || !sv2.value.has_value() ||
            sv3.status != axiom::StatusCode::Ok || !sv3.value.has_value() ||
            se0.status != axiom::StatusCode::Ok || !se0.value.has_value() ||
            se1.status != axiom::StatusCode::Ok || !se1.value.has_value() ||
            se2.status != axiom::StatusCode::Ok || !se2.value.has_value() ||
            se3.status != axiom::StatusCode::Ok || !se3.value.has_value() ||
            sc0.status != axiom::StatusCode::Ok || !sc0.value.has_value() ||
            sc1.status != axiom::StatusCode::Ok || !sc1.value.has_value() ||
            sc2.status != axiom::StatusCode::Ok || !sc2.value.has_value() ||
            sc3.status != axiom::StatusCode::Ok || !sc3.value.has_value()) {
            std::cerr << "swept-surface tessellation: topology txn failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        const std::array<axiom::CoedgeId, 4> sw_coedges {*sc0.value, *sc1.value, *sc2.value, *sc3.value};
        auto sw_loop = sw_tx.create_loop(sw_coedges);
        auto sw_face = sw_tx.create_face(*sw_surf.value, *sw_loop.value, {});
        const std::array<axiom::FaceId, 1> sw_faces {*sw_face.value};
        auto sw_shell = sw_tx.create_shell(sw_faces);
        const std::array<axiom::ShellId, 1> sw_shells {*sw_shell.value};
        auto sw_body = sw_tx.create_body(sw_shells);
        if (sw_loop.status != axiom::StatusCode::Ok || sw_face.status != axiom::StatusCode::Ok ||
            sw_shell.status != axiom::StatusCode::Ok || sw_body.status != axiom::StatusCode::Ok ||
            !sw_loop.value.has_value() || !sw_face.value.has_value() || !sw_shell.value.has_value() ||
            !sw_body.value.has_value()) {
            std::cerr << "swept-surface tessellation: body creation failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto sw_commit = sw_tx.commit();
        if (sw_commit.status != axiom::StatusCode::Ok) {
            std::cerr << "swept-surface tessellation: commit failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        axiom::TessellationOptions sw_tes {0.12, 18.0, true};
        auto sw_mesh = kernel.convert().brep_to_mesh(*sw_body.value, sw_tes);
        if (sw_mesh.status != axiom::StatusCode::Ok || !sw_mesh.value.has_value()) {
            std::cerr << "swept-surface brep_to_mesh failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto sw_tris = kernel.convert().mesh_triangle_count(*sw_mesh.value);
        auto sw_insp = kernel.convert().inspect_mesh(*sw_mesh.value);
        if (sw_tris.status != axiom::StatusCode::Ok || !sw_tris.value.has_value() ||
            sw_insp.status != axiom::StatusCode::Ok || !sw_insp.value.has_value() ||
            *sw_tris.value < 8 || sw_insp.value->tessellation_strategy != "owned_topo_welded") {
            std::cerr << "swept linear surface should use derived parametric patch tessellation\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
    }

    // 解析 Revolved 曲面 + 四边形环：`sweeps().revolve` 物化体多为平面三角片，此处覆盖 `mesh_from_face_derived_patch`。
    {
        const double pi = 3.14159265358979323846;
        axiom::Axis3 axis_z {{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
        auto gen = kernel.curves().make_line_segment({1.0, 0.0, 0.0}, {2.0, 0.0, 0.0});
        auto rev_surf = kernel.surfaces().make_revolved(*gen.value, axis_z, pi);
        if (gen.status != axiom::StatusCode::Ok || !gen.value.has_value() ||
            rev_surf.status != axiom::StatusCode::Ok || !rev_surf.value.has_value()) {
            std::cerr << "revolved-surface tessellation: geometry init failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        const auto corner_uvs = std::array<std::pair<double, double>, 4> {{
            {pi * 0.15, 0.0},
            {pi * 0.65, 0.0},
            {pi * 0.65, 1.0},
            {pi * 0.15, 1.0},
        }};
        std::array<axiom::Point3, 4> corners {};
        for (std::size_t i = 0; i < 4; ++i) {
            auto ev = kernel.surface_service().eval(*rev_surf.value, corner_uvs[i].first, corner_uvs[i].second, 0);
            if (ev.status != axiom::StatusCode::Ok || !ev.value.has_value()) {
                std::cerr << "revolved-surface tessellation: surface eval failed\n";
                std::filesystem::remove(mesh_report_path);
                return 1;
            }
            corners[i] = ev.value->point;
        }
        auto le0 = kernel.curves().make_line_segment(corners[0], corners[1]);
        auto le1 = kernel.curves().make_line_segment(corners[1], corners[2]);
        auto le2 = kernel.curves().make_line_segment(corners[2], corners[3]);
        auto le3 = kernel.curves().make_line_segment(corners[3], corners[0]);
        if (le0.status != axiom::StatusCode::Ok || !le0.value.has_value() ||
            le1.status != axiom::StatusCode::Ok || !le1.value.has_value() ||
            le2.status != axiom::StatusCode::Ok || !le2.value.has_value() ||
            le3.status != axiom::StatusCode::Ok || !le3.value.has_value()) {
            std::cerr << "revolved-surface tessellation: boundary curves failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto rv_tx = kernel.topology().begin_transaction();
        auto rv0 = rv_tx.create_vertex(corners[0]);
        auto rv1 = rv_tx.create_vertex(corners[1]);
        auto rv2 = rv_tx.create_vertex(corners[2]);
        auto rv3 = rv_tx.create_vertex(corners[3]);
        auto re0 = rv_tx.create_edge(*le0.value, *rv0.value, *rv1.value);
        auto re1 = rv_tx.create_edge(*le1.value, *rv1.value, *rv2.value);
        auto re2 = rv_tx.create_edge(*le2.value, *rv2.value, *rv3.value);
        auto re3 = rv_tx.create_edge(*le3.value, *rv3.value, *rv0.value);
        auto rc0 = rv_tx.create_coedge(*re0.value, false);
        auto rc1 = rv_tx.create_coedge(*re1.value, false);
        auto rc2 = rv_tx.create_coedge(*re2.value, false);
        auto rc3 = rv_tx.create_coedge(*re3.value, false);
        if (rv0.status != axiom::StatusCode::Ok || !rv0.value.has_value() ||
            rv1.status != axiom::StatusCode::Ok || !rv1.value.has_value() ||
            rv2.status != axiom::StatusCode::Ok || !rv2.value.has_value() ||
            rv3.status != axiom::StatusCode::Ok || !rv3.value.has_value() ||
            re0.status != axiom::StatusCode::Ok || !re0.value.has_value() ||
            re1.status != axiom::StatusCode::Ok || !re1.value.has_value() ||
            re2.status != axiom::StatusCode::Ok || !re2.value.has_value() ||
            re3.status != axiom::StatusCode::Ok || !re3.value.has_value() ||
            rc0.status != axiom::StatusCode::Ok || !rc0.value.has_value() ||
            rc1.status != axiom::StatusCode::Ok || !rc1.value.has_value() ||
            rc2.status != axiom::StatusCode::Ok || !rc2.value.has_value() ||
            rc3.status != axiom::StatusCode::Ok || !rc3.value.has_value()) {
            std::cerr << "revolved-surface tessellation: topology txn failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        const std::array<axiom::CoedgeId, 4> rv_coedges {*rc0.value, *rc1.value, *rc2.value, *rc3.value};
        auto rv_loop = rv_tx.create_loop(rv_coedges);
        auto rv_face = rv_tx.create_face(*rev_surf.value, *rv_loop.value, {});
        const std::array<axiom::FaceId, 1> rv_faces {*rv_face.value};
        auto rv_shell = rv_tx.create_shell(rv_faces);
        const std::array<axiom::ShellId, 1> rv_shells {*rv_shell.value};
        auto rv_body = rv_tx.create_body(rv_shells);
        if (rv_loop.status != axiom::StatusCode::Ok || rv_face.status != axiom::StatusCode::Ok ||
            rv_shell.status != axiom::StatusCode::Ok || rv_body.status != axiom::StatusCode::Ok ||
            !rv_loop.value.has_value() || !rv_face.value.has_value() || !rv_shell.value.has_value() ||
            !rv_body.value.has_value()) {
            std::cerr << "revolved-surface tessellation: body creation failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto rv_commit = rv_tx.commit();
        if (rv_commit.status != axiom::StatusCode::Ok) {
            std::cerr << "revolved-surface tessellation: commit failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        axiom::TessellationOptions rev_tes {0.12, 18.0, true};
        auto rev_mesh = kernel.convert().brep_to_mesh(*rv_body.value, rev_tes);
        if (rev_mesh.status != axiom::StatusCode::Ok || !rev_mesh.value.has_value()) {
            std::cerr << "revolved-surface brep_to_mesh failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto rev_tris = kernel.convert().mesh_triangle_count(*rev_mesh.value);
        auto rev_insp = kernel.convert().inspect_mesh(*rev_mesh.value);
        if (rev_tris.status != axiom::StatusCode::Ok || !rev_tris.value.has_value() ||
            rev_insp.status != axiom::StatusCode::Ok || !rev_insp.value.has_value() ||
            *rev_tris.value < 8 || rev_insp.value->tessellation_strategy != "owned_topo_welded" ||
            rev_insp.value->has_degenerate_triangles) {
            std::cerr << "revolved analytic surface should use derived parametric patch tessellation\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
    }

    // 体级三角化缓存：相同体与相同 options 第二次调用应命中。
    {
        const axiom::TessellationOptions cache_opts {0.251, 31.0, true};
        auto s0 = kernel.tessellation_cache_stats();
        if (s0.status != axiom::StatusCode::Ok || !s0.value.has_value()) {
            std::cerr << "tessellation_cache_stats query failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        const auto h0 = s0.value->body_cache_hits;
        const auto m0 = s0.value->body_cache_misses;
        auto c1 = kernel.convert().brep_to_mesh(*box.value, cache_opts);
        if (c1.status != axiom::StatusCode::Ok || !c1.value.has_value()) {
            std::cerr << "cache probe brep_to_mesh (1) failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        auto s1 = kernel.tessellation_cache_stats();
        auto c2 = kernel.convert().brep_to_mesh(*box.value, cache_opts);
        auto s2 = kernel.tessellation_cache_stats();
        if (s1.status != axiom::StatusCode::Ok || !s1.value.has_value() ||
            s2.status != axiom::StatusCode::Ok || !s2.value.has_value() ||
            c2.status != axiom::StatusCode::Ok || !c2.value.has_value()) {
            std::cerr << "tessellation_cache_stats or cache probe (2) failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        if (s1.value->body_cache_misses != m0 + 1 || s2.value->body_cache_hits != h0 + 1 ||
            s2.value->body_cache_misses != m0 + 1) {
            std::cerr << "body tessellation cache hit/miss counters unexpected\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        const auto tcs_path = std::filesystem::temp_directory_path() / "axiom_tess_cache_stats.json";
        auto exp_tcs = kernel.export_tessellation_cache_stats_json(tcs_path.string());
        if (exp_tcs.status != axiom::StatusCode::Ok) {
            std::cerr << "export_tessellation_cache_stats_json failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        std::ifstream tcs_in {tcs_path};
        std::string tcs_text((std::istreambuf_iterator<char>(tcs_in)), std::istreambuf_iterator<char>());
        if (tcs_text.find("\"body_cache_hits\":") == std::string::npos ||
            tcs_text.find("\"face_cache_misses\":") == std::string::npos) {
            std::cerr << "tessellation cache stats json missing expected fields\n";
            std::filesystem::remove(mesh_report_path);
            std::filesystem::remove(tcs_path);
            return 1;
        }
        std::filesystem::remove(tcs_path);
    }

    const std::vector<axiom::BodyId> batch_body_ids {*box.value};
    auto batch_mesh = kernel.convert().brep_to_mesh_batch(batch_body_ids, {0.1, 10.0, true});
    if (batch_mesh.status != axiom::StatusCode::Ok || !batch_mesh.value.has_value() || batch_mesh.value->size() != 1) {
        std::cerr << "failed to run batch brep to mesh conversion\n";
        return 1;
    }
    auto bad_mesh = kernel.convert().brep_to_mesh(*box.value, {0.0, 5.0, true});
    if (bad_mesh.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected invalid tessellation options to fail\n";
        return 1;
    }
    auto bad_mesh_diag = kernel.diagnostics().get(bad_mesh.diagnostic_id);
    if (bad_mesh_diag.status != axiom::StatusCode::Ok || !bad_mesh_diag.value.has_value() ||
        !has_issue_code(*bad_mesh_diag.value, axiom::diag_codes::kCoreParameterOutOfRange)) {
        std::cerr << "invalid tessellation options should carry parameter diagnostic\n";
        return 1;
    }
    axiom::TessellationOptions bad_refine {0.1, 10.0, true};
    bad_refine.refine_patch_chordal_max_passes = 99;
    auto bad_refine_mesh = kernel.convert().brep_to_mesh(*box.value, bad_refine);
    if (bad_refine_mesh.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected invalid refine_patch_chordal_max_passes to fail\n";
        std::filesystem::remove(mesh_report_path);
        return 1;
    }
    auto invalid_mesh_triangles = kernel.convert().mesh_triangle_count(axiom::MeshId {999999});
    auto invalid_mesh_report = kernel.convert().inspect_mesh(axiom::MeshId {999999});
    auto invalid_export_mesh_report =
        kernel.convert().export_mesh_report_json(axiom::MeshId {999999}, mesh_report_path.string());
    if (invalid_mesh_triangles.status != axiom::StatusCode::InvalidInput ||
        invalid_mesh_report.status != axiom::StatusCode::InvalidInput ||
        invalid_export_mesh_report.status == axiom::StatusCode::Ok) {
        std::cerr << "mesh statistics should reject invalid mesh handle\n";
        std::filesystem::remove(mesh_report_path);
        return 1;
    }

    auto brep = kernel.convert().mesh_to_brep(*mesh.value);
    if (brep.status != axiom::StatusCode::Ok || !brep.value.has_value()) {
        std::cerr << "failed to convert mesh back to brep\n";
        return 1;
    }

    auto rt_brep_mesh = kernel.convert().verify_brep_mesh_round_trip(*box.value, {0.1, 10.0, true});
    if (rt_brep_mesh.status != axiom::StatusCode::Ok || !rt_brep_mesh.value.has_value() || !rt_brep_mesh.value->passed ||
        rt_brep_mesh.value->tessellation_strategy != "primitive_box" ||
        rt_brep_mesh.value->tessellation_budget_digest.find("chordal_error") == std::string::npos ||
        rt_brep_mesh.value->tessellation_budget_digest.find("angular_error_deg") == std::string::npos) {
        std::cerr << "brep-mesh round-trip report should pass with strategy and budget digest\n";
        return 1;
    }
    const auto rt_json_path = std::filesystem::temp_directory_path() / "axiom_round_trip_report.json";
    auto exp_rt = kernel.export_round_trip_report_json(*rt_brep_mesh.value, rt_json_path.string());
    if (exp_rt.status != axiom::StatusCode::Ok) {
        std::cerr << "export_round_trip_report_json failed\n";
        std::filesystem::remove(mesh_report_path);
        return 1;
    }
    std::ifstream rt_json_in {rt_json_path};
    std::string rt_json_text((std::istreambuf_iterator<char>(rt_json_in)), std::istreambuf_iterator<char>());
    if (rt_json_text.find("\"passed\":true") == std::string::npos ||
        rt_json_text.find("\"budget\":") == std::string::npos ||
        rt_json_text.find("\"normal_angle_deg_tol\":") == std::string::npos ||
        rt_json_text.find("\"chordal_error_basis\":") == std::string::npos ||
        rt_json_text.find("\"angular_error_basis_deg\":") == std::string::npos ||
        rt_json_text.find("\"tessellation_strategy\":\"primitive_box\"") == std::string::npos ||
        rt_json_text.find("\"tessellation_budget_digest\":") == std::string::npos ||
        rt_json_text.find("\"normal_deviation_measured\":false") == std::string::npos ||
        rt_json_text.find("\"max_normal_angle_deg_delta\":") == std::string::npos) {
        std::cerr << "round-trip report json export content unexpected\n";
        std::filesystem::remove(mesh_report_path);
        std::filesystem::remove(rt_json_path);
        return 1;
    }
    if (kernel.export_round_trip_report_json(*rt_brep_mesh.value, "").status != axiom::StatusCode::InvalidInput) {
        std::cerr << "export_round_trip_report_json should reject empty path\n";
        std::filesystem::remove(mesh_report_path);
        std::filesystem::remove(rt_json_path);
        return 1;
    }
    std::filesystem::remove(rt_json_path);

    // 圆柱：round-trip 点/包围盒门禁；法向预算待按「侧面径向 vs 端盖轴向」分类后再启用（当前统一径向对比会在端盖上得到 ~90° 伪差）。
    {
        auto cyl = kernel.primitives().cylinder({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 2.0, 5.0);
        if (cyl.status != axiom::StatusCode::Ok || !cyl.value.has_value()) {
            std::cerr << "failed to create cylinder for round-trip budget\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        // mesh_to_brep 重建圆柱的径向近似在粗网格下可至 O(1) 量级；弦高预算需覆盖该点误差门禁。
        axiom::TessellationOptions cyl_tes {2.5, 12.0, false};
        auto rt_cyl = kernel.convert().verify_brep_mesh_round_trip(*cyl.value, cyl_tes);
        if (rt_cyl.status != axiom::StatusCode::Ok || !rt_cyl.value.has_value() || !rt_cyl.value->passed ||
            rt_cyl.value->normal_deviation_measured || rt_cyl.value->tessellation_strategy != "primitive_cylinder") {
            std::cerr << "cylinder brep-mesh round-trip failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
    }

    {
        axiom::TessellationOptions tes_budget {0.05, 6.0, true};
        auto cb = kernel.conversion_error_budget_for_tessellation(tes_budget);
        if (cb.status != axiom::StatusCode::Ok || !cb.value.has_value() ||
            !approx(cb.value->chordal_error_basis, 0.05) || !approx(cb.value->angular_error_basis_deg, 6.0) ||
            !approx(cb.value->max_point_abs_tol, 0.05) || !approx(cb.value->bbox_abs_tol, 0.1) ||
            !approx(cb.value->normal_angle_deg_tol, 6.0)) {
            std::cerr << "conversion_error_budget_for_tessellation mapping unexpected\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        const auto cb_json_path = std::filesystem::temp_directory_path() / "axiom_conversion_budget.json";
        if (kernel.export_conversion_error_budget_json(tes_budget, cb_json_path.string()).status != axiom::StatusCode::Ok) {
            std::cerr << "export_conversion_error_budget_json failed\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
        std::ifstream cb_in {cb_json_path};
        std::string cb_text((std::istreambuf_iterator<char>(cb_in)), std::istreambuf_iterator<char>());
        if (cb_text.find("\"derivation\":\"tessellation_options_v1\"") == std::string::npos ||
            cb_text.find("\"bbox_abs_tol\":") == std::string::npos) {
            std::cerr << "conversion error budget json export unexpected\n";
            std::filesystem::remove(mesh_report_path);
            std::filesystem::remove(cb_json_path);
            return 1;
        }
        std::filesystem::remove(cb_json_path);
        auto cb_bad = kernel.conversion_error_budget_for_tessellation({0.0, 5.0, true});
        if (cb_bad.status != axiom::StatusCode::InvalidInput) {
            std::cerr << "conversion_error_budget_for_tessellation should reject invalid options\n";
            std::filesystem::remove(mesh_report_path);
            return 1;
        }
    }

    auto brep_bbox = kernel.representation().bbox_of_body(*brep.value);
    if (brep_bbox.status != axiom::StatusCode::Ok || !brep_bbox.value.has_value()) {
        std::cerr << "failed to query converted brep bbox\n";
        return 1;
    }

    if (!approx(brep_bbox.value->max.z, 30.0)) {
        std::cerr << "mesh to brep bbox was not preserved\n";
        return 1;
    }
    const std::vector<axiom::MeshId> batch_mesh_ids {*mesh.value, *fine_mesh.value};
    auto batch_brep = kernel.convert().mesh_to_brep_batch(batch_mesh_ids);
    if (batch_brep.status != axiom::StatusCode::Ok || !batch_brep.value.has_value() || batch_brep.value->size() != 2) {
        std::cerr << "failed to run batch mesh to brep conversion\n";
        return 1;
    }

    auto implicit_invalid = kernel.convert().implicit_to_mesh(axiom::ImplicitFieldId {0}, {});
    if (implicit_invalid.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "implicit conversion should reject invalid field id\n";
        return 1;
    }
    auto implicit_invalid_diag = kernel.diagnostics().get(implicit_invalid.diagnostic_id);
    if (implicit_invalid_diag.status != axiom::StatusCode::Ok || !implicit_invalid_diag.value.has_value() ||
        !has_issue_code(*implicit_invalid_diag.value, axiom::diag_codes::kCoreInvalidHandle)) {
        std::cerr << "invalid implicit id should carry invalid handle diagnostic\n";
        return 1;
    }

    auto implicit_mesh = kernel.convert().implicit_to_mesh(axiom::ImplicitFieldId {1}, {0.2, 15.0, true});
    if (implicit_mesh.status != axiom::StatusCode::Ok || !implicit_mesh.value.has_value()) {
        std::cerr << "failed to convert implicit field to mesh with valid options\n";
        return 1;
    }
    auto rt_mesh_brep = kernel.convert().verify_mesh_brep_round_trip(*implicit_mesh.value, {0.2, 15.0, true});
    if (rt_mesh_brep.status != axiom::StatusCode::Ok || !rt_mesh_brep.value.has_value() ||
        !rt_mesh_brep.value->passed) {
        std::cerr << "mesh-brep round-trip report should pass\n";
        return 1;
    }
    // `verify_mesh_brep_round_trip` 内部会 `mesh_to_brep` 并再 `brep_to_mesh`：绑定后应返回同一嵌入网格（策略为原网格记录上的 implicit_bbox_proxy）。
    const auto& rt_mb = *rt_mesh_brep.value;
    const bool rt_strat_ok =
        rt_mb.tessellation_strategy == "bbox_proxy" || rt_mb.tessellation_strategy == "implicit_bbox_proxy";
    if (!rt_strat_ok || rt_mb.tessellation_budget_digest.find("chordal_error") == std::string::npos) {
        std::cerr << "mesh-brep round-trip report should carry strategy and budget digest\n";
        return 1;
    }
    auto implicit_brep = kernel.convert().mesh_to_brep(*implicit_mesh.value);
    if (implicit_brep.status != axiom::StatusCode::Ok || !implicit_brep.value.has_value()) {
        std::cerr << "failed to convert implicit mesh back to brep\n";
        return 1;
    }
    auto embed_roundtrip = kernel.convert().brep_to_mesh(*implicit_brep.value, {0.2, 15.0, true});
    if (embed_roundtrip.status != axiom::StatusCode::Ok || !embed_roundtrip.value.has_value() ||
        !implicit_mesh.value.has_value() || *embed_roundtrip.value != *implicit_mesh.value) {
        std::cerr << "MeshRep should return embedded mesh id after mesh_to_brep linkage\n";
        return 1;
    }
    auto implicit_bbox = kernel.representation().bbox_of_body(*implicit_brep.value);
    if (implicit_bbox.status != axiom::StatusCode::Ok || !implicit_bbox.value.has_value() ||
        !approx(implicit_bbox.value->max.x, 2.0)) {
        std::cerr << "implicit conversion bbox did not reflect tessellation options\n";
        return 1;
    }

    auto section_ok = kernel.query().section(*box.value, {{0.0, 0.0, 15.0}, {0.0, 0.0, 1.0}});
    auto section_fail = kernel.query().section(*box.value, {{0.0, 0.0, 100.0}, {0.0, 0.0, 1.0}});
    if (section_ok.status != axiom::StatusCode::Ok || !section_ok.value.has_value()) {
        std::cerr << "expected valid section result\n";
        return 1;
    }
    if (section_fail.status == axiom::StatusCode::Ok) {
        std::cerr << "expected section failure for non-intersecting plane\n";
        return 1;
    }

    auto box2 = kernel.primitives().box({20.0, 0.0, 0.0}, 10.0, 20.0, 30.0);
    if (box2.status != axiom::StatusCode::Ok || !box2.value.has_value()) {
        std::cerr << "failed to create second box\n";
        return 1;
    }

    auto min_distance = kernel.query().min_distance(*box.value, *box2.value);
    if (min_distance.status != axiom::StatusCode::Ok || !min_distance.value.has_value()) {
        std::cerr << "failed to query min distance\n";
        return 1;
    }

    if (!approx(*min_distance.value, 10.0)) {
        std::cerr << "unexpected min distance result\n";
        return 1;
    }

    // Local re-tessellation (Topo-driven) should work for derived bodies with owned topology.
    // Use a placeholder boolean result body which materializes minimal owned topology.
    axiom::BooleanOptions bool_opts;
    bool_opts.diagnostics = true;
    auto boolean_result = kernel.booleans().run(axiom::BooleanOp::Union, *box.value, *box2.value, bool_opts);
    if (boolean_result.status != axiom::StatusCode::Ok || !boolean_result.value.has_value() ||
        boolean_result.value->status != axiom::StatusCode::Ok || boolean_result.value->output.value == 0) {
        std::cerr << "failed to create derived body for local tessellation\n";
        return 1;
    }
    auto derived_body = boolean_result.value->output;
    auto derived_faces = kernel.topology().query().faces_of_body(derived_body);
    if (derived_faces.status != axiom::StatusCode::Ok || !derived_faces.value.has_value() || derived_faces.value->empty()) {
        std::cerr << "expected derived body to have owned faces\n";
        return 1;
    }
    std::array<axiom::FaceId, 1> dirty_faces{derived_faces.value->front()};
    auto local_mesh = kernel.convert().brep_to_mesh_local(derived_body, dirty_faces, {0.2, 15.0, true});
    if (local_mesh.status != axiom::StatusCode::Ok || !local_mesh.value.has_value()) {
        std::cerr << "local brep to mesh tessellation failed\n";
        return 1;
    }
    auto local_mesh_tris = kernel.convert().mesh_triangle_count(*local_mesh.value);
    if (local_mesh_tris.status != axiom::StatusCode::Ok || !local_mesh_tris.value.has_value() || *local_mesh_tris.value == 0) {
        std::cerr << "local tessellation should generate triangles\n";
        return 1;
    }

    const auto out_path = std::filesystem::temp_directory_path() / "axiom_representation_io_test.step";
    axiom::ExportOptions export_options;
    export_options.embed_metadata = true;

    auto exported = kernel.io().export_step(*box.value, out_path.string(), export_options);
    if (exported.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to export step with metadata\n";
        return 1;
    }

    auto imported = kernel.io().import_step(out_path.string(), {});
    if (imported.status != axiom::StatusCode::Ok || !imported.value.has_value()) {
        std::cerr << "failed to import step with metadata\n";
        return 1;
    }

    auto imported_bbox = kernel.representation().bbox_of_body(*imported.value);
    if (imported_bbox.status != axiom::StatusCode::Ok || !imported_bbox.value.has_value()) {
        std::cerr << "failed to query imported bbox\n";
        return 1;
    }

    if (!approx(imported_bbox.value->max.x, 10.0) || !approx(imported_bbox.value->max.y, 20.0) ||
        !approx(imported_bbox.value->max.z, 30.0)) {
        std::cerr << "imported step metadata bbox was not preserved\n";
        return 1;
    }

    axiom::KernelConfig tolerant_config;
    tolerant_config.tolerance.linear = 0.1;
    // resolve_linear_tolerance 会按 max_local 夹紧；默认 1e-3 会使 0.1 无法生效。
    tolerant_config.tolerance.max_local = 1.0;
    axiom::Kernel tolerant_kernel(tolerant_config);
    auto tolerant_box = tolerant_kernel.primitives().box({0.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
    if (tolerant_box.status != axiom::StatusCode::Ok || !tolerant_box.value.has_value()) {
        std::cerr << "failed to create tolerant box\n";
        return 1;
    }
    auto near_boundary = tolerant_kernel.representation().classify_point(*tolerant_box.value, {1.05, 0.5, 0.5});
    if (near_boundary.status != axiom::StatusCode::Ok || !near_boundary.value.has_value() || !*near_boundary.value) {
        std::cerr << "classification should honor linear tolerance near boundary\n";
        return 1;
    }

    auto sphere = kernel.primitives().sphere({0.0, 0.0, 0.0}, 2.0);
    auto cone = kernel.primitives().cone({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, std::acos(-1.0) / 6.0, 6.0);
    if (sphere.status != axiom::StatusCode::Ok || cone.status != axiom::StatusCode::Ok ||
        !sphere.value.has_value() || !cone.value.has_value()) {
        std::cerr << "failed to create primitive bodies for io metadata test\n";
        return 1;
    }

    const auto sphere_path = std::filesystem::temp_directory_path() / "axiom_representation_io_sphere.step";
    const auto cone_path = std::filesystem::temp_directory_path() / "axiom_representation_io_cone.step";
    if (kernel.io().export_step(*sphere.value, sphere_path.string(), export_options).status != axiom::StatusCode::Ok ||
        kernel.io().export_step(*cone.value, cone_path.string(), export_options).status != axiom::StatusCode::Ok) {
        std::cerr << "failed to export primitive metadata step\n";
        return 1;
    }

    auto imported_sphere = kernel.io().import_step(sphere_path.string(), {});
    auto imported_cone = kernel.io().import_step(cone_path.string(), {});
    if (imported_sphere.status != axiom::StatusCode::Ok || imported_cone.status != axiom::StatusCode::Ok ||
        !imported_sphere.value.has_value() || !imported_cone.value.has_value()) {
        std::cerr << "failed to import primitive metadata step\n";
        return 1;
    }

    auto imported_sphere_props = kernel.query().mass_properties(*imported_sphere.value);
    auto imported_cone_props = kernel.query().mass_properties(*imported_cone.value);
    auto sphere_props = kernel.query().mass_properties(*sphere.value);
    auto cone_props = kernel.query().mass_properties(*cone.value);
    if (imported_sphere_props.status != axiom::StatusCode::Ok || imported_cone_props.status != axiom::StatusCode::Ok ||
        sphere_props.status != axiom::StatusCode::Ok || cone_props.status != axiom::StatusCode::Ok ||
        !imported_sphere_props.value.has_value() || !imported_cone_props.value.has_value()) {
        std::cerr << "failed to query imported primitive mass properties\n";
        return 1;
    }

    if (!sphere_props.value.has_value() || !cone_props.value.has_value() ||
        !approx(imported_sphere_props.value->volume, sphere_props.value->volume) ||
        !approx(imported_cone_props.value->volume, cone_props.value->volume, 1e-3) ||
        !approx(imported_cone_props.value->centroid.z, cone_props.value->centroid.z, 1e-5)) {
        std::cerr << "imported primitive metadata was not restored into mass properties\n";
        return 1;
    }

    std::filesystem::remove(out_path);
    std::filesystem::remove(sphere_path);
    std::filesystem::remove(cone_path);
    std::filesystem::remove(mesh_report_path);
    return 0;
}
