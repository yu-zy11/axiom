#pragma once

#include <memory>
#include <vector>

#include "axiom/diag/diagnostic_service.h"
#include "axiom/eval/eval_services.h"
#include "axiom/geo/geometry_services.h"
#include "axiom/heal/heal_services.h"
#include "axiom/io/io_service.h"
#include "axiom/math/math_services.h"
#include "axiom/ops/ops_services.h"
#include "axiom/rep/representation_conversion_service.h"
#include "axiom/topo/topology_service.h"
#include "axiom/plugin/plugin_registry.h"

namespace axiom {
namespace detail {
struct KernelState;
}

class Kernel {
public:
    explicit Kernel(const KernelConfig& config = {});
    Result<KernelConfig> config() const;
    Result<void> set_enable_diagnostics(bool enabled);
    Result<bool> enable_diagnostics() const;
    Result<void> set_linear_tolerance(Scalar value);
    Result<Scalar> linear_tolerance() const;
    Result<void> set_angular_tolerance(Scalar value);
    Result<Scalar> angular_tolerance() const;
    Result<std::uint64_t> next_object_id() const;
    Result<std::uint64_t> next_diagnostic_id() const;
    Result<std::uint64_t> object_count_total() const;
    Result<std::uint64_t> geometry_count() const;
    Result<std::uint64_t> topology_count() const;
    Result<std::uint64_t> body_count() const;
    Result<std::uint64_t> mesh_count() const;
    Result<std::uint64_t> intersection_count() const;
    Result<std::uint64_t> eval_node_count() const;
    Result<std::uint64_t> cache_entry_count() const;
    Result<void> clear_eval_caches();
    Result<void> clear_curve_eval_cache();
    Result<void> clear_surface_eval_cache();
    Result<void> clear_diagnostics_store();
    Result<void> clear_intersections_store();
    Result<void> clear_mesh_store();
    Result<bool> has_body_id(BodyId id) const;
    Result<bool> has_curve_id(CurveId id) const;
    Result<bool> has_pcurve_id(PCurveId id) const;
    Result<bool> has_surface_id(SurfaceId id) const;
    Result<bool> has_face_id(FaceId id) const;
    Result<bool> has_shell_id(ShellId id) const;
    Result<bool> has_edge_id(EdgeId id) const;
    Result<void> reset_runtime_stores();
    // capability and availability
    Result<std::vector<std::string>> services_available() const;
    Result<std::vector<std::string>> module_names() const;
    Result<bool> has_service_geometry() const;
    Result<bool> has_service_topology() const;
    Result<bool> has_service_io() const;
    Result<bool> has_service_diagnostics() const;
    Result<bool> has_service_eval() const;
    Result<bool> has_service_ops() const;
    Result<bool> has_service_heal() const;
    Result<bool> has_service_representation() const;
    Result<bool> has_service_conversion() const;
    Result<bool> has_service_math() const;
    Result<bool> has_service_geo() const;
    Result<std::vector<std::string>> io_supported_formats() const;
    Result<bool> io_can_import_format(std::string_view fmt) const;
    Result<bool> io_can_export_format(std::string_view fmt) const;
    Result<std::vector<std::string>> capability_report_lines() const;
    Result<std::string> capability_report_txt() const;
    // plugin views via kernel
    Result<std::vector<std::string>> plugin_manifest_names() const;
    Result<std::uint64_t> plugin_total_count() const;
    Result<bool> has_any_plugins() const;
    Result<std::vector<std::string>> plugin_vendors() const;
    Result<std::vector<std::string>> plugin_capabilities_histogram_lines() const;

    CurveFactory& curves();
    PCurveFactory& pcurves();
    SurfaceFactory& surfaces();
    CurveService& curve_service();
    PCurveService& pcurve_service();
    SurfaceService& surface_service();
    GeometryTransformService& geometry_transform();
    GeometryIntersectionService& geometry_intersection();
    LinearAlgebraService& linear_algebra();
    PredicateService& predicates();
    ToleranceService& tolerance();
    TopologyService& topology();
    PrimitiveService& primitives();
    SweepService& sweeps();
    BooleanService& booleans();
    ModifyService& modify();
    BlendService& blends();
    QueryService& query();
    ValidationService& validate();
    RepairService& repair();
    RepresentationService& representation();
    RepresentationConversionService& convert();
    IOService& io();
    DiagnosticService& diagnostics();
    EvalGraphService& eval_graph();
    PluginRegistry& plugins();

private:
    std::shared_ptr<detail::KernelState> state_;
    CurveFactory curve_factory_;
    PCurveFactory pcurve_factory_;
    SurfaceFactory surface_factory_;
    CurveService curve_service_;
    PCurveService pcurve_service_;
    SurfaceService surface_service_;
    GeometryTransformService geometry_transform_service_;
    GeometryIntersectionService geometry_intersection_service_;
    LinearAlgebraService linear_algebra_service_;
    PredicateService predicate_service_;
    ToleranceService tolerance_service_;
    TopologyService topology_service_;
    PrimitiveService primitive_service_;
    SweepService sweep_service_;
    BooleanService boolean_service_;
    ModifyService modify_service_;
    BlendService blend_service_;
    QueryService query_service_;
    ValidationService validation_service_;
    RepairService repair_service_;
    RepresentationService representation_service_;
    RepresentationConversionService conversion_service_;
    IOService io_service_;
    DiagnosticService diagnostic_service_;
    EvalGraphService eval_graph_service_;
};

}  // namespace axiom
