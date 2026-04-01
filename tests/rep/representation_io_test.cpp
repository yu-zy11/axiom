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
    if (rt_brep_mesh.status != axiom::StatusCode::Ok || !rt_brep_mesh.value.has_value() || !rt_brep_mesh.value->passed) {
        std::cerr << "brep-mesh round-trip report should pass\n";
        return 1;
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
    if (rt_mesh_brep.status != axiom::StatusCode::Ok || !rt_mesh_brep.value.has_value() || !rt_mesh_brep.value->passed) {
        std::cerr << "mesh-brep round-trip report should pass\n";
        return 1;
    }
    auto implicit_brep = kernel.convert().mesh_to_brep(*implicit_mesh.value);
    if (implicit_brep.status != axiom::StatusCode::Ok || !implicit_brep.value.has_value()) {
        std::cerr << "failed to convert implicit mesh back to brep\n";
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
