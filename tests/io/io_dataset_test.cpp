#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: axiom_io_dataset_test <absolute path to tests/data/io>\n";
        return 2;
    }
    const std::filesystem::path data_dir = argv[1];
    if (!std::filesystem::is_directory(data_dir)) {
        std::cerr << "data directory missing: " << data_dir.string() << "\n";
        return 2;
    }

    axiom::Kernel kernel;
    axiom::ImportOptions imp;
    imp.run_validation = false;

    const auto obj_path = data_dir / "triangle_vn_vt.obj";
    const auto step_path = data_dir / "minimal_step_subset.step";

    auto obj_imp = kernel.io().import_obj(obj_path.string(), imp);
    if (obj_imp.status != axiom::StatusCode::Ok || !obj_imp.value.has_value()) {
        std::cerr << "obj dataset import failed\n";
        return 1;
    }
    auto obj_bbox = kernel.representation().bbox_of_body(*obj_imp.value);
    if (obj_bbox.status != axiom::StatusCode::Ok || !obj_bbox.value.has_value() ||
        obj_bbox.value->max.x < 0.9 || obj_bbox.value->max.y < 0.9) {
        std::cerr << "obj dataset bbox unexpected\n";
        return 1;
    }

    auto step_imp = kernel.io().import_step(step_path.string(), imp);
    if (step_imp.status != axiom::StatusCode::Ok || !step_imp.value.has_value()) {
        std::cerr << "step dataset import failed\n";
        return 1;
    }

    const auto std_step_path = data_dir / "standard_step_express_stub.step";
    auto std_step_imp = kernel.io().import_step(std_step_path.string(), imp);
    if (std_step_imp.status != axiom::StatusCode::NotImplemented) {
        std::cerr << "expected NotImplemented for standard EXPRESS STEP stub\n";
        return 1;
    }
    {
        auto dr = kernel.diagnostics().get(std_step_imp.diagnostic_id);
        bool saw_err = false;
        bool saw_scan = false;
        if (dr.status == axiom::StatusCode::Ok && dr.value.has_value()) {
            for (const auto& iss : dr.value->issues) {
                if (iss.code == axiom::diag_codes::kIoStepStandardEntitiesUnsupported) {
                    saw_err = true;
                }
                if (iss.code == axiom::diag_codes::kIoStepStandardFileScanSummary) {
                    saw_scan = true;
                    if (iss.message.find("CARTESIAN_POINT") == std::string::npos) {
                        std::cerr << "STEP scan summary should mention CARTESIAN_POINT\n";
                        return 1;
                    }
                }
            }
        }
        if (!saw_err) {
            std::cerr << "standard STEP stub missing kIoStepStandardEntitiesUnsupported\n";
            return 1;
        }
        if (!saw_scan) {
            std::cerr << "standard STEP stub missing kIoStepStandardFileScanSummary\n";
            return 1;
        }
    }

    const auto std_iges_path = data_dir / "standard_iges_deck_stub.iges";
    auto std_iges_imp = kernel.io().import_iges(std_iges_path.string(), imp);
    if (std_iges_imp.status != axiom::StatusCode::NotImplemented) {
        std::cerr << "expected NotImplemented for standard IGES deck stub\n";
        return 1;
    }
    {
        auto dr = kernel.diagnostics().get(std_iges_imp.diagnostic_id);
        bool saw_err = false;
        bool saw_scan = false;
        if (dr.status == axiom::StatusCode::Ok && dr.value.has_value()) {
            for (const auto& iss : dr.value->issues) {
                if (iss.code == axiom::diag_codes::kIgesStandardEntitiesUnsupported) {
                    saw_err = true;
                }
                if (iss.code == axiom::diag_codes::kIgesStandardFileScanSummary) {
                    saw_scan = true;
                    if (iss.message.find("Directory Entry") == std::string::npos) {
                        std::cerr << "IGES scan summary should mention Directory Entry\n";
                        return 1;
                    }
                }
            }
        }
        if (!saw_err) {
            std::cerr << "standard IGES stub missing kIgesStandardEntitiesUnsupported\n";
            return 1;
        }
        if (!saw_scan) {
            std::cerr << "standard IGES stub missing kIgesStandardFileScanSummary\n";
            return 1;
        }
    }
    auto step_bbox = kernel.representation().bbox_of_body(*step_imp.value);
    if (step_bbox.status != axiom::StatusCode::Ok || !step_bbox.value.has_value() ||
        step_bbox.value->max.x < 9.9 || step_bbox.value->max.y < 19.9) {
        std::cerr << "step dataset bbox unexpected\n";
        return 1;
    }

    const auto uniq = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto tmp = std::filesystem::temp_directory_path();
    const auto out_step = tmp / ("axiom_dataset_rexport_" + uniq + ".step");
    axiom::ExportOptions exp;
    exp.embed_metadata = true;
    if (kernel.io().export_step(*step_imp.value, out_step.string(), exp).status != axiom::StatusCode::Ok) {
        std::cerr << "step re-export failed\n";
        return 1;
    }
    std::ifstream step_in(out_step);
    std::string step_text((std::istreambuf_iterator<char>(step_in)), std::istreambuf_iterator<char>());
    if (step_text.find("AXIOM_STEP_SCHEMA") == std::string::npos ||
        step_text.find("CONFIG_CONTROL_DESIGN") == std::string::npos ||
        step_text.find("AXIOM_STEP_ENTITY") == std::string::npos ||
        step_text.find("MANIFOLD_SOLID_BREP") == std::string::npos ||
        step_text.find("FILE_SCHEMA") == std::string::npos) {
        std::cerr << "re-exported step missing schema/entity markers\n";
        std::filesystem::remove(out_step);
        return 1;
    }
    std::filesystem::remove(out_step);

    auto box = kernel.primitives().box({0.0, 0.0, 0.0}, 4.0, 5.0, 6.0);
    if (box.status != axiom::StatusCode::Ok || !box.value.has_value()) {
        std::cerr << "box creation failed\n";
        return 1;
    }
    const auto stl_a = tmp / ("axiom_dataset_stl_strict_" + uniq + ".stl");
    const auto stl_b = tmp / ("axiom_dataset_stl_compat_" + uniq + ".stl");
    const auto stl_c = tmp / ("axiom_dataset_stl_report_" + uniq + ".stl");
    axiom::ExportOptions e_strict;
    e_strict.compatibility_mode = false;
    axiom::ExportOptions e_compat;
    e_compat.compatibility_mode = true;
    axiom::ExportOptions e_report;
    e_report.write_mesh_validation_report = true;
    e_report.compatibility_mode = false;
    if (kernel.io().export_stl(*box.value, stl_a.string(), e_strict).status != axiom::StatusCode::Ok) {
        std::cerr << "strict stl export failed\n";
        return 1;
    }
    if (kernel.io().export_stl(*box.value, stl_b.string(), e_compat).status != axiom::StatusCode::Ok) {
        std::cerr << "compat stl export failed\n";
        return 1;
    }
    if (kernel.io().export_stl(*box.value, stl_c.string(), e_report).status != axiom::StatusCode::Ok) {
        std::cerr << "stl with mesh report export failed\n";
        return 1;
    }
    const auto sidecar = tmp / ("axiom_dataset_stl_report_" + uniq + ".mesh_report.json");
    if (!std::filesystem::exists(sidecar)) {
        std::cerr << "mesh_report sidecar missing\n";
        std::filesystem::remove(stl_a);
        std::filesystem::remove(stl_b);
        std::filesystem::remove(stl_c);
        return 1;
    }
    std::filesystem::remove(stl_a);
    std::filesystem::remove(stl_b);
    std::filesystem::remove(stl_c);
    std::filesystem::remove(sidecar);

    const auto out_iges = tmp / ("axiom_dataset_iges_" + uniq + ".iges");
    if (kernel.io().export_iges(*step_imp.value, out_iges.string(), exp).status != axiom::StatusCode::Ok) {
        std::cerr << "iges export from step-imported body failed\n";
        return 1;
    }
    std::ifstream iges_in(out_iges);
    std::string iges_text((std::istreambuf_iterator<char>(iges_in)), std::istreambuf_iterator<char>());
    if (iges_text.find("AXIOM_IGES_ENTITY") == std::string::npos) {
        std::cerr << "iges export missing entity hint line\n";
        std::filesystem::remove(out_iges);
        return 1;
    }
    std::filesystem::remove(out_iges);

    const auto p3 = tmp / ("axiom_dataset_3mf_" + uniq + ".3mf");
    if (kernel.io().export_3mf(*box.value, p3.string(), e_compat).status != axiom::StatusCode::Ok) {
        std::cerr << "3mf export failed\n";
        return 1;
    }
    auto imp3 = kernel.io().import_3mf(p3.string(), imp);
    if (imp3.status != axiom::StatusCode::Ok || !imp3.value.has_value()) {
        std::cerr << "3mf re-import failed\n";
        std::filesystem::remove(p3);
        return 1;
    }
    auto bb3 = kernel.representation().bbox_of_body(*imp3.value);
    if (bb3.status != axiom::StatusCode::Ok || !bb3.value.has_value() || bb3.value->max.x < 3.9) {
        std::cerr << "3mf round-trip bbox unexpected\n";
        std::filesystem::remove(p3);
        return 1;
    }
    std::filesystem::remove(p3);

    return 0;
}
