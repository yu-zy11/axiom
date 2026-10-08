#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

namespace {

class CommaDecimal : public std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};

class GlobalLocaleGuard {
public:
    explicit GlobalLocaleGuard(const std::locale& value) : previous_(std::locale::global(value)) {}
    ~GlobalLocaleGuard() { std::locale::global(previous_); }
private:
    std::locale previous_;
};

// These files exchange native model-unit coordinates. Their STEP/IGES entity
// hints describe Axiom metadata only; they do not contain standard BRep faces.
bool check_stage5_fixed_precision_corpus(const std::filesystem::path& data_dir) {
    using Importer = axiom::Result<axiom::BodyId> (axiom::IOService::*)(
        std::string_view, const axiom::ImportOptions&);
    using Exporter = axiom::Result<void> (axiom::IOService::*)(
        axiom::BodyId, std::string_view, const axiom::ExportOptions&);
    struct Format { const char* name; Importer read; Exporter write; };
    const Format formats[] = {
        {"step", &axiom::IOService::import_step, &axiom::IOService::export_step},
        {"iges", &axiom::IOService::import_iges, &axiom::IOService::export_iges},
        {"brep", &axiom::IOService::import_brep, &axiom::IOService::export_brep},
    };
    const std::array<double, 3> origin {123456.123456789,-1.2345678901234567,3.456789012345679};
    const std::array<double, 3> params {2.345678901234568,3.456789012345679,4.567890123456789};
    const std::array<double, 6> bounds {origin[0],origin[1],origin[2],
        origin[0]+params[0],origin[1]+params[1],origin[2]+params[2]};
    const auto root = std::filesystem::temp_directory_path() /
        ("axiom_s5_io_precision_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    for (const auto& format : formats) {
        axiom::Kernel kernel;
        axiom::ImportOptions options;
        options.auto_repair = true;
        const auto path = data_dir / (std::string("s5_io_precision_subset.")+format.name);
        const auto imported = (kernel.io().*format.read)(path.string(), options);
        if (!imported.value || imported.status != axiom::StatusCode::Ok) return false;
        const auto check_record = [&](axiom::BodyId body) {
            const auto kind = kernel.representation().kind_of_body(body);
            const auto shells = kernel.topology().query().shell_count_of_body(body);
            const auto bbox = kernel.representation().bbox_of_body(body);
            if (!kind.value || *kind.value != axiom::RepKind::ExactBRep || !shells.value || *shells.value != 0 ||
                !bbox.value || kernel.validate().validate_all(body,axiom::ValidationMode::Standard).status !=
                    axiom::StatusCode::Ok) return false;
            const std::array<double,6> actual {bbox.value->min.x,bbox.value->min.y,bbox.value->min.z,
                bbox.value->max.x,bbox.value->max.y,bbox.value->max.z};
            return actual == bounds;
        };
        if (!check_record(*imported.value)) return false;
        if (std::string(format.name)=="iges") {
            std::ifstream original {path};
            std::string text {std::istreambuf_iterator<char>(original),std::istreambuf_iterator<char>()};
            const auto label=text.find("AXIOM_LABEL ");
            const auto end=text.find('\n',label);
            if (label==std::string::npos || end==std::string::npos) return false;
            text.replace(label,end-label,"AXIOM_LABEL 1H 2H 3H 4H "+std::string(100,'x'));
            text+="AXIOM_STEP_SCHEMA "+std::string(100,'y')+"\n";
            const auto variant=root/"valid_hollerith_label.iges";
            { std::ofstream write {variant}; write<<text; }
            const auto relabelled=kernel.io().import_iges(variant.string(),options);
            if (!relabelled.value || relabelled.status!=axiom::StatusCode::Ok || !check_record(*relabelled.value))
                return false;
        }
        const auto output = root / (std::string("roundtrip.")+format.name);
        {
            GlobalLocaleGuard locale {std::locale(std::locale::classic(),new CommaDecimal)};
            if ((kernel.io().*format.write)(*imported.value,output.string(),{}).status != axiom::StatusCode::Ok)
                return false;
        }
        // Parse serialized coordinates independently from the production parser.
        std::ifstream input {output};
        const std::string text {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
        if (std::string(format.name) == "brep") {
            const std::array<const char*,12> keys {"origin_x","origin_y","origin_z","param_a","param_b","param_c",
                "bbox_min_x","bbox_min_y","bbox_min_z","bbox_max_x","bbox_max_y","bbox_max_z"};
            const std::array<double,12> expected {origin[0],origin[1],origin[2],params[0],params[1],params[2],
                bounds[0],bounds[1],bounds[2],bounds[3],bounds[4],bounds[5]};
            for (std::size_t i=0; i<keys.size(); ++i) {
                const auto start=text.find(std::string("\"")+keys[i]+"\"");
                const auto colon=text.find(':',start);
                if (start==std::string::npos || colon==std::string::npos) return false;
                std::istringstream number {text.substr(colon+1)};
                number.imbue(std::locale::classic());
                double actual=0;
                if (!(number>>actual) || actual!=expected[i]) return false;
            }
        } else {
            const std::array<const char*,3> names {"AXIOM_ORIGIN","AXIOM_PARAMS","AXIOM_BBOX"};
            for (std::size_t field=0;field<names.size();++field) {
                const auto start=text.find(names[field]);
                if (start==std::string::npos) return false;
                std::istringstream numbers {text.substr(start+std::string(names[field]).size())};
                numbers.imbue(std::locale::classic());
                const auto count=field==2 ? bounds.size() : origin.size();
                for (std::size_t i=0;i<count;++i) {
                    double actual=0;
                    const double expected=field==0 ? origin[i] : field==1 ? params[i] : bounds[i];
                    if (!(numbers>>actual) || actual!=expected) return false;
                }
            }
        }
        const auto reread=(kernel.io().*format.read)(output.string(),options);
        if (!reread.value || reread.status!=axiom::StatusCode::Ok || !check_record(*reread.value)) return false;
    }
    axiom::Kernel kernel;
    const auto tetra=kernel.io().import_stl((data_dir/"s5_io_precision_tetra.stl").string(),{});
    if (!tetra.value || tetra.status!=axiom::StatusCode::Ok) return false;
    const auto output=root/"roundtrip.stl";
    {
        GlobalLocaleGuard locale {std::locale(std::locale::classic(),new CommaDecimal)};
        if (kernel.io().export_stl(*tetra.value,output.string(),{}).status!=axiom::StatusCode::Ok) return false;
    }
    // ASCII vertex records preserve all twelve facet coordinates exactly. STL
    // stores triangle geometry; it carries neither SI units nor BRep shells.
    const auto read_vertices=[](const std::filesystem::path& path) {
        std::ifstream input {path};
        input.imbue(std::locale::classic());
        std::vector<double> values;
        std::string token;
        while (input>>token) if (token=="vertex") {
            std::array<double,3> point{};
            if (!(input>>point[0]>>point[1]>>point[2])) return std::vector<double>{};
            values.insert(values.end(),point.begin(),point.end());
        }
        return values;
    };
    const auto source=read_vertices(data_dir/"s5_io_precision_tetra.stl");
    if (source.size()!=36 || read_vertices(output)!=source) return false;
    const auto reread=kernel.io().import_stl(output.string(),{});
    for (const auto body : {tetra.value,reread.value}) {
        if (!body) return false;
        const auto kind=kernel.representation().kind_of_body(*body);
        const auto mesh=kernel.convert().brep_to_mesh(*body,{});
        if (!kind.value || *kind.value!=axiom::RepKind::MeshRep || !mesh.value ||
            kernel.convert().mesh_triangle_count(*mesh.value).value!=std::optional<std::uint64_t>{4} ||
            kernel.validate().validate_all(*body,axiom::ValidationMode::Standard).status!=axiom::StatusCode::Ok)
            return false;
    }
    std::filesystem::remove_all(root);
    return true;
}

}  // namespace

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

    if (!check_stage5_fixed_precision_corpus(data_dir)) {
        std::cerr << "Stage 5 fixed corpus precision/support matrix regression failed\n";
        return 1;
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
