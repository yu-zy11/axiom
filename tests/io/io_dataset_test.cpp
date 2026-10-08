#include <array>
#include <chrono>
#include <cmath>
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
    std::string current_case="metadata";
    const auto fail=[&](int line) {
        std::cerr << "Stage 5 exit corpus case=" << current_case << " line=" << line << "\n";
        return false;
    };
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
        current_case=format.name;
        axiom::Kernel kernel;
        axiom::ImportOptions options;
        options.auto_repair = true;
        const auto path = data_dir / (std::string("s5_io_precision_subset.")+format.name);
        const auto imported = (kernel.io().*format.read)(path.string(), options);
        if (!imported.value || imported.status != axiom::StatusCode::Ok) return fail(__LINE__);
        const auto check_record = [&](axiom::BodyId body) {
            const auto kind = kernel.representation().kind_of_body(body);
            const auto shells = kernel.topology().query().shell_count_of_body(body);
            const auto bbox = kernel.representation().bbox_of_body(body);
            if (!kind.value || *kind.value != axiom::RepKind::ExactBRep || !shells.value || *shells.value != 0 ||
                !bbox.value || kernel.validate().validate_all(body,axiom::ValidationMode::Standard).status !=
                    axiom::StatusCode::Ok) return fail(__LINE__);
            const std::array<double,6> actual {bbox.value->min.x,bbox.value->min.y,bbox.value->min.z,
                bbox.value->max.x,bbox.value->max.y,bbox.value->max.z};
            return actual == bounds;
        };
        if (!check_record(*imported.value)) return fail(__LINE__);
        // ReportOnly explicitly exercises repair preflight without promoting
        // metadata into an owned boundary. A Box tag without owned topology
        // has no native tessellation certificate and must reject conversion.
        const auto repaired=kernel.repair().auto_repair(*imported.value,axiom::RepairMode::ReportOnly);
        if (!repaired.value || repaired.status!=axiom::StatusCode::Ok ||
            repaired.value->status!=axiom::StatusCode::Ok || repaired.value->output!=*imported.value ||
            !check_record(repaired.value->output)) return fail(__LINE__);
        const auto unavailable=kernel.convert().brep_to_mesh(repaired.value->output,{});
        const auto conversion_diagnostic=kernel.diagnostics().get(unavailable.diagnostic_id);
        bool missing_topology=false;
        if (conversion_diagnostic.value) for (const auto& issue : conversion_diagnostic.value->issues)
            missing_topology=missing_topology || (issue.code==axiom::diag_codes::kTesFailure &&
                issue.stage=="rep.tessellation.topology");
        const auto mass=kernel.topology().query().body_mass_properties(repaired.value->output);
        const auto mass_diagnostic=kernel.diagnostics().get(mass.diagnostic_id);
        bool no_solid=false;
        if (mass_diagnostic.value) for (const auto& issue : mass_diagnostic.value->issues)
            no_solid=no_solid || (issue.code==axiom::diag_codes::kTopoShellNotClosed &&
                issue.stage=="query.mass_properties.empty_gate");
        if (unavailable.status==axiom::StatusCode::Ok || unavailable.value || !missing_topology ||
            mass.status!=axiom::StatusCode::InvalidTopology || mass.value || !no_solid) return fail(__LINE__);
        // Existing Safe metadata repair materializes a synthetic bbox boundary.
        // Its display triangles and metadata round-trip do not recover the
        // source's BRep, and the mass support gate must keep that distinction.
        const auto display_body=kernel.repair().auto_repair(*imported.value,axiom::RepairMode::Safe);
        if (!display_body.value || display_body.status!=axiom::StatusCode::Ok ||
            display_body.value->output==*imported.value || !check_record(*imported.value)) return fail(__LINE__);
        const auto check_display=[&](axiom::BodyId body,const char* strategy) {
            const auto bbox=kernel.representation().bbox_of_body(body);
            const auto display=kernel.convert().brep_to_mesh(body,{});
            if (!bbox.value || !display.value || display.status!=axiom::StatusCode::Ok ||
                kernel.validate().validate_all(body,axiom::ValidationMode::Standard).status!=axiom::StatusCode::Ok)
                return fail(__LINE__);
            const std::array<double,6> actual {bbox.value->min.x,bbox.value->min.y,bbox.value->min.z,
                bbox.value->max.x,bbox.value->max.y,bbox.value->max.z};
            const auto inspection=kernel.convert().inspect_mesh(*display.value);
            const auto mass=kernel.topology().query().body_mass_properties(body);
            const auto diagnostic=kernel.diagnostics().get(mass.diagnostic_id);
            bool unsupported=false;
            if (diagnostic.value) for (const auto& issue : diagnostic.value->issues)
                unsupported=unsupported || (issue.code==axiom::diag_codes::kCoreOperationUnsupported &&
                    issue.stage=="query.mass_properties.support_gate");
            return actual==bounds && inspection.value && inspection.value->triangle_count>0 &&
                !inspection.value->has_out_of_range_indices && !inspection.value->has_degenerate_triangles &&
                inspection.value->tessellation_strategy==strategy && mass.status==axiom::StatusCode::NotImplemented &&
                !mass.value && unsupported;
        };
        const auto shells=kernel.topology().query().shell_count_of_body(display_body.value->output);
        if (!shells.value || *shells.value==0 || !check_display(display_body.value->output,"owned_topo_welded"))
            return fail(__LINE__);
        if (std::string(format.name)=="iges") {
            std::ifstream original {path};
            std::string text {std::istreambuf_iterator<char>(original),std::istreambuf_iterator<char>()};
            const auto label=text.find("AXIOM_LABEL ");
            const auto end=text.find('\n',label);
            if (label==std::string::npos || end==std::string::npos) return fail(__LINE__);
            text.replace(label,end-label,"AXIOM_LABEL 1H 2H 3H 4H "+std::string(100,'x'));
            text+="AXIOM_STEP_SCHEMA "+std::string(100,'y')+"\n";
            const auto variant=root/"valid_hollerith_label.iges";
            { std::ofstream write {variant}; write<<text; }
            const auto relabelled=kernel.io().import_iges(variant.string(),options);
            if (!relabelled.value || relabelled.status!=axiom::StatusCode::Ok || !check_record(*relabelled.value))
                return fail(__LINE__);
        }
        const auto output = root / (std::string("roundtrip.")+format.name);
        {
            GlobalLocaleGuard locale {std::locale(std::locale::classic(),new CommaDecimal)};
            if ((kernel.io().*format.write)(display_body.value->output,output.string(),{}).status != axiom::StatusCode::Ok)
                return fail(__LINE__);
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
                if (start==std::string::npos || colon==std::string::npos) return fail(__LINE__);
                std::istringstream number {text.substr(colon+1)};
                number.imbue(std::locale::classic());
                double actual=0;
                if (!(number>>actual) || actual!=expected[i]) return fail(__LINE__);
            }
        } else {
            const std::array<const char*,3> names {"AXIOM_ORIGIN","AXIOM_PARAMS","AXIOM_BBOX"};
            for (std::size_t field=0;field<names.size();++field) {
                const auto start=text.find(names[field]);
                if (start==std::string::npos) return fail(__LINE__);
                std::istringstream numbers {text.substr(start+std::string(names[field]).size())};
                numbers.imbue(std::locale::classic());
                const auto count=field==2 ? bounds.size() : origin.size();
                for (std::size_t i=0;i<count;++i) {
                    double actual=0;
                    const double expected=field==0 ? origin[i] : field==1 ? params[i] : bounds[i];
                    if (!(numbers>>actual) || actual!=expected) return fail(__LINE__);
                }
            }
        }
        const auto reread=(kernel.io().*format.read)(output.string(),options);
        if (!reread.value || reread.status!=axiom::StatusCode::Ok || !check_record(*reread.value) ||
            !check_display(*reread.value,"bbox_proxy")) return fail(__LINE__);
    }
    current_case="stl_safe_lifecycle";
    axiom::Kernel kernel;
    const auto tetra=kernel.io().import_stl((data_dir/"s5_io_precision_tetra.stl").string(),{});
    if (!tetra.value || tetra.status!=axiom::StatusCode::Ok) return fail(__LINE__);
    if (kernel.validate().validate_all(*tetra.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok)
        return fail(__LINE__);
    const auto original_mesh=kernel.convert().brep_to_mesh(*tetra.value,{});
    const auto repaired=kernel.repair().auto_repair(*tetra.value,axiom::RepairMode::Safe);
    if (!original_mesh.value || !repaired.value || repaired.status!=axiom::StatusCode::Ok ||
        repaired.value->status!=axiom::StatusCode::Ok || repaired.value->output==*tetra.value ||
        kernel.validate().validate_all(repaired.value->output,axiom::ValidationMode::Strict).status!=
            axiom::StatusCode::Ok) return fail(__LINE__);
    const auto repaired_mesh=kernel.convert().brep_to_mesh(repaired.value->output,{});
    const auto retained_mesh=kernel.convert().brep_to_mesh(*tetra.value,{});
    const auto repair_diagnostic=kernel.diagnostics().get(repaired.diagnostic_id);
    bool repair_validated=false;
    if (repair_diagnostic.value) for (const auto& issue : repair_diagnostic.value->issues)
        repair_validated=repair_validated || (issue.code==axiom::diag_codes::kHealRepairValidated &&
            issue.stage=="heal.auto_repair.post_validate");
    if (!repaired_mesh.value || *repaired_mesh.value==*original_mesh.value ||
        retained_mesh.value!=original_mesh.value || !repair_validated) return fail(__LINE__);
    const auto output=root/"roundtrip.stl";
    {
        GlobalLocaleGuard locale {std::locale(std::locale::classic(),new CommaDecimal)};
        if (kernel.io().export_stl(repaired.value->output,output.string(),{}).status!=axiom::StatusCode::Ok)
            return fail(__LINE__);
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
    if (source.size()!=36 || read_vertices(output)!=source) return fail(__LINE__);
    const auto retained=root/"retained_source.stl";
    if (kernel.io().export_stl(*tetra.value,retained.string(),{}).status!=axiom::StatusCode::Ok ||
        read_vertices(retained)!=source) return fail(__LINE__);
    // Independent ASCII STL integration uses the fixed translated tetrahedron,
    // not production mass/round-trip reports or the enclosing bbox (volume 24).
    const auto check_reference=[&](const std::filesystem::path& path) {
        const auto coordinates=read_vertices(path);
        if (coordinates.size()!=36) return fail(__LINE__);
        const std::array<long double,3> origin {0.12345678901234566L,-1.2345678901234567L,
                                               3.456789012345679L};
        long double volume=0,area=0;
        std::array<long double,3> moment{};
        for (std::size_t facet=0;facet<coordinates.size();facet+=9) {
            std::array<std::array<long double,3>,3> point{};
            for (std::size_t vertex=0;vertex<3;++vertex)
                for (std::size_t axis=0;axis<3;++axis)
                    point[vertex][axis]=static_cast<long double>(coordinates[facet+3*vertex+axis])-origin[axis];
            const auto& p=point[0];
            const auto& q=point[1];
            const auto& r=point[2];
            const long double tetra=(p[0]*(q[1]*r[2]-q[2]*r[1])+p[1]*(q[2]*r[0]-q[0]*r[2])+
                                     p[2]*(q[0]*r[1]-q[1]*r[0]))/6;
            volume+=tetra;
            for (std::size_t axis=0;axis<3;++axis) moment[axis]+=tetra*(p[axis]+q[axis]+r[axis])/4;
            const std::array<long double,3> u {q[0]-p[0],q[1]-p[1],q[2]-p[2]};
            const std::array<long double,3> v {r[0]-p[0],r[1]-p[1],r[2]-p[2]};
            const long double nx=u[1]*v[2]-u[2]*v[1],ny=u[2]*v[0]-u[0]*v[2],nz=u[0]*v[1]-u[1]*v[0];
            const long double twice_area=std::sqrt(nx*nx+ny*ny+nz*nz);
            if (!(twice_area>0) || !std::isfinite(twice_area)) return fail(__LINE__);
            area+=twice_area/2;
        }
        return std::abs(volume-4)<=1e-12L && std::abs(area-(13+std::sqrt(244.0L)/2))<=1e-12L &&
            std::abs(moment[0]/volume-0.5L)<=1e-12L && std::abs(moment[1]/volume-0.75L)<=1e-12L &&
            std::abs(moment[2]/volume-1)<=1e-12L;
    };
    if (!check_reference(data_dir/"s5_io_precision_tetra.stl") || !check_reference(output) ||
        !check_reference(retained)) return fail(__LINE__);
    const auto reread=kernel.io().import_stl(output.string(),{});
    if (reread.status!=axiom::StatusCode::Ok) return fail(__LINE__);
    for (const auto body : {tetra.value,std::optional<axiom::BodyId>{repaired.value->output},reread.value}) {
        if (!body) return fail(__LINE__);
        const auto kind=kernel.representation().kind_of_body(*body);
        const auto mesh=kernel.convert().brep_to_mesh(*body,{});
        if (!kind.value || *kind.value!=axiom::RepKind::MeshRep || !mesh.value ||
            kernel.convert().mesh_triangle_count(*mesh.value).value!=std::optional<std::uint64_t>{4} ||
            kernel.validate().validate_all(*body,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok)
            return fail(__LINE__);
        const auto inspection=kernel.convert().inspect_mesh(*mesh.value);
        const auto shells=kernel.topology().query().shell_count_of_body(*body);
        if (!inspection.value || inspection.value->triangle_count!=4 ||
            inspection.value->has_out_of_range_indices || inspection.value->has_degenerate_triangles ||
            inspection.value->tessellation_strategy!="io_import_stl" ||
            shells.value!=std::optional<std::uint64_t>{0}) return fail(__LINE__);
        const auto geometry=root/("checked_"+std::to_string(body->value)+".stl");
        if (kernel.io().export_stl(*body,geometry.string(),{}).status!=axiom::StatusCode::Ok ||
            read_vertices(geometry)!=source || !check_reference(geometry)) return fail(__LINE__);
    }
    // Removing the attached source snapshot must fail repair instead of
    // constructing a box. Seed a separate live cache and valid Eval binding
    // after the deliberate removal to make rollback isolation observable.
    current_case="stl_missing_mesh_rollback";
    if (kernel.clear_mesh_store().status!=axiom::StatusCode::Ok) return fail(__LINE__);
    const auto sentinel=kernel.primitives().box({10,10,10},2,3,4);
    if (!sentinel.value) return fail(__LINE__);
    const auto sentinel_mesh=kernel.convert().brep_to_mesh(*sentinel.value,{});
    const auto bound=kernel.eval_graph().register_node(axiom::NodeKind::Analysis,
        "body:"+std::to_string(tetra.value->value));
    if (!sentinel_mesh.value || !bound.value ||
        kernel.eval_graph().recompute(*bound.value).status!=axiom::StatusCode::Ok) return fail(__LINE__);
    const auto model_counts=[&] {
        return std::array {kernel.object_count_total().value,kernel.geometry_count().value,
            kernel.topology_count().value,kernel.body_count().value,kernel.mesh_count().value,
            kernel.cache_entry_count().value,kernel.eval_node_count().value};
    };
    const auto cache_counts=[&] {
        const auto stores=kernel.runtime_store_counts();
        if (!stores.value) return std::array<std::uint64_t,9>{};
        const auto& value=*stores.value;
        const auto& stats=value.tessellation_metrics;
        return std::array {value.mesh_records,value.tessellation_cache_entries,value.face_tessellation_cache_entries,
            stats.body_cache_hits,stats.body_cache_misses,stats.body_cache_stale_evictions,
            stats.face_cache_hits,stats.face_cache_misses,stats.face_cache_stale_evictions};
    };
    const auto counts_before=model_counts();
    const auto caches_before=cache_counts();
    const auto recompute_before=kernel.eval_graph().recompute_count(*bound.value).value;
    for (int attempt=0;attempt<2;++attempt) {
        const auto rejected=kernel.repair().auto_repair(*tetra.value,axiom::RepairMode::Safe);
        const auto diagnostic=kernel.diagnostics().get(rejected.diagnostic_id);
        bool post_failure=false;
        bool missing_mesh=false;
        if (diagnostic.value) for (const auto& issue : diagnostic.value->issues) {
            post_failure=post_failure || (issue.code==axiom::diag_codes::kHealAutoRepairFailure &&
                issue.stage=="heal.auto_repair.post_validate" && !issue.numeric_evidence.empty());
            missing_mesh=missing_mesh || (issue.code==axiom::diag_codes::kValDegenerateGeometry &&
                issue.stage=="heal.auto_repair.post_validate" && !issue.related_entities.empty());
        }
        // Standalone Heal may consume IDs during a rejected attempt; IO's outer
        // transaction owns next_id restoration. Diagnostics also remain queryable.
        if (rejected.status!=axiom::StatusCode::OperationFailed || rejected.value || !post_failure ||
            !missing_mesh || model_counts()!=counts_before || cache_counts()!=caches_before ||
            kernel.eval_graph().is_invalid(*bound.value).value!=std::optional<bool>{false} ||
            kernel.eval_graph().recompute_count(*bound.value).value!=recompute_before ||
            kernel.has_body_id(*tetra.value).value!=std::optional<bool>{true} ||
            !kernel.convert().inspect_mesh(*sentinel_mesh.value).value ||
            kernel.core_runtime_invariants_hold().value!=std::optional<bool>{true}) return fail(__LINE__);
    }
    const auto retry=kernel.io().import_stl((data_dir/"s5_io_precision_tetra.stl").string(),{});
    if (!retry.value || retry.status!=axiom::StatusCode::Ok ||
        kernel.validate().validate_all(*retry.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok)
        return fail(__LINE__);
    // Force post-validation failure after an actual mesh has been copied. This
    // complements missing-mesh rejection by exercising removal of the newly
    // attached mesh and preservation of the original mesh/Eval binding.
    current_case="stl_copied_mesh_rollback";
    axiom::KernelConfig invalid_config;
    invalid_config.tolerance.angular=0;
    axiom::Kernel rejected_kernel {invalid_config};
    axiom::ImportOptions unchecked;
    unchecked.run_validation=false;
    const auto valid_mesh_body=rejected_kernel.io().import_stl((data_dir/"s5_io_precision_tetra.stl").string(),unchecked);
    if (!valid_mesh_body.value || valid_mesh_body.status!=axiom::StatusCode::Ok) return fail(__LINE__);
    const auto actual_mesh=rejected_kernel.convert().brep_to_mesh(*valid_mesh_body.value,{});
    const auto valid_bound=rejected_kernel.eval_graph().register_node(axiom::NodeKind::Analysis,
        "body:"+std::to_string(valid_mesh_body.value->value));
    if (!actual_mesh.value || !valid_bound.value ||
        rejected_kernel.eval_graph().recompute(*valid_bound.value).status!=axiom::StatusCode::Ok) return fail(__LINE__);
    const auto copied_model_counts=[&] {
        return std::array {rejected_kernel.object_count_total().value,rejected_kernel.geometry_count().value,
            rejected_kernel.topology_count().value,rejected_kernel.body_count().value,
            rejected_kernel.mesh_count().value,rejected_kernel.cache_entry_count().value,
            rejected_kernel.eval_node_count().value};
    };
    const auto copied_counts=copied_model_counts();
    const auto copied_stores=rejected_kernel.runtime_store_counts();
    const auto copied_recompute=rejected_kernel.eval_graph().recompute_count(*valid_bound.value).value;
    const auto rejected_copy=rejected_kernel.repair().auto_repair(*valid_mesh_body.value,axiom::RepairMode::Safe);
    const auto copy_diagnostic=rejected_kernel.diagnostics().get(rejected_copy.diagnostic_id);
    bool copied_rollback=false;
    bool copied_allocations=false;
    bool tolerance_failure=false;
    if (copy_diagnostic.value) for (const auto& issue : copy_diagnostic.value->issues) {
        if (issue.code==axiom::diag_codes::kHealAutoRepairFailure && issue.stage=="heal.auto_repair.post_validate")
            for (const auto& evidence : issue.numeric_evidence) {
                copied_rollback=copied_rollback || (evidence.name=="rollback_applied" && evidence.value==1);
                copied_allocations=copied_allocations || (evidence.name=="allocated_object_count" && evidence.value>=2);
            }
        tolerance_failure=tolerance_failure || (issue.code==axiom::diag_codes::kValToleranceConflict &&
            issue.stage=="heal.auto_repair.post_validate");
    }
    const auto copied_after=rejected_kernel.runtime_store_counts();
    if (rejected_copy.status!=axiom::StatusCode::OperationFailed || rejected_copy.value || !copied_rollback ||
        !copied_allocations ||
        !tolerance_failure || copied_model_counts()!=copied_counts || !copied_stores.value || !copied_after.value ||
        copied_after.value->mesh_records!=copied_stores.value->mesh_records ||
        copied_after.value->tessellation_cache_entries!=copied_stores.value->tessellation_cache_entries ||
        copied_after.value->face_tessellation_cache_entries!=copied_stores.value->face_tessellation_cache_entries ||
        copied_after.value->tessellation_metrics.body_cache_hits!=copied_stores.value->tessellation_metrics.body_cache_hits ||
        copied_after.value->tessellation_metrics.body_cache_misses!=copied_stores.value->tessellation_metrics.body_cache_misses ||
        copied_after.value->tessellation_metrics.body_cache_stale_evictions!=
            copied_stores.value->tessellation_metrics.body_cache_stale_evictions ||
        copied_after.value->tessellation_metrics.face_cache_hits!=copied_stores.value->tessellation_metrics.face_cache_hits ||
        copied_after.value->tessellation_metrics.face_cache_misses!=copied_stores.value->tessellation_metrics.face_cache_misses ||
        copied_after.value->tessellation_metrics.face_cache_stale_evictions!=
            copied_stores.value->tessellation_metrics.face_cache_stale_evictions ||
        rejected_kernel.eval_graph().is_invalid(*valid_bound.value).value!=std::optional<bool>{false} ||
        rejected_kernel.eval_graph().recompute_count(*valid_bound.value).value!=copied_recompute ||
        rejected_kernel.convert().brep_to_mesh(*valid_mesh_body.value,{}).value!=actual_mesh.value)
        return fail(__LINE__);
    const auto preserved=root/"copied_rollback_source.stl";
    if (rejected_kernel.io().export_stl(*valid_mesh_body.value,preserved.string(),{}).status!=axiom::StatusCode::Ok ||
        read_vertices(preserved)!=source || !check_reference(preserved)) return fail(__LINE__);
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
