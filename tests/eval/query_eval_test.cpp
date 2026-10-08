#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

namespace {

bool direct_edit_query_eval_regression() {
    axiom::Kernel kernel;
    auto& query=kernel.topology().query();
    auto& eval=kernel.eval_graph();
    const auto source=kernel.primitives().box({0,0,0},4,5,6);
    if (!source.value) return false;
    const auto faces=query.faces_of_body(*source.value);
    if (!faces.value) return false;
    axiom::FaceId top {};
    for (const auto face : *faces.value) {
        const auto bbox=query.bbox_of_face(face);
        if (bbox.value && bbox.value->min.z==6 && bbox.value->max.z==6) top=face;
    }
    const auto mesh=kernel.convert().brep_to_mesh(*source.value,{});
    const auto node=eval.register_node(axiom::NodeKind::Geometry,"body:"+std::to_string(source.value->value));
    const auto consumer=eval.register_node(axiom::NodeKind::Analysis,"direct_edit:downstream");
    if (!top.value || !mesh.value || !node.value || !consumer.value ||
        eval.add_dependency(*consumer.value,*node.value).status!=axiom::StatusCode::Ok) return false;
    auto transaction=kernel.topology().begin_transaction();
    const auto savepoint=transaction.create_savepoint();
    const auto moved=kernel.modify().move_face(*source.value,top,1);
    if (!savepoint.value || !moved.value ||
        eval.is_invalid(*node.value).value!=std::optional<bool>{true} ||
        eval.is_invalid(*consumer.value).value!=std::optional<bool>{true}) return false;
    const auto mass=kernel.query().mass_properties(moved.value->output);
    const auto inside=query.locate_point(moved.value->output,{2,2.5,6.5});
    const auto source_outside=query.locate_point(*source.value,{2,2.5,6.5});
    const auto boundary=query.locate_point(moved.value->output,{2,2.5,7});
    if (!mass.value || std::abs(mass.value->volume-140)>1e-7 || std::abs(mass.value->area-166)>1e-7 ||
        !inside.value || inside.value->location!=axiom::BodyPointLocation::Inside ||
        !source_outside.value || source_outside.value->location!=axiom::BodyPointLocation::Outside ||
        !boundary.value || boundary.value->location!=axiom::BodyPointLocation::Boundary ||
        eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok) return false;
    const auto output_mesh=kernel.convert().brep_to_mesh(moved.value->output,{});
    const auto recomputes=eval.total_recompute_count().value;
    const auto failed=kernel.modify().move_face(*source.value,top,-6);
    if (failed.status==axiom::StatusCode::Ok || failed.value ||
        eval.is_invalid(*node.value).value!=std::optional<bool>{false} ||
        eval.is_invalid(*consumer.value).value!=std::optional<bool>{false} ||
        eval.total_recompute_count().value!=recomputes || !output_mesh.value ||
        transaction.rollback_to_savepoint(*savepoint.value).status!=axiom::StatusCode::Ok ||
        query.has_body(moved.value->output).value!=std::optional<bool>{false} ||
        kernel.convert().inspect_mesh(*output_mesh.value).value ||
        kernel.convert().brep_to_mesh(*source.value,{}).value!=mesh.value ||
        transaction.rollback().status!=axiom::StatusCode::Ok) return false;
    // Historical source damage is not the current edited body's boundary.
    // Chained editing validates that current boundary in private staging.
    const auto first=kernel.modify().move_face(*source.value,top,.5);
    if (!first.value) return false;
    const auto first_faces=query.faces_of_body(first.value->output);
    if (!first_faces.value) return false;
    axiom::FaceId first_top {};
    for (const auto face : *first_faces.value) {
        const auto bbox=query.bbox_of_face(face);
        if (bbox.value && bbox.value->min.z==6.5 && bbox.value->max.z==6.5) first_top=face;
    }
    auto stale_transaction=kernel.topology().begin_transaction();
    if (!first_top.value || stale_transaction.delete_face(top).status!=axiom::StatusCode::Ok) return false;
    const auto second=kernel.modify().move_face(first.value->output,first_top,.5);
    if (!second.value || query.source_bodies_of_body(second.value->output).value!=std::optional{std::vector{first.value->output}} ||
        query.source_faces_of_body(second.value->output).value!=first_faces.value ||
        kernel.validate().validate_all(second.value->output,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok ||
        !kernel.query().mass_properties(second.value->output).value ||
        stale_transaction.rollback().status!=axiom::StatusCode::Ok) return false;
    return query.has_face(top).value==std::optional<bool>{true} &&
        query.has_body(second.value->output).value==std::optional<bool>{false} &&
        kernel.query().mass_properties(first.value->output).value.has_value() &&
        kernel.core_runtime_invariants_hold().value==std::optional<bool>{true};
}

// Failure and service allocations preserve the active writer's own changes;
// rolling back derived geometry also discards its Eval and tessellation caches.
bool stage6_blend_eval_rollback_regression() {
    for (const bool fillet : {true,false}) {
        axiom::Kernel kernel;
        auto& query = kernel.topology().query();
        const auto source = kernel.primitives().box({0,0,0},4,5,6);
        const auto foreign = kernel.primitives().box({10,10,10},4,5,6);
        const auto wedge = kernel.primitives().wedge({0,0,0},4,5,6);
        const auto rounded_coordinates = kernel.primitives().box({1e16,1e16,1e16},32,40,48);
        if (!source.value || !foreign.value || !wedge.value || !rounded_coordinates.value) return false;
        const auto edges = query.edges_of_body(*source.value);
        const auto foreign_edges = query.edges_of_body(*foreign.value);
        const auto wedge_edges = query.edges_of_body(*wedge.value);
        const auto rounded_edges = query.edges_of_body(*rounded_coordinates.value);
        const auto faces = query.faces_of_body(*source.value);
        if (!edges.value || !foreign_edges.value || !wedge_edges.value || !rounded_edges.value || !faces.value || edges.value->empty()) return false;
        const auto edge = edges.value->front();
        const auto curve = query.curve_of_edge(edge);
        const auto surface = query.surface_of_face(faces.value->front());
        const auto node = kernel.eval_graph().register_node(axiom::NodeKind::Geometry,"body:"+std::to_string(source.value->value));
        const auto consumer = kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"Stage6:blend:consumer");
        if (!curve.value || !surface.value || !node.value || !consumer.value ||
            kernel.eval_graph().add_dependency(*consumer.value,*node.value).status != axiom::StatusCode::Ok ||
            kernel.eval_graph().recompute(*consumer.value).status != axiom::StatusCode::Ok ||
            !kernel.curve_service().eval(*curve.value,0.5,1).value ||
            !kernel.surface_service().eval(*surface.value,0,0,1).value ||
            !kernel.convert().brep_to_mesh(*source.value,{}).value) return false;
        const auto stores = [&] {
            const auto r = kernel.runtime_store_counts();
            if (!r.value) return std::array<std::uint64_t,10>{};
            return std::array{kernel.object_count_total().value.value_or(0),kernel.geometry_count().value.value_or(0),
                kernel.body_count().value.value_or(0),r.value->mesh_records,r.value->tessellation_cache_entries,
                r.value->face_tessellation_cache_entries,r.value->intersection_records,r.value->curve_eval_cache_entries,
                r.value->surface_eval_cache_entries,r.value->eval_node_records};
        };
        const auto valid_eval = [&] {
            return kernel.eval_graph().is_invalid(*node.value).value == std::optional<bool>{false} &&
                kernel.eval_graph().is_invalid(*consumer.value).value == std::optional<bool>{false} &&
                kernel.eval_graph().has_dependency(*consumer.value,*node.value).value == std::optional<bool>{true} &&
                kernel.eval_graph_store_maps_consistent().value == std::optional<bool>{true} &&
                kernel.runtime_tessellation_caches_consistent().value == std::optional<bool>{true};
        };
        const auto recomputes = kernel.eval_graph().total_recompute_count().value;
        const auto baseline = stores();
        const auto blend = [&](axiom::BodyId body, const std::vector<axiom::EdgeId>& selection, double parameter) {
            return fillet ? kernel.blends().fillet_edges(body,selection,parameter)
                          : kernel.blends().chamfer_edges(body,selection,parameter);
        };
        const auto reject = [&](axiom::BodyId body, const std::vector<axiom::EdgeId>& selection, double parameter,
                                std::string_view code, std::string_view stage) {
            const auto before = stores();
            const auto result = blend(body,selection,parameter);
            const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
            return result.status != axiom::StatusCode::Ok && !result.value && diagnostic.value && stores() == before &&
                valid_eval() && kernel.eval_graph().total_recompute_count().value == recomputes &&
                query.edges_of_body(*source.value).value == edges.value && query.faces_of_body(*source.value).value == faces.value &&
                std::any_of(diagnostic.value->issues.begin(),diagnostic.value->issues.end(),[&](const auto& issue) {
                    return issue.code == code && issue.stage == std::string(fillet ? "blend.fillet." : "blend.chamfer.")+std::string(stage);
                });
        };
        axiom::EdgeId crossing {};
        const auto endpoints = query.vertices_of_edge(edge);
        if (!endpoints.value) return false;
        for (const auto candidate : *edges.value) {
            if (candidate == edge) continue;
            const auto other = query.vertices_of_edge(candidate);
            if (!other.value) return false;
            if ((*other.value)[0] == (*endpoints.value)[0] || (*other.value)[1] == (*endpoints.value)[0] ||
                (*other.value)[0] == (*endpoints.value)[1] || (*other.value)[1] == (*endpoints.value)[1]) {
                crossing = candidate; break;
            }
        }
        if (!crossing.value) return false;
        const auto point_xyz = [](const axiom::Point3& p) { return std::array{p.x,p.y,p.z}; };
        const auto base_point = query.point_of_vertex((*endpoints.value)[0]);
        const auto end_point = query.point_of_vertex((*endpoints.value)[1]);
        if (!base_point.value || !end_point.value) return false;
        const auto p = point_xyz(*base_point.value), q = point_xyz(*end_point.value);
        int direction = 0;
        while (direction < 3 && std::abs(p[direction]-q[direction]) < 1e-8) ++direction;
        if (direction == 3) return false;
        axiom::EdgeId adjacent_parallel {};
        double contact_parameter = 0;
        for (const auto candidate : *edges.value) {
            if (candidate == edge) continue;
            const auto other = query.vertices_of_edge(candidate);
            if (!other.value) return false;
            const auto first_point = query.point_of_vertex((*other.value)[0]);
            const auto second_point = query.point_of_vertex((*other.value)[1]);
            if (!first_point.value || !second_point.value) return false;
            const auto a = point_xyz(*first_point.value), b = point_xyz(*second_point.value);
            if (std::abs(a[direction]-b[direction]) < 1e-8) continue;
            int different_cross_coordinates = 0;
            double cross_distance = 0;
            for (int c = 0; c < 3; ++c) if (c != direction && std::abs(a[c]-p[c]) > 1e-8) {
                ++different_cross_coordinates; cross_distance = std::abs(a[c]-p[c]);
            }
            if (different_cross_coordinates == 1) {
                adjacent_parallel = candidate; contact_parameter = cross_distance/2; break;
            }
        }
        if (!adjacent_parallel.value) return false;
        for (const bool active : {false,true}) {
            auto transaction = kernel.topology().begin_transaction();
            if (!active && transaction.rollback().status != axiom::StatusCode::Ok) return false;
            const auto sentinel = active ? transaction.create_vertex({30,31,32}) : axiom::Result<axiom::VertexId>{};
            const auto writes = transaction.write_operation_count().value;
            if ((active && !sentinel.value) ||
                !reject({}, {edge},0.4,axiom::diag_codes::kBlendInvalidTarget,"input_gate") ||
                !reject(*source.value,{},0.4,axiom::diag_codes::kBlendInvalidTarget,"input_gate") ||
                !reject(*source.value,{edge,edge},0.4,axiom::diag_codes::kBlendInvalidTarget,"input_gate") ||
                !reject(*source.value,{foreign_edges.value->front()},0.4,axiom::diag_codes::kBlendInvalidTarget,"input_gate") ||
                !reject(*source.value,{edge,crossing},0.4,axiom::diag_codes::kBlendIntersectingEdges,"intersection_gate") ||
                !reject(*wedge.value,{wedge_edges.value->front()},0.4,axiom::diag_codes::kBlendUnsupportedGeometry,"support_gate") ||
                !reject(*source.value,{edge},1e-12,axiom::diag_codes::kBlendDegenerateGeometry,"geometry_gate") ||
                !reject(*rounded_coordinates.value,{rounded_edges.value->front()},0.4,axiom::diag_codes::kBlendDegenerateGeometry,"geometry_gate") ||
                !reject(*source.value,{edge,adjacent_parallel},contact_parameter,axiom::diag_codes::kBlendParameterTooLarge,fillet ? "radius_gate" : "distance_gate") ||
                !reject(*source.value,{edge},6,axiom::diag_codes::kBlendParameterTooLarge,fillet ? "radius_gate" : "distance_gate")) return false;
            for (const double parameter : {0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
                if (!reject(*source.value,{edge},parameter,axiom::diag_codes::kBlendInvalidTarget,"input_gate")) return false;
            if (active && (transaction.write_operation_count().value != writes ||
                transaction.has_created_vertex(*sentinel.value).value != std::optional<bool>{true} ||
                transaction.rollback().status != axiom::StatusCode::Ok || stores() != baseline)) return false;
        }
        // Two successful allocations: a service savepoint rollback keeps the
        // preceding result, then writer rollback restores the warmed source.
        auto transaction = kernel.topology().begin_transaction();
        const auto sentinel = transaction.create_vertex({40,41,42});
        const auto writes = transaction.write_operation_count().value;
        const auto first = blend(*source.value,{edge},0.4);
        const auto savepoint = transaction.create_savepoint();
        const auto second = blend(*source.value,{edge},0.3);
        if (!sentinel.value || !first.value || !savepoint.value || !second.value) return false;
        const auto generated_faces = query.faces_of_body(second.value->output);
        const auto generated_edges = query.edges_of_body(second.value->output);
        if (!generated_faces.value || !generated_edges.value) return false;
        const auto generated_surface = query.surface_of_face(generated_faces.value->front());
        const auto generated_curve = query.curve_of_edge(generated_edges.value->front());
        if (!generated_surface.value || !generated_curve.value ||
            !kernel.surface_service().eval(*generated_surface.value,0,0,1).value ||
            !kernel.curve_service().eval(*generated_curve.value,0.5,1).value ||
            !kernel.convert().brep_to_mesh(second.value->output,{}).value || !valid_eval() ||
            transaction.write_operation_count().value != writes ||
            transaction.rollback_to_savepoint(*savepoint.value).status != axiom::StatusCode::Ok ||
            query.has_body(second.value->output).value != std::optional<bool>{false} ||
            query.has_body(first.value->output).value != std::optional<bool>{true} ||
            kernel.has_surface_id(*generated_surface.value).value != std::optional<bool>{false} ||
            kernel.has_curve_id(*generated_curve.value).value != std::optional<bool>{false} || !valid_eval() ||
            transaction.rollback().status != axiom::StatusCode::Ok || stores() != baseline || !valid_eval() ||
            kernel.eval_graph().total_recompute_count().value != recomputes ||
            query.has_body(first.value->output).value != std::optional<bool>{false}) return false;
        const auto retry = blend(*source.value,{edge},0.4);
        if (!retry.value || !valid_eval() ||
            kernel.validate().validate_all(retry.value->output,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok) return false;
    }
    return true;
}


// Derived solids participate in writer/savepoint rollback while existing Eval
// dependencies remain valid. Spatial queries read the new material boundary.
bool rebuilt_boundary_eval_rollback_regression() {
    for (const int variant : {0,1,2}) {
        const bool coplanar = variant == 1, safe_repair = variant == 2;
        constexpr double overlap = 2e-4;
        axiom::Kernel kernel;
        axiom::DiagnosticId last_diagnostic {};
        const auto fail = [&](int line) {
            std::cerr << "rebuilt query/Eval variant=" << variant << " line=" << line << "\n";
            const auto diagnostic = kernel.diagnostics().get(last_diagnostic);
            if (diagnostic.value) for (const auto& issue : diagnostic.value->issues)
                std::cerr << issue.stage << " " << issue.code << " " << issue.message << "\n";
            return false;
        };

        const auto query = kernel.topology().query();
        const auto a = kernel.primitives().box({0,0,0},2,2,2);
        const auto b = kernel.primitives().box(safe_repair ? axiom::Point3{2-overlap,0,0} :
            coplanar ? axiom::Point3{1,0,0} : axiom::Point3{1,1,1},2,2,2);
        if (!a.value || !b.value || (safe_repair && kernel.set_linear_tolerance(1e-3).status != axiom::StatusCode::Ok))
            return fail(__LINE__);
        axiom::BooleanRebuildOptions options;
        options.auto_repair = safe_repair;
        options.preparation.intersection.tolerance.linear = 1e-6;
        const auto second_operation = safe_repair ? axiom::BooleanOp::Subtract : axiom::BooleanOp::Intersect;
        const auto source = kernel.eval_graph().register_node(axiom::NodeKind::Geometry,"body:"+std::to_string(a.value->value));
        const auto rhs_source = kernel.eval_graph().register_node(axiom::NodeKind::Geometry,"body:"+std::to_string(b.value->value));
        const auto dependent = kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"s4-rebuild-input-analysis");
        if (!source.value || !rhs_source.value || !dependent.value ||
            kernel.eval_graph().add_dependency(*dependent.value,*source.value).status != axiom::StatusCode::Ok ||
            kernel.eval_graph().add_dependency(*dependent.value,*rhs_source.value).status != axiom::StatusCode::Ok ||
            kernel.eval_graph().recompute(*dependent.value).status != axiom::StatusCode::Ok) return fail(__LINE__);
        const auto recomputes = kernel.eval_graph().total_recompute_count().value;
        const auto counts = [&] {
            const auto runtime = kernel.runtime_store_counts();
            if (!runtime.value) return std::array<std::uint64_t,10>{};
            const auto& r = *runtime.value;
            return std::array{kernel.body_count().value.value_or(0),kernel.geometry_count().value.value_or(0),
                kernel.topology_count().value.value_or(0),r.mesh_records,r.tessellation_cache_entries,
                r.face_tessellation_cache_entries,r.intersection_records,r.curve_eval_cache_entries,
                r.surface_eval_cache_entries,r.eval_node_records};
        };
        const auto baseline = counts();
        const auto bridge_counts = [&] {
            const auto metrics = kernel.eval_graph_metrics();
            if (!metrics.value) return std::array<std::uint64_t,5>{};
            const auto& b = metrics.value->invalidation_bridge;
            return std::array{b.for_body_entries,b.for_faces_entries,b.for_bodies_batches,
                b.for_bodies_list_size_total,b.downstream_invalidation_steps};
        };
        if (!kernel.eval_graph_metrics().value) return fail(__LINE__);
        const auto bridge_before = bridge_counts();
        const auto bridge_after_discard = [&](std::uint64_t discarded_bodies) {
            auto expected = bridge_before;
            // Savepoint rollback invalidates each removed body once before
            // restoration and once while discarding its service allocation.
            // These cumulative handle-entry metrics survive legal rollback;
            // unbound derived bodies must not invalidate input consumers.
            expected[0] += 2*discarded_bodies;
            return bridge_counts() == expected;
        };
        auto transaction = kernel.topology().begin_transaction();
        const auto sentinel = transaction.create_vertex({99,98,97});
        const auto first = transaction.create_savepoint();
        if (!sentinel.value || !first.value) return fail(__LINE__);
        const auto writes = transaction.write_operation_count().value;
        const auto out1 = kernel.booleans().run_rebuilt(axiom::BooleanOp::Union,*a.value,*b.value,options);
        last_diagnostic = out1.diagnostic_id;
        if (!out1.value || !out1.value->output || out1.value->repaired != safe_repair || !bridge_after_discard(0)) return fail(__LINE__);
        const auto second = transaction.create_savepoint();
        const auto out2 = kernel.booleans().run_rebuilt(second_operation,*a.value,*b.value,options);
        last_diagnostic = out2.diagnostic_id;
        // Safe is optional: the second operation is already Strict-valid,
        // including subtraction of the narrow overlap, and must remain unrepaired.
        if (!second.value || !out2.value || !out2.value->output || out2.value->repaired || !bridge_after_discard(0)) return fail(__LINE__);
        const auto faces = query.faces_of_body(*out2.value->output);
        if (!faces.value || faces.value->empty()) return fail(__LINE__);
        const auto surface = query.surface_of_face(faces.value->front());
        if (!surface.value || !kernel.surface_service().eval(*surface.value,0,0,0).value) return fail(__LINE__);
        const auto phantom = query.locate_point(*out1.value->output,{0.5,2.5,1.5});
        axiom::BodySpatialQueryOptions query_options;
        query_options.position_tolerance = 1e-6;
        const auto section = kernel.query().section_detailed(*out2.value->output,{{0,0,1.5},{0,0,1}},query_options);
        if (!phantom.value || phantom.value->location != axiom::BodyPointLocation::Outside ||
            !section.value || std::abs(section.value->area-(safe_repair ? 4-2*overlap : coplanar ? 2 : 1)) > 1e-7 ||
            !kernel.topology().has_active_write_transaction().value.value_or(false) ||
            transaction.write_operation_count().value != writes || !bridge_after_discard(0) ||
            kernel.eval_graph().is_invalid(*source.value).value.value_or(true) ||
            kernel.eval_graph().is_invalid(*rhs_source.value).value.value_or(true) ||
            !kernel.eval_graph().has_dependency(*dependent.value,*source.value).value.value_or(false) ||
            !kernel.eval_graph().has_dependency(*dependent.value,*rhs_source.value).value.value_or(false) ||
            kernel.eval_graph().is_invalid(*dependent.value).value.value_or(true) ||
            kernel.eval_graph().total_recompute_count().value != recomputes) return fail(__LINE__);
        if (transaction.rollback_to_savepoint(*second.value).status != axiom::StatusCode::Ok ||
            query.has_body(*out2.value->output).value.value_or(true) ||
            !query.has_body(*out1.value->output).value.value_or(false) ||
            kernel.has_surface_id(*surface.value).value.value_or(true) || !bridge_after_discard(1) ||
            kernel.eval_graph().is_invalid(*source.value).value.value_or(true) ||
            kernel.eval_graph().is_invalid(*rhs_source.value).value.value_or(true) ||
            !kernel.eval_graph().has_dependency(*dependent.value,*source.value).value.value_or(false) ||
            !kernel.eval_graph().has_dependency(*dependent.value,*rhs_source.value).value.value_or(false) ||
            kernel.eval_graph().is_invalid(*dependent.value).value.value_or(true) ||
            kernel.eval_graph().total_recompute_count().value != recomputes) return fail(__LINE__);
        const auto retained = kernel.query().mass_properties(*out1.value->output);
        if (!retained.value || std::abs(retained.value->volume-(safe_repair ? 16-4*overlap : coplanar ? 12 : 15)) > 1e-7 ||
            transaction.rollback_to_savepoint(*first.value).status != axiom::StatusCode::Ok ||
            query.has_body(*out1.value->output).value.value_or(true) ||
            !bridge_after_discard(2) ||
            !transaction.has_created_vertex(*sentinel.value).value.value_or(false) ||
            transaction.rollback().status != axiom::StatusCode::Ok || counts() != baseline ||
            kernel.eval_graph().is_invalid(*source.value).value.value_or(true) ||
            kernel.eval_graph().is_invalid(*rhs_source.value).value.value_or(true) ||
            !kernel.eval_graph().has_dependency(*dependent.value,*source.value).value.value_or(false) ||
            !kernel.eval_graph().has_dependency(*dependent.value,*rhs_source.value).value.value_or(false) ||
            kernel.eval_graph().is_invalid(*dependent.value).value.value_or(true) ||
            kernel.eval_graph().total_recompute_count().value != recomputes ||
            !bridge_after_discard(2) || kernel.topology().has_active_write_transaction().value.value_or(true) ||
            !kernel.eval_graph_store_maps_consistent().value.value_or(false) ||
            !kernel.runtime_tessellation_caches_consistent().value.value_or(false)) return fail(__LINE__);
        const auto bridge_before_retry = bridge_counts();
        const auto retry = kernel.booleans().run_rebuilt(second_operation,*a.value,*b.value,options);
        last_diagnostic = retry.diagnostic_id;
        const auto mass = retry.value && retry.value->output ? kernel.query().mass_properties(*retry.value->output)
                                                           : axiom::Result<axiom::MassProperties>{};
        if (!retry.value || retry.value->repaired || !mass.value ||
            std::abs(mass.value->volume-(safe_repair ? 8-4*overlap : coplanar ? 4 : 1)) > 1e-7 ||
            std::abs(mass.value->area-(safe_repair ? 24-8*overlap : coplanar ? 16 : 6)) > 1e-7 || bridge_counts() != bridge_before_retry ||
            kernel.eval_graph().total_recompute_count().value != recomputes) return fail(__LINE__);
    }
    return true;
}

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

bool length_query_regression() {
    axiom::Kernel kernel;
    auto& geo = kernel.curve_service();
    auto& topo = kernel.topology().query();
    const auto equal = [](const axiom::Result<axiom::Scalar>& result, double expected) {
        return result.status == axiom::StatusCode::Ok && result.value &&
               std::abs(*result.value - expected) <= 1e-12 * std::max(1.0, std::abs(expected));
    };
    const auto failed = [&](const axiom::Result<axiom::Scalar>& result, axiom::StatusCode status,
                            std::string_view code) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        return result.status == status && !result.value && report.value && has_issue_code(*report.value, code);
    };
    const double pi = std::acos(-1.0);
    const auto line = kernel.curves().make_line({7, -2, 8}, {0, 0, 5});
    const auto segment = kernel.curves().make_line_segment({0, 0, 0}, {3, 4, 0});
    const auto circle = kernel.curves().make_circle({1, 2, 3}, {1, 2, 3}, 2);
    const auto polyline = kernel.curves().make_composite_polyline(
        std::array<axiom::Point3, 4>{{{0, 0, 0}, {3, 0, 0}, {3, 0, 0}, {3, 4, 0}}});
    const auto constant = kernel.curves().make_composite_polyline(
        std::array<axiom::Point3, 2>{{{1, 2, 3}, {1, 2, 3}}});
    const auto ellipse = kernel.curves().make_ellipse({0, 0, 0}, {3, 0, 0}, {0, 2, 0});
    const auto bezier = kernel.curves().make_bezier(
        std::array<axiom::Point3, 3>{{{0, 0, 0}, {1, 2, 0}, {2, 0, 0}}});
    if (!line.value || !segment.value || !circle.value || !polyline.value || !constant.value ||
        !ellipse.value || !bezier.value) return false;
    if (!equal(geo.length(*line.value, -3, 7), 10) ||
        !equal(geo.length(*segment.value), 5) || !equal(geo.length(*segment.value, .8, .2), 3) ||
        !equal(geo.length(*circle.value), 4 * pi) || !equal(geo.length(*circle.value, pi, 0), 2 * pi) ||
        !equal(geo.length(*polyline.value), 7) || !equal(geo.length(*polyline.value, .5, 2.5), 3.5) ||
        !equal(geo.length(*polyline.value, 1, 2), 0) || !equal(geo.length(*constant.value), 0) ||
        !equal(geo.length(*circle.value, 2 * pi, 2 * pi), 0)) return false;
    // Child domains are deliberately NOT rescaled: circle contributes one radian,
    // polyline contributes its first segment, nested chain its first child.
    const auto chain = kernel.curves().make_composite_chain(std::array{*segment.value, *circle.value, *polyline.value});
    if (!chain.value) return false;
    const auto nested = kernel.curves().make_composite_chain(std::array{*chain.value, *line.value});
    const auto unsupported_chain = kernel.curves().make_composite_chain(std::array{*segment.value, *bezier.value});
    if (!nested.value || !unsupported_chain.value || !equal(geo.length(*chain.value), 10) ||
        !equal(geo.length(*chain.value, .5, 2.5), 6) || !equal(geo.length(*nested.value), 6) ||
        !equal(geo.length(*unsupported_chain.value, 0, 1), 5)) return false;
    const auto big = kernel.curves().make_line_segment({-1e200, 0, 0}, {1e200, 0, 0});
    const auto overflow = kernel.curves().make_circle({0, 0, 0}, {0, 0, 1}, 1e308);
    if (!big.value || !overflow.value || !equal(geo.length(*big.value), 2e200) ||
        !equal(geo.length(*overflow.value, 0, .1), 1e307)) return false;
    const auto objects_before = kernel.object_count_total();
    const auto geometry_before = kernel.geometry_count();
    const auto runtime_before = kernel.runtime_store_counts();
    const auto invalid_before = kernel.eval_graph().invalid_node_count();
    if (!objects_before.value || !geometry_before.value || !runtime_before.value || !invalid_before.value) return false;
    for (const auto id : {*ellipse.value, *bezier.value, *unsupported_chain.value}) {
        if (!geo.length(id).value) return false;
    }
    if (!failed(geo.length({}), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
        !failed(geo.length(*line.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange) ||
        !failed(geo.length(*overflow.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange) ||
        !failed(geo.length(*line.value, -1e308, 1e308), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
        if (!failed(geo.length(*segment.value, bad, 0), axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange) ||
            !failed(geo.length(*segment.value, 0, bad), axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    }
    for (const auto id : {*segment.value, *circle.value, *polyline.value, *chain.value}) {
        if (!failed(geo.length(id, -.1, 0), axiom::StatusCode::InvalidInput, axiom::diag_codes::kGeoParameterOutOfDomain) ||
            !failed(geo.length(id, 0, 100), axiom::StatusCode::InvalidInput, axiom::diag_codes::kGeoParameterOutOfDomain)) return false;
    }
    if (!equal(geo.length(*segment.value), 5) || kernel.object_count_total().value != objects_before.value ||
        kernel.geometry_count().value != geometry_before.value || kernel.eval_graph().invalid_node_count().value != invalid_before.value)
        return false;
    const auto runtime_after = kernel.runtime_store_counts();
    if (!runtime_after.value || runtime_before.value->curve_eval_cache_entries != runtime_after.value->curve_eval_cache_entries ||
        runtime_before.value->surface_eval_cache_entries != runtime_after.value->surface_eval_cache_entries ||
        runtime_before.value->mesh_records != runtime_after.value->mesh_records ||
        runtime_before.value->tessellation_cache_entries != runtime_after.value->tessellation_cache_entries ||
        runtime_before.value->face_tessellation_cache_entries != runtime_after.value->face_tessellation_cache_entries ||
        runtime_before.value->intersection_records != runtime_after.value->intersection_records) return false;

    // Explicit topology trim intervals make curved-edge length unambiguous.
    // Creation validates domain and endpoint correspondence before allocating an EdgeId.
    {
        const auto c0 = geo.eval(*circle.value, 0, 0);
        const auto cpi = geo.eval(*circle.value, pi, 0);
        const auto b0 = geo.eval(*bezier.value, .25, 0);
        const auto b1 = geo.eval(*bezier.value, .75, 0);
        if (!c0.value || !cpi.value || !b0.value || !b1.value) return false;
        const auto public_query_stores = kernel.runtime_store_counts();
        const auto uncached_c0 = geo.point_at_parameter(*circle.value, 0);
        const auto half_circle_bbox = geo.bbox(*circle.value, 0, pi);
        const auto invalid_parameter_point =
            geo.point_at_parameter(*circle.value, -0.1);
        const auto public_query_stores_after = kernel.runtime_store_counts();
        const auto point_equal = [](const axiom::Point3 &lhs,
                                    const axiom::Point3 &rhs) {
            return approx(lhs.x, rhs.x) && approx(lhs.y, rhs.y) &&
                   approx(lhs.z, rhs.z);
        };
        if (!uncached_c0.value ||
            !point_equal(*uncached_c0.value, c0.value->point) ||
            !half_circle_bbox.value || !half_circle_bbox.value->is_valid ||
            invalid_parameter_point.status != axiom::StatusCode::InvalidInput ||
            invalid_parameter_point.value || !public_query_stores.value ||
            !public_query_stores_after.value ||
            public_query_stores.value->curve_eval_cache_entries !=
                public_query_stores_after.value->curve_eval_cache_entries) return false;
        auto trim_txn = kernel.topology().begin_transaction();
        const auto cv0 = trim_txn.create_vertex(c0.value->point);
        const auto cv1 = trim_txn.create_vertex(cpi.value->point);
        const auto bv0 = trim_txn.create_vertex(b0.value->point);
        const auto bv1 = trim_txn.create_vertex(b1.value->point);
        if (!cv0.value || !cv1.value || !bv0.value || !bv1.value) return false;
        const auto writes_before_rejection = trim_txn.write_operation_count();
        const auto objects_before_rejection = kernel.object_count_total();
        const auto caches_before_rejection = kernel.runtime_store_counts();
        const auto bad_domain = trim_txn.create_trimmed_edge(
            *circle.value, -.1, pi, *cv0.value, *cv1.value);
        const auto zero_interval = trim_txn.create_trimmed_edge(
            *circle.value, 0, 0, *cv0.value, *cv1.value);
        const auto mismatched = trim_txn.create_trimmed_edge(
            *circle.value, 0, pi, *cv1.value, *cv0.value);
        const auto bad_domain_report = kernel.diagnostics().get(bad_domain.diagnostic_id);
        const auto mismatch_report = kernel.diagnostics().get(mismatched.diagnostic_id);
        const auto caches_after_rejection = kernel.runtime_store_counts();
        if (bad_domain.status != axiom::StatusCode::InvalidInput || bad_domain.value ||
            !bad_domain_report.value || !has_issue_code(*bad_domain_report.value,
                axiom::diag_codes::kGeoParameterOutOfDomain) ||
            zero_interval.status != axiom::StatusCode::InvalidInput || zero_interval.value ||
            mismatched.status != axiom::StatusCode::InvalidTopology || mismatched.value ||
            !mismatch_report.value || !has_issue_code(*mismatch_report.value,
                axiom::diag_codes::kTopoCurveTopologyMismatch) ||
            trim_txn.write_operation_count().value != writes_before_rejection.value ||
            kernel.object_count_total().value != objects_before_rejection.value ||
            !caches_before_rejection.value || !caches_after_rejection.value ||
            caches_before_rejection.value->curve_eval_cache_entries !=
                caches_after_rejection.value->curve_eval_cache_entries) return false;

        const auto first_arc = trim_txn.create_trimmed_edge(
            *circle.value, 0, pi, *cv0.value, *cv1.value);
        const auto second_arc = trim_txn.create_trimmed_edge(
            *circle.value, pi, 2 * pi, *cv1.value, *cv0.value);
        const auto bezier_trim = trim_txn.create_trimmed_edge(
            *bezier.value, .25, .75, *bv0.value, *bv1.value);
        const auto bezier_trim_length = geo.length(*bezier.value, .25, .75);
        if (!first_arc.value || !second_arc.value || !bezier_trim.value ||
            !bezier_trim_length.value ||
            !equal(topo.edge_length(*first_arc.value), 2 * pi) ||
            !equal(topo.edge_length(*second_arc.value), 2 * pi) ||
            !equal(topo.edge_length(*bezier_trim.value),
                   *bezier_trim_length.value)) return false;
        const auto interval = topo.edge_curve_interval(*second_arc.value);
        const auto legacy_interval = topo.edge_curve_interval({});
        if (!interval.value || !interval.value->has_value() ||
            !approx(interval.value->value().start_parameter, pi) ||
            !approx(interval.value->value().end_parameter, 2 * pi) ||
            legacy_interval.status != axiom::StatusCode::InvalidInput ||
            legacy_interval.value) return false;
        const auto ce0 = trim_txn.create_coedge(*first_arc.value, false);
        const auto ce1 = trim_txn.create_coedge(*second_arc.value, false);
        if (!ce0.value || !ce1.value) return false;
        const auto arc_loop = trim_txn.create_loop(std::array{*ce0.value, *ce1.value});
        const auto circle_plane = kernel.surfaces().make_plane({1, 2, 3}, {1, 2, 3});
        if (!arc_loop.value || !circle_plane.value ||
            !equal(topo.loop_length(*arc_loop.value), 4 * pi)) return false;
        const auto arc_face = trim_txn.create_face(*circle_plane.value, *arc_loop.value, {});
        const auto face_bbox = arc_face.value ? topo.bbox_of_face(*arc_face.value)
                                              : axiom::Result<axiom::BoundingBox>{};
        const auto curve_bbox = geo.bbox(*circle.value);
        const auto same_bbox = [&](const axiom::BoundingBox &lhs,
                                   const axiom::BoundingBox &rhs) {
            return lhs.is_valid && rhs.is_valid && approx(lhs.min.x, rhs.min.x) &&
                   approx(lhs.min.y, rhs.min.y) && approx(lhs.min.z, rhs.min.z) &&
                   approx(lhs.max.x, rhs.max.x) && approx(lhs.max.y, rhs.max.y) &&
                   approx(lhs.max.z, rhs.max.z);
        };
        if (!arc_face.value || !face_bbox.value || !curve_bbox.value ||
            !same_bbox(*face_bbox.value, *curve_bbox.value) ||
            !equal(topo.face_boundary_length(*arc_face.value), 4 * pi) ||
            kernel.topology().validate().validate_edge(*first_arc.value).status !=
                axiom::StatusCode::Ok ||
            trim_txn.rollback().status != axiom::StatusCode::Ok ||
            topo.edge_curve_interval(*first_arc.value).status !=
                axiom::StatusCode::InvalidInput) return false;
    }

    // Full boundary workflow: concave outer loop + two holes, independent winding,
    // reversed coedges, two planes and three model-unit scales (24 variants).
    for (double scale : {0.001, 1.0, 1000.0}) {
        for (bool tilted : {false, true}) {
            for (bool reverse_outer : {false, true}) {
                for (bool reverse_holes : {false, true}) {
                    const auto point = [&](double x, double y) -> axiom::Point3 {
                        return tilted ? axiom::Point3{10 + x * scale, 20 + .6 * y * scale, 30 + .8 * y * scale}
                                      : axiom::Point3{10 + x * scale, 20 + y * scale, 30};
                    };
                    const auto plane = kernel.surfaces().make_plane(point(0, 0), tilted ? axiom::Vec3{0, -.8, .6} : axiom::Vec3{0, 0, 1});
                    if (!plane.value) return false;
                    auto txn = kernel.topology().begin_transaction();
                    const auto make_loop = [&](std::vector<axiom::Point3> points, bool reverse) -> axiom::LoopId {
                        if (reverse) std::reverse(points.begin(), points.end());
                        std::vector<axiom::VertexId> vertices;
                        std::vector<axiom::CoedgeId> coedges;
                        for (const auto& p : points) {
                            const auto v = txn.create_vertex(p);
                            if (!v.value) return {};
                            vertices.push_back(*v.value);
                        }
                        for (std::size_t i = 0; i < points.size(); ++i) {
                            const auto j = (i + 1) % points.size();
                            const auto c = kernel.curves().make_line_segment(points[j], points[i]);
                            if (!c.value) return {};
                            const auto e = txn.create_edge(*c.value, vertices[j], vertices[i]);
                            const auto curve_length = geo.length(*c.value);
                            if (!e.value || !curve_length.value || !equal(topo.edge_length(*e.value), *curve_length.value)) return {};
                            const auto ce = txn.create_coedge(*e.value, true);
                            if (!ce.value) return {};
                            coedges.push_back(*ce.value);
                        }
                        const auto loop = txn.create_loop(coedges);
                        return loop.value.value_or(axiom::LoopId{});
                    };
                    const auto outer = make_loop({point(0, 0), point(6, 0), point(6, 4), point(3, 4), point(3, 6), point(0, 6)}, reverse_outer);
                    const auto hole1 = make_loop({point(1, 1), point(2, 1), point(2, 2), point(1, 2)}, reverse_holes);
                    const auto hole2 = make_loop({point(4, 1), point(5, 1), point(5, 2), point(4, 2)}, !reverse_holes);
                    if (!outer.value || !hole1.value || !hole2.value) return false;
                    const auto face = txn.create_face(*plane.value, outer, std::array{hole2, hole1});
                    if (!face.value) return false;
                    const auto writes = txn.write_operation_count();
                    const auto audit = topo.query_operation_count();
                    if (!equal(topo.face_boundary_length(*face.value), 32 * scale)) return false;
                    const auto audit_after = topo.query_operation_count();
                    if (!audit.value || !audit_after.value || *audit_after.value != *audit.value + 1 ||
                        !equal(topo.loop_length(outer), 24 * scale) || !equal(topo.loop_length(hole1), 4 * scale) ||
                        !failed(topo.edge_length({}), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
                        !failed(topo.loop_length({}), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
                        !failed(topo.face_boundary_length({}), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
                        txn.write_operation_count().value != writes.value || txn.commit().status != axiom::StatusCode::Ok) return false;
                    {
                        auto edit = kernel.topology().begin_transaction();
                        if (edit.delete_face(*face.value).status != axiom::StatusCode::Ok ||
                            !failed(topo.face_boundary_length(*face.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
                            edit.rollback().status != axiom::StatusCode::Ok || !equal(topo.face_boundary_length(*face.value), 32 * scale)) return false;
                    }
                }
            }
        }
    }
    // Public creation permits geometrically inconsistent edges: queries must reject
    // them without manufacturing a chord for a curved edge or mutating the transaction.
    auto txn = kernel.topology().begin_transaction();
    const auto v0 = txn.create_vertex({0, 0, 0});
    const auto v1 = txn.create_vertex({1.5, 2, 0});
    const auto v2 = txn.create_vertex({3, 4, 0});
    const auto off = txn.create_vertex({0, 0, 1});
    const auto outside = txn.create_vertex({6, 8, 0});
    const auto coincident = txn.create_vertex({0, 0, 0});
    if (!v0.value || !v1.value || !v2.value || !off.value || !outside.value || !coincident.value) return false;
    const auto partial = txn.create_edge(*segment.value, *v2.value, *v1.value);
    const auto mismatch = txn.create_edge(*segment.value, *v0.value, *off.value);
    const auto out_of_domain = txn.create_edge(*segment.value, *v0.value, *outside.value);
    const auto degenerate = txn.create_edge(*segment.value, *v0.value, *coincident.value);
    const auto curved = txn.create_edge(*circle.value, *v0.value, *v2.value);
    if (!partial.value || !mismatch.value || !out_of_domain.value || !degenerate.value || !curved.value) return false;
    const auto legacy_partial_interval = topo.edge_curve_interval(*partial.value);
    if (!legacy_partial_interval.value || legacy_partial_interval.value->has_value()) return false;
    const auto straight = kernel.curves().make_line({0, 0, 0}, {3, 4, 0});
    const auto huge_line = kernel.curves().make_line({0, 0, 0}, {1, 0, 0});
    const auto huge0 = txn.create_vertex({-1e308, 0, 0});
    const auto huge1 = txn.create_vertex({1e308, 0, 0});
    if (!straight.value || !huge_line.value || !huge0.value || !huge1.value) return false;
    const auto linear = txn.create_edge(*straight.value, *v0.value, *outside.value);
    const auto huge_edge = txn.create_edge(*huge_line.value, *huge0.value, *huge1.value);
    if (!linear.value || !huge_edge.value || !equal(topo.edge_length(*linear.value), 10) ||
        !failed(topo.edge_length(*huge_edge.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    const auto curved_return = txn.create_edge(*circle.value, *v2.value, *v0.value);
    if (!curved_return.value) return false;
    const auto ce0 = txn.create_coedge(*curved.value, false);
    const auto ce1 = txn.create_coedge(*curved_return.value, false);
    if (!ce0.value || !ce1.value) return false;
    const auto curved_loop = txn.create_loop(std::array{*ce0.value, *ce1.value});
    if (!curved_loop.value || !failed(topo.loop_length(*curved_loop.value), axiom::StatusCode::NotImplemented,
                                     axiom::diag_codes::kCoreOperationUnsupported)) return false;
    const auto curved_plane = kernel.surfaces().make_plane({1, 2, 3}, {1, 2, 3});
    if (!curved_plane.value) return false;
    const auto curved_face = txn.create_face(*curved_plane.value, *curved_loop.value, {});
    if (!curved_face.value || !failed(topo.face_boundary_length(*curved_face.value), axiom::StatusCode::NotImplemented,
                                     axiom::diag_codes::kCoreOperationUnsupported)) return false;
    const auto writes = txn.write_operation_count();
    const auto objects = kernel.object_count_total();
    const auto runtime_topo_before = kernel.runtime_store_counts();
    const auto geometry_topo_before = kernel.geometry_count();
    const auto invalid_topo_before = kernel.eval_graph().invalid_node_count();
    if (!equal(topo.edge_length(*partial.value), 2.5) ||
        !failed(topo.edge_length(*curved.value), axiom::StatusCode::NotImplemented, axiom::diag_codes::kCoreOperationUnsupported)) return false;
    for (const auto edge : {*mismatch.value, *out_of_domain.value, *degenerate.value}) {
        if (!failed(topo.edge_length(edge), axiom::StatusCode::InvalidTopology, axiom::diag_codes::kTopoCurveTopologyMismatch)) return false;
    }
    const auto runtime_topo_after = kernel.runtime_store_counts();
    if (!runtime_topo_before.value || !runtime_topo_after.value ||
        runtime_topo_before.value->curve_eval_cache_entries != runtime_topo_after.value->curve_eval_cache_entries ||
        runtime_topo_before.value->surface_eval_cache_entries != runtime_topo_after.value->surface_eval_cache_entries ||
        runtime_topo_before.value->mesh_records != runtime_topo_after.value->mesh_records ||
        runtime_topo_before.value->tessellation_cache_entries != runtime_topo_after.value->tessellation_cache_entries ||
        runtime_topo_before.value->face_tessellation_cache_entries != runtime_topo_after.value->face_tessellation_cache_entries ||
        runtime_topo_before.value->intersection_records != runtime_topo_after.value->intersection_records ||
        kernel.geometry_count().value != geometry_topo_before.value ||
        kernel.eval_graph().invalid_node_count().value != invalid_topo_before.value) return false;
    if (txn.write_operation_count().value != writes.value || kernel.object_count_total().value != objects.value ||
        !equal(topo.edge_length(*partial.value), 2.5) || txn.rollback().status != axiom::StatusCode::Ok ||
        !failed(topo.edge_length(*partial.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
        !failed(topo.loop_length(*curved_loop.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle)) return false;
    // Isolated Eval graph: successful and failed reads preserve the bound node.
    const auto box = kernel.primitives().box({0, 0, 0}, 2, 3, 4);
    if (!box.value) return false;
    const auto node = kernel.eval_graph().register_node(axiom::NodeKind::Geometry,
        std::string("body:") + std::to_string(box.value->value));
    const auto faces = topo.faces_of_body(*box.value);
    if (!node.value || !faces.value || faces.value->size() != 6 ||
        kernel.eval_graph().recompute(*node.value).status != axiom::StatusCode::Ok) return false;
    const auto invalid = kernel.eval_graph().is_invalid(*node.value);
    const auto recomputes = kernel.eval_graph().recompute_count(*node.value);
    for (const auto face : *faces.value) {
        if (!topo.face_boundary_length(face).value) return false;
    }
    if (!failed(topo.face_boundary_length({}), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle) ||
        kernel.eval_graph().is_invalid(*node.value).value != invalid.value ||
        kernel.eval_graph().recompute_count(*node.value).value != recomputes.value) return false;
    return true;
}

bool numerical_length_query_regression() {
    axiom::Kernel kernel;
    auto& factory = kernel.curves();
    auto& geo = kernel.curve_service();
    const double pi = std::acos(-1.0);
    const double quadratic_length = std::sqrt(5.0) / 2 + std::asinh(2.0) / 4;
    const auto equal = [](const axiom::Result<double>& r, double expected) {
        return r.status == axiom::StatusCode::Ok && r.value &&
               std::abs(*r.value - expected) <= 2e-8 * std::max(1e-3, std::abs(expected));
    };
    const auto failed = [&](const axiom::Result<double>& r, axiom::StatusCode status, std::string_view code) {
        const auto report = kernel.diagnostics().get(r.diagnostic_id);
        return r.status == status && !r.value && report.value && has_issue_code(*report.value, code);
    };
    // x=t, y=t^2, represented in three bases, scales and spatial frames.
    for (const double scale : {1e-3, 1.0, 1e3}) {
        for (const bool tilted : {false, true}) {
            const auto point = [&](double x, double y) -> axiom::Point3 {
                return tilted ? axiom::Point3{7 + .6 * scale * x, -3 + .8 * scale * x, 2 + scale * y}
                              : axiom::Point3{scale * x, scale * y, 0};
            };
            const std::vector<axiom::Point3> poles{point(0, 0), point(.5, 0), point(1, 1)};
            const auto bezier = factory.make_bezier(poles);
            axiom::BSplineCurveDesc desc;
            desc.poles = poles; desc.degree = 2; desc.knots = {2, 2, 2, 5, 5, 5};
            const auto spline = factory.make_bspline(desc);
            axiom::NURBSCurveDesc rational;
            rational.poles = poles; rational.degree = 2; rational.knots = desc.knots;
            rational.weights = {7, 7, 7};
            const auto nurbs = factory.make_nurbs(rational);
            if (!bezier.value || !spline.value || !nurbs.value) return false;
            for (const auto id : {*bezier.value, *spline.value, *nurbs.value}) {
                const auto domain = geo.domain(id);
                if (!domain.value || !equal(geo.length(id), scale * quadratic_length)) return false;
                const auto a = domain.value->min, b = domain.value->max, m = (a + b) / 2;
                const auto first = geo.length(id, a, m), last = geo.length(id, m, b);
                if (!first.value || !last.value || !equal(geo.length(id, b, a), scale * quadratic_length) ||
                    !equal(geo.length(id), *first.value + *last.value) || !equal(geo.length(id, m, m), 0)) return false;
                if (!failed(geo.length(id, a - 1, b), axiom::StatusCode::InvalidInput,
                            axiom::diag_codes::kGeoParameterOutOfDomain)) return false;
            }
        }
    }
    const auto ellipse = factory.make_ellipse({4, -3, 2}, {3, 0, 0}, {0, 0, 2});
    const auto round = factory.make_ellipse({0, 0, 0}, {2, 0, 0}, {0, 2, 0});
    const auto parabola = factory.make_parabola({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, .25);
    const auto hyperbola = factory.make_hyperbola({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 1, 1);
    if (!ellipse.value || !round.value || !parabola.value || !hyperbola.value ||
        !equal(geo.length(*ellipse.value), 15.8654395892905898) ||
        !equal(geo.length(*ellipse.value, 0, pi / 2), 15.8654395892905898 / 4) ||
        !equal(geo.length(*round.value), 4 * pi) ||
        !equal(geo.length(*parabola.value, 0, 1), quadratic_length) ||
        !equal(geo.length(*parabola.value), 10 * std::sqrt(401.0) + std::asinh(20.0) / 2)) return false;
    // Independent dense composite midpoint oracle for non-orthogonal conics.
    const auto skew = factory.make_ellipse({0, 0, 0}, {3, 0, 0}, {1, 2, 0});
    if (!skew.value) return false;
    double hyperbola_reference = 0, skew_reference = 0;
    constexpr int samples = 100000;
    for (int i = 0; i < samples; ++i) {
        const double t = (i + .5) / samples;
        hyperbola_reference += std::sqrt(std::cosh(2 * t)) / samples;
        const double angle = 2 * pi * t;
        skew_reference += std::hypot(-3 * std::sin(angle) + std::cos(angle), 2 * std::cos(angle)) * 2 * pi / samples;
    }
    if (!equal(geo.length(*hyperbola.value, 0, 1), hyperbola_reference) ||
        !equal(geo.length(*skew.value), skew_reference)) return false;
    axiom::NURBSCurveDesc quarter_desc;
    quarter_desc.poles = {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    quarter_desc.weights = {1, std::sqrt(.5), 1};
    quarter_desc.degree = 2; quarter_desc.knots = {0, 0, 0, 1, 1, 1};
    const auto quarter = factory.make_nurbs(quarter_desc);
    const auto cusp = factory.make_bezier(std::array<axiom::Point3, 3>{{{0, 0, 0}, {1, 0, 0}, {0, 0, 0}}});
    const auto constant = factory.make_bezier(std::array<axiom::Point3, 1>{{{7, 8, 9}}});
    if (!quarter.value || !cusp.value || !constant.value || !equal(geo.length(*quarter.value), pi / 2) ||
        !equal(geo.length(*quarter.value, .5, 0), pi / 4) || !equal(geo.length(*cusp.value), 1) ||
        !equal(geo.length(*constant.value), 0)) return false;
    // Degree-elevated quadratic and nonconstant-weight straight rational curve.
    std::vector<axiom::Point3> elevated;
    for (int i = 0; i <= 12; ++i) elevated.push_back({i / 12.0, i * (i - 1) / 132.0, 0});
    const auto high_degree = factory.make_bezier(elevated);
    axiom::NURBSCurveDesc rational_line;
    rational_line.poles = {{0, 0, 0}, {3, 4, 0}}; rational_line.weights = {1, 10};
    const auto weighted_line = factory.make_nurbs(rational_line);
    if (!high_degree.value || !weighted_line.value || !equal(geo.length(*high_degree.value), quadratic_length) ||
        !equal(geo.length(*weighted_line.value), 5)) return false;
    // Repeated/discontinuous, non-clamped, and extremely narrow knot spans.
    for (int variant = 0; variant < 3; ++variant) {
        axiom::BSplineCurveDesc desc;
        desc.degree = 1;
        if (variant == 0) {
            desc.poles = {{0, 0, 0}, {3, 0, 0}, {30, 40, 0}, {30, 44, 0}};
            desc.knots = {0, 0, .5, .5, 1, 1};
        } else {
            desc.poles = {{0, 0, 0}, {3, 0, 0}, {3, 4, 0}};
            desc.knots = variant == 1 ? std::vector<double>{0, 1, 2, 3, 4}
                                      : std::vector<double>{0, 0, 1e-30, 1, 1};
        }
        const auto spline = factory.make_bspline(desc);
        axiom::NURBSCurveDesc rational;
        rational.poles = desc.poles; rational.knots = desc.knots; rational.degree = 1;
        const auto nurbs = factory.make_nurbs(rational);
        if (!spline.value || !nurbs.value || !equal(geo.length(*spline.value), 7) || !equal(geo.length(*nurbs.value), 7)) return false;
    }
    const auto chain = factory.make_composite_chain(std::array{*quarter.value, *parabola.value, *cusp.value});
    if (!chain.value || !equal(geo.length(*chain.value), pi / 2 + quadratic_length + 1) ||
        !equal(geo.length(*chain.value, .5, 2), pi / 4 + quadratic_length)) return false;
    const auto nested = factory.make_composite_chain(std::array{*chain.value, *constant.value});
    if (!nested.value || !equal(geo.length(*nested.value), pi / 2)) return false;
    const auto moved = kernel.geometry_transform().transform_curve(*quarter.value,
        kernel.linear_algebra().make_translation({5, -3, 8}));
    if (!moved.value || !equal(geo.length(*moved.value), pi / 2) || !equal(geo.length(*quarter.value), pi / 2)) return false;
    axiom::BSplineCurveDesc shifted_desc;
    shifted_desc.poles = {{0, 0, 0}, {1, 0, 0}}; shifted_desc.degree = 1; shifted_desc.knots = {2, 2, 3, 3};
    const auto shifted = factory.make_bspline(shifted_desc);
    if (!shifted.value) return false;
    const auto invalid_chain = factory.make_composite_chain(std::array{*quarter.value, *shifted.value});
    if (!invalid_chain.value || !equal(geo.length(*invalid_chain.value, 0, 1), pi / 2) ||
        !failed(geo.length(*invalid_chain.value), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kGeoParameterOutOfDomain)) return false;
    shifted_desc.poles = {{5, 7, 8}, {5, 7, 8}};
    const auto constant_spline = factory.make_bspline(shifted_desc);
    axiom::NURBSCurveDesc constant_desc;
    constant_desc.poles = shifted_desc.poles; constant_desc.weights = {1, 5};
    const auto constant_nurbs = factory.make_nurbs(constant_desc);
    if (!constant_spline.value || !constant_nurbs.value || !equal(geo.length(*constant_spline.value), 0) ||
        !equal(geo.length(*constant_nurbs.value), 0)) return false;
    const auto huge = factory.make_bezier(std::array<axiom::Point3, 2>{{{-1e308, 0, 0}, {1e308, 0, 0}}});
    const auto invalid_curve = factory.make_bezier(std::array<axiom::Point3, 2>{{{0, 0, 0},
        {std::numeric_limits<double>::infinity(), 0, 0}}});
    if (!huge.value) return false;
    // Query success/failure is read-only, even within a topology transaction.
    auto txn = kernel.topology().begin_transaction();
    const auto v0 = txn.create_vertex({1, 0, 0}), v1 = txn.create_vertex({0, 1, 0});
    if (!v0.value || !v1.value) return false;
    const auto edge = txn.create_edge(*quarter.value, *v0.value, *v1.value);
    const auto node = kernel.eval_graph().register_node(axiom::NodeKind::Geometry, "curve:length-regression");
    if (!edge.value || !node.value || kernel.eval_graph().recompute(*node.value).status != axiom::StatusCode::Ok) return false;
    const auto writes = txn.write_operation_count();
    const auto objects = kernel.object_count_total();
    const auto geometry = kernel.geometry_count();
    const auto stores = kernel.runtime_store_counts();
    const auto recomputes = kernel.eval_graph().recompute_count(*node.value);
    if (!stores.value || !writes.value || !objects.value || !geometry.value) return false;
    axiom::CurveLengthOptions tight;
    tight.absolute_tolerance = 1e-11; tight.relative_tolerance = 1e-11;
    if (!equal(geo.length(*quarter.value, tight), pi / 2)) return false;
    axiom::CurveLengthOptions limited = tight;
    limited.max_evaluations = 1;
    const auto exhausted = geo.length(*quarter.value, limited);
    const auto report_path = std::filesystem::temp_directory_path() / "axiom_query_length_diagnostic.json";
    if (kernel.diagnostics().export_report_json(exhausted.diagnostic_id, report_path.string()).status != axiom::StatusCode::Ok) return false;
    std::ifstream report_file(report_path);
    const std::string report_json((std::istreambuf_iterator<char>(report_file)), std::istreambuf_iterator<char>{});
    report_file.close();
    std::filesystem::remove(report_path);
    if (report_json.find(axiom::diag_codes::kGeoLengthIntegrationFailure) == std::string::npos) return false;
    auto unattainable = tight;
    unattainable.absolute_tolerance = 0; unattainable.relative_tolerance = 1e-30; unattainable.max_evaluations = 1000;
    if (!failed(geo.length(*quarter.value, unattainable), axiom::StatusCode::OperationFailed,
                axiom::diag_codes::kGeoLengthIntegrationFailure)) return false;
    for (const bool relative_only : {false, true}) {
        auto options = tight;
        if (relative_only) options.absolute_tolerance = 0;
        else options.relative_tolerance = 0;
        if (!equal(geo.length(*quarter.value, options), pi / 2)) return false;
    }
    if (!failed(geo.length(*quarter.value, limited), axiom::StatusCode::OperationFailed, axiom::diag_codes::kGeoLengthIntegrationFailure) ||
        !failed(geo.length(*chain.value, limited), axiom::StatusCode::OperationFailed, axiom::diag_codes::kGeoLengthIntegrationFailure) ||
        !equal(geo.length(*quarter.value, .5, .5, limited), 0) || !equal(geo.length(*quarter.value, tight), pi / 2) ||
        !failed(geo.length(*huge.value), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange) ||
        !failed(geo.length({}), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreInvalidHandle)) return false;
    if (invalid_curve.value && !failed(geo.length(*invalid_curve.value), axiom::StatusCode::OperationFailed,
                                      axiom::diag_codes::kGeoLengthIntegrationFailure)) return false;
    for (const double bad : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto options = tight; options.absolute_tolerance = bad;
        if (!failed(geo.length(*quarter.value, options), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange)) return false;
        options = tight; options.relative_tolerance = bad;
        if (!failed(geo.length(*quarter.value, options), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    }
    for (int variant = 0; variant < 2; ++variant) {
        auto options = tight;
        if (variant == 0) options.max_evaluations = 0;
        else options.absolute_tolerance = options.relative_tolerance = 0;
        if (!failed(geo.length(*quarter.value, options), axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    }
    if (!failed(kernel.topology().query().edge_length(*edge.value), axiom::StatusCode::NotImplemented,
                axiom::diag_codes::kCoreOperationUnsupported) || !equal(geo.length(*quarter.value), pi / 2)) return false;
    const auto after = kernel.runtime_store_counts();
    if (!after.value || stores.value->curve_eval_cache_entries != after.value->curve_eval_cache_entries ||
        stores.value->surface_eval_cache_entries != after.value->surface_eval_cache_entries ||
        stores.value->mesh_records != after.value->mesh_records ||
        stores.value->tessellation_cache_entries != after.value->tessellation_cache_entries ||
        stores.value->face_tessellation_cache_entries != after.value->face_tessellation_cache_entries ||
        stores.value->intersection_records != after.value->intersection_records ||
        txn.write_operation_count().value != writes.value || kernel.object_count_total().value != objects.value ||
        kernel.geometry_count().value != geometry.value || kernel.eval_graph().is_invalid(*node.value).value != false ||
        kernel.eval_graph().recompute_count(*node.value).value != recomputes.value) return false;
    if (txn.rollback().status != axiom::StatusCode::Ok || !equal(geo.length(*quarter.value), pi / 2) ||
        !failed(kernel.topology().query().edge_length(*edge.value), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreInvalidHandle)) return false;
    return true;
}

bool analytic_face_area_regression() {
    axiom::Kernel kernel;
    auto& topo = kernel.topology().query();
    const double pi = std::acos(-1.0);
    const auto equal = [](const axiom::Result<axiom::Scalar>& result, double expected,
                          double tolerance = 2e-10) {
        return result.status == axiom::StatusCode::Ok && result.value &&
               std::abs(*result.value - expected) <= tolerance * std::max(1.0, std::abs(expected));
    };
    const auto failed = [&](const axiom::Result<axiom::Scalar>& result,
                            axiom::StatusCode status, std::string_view code) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        return result.status == status && !result.value && report.value &&
               has_issue_code(*report.value, code);
    };
    struct Patch {
        axiom::FaceId face{};
        std::vector<axiom::CoedgeId> coedges;
    };
    const auto make_patch = [&](axiom::SurfaceId surface,
                                std::vector<std::vector<axiom::Point2>> rings,
                                const auto& map_uv) -> Patch {
        auto txn = kernel.topology().begin_transaction();
        std::vector<axiom::LoopId> loops;
        Patch patch;
        for (const auto& ring : rings) {
            if (ring.size() < 3) return {};
            std::vector<axiom::VertexId> vertices;
            std::vector<axiom::CoedgeId> coedges;
            for (const auto& uv : ring) {
                const auto vertex = txn.create_vertex(map_uv(uv));
                if (!vertex.value) return {};
                vertices.push_back(*vertex.value);
            }
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const auto j = (i + 1) % ring.size();
                const auto p0 = map_uv(ring[i]);
                const auto p1 = map_uv(ring[j]);
                const auto curve = kernel.curves().make_line_segment(p0, p1);
                const auto edge = curve.value
                                      ? txn.create_edge(*curve.value, vertices[i], vertices[j])
                                      : axiom::Result<axiom::EdgeId>{};
                const auto coedge = edge.value
                                        ? txn.create_coedge(*edge.value, false)
                                        : axiom::Result<axiom::CoedgeId>{};
                const auto pcurve = kernel.pcurves().make_polyline(
                    std::array<axiom::Point2, 2>{ring[i], ring[j]});
                if (!coedge.value || !pcurve.value ||
                    txn.set_coedge_pcurve(*coedge.value, *pcurve.value).status !=
                        axiom::StatusCode::Ok) return {};
                coedges.push_back(*coedge.value);
                patch.coedges.push_back(*coedge.value);
            }
            const auto loop = txn.create_loop(coedges);
            if (!loop.value) return {};
            loops.push_back(*loop.value);
        }
        const auto face = txn.create_face(surface, loops.front(),
                                          std::span<const axiom::LoopId>(loops).subspan(1));
        if (!face.value || txn.commit().status != axiom::StatusCode::Ok) return {};
        patch.face = *face.value;
        return patch;
    };
    const auto rectangle = [](double u0, double v0, double u1, double v1,
                              bool reverse = false) {
        std::vector<axiom::Point2> ring{{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        if (reverse) std::reverse(ring.begin(), ring.end());
        return ring;
    };

    const auto cylinder = kernel.surfaces().make_cylinder({3, -2, 7}, {0, 0, 5}, 3);
    const auto same_cylinder = kernel.surfaces().make_cylinder({3, -2, 7}, {0, 0, 1}, 3);
    const auto sphere = kernel.surfaces().make_sphere({0, 0, 0}, 2);
    const auto cone = kernel.surfaces().make_cone({0, 0, 0}, {0, 0, 1}, pi / 6);
    const auto torus = kernel.surfaces().make_torus({0, 0, 0}, {0, 0, 1}, 5, 2);
    const auto plane = kernel.surfaces().make_plane({0, 0, 4}, {0, 0, 1});
    if (!cylinder.value || !same_cylinder.value || !sphere.value || !cone.value ||
        !torus.value || !plane.value) return false;
    const auto cylinder_point = [](const axiom::Point2& uv) {
        return axiom::Point3{3 + 3 * std::cos(uv.x), -2 + 3 * std::sin(uv.x), 7 + uv.y};
    };
    const auto sphere_point = [](const axiom::Point2& uv) {
        return axiom::Point3{2 * std::cos(uv.x) * std::sin(uv.y),
                             2 * std::sin(uv.x) * std::sin(uv.y), 2 * std::cos(uv.y)};
    };
    const auto cone_point = [pi](const axiom::Point2& uv) {
        const double r = std::tan(pi / 6) * uv.y;
        return axiom::Point3{r * std::cos(uv.x), r * std::sin(uv.x), uv.y};
    };
    const auto torus_point = [](const axiom::Point2& uv) {
        const double ring = 5 + 2 * std::cos(uv.y);
        return axiom::Point3{ring * std::cos(uv.x), ring * std::sin(uv.x),
                             2 * std::sin(uv.y)};
    };
    const auto plane_point = [](const axiom::Point2& uv) {
        return axiom::Point3{uv.x, uv.y, 4};
    };

    const auto common_outer = rectangle(.2, .4, 1.2, 1.4, true);
    const auto common_hole = rectangle(.5, .7, .7, .9, false);
    const auto cylinder_patch = make_patch(*cylinder.value, {common_outer, common_hole}, cylinder_point);
    const auto sphere_patch = make_patch(*sphere.value, {common_outer, common_hole}, sphere_point);
    const auto cone_outer = rectangle(.1, 1, 1.6, 4);
    const auto cone_hole = rectangle(.4, 2, .7, 3, true);
    const auto cone_patch = make_patch(*cone.value, {cone_outer, cone_hole}, cone_point);
    const auto torus_outer = rectangle(.3, .2, 1.8, 1.4, true);
    const auto torus_hole = rectangle(.7, .5, 1.0, .8);
    const auto torus_patch = make_patch(*torus.value, {torus_outer, torus_hole}, torus_point);
    const std::vector<axiom::Point2> concave_outer{
        {0, 0}, {6, 0}, {6, 4}, {3, 4}, {3, 6}, {0, 6}};
    const auto plane_patch = make_patch(*plane.value,
                                        {concave_outer, rectangle(1, 1, 2, 2, true)},
                                        plane_point);
    if (!cylinder_patch.face.value || !sphere_patch.face.value || !cone_patch.face.value ||
        !torus_patch.face.value || !plane_patch.face.value) return false;

    const double cylinder_expected = 3.0 * (1.0 * 1.0 - .2 * .2);
    const double sphere_expected =
        4.0 * (1.0 * (std::cos(.4) - std::cos(1.4)) -
               .2 * (std::cos(.7) - std::cos(.9)));
    const double cone_scale = std::tan(pi / 6) / std::cos(pi / 6);
    const double cone_expected = cone_scale * .5 *
        (1.5 * (4.0 * 4.0 - 1.0) - .3 * (3.0 * 3.0 - 2.0 * 2.0));
    const auto torus_rectangle_area = [](double u0, double v0, double u1, double v1) {
        return 2.0 * (u1 - u0) *
               (5.0 * (v1 - v0) + 2.0 * (std::sin(v1) - std::sin(v0)));
    };
    const double torus_expected = torus_rectangle_area(.3, .2, 1.8, 1.4) -
                                  torus_rectangle_area(.7, .5, 1.0, .8);
    if (!equal(topo.face_area(cylinder_patch.face), cylinder_expected) ||
        !equal(topo.face_area(sphere_patch.face), sphere_expected) ||
        !equal(topo.face_area(cone_patch.face), cone_expected) ||
        !equal(topo.face_area(torus_patch.face), torus_expected) ||
        !equal(topo.face_area(plane_patch.face), 29.0)) return false;

    const auto outside_hole_patch = make_patch(
        *plane.value, {rectangle(0, 0, 2, 2), rectangle(3, 3, 4, 4, true)}, plane_point);
    const auto out_of_domain_patch = make_patch(
        *sphere.value, {rectangle(.2, -.4, 1.2, .2)}, sphere_point);
    if (!outside_hole_patch.face.value || !out_of_domain_patch.face.value ||
        !failed(topo.face_area(outside_hole_patch.face), axiom::StatusCode::InvalidTopology,
                axiom::diag_codes::kTopoFaceInnerLoopInvalid) ||
        !failed(topo.face_area(out_of_domain_patch.face), axiom::StatusCode::InvalidTopology,
                axiom::diag_codes::kTopoFaceOuterLoopInvalid)) return false;

    // Trimmed and offset wrappers preserve the same UV contract while changing the metric.
    const auto trimmed = kernel.surfaces().make_trimmed(*cylinder.value, .1, 1.5, .2, 1.6);
    if (!trimmed.value) return false;
    const auto offset = kernel.surfaces().make_offset(*trimmed.value, 2.0);
    if (!offset.value) return false;
    const auto offset_cylinder_point = [](const axiom::Point2& uv) {
        return axiom::Point3{3 + 5 * std::cos(uv.x), -2 + 5 * std::sin(uv.x), 7 + uv.y};
    };
    const auto wrapped_patch = make_patch(*offset.value, {common_outer, common_hole},
                                          offset_cylinder_point);
    if (!wrapped_patch.face.value || !equal(topo.face_area(wrapped_patch.face),
                                             5.0 * (1.0 - .04))) return false;

    // Legacy straight-edged planar faces need no PCurve and retain exact compatibility.
    const auto box = kernel.primitives().box({0, 0, 0}, 2, 3, 4);
    const auto box_faces = box.value ? topo.faces_of_body(*box.value)
                                     : axiom::Result<std::vector<axiom::FaceId>>{};
    if (!box_faces.value || box_faces.value->empty()) return false;
    for (const auto face : *box_faces.value) {
        const auto legacy = topo.planar_face_area(face);
        if (!legacy.value || !equal(topo.face_area(face), *legacy.value)) return false;
    }

    const auto spline = kernel.surfaces().make_bspline(
        {{{0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 1}}});
    const auto broken_pc = kernel.pcurves().make_polyline(
        std::array<axiom::Point2, 2>{{{2.5, 2.5}, {2.7, 2.5}}});
    const auto crossing_pc = kernel.pcurves().make_polyline(
        std::array<axiom::Point2, 3>{{{.2, 1.4}, {.7, .1}, {1.2, 1.4}}});
    if (!spline.value || !broken_pc.value || !crossing_pc.value) return false;

    const auto runtime_before = kernel.runtime_store_counts();
    const auto objects_before = kernel.object_count_total();
    const auto geometry_before = kernel.geometry_count();
    if (!runtime_before.value || !objects_before.value || !geometry_before.value) return false;
    if (!failed(topo.face_area({}), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreInvalidHandle)) return false;
    {
        auto edit = kernel.topology().begin_transaction();
        const auto writes = edit.write_operation_count();
        if (edit.replace_surface(cylinder_patch.face, *same_cylinder.value).status !=
                axiom::StatusCode::Ok ||
            !equal(topo.face_area(cylinder_patch.face), cylinder_expected) ||
            edit.write_operation_count().value == writes.value ||
            edit.rollback().status != axiom::StatusCode::Ok ||
            !equal(topo.face_area(cylinder_patch.face), cylinder_expected)) return false;
    }
    {
        auto edit = kernel.topology().begin_transaction();
        if (edit.replace_surface(cylinder_patch.face, *sphere.value).status != axiom::StatusCode::Ok ||
            !failed(topo.face_area(cylinder_patch.face), axiom::StatusCode::InvalidTopology,
                    axiom::diag_codes::kTopoFaceOuterLoopInvalid) ||
            edit.rollback().status != axiom::StatusCode::Ok ||
            !equal(topo.face_area(cylinder_patch.face), cylinder_expected)) return false;
    }
    {
        auto edit = kernel.topology().begin_transaction();
        if (edit.replace_surface(cylinder_patch.face, *spline.value).status != axiom::StatusCode::Ok ||
            !failed(topo.face_area(cylinder_patch.face), axiom::StatusCode::NotImplemented,
                    axiom::diag_codes::kCoreOperationUnsupported) ||
            edit.rollback().status != axiom::StatusCode::Ok ||
            !equal(topo.face_area(cylinder_patch.face), cylinder_expected)) return false;
    }
    {
        auto edit = kernel.topology().begin_transaction();
        if (edit.set_coedge_pcurve(cylinder_patch.coedges.front(), {}).status != axiom::StatusCode::Ok ||
            !failed(topo.face_area(cylinder_patch.face), axiom::StatusCode::InvalidTopology,
                    axiom::diag_codes::kTopoCurveTopologyMismatch) ||
            edit.rollback().status != axiom::StatusCode::Ok ||
            !equal(topo.face_area(cylinder_patch.face), cylinder_expected)) return false;
    }
    {
        auto edit = kernel.topology().begin_transaction();
        if (edit.set_coedge_pcurve(cylinder_patch.coedges.front(), *broken_pc.value).status !=
                axiom::StatusCode::Ok ||
            !failed(topo.face_area(cylinder_patch.face), axiom::StatusCode::InvalidTopology,
                    axiom::diag_codes::kTopoFaceOuterLoopInvalid) ||
            edit.rollback().status != axiom::StatusCode::Ok ||
            !equal(topo.face_area(cylinder_patch.face), cylinder_expected)) return false;
    }
    {
        auto edit = kernel.topology().begin_transaction();
        if (edit.set_coedge_pcurve(cylinder_patch.coedges.front(), *crossing_pc.value).status !=
                axiom::StatusCode::Ok ||
            !failed(topo.face_area(cylinder_patch.face), axiom::StatusCode::InvalidTopology,
                    axiom::diag_codes::kTopoFaceOuterLoopInvalid) ||
            edit.rollback().status != axiom::StatusCode::Ok ||
            !equal(topo.face_area(cylinder_patch.face), cylinder_expected)) return false;
    }
    {
        auto edit = kernel.topology().begin_transaction();
        if (edit.delete_face(cylinder_patch.face).status != axiom::StatusCode::Ok ||
            !failed(topo.face_area(cylinder_patch.face), axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreInvalidHandle) ||
            edit.rollback().status != axiom::StatusCode::Ok ||
            !equal(topo.face_area(cylinder_patch.face), cylinder_expected)) return false;
    }
    const auto runtime_after = kernel.runtime_store_counts();
    if (!runtime_after.value ||
        runtime_after.value->curve_eval_cache_entries != runtime_before.value->curve_eval_cache_entries ||
        runtime_after.value->surface_eval_cache_entries != runtime_before.value->surface_eval_cache_entries ||
        runtime_after.value->mesh_records != runtime_before.value->mesh_records ||
        runtime_after.value->tessellation_cache_entries != runtime_before.value->tessellation_cache_entries ||
        runtime_after.value->face_tessellation_cache_entries != runtime_before.value->face_tessellation_cache_entries ||
        runtime_after.value->intersection_records != runtime_before.value->intersection_records) return false;
    if (kernel.object_count_total().value != objects_before.value ||
        kernel.geometry_count().value != geometry_before.value) return false;
    return true;
}

bool stage3_section_distance_regression() {
    axiom::Kernel kernel;
    auto& topo = kernel.topology().query();
    auto& query = kernel.query();
    const auto close = [](double a, double b) {
        return std::abs(a-b) <= 2e-8*std::max({1.0,std::abs(a),std::abs(b)});
    };
    const auto failed = [&](const auto& result, axiom::StatusCode status,
                            std::string_view code, std::string_view stage) {
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        const bool matches = !result.value && result.status == status && diagnostic.value &&
            std::any_of(diagnostic.value->issues.begin(),diagnostic.value->issues.end(),
                [&](const axiom::Issue& issue) { return issue.code == code && issue.stage == stage; });
        if (!matches) {
            std::cerr << "Expected failure " << code << " at " << stage << " actual status=" << static_cast<int>(result.status) << "\n";
            if (diagnostic.value) for (const auto& issue : diagnostic.value->issues)
                std::cerr << issue.code << " " << issue.stage << " " << issue.message << "\n";
        }
        return matches;
    };
    const auto check_section = [&](axiom::BodyId body, const axiom::Plane& plane, double area) {
        const auto actual = query.section_detailed(body,plane);
        const auto dedicated = topo.section(body,plane);
        if (!actual.value || !dedicated.value || !close(actual.value->area,area) ||
            !close(dedicated.value->area,area) || actual.value->triangles.size() != dedicated.value->triangles.size())
            return false;
        double triangle_area = 0;
        for (const auto& triangle : actual.value->triangles) {
            for (const auto index : triangle)
                if (index < 0 || static_cast<std::size_t>(index) >= actual.value->vertices.size()) return false;
            const auto a = actual.value->vertices[static_cast<std::size_t>(triangle[0])];
            const auto b = actual.value->vertices[static_cast<std::size_t>(triangle[1])];
            const auto c = actual.value->vertices[static_cast<std::size_t>(triangle[2])];
            const axiom::Vec3 ab{b.x-a.x,b.y-a.y,b.z-a.z}, ac{c.x-a.x,c.y-a.y,c.z-a.z};
            triangle_area += .5*std::hypot(ab.y*ac.z-ab.z*ac.y,ab.z*ac.x-ab.x*ac.z,ab.x*ac.y-ab.y*ac.x);
        }
        for (const auto& point : actual.value->vertices) {
            const double residual = (point.x-plane.origin.x)*plane.normal.x+
                (point.y-plane.origin.y)*plane.normal.y+(point.z-plane.origin.z)*plane.normal.z;
            if (!close(residual,0)) return false;
        }
        return close(triangle_area,area);
    };
    const auto check_distance = [&](axiom::BodyId first, axiom::BodyId second, double distance) {
        const auto result = query.closest_points(first,second);
        const auto dedicated = topo.closest_points(first,second);
        const auto scalar = query.min_distance(first,second);
        const auto reversed = query.min_distance(second,first);
        if (!result.value || !dedicated.value || !scalar.value || !reversed.value ||
            !close(result.value->distance,distance) || !close(dedicated.value->distance,distance) ||
            !close(*scalar.value,distance) || !close(*reversed.value,distance) ||
            result.value->first_face.value != dedicated.value->first_face.value ||
            result.value->second_face.value != dedicated.value->second_face.value ||
            result.value->first_shell.value != dedicated.value->first_shell.value ||
            result.value->second_shell.value != dedicated.value->second_shell.value) return false;
        const auto a = result.value->first_point, b = result.value->second_point;
        if (!close(std::hypot(a.x-b.x,a.y-b.y,a.z-b.z),distance)) return false;
        if (distance > 0 && (result.value->first_face.value == 0 || result.value->second_face.value == 0)) return false;
        const auto first_location = topo.locate_point(first,a);
        const auto second_location = topo.locate_point(second,b);
        if (!first_location.value || !second_location.value ||
            first_location.value->location == axiom::BodyPointLocation::Outside ||
            second_location.value->location == axiom::BodyPointLocation::Outside) return false;
        if (distance > 0 && (first_location.value->location != axiom::BodyPointLocation::Boundary ||
                            second_location.value->location != axiom::BodyPointLocation::Boundary)) return false;
        return true;
    };
    const auto box = kernel.primitives().box({0,0,0},1,1,1);
    const auto wedge = kernel.primitives().wedge({0,0,0},1,1,1);
    axiom::ProfileRef profile;
    profile.label = "stage3_section_concave_hole";
    profile.polygon_xyz = {{0,0,0},{6,0,0},{6,2,0},{2,2,0},{2,6,0},{0,6,0}};
    profile.holes_xyz = {{{.5,.5,0},{1.5,.5,0},{1.5,1.5,0},{.5,1.5,0}}};
    const auto prism = kernel.sweeps().extrude(profile,{0,0,1},5);
    if (!box.value || !wedge.value || !prism.value) return false;
    // Independent references: unit intercept triangle, L-polygon minus a unit
    // hole, and two disjoint diagonal rectangles of width 2*sqrt(2), height 5.
    if (!check_section(*box.value,{{1,0,0},{1,1,1}},std::sqrt(3.0)/2) ||
        !check_section(*wedge.value,{{0,0,.5},{0,0,1}},.5) ||
        !check_section(*prism.value,{{0,0,2},{0,0,1}},19) ||
        !check_section(*prism.value,{{5,0,0},{1,1,0}},20*std::sqrt(2.0)) ||
        !check_section(*prism.value,{{0,0,0},{0,0,-7}},19)) return false;
    const auto wedge_point = query.closest_point(*wedge.value,{1,1,.5});
    if (!wedge_point.value || !wedge_point.value->nearest_boundary ||
        wedge_point.value->location != axiom::BodyPointLocation::Outside ||
        !close(wedge_point.value->nearest_boundary->point.x,.5) ||
        !close(wedge_point.value->nearest_boundary->point.y,.5) ||
        !close(wedge_point.value->nearest_boundary->point.z,.5) ||
        !close(wedge_point.value->nearest_boundary->distance,1/std::sqrt(2.0))) return false;
    // Unequal wedge legs require the sloped support plane normal (dy,dx,0).
    // A square wedge would hide a stale (1,1,0) placeholder normal.
    const auto unequal_wedge = kernel.primitives().wedge({1,2,3},2,3,4);
    if (!unequal_wedge.value || !check_section(*unequal_wedge.value,{{0,0,5},{0,0,1}},3)) return false;
    const auto unequal_point = query.closest_point(*unequal_wedge.value,{3,5,5});
    if (!unequal_point.value || !unequal_point.value->nearest_boundary ||
        unequal_point.value->location != axiom::BodyPointLocation::Outside ||
        !close(unequal_point.value->nearest_boundary->point.x,1+8.0/13) ||
        !close(unequal_point.value->nearest_boundary->point.y,2+27.0/13) ||
        !close(unequal_point.value->nearest_boundary->point.z,5) ||
        !close(unequal_point.value->nearest_boundary->distance,6/std::sqrt(13.0))) return false;
    const auto cap = query.section_detailed(*prism.value,{{0,0,2},{0,0,1}});
    if (!cap.value) return false;
    // Every filled triangle centroid must be in material: no bridging the hole
    // or filling the concave missing corner, even though both are inside bbox.
    for (const auto& triangle : cap.value->triangles) {
        axiom::Point3 center{};
        for (const auto index : triangle) {
            const auto p = cap.value->vertices[static_cast<std::size_t>(index)];
            center.x += p.x/3; center.y += p.y/3; center.z += p.z/3;
        }
        const auto location = topo.locate_point(*prism.value,center);
        if (!location.value || location.value->location != axiom::BodyPointLocation::Inside) return false;
    }
    const auto world = [](const axiom::Point3& p) {
        return axiom::Point3{100+(p.x-p.z)/std::sqrt(2.0),-200+p.y,300+(p.x+p.z)/std::sqrt(2.0)};
    };
    auto rotated = profile;
    for (auto& p : rotated.polygon_xyz) p = world(p);
    for (auto& ring : rotated.holes_xyz) for (auto& p : ring) p = world(p);
    const auto rotated_body = kernel.sweeps().extrude(rotated,{-1,0,1},5);
    if (!rotated_body.value || !check_section(*rotated_body.value,{world({0,0,2}),{-1,0,1}},19)) return false;

    // Missing-corner plane intersects bbox but not the actual concave solid.
    for (const auto plane : std::array<axiom::Plane,2>{{{{9,0,0},{1,1,0}},{{0,0,20},{0,0,1}}}}) {
        const auto empty = query.section_detailed(*prism.value,plane);
        const auto mesh = query.section(*prism.value,plane);
        if (!empty.value || empty.value->area != 0 || !empty.value->vertices.empty() ||
            !empty.value->boundary_segments.empty() || !empty.value->contact_points.empty() ||
            empty.value->bbox.is_valid || !mesh.value || mesh.value->value != 0) return false;
    }
    // Face/edge/vertex tangencies use the same success semantics; no invented area.
    if (!check_section(*box.value,{{0,0,0},{0,0,1}},1) ||
        !check_section(*box.value,{{0,0,0},{1,1,0}},0) ||
        !check_section(*box.value,{{0,0,0},{1,1,1}},0)) return false;
    const auto edge = query.section_detailed(*box.value,{{0,0,0},{1,1,0}});
    const auto vertex = query.section_detailed(*box.value,{{0,0,0},{1,1,1}});
    if (!edge.value || edge.value->boundary_segments.empty() || !vertex.value || vertex.value->contact_points.empty())
        return false;
    // One representable step beyond a cap is still empty, even with a large
    // caller tolerance. It must not be snapped into a coplanar unit-area cap.
    axiom::BodySpatialQueryOptions broad_tolerance;
    broad_tolerance.position_tolerance = .1;
    const axiom::Plane near_miss{{0,0,std::nextafter(1.0,2.0)},{0,0,1}};
    const auto near_section = query.section_detailed(*box.value,near_miss,broad_tolerance);
    const auto near_dedicated = topo.section(*box.value,near_miss,broad_tolerance);
    const auto near_mesh = query.section(*box.value,near_miss);
    if (!near_section.value || !near_dedicated.value || !near_mesh.value || near_mesh.value->value != 0 ||
        near_section.value->area != 0 || !near_section.value->triangles.empty() ||
        !near_section.value->boundary_segments.empty() || !near_section.value->contact_points.empty() ||
        near_section.value->bbox.is_valid || near_dedicated.value->bbox.is_valid) return false;

    // A positive gap below the triangle clipping roundoff band is independently
    // representable near z=0. Distance queries must retain it rather than borrow
    // clipping's coplanarity band or the much larger modelling tolerance.
    constexpr double gap = 1e-18;
    const auto below = kernel.primitives().box({0,0,-1},1,1,1);
    const auto above = kernel.primitives().box({0,0,gap},1,1,1);
    if (!below.value || !above.value) return false;
    const auto tiny_distance = query.closest_points(*below.value,*above.value,broad_tolerance);
    const auto tiny_dedicated = topo.closest_points(*below.value,*above.value,broad_tolerance);
    const auto tiny_scalar = query.min_distance(*below.value,*above.value);
    const auto tiny_reversed = query.min_distance(*above.value,*below.value);
    if (!tiny_distance.value || !tiny_dedicated.value || !tiny_scalar.value || !tiny_reversed.value ||
        std::abs(tiny_distance.value->distance/gap-1) > 1e-10 ||
        std::abs(tiny_dedicated.value->distance/gap-1) > 1e-10 ||
        std::abs(*tiny_scalar.value/gap-1) > 1e-10 || std::abs(*tiny_reversed.value/gap-1) > 1e-10 ||
        tiny_distance.value->first_point.z != 0 || tiny_distance.value->second_point.z != gap ||
        tiny_distance.value->first_face.value == 0 || tiny_distance.value->second_face.value == 0) return false;

    const auto missing_corner = kernel.primitives().box({3,3,2},1,1,1);
    const auto in_hole = kernel.primitives().box({.75,.75,2},.5,.5,1);
    const auto tangent = kernel.primitives().box({2,3,2},1,1,1);
    if (!missing_corner.value || !in_hole.value || !tangent.value ||
        !check_distance(*prism.value,*missing_corner.value,1) ||
        !check_distance(*prism.value,*in_hole.value,.25) ||
        !check_distance(*prism.value,*tangent.value,0) || !check_distance(*box.value,*box.value,0)) return false;
    axiom::ProfileRef triangle_a{"diagonal_distance_a",{{0,0,0},{2,0,0},{0,2,0}}};
    axiom::ProfileRef triangle_b{"diagonal_distance_b",{{2,2,0},{1.5,2,0},{2,1.5,0}}};
    const auto a = kernel.sweeps().extrude(triangle_a,{0,0,1},1);
    const auto b = kernel.sweeps().extrude(triangle_b,{0,0,1},1);
    if (!a.value || !b.value || !check_distance(*a.value,*b.value,1.5/std::sqrt(2.0))) return false;
    // Skew ridge/ridge minimum occurs in the interiors of both edges. A has
    // z<=0, B has z>=1; their orthogonal ridge lines meet at (0,0) in XY.
    const axiom::ProfileRef ridge_a{"skew_ridge_a",{{-2,-1,-1},{-2,1,-1},{-2,0,0}}};
    const axiom::ProfileRef ridge_b{"skew_ridge_b",{{-1,-2,2},{0,-2,1},{1,-2,2}}};
    const auto ridge_first = kernel.sweeps().extrude(ridge_a,{1,0,0},4);
    const auto ridge_second = kernel.sweeps().extrude(ridge_b,{0,1,0},4);
    const auto point_touch = kernel.primitives().box({1,1,1},1,1,1);
    if (!ridge_first.value || !ridge_second.value || !point_touch.value ||
        !check_distance(*ridge_first.value,*ridge_second.value,1) ||
        !check_distance(*box.value,*point_touch.value,0)) return false;
    const auto point = query.closest_point(*prism.value,{1,1,2});
    const auto dedicated_point = topo.locate_point(*prism.value,{1,1,2});
    if (!point.value || !point.value->nearest_boundary || !dedicated_point.value ||
        !dedicated_point.value->nearest_boundary || point.value->location != axiom::BodyPointLocation::Outside ||
        !close(point.value->nearest_boundary->distance,.5) ||
        point.value->nearest_boundary->face.value != dedicated_point.value->nearest_boundary->face.value) return false;

    const auto outer = kernel.primitives().box({0,0,0},10,10,10);
    const auto cavity = kernel.primitives().box({2,2,2},6,6,6);
    const auto island = kernel.primitives().box({4,4,4},2,2,2);
    const auto floating = kernel.primitives().box({2.5,2.5,3},1,1,1);
    const auto contained = kernel.primitives().box({.2,.2,.2},.2,.2,.2);
    if (!outer.value || !cavity.value || !island.value || !floating.value || !contained.value) return false;
    const auto outer_shells = topo.shells_of_body(*outer.value);
    const auto cavity_shells = topo.shells_of_body(*cavity.value);
    const auto island_shells = topo.shells_of_body(*island.value);
    if (!outer_shells.value || !cavity_shells.value || !island_shells.value) return false;
    {
        auto txn = kernel.topology().begin_transaction();
        const auto hollow = txn.create_body(std::array{island_shells.value->front(),outer_shells.value->front(),
                                                       cavity_shells.value->front()});
        const auto writes = txn.write_operation_count();
        // Empty shell lists cannot create a body through the public transaction
        // API. Check that rejection without treating its absent value as a body.
        const auto empty = txn.create_body({});
        const auto empty_report = kernel.diagnostics().get(empty.diagnostic_id);
        if (empty.value || empty.status != axiom::StatusCode::OperationFailed || !empty_report.value ||
            !has_issue_code(*empty_report.value,axiom::diag_codes::kTxCommitFailure) ||
            !hollow.value || !check_section(*hollow.value,{{0,0,5},{0,0,1}},68) ||
            !check_distance(*hollow.value,*floating.value,.5) || !check_distance(*outer.value,*contained.value,0) ||
            txn.write_operation_count().value != writes.value || txn.rollback().status != axiom::StatusCode::Ok) return false;
        if (!failed(query.section_detailed(*hollow.value,{{0,0,5},{0,0,1}}),axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreInvalidHandle,"query.section.preflight")) return false;
    }

    const auto sphere = kernel.primitives().sphere({0,0,0},1);
    const auto curved_surface = kernel.surfaces().make_sphere({0,0,0},1);
    const auto displaced_plane = kernel.surfaces().make_plane({0,0,100},{0,0,1});
    const auto faces = topo.faces_of_body(*box.value);
    const auto node = kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"stage3:queries");
    if (!sphere.value || !curved_surface.value || !displaced_plane.value || !faces.value || !node.value) return false;
    // Planar face thicken now materializes a physical prism. Curved native
    // primitives retain explicit spatial support rejection below.
    const auto planar_thicken = kernel.sweeps().thicken(faces.value->front(),1);
    if (!planar_thicken.value || !query.mass_properties(*planar_thicken.value).value ||
        !query.closest_point(*planar_thicken.value,{0,0,0}).value) return false;
    // Individually resolvable components can exceed the combined plane-frame
    // resolution when very far apart. Do not silently erase the small component.
    const auto tiny = kernel.primitives().box({1e12,0,0},.001,.001,.001);
    const auto box_shells = topo.shells_of_body(*box.value);
    const auto tiny_shells = tiny.value ? topo.shells_of_body(*tiny.value)
                                       : axiom::Result<std::vector<axiom::ShellId>>{};
    if (!box_shells.value || !tiny_shells.value) return false;
    {
        auto txn = kernel.topology().begin_transaction();
        const auto wide = txn.create_body(std::array{box_shells.value->front(),tiny_shells.value->front()});
        if (!wide.value || !failed(query.section_detailed(*wide.value,{{0,0,.0005},{0,0,1}}),
            axiom::StatusCode::NumericalInstability,axiom::diag_codes::kQuerySectionFailure,"query.section.numeric") ||
            txn.rollback().status != axiom::StatusCode::Ok) return false;
    }
    const auto objects = kernel.object_count_total();
    const auto stores = kernel.runtime_store_counts();
    const auto invalid = kernel.eval_graph().is_invalid(*node.value);
    const auto recompute = kernel.eval_graph().recompute_count(*node.value);
    if (!objects.value || !stores.value) return false;
    for (const auto unsupported : {*sphere.value}) {
        if (!failed(query.section_detailed(unsupported,{{0,0,0},{0,0,1}}),axiom::StatusCode::NotImplemented,
                axiom::diag_codes::kCoreOperationUnsupported,"query.section.support_gate") ||
        !failed(query.section(unsupported,{{0,0,0},{0,0,1}}),axiom::StatusCode::NotImplemented,
                axiom::diag_codes::kCoreOperationUnsupported,"query.section.support_gate") ||
        !failed(query.min_distance(unsupported,*box.value),axiom::StatusCode::NotImplemented,
                axiom::diag_codes::kCoreOperationUnsupported,"query.distance.support_gate") ||
        !failed(query.closest_point(unsupported,{0,0,0}),axiom::StatusCode::NotImplemented,
                axiom::diag_codes::kCoreOperationUnsupported,"query.closest_point.support_gate")) return false;
    }
    if (!failed(query.section_detailed(*box.value,{{0,0,0},{0,0,0}}),axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreParameterOutOfRange,"query.section.input_gate")) return false;
    for (const double bad : {-1.0,std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
        axiom::BodySpatialQueryOptions options;
        options.position_tolerance = bad;
        if (!failed(query.section_detailed(*box.value,{{0,0,0},{0,0,1}},options),axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange,"query.section.input_gate") ||
            !failed(query.closest_points(*box.value,*prism.value,options),axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange,"query.distance.input_gate") ||
            !failed(query.closest_point(*box.value,{0,0,0},options),axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange,"query.closest_point.input_gate")) return false;
    }
    axiom::BodySpatialQueryOptions zero_budget;
    zero_budget.max_triangle_tests = 0;
    if (!failed(query.section_detailed(*box.value,{{0,0,0},{0,0,1}},zero_budget),axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreParameterOutOfRange,"query.section.input_gate") ||
        !failed(query.closest_points(*box.value,*prism.value,zero_budget),axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreParameterOutOfRange,"query.distance.input_gate")) return false;
    for (const double bad : {std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        if (!failed(query.section_detailed(*box.value,{{bad,0,0},{0,0,1}}),axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange,"query.section.input_gate") ||
            !failed(query.section_detailed(*box.value,{{0,0,0},{0,bad,1}}),axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange,"query.section.input_gate") ||
            !failed(query.closest_point(*box.value,{bad,0,0}),axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange,"query.closest_point.input_gate")) return false;
    }
    const auto section_work = query.section_detailed(*prism.value,{{0,0,2},{0,0,1}});
    const auto distance_work = query.closest_points(*prism.value,*in_hole.value);
    if (!section_work.value || !distance_work.value) return false;
    axiom::BodySpatialQueryOptions budget;
    budget.max_triangle_tests = section_work.value->triangle_tests;
    if (!query.section_detailed(*prism.value,{{0,0,2},{0,0,1}},budget).value) return false;
    --budget.max_triangle_tests;
    if (!failed(query.section_detailed(*prism.value,{{0,0,2},{0,0,1}},budget),axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreParameterOutOfRange,"query.section.budget")) return false;
    budget.max_triangle_tests = distance_work.value->triangle_tests;
    if (!query.closest_points(*prism.value,*in_hole.value,budget).value) return false;
    --budget.max_triangle_tests;
    if (!failed(query.closest_points(*prism.value,*in_hole.value,budget),axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreParameterOutOfRange,"query.distance.budget")) return false;
    for (int edit = 0; edit < 4; ++edit) {
        auto txn = kernel.topology().begin_transaction();
        const auto change = edit == 0 ? txn.delete_face(faces.value->front())
                          : edit == 1 ? txn.replace_surface(faces.value->front(),*curved_surface.value)
                          : edit == 2 ? txn.replace_surface(faces.value->front(),*displaced_plane.value)
                                      : txn.delete_body(*box.value);
        if (change.status != axiom::StatusCode::Ok) return false;
        const auto writes = txn.write_operation_count();
        const auto status = edit == 1 ? axiom::StatusCode::NotImplemented
                          : edit == 3 ? axiom::StatusCode::InvalidInput : axiom::StatusCode::InvalidTopology;
        const auto code = edit == 0 ? axiom::diag_codes::kTopoShellNotClosed
                        : edit == 1 ? axiom::diag_codes::kCoreOperationUnsupported
                        : edit == 2 ? axiom::diag_codes::kTopoCurveTopologyMismatch : axiom::diag_codes::kCoreInvalidHandle;
        if (!failed(query.section_detailed(*box.value,{{0,0,20},{0,0,1}}),status,code,
                    edit == 1 ? "query.section.support_gate" : "query.section.preflight") ||
            !failed(query.min_distance(*box.value,*prism.value),status,code,
                    edit == 1 ? "query.distance.support_gate" : "query.distance.preflight") ||
            !failed(query.closest_point(*box.value,{1,1,1}),status,code,
                    edit == 1 ? "query.closest_point.support_gate" : "query.closest_point.preflight") ||
            query.mass_properties(*box.value).value || txn.write_operation_count().value != writes.value ||
            txn.rollback().status != axiom::StatusCode::Ok ||
            !check_section(*box.value,{{1,0,0},{1,1,1}},std::sqrt(3.0)/2)) return false;
    }
    const auto after = kernel.runtime_store_counts();
    if (!after.value || kernel.object_count_total().value != objects.value ||
        after.value->mesh_records != stores.value->mesh_records ||
        after.value->intersection_records != stores.value->intersection_records ||
        after.value->tessellation_cache_entries != stores.value->tessellation_cache_entries ||
        after.value->face_tessellation_cache_entries != stores.value->face_tessellation_cache_entries ||
        after.value->curve_eval_cache_entries != stores.value->curve_eval_cache_entries ||
        after.value->surface_eval_cache_entries != stores.value->surface_eval_cache_entries ||
        kernel.eval_graph().is_invalid(*node.value).value != invalid.value ||
        kernel.eval_graph().recompute_count(*node.value).value != recompute.value) return false;
    // The legacy MeshId entry explicitly publishes only its successful filled
    // result; failure/empty queries above published nothing and filled no cache.
    const auto mesh = query.section(*prism.value,{{0,0,2},{0,0,1}});
    const auto count = kernel.convert().mesh_triangle_count(mesh.value ? *mesh.value : axiom::MeshId{});
    const auto published = kernel.runtime_store_counts();
    return mesh.value && mesh.value->value != 0 && count.value && *count.value == cap.value->triangles.size() &&
        published.value && published.value->mesh_records == stores.value->mesh_records+1 &&
        published.value->tessellation_cache_entries == stores.value->tessellation_cache_entries;
}

bool body_spatial_query_regression() {
    axiom::Kernel kernel;
    auto& topo = kernel.topology().query();
    using Location = axiom::BodyPointLocation;
    const auto close = [](double a, double b) {
        return std::abs(a - b) <= 2e-9 * std::max({1.0, std::abs(a), std::abs(b)});
    };
    const auto failed = [&](const auto& result, axiom::StatusCode status, std::string_view code) {
        const auto diagnostic = kernel.diagnostics().get(result.diagnostic_id);
        return result.status == status && !result.value && diagnostic.value &&
               has_issue_code(*diagnostic.value, code);
    };
    const auto intervals_match = [&](const axiom::Result<axiom::BodySegmentQuery>& result,
                                     const std::vector<axiom::BodySegmentInterval>& expected,
                                     double length) {
        if (result.status != axiom::StatusCode::Ok || !result.value ||
            result.diagnostic_id.value == 0 || result.value->intervals.size() != expected.size() ||
            !close(result.value->material_length, length)) return false;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            const auto& actual = result.value->intervals[i];
            if (actual.location != expected[i].location ||
                !close(actual.parameters.min, expected[i].parameters.min) ||
                !close(actual.parameters.max, expected[i].parameters.max)) return false;
        }
        return true;
    };
    const auto box = kernel.primitives().box({0,0,0}, 2,3,4);
    if (!box.value) return false;
    const auto shells = topo.shells_of_body(*box.value);
    const auto faces = topo.faces_of_body(*box.value);
    if (!shells.value || shells.value->size() != 1 || !faces.value) return false;
    // Face, edge and corner closest points are independent analytic projections.
    for (const auto& sample : std::array{
             std::array<double, 7>{-1,1,2, 0,1,2, 1},
             std::array<double, 7>{-1,-2,2, 0,0,2, std::sqrt(5.0)},
             std::array<double, 7>{-1,-2,-3, 0,0,0, std::sqrt(14.0)},
             std::array<double, 7>{.5,1.5,2, 0,1.5,2, .5}}) {
        const auto query = topo.locate_point(*box.value, {sample[0],sample[1],sample[2]});
        if (!query.value || !query.value->nearest_boundary || query.diagnostic_id.value == 0 ||
            query.value->location != (sample[0] < 0 ? Location::Outside : Location::Inside) ||
            !close(query.value->nearest_boundary->point.x, sample[3]) ||
            !close(query.value->nearest_boundary->point.y, sample[4]) ||
            !close(query.value->nearest_boundary->point.z, sample[5]) ||
            !close(query.value->nearest_boundary->distance, sample[6]) ||
            query.value->nearest_boundary->shell.value != shells.value->front().value ||
            std::none_of(faces.value->begin(), faces.value->end(), [&](axiom::FaceId id) {
                return id.value == query.value->nearest_boundary->face.value;
            }) || query.value->triangle_tests == 0) return false;
    }
    axiom::BodySpatialQueryOptions tolerance;
    tolerance.position_tolerance = 1e-4;
    for (const auto point : std::array<axiom::Point3, 5>{{{0,1,2}, {0,0,2}, {0,0,0},
                                                       {-5e-5,1,2}, {5e-5,1,2}}}) {
        const auto result = topo.locate_point(*box.value, point, tolerance);
        if (!result.value || result.value->location != Location::Boundary ||
            result.value->position_tolerance != tolerance.position_tolerance) return false;
    }
    const auto beyond_tolerance = topo.locate_point(*box.value, {-2e-4,1,2}, tolerance);
    if (!beyond_tolerance.value || beyond_tolerance.value->location != Location::Outside) return false;
    if (!intervals_match(topo.clip_segment(*box.value, {-1,1,2}, {3,1,2}),
                         {{{.25,.75}, Location::Inside}}, 2) ||
        !intervals_match(topo.clip_segment(*box.value, {3,1,2}, {-1,1,2}),
                         {{{.25,.75}, Location::Inside}}, 2) ||
        !intervals_match(topo.clip_segment(*box.value, {.5,1,2}, {1.5,1,2}),
                         {{{0,1}, Location::Inside}}, 1) ||
        !intervals_match(topo.clip_segment(*box.value, {0,1,2}, {1,1,2}),
                         {{{0,1}, Location::Inside}}, 1) ||
        !intervals_match(topo.clip_segment(*box.value, {0,1,2}, {-1,1,2}),
                         {{{0,0}, Location::Boundary}}, 0) ||
        !intervals_match(topo.clip_segment(*box.value, {-1,0,2}, {3,0,2}),
                         {{{.25,.75}, Location::Boundary}}, 0) ||
        !intervals_match(topo.clip_segment(*box.value, {-1,0,0}, {3,0,0}),
                         {{{.25,.75}, Location::Boundary}}, 0) ||
        !intervals_match(topo.clip_segment(*box.value, {-1,1,2}, {1,-1,2}),
                         {{{.5,.5}, Location::Boundary}}, 0) ||
        !intervals_match(topo.clip_segment(*box.value, {-1,1,-1}, {1,-1,1}),
                         {{{.5,.5}, Location::Boundary}}, 0) ||
        !intervals_match(topo.clip_segment(*box.value, {-1,-1,-1}, {3,3,3}),
                         {{{.25,.75}, Location::Inside}}, 2 * std::sqrt(3.0)) ||
        !intervals_match(topo.clip_segment(*box.value, {-1,4,2}, {3,4,2}), {}, 0)) return false;

    axiom::ProfileRef profile;
    profile.label = "spatial_concave_with_hole";
    profile.polygon_xyz = {{0,0,0}, {6,0,0}, {6,2,0}, {2,2,0}, {2,6,0}, {0,6,0}};
    profile.holes_xyz = {{{.5,.5,0}, {1.5,.5,0}, {1.5,1.5,0}, {.5,1.5,0}}};
    const auto prism = kernel.sweeps().extrude(profile, {0,0,1}, 5);
    if (!prism.value || !intervals_match(topo.clip_segment(*prism.value, {-1,1,2}, {7,1,2}),
        {{{1.0/8,1.5/8}, Location::Inside}, {{2.5/8,7.0/8}, Location::Inside}}, 5)) return false;
    // Both points lie in the body bbox, but one is in a through-hole and one in
    // the missing corner of the concave cap.  Neither can be classified by bbox.
    for (const auto point : std::array<axiom::Point3, 2>{{{1,1,2}, {4,4,2}}}) {
        const auto location = topo.locate_point(*prism.value, point);
        if (!location.value || location.value->location != Location::Outside ||
            !location.value->nearest_boundary) return false;
    }
    if (!intervals_match(topo.clip_segment(*prism.value, {-1,1,0}, {7,1,0}),
        {{{1.0/8,1.5/8}, Location::Boundary}, {{2.5/8,7.0/8}, Location::Boundary}}, 0)) return false;

    const auto outer = kernel.primitives().box({0,0,0}, 10,10,10);
    const auto cavity = kernel.primitives().box({2,2,2}, 6,6,6);
    const auto island = kernel.primitives().box({4,4,4}, 2,2,2);
    const auto separate = kernel.primitives().box({12,4,4}, 2,2,2);
    if (!outer.value || !cavity.value || !island.value || !separate.value) return false;
    const auto outer_shells = topo.shells_of_body(*outer.value);
    const auto cavity_shells = topo.shells_of_body(*cavity.value);
    const auto island_shells = topo.shells_of_body(*island.value);
    const auto separate_shells = topo.shells_of_body(*separate.value);
    if (!outer_shells.value || !cavity_shells.value || !island_shells.value || !separate_shells.value) return false;
    {
        auto txn = kernel.topology().begin_transaction();
        const std::array unordered_shells{island_shells.value->front(), separate_shells.value->front(),
                                         cavity_shells.value->front(), outer_shells.value->front()};
        const auto body = txn.create_body(unordered_shells);
        const std::array reversed_shells{outer_shells.value->front(), cavity_shells.value->front(),
                                        separate_shells.value->front(), island_shells.value->front()};
        const auto reordered = txn.create_body(reversed_shells);
        if (!body.value || !reordered.value) return false;
        for (const auto& sample : std::array{
                 std::pair{axiom::Point3{1,5,5}, Location::Inside},
                 std::pair{axiom::Point3{3,5,5}, Location::Outside},
                 std::pair{axiom::Point3{5,5,5}, Location::Inside},
                 std::pair{axiom::Point3{11,5,5}, Location::Outside},
                 std::pair{axiom::Point3{13,5,5}, Location::Inside},
                 std::pair{axiom::Point3{2,5,5}, Location::Boundary}}) {
            const auto result = topo.locate_point(*body.value, sample.first);
            const auto again = topo.locate_point(*reordered.value, sample.first);
            if (!result.value || !again.value || result.value->location != sample.second ||
                again.value->location != sample.second || !result.value->nearest_boundary ||
                !again.value->nearest_boundary ||
                result.value->nearest_boundary->shell.value != again.value->nearest_boundary->shell.value ||
                result.value->nearest_boundary->face.value != again.value->nearest_boundary->face.value ||
                !close(result.value->nearest_boundary->distance, again.value->nearest_boundary->distance)) return false;
        }
        const auto query = topo.clip_segment(*body.value, {-1,5,5}, {15,5,5});
        if (!intervals_match(query,
            {{{1.0/16,3.0/16}, Location::Inside}, {{5.0/16,7.0/16}, Location::Inside},
             {{9.0/16,11.0/16}, Location::Inside}, {{13.0/16,15.0/16}, Location::Inside}}, 8) ||
            !intervals_match(topo.clip_segment(*reordered.value, {15,5,5}, {-1,5,5}),
            {{{1.0/16,3.0/16}, Location::Inside}, {{5.0/16,7.0/16}, Location::Inside},
             {{9.0/16,11.0/16}, Location::Inside}, {{13.0/16,15.0/16}, Location::Inside}}, 8)) return false;
        // Exact work evidence allows successful replay at the reported budget;
        // removing one operation must fail without exposing partial intervals.
        if (!query.value || query.value->triangle_tests <= 1) return false;
        axiom::BodySpatialQueryOptions budget;
        budget.max_triangle_tests = query.value->triangle_tests;
        if (!intervals_match(topo.clip_segment(*body.value, {-1,5,5}, {15,5,5}, budget),
                             query.value->intervals, 8)) return false;
        --budget.max_triangle_tests;
        const auto writes = txn.write_operation_count();
        if (!failed(topo.clip_segment(*body.value, {-1,5,5}, {15,5,5}, budget),
                    axiom::StatusCode::InvalidInput, axiom::diag_codes::kCoreParameterOutOfRange) ||
            txn.write_operation_count().value != writes.value ||
            txn.rollback().status != axiom::StatusCode::Ok ||
            !failed(topo.locate_point(*body.value, {1,5,5}), axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreInvalidHandle)) return false;
    }

    // Caller point tolerance must not thicken material or erase a narrow shell
    // layer.  Shell separation remains governed by the kernel modelling policy.
    const auto thin_cavity = kernel.primitives().box({1e-4,1e-4,1e-4}, 9.9998,9.9998,9.9998);
    const auto thin_shells = thin_cavity.value ? topo.shells_of_body(*thin_cavity.value)
                                              : axiom::Result<std::vector<axiom::ShellId>>{};
    if (!thin_shells.value) return false;
    {
        auto txn = kernel.topology().begin_transaction();
        const auto thin = txn.create_body(std::array{outer_shells.value->front(), thin_shells.value->front()});
        axiom::BodySpatialQueryOptions coarse_point_tolerance;
        coarse_point_tolerance.position_tolerance = .1;
        if (!thin.value || !intervals_match(topo.clip_segment(*thin.value, {-1,5,5}, {11,5,5}, coarse_point_tolerance),
            {{{1.0/12,1.0001/12}, Location::Inside}, {{10.9999/12,11.0/12}, Location::Inside}}, .0002) ||
            txn.rollback().status != axiom::StatusCode::Ok) return false;
    }
    // A very long segment can compress distinct model surfaces below the
    // normalized parameter resolution.  It must fail rather than erase material.
    if (!failed(topo.clip_segment(*box.value, {-1e15,1,2}, {1e15,1,2}),
                axiom::StatusCode::NumericalInstability, axiom::diag_codes::kQuerySectionFailure)) return false;

    // Rotation, translation and scaling change world geometry but not normalized
    // segment parameters.  This is a real rotated prism, not a transformed bbox.
    for (const double scale : {1e-3, 1.0, 100.0}) {
        const auto world = [scale](double x, double y, double z) {
            return axiom::Point3{100 + scale * (x - z) / std::sqrt(2.0),
                                 -200 + scale * y, 300 + scale * (x + z) / std::sqrt(2.0)};
        };
        axiom::ProfileRef rotated;
        rotated.label = "spatial_rotated_prism";
        rotated.polygon_xyz = {world(0,0,0), world(2,0,0), world(2,3,0), world(0,3,0)};
        const auto body = kernel.sweeps().extrude(rotated, {-1,0,1}, 4 * scale);
        if (!body.value || !intervals_match(topo.clip_segment(*body.value, world(-1,1,2), world(3,1,2)),
                                           {{{.25,.75}, Location::Inside}}, 2 * scale)) return false;
        const auto point = topo.locate_point(*body.value, world(-1,1,2));
        const auto expected = world(0,1,2);
        if (!point.value || point.value->location != Location::Outside || !point.value->nearest_boundary ||
            !close(point.value->nearest_boundary->distance, scale) ||
            !close(point.value->nearest_boundary->point.x, expected.x) ||
            !close(point.value->nearest_boundary->point.y, expected.y) ||
            !close(point.value->nearest_boundary->point.z, expected.z)) return false;
    }

    const auto objects_before = kernel.object_count_total();
    const auto geometry_before = kernel.geometry_count();
    const auto stores_before = kernel.runtime_store_counts();
    const auto node = kernel.eval_graph().register_node(axiom::NodeKind::Analysis, "spatial:readonly");
    if (!node.value || !objects_before.value || !geometry_before.value || !stores_before.value) return false;
    const auto invalid_before = kernel.eval_graph().is_invalid(*node.value);
    const auto recomputes_before = kernel.eval_graph().recompute_count(*node.value);
    const auto audit_before = topo.query_operation_count();
    const auto inside = topo.locate_point(*box.value, {.5,1,2});
    const auto audit_after = topo.query_operation_count();
    if (!inside.value || !audit_before.value || !audit_after.value ||
        *audit_after.value != *audit_before.value + 1) return false;
    axiom::BodySpatialQueryOptions budget;
    budget.max_triangle_tests = inside.value->triangle_tests;
    if (!topo.locate_point(*box.value, {.5,1,2}, budget).value) return false;
    --budget.max_triangle_tests;
    if (!failed(topo.locate_point(*box.value, {.5,1,2}, budget), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    budget.max_triangle_tests = 1;
    if (!failed(topo.clip_segment(*box.value, {-1,1,2}, {3,1,2}, budget), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    for (const double value : {-1.0, std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::quiet_NaN()}) {
        axiom::BodySpatialQueryOptions bad;
        bad.position_tolerance = value;
        if (!failed(topo.locate_point(*box.value, {1,1,1}, bad), axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange) ||
            !failed(topo.clip_segment(*box.value, {-1,1,2}, {3,1,2}, bad), axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    }
    for (const double value : {std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::quiet_NaN()}) {
        if (!failed(topo.locate_point(*box.value, {value,0,0}), axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange) ||
            !failed(topo.clip_segment(*box.value, {0,0,0}, {0,value,0}), axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    }
    budget.max_triangle_tests = 0;
    if (!failed(topo.locate_point(*box.value, {1,1,1}, budget), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreParameterOutOfRange) ||
        !failed(topo.clip_segment(*box.value, {0,0,0}, {1,0,0}, budget), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreParameterOutOfRange) ||
        !failed(topo.locate_point({}, {0,0,0}), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreInvalidHandle) ||
        !failed(topo.clip_segment({}, {0,0,0}, {1,0,0}), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreInvalidHandle) ||
        !failed(topo.clip_segment(*box.value, {0,0,0}, {0,0,0}), axiom::StatusCode::DegenerateGeometry,
                axiom::diag_codes::kGeoDegenerateGeometry) ||
        !failed(topo.clip_segment(*box.value, {0,0,0}, {5e-5,0,0}, tolerance), axiom::StatusCode::DegenerateGeometry,
                axiom::diag_codes::kGeoDegenerateGeometry) ||
        !failed(topo.clip_segment(*box.value, {-1e308,0,0}, {1e308,0,0}), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreParameterOutOfRange)) return false;
    const auto stores_after = kernel.runtime_store_counts();
    if (kernel.object_count_total().value != objects_before.value ||
        kernel.geometry_count().value != geometry_before.value || !stores_after.value ||
        stores_after.value->curve_eval_cache_entries != stores_before.value->curve_eval_cache_entries ||
        stores_after.value->surface_eval_cache_entries != stores_before.value->surface_eval_cache_entries ||
        stores_after.value->mesh_records != stores_before.value->mesh_records ||
        stores_after.value->intersection_records != stores_before.value->intersection_records ||
        stores_after.value->tessellation_cache_entries != stores_before.value->tessellation_cache_entries ||
        stores_after.value->face_tessellation_cache_entries != stores_before.value->face_tessellation_cache_entries ||
        kernel.eval_graph().is_invalid(*node.value).value != invalid_before.value ||
        kernel.eval_graph().recompute_count(*node.value).value != recomputes_before.value) return false;

    const auto sphere = kernel.surfaces().make_sphere({0,0,0}, 1);
    const auto displaced_plane = kernel.surfaces().make_plane({0,0,100}, {0,0,1});
    if (!sphere.value || !displaced_plane.value) return false;
    for (int edit_kind = 0; edit_kind < 4; ++edit_kind) {
        auto txn = kernel.topology().begin_transaction();
        const auto status = edit_kind == 0 ? txn.delete_face(faces.value->front())
                          : edit_kind == 1 ? txn.replace_surface(faces.value->front(), *sphere.value)
                          : edit_kind == 2 ? txn.delete_body(*box.value)
                                           : txn.replace_surface(faces.value->front(), *displaced_plane.value);
        if (status.status != axiom::StatusCode::Ok) return false;
        const auto expected_status = edit_kind == 1 ? axiom::StatusCode::NotImplemented
                                   : edit_kind == 2 ? axiom::StatusCode::InvalidInput
                                                    : axiom::StatusCode::InvalidTopology;
        const auto expected_code = edit_kind == 0 ? axiom::diag_codes::kTopoShellNotClosed
                                 : edit_kind == 1 ? axiom::diag_codes::kCoreOperationUnsupported
                                 : edit_kind == 2 ? axiom::diag_codes::kCoreInvalidHandle
                                                  : axiom::diag_codes::kTopoCurveTopologyMismatch;
        const auto writes = txn.write_operation_count();
        if (!failed(topo.locate_point(*box.value, {1,1,2}), expected_status, expected_code) ||
            !failed(topo.clip_segment(*box.value, {-1,1,2}, {3,1,2}), expected_status, expected_code) ||
            txn.write_operation_count().value != writes.value ||
            txn.rollback().status != axiom::StatusCode::Ok ||
            !intervals_match(topo.clip_segment(*box.value, {-1,1,2}, {3,1,2}),
                             {{{.25,.75}, Location::Inside}}, 2) ||
            !topo.locate_point(*box.value, {1,1,2}).value) return false;
    }
    // An intersecting or touching multi-shell body must fail before either
    // point or segment classification, including queries far outside its bbox.
    for (const double x : {1.0, 2.0}) {
        const auto touching = kernel.primitives().box({x,0,0}, 2,3,4);
        if (!touching.value) return false;
        const auto other_shells = topo.shells_of_body(*touching.value);
        if (!other_shells.value) return false;
        auto txn = kernel.topology().begin_transaction();
        const auto body = txn.create_body(std::array{shells.value->front(), other_shells.value->front()});
        if (!body.value ||
            !failed(topo.locate_point(*body.value, {-100,0,0}), axiom::StatusCode::InvalidTopology,
                    axiom::diag_codes::kQueryShellArrangementInvalid) ||
            !failed(topo.clip_segment(*body.value, {-100,0,0}, {-99,0,0}), axiom::StatusCode::InvalidTopology,
                    axiom::diag_codes::kQueryShellArrangementInvalid) ||
            txn.rollback().status != axiom::StatusCode::Ok) return false;
    }
    return true;
}

bool topology_mass_properties_regression() {
    axiom::Kernel kernel;
    auto& topo = kernel.topology().query();
    const auto close = [](double actual, double expected, double scale = 1.0) {
        return std::abs(actual - expected) <= 2e-9 * std::max(scale, std::abs(expected));
    };
    const auto failed = [&](const auto& result,
                            axiom::StatusCode status, std::string_view code) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        return result.status == status && !result.value && report.value &&
               has_issue_code(*report.value, code);
    };
    const auto same_properties = [&](const axiom::MassProperties& actual,
                                     const axiom::MassProperties& expected) {
        if (!close(actual.volume, expected.volume) ||
            !close(actual.area, expected.area) ||
            !close(actual.centroid.x, expected.centroid.x) ||
            !close(actual.centroid.y, expected.centroid.y) ||
            !close(actual.centroid.z, expected.centroid.z)) return false;
        for (std::size_t i = 0; i < actual.inertia.size(); ++i)
            if (!close(actual.inertia[i], expected.inertia[i], 10.0)) return false;
        return true;
    };
    const auto result_matches = [&](const axiom::Result<axiom::MassProperties>& result,
                                    const axiom::MassProperties& expected) {
        return result.value && same_properties(*result.value, expected);
    };

    // Analytic translated cuboids exercise volume, surface area, centroid and
    // every diagonal inertia component independently of topology triangulation.
    for (const auto& model : std::array{
             std::array<double, 6>{1, 2, 3, 2, 3, 4},
             std::array<double, 6>{-7, 5, -2, .5, 2, 3},
             std::array<double, 6>{1e3, -2e3, 3e3, 4, .25, 8}}) {
        const auto body = kernel.primitives().box(
            {model[0], model[1], model[2]}, model[3], model[4], model[5]);
        if (!body.value) return false;
        const auto shells = topo.shells_of_body(*body.value);
        if (!shells.value || shells.value->size() != 1) return false;
        const double volume = model[3] * model[4] * model[5];
        axiom::MassProperties expected;
        expected.volume = volume;
        expected.area = 2 * (model[3] * model[4] + model[4] * model[5] +
                             model[3] * model[5]);
        expected.centroid = {model[0] + model[3] / 2, model[1] + model[4] / 2,
                             model[2] + model[5] / 2};
        expected.inertia = {
            volume * (model[4] * model[4] + model[5] * model[5]) / 12, 0, 0,
            0, volume * (model[3] * model[3] + model[5] * model[5]) / 12, 0,
            0, 0, volume * (model[3] * model[3] + model[4] * model[4]) / 12};
        const auto shell_properties = topo.shell_mass_properties(shells.value->front());
        const auto body_properties = topo.body_mass_properties(*body.value);
        if (!shell_properties.value || !body_properties.value ||
            !same_properties(*shell_properties.value, expected) ||
            !same_properties(*body_properties.value, expected)) return false;
    }

    // A concave cap plus a disjoint hole exercises constrained face
    // triangulation, side walls and topology reconstruction from a real Ops BRep.
    axiom::ProfileRef holed;
    holed.label = "query_mass_concave_hole";
    holed.polygon_xyz = {{0,0,0}, {6,0,0}, {6,2,0}, {2,2,0}, {2,6,0}, {0,6,0}};
    holed.holes_xyz = {{{.5,.5,0}, {1.5,.5,0}, {1.5,1.5,0}, {.5,1.5,0}}};
    const auto prism = kernel.sweeps().extrude(holed, {0, 0, 2}, 5);
    if (!prism.value) return false;
    const auto prism_properties = topo.body_mass_properties(*prism.value);
    const auto cached_properties = kernel.query().mass_properties(*prism.value);
    if (!prism_properties.value || !cached_properties.value ||
        !close(prism_properties.value->volume, 95) ||
        !close(prism_properties.value->area, 178) ||
        !close(prism_properties.value->centroid.x, 43.0 / 19.0) ||
        !close(prism_properties.value->centroid.y, 43.0 / 19.0) ||
        !close(prism_properties.value->centroid.z, 2.5) ||
        !same_properties(*prism_properties.value, *cached_properties.value)) return false;

    // Multiple shells are independent solid components; body aggregation must
    // apply the parallel-axis theorem about the combined centroid.
    const auto left = kernel.primitives().box({0, 0, 0}, 1, 1, 1);
    const auto right = kernel.primitives().box({3, 0, 0}, 1, 1, 1);
    const auto left_shells = left.value ? topo.shells_of_body(*left.value)
                                        : axiom::Result<std::vector<axiom::ShellId>>{};
    const auto right_shells = right.value ? topo.shells_of_body(*right.value)
                                          : axiom::Result<std::vector<axiom::ShellId>>{};
    if (!left_shells.value || !right_shells.value || left_shells.value->size() != 1 ||
        right_shells.value->size() != 1) return false;
    {
        auto txn = kernel.topology().begin_transaction();
        const std::array combined_shells{left_shells.value->front(), right_shells.value->front()};
        const auto combined = txn.create_body(combined_shells);
        if (!combined.value) return false;
        const auto properties = topo.body_mass_properties(*combined.value);
        const auto public_properties = kernel.query().mass_properties(*combined.value);
        if (!properties.value || !public_properties.value ||
            !same_properties(*public_properties.value,*properties.value)) return false;
        if (!properties.value || !close(properties.value->volume, 2) ||
            !close(properties.value->area, 12) ||
            !close(properties.value->centroid.x, 2) ||
            !close(properties.value->centroid.y, .5) ||
            !close(properties.value->centroid.z, .5) ||
            !close(properties.value->inertia[0], 1.0 / 3.0) ||
            !close(properties.value->inertia[4], 29.0 / 6.0) ||
            !close(properties.value->inertia[8], 29.0 / 6.0)) return false;
        // Deleting one material component is a successful geometry change. The
        // creation-time bbox and two source-body references are not mass authority.
        const auto savepoint = txn.create_savepoint();
        if (!savepoint.value || txn.delete_shell(right_shells.value->front()).status != axiom::StatusCode::Ok)
            return false;
        const auto edited = kernel.query().mass_properties(*combined.value);
        if (!edited.value || !close(edited.value->volume,1) || !close(edited.value->area,6) ||
            !close(edited.value->centroid.x,.5) || !close(edited.value->centroid.y,.5) ||
            !close(edited.value->centroid.z,.5) || !close(edited.value->inertia[0],1.0/6) ||
            !close(edited.value->inertia[4],1.0/6) || !close(edited.value->inertia[8],1.0/6) ||
            txn.rollback_to_savepoint(*savepoint.value).status != axiom::StatusCode::Ok ||
            !result_matches(kernel.query().mass_properties(*combined.value),*properties.value) ||
            txn.rollback().status != axiom::StatusCode::Ok ||
            !failed(topo.body_mass_properties(*combined.value), axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreInvalidHandle)) return false;
    }

    // Strict nesting alternates material, void and material-island roles.  The
    // shell order is intentionally not spatial order, proving that depth is
    // derived from geometry rather than body insertion order or shell winding.
    const auto nesting_outer = kernel.primitives().box({0, 0, 0}, 10, 10, 10);
    const auto nesting_void = kernel.primitives().box({2, 2, 2}, 6, 6, 6);
    const auto nesting_island = kernel.primitives().box({4, 4, 4}, 2, 2, 2);
    const auto outer_shells = nesting_outer.value
        ? topo.shells_of_body(*nesting_outer.value)
        : axiom::Result<std::vector<axiom::ShellId>>{};
    const auto void_shells = nesting_void.value
        ? topo.shells_of_body(*nesting_void.value)
        : axiom::Result<std::vector<axiom::ShellId>>{};
    const auto island_shells = nesting_island.value
        ? topo.shells_of_body(*nesting_island.value)
        : axiom::Result<std::vector<axiom::ShellId>>{};
    if (!outer_shells.value || !void_shells.value || !island_shells.value ||
        outer_shells.value->size() != 1 || void_shells.value->size() != 1 ||
        island_shells.value->size() != 1) return false;
    {
        auto txn = kernel.topology().begin_transaction();
        const std::array nested_shells{island_shells.value->front(),
                                       outer_shells.value->front(),
                                       void_shells.value->front()};
        const auto nested = txn.create_body(nested_shells);
        if (!nested.value) return false;
        const auto regions = topo.body_shell_regions(*nested.value);
        const auto properties = topo.body_mass_properties(*nested.value);
        const auto public_properties = kernel.query().mass_properties(*nested.value);
        if (!properties.value || !public_properties.value ||
            !same_properties(*public_properties.value,*properties.value)) return false;
        if (!regions.value || regions.value->size() != 3 || !properties.value ||
            (*regions.value)[0].shell.value != island_shells.value->front().value ||
            (*regions.value)[0].role != axiom::BodyShellRole::Material ||
            (*regions.value)[0].nesting_depth != 2 ||
            !(*regions.value)[0].parent_shell ||
            (*regions.value)[0].parent_shell->value != void_shells.value->front().value ||
            (*regions.value)[1].role != axiom::BodyShellRole::Material ||
            (*regions.value)[1].nesting_depth != 0 ||
            (*regions.value)[1].parent_shell ||
            (*regions.value)[2].role != axiom::BodyShellRole::Void ||
            (*regions.value)[2].nesting_depth != 1 ||
            !(*regions.value)[2].parent_shell ||
            (*regions.value)[2].parent_shell->value != outer_shells.value->front().value ||
            !close(properties.value->volume, 792) ||
            !close(properties.value->area, 840) ||
            !close(properties.value->centroid.x, 5) ||
            !close(properties.value->centroid.y, 5) ||
            !close(properties.value->centroid.z, 5) ||
            !close(properties.value->inertia[0], 15376, 15376) ||
            !close(properties.value->inertia[4], 15376, 15376) ||
            !close(properties.value->inertia[8], 15376, 15376)) return false;
        for (const auto index : std::array<std::size_t, 6>{1, 2, 3, 5, 6, 7})
            if (!close(properties.value->inertia[index], 0)) return false;

        const auto writes = txn.write_operation_count();
        const auto regions_again = topo.body_shell_regions(*nested.value);
        if (!writes.value || !regions_again.value ||
            txn.write_operation_count().value != writes.value ||
            txn.commit().status != axiom::StatusCode::Ok) return false;
        auto delete_txn = kernel.topology().begin_transaction();
        const auto deletion = delete_txn.delete_body(*nested.value);
        const auto deleted_mass = topo.body_mass_properties(*nested.value);
        if (deletion.status != axiom::StatusCode::Ok ||
            !failed(deleted_mass, axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreInvalidHandle) ||
            delete_txn.rollback().status != axiom::StatusCode::Ok ||
            !topo.body_mass_properties(*nested.value).value) return false;
    }

    // An eccentric cavity exercises signed first moments, the signed
    // parallel-axis theorem and non-zero products of inertia.
    const auto eccentric_void = kernel.primitives().box({1, 2, 3}, 2, 3, 4);
    const auto eccentric_shells = eccentric_void.value
        ? topo.shells_of_body(*eccentric_void.value)
        : axiom::Result<std::vector<axiom::ShellId>>{};
    if (!eccentric_shells.value || eccentric_shells.value->size() != 1) return false;
    {
        auto txn = kernel.topology().begin_transaction();
        const std::array hollow_shells{outer_shells.value->front(),
                                       eccentric_shells.value->front()};
        const auto hollow = txn.create_body(hollow_shells);
        if (!hollow.value) return false;
        const auto properties = topo.body_mass_properties(*hollow.value);
        const auto public_properties = kernel.query().mass_properties(*hollow.value);
        if (!properties.value || !public_properties.value ||
            !same_properties(*public_properties.value,*properties.value)) return false;
        const auto regions = topo.body_shell_regions(*hollow.value);
        const double volume = 1000.0 - 24.0;
        const axiom::Point3 expected_centroid{
            (5000.0 - 24.0 * 2.0) / volume,
            (5000.0 - 24.0 * 3.5) / volume,
            (5000.0 - 24.0 * 5.0) / volume};
        const auto shift_tensor = [](double mass, const axiom::Point3& from,
                                     const axiom::Point3& to) {
            const double x = from.x - to.x;
            const double y = from.y - to.y;
            const double z = from.z - to.z;
            return std::array<double, 9>{
                mass * (y*y + z*z), -mass*x*y, -mass*x*z,
                -mass*x*y, mass * (x*x + z*z), -mass*y*z,
                -mass*x*z, -mass*y*z, mass * (x*x + y*y)};
        };
        std::array<double, 9> expected_inertia{
            1000.0 * 200.0 / 12.0, 0, 0,
            0, 1000.0 * 200.0 / 12.0, 0,
            0, 0, 1000.0 * 200.0 / 12.0};
        const std::array<double, 9> cavity_centroidal{
            24.0 * 25.0 / 12.0, 0, 0,
            0, 24.0 * 20.0 / 12.0, 0,
            0, 0, 24.0 * 13.0 / 12.0};
        const auto outer_shift = shift_tensor(1000, {5,5,5}, expected_centroid);
        const auto cavity_shift = shift_tensor(24, {2,3.5,5}, expected_centroid);
        for (std::size_t i = 0; i < expected_inertia.size(); ++i)
            expected_inertia[i] += outer_shift[i] - cavity_centroidal[i] - cavity_shift[i];
        if (!properties.value || !regions.value || regions.value->size() != 2 ||
            (*regions.value)[1].role != axiom::BodyShellRole::Void ||
            !close(properties.value->volume, volume) ||
            !close(properties.value->area, 652) ||
            !close(properties.value->centroid.x, expected_centroid.x) ||
            !close(properties.value->centroid.y, expected_centroid.y) ||
            !close(properties.value->centroid.z, expected_centroid.z)) return false;
        for (std::size_t i = 0; i < expected_inertia.size(); ++i)
            if (!close(properties.value->inertia[i], expected_inertia[i], 20000)) return false;
        if (txn.rollback().status != axiom::StatusCode::Ok) return false;
    }

    // Independent shells remain additive and have no parent.
    {
        auto txn = kernel.topology().begin_transaction();
        const std::array disjoint_shells{left_shells.value->front(),
                                         right_shells.value->front()};
        const auto disjoint = txn.create_body(disjoint_shells);
        if (!disjoint.value) return false;
        const auto disjoint_regions = topo.body_shell_regions(*disjoint.value);
        if (!disjoint_regions.value || disjoint_regions.value->size() != 2 ||
            (*disjoint_regions.value)[0].role != axiom::BodyShellRole::Material ||
            (*disjoint_regions.value)[0].nesting_depth != 0 ||
            (*disjoint_regions.value)[0].parent_shell ||
            (*disjoint_regions.value)[1].role != axiom::BodyShellRole::Material ||
            (*disjoint_regions.value)[1].nesting_depth != 0 ||
            (*disjoint_regions.value)[1].parent_shell ||
            txn.rollback().status != axiom::StatusCode::Ok) return false;
    }

    // Crossing, face-touching and coincident shell boundaries are ambiguous:
    // neither relationship nor mass query may return a partial answer.
    const auto crossing_box = kernel.primitives().box({9, 4, 4}, 2, 2, 2);
    const auto touching_box = kernel.primitives().box({10, 2, 2}, 1, 1, 1);
    const auto coincident_box = kernel.primitives().box({0, 0, 0}, 10, 10, 10);
    const auto crossing_shells = crossing_box.value
        ? topo.shells_of_body(*crossing_box.value)
        : axiom::Result<std::vector<axiom::ShellId>>{};
    const auto touching_shells = touching_box.value
        ? topo.shells_of_body(*touching_box.value)
        : axiom::Result<std::vector<axiom::ShellId>>{};
    const auto coincident_shells = coincident_box.value
        ? topo.shells_of_body(*coincident_box.value)
        : axiom::Result<std::vector<axiom::ShellId>>{};
    if (!crossing_shells.value || !touching_shells.value || !coincident_shells.value)
        return false;
    for (const auto conflicting_shell : std::array{
             crossing_shells.value->front(), touching_shells.value->front(),
             coincident_shells.value->front()}) {
        auto txn = kernel.topology().begin_transaction();
        const std::array ambiguous_shells{outer_shells.value->front(), conflicting_shell};
        const auto ambiguous = txn.create_body(ambiguous_shells);
        if (!ambiguous.value) return false;
        const auto regions = topo.body_shell_regions(*ambiguous.value);
        const auto properties = topo.body_mass_properties(*ambiguous.value);
        const auto regions_report = kernel.diagnostics().get(regions.diagnostic_id);
        if (regions.status != axiom::StatusCode::InvalidTopology || regions.value ||
            !regions_report.value ||
            !has_issue_code(*regions_report.value,
                            axiom::diag_codes::kQueryShellArrangementInvalid) ||
            !failed(properties, axiom::StatusCode::InvalidTopology,
                    axiom::diag_codes::kQueryShellArrangementInvalid) ||
            txn.rollback().status != axiom::StatusCode::Ok) return false;
    }

    const auto reference_box = kernel.primitives().box({10, 20, 30}, 2, 3, 4);
    const auto reference_shells = reference_box.value
        ? topo.shells_of_body(*reference_box.value)
        : axiom::Result<std::vector<axiom::ShellId>>{};
    const auto original = reference_box.value
        ? topo.body_mass_properties(*reference_box.value)
        : axiom::Result<axiom::MassProperties>{};
    if (!reference_box.value || !reference_shells.value || reference_shells.value->size() != 1 ||
        !original.value) return false;
    const auto face_ids = topo.faces_of_body(*reference_box.value);
    const auto sphere = kernel.surfaces().make_sphere({0, 0, 0}, 1);
    if (!face_ids.value || face_ids.value->empty() || !sphere.value) return false;

    const auto eval_node = kernel.eval_graph().register_node(
        axiom::NodeKind::Geometry,
        std::string("body:") + std::to_string(reference_box.value->value));
    if (!eval_node.value ||
        kernel.eval_graph().recompute(*eval_node.value).status != axiom::StatusCode::Ok)
        return false;
    const auto eval_invalid_before = kernel.eval_graph().is_invalid(*eval_node.value);
    const auto eval_recomputes_before = kernel.eval_graph().recompute_count(*eval_node.value);

    const auto objects_before = kernel.object_count_total();
    const auto geometry_before = kernel.geometry_count();
    const auto runtime_before = kernel.runtime_store_counts();
    if (!objects_before.value || !geometry_before.value || !runtime_before.value ||
        !topo.shell_mass_properties(reference_shells.value->front()).value ||
        !failed(topo.shell_mass_properties({}), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreInvalidHandle) ||
        !failed(topo.body_mass_properties({}), axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreInvalidHandle)) return false;
    const auto audit_before = topo.query_operation_count();
    const auto audited_properties = topo.body_mass_properties(*reference_box.value);
    const auto audit_after = topo.query_operation_count();
    const auto runtime_after = kernel.runtime_store_counts();
    if (!audited_properties.value || !audit_before.value || !audit_after.value ||
        *audit_after.value != *audit_before.value + 1 || !runtime_after.value ||
        kernel.object_count_total().value != objects_before.value ||
        kernel.geometry_count().value != geometry_before.value ||
        runtime_after.value->curve_eval_cache_entries != runtime_before.value->curve_eval_cache_entries ||
        runtime_after.value->surface_eval_cache_entries != runtime_before.value->surface_eval_cache_entries ||
        runtime_after.value->mesh_records != runtime_before.value->mesh_records ||
        runtime_after.value->tessellation_cache_entries != runtime_before.value->tessellation_cache_entries ||
        runtime_after.value->face_tessellation_cache_entries != runtime_before.value->face_tessellation_cache_entries ||
        runtime_after.value->intersection_records != runtime_before.value->intersection_records ||
        kernel.eval_graph().is_invalid(*eval_node.value).value != eval_invalid_before.value ||
        kernel.eval_graph().recompute_count(*eval_node.value).value != eval_recomputes_before.value)
        return false;

    {
        auto txn = kernel.topology().begin_transaction();
        if (txn.delete_face(face_ids.value->front()).status != axiom::StatusCode::Ok) return false;
        const auto writes = txn.write_operation_count();
        if (!failed(topo.body_mass_properties(*reference_box.value), axiom::StatusCode::InvalidTopology,
                    axiom::diag_codes::kTopoShellNotClosed) ||
            txn.write_operation_count().value != writes.value ||
            txn.rollback().status != axiom::StatusCode::Ok ||
            !result_matches(topo.body_mass_properties(*reference_box.value), *original.value)) return false;
    }
    {
        auto txn = kernel.topology().begin_transaction();
        if (txn.replace_surface(face_ids.value->front(), *sphere.value).status != axiom::StatusCode::Ok)
            return false;
        const auto writes = txn.write_operation_count();
        if (!failed(topo.body_mass_properties(*reference_box.value), axiom::StatusCode::NotImplemented,
                    axiom::diag_codes::kCoreOperationUnsupported) ||
            txn.write_operation_count().value != writes.value ||
            txn.rollback().status != axiom::StatusCode::Ok ||
            !result_matches(topo.body_mass_properties(*reference_box.value), *original.value)) return false;
    }
    {
        auto txn = kernel.topology().begin_transaction();
        if (txn.delete_body(*reference_box.value).status != axiom::StatusCode::Ok ||
            !failed(topo.body_mass_properties(*reference_box.value), axiom::StatusCode::InvalidInput,
                    axiom::diag_codes::kCoreInvalidHandle) ||
            txn.rollback().status != axiom::StatusCode::Ok ||
            !result_matches(topo.body_mass_properties(*reference_box.value), *original.value)) return false;
    }

    // A doubled planar triangle is topologically closed (each edge is used
    // twice) but encloses no volume, so it must not leak a zero-valued success.
    // Replacing a shared straight edge by an explicitly trimmed Bezier must
    // fail as unsupported before any endpoint-chord integration/classification.
    for (const bool curved_boundary : {false, true}) {
        auto txn = kernel.topology().begin_transaction();
        const std::array<axiom::Point3, 3> points{{{0,0,7}, {2,0,7}, {0,3,7}}};
        std::array<axiom::VertexId, 3> vertices{};
        std::array<axiom::EdgeId, 3> edges{};
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto vertex = txn.create_vertex(points[i]);
            if (!vertex.value) return false;
            vertices[i] = *vertex.value;
        }
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto j = (i + 1) % points.size();
            const auto curve = curved_boundary && i == 0
                ? kernel.curves().make_bezier(std::array<axiom::Point3, 3>{{points[i], {1,-1,7}, points[j]}})
                : kernel.curves().make_line_segment(points[i], points[j]);
            const auto edge = !curve.value ? axiom::Result<axiom::EdgeId>{}
                : curved_boundary && i == 0
                    ? txn.create_trimmed_edge(*curve.value, 0, 1, vertices[i], vertices[j])
                    : txn.create_edge(*curve.value, vertices[i], vertices[j]);
            if (!edge.value) return false;
            edges[i] = *edge.value;
        }
        std::array<axiom::CoedgeId, 3> front_coedges{};
        std::array<axiom::CoedgeId, 3> back_coedges{};
        for (std::size_t i = 0; i < edges.size(); ++i) {
            const auto front = txn.create_coedge(edges[i], false);
            const auto back = txn.create_coedge(edges[2 - i], true);
            if (!front.value || !back.value) return false;
            front_coedges[i] = *front.value;
            back_coedges[i] = *back.value;
        }
        const auto front_loop = txn.create_loop(front_coedges);
        const auto back_loop = txn.create_loop(back_coedges);
        const auto front_plane = kernel.surfaces().make_plane(points[0], {0,0,1});
        const auto back_plane = kernel.surfaces().make_plane(points[0], {0,0,-1});
        const auto front_face = front_loop.value && front_plane.value
            ? txn.create_face(*front_plane.value, *front_loop.value, {})
            : axiom::Result<axiom::FaceId>{};
        const auto back_face = back_loop.value && back_plane.value
            ? txn.create_face(*back_plane.value, *back_loop.value, {})
            : axiom::Result<axiom::FaceId>{};
        if (!front_face.value || !back_face.value) return false;
        const std::array faces{*front_face.value, *back_face.value};
        const auto shell = txn.create_shell(faces);
        const auto body = shell.value
            ? txn.create_body(std::array{*shell.value})
            : axiom::Result<axiom::BodyId>{};
        const auto expected_status = curved_boundary ? axiom::StatusCode::NotImplemented
                                                     : axiom::StatusCode::DegenerateGeometry;
        const auto expected_code = curved_boundary ? axiom::diag_codes::kCoreOperationUnsupported
                                                   : axiom::diag_codes::kGeoDegenerateGeometry;
        if (!shell.value || !body.value ||
            !failed(topo.shell_mass_properties(*shell.value), expected_status, expected_code) ||
            !failed(topo.body_mass_properties(*body.value), expected_status, expected_code) ||
            !failed(topo.locate_point(*body.value, {0,0,7}), expected_status, expected_code) ||
            !failed(topo.clip_segment(*body.value, {-1,0,7}, {3,0,7}), expected_status, expected_code) ||
            !failed(kernel.query().section_detailed(*body.value, {{0,0,7},{0,0,1}}), expected_status, expected_code) ||
            !failed(kernel.query().closest_point(*body.value, {0,0,7}), expected_status, expected_code) ||
            !failed(kernel.query().closest_points(*body.value, *reference_box.value), expected_status, expected_code) ||
            txn.rollback().status != axiom::StatusCode::Ok) return false;
    }
    return true;
}

bool stage3_eval_rollback_consistency_regression() {
    axiom::Kernel kernel;
    auto& topo=kernel.topology().query();
    auto& eval=kernel.eval_graph();
    const axiom::ProfileRef profile{"eval_consistency",{{0,0,0},{2,0,0},{2,3,0},{0,3,0}}};
    const auto body=kernel.sweeps().extrude(profile,{0,0,1},4);
    const auto other=kernel.sweeps().extrude(profile,{0,0,1},5);
    if (!body.value || !other.value) return false;
    const auto node=eval.register_node(axiom::NodeKind::Analysis,"body:"+std::to_string(body.value->value));
    const auto consumer=eval.register_node(axiom::NodeKind::Analysis,"consistency:downstream");
    const auto unrelated=eval.register_node(axiom::NodeKind::Analysis,"body:"+std::to_string(other.value->value));
    const auto faces=topo.faces_of_body(*body.value);
    const auto shells=topo.shells_of_body(*body.value);
    const auto sources=topo.source_faces_of_body(*body.value);
    const auto original_mass=kernel.query().mass_properties(*body.value);
    const auto original_mesh=kernel.convert().brep_to_mesh(*body.value,{});
    const auto displaced=kernel.surfaces().make_plane({0,0,100},{0,0,1});
    const auto pcurve=kernel.pcurves().make_polyline(std::array<axiom::Point2,2>{{{0,0},{1,0}}});
    if (!node.value || !consumer.value || !unrelated.value || !faces.value || faces.value->empty() ||
        !shells.value || shells.value->size()!=1 || !sources.value || !original_mass.value ||
        !original_mesh.value || !displaced.value || !pcurve.value ||
        eval.add_dependency(*consumer.value,*node.value).status!=axiom::StatusCode::Ok) return false;
    const auto dirty=[&](bool expected) {
        return eval.is_invalid(*node.value).value==std::optional<bool>{expected} &&
            eval.is_invalid(*consumer.value).value==std::optional<bool>{expected} &&
            eval.is_invalid(*unrelated.value).value==std::optional<bool>{false};
    };
    const auto restored=[&] {
        const auto mass=kernel.query().mass_properties(*body.value);
        const auto section=kernel.query().section_detailed(*body.value,{{0,0,2},{0,0,1}});
        const auto nearest=kernel.query().closest_point(*body.value,{-1,1,2});
        const auto distance=kernel.query().min_distance(*body.value,*other.value);
        if (!mass.value || !section.value || std::abs(section.value->area-6)>1e-8 || !nearest.value ||
            !nearest.value->nearest_boundary || std::abs(nearest.value->nearest_boundary->distance-1)>1e-8 ||
            !distance.value || std::abs(*distance.value)>1e-8 ||
            std::abs(mass.value->volume-24)>1e-8 || std::abs(mass.value->area-52)>1e-8 ||
            topo.faces_of_body(*body.value).value!=faces.value || topo.source_faces_of_body(*body.value).value!=sources.value ||
            kernel.convert().brep_to_mesh(*body.value,{}).value!=original_mesh.value ||
            kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok) return false;
        for (std::size_t i=0; i<9; ++i)
            if (std::abs(mass.value->inertia[i]-original_mass.value->inertia[i])>1e-8) return false;
        const auto bindings=eval.nodes_of_body(*body.value);
        return bindings.value && bindings.value->size()==1 && bindings.value->front().value==node.value->value;
    };
    {
        auto txn=kernel.topology().begin_transaction();
        const auto savepoint=txn.create_savepoint();
        const auto writes=txn.write_operation_count();
        if (!savepoint.value || !writes.value ||
            txn.replace_surface(faces.value->front(),{}).status!=axiom::StatusCode::InvalidInput ||
            txn.write_operation_count().value!=writes.value || !dirty(false) ||
            txn.rollback_to_savepoint(*savepoint.value).status!=axiom::StatusCode::Ok || !dirty(false)) return false;
        if (txn.replace_surface(faces.value->front(),*displaced.value).status!=axiom::StatusCode::Ok || !dirty(true)) return false;
        // The edited support invalidates the warm body cache. All tessellation
        // entry points must reject it without allocations, cache counter changes,
        // Eval recomputes, or changes to the caller's transaction write set.
        const auto fingerprint=[&] {
            const auto r=kernel.runtime_store_counts();
            const auto c=kernel.tessellation_cache_stats();
            if (!r.value || !c.value) return std::array<std::uint64_t,14>{};
            return std::array{kernel.next_object_id().value.value_or(0),kernel.object_count_total().value.value_or(0),
                r.value->mesh_records,r.value->tessellation_cache_entries,r.value->face_tessellation_cache_entries,
                r.value->surface_eval_cache_entries,c.value->body_cache_hits,c.value->body_cache_misses,
                c.value->body_cache_stale_evictions,c.value->face_cache_hits,c.value->face_cache_misses,
                c.value->face_cache_stale_evictions,txn.write_operation_count().value.value_or(0),
                eval.total_recompute_count().value.value_or(0)};
        };
        const auto before_conversion=fingerprint();
        const auto rejects=[&](const axiom::Result<axiom::MeshId>& result) {
            const auto diagnostic=kernel.diagnostics().get(result.diagnostic_id);
            return result.status==axiom::StatusCode::OperationFailed && !result.value && diagnostic.value &&
                std::any_of(diagnostic.value->issues.begin(),diagnostic.value->issues.end(),[](const axiom::Issue& issue) {
                    return issue.code==axiom::diag_codes::kTesFailure && issue.stage=="rep.tessellation.face";
                }) && fingerprint()==before_conversion && dirty(true);
        };
        const axiom::TessellationOptions cold{.083,13,true};
        if (!rejects(kernel.convert().brep_to_mesh(*body.value,{})) ||
            !rejects(kernel.convert().brep_to_mesh(*body.value,cold)) ||
            !rejects(kernel.convert().brep_to_mesh_local(*body.value,*faces.value,cold)) ||
            !rejects(kernel.convert().brep_to_mesh_shell(*body.value,shells.value->front(),cold))) return false;
        const auto batch=kernel.convert().brep_to_mesh_batch(std::array{*other.value,*body.value},cold);
        const auto roundtrip=kernel.convert().verify_brep_mesh_round_trip(*body.value,cold);
        if (batch.status==axiom::StatusCode::Ok || batch.value || roundtrip.status==axiom::StatusCode::Ok ||
            roundtrip.value || fingerprint()!=before_conversion || !dirty(true) ||
            !kernel.convert().inspect_mesh(*original_mesh.value).value) return false;
        const auto failed=kernel.query().mass_properties(*body.value);
        const auto diagnostic=kernel.diagnostics().get(failed.diagnostic_id);
        if (failed.value || !diagnostic.value ||
            std::none_of(diagnostic.value->issues.begin(),diagnostic.value->issues.end(),[](const axiom::Issue& issue) {
                return issue.stage=="query.mass_properties.preflight";
            })) return false;
        // Graph recompute is administrative; it may run while a query is rejected.
        // Undo must dirty these consumers again rather than restore a clean flag.
        if (eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok || !dirty(false) ||
            txn.rollback_to_savepoint(*savepoint.value).status!=axiom::StatusCode::Ok || !dirty(true) || !restored() ||
            eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok || !dirty(false) ||
            txn.rollback_to_savepoint(*savepoint.value).status!=axiom::StatusCode::Ok || !dirty(false) ||
            txn.rollback().status!=axiom::StatusCode::Ok || !dirty(false)) return false;
    }
    // PCurve rebinding on a polyhedral face must dirty the same body even though
    // it never held analytic mass eligibility. Its mesh key also follows the bind.
    const auto edges=topo.edges_of_body(*body.value);
    const auto vertices=topo.vertices_of_body(*body.value);
    const auto coedges=edges.value && !edges.value->empty() ? topo.coedges_of_edge(edges.value->front())
        : axiom::Result<std::vector<axiom::CoedgeId>>{};
    const auto owners=edges.value && !edges.value->empty() ? topo.faces_of_edge(edges.value->front())
        : axiom::Result<std::vector<axiom::FaceId>>{};
    const auto endpoints=edges.value && !edges.value->empty() ? topo.vertices_of_edge(edges.value->front())
        : axiom::Result<std::array<axiom::VertexId,2>>{};
    if (!coedges.value || coedges.value->empty() || !owners.value || owners.value->empty() ||
        !vertices.value || vertices.value->size()!=8 || !endpoints.value) return false;
    // This fixed extrude allocates the four input profile vertices followed by
    // the four vertices translated by Z=4. Handles recover that known fixture
    // order; no private vertex record or triangulator supplies the coordinates.
    auto ordered_vertices=*vertices.value;
    std::sort(ordered_vertices.begin(),ordered_vertices.end(),[](auto lhs,auto rhs) { return lhs.value<rhs.value; });
    std::array<axiom::Point3,2> points{};
    for (std::size_t i=0; i<points.size(); ++i) {
        const auto vertex=std::find(ordered_vertices.begin(),ordered_vertices.end(),(*endpoints.value)[i]);
        if (vertex==ordered_vertices.end()) return false;
        const auto index=static_cast<std::size_t>(vertex-ordered_vertices.begin());
        points[i]=profile.polygon_xyz[index%4];
        if (index>=4) points[i].z+=4;
    }
    // Coedges and faces are allocated together, one face at a time. Selecting
    // their minima identifies the earliest owner without index iteration order.
    const auto coedge=*std::min_element(coedges.value->begin(),coedges.value->end(),
        [](auto lhs,auto rhs) { return lhs.value<rhs.value; });
    const auto owner=*std::min_element(owners.value->begin(),owners.value->end(),
        [](auto lhs,auto rhs) { return lhs.value<rhs.value; });
    const auto owner_surface=topo.surface_of_face(owner);
    const auto plane=owner_surface.value ? kernel.surface_service().eval(*owner_surface.value,0,0,1)
        : axiom::Result<axiom::SurfaceEvalResult>{};
    if (!plane.value) return false;
    std::array<axiom::Point2,2> uv{};
    for (std::size_t i=0; i<points.size(); ++i) {
        const axiom::Vec3 d{points[i].x-plane.value->point.x,points[i].y-plane.value->point.y,points[i].z-plane.value->point.z};
        uv[i]={d.x*plane.value->du.x+d.y*plane.value->du.y+d.z*plane.value->du.z,
               d.x*plane.value->dv.x+d.y*plane.value->dv.y+d.z*plane.value->dv.z};
        const auto mapped=kernel.surface_service().eval(*owner_surface.value,uv[i].x,uv[i].y,0);
        if (!mapped.value || std::abs(mapped.value->point.x-points[i].x)>1e-12 ||
            std::abs(mapped.value->point.y-points[i].y)>1e-12 || std::abs(mapped.value->point.z-points[i].z)>1e-12) return false;
    }
    const auto matching_pcurve=kernel.pcurves().make_polyline(uv);
    if (!matching_pcurve.value) return false;
    {
        auto txn=kernel.topology().begin_transaction();
        if (txn.set_coedge_pcurve(coedge,*pcurve.value).status!=axiom::StatusCode::Ok || !dirty(true)) return false;
        const auto fingerprint=[&] {
            const auto r=kernel.runtime_store_counts();
            const auto c=kernel.tessellation_cache_stats();
            if (!r.value || !c.value) return std::array<std::uint64_t,14>{};
            return std::array{kernel.next_object_id().value.value_or(0),kernel.object_count_total().value.value_or(0),
                r.value->mesh_records,r.value->tessellation_cache_entries,r.value->face_tessellation_cache_entries,
                r.value->surface_eval_cache_entries,c.value->body_cache_hits,c.value->body_cache_misses,
                c.value->body_cache_stale_evictions,c.value->face_cache_hits,c.value->face_cache_misses,
                c.value->face_cache_stale_evictions,txn.write_operation_count().value.value_or(0),
                eval.total_recompute_count().value.value_or(0)};
        };
        const auto before=fingerprint();
        const auto rejected=kernel.convert().brep_to_mesh(*body.value,{});
        const auto diagnostic=kernel.diagnostics().get(rejected.diagnostic_id);
        if (rejected.status!=axiom::StatusCode::OperationFailed || rejected.value || !diagnostic.value ||
            std::none_of(diagnostic.value->issues.begin(),diagnostic.value->issues.end(),[](const axiom::Issue& issue) {
                return issue.code==axiom::diag_codes::kTesFailure && issue.stage=="rep.tessellation.face";
            }) || fingerprint()!=before || !dirty(true) || txn.rollback().status!=axiom::StatusCode::Ok ||
            !dirty(true) || !restored() || eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok || !dirty(false)) {
            std::cerr << "Eval mismatched PCurve rejection/rollback failed\n";
            return false;
        }
    }
    {
        auto txn=kernel.topology().begin_transaction();
        if (txn.set_coedge_pcurve(coedge,*matching_pcurve.value).status!=axiom::StatusCode::Ok || !dirty(true)) return false;
        const auto changed=kernel.convert().brep_to_mesh(*body.value,{});
        if (!changed.value || changed.value==original_mesh.value ||
            eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok || !dirty(false) ||
            txn.rollback().status!=axiom::StatusCode::Ok || !dirty(true) || !restored() ||
            eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok || !dirty(false)) {
            std::cerr << "Eval matching PCurve cache identity/rollback failed\n";
            return false;
        }
    }
    for (int removal=0; removal<3; ++removal) {
        auto txn=kernel.topology().begin_transaction();
        const auto removed=removal==0 ? txn.delete_face(faces.value->front())
            : removal==1 ? txn.delete_shell(shells.value->front()) : txn.delete_body(*body.value);
        if (removed.status!=axiom::StatusCode::Ok || !dirty(true) ||
            kernel.query().mass_properties(*body.value).value ||
            eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok || !dirty(false) ||
            txn.rollback().status!=axiom::StatusCode::Ok || !dirty(true) || !restored() ||
            eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok || !dirty(false)) return false;
    }
    {
        auto txn=kernel.topology().begin_transaction();
        if (txn.replace_surface(faces.value->front(),*displaced.value).status!=axiom::StatusCode::Ok ||
            eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok || !dirty(false)) return false;
        // Scope exit follows the same undo/dirty propagation path.
    }
    if (!dirty(true) || !restored() || eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok) return false;
    axiom::TopologyCancellationSource cancellation;
    auto cancelled=kernel.topology().begin_transaction(cancellation.token());
    if (cancelled.delete_body(*body.value).status!=axiom::StatusCode::Ok ||
        eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok || !cancellation.request_cancellation() ||
        cancelled.poll_cancellation().status!=axiom::StatusCode::OperationFailed || !dirty(true) || !restored()) return false;
    // A successful committed support replacement keeps the same physical
    // boundary, but the representation must track the new support identity.
    const auto support=topo.surface_of_face(faces.value->front());
    const auto sample=support.value ? kernel.surface_service().eval(*support.value,0,0,1)
        : axiom::Result<axiom::SurfaceEvalResult>{};
    if (!sample.value) return false;
    const auto equivalent=kernel.surfaces().make_plane(sample.value->point,sample.value->normal);
    if (!equivalent.value || eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok) return false;
    axiom::MeshId committed_mesh{};
    {
        auto txn=kernel.topology().begin_transaction();
        if (txn.replace_surface(faces.value->front(),*equivalent.value).status!=axiom::StatusCode::Ok || !dirty(true)) return false;
        const auto mesh=kernel.convert().brep_to_mesh(*body.value,{});
        if (!mesh.value || mesh.value==original_mesh.value || txn.commit().status!=axiom::StatusCode::Ok || !dirty(true)) return false;
        committed_mesh=*mesh.value;
    }
    const auto committed_mass=kernel.query().mass_properties(*body.value);
    const auto committed_section=kernel.query().section_detailed(*body.value,{{0,0,2},{0,0,1}});
    if (!committed_mass.value || std::abs(committed_mass.value->volume-24)>1e-8 ||
        std::abs(committed_mass.value->area-52)>1e-8 || !committed_section.value ||
        std::abs(committed_section.value->area-6)>1e-8 || topo.faces_of_body(*body.value).value!=faces.value ||
        topo.source_faces_of_body(*body.value).value!=sources.value ||
        topo.surface_of_face(faces.value->front()).value!=equivalent.value ||
        kernel.convert().brep_to_mesh(*body.value,{}).value!=std::optional<axiom::MeshId>{committed_mesh} ||
        kernel.validate().validate_all(*body.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok ||
        eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok || !dirty(false)) return false;
    for (std::size_t i=0; i<9; ++i)
        if (std::abs(committed_mass.value->inertia[i]-original_mass.value->inertia[i])>1e-8) return false;
    {
        auto txn=kernel.topology().begin_transaction();
        if (txn.replace_surface(faces.value->front(),*displaced.value).status!=axiom::StatusCode::Ok ||
            kernel.convert().brep_to_mesh(*body.value,{}).value ||
            eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok ||
            txn.rollback().status!=axiom::StatusCode::Ok || !dirty(true) ||
            topo.surface_of_face(faces.value->front()).value!=equivalent.value ||
            kernel.convert().brep_to_mesh(*body.value,{}).value!=std::optional<axiom::MeshId>{committed_mesh} ||
            topo.source_faces_of_body(*body.value).value!=sources.value ||
            eval.recompute(*consumer.value).status!=axiom::StatusCode::Ok || !dirty(false)) return false;
    }
    const auto map_consistency=kernel.eval_graph_store_maps_consistent();
    return map_consistency.value && *map_consistency.value;
}

// S3-EXIT: one executable matrix for the declared body classes and all four
// physical query columns. Modeling variants, full inertia references and
// edit/rollback/representation chains remain in the dedicated Stage 3 tests.
bool stage3_exit_support_matrix_regression() {
    axiom::Kernel kernel;
    auto& topo = kernel.topology().query();
    auto& query = kernel.query();
    const double pi = std::acos(-1.0);
    const auto close = [](double actual, double expected) {
        return std::abs(actual-expected) <= 2e-8*std::max(1.0,std::abs(expected));
    };
    const auto point_equal = [&](const axiom::Point3& actual, const axiom::Point3& expected) {
        return close(actual.x,expected.x) && close(actual.y,expected.y) && close(actual.z,expected.z);
    };
    const auto rejected = [&](const auto& result, std::string_view stage, bool invalid = false) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        return !result.value && result.status == (invalid ? axiom::StatusCode::InvalidInput : axiom::StatusCode::NotImplemented) &&
            report.value && std::any_of(report.value->issues.begin(),report.value->issues.end(),
                [&](const axiom::Issue& issue) {
                    return issue.stage == stage && issue.code == (invalid ? axiom::diag_codes::kCoreInvalidHandle :
                        axiom::diag_codes::kCoreOperationUnsupported);
                });
    };
    const auto box = kernel.primitives().box({0,0,0},2,3,4);
    const auto wedge = kernel.primitives().wedge({0,0,0},2,3,4);
    const auto remote = kernel.primitives().box({50,50,50},1,1,1);
    const axiom::ProfileRef rectangle{"exit_rectangle",{{0,0,0},{2,0,0},{2,3,0},{0,3,0}}};
    const axiom::ProfileRef holed{"exit_holed",{{0,0,0},{4,0,0},{4,4,0},{0,4,0}},
        {{{1,1,0},{3,1,0},{3,3,0},{1,3,0}}}};
    const auto extrude = kernel.sweeps().extrude(holed,{0,0,1},3);
    const auto rail = kernel.curves().make_line_segment({0,0,0},{0,0,4});
    const auto sweep = rail.value ? kernel.sweeps().sweep(rectangle,*rail.value) : axiom::Result<axiom::BodyId>{};
    auto upper = rectangle;
    for (auto& point : upper.polygon_xyz) point.z = 4;
    const auto loft = kernel.sweeps().loft(std::array{rectangle,upper});
    const axiom::ProfileRef meridian{"exit_meridian",{{2,0,0},{3,0,0},{3,0,4},{2,0,4}}};
    const auto revolve = kernel.sweeps().revolve_between(meridian,{{0,0,0},{0,0,1}},0,-pi/2);
    const auto faces = box.value ? topo.faces_of_body(*box.value) : axiom::Result<std::vector<axiom::FaceId>>{};
    if (!box.value || !wedge.value || !remote.value || !extrude.value || !sweep.value || !loft.value ||
        !revolve.value || !faces.value || faces.value->empty()) return false;
    const auto thicken = kernel.sweeps().thicken(faces.value->front(),2);
    const std::array analytic{
        kernel.primitives().sphere({0,0,0},2),
        kernel.primitives().cylinder({0,0,0},{0,0,1},2,6),
        kernel.primitives().cone({0,0,0},{0,0,1},std::atan(1.0/3),6),
        kernel.primitives().torus({0,0,0},{0,0,1},5,2)};
    const auto legacy = kernel.sweeps().extrude(axiom::ProfileRef{"exit_label_only",{},{}},{0,0,1},4);
    const auto mesh = kernel.convert().brep_to_mesh(*box.value,{});
    const auto derived = mesh.value ? kernel.convert().mesh_to_brep(*mesh.value) : axiom::Result<axiom::BodyId>{};
    const auto boolean = kernel.booleans().run(axiom::BooleanOp::Union,*box.value,*remote.value,{});
    if (!thicken.value || !legacy.value || !derived.value || !boolean.value ||
        boolean.value->status != axiom::StatusCode::Ok || boolean.value->output.value == 0 ||
        std::any_of(analytic.begin(),analytic.end(),[](const auto& result) { return !result.value; })) return false;
    const auto outer = kernel.primitives().box({0,0,0},10,10,10);
    const auto cavity = kernel.primitives().box({2,2,2},6,6,6);
    const auto island = kernel.primitives().box({4,4,4},2,2,2);
    if (!outer.value || !cavity.value || !island.value) return false;
    const auto box_shells = topo.shells_of_body(*box.value);
    const auto outer_shells = topo.shells_of_body(*outer.value);
    const auto cavity_shells = topo.shells_of_body(*cavity.value);
    const auto island_shells = topo.shells_of_body(*island.value);
    if (!box_shells.value || box_shells.value->size()!=1 || !outer_shells.value || outer_shells.value->size()!=1 ||
        !cavity_shells.value || cavity_shells.value->size()!=1 || !island_shells.value || island_shells.value->size()!=1) return false;
    auto setup = kernel.topology().begin_transaction();
    const auto generic = setup.create_body(*box_shells.value);
    const auto hollow = setup.create_body(std::array{island_shells.value->front(),outer_shells.value->front(),cavity_shells.value->front()});
    if (!generic.value || !hollow.value || setup.commit().status != axiom::StatusCode::Ok) return false;

    // Polygon-annulus area/perimeter are independent of the topology integrator.
    // ExactBRep identifies ownership, not analytic accuracy: revolve is sampled.
    const auto vertex_count = topo.vertex_count_of_body(*revolve.value);
    if (!vertex_count.value || *vertex_count.value<=4 || *vertex_count.value%4!=0) return false;
    const double intervals = static_cast<double>(*vertex_count.value/4-1);
    const double step = pi/(2*intervals);
    const double annulus = 2.5*intervals*std::sin(step);
    const double perimeter = 10*intervals*std::sin(step/2)+2;
    const double revolve_center = 19/(15*intervals*std::tan(step/2));
    enum class Support { Planar, Sampled, AnalyticMassOnly, Unsupported, InvalidHandle };
    struct Row {
        const char* label;
        axiom::BodyId body;
        Support support;
        double volume;
        double area;
        axiom::Point3 centroid;
        double section_z;
        double section_area;
        axiom::Point3 remote_corner;
    };
    const std::array<Row,17> rows{{
        {"box",*box.value,Support::Planar,24,52,{1,1.5,2},2,6,{2,3,4}},
        {"wedge",*wedge.value,Support::Planar,12,26+4*std::sqrt(13.0),{2.0/3,1,2},2,3,{0,3,4}},
        {"extrude/holed",*extrude.value,Support::Planar,36,96,{2,2,1.5},1.5,12,{4,4,3}},
        {"sweep/line",*sweep.value,Support::Planar,24,52,{1,1.5,2},2,6,{2,3,4}},
        {"loft/compatible",*loft.value,Support::Planar,24,52,{1,1.5,2},2,6,{2,3,4}},
        {"thicken/planar",*thicken.value,Support::Planar,12,32,{1,1.5,-1},-1,6,{2,3,0}},
        {"revolve/chords",*revolve.value,Support::Sampled,4*annulus,2*annulus+4*perimeter,
            {revolve_center,-revolve_center,2},2,annulus,{3,0,4}},
        {"generic/closed",*generic.value,Support::Planar,24,52,{1,1.5,2},2,6,{2,3,4}},
        {"generic/cavity/island",*hollow.value,Support::Planar,792,840,{5,5,5},5,68,{10,10,10}},
        {"sphere/native",*analytic[0].value,Support::AnalyticMassOnly,32*pi/3,16*pi,{0,0,0},0,0,{}},
        {"cylinder/native",*analytic[1].value,Support::AnalyticMassOnly,24*pi,32*pi,{0,0,0},0,0,{}},
        {"cone/native",*analytic[2].value,Support::AnalyticMassOnly,8*pi,4*pi+2*pi*std::sqrt(40.0),{0,0,4.5},0,0,{}},
        {"torus/native",*analytic[3].value,Support::AnalyticMassOnly,40*pi*pi,40*pi*pi,{0,0,0},0,0,{}},
        {"sweep/label_only",*legacy.value,Support::Unsupported,0,0,{},0,0,{}},
        {"mesh/derived",*derived.value,Support::Unsupported,0,0,{},0,0,{}},
        {"boolean/placeholder",boolean.value->output,Support::Unsupported,0,0,{},0,0,{}},
        {"invalid_handle",{},Support::InvalidHandle,0,0,{},0,0,{}}
    }};
    // Strict validation may tessellate non-Sweep bodies. Finish that modeling
    // step before taking the snapshot for the detailed queries' read-only gate.
    for (const auto& row : rows) {
        if ((row.support==Support::Planar || row.support==Support::Sampled) &&
            kernel.validate().validate_all(row.body,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok) {
            std::cerr << "S3-EXIT Strict validation: " << row.label << '\n';
            return false;
        }
    }
    const auto node = kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"body:"+std::to_string(box.value->value));
    const auto stores = kernel.runtime_store_counts();
    const auto objects = kernel.object_count_total();
    const auto geometry = kernel.geometry_count();
    const auto next = kernel.next_object_id();
    auto txn = kernel.topology().begin_transaction();
    const auto writes = txn.write_operation_count();
    if (!node.value || !stores.value || !objects.value || !geometry.value || !next.value || !writes.value) return false;
    for (const auto& row : rows) {
        const auto mass = query.mass_properties(row.body);
        const auto boundary_mass = topo.body_mass_properties(row.body);
        const auto section = query.section_detailed(row.body,{{0,0,row.section_z},{0,0,1}});
        const auto dedicated_section = topo.section(row.body,{{0,0,row.section_z},{0,0,1}});
        const auto nearest = query.closest_point(row.body,{60,60,row.section_z});
        const auto dedicated_nearest = topo.locate_point(row.body,{60,60,row.section_z});
        const auto pair = query.closest_points(row.body,*remote.value);
        const auto dedicated_pair = topo.closest_points(row.body,*remote.value);
        const auto distance = query.min_distance(row.body,*remote.value);
        const auto reverse = query.min_distance(*remote.value,row.body);
        const bool spatial = row.support==Support::Planar || row.support==Support::Sampled;
        if (spatial || row.support==Support::AnalyticMassOnly) {
            if (mass.status!=axiom::StatusCode::Ok || !mass.value || !close(mass.value->volume,row.volume) ||
                !close(mass.value->area,row.area) || !point_equal(mass.value->centroid,row.centroid)) {
                std::cerr << "S3-EXIT mass reference: " << row.label << '\n';
                return false;
            }
        } else if (!rejected(mass,row.support==Support::InvalidHandle ?
                "query.mass_properties.preflight" : "query.mass_properties.support_gate",row.support==Support::InvalidHandle)) return false;
        if (spatial) {
            const double point_distance = std::hypot(60-row.remote_corner.x,60-row.remote_corner.y);
            const double body_distance = std::hypot(50-row.remote_corner.x,50-row.remote_corner.y,50-row.remote_corner.z);
            const axiom::Point3 nearest_reference{row.remote_corner.x,row.remote_corner.y,row.section_z};
            if (!boundary_mass.value || !close(boundary_mass.value->volume,row.volume) || !close(boundary_mass.value->area,row.area) ||
                !point_equal(boundary_mass.value->centroid,row.centroid) || !section.value || !dedicated_section.value ||
                !close(section.value->area,row.section_area) || !close(dedicated_section.value->area,row.section_area) ||
                section.value->triangles.empty() || !nearest.value || !nearest.value->nearest_boundary ||
                !dedicated_nearest.value || !dedicated_nearest.value->nearest_boundary ||
                !close(nearest.value->nearest_boundary->distance,point_distance) ||
                !point_equal(nearest.value->nearest_boundary->point,nearest_reference) ||
                !point_equal(dedicated_nearest.value->nearest_boundary->point,nearest_reference) ||
                !pair.value || !dedicated_pair.value || !distance.value || !reverse.value ||
                !close(*distance.value,body_distance) || !close(*reverse.value,body_distance) ||
                !close(pair.value->distance,body_distance) || !close(dedicated_pair.value->distance,body_distance) ||
                !point_equal(pair.value->first_point,row.remote_corner) || !point_equal(pair.value->second_point,{50,50,50})) {
                std::cerr << "S3-EXIT physical boundary reference: " << row.label << '\n';
                return false;
            }
        } else {
            const bool invalid = row.support==Support::InvalidHandle;
            if (!rejected(boundary_mass,invalid ? "query.mass_properties.preflight" : "query.mass_properties.support_gate",invalid) ||
                !rejected(section,invalid ? "query.section.preflight" : "query.section.support_gate",invalid) ||
                !rejected(dedicated_section,invalid ? "query.section.preflight" : "query.section.support_gate",invalid) ||
                !rejected(query.section(row.body,{{0,0,0},{0,0,1}}),invalid ? "query.section.preflight" : "query.section.support_gate",invalid) ||
                !rejected(nearest,invalid ? "query.closest_point.preflight" : "query.closest_point.support_gate",invalid) ||
                !rejected(dedicated_nearest,invalid ? "query.closest_point.preflight" : "query.closest_point.support_gate",invalid) ||
                !rejected(pair,invalid ? "query.distance.preflight" : "query.distance.support_gate",invalid) ||
                !rejected(dedicated_pair,invalid ? "query.distance.preflight" : "query.distance.support_gate",invalid) ||
                !rejected(distance,invalid ? "query.distance.preflight" : "query.distance.support_gate",invalid) ||
                !rejected(reverse,invalid ? "query.distance.preflight" : "query.distance.support_gate",invalid)) {
                std::cerr << "S3-EXIT rejection contract: " << row.label << '\n';
                return false;
            }
        }
    }
    const auto after = kernel.runtime_store_counts();
    return after.value && kernel.object_count_total().value==objects.value && txn.write_operation_count().value==writes.value &&
        kernel.geometry_count().value==geometry.value && kernel.next_object_id().value==next.value &&
        after.value->mesh_records==stores.value->mesh_records &&
        after.value->tessellation_cache_entries==stores.value->tessellation_cache_entries &&
        after.value->face_tessellation_cache_entries==stores.value->face_tessellation_cache_entries &&
        after.value->curve_eval_cache_entries==stores.value->curve_eval_cache_entries &&
        after.value->surface_eval_cache_entries==stores.value->surface_eval_cache_entries &&
        after.value->intersection_records==stores.value->intersection_records &&
        kernel.eval_graph().is_invalid(*node.value).value==std::optional<bool>{false} &&
        kernel.eval_graph().recompute_count(*node.value).value==std::optional<std::uint64_t>{0} &&
        txn.rollback().status==axiom::StatusCode::Ok;
}

bool stage3_mass_authority_regression() {
    axiom::Kernel kernel;
    auto& topo = kernel.topology().query();
    const auto close = [](double a, double b) {
        return std::abs(a-b) <= 2e-10 * std::max(1.0, std::abs(b));
    };
    const auto same = [&](const axiom::MassProperties& a, const axiom::MassProperties& b) {
        if (!close(a.volume,b.volume) || !close(a.area,b.area) ||
            !close(a.centroid.x,b.centroid.x) || !close(a.centroid.y,b.centroid.y) ||
            !close(a.centroid.z,b.centroid.z)) return false;
        for (std::size_t i=0; i<9; ++i) if (!close(a.inertia[i],b.inertia[i])) return false;
        return true;
    };
    const auto failure = [&](const auto& result, axiom::StatusCode status,
                             std::string_view code, std::string_view stage) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        return result.status == status && !result.value && report.value &&
            std::any_of(report.value->issues.begin(), report.value->issues.end(),
                [&](const axiom::Issue& issue) { return issue.code == code && issue.stage == stage; });
    };
    const auto unsupported = [&](const auto& result) {
        return failure(result, axiom::StatusCode::NotImplemented,
            axiom::diag_codes::kCoreOperationUnsupported, "query.mass_properties.support_gate");
    };
    const double pi = std::acos(-1.0);
    // Independent textbook solid integrals, density 1. Both an axis-aligned
    // tensor and all nonzero world-frame products are checked. No sampling error.
    for (const auto axis : std::array{axiom::Vec3{0,0,1}, axiom::Vec3{1.0/3,2.0/3,2.0/3}}) {
        const axiom::Point3 origin{17,-23,31};
        const std::array models{
            kernel.primitives().sphere(origin,2),
            kernel.primitives().cylinder(origin,axis,2,6),
            kernel.primitives().cone(origin,axis,std::atan(1.0/3),6),
            kernel.primitives().torus(origin,axis,5,2)};
        const std::array volumes{32*pi/3,24*pi,8*pi,40*pi*pi};
        const std::array areas{16*pi,32*pi,4*pi+2*pi*std::sqrt(40.0),40*pi*pi};
        const std::array transverse{volumes[0]*8/5,volumes[1]*4,
            volumes[2]*(3.0*4/20+3.0*36/80),volumes[3]*(25.0/2+5.0*4/8)};
        const std::array axial{transverse[0],volumes[1]*2,volumes[2]*3*4/10,
            volumes[3]*(25+3.0*4/4)};
        for (std::size_t i=0; i<models.size(); ++i) {
            if (!models[i].value) return false;
            const auto body = *models[i].value;
            axiom::MassProperties expected{};
            expected.volume = volumes[i];
            expected.area = areas[i];
            expected.centroid = origin;
            if (i == 2) expected.centroid = {origin.x+4.5*axis.x,
                origin.y+4.5*axis.y,origin.z+4.5*axis.z};
            const std::array direction{axis.x,axis.y,axis.z};
            for (std::size_t r=0; r<3; ++r) for (std::size_t c=0; c<3; ++c)
                expected.inertia[3*r+c] = (r == c ? transverse[i] : 0) +
                    (axial[i]-transverse[i])*direction[r]*direction[c];
            const auto count = kernel.object_count_total();
            const auto stores = kernel.runtime_store_counts();
            const auto result = kernel.query().mass_properties(body);
            const auto after = kernel.runtime_store_counts();
            if (!count.value || !stores.value || !after.value || !result.value ||
                !same(*result.value,expected) || kernel.object_count_total().value != count.value ||
                after.value->mesh_records != stores.value->mesh_records ||
                after.value->tessellation_cache_entries != stores.value->tessellation_cache_entries ||
                after.value->face_tessellation_cache_entries != stores.value->face_tessellation_cache_entries ||
                after.value->curve_eval_cache_entries != stores.value->curve_eval_cache_entries ||
                after.value->surface_eval_cache_entries != stores.value->surface_eval_cache_entries) return false;
            const auto shells = topo.shells_of_body(body);
            const auto faces = topo.faces_of_body(body);
            if (!shells.value || shells.value->size() != 1 || !faces.value || faces.value->empty() ||
                !unsupported(topo.shell_mass_properties(shells.value->front())) ||
                !unsupported(topo.body_mass_properties(body))) return false;
            const auto replacement = kernel.surfaces().make_plane({0,0,0},{0,0,1});
            if (!replacement.value) return false;
            {
                auto txn = kernel.topology().begin_transaction();
                const auto savepoint = txn.create_savepoint();
                const auto writes = txn.write_operation_count();
                const auto invalid_edit = txn.replace_surface(faces.value->front(),{});
                const auto after_failed_edit = kernel.query().mass_properties(body);
                if (!savepoint.value || !writes.value || invalid_edit.status != axiom::StatusCode::InvalidInput ||
                    txn.write_operation_count().value != writes.value ||
                    !after_failed_edit.value || !same(*after_failed_edit.value,expected) ||
                    txn.replace_surface(faces.value->front(),*replacement.value).status != axiom::StatusCode::Ok ||
                    !unsupported(kernel.query().mass_properties(body)) ||
                    txn.rollback_to_savepoint(*savepoint.value).status != axiom::StatusCode::Ok) return false;
                const auto restored = kernel.query().mass_properties(body);
                if (!restored.value || !same(*restored.value,expected) ||
                    txn.delete_face(faces.value->front()).status != axiom::StatusCode::Ok ||
                    !unsupported(kernel.query().mass_properties(body)) ||
                    txn.rollback().status != axiom::StatusCode::Ok) return false;
            }
            const auto restored = kernel.query().mass_properties(body);
            if (!restored.value || !same(*restored.value,expected)) return false;
            const auto edges = topo.edges_of_body(body);
            const auto coedges = edges.value && !edges.value->empty() ? topo.coedges_of_edge(edges.value->front()) :
                axiom::Result<std::vector<axiom::CoedgeId>>{};
            const auto pcurve = kernel.pcurves().make_polyline(std::array<axiom::Point2,2>{{{0,0},{1,0}}});
            if (!coedges.value || coedges.value->empty() || !pcurve.value) return false;
            {
                auto txn = kernel.topology().begin_transaction();
                if (txn.set_coedge_pcurve(coedges.value->front(),*pcurve.value).status != axiom::StatusCode::Ok ||
                    !unsupported(kernel.query().mass_properties(body)) ||
                    txn.rollback().status != axiom::StatusCode::Ok) return false;
                const auto restored_pcurve = kernel.query().mass_properties(body);
                if (!restored_pcurve.value || !same(*restored_pcurve.value,expected)) return false;
            }
            {
                // Proxy faces remain proxies after re-shelling and deleting all
                // original owners. Provenance is not a geometry certificate.
                auto txn = kernel.topology().begin_transaction();
                const auto shell = txn.create_shell(*faces.value);
                const auto generic = shell.value ? txn.create_body(std::array{*shell.value}) :
                    axiom::Result<axiom::BodyId>{};
                if (!generic.value || !unsupported(kernel.query().mass_properties(*generic.value)) ||
                    txn.delete_body(body).status != axiom::StatusCode::Ok ||
                    txn.delete_shell(shells.value->front()).status != axiom::StatusCode::Ok ||
                    !unsupported(kernel.query().mass_properties(*generic.value)) ||
                    txn.rollback().status != axiom::StatusCode::Ok) return false;
            }
            {
                auto txn = kernel.topology().begin_transaction();
                if (txn.replace_surface(faces.value->front(),*replacement.value).status != axiom::StatusCode::Ok ||
                    txn.commit().status != axiom::StatusCode::Ok ||
                    !unsupported(kernel.query().mass_properties(body))) return false;
            }
        }
    }
    // Overflow and underflow in inertia cannot leak a partial volume/centroid.
    for (const auto radius : {1e70,1e-70}) {
        const auto body = kernel.primitives().sphere({0,0,0},radius);
        if (!body.value || !failure(kernel.query().mass_properties(*body.value),
                axiom::StatusCode::NumericalInstability,axiom::diag_codes::kQueryMassPropertiesFailure,
                "query.mass_properties.numeric")) return false;
    }
    if (!failure(kernel.query().mass_properties({}),axiom::StatusCode::InvalidInput,
            axiom::diag_codes::kCoreInvalidHandle,"query.mass_properties.preflight") ||
        !failure(topo.shell_mass_properties({}),axiom::StatusCode::InvalidInput,
            axiom::diag_codes::kCoreInvalidHandle,"query.mass_properties.preflight")) return false;
    // A warmed mesh cannot restore pre-edit numbers from a real polyhedron.
    // Read-only rejected mass queries preserve writes, provenance and Eval state.
    const auto box = kernel.primitives().box({0,0,0},2,3,4);
    const auto curved = kernel.surfaces().make_sphere({0,0,0},1);
    const auto shifted = kernel.surfaces().make_plane({0,0,100},{0,0,1});
    const auto node = kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"mass:edits");
    if (!box.value || !curved.value || !shifted.value || !node.value ||
        !kernel.convert().brep_to_mesh(*box.value,{}).value) return false;
    const auto original = kernel.query().mass_properties(*box.value);
    const auto faces = topo.faces_of_body(*box.value);
    const auto shells = topo.shells_of_body(*box.value);
    const auto source_faces = topo.source_faces_of_body(*box.value);
    if (!original.value || !faces.value || faces.value->empty() || !shells.value ||
        shells.value->size() != 1 || !source_faces.value) return false;
    for (int edit=0; edit<4; ++edit) {
        auto txn = kernel.topology().begin_transaction();
        const auto savepoint = txn.create_savepoint();
        const auto changed = edit == 0 ? txn.replace_surface(faces.value->front(),*curved.value)
            : edit == 1 ? txn.replace_surface(faces.value->front(),*shifted.value)
            : edit == 2 ? txn.delete_face(faces.value->front()) : txn.delete_shell(shells.value->front());
        const auto writes = txn.write_operation_count();
        const auto stores = kernel.runtime_store_counts();
        const auto invalid = kernel.eval_graph().is_invalid(*node.value);
        const auto recompute = kernel.eval_graph().recompute_count(*node.value);
        if (!savepoint.value || changed.status != axiom::StatusCode::Ok || !writes.value || !stores.value ||
            !invalid.value || !recompute.value) return false;
        const auto result = kernel.query().mass_properties(*box.value);
        const auto after = kernel.runtime_store_counts();
        const auto code = edit == 0 ? axiom::diag_codes::kCoreOperationUnsupported
            : edit == 1 ? axiom::diag_codes::kTopoCurveTopologyMismatch
            : edit == 2 ? axiom::diag_codes::kTopoShellNotClosed : axiom::diag_codes::kCoreInvalidHandle;
        const auto stage = edit == 0 ? "query.mass_properties.support_gate" : "query.mass_properties.preflight";
        // Removing the last shell deletes its owning body; the public API does
        // not leave a zero-shell fixture for the internal empty_gate branch.
        const auto status = edit == 0 ? axiom::StatusCode::NotImplemented
            : edit == 3 ? axiom::StatusCode::InvalidInput : axiom::StatusCode::InvalidTopology;
        if (!failure(result,status,
                code,stage) || !after.value || txn.write_operation_count().value != writes.value ||
            after.value->mesh_records != stores.value->mesh_records ||
            after.value->tessellation_cache_entries != stores.value->tessellation_cache_entries ||
            after.value->face_tessellation_cache_entries != stores.value->face_tessellation_cache_entries ||
            after.value->curve_eval_cache_entries != stores.value->curve_eval_cache_entries ||
            after.value->surface_eval_cache_entries != stores.value->surface_eval_cache_entries ||
            kernel.eval_graph().is_invalid(*node.value).value != invalid.value ||
            kernel.eval_graph().recompute_count(*node.value).value != recompute.value ||
            txn.rollback_to_savepoint(*savepoint.value).status != axiom::StatusCode::Ok) return false;
        const auto restored = kernel.query().mass_properties(*box.value);
        if (!restored.value || !same(*restored.value,*original.value) ||
            topo.source_faces_of_body(*box.value).value != source_faces.value ||
            txn.rollback().status != axiom::StatusCode::Ok) return false;
    }
    return true;
}

bool curve_curve_intersection_regression() {
    axiom::Kernel kernel;
    auto& intersections = kernel.geometry_intersection();
    const auto close = [](double actual, double expected, double tolerance = 2e-6) {
        return std::abs(actual - expected) <= tolerance;
    };
    const auto failed = [&](const axiom::Result<axiom::CurveCurveIntersectionResult>& result,
                            axiom::StatusCode status, std::string_view code) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        return result.status == status && !result.value && report.value &&
               has_issue_code(*report.value, code);
    };
    axiom::CurveCurveIntersectionOptions accurate;
    accurate.position_tolerance = 1e-7;
    accurate.parameter_tolerance = 1e-7;
    accurate.angular_tolerance = 1e-6;

    // The piecewise-linear path is analytic in 3D and keeps point, endpoint,
    // skew-empty and reversed-overlap semantics separate.
    const auto horizontal = kernel.curves().make_line_segment({-2, 0, 0}, {2, 0, 0});
    const auto vertical = kernel.curves().make_line_segment({0, -3, 0}, {0, 3, 0});
    const auto endpoint = kernel.curves().make_line_segment({2, 0, 0}, {3, 2, 0});
    const auto skew = kernel.curves().make_line_segment({0, -3, 1}, {0, 3, 1});
    const auto overlap_forward = kernel.curves().make_line_segment({0, 0, 0}, {4, 0, 0});
    const auto overlap_reverse = kernel.curves().make_line_segment({3, 0, 0}, {1, 0, 0});
    if (!horizontal.value || !vertical.value || !endpoint.value || !skew.value ||
        !overlap_forward.value || !overlap_reverse.value) return false;
    const auto crossing = intersections.intersect_curve_curve(
        *horizontal.value, *vertical.value, accurate);
    const auto at_endpoint = intersections.intersect_curve_curve(
        *horizontal.value, *endpoint.value, accurate);
    const auto empty = intersections.intersect_curve_curve(
        *horizontal.value, *skew.value, accurate);
    const auto overlap = intersections.intersect_curve_curve(
        *overlap_forward.value, *overlap_reverse.value, accurate);
    if (!crossing.value || crossing.value->points.size() != 1 ||
        !crossing.value->overlaps.empty() ||
        crossing.value->points.front().kind !=
            axiom::CurveCurveIntersectionKind::Transverse ||
        !close(crossing.value->points.front().point.x, 0) ||
        !close(crossing.value->points.front().point.y, 0) ||
        !close(crossing.value->points.front().first_parameter, .5) ||
        !close(crossing.value->points.front().second_parameter, .5) ||
        !at_endpoint.value || at_endpoint.value->points.size() != 1 ||
        at_endpoint.value->points.front().kind !=
            axiom::CurveCurveIntersectionKind::Endpoint ||
        !empty.value || !empty.value->points.empty() ||
        !empty.value->overlaps.empty() ||
        !overlap.value || !overlap.value->points.empty() ||
        overlap.value->overlaps.size() != 1 ||
        overlap.value->overlaps.front().same_direction ||
        !close(overlap.value->overlaps.front().first_interval.min, .25) ||
        !close(overlap.value->overlaps.front().first_interval.max, .75) ||
        !close(overlap.value->overlaps.front().second_interval.min, 0) ||
        !close(overlap.value->overlaps.front().second_interval.max, 1)) return false;

    // A constant (zero-speed) Bezier is a point-set query, not a numerical
    // failure and not eight duplicate hits from the initial Bezier partition.
    const auto constant_on = kernel.curves().make_bezier(
        std::array<axiom::Point3, 1>{{{0,0,0}}});
    const auto constant_off = kernel.curves().make_bezier(
        std::array<axiom::Point3, 1>{{{0,2,0}}});
    if (!constant_on.value || !constant_off.value) return false;
    const auto constant_hit = intersections.intersect_curve_curve(
        *constant_on.value, *horizontal.value, accurate);
    const auto constant_empty = intersections.intersect_curve_curve(
        *constant_off.value, *horizontal.value, accurate);
    if (!constant_hit.value || constant_hit.value->points.size() != 1 ||
        !close(constant_hit.value->points.front().point.x, 0) ||
        !close(constant_hit.value->points.front().point.y, 0) ||
        !constant_empty.value || !constant_empty.value->points.empty() ||
        !constant_empty.value->overlaps.empty()) return false;

    // A quadratic Bezier crosses y=.5 twice and is tangent to y=1 once.
    // Both queries use the general conservative parameter-rectangle path.
    const auto arch = kernel.curves().make_bezier(
        std::array<axiom::Point3, 3>{{{0,0,0}, {1,2,0}, {2,0,0}}});
    const auto half_height = kernel.curves().make_line_segment({-1,.5,0}, {3,.5,0});
    const auto tangent_line = kernel.curves().make_line_segment({-1,1,0}, {3,1,0});
    const auto off_plane = kernel.curves().make_line_segment({-1,.5,.01}, {3,.5,.01});
    if (!arch.value || !half_height.value || !tangent_line.value || !off_plane.value)
        return false;
    const auto bezier_crossings = intersections.intersect_curve_curve(
        *arch.value, *half_height.value, accurate);
    const auto bezier_tangent = intersections.intersect_curve_curve(
        *arch.value, *tangent_line.value, accurate);
    const auto bezier_empty = intersections.intersect_curve_curve(
        *arch.value, *off_plane.value, accurate);
    const double root0 = (1.0 - std::sqrt(.5)) * .5;
    const double root1 = (1.0 + std::sqrt(.5)) * .5;
    if (!bezier_crossings.value || bezier_crossings.value->points.size() != 2 ||
        !close(bezier_crossings.value->points[0].first_parameter, root0, 2e-5) ||
        !close(bezier_crossings.value->points[1].first_parameter, root1, 2e-5) ||
        bezier_crossings.value->points[0].kind !=
            axiom::CurveCurveIntersectionKind::Transverse ||
        bezier_crossings.value->points[1].kind !=
            axiom::CurveCurveIntersectionKind::Transverse ||
        !bezier_tangent.value || bezier_tangent.value->points.size() != 1 ||
        !close(bezier_tangent.value->points.front().first_parameter, .5, 2e-5) ||
        bezier_tangent.value->points.front().kind !=
            axiom::CurveCurveIntersectionKind::Tangent ||
        !bezier_empty.value || !bezier_empty.value->points.empty() ||
        !bezier_empty.value->overlaps.empty()) return false;

    // The same geometric record (including a separately allocated exact copy)
    // is represented as one continuous overlap rather than sampled point spam.
    const auto arch_copy = kernel.curves().make_bezier(
        std::array<axiom::Point3, 3>{{{0,0,0}, {1,2,0}, {2,0,0}}});
    if (!arch_copy.value) return false;
    auto restricted_overlap_options = accurate;
    restricted_overlap_options.first_interval = axiom::Range1D{.2, .8};
    restricted_overlap_options.second_interval = axiom::Range1D{.4, 1.0};
    const auto curved_overlap = intersections.intersect_curve_curve(
        *arch.value, *arch_copy.value, restricted_overlap_options);
    if (!curved_overlap.value || curved_overlap.value->overlaps.size() != 1 ||
        !close(curved_overlap.value->overlaps.front().first_interval.min, .4) ||
        !close(curved_overlap.value->overlaps.front().first_interval.max, .8) ||
        !close(curved_overlap.value->overlaps.front().second_interval.min, .4) ||
        !close(curved_overlap.value->overlaps.front().second_interval.max, .8) ||
        !curved_overlap.value->overlaps.front().same_direction ||
        curved_overlap.value->overlaps.front().maximum_separation != 0) return false;

    // B-spline and positive-weight NURBS use independent knot-span bounds.  A
    // full circle and a composite chain exercise periodic and child domains.
    axiom::BSplineCurveDesc bspline_desc;
    bspline_desc.poles = {{0,0,0}, {1,2,0}, {2,0,0}};
    bspline_desc.degree = 2;
    const auto bspline = kernel.curves().make_bspline(bspline_desc);
    axiom::NURBSCurveDesc nurbs_desc;
    nurbs_desc.poles = {{0,0,0}, {1,2,0}, {2,0,0}};
    nurbs_desc.weights = {1, 2, 1};
    nurbs_desc.degree = 2;
    const auto nurbs = kernel.curves().make_nurbs(nurbs_desc);
    const auto circle = kernel.curves().make_circle({0,0,0}, {0,0,1}, 2);
    const auto diameter = kernel.curves().make_line_segment({-3,0,0}, {3,0,0});
    const auto chain = kernel.curves().make_composite_chain(
        std::array{*vertical.value, *endpoint.value});
    if (!bspline.value || !nurbs.value || !circle.value || !diameter.value ||
        !chain.value) return false;
    const auto bspline_hits = intersections.intersect_curve_curve(
        *bspline.value, *half_height.value, accurate);
    const auto nurbs_hits = intersections.intersect_curve_curve(
        *nurbs.value, *half_height.value, accurate);
    const auto circle_hits = intersections.intersect_curve_curve(
        *circle.value, *diameter.value, accurate);
    const auto chain_hits = intersections.intersect_curve_curve(
        *chain.value, *horizontal.value, accurate);
    if (!bspline_hits.value || bspline_hits.value->points.size() != 2 ||
        bspline_hits.value->evaluations == 0 ||
        bspline_hits.value->parameter_rectangles_processed == 0 ||
        !nurbs_hits.value || nurbs_hits.value->points.size() != 2 ||
        nurbs_hits.value->evaluations == 0 ||
        !circle_hits.value || circle_hits.value->points.size() != 2 ||
        !close(std::abs(circle_hits.value->points[0].point.x), 2, 2e-5) ||
        !close(std::abs(circle_hits.value->points[1].point.x), 2, 2e-5) ||
        circle_hits.value->points[0].point.x * circle_hits.value->points[1].point.x >= 0 ||
        !chain_hits.value || chain_hits.value->points.size() != 2) return false;

    // Infinite defaults, out-of-domain ranges, invalid tolerances and a tiny
    // evaluation budget all fail structurally and never leak partial results.
    const auto infinite_line = kernel.curves().make_line({0,0,0}, {1,0,0});
    if (!infinite_line.value) return false;
    auto bounded_line = accurate;
    bounded_line.first_interval = axiom::Range1D{-3, 3};
    const auto bounded_line_hits = intersections.intersect_curve_curve(
        *infinite_line.value, *vertical.value, bounded_line);
    auto outside = accurate;
    outside.first_interval = axiom::Range1D{-1, .5};
    auto invalid_tolerance = accurate;
    invalid_tolerance.position_tolerance = 0;
    auto exhausted = accurate;
    exhausted.max_evaluations = 6;
    exhausted.max_subdivisions = 100000;
    if (!bounded_line_hits.value || bounded_line_hits.value->points.size() != 1 ||
        !failed(intersections.intersect_curve_curve(
                    *infinite_line.value, *vertical.value, accurate),
                axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreParameterOutOfRange) ||
        !failed(intersections.intersect_curve_curve(
                    *arch.value, *vertical.value, outside),
                axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kGeoParameterOutOfDomain) ||
        !failed(intersections.intersect_curve_curve(
                    *arch.value, *vertical.value, invalid_tolerance),
                axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreParameterOutOfRange) ||
        !failed(intersections.intersect_curve_curve(
                    *arch.value, *half_height.value, exhausted),
                axiom::StatusCode::OperationFailed,
                axiom::diag_codes::kGeoIntersectionFailure) ||
        !failed(intersections.intersect_curve_curve(
                    axiom::CurveId{999999}, *vertical.value, accurate),
                axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreInvalidHandle)) return false;

    // Query success and failure are read-only even during an active topology
    // transaction; rollback restores the sole deliberate vertex allocation.
    const auto objects_before = kernel.object_count_total();
    const auto geometry_before = kernel.geometry_count();
    const auto topology_before = kernel.topology_count();
    const auto intersections_before = kernel.intersection_count();
    const auto runtime_before = kernel.runtime_store_counts();
    if (!objects_before.value || !geometry_before.value || !topology_before.value ||
        !intersections_before.value || !runtime_before.value) return false;
    auto txn = kernel.topology().begin_transaction();
    const auto vertex = txn.create_vertex({7,8,9});
    const auto writes_before_queries = txn.write_operation_count();
    if (!vertex.value || !writes_before_queries.value ||
        !intersections.intersect_curve_curve(
             *arch.value, *half_height.value, accurate).value ||
        !failed(intersections.intersect_curve_curve(
                    *infinite_line.value, *vertical.value, accurate),
                axiom::StatusCode::InvalidInput,
                axiom::diag_codes::kCoreParameterOutOfRange) ||
        txn.write_operation_count().value != writes_before_queries.value ||
        txn.rollback().status != axiom::StatusCode::Ok) return false;
    const auto runtime_after = kernel.runtime_store_counts();
    if (!runtime_after.value ||
        kernel.object_count_total().value != objects_before.value ||
        kernel.geometry_count().value != geometry_before.value ||
        kernel.topology_count().value != topology_before.value ||
        kernel.intersection_count().value != intersections_before.value ||
        runtime_after.value->curve_eval_cache_entries !=
            runtime_before.value->curve_eval_cache_entries ||
        runtime_after.value->surface_eval_cache_entries !=
            runtime_before.value->surface_eval_cache_entries ||
        runtime_after.value->mesh_records != runtime_before.value->mesh_records ||
        runtime_after.value->tessellation_cache_entries !=
            runtime_before.value->tessellation_cache_entries ||
        runtime_after.value->face_tessellation_cache_entries !=
            runtime_before.value->face_tessellation_cache_entries ||
        runtime_after.value->intersection_records !=
            runtime_before.value->intersection_records) return false;
    return true;
}

}  // namespace

int main() {
    if (!direct_edit_query_eval_regression()) {
        std::cerr << "Stage 6 direct edit query/Eval/savepoint regression failed\n";
        return 1;
    }
    if (!stage6_blend_eval_rollback_regression()) {
        std::cerr << "Stage 6 blend failure/Eval/cache rollback regression failed\n";
        return 1;
    }
    if (!rebuilt_boundary_eval_rollback_regression()) {
        std::cerr << "rebuilt boundary query/Eval savepoint regression\n";
        return 1;
    }
    if (!stage3_exit_support_matrix_regression()) {
        std::cerr << "Stage 3 exit support matrix regression failed\n";
        return 1;
    }
    if (!stage3_eval_rollback_consistency_regression()) {
        std::cerr << "Stage 3 Eval rollback consistency regression failed\n";
        return 1;
    }
    if (!stage3_mass_authority_regression()) {
        std::cerr << "Stage 3 mass authority regression failed\n";
        return 1;
    }
    if (!stage3_section_distance_regression()) {
        std::cerr << "Stage 3 actual section/distance regression failed\n";
        return 1;
    }
    if (!body_spatial_query_regression()) {
        std::cerr << "body spatial query regression failed\n";
        return 1;
    }
    if (!curve_curve_intersection_regression()) {
        std::cerr << "curve-curve intersection regression failed\n";
        return 1;
    }
    if (!topology_mass_properties_regression()) {
        std::cerr << "topology mass properties regression failed\n";
        return 1;
    }
    if (!analytic_face_area_regression()) {
        std::cerr << "analytic face area regression failed\n";
        return 1;
    }
    if (!numerical_length_query_regression()) {
        std::cerr << "numerical length query regression failed\n";
        return 1;
    }
    if (!length_query_regression()) {
        std::cerr << "analytic length query regression failed\n";
        return 1;
    }
    axiom::Kernel kernel;

    auto line = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    auto circle = kernel.curves().make_circle({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 2.0);
    auto tilted_circle = kernel.curves().make_circle({1.0, 2.0, 3.0}, {1.0, 0.0, 0.0}, 2.0);
    auto bezier = kernel.curves().make_bezier({{{0.0, 0.0, 0.0}, {1.0, 2.0, 0.0}, {2.0, 0.0, 0.0}}});
    auto bspline = kernel.curves().make_bspline({{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}}});
    axiom::NURBSCurveDesc nurbs_desc;
    nurbs_desc.poles = {{0.0, 0.0, 0.0}, {1.0, 2.0, 0.0}, {2.0, 0.0, 0.0}};
    nurbs_desc.weights = {1.0, 2.0, 1.0};
    auto nurbs = kernel.curves().make_nurbs(nurbs_desc);
    auto sphere = kernel.surfaces().make_sphere({0.0, 0.0, 0.0}, 5.0);
    auto cylinder = kernel.surfaces().make_cylinder({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 3.0);
    auto tilted_cylinder =
        kernel.surfaces().make_cylinder({1.0, 2.0, 3.0}, {1.0, 0.0, 0.0}, 3.0);
    auto cone = kernel.surfaces().make_cone({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, std::acos(-1.0) * 0.25);
    auto torus = kernel.surfaces().make_torus({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 5.0, 2.0);
    auto bspline_surface =
        kernel.surfaces().make_bspline({{{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 1.0}}});
    axiom::NURBSSurfaceDesc nurbs_surface_desc;
    nurbs_surface_desc.poles = {{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 1.0}};
    nurbs_surface_desc.weights = {1.0, 1.0, 1.0, 3.0};
    auto nurbs_surface = kernel.surfaces().make_nurbs(nurbs_surface_desc);
    if (line.status != axiom::StatusCode::Ok || circle.status != axiom::StatusCode::Ok ||
        tilted_circle.status != axiom::StatusCode::Ok || bezier.status != axiom::StatusCode::Ok ||
        bspline.status != axiom::StatusCode::Ok || nurbs.status != axiom::StatusCode::Ok ||
        sphere.status != axiom::StatusCode::Ok || cylinder.status != axiom::StatusCode::Ok ||
        tilted_cylinder.status != axiom::StatusCode::Ok || cone.status != axiom::StatusCode::Ok ||
        torus.status != axiom::StatusCode::Ok || bspline_surface.status != axiom::StatusCode::Ok ||
        nurbs_surface.status != axiom::StatusCode::Ok ||
        !line.value.has_value() || !circle.value.has_value() || !tilted_circle.value.has_value() ||
        !bezier.value.has_value() || !bspline.value.has_value() || !nurbs.value.has_value() ||
        !sphere.value.has_value() || !cylinder.value.has_value() ||
        !tilted_cylinder.value.has_value() || !cone.value.has_value() || !torus.value.has_value() ||
        !bspline_surface.value.has_value() || !nurbs_surface.value.has_value()) {
        std::cerr << "failed to create geometry for query/eval test\n";
        return 1;
    }

    // Ops materializes a box as planar faces; Topo measures current boundary geometry.
    const auto box = kernel.primitives().box({1.0, 2.0, 3.0}, 2.0, 3.0, 4.0);
    if (!box.value) return 1;
    const auto faces = kernel.topology().query().faces_of_body(*box.value);
    if (!faces.value || faces.value->size() != 6) return 1;
    std::array<double, 6> perimeters{};
    for (std::size_t i = 0; i < perimeters.size(); ++i) {
        const auto measured = kernel.topology().query().face_boundary_length((*faces.value)[i]);
        if (!measured.value || measured.status != axiom::StatusCode::Ok) return 1;
        perimeters[i] = *measured.value;
    }
    std::sort(perimeters.begin(), perimeters.end());
    if (perimeters != std::array<double, 6>{10, 10, 12, 12, 14, 14}) return 1;
    std::array<double, 6> areas{};
    for (std::size_t i = 0; i < areas.size(); ++i) {
        const auto measured = kernel.topology().query().planar_face_area((*faces.value)[i]);
        if (measured.status != axiom::StatusCode::Ok || !measured.value) return 1;
        areas[i] = *measured.value;
    }
    std::sort(areas.begin(), areas.end());
    if (areas != std::array<double, 6>{6.0, 6.0, 8.0, 8.0, 12.0, 12.0}) {
        std::cerr << "unexpected planar face area in squared model units\n";
        return 1;
    }
    const auto face = faces.value->front();
    const auto original = kernel.topology().query().planar_face_area(face);
    const auto original_boundary = kernel.topology().query().face_boundary_length(face);
    const auto missing = kernel.topology().query().planar_face_area(axiom::FaceId{});
    const auto missing_report = kernel.diagnostics().get(missing.diagnostic_id);
    if (!original.value || missing.status != axiom::StatusCode::InvalidInput || missing.value ||
        !missing_report.value || !has_issue_code(*missing_report.value, axiom::diag_codes::kCoreInvalidHandle) ||
        kernel.topology().query().planar_face_area(face).value != original.value) return 1;

    {
        auto txn = kernel.topology().begin_transaction();
        if (txn.replace_surface(face, *sphere.value).status != axiom::StatusCode::Ok) return 1;
        if (kernel.topology().query().face_boundary_length(face).value != original_boundary.value) return 1;
        const auto curved = kernel.topology().query().planar_face_area(face);
        const auto curved_report = kernel.diagnostics().get(curved.diagnostic_id);
        if (curved.status != axiom::StatusCode::NotImplemented || curved.value ||
            !curved_report.value || !has_issue_code(*curved_report.value, axiom::diag_codes::kCoreOperationUnsupported) ||
            txn.rollback().status != axiom::StatusCode::Ok ||
            kernel.topology().query().planar_face_area(face).value != original.value) return 1;
    }
    const auto displaced_plane = kernel.surfaces().make_plane({100.0, 100.0, 100.0}, {1.0, 1.0, 1.0});
    if (!displaced_plane.value) return 1;
    {
        auto txn = kernel.topology().begin_transaction();
        if (txn.replace_surface(face, *displaced_plane.value).status != axiom::StatusCode::Ok) return 1;
        const auto nonplanar = kernel.topology().query().planar_face_area(face);
        if (nonplanar.status != axiom::StatusCode::InvalidTopology || nonplanar.value ||
            txn.rollback().status != axiom::StatusCode::Ok ||
            kernel.topology().query().planar_face_area(face).value != original.value) return 1;
    }
    {
        auto txn = kernel.topology().begin_transaction();
        if (txn.delete_face(face).status != axiom::StatusCode::Ok) return 1;
        const auto deleted = kernel.topology().query().planar_face_area(face);
        const auto deleted_boundary = kernel.topology().query().face_boundary_length(face);
        if (deleted_boundary.status != axiom::StatusCode::InvalidInput || deleted_boundary.value) return 1;
        if (deleted.status != axiom::StatusCode::InvalidInput || deleted.value ||
            txn.rollback().status != axiom::StatusCode::Ok ||
            kernel.topology().query().planar_face_area(face).value != original.value) return 1;
    }
    if (kernel.topology().query().face_boundary_length(face).value != original_boundary.value) return 1;
    const auto hole_plane = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    if (!hole_plane.value) return 1;
    {
        auto txn = kernel.topology().begin_transaction();
        const auto make_square_loop = [&](const std::array<axiom::Point3, 4>& points) {
            std::array<axiom::VertexId, 4> vertices{};
            std::array<axiom::CoedgeId, 4> coedges{};
            for (std::size_t i = 0; i < 4; ++i) {
                const auto vertex = txn.create_vertex(points[i]);
                if (!vertex.value) return axiom::LoopId{};
                vertices[i] = *vertex.value;
            }
            for (std::size_t i = 0; i < 4; ++i) {
                const auto next = (i + 1) % 4;
                const auto curve = kernel.curves().make_line_segment(points[i], points[next]);
                if (!curve.value) return axiom::LoopId{};
                const auto edge = txn.create_edge(*curve.value, vertices[i], vertices[next]);
                if (!edge.value) return axiom::LoopId{};
                const auto coedge = txn.create_coedge(*edge.value, false);
                if (!coedge.value) return axiom::LoopId{};
                coedges[i] = *coedge.value;
            }
            const auto loop = txn.create_loop(coedges);
            return loop.value.value_or(axiom::LoopId{});
        };
        const auto outer = make_square_loop({{{0.0, 0.0, 0.0}, {4.0, 0.0, 0.0},
                                             {4.0, 4.0, 0.0}, {0.0, 4.0, 0.0}}});
        const auto inner = make_square_loop({{{1.0, 1.0, 0.0}, {1.0, 2.0, 0.0},
                                             {2.0, 2.0, 0.0}, {2.0, 1.0, 0.0}}});
        if (!outer.value || !inner.value) return 1;
        const auto holed_face = txn.create_face(*hole_plane.value, outer, std::array{inner});
        if (!holed_face.value || txn.commit().status != axiom::StatusCode::Ok) return 1;
        const auto holed_area = kernel.topology().query().planar_face_area(*holed_face.value);
        if (holed_area.status != axiom::StatusCode::Ok || !holed_area.value ||
            !approx(*holed_area.value, 15.0)) {
            std::cerr << "unexpected planar face area with an inner loop\n";
            return 1;
        }
    }

    auto line_t = kernel.curve_service().closest_parameter(*line.value, {2.5, 3.0, 0.0});
    if (line_t.status != axiom::StatusCode::Ok || !line_t.value.has_value() || !approx(*line_t.value, 2.5)) {
        std::cerr << "unexpected line closest parameter\n";
        return 1;
    }

    auto circle_t = kernel.curve_service().closest_parameter(*circle.value, {0.0, 2.0, 0.0});
    if (circle_t.status != axiom::StatusCode::Ok || !circle_t.value.has_value() ||
        !approx(*circle_t.value, std::acos(-1.0) * 0.5)) {
        std::cerr << "unexpected circle closest parameter\n";
        return 1;
    }

    auto tilted_circle_eval = kernel.curve_service().eval(*tilted_circle.value, std::acos(-1.0) * 0.5, 1);
    if (tilted_circle_eval.status != axiom::StatusCode::Ok || !tilted_circle_eval.value.has_value() ||
        !approx(tilted_circle_eval.value->point.x, 1.0) || !approx(tilted_circle_eval.value->point.y, 2.0) ||
        !approx(tilted_circle_eval.value->point.z, 5.0)) {
        std::cerr << "unexpected tilted circle eval\n";
        return 1;
    }

    auto tilted_circle_t = kernel.curve_service().closest_parameter(*tilted_circle.value, tilted_circle_eval.value->point);
    if (tilted_circle_t.status != axiom::StatusCode::Ok || !tilted_circle_t.value.has_value() ||
        !approx(*tilted_circle_t.value, std::acos(-1.0) * 0.5)) {
        std::cerr << "unexpected tilted circle closest parameter\n";
        return 1;
    }

    auto tilted_circle_closest =
        kernel.curve_service().closest_point(*tilted_circle.value, {1.0, 2.0, 8.0});
    if (tilted_circle_closest.status != axiom::StatusCode::Ok || !tilted_circle_closest.value.has_value() ||
        !approx(tilted_circle_closest.value->x, 1.0) || !approx(tilted_circle_closest.value->y, 2.0) ||
        !approx(tilted_circle_closest.value->z, 5.0)) {
        std::cerr << "unexpected tilted circle closest point\n";
        return 1;
    }

    auto bezier_t = kernel.curve_service().closest_parameter(*bezier.value, {1.0, 1.0, 0.0});
    if (bezier_t.status != axiom::StatusCode::Ok || !bezier_t.value.has_value() ||
        !approx(*bezier_t.value, 0.5, 0.05)) {
        std::cerr << "unexpected bezier closest parameter\n";
        return 1;
    }

    auto bspline_t = kernel.curve_service().closest_parameter(*bspline.value, {1.0, 0.5, 0.0});
    if (bspline_t.status != axiom::StatusCode::Ok || !bspline_t.value.has_value() ||
        !approx(*bspline_t.value, 1.5, 0.05)) {
        std::cerr << "unexpected bspline closest parameter\n";
        return 1;
    }

    auto nurbs_t = kernel.curve_service().closest_parameter(*nurbs.value, {1.0, 1.33, 0.0});
    if (nurbs_t.status != axiom::StatusCode::Ok || !nurbs_t.value.has_value() ||
        !approx(*nurbs_t.value, 0.5, 0.05)) {
        std::cerr << "unexpected nurbs closest parameter\n";
        return 1;
    }

    auto sphere_uv = kernel.surface_service().closest_uv(*sphere.value, {0.0, 0.0, 5.0});
    if (sphere_uv.status != axiom::StatusCode::Ok || !sphere_uv.value.has_value() ||
        !approx(sphere_uv.value->first, 0.0) || !approx(sphere_uv.value->second, 0.0)) {
        std::cerr << "unexpected sphere closest uv\n";
        return 1;
    }

    auto cylinder_uv = kernel.surface_service().closest_uv(*cylinder.value, {0.0, 3.0, 7.0});
    if (cylinder_uv.status != axiom::StatusCode::Ok || !cylinder_uv.value.has_value() ||
        !approx(cylinder_uv.value->first, std::acos(-1.0) * 0.5) || !approx(cylinder_uv.value->second, 7.0)) {
        std::cerr << "unexpected cylinder closest uv\n";
        return 1;
    }

    auto tilted_eval =
        kernel.surface_service().eval(*tilted_cylinder.value, std::acos(-1.0) * 0.5, 7.0, 1);
    if (tilted_eval.status != axiom::StatusCode::Ok || !tilted_eval.value.has_value() ||
        !approx(tilted_eval.value->point.x, 8.0) || !approx(tilted_eval.value->point.y, 2.0) ||
        !approx(tilted_eval.value->point.z, 6.0)) {
        std::cerr << "unexpected tilted cylinder eval\n";
        return 1;
    }

    auto tilted_uv = kernel.surface_service().closest_uv(*tilted_cylinder.value, tilted_eval.value->point);
    if (tilted_uv.status != axiom::StatusCode::Ok || !tilted_uv.value.has_value() ||
        !approx(tilted_uv.value->first, std::acos(-1.0) * 0.5) || !approx(tilted_uv.value->second, 7.0)) {
        std::cerr << "unexpected tilted cylinder closest uv\n";
        return 1;
    }

    auto cone_eval = kernel.surface_service().eval(*cone.value, 0.0, 2.0, 1);
    if (cone_eval.status != axiom::StatusCode::Ok || !cone_eval.value.has_value() ||
        !approx(cone_eval.value->point.x, 2.0) || !approx(cone_eval.value->point.y, 0.0) ||
        !approx(cone_eval.value->point.z, 2.0)) {
        std::cerr << "unexpected cone eval\n";
        return 1;
    }

    auto cone_uv = kernel.surface_service().closest_uv(*cone.value, cone_eval.value->point);
    if (cone_uv.status != axiom::StatusCode::Ok || !cone_uv.value.has_value() ||
        !approx(cone_uv.value->first, 0.0) || !approx(cone_uv.value->second, 2.0)) {
        std::cerr << "unexpected cone closest uv\n";
        return 1;
    }

    auto cone_closest = kernel.surface_service().closest_point(*cone.value, {4.0, 0.0, 1.0});
    if (cone_closest.status != axiom::StatusCode::Ok || !cone_closest.value.has_value() ||
        !approx(cone_closest.value->x, 2.5) || !approx(cone_closest.value->y, 0.0) ||
        !approx(cone_closest.value->z, 2.5)) {
        std::cerr << "unexpected cone closest point\n";
        return 1;
    }

    auto torus_eval = kernel.surface_service().eval(*torus.value, 0.0, 0.0, 1);
    if (torus_eval.status != axiom::StatusCode::Ok || !torus_eval.value.has_value() ||
        !approx(torus_eval.value->point.x, 7.0) || !approx(torus_eval.value->point.y, 0.0) ||
        !approx(torus_eval.value->point.z, 0.0)) {
        std::cerr << "unexpected torus eval\n";
        return 1;
    }

    auto torus_uv = kernel.surface_service().closest_uv(*torus.value, torus_eval.value->point);
    if (torus_uv.status != axiom::StatusCode::Ok || !torus_uv.value.has_value() ||
        !approx(torus_uv.value->first, 0.0) || !approx(torus_uv.value->second, 0.0)) {
        std::cerr << "unexpected torus closest uv\n";
        return 1;
    }

    auto torus_closest = kernel.surface_service().closest_point(*torus.value, {10.0, 0.0, 0.0});
    if (torus_closest.status != axiom::StatusCode::Ok || !torus_closest.value.has_value() ||
        !approx(torus_closest.value->x, 7.0) || !approx(torus_closest.value->y, 0.0) ||
        !approx(torus_closest.value->z, 0.0)) {
        std::cerr << "unexpected torus closest point\n";
        return 1;
    }

    auto bspline_surface_uv = kernel.surface_service().closest_uv(*bspline_surface.value, {0.5, 0.5, 0.25});
    if (bspline_surface_uv.status != axiom::StatusCode::Ok || !bspline_surface_uv.value.has_value() ||
        !approx(bspline_surface_uv.value->first, 0.5, 0.08) ||
        !approx(bspline_surface_uv.value->second, 0.5, 0.08)) {
        std::cerr << "unexpected bspline surface closest uv\n";
        return 1;
    }

    auto bspline_surface_closest =
        kernel.surface_service().closest_point(*bspline_surface.value, {0.5, 0.5, 0.25});
    if (bspline_surface_closest.status != axiom::StatusCode::Ok || !bspline_surface_closest.value.has_value() ||
        !approx(bspline_surface_closest.value->x, 0.5, 0.08) ||
        !approx(bspline_surface_closest.value->y, 0.5, 0.08) ||
        !approx(bspline_surface_closest.value->z, 0.25, 0.08)) {
        std::cerr << "unexpected bspline surface closest point\n";
        return 1;
    }

    auto nurbs_surface_uv = kernel.surface_service().closest_uv(*nurbs_surface.value, {0.666666666667, 0.666666666667, 0.5});
    if (nurbs_surface_uv.status != axiom::StatusCode::Ok || !nurbs_surface_uv.value.has_value() ||
        !approx(nurbs_surface_uv.value->first, 0.5, 0.08) ||
        !approx(nurbs_surface_uv.value->second, 0.5, 0.08)) {
        std::cerr << "unexpected nurbs surface closest uv\n";
        return 1;
    }

    auto body = kernel.primitives().box({0.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
    if (body.status != axiom::StatusCode::Ok || !body.value.has_value()) {
        std::cerr << "failed to create body for eval graph test\n";
        return 1;
    }

    const auto body_label = std::string("body:") + std::to_string(body.value->value);
    auto body_node = kernel.eval_graph().register_node(axiom::NodeKind::Geometry, body_label);
    auto cache_node = kernel.eval_graph().register_node(axiom::NodeKind::Cache, "cache:mass");
    auto analysis_node = kernel.eval_graph().register_node(axiom::NodeKind::Analysis, "analysis:report");
    if (body_node.status != axiom::StatusCode::Ok || cache_node.status != axiom::StatusCode::Ok ||
        analysis_node.status != axiom::StatusCode::Ok || !body_node.value.has_value() ||
        !cache_node.value.has_value() || !analysis_node.value.has_value()) {
        std::cerr << "failed to register eval graph nodes\n";
        return 1;
    }

    auto dep1 = kernel.eval_graph().add_dependency(*analysis_node.value, *cache_node.value);
    auto dep2 = kernel.eval_graph().add_dependency(*cache_node.value, *body_node.value);
    auto dep_cycle = kernel.eval_graph().add_dependency(*body_node.value, *analysis_node.value);
    if (dep1.status != axiom::StatusCode::Ok || dep2.status != axiom::StatusCode::Ok ||
        dep_cycle.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "unexpected eval graph dependency behavior\n";
        return 1;
    }
    auto dep_cycle_diag = kernel.diagnostics().get(dep_cycle.diagnostic_id);
    if (dep_cycle_diag.status != axiom::StatusCode::Ok || !dep_cycle_diag.value.has_value() ||
        !has_issue_code(*dep_cycle_diag.value, axiom::diag_codes::kEvalCycleDetected)) {
        std::cerr << "cycle dependency should carry eval cycle diagnostic code\n";
        return 1;
    }
    auto analysis_deps = kernel.eval_graph().dependencies_of(*analysis_node.value);
    auto body_dependents = kernel.eval_graph().dependents_of(*body_node.value);
    if (analysis_deps.status != axiom::StatusCode::Ok || !analysis_deps.value.has_value() ||
        analysis_deps.value->size() != 1 || analysis_deps.value->front().value != cache_node.value->value ||
        body_dependents.status != axiom::StatusCode::Ok || !body_dependents.value.has_value() ||
        body_dependents.value->size() != 1 || body_dependents.value->front().value != cache_node.value->value) {
        std::cerr << "unexpected eval graph dependency query behavior\n";
        return 1;
    }

    auto exists_body_node = kernel.eval_graph().exists(*body_node.value);
    auto kind_body_node = kernel.eval_graph().kind_of(*body_node.value);
    auto label_body_node = kernel.eval_graph().label_of(*body_node.value);
    auto set_label_body_node = kernel.eval_graph().set_label(*body_node.value, "body:relabeled");
    auto node_count = kernel.eval_graph().node_count();
    auto dep_count_analysis = kernel.eval_graph().dependency_count(*analysis_node.value);
    auto dependent_count_body = kernel.eval_graph().dependent_count(*body_node.value);
    auto has_dep = kernel.eval_graph().has_dependency(*analysis_node.value, *cache_node.value);
    auto all_nodes = kernel.eval_graph().all_nodes();
    auto find_by_label = kernel.eval_graph().find_by_label_token("body", 10);
    auto labels = kernel.eval_graph().labels_of_nodes(std::array<axiom::NodeId, 2>{*body_node.value, *cache_node.value});
    auto is_leaf_body = kernel.eval_graph().is_leaf(*body_node.value);
    auto is_root_analysis = kernel.eval_graph().is_root(*analysis_node.value);
    auto body_binding_count = kernel.eval_graph().body_binding_count(*body.value);
    auto body_nodes = kernel.eval_graph().nodes_of_body(*body.value);
    if (exists_body_node.status != axiom::StatusCode::Ok || !exists_body_node.value.has_value() || !*exists_body_node.value ||
        kind_body_node.status != axiom::StatusCode::Ok || !kind_body_node.value.has_value() || *kind_body_node.value != axiom::NodeKind::Geometry ||
        label_body_node.status != axiom::StatusCode::Ok || !label_body_node.value.has_value() || label_body_node.value->empty() ||
        set_label_body_node.status != axiom::StatusCode::Ok ||
        node_count.status != axiom::StatusCode::Ok || !node_count.value.has_value() || *node_count.value < 3 ||
        dep_count_analysis.status != axiom::StatusCode::Ok || !dep_count_analysis.value.has_value() || *dep_count_analysis.value != 1 ||
        dependent_count_body.status != axiom::StatusCode::Ok || !dependent_count_body.value.has_value() || *dependent_count_body.value != 1 ||
        has_dep.status != axiom::StatusCode::Ok || !has_dep.value.has_value() || !*has_dep.value ||
        all_nodes.status != axiom::StatusCode::Ok || !all_nodes.value.has_value() || all_nodes.value->size() < 3 ||
        find_by_label.status != axiom::StatusCode::Ok || !find_by_label.value.has_value() || find_by_label.value->empty() ||
        labels.status != axiom::StatusCode::Ok || !labels.value.has_value() || labels.value->size() != 2 ||
        is_leaf_body.status != axiom::StatusCode::Ok || !is_leaf_body.value.has_value() || !*is_leaf_body.value ||
        is_root_analysis.status != axiom::StatusCode::Ok || !is_root_analysis.value.has_value() || !*is_root_analysis.value ||
        body_binding_count.status != axiom::StatusCode::Ok || !body_binding_count.value.has_value() || *body_binding_count.value == 0 ||
        body_nodes.status != axiom::StatusCode::Ok || !body_nodes.value.has_value() || body_nodes.value->empty()) {
        std::cerr << "unexpected extended eval graph query behavior\n";
        return 1;
    }

    auto invalidate_body = kernel.eval_graph().invalidate_body(*body.value);
    if (invalidate_body.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to invalidate body-linked nodes\n";
        return 1;
    }

    auto body_invalid = kernel.eval_graph().is_invalid(*body_node.value);
    auto cache_invalid = kernel.eval_graph().is_invalid(*cache_node.value);
    auto analysis_invalid = kernel.eval_graph().is_invalid(*analysis_node.value);
    if (body_invalid.status != axiom::StatusCode::Ok || cache_invalid.status != axiom::StatusCode::Ok ||
        analysis_invalid.status != axiom::StatusCode::Ok || !body_invalid.value.has_value() ||
        !cache_invalid.value.has_value() || !analysis_invalid.value.has_value() ||
        !*body_invalid.value || !*cache_invalid.value || !*analysis_invalid.value) {
        std::cerr << "unexpected invalidation propagation\n";
        return 1;
    }

    auto recompute = kernel.eval_graph().recompute(*analysis_node.value);
    if (recompute.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to recompute analysis node\n";
        return 1;
    }
    auto total_recompute_before_reset = kernel.eval_graph().total_recompute_count();
    if (total_recompute_before_reset.status != axiom::StatusCode::Ok || !total_recompute_before_reset.value.has_value() ||
        *total_recompute_before_reset.value == 0) {
        std::cerr << "unexpected total recompute count\n";
        return 1;
    }
    auto body_count = kernel.eval_graph().recompute_count(*body_node.value);
    auto cache_count = kernel.eval_graph().recompute_count(*cache_node.value);
    auto analysis_count = kernel.eval_graph().recompute_count(*analysis_node.value);
    if (body_count.status != axiom::StatusCode::Ok || cache_count.status != axiom::StatusCode::Ok ||
        analysis_count.status != axiom::StatusCode::Ok || !body_count.value.has_value() ||
        !cache_count.value.has_value() || !analysis_count.value.has_value() ||
        *body_count.value != 1 || *cache_count.value != 1 || *analysis_count.value != 1) {
        std::cerr << "unexpected recompute counts\n";
        return 1;
    }

    auto egm_after_first = kernel.eval_graph_metrics();
    if (egm_after_first.status != axiom::StatusCode::Ok || !egm_after_first.value.has_value() ||
        egm_after_first.value->recompute_events_total != *total_recompute_before_reset.value ||
        egm_after_first.value->max_per_node_recompute_count != 1 ||
        egm_after_first.value->nodes_with_recompute_nonzero != 3 ||
        !approx(egm_after_first.value->mean_recompute_events_per_node, 1.0, 1e-9) ||
        !approx(egm_after_first.value->mean_recompute_events_per_touched_node, 1.0, 1e-9)) {
        std::cerr << "eval_graph_metrics recompute distribution unexpected after first recompute\n";
        return 1;
    }

    auto tel_after_first_recompute = kernel.eval_graph().telemetry();
    if (tel_after_first_recompute.status != axiom::StatusCode::Ok || !tel_after_first_recompute.value.has_value() ||
        tel_after_first_recompute.value->invalidate_body_calls != 1 ||
        tel_after_first_recompute.value->recompute_finish_events != 3 ||
        tel_after_first_recompute.value->recompute_single_root_max_finish_nodes != 3 ||
        tel_after_first_recompute.value->recompute_single_root_max_stack_depth != 3) {
        std::cerr << "unexpected eval graph telemetry after first recompute\n";
        return 1;
    }

    auto analysis_invalid_after = kernel.eval_graph().is_invalid(*analysis_node.value);
    if (analysis_invalid_after.status != axiom::StatusCode::Ok || !analysis_invalid_after.value.has_value() ||
        *analysis_invalid_after.value) {
        std::cerr << "expected analysis node to be valid after recompute\n";
        return 1;
    }

    auto offset_result = kernel.modify().offset_body(*body.value, 0.2, {});
    if (offset_result.status != axiom::StatusCode::Ok || !offset_result.value.has_value()) {
        std::cerr << "failed to run offset operation for eval graph linkage test\n";
        return 1;
    }

    auto body_invalid_after_offset = kernel.eval_graph().is_invalid(*body_node.value);
    auto cache_invalid_after_offset = kernel.eval_graph().is_invalid(*cache_node.value);
    auto analysis_invalid_after_offset = kernel.eval_graph().is_invalid(*analysis_node.value);
    if (body_invalid_after_offset.status != axiom::StatusCode::Ok ||
        cache_invalid_after_offset.status != axiom::StatusCode::Ok ||
        analysis_invalid_after_offset.status != axiom::StatusCode::Ok ||
        !body_invalid_after_offset.value.has_value() ||
        !cache_invalid_after_offset.value.has_value() ||
        !analysis_invalid_after_offset.value.has_value() ||
        !*body_invalid_after_offset.value ||
        !*cache_invalid_after_offset.value ||
        !*analysis_invalid_after_offset.value) {
        std::cerr << "expected eval graph to be invalidated by topology-changing operation\n";
        return 1;
    }

    auto recompute_after_offset = kernel.eval_graph().recompute(*analysis_node.value);
    if (recompute_after_offset.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to recompute analysis node after offset invalidation\n";
        return 1;
    }
    auto body_count_after_offset = kernel.eval_graph().recompute_count(*body_node.value);
    auto cache_count_after_offset = kernel.eval_graph().recompute_count(*cache_node.value);
    auto analysis_count_after_offset = kernel.eval_graph().recompute_count(*analysis_node.value);
    if (body_count_after_offset.status != axiom::StatusCode::Ok ||
        cache_count_after_offset.status != axiom::StatusCode::Ok ||
        analysis_count_after_offset.status != axiom::StatusCode::Ok ||
        !body_count_after_offset.value.has_value() ||
        !cache_count_after_offset.value.has_value() ||
        !analysis_count_after_offset.value.has_value() ||
        *body_count_after_offset.value != 2 ||
        *cache_count_after_offset.value != 2 ||
        *analysis_count_after_offset.value != 2) {
        std::cerr << "unexpected recompute counts after topology-linked invalidation\n";
        return 1;
    }

    auto tel_after_second_recompute = kernel.eval_graph().telemetry();
    if (tel_after_second_recompute.status != axiom::StatusCode::Ok || !tel_after_second_recompute.value.has_value() ||
        tel_after_second_recompute.value->recompute_finish_events != 6) {
        std::cerr << "unexpected eval graph telemetry after second recompute\n";
        return 1;
    }
    if (kernel.eval_graph().reset_telemetry().status != axiom::StatusCode::Ok) {
        std::cerr << "failed to reset eval graph telemetry\n";
        return 1;
    }
    auto tel_reset = kernel.eval_graph().telemetry();
    if (tel_reset.status != axiom::StatusCode::Ok || !tel_reset.value.has_value() ||
        tel_reset.value->invalidate_body_calls != 0 || tel_reset.value->recompute_finish_events != 0 ||
        tel_reset.value->invalidate_node_redundant_calls != 0 ||
        tel_reset.value->recompute_root_already_valid_calls != 0 ||
        tel_reset.value->recompute_single_root_max_finish_nodes != 0 ||
        tel_reset.value->recompute_single_root_max_stack_depth != 0) {
        std::cerr << "eval graph telemetry not cleared by reset_telemetry\n";
        return 1;
    }

    if (kernel.eval_graph().reset_recompute_count(*analysis_node.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().reset_all_recompute_counts().status != axiom::StatusCode::Ok) {
        std::cerr << "failed to reset recompute counters\n";
        return 1;
    }

    auto shared_leaf = kernel.eval_graph().register_node(axiom::NodeKind::Geometry, "shared:leaf");
    auto cache_branch_1 = kernel.eval_graph().register_node(axiom::NodeKind::Cache, "shared:cache:1");
    auto cache_branch_2 = kernel.eval_graph().register_node(axiom::NodeKind::Cache, "shared:cache:2");
    auto analysis_root = kernel.eval_graph().register_node(axiom::NodeKind::Analysis, "shared:analysis");
    if (shared_leaf.status != axiom::StatusCode::Ok || cache_branch_1.status != axiom::StatusCode::Ok ||
        cache_branch_2.status != axiom::StatusCode::Ok || analysis_root.status != axiom::StatusCode::Ok ||
        !shared_leaf.value.has_value() || !cache_branch_1.value.has_value() ||
        !cache_branch_2.value.has_value() || !analysis_root.value.has_value()) {
        std::cerr << "failed to create shared dependency graph\n";
        return 1;
    }
    if (kernel.eval_graph().add_dependency(*cache_branch_1.value, *shared_leaf.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().add_dependency(*cache_branch_2.value, *shared_leaf.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().add_dependency(*analysis_root.value, *cache_branch_1.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().add_dependency(*analysis_root.value, *cache_branch_2.value).status != axiom::StatusCode::Ok) {
        std::cerr << "failed to setup shared dependency graph\n";
        return 1;
    }
    if (kernel.eval_graph().invalidate(*shared_leaf.value).status != axiom::StatusCode::Ok) {
        std::cerr << "failed to invalidate shared dependency leaf\n";
        return 1;
    }
    if (kernel.eval_graph().recompute(*analysis_root.value).status != axiom::StatusCode::Ok) {
        std::cerr << "failed to recompute shared dependency graph\n";
        return 1;
    }
    auto shared_leaf_count = kernel.eval_graph().recompute_count(*shared_leaf.value);
    if (shared_leaf_count.status != axiom::StatusCode::Ok || !shared_leaf_count.value.has_value() ||
        *shared_leaf_count.value != 1) {
        std::cerr << "shared leaf should be recomputed once in DAG traversal\n";
        return 1;
    }
    if (kernel.eval_graph().invalidate_many(std::array<axiom::NodeId, 2>{*cache_branch_1.value, *cache_branch_2.value}).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().recompute_many(std::array<axiom::NodeId, 2>{*cache_branch_1.value, *cache_branch_2.value}).status != axiom::StatusCode::Ok) {
        std::cerr << "batch invalidate/recompute failed\n";
        return 1;
    }
    auto tel_recompute_many = kernel.eval_graph().telemetry();
    if (tel_recompute_many.status != axiom::StatusCode::Ok || !tel_recompute_many.value.has_value() ||
        tel_recompute_many.value->recompute_many_batches != 1 ||
        tel_recompute_many.value->recompute_many_root_total != 2) {
        std::cerr << "unexpected recompute_many telemetry\n";
        return 1;
    }
    if (kernel.eval_graph().clear_dependencies(*analysis_root.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().clear_dependents(*shared_leaf.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().remove_dependency(*cache_branch_1.value, *shared_leaf.value).status != axiom::StatusCode::Ok) {
        std::cerr << "dependency clear/remove failed\n";
        return 1;
    }
    auto invalid_list = kernel.eval_graph().invalid_nodes();
    auto valid_list = kernel.eval_graph().valid_nodes();
    auto kind_nodes = kernel.eval_graph().nodes_of_kind(axiom::NodeKind::Cache);
    auto ids_asc = kernel.eval_graph().ids_sorted_asc();
    auto ids_desc = kernel.eval_graph().ids_sorted_desc();
    auto invalid_ratio = kernel.eval_graph().invalid_ratio();
    auto recompute_counts = kernel.eval_graph().recompute_counts_of(std::array<axiom::NodeId, 2>{*shared_leaf.value, *analysis_root.value});
    auto total_dep_edges = kernel.eval_graph().total_dependency_edges();
    auto total_rev_edges = kernel.eval_graph().total_reverse_dependency_edges();
    auto isolated = kernel.eval_graph().isolated_nodes();
    auto pruned = kernel.eval_graph().prune_dangling_dependencies();
    auto relabeled = kernel.eval_graph().relabel_by_prefix("shared:", "eval:");
    auto relabel_many = kernel.eval_graph().relabel_many(std::array<axiom::NodeId, 1>{*analysis_root.value}, "batch:");
    auto dep_pairs = kernel.eval_graph().dependency_pairs();
    auto rev_pairs = kernel.eval_graph().reverse_dependency_pairs();
    auto max_recompute_node = kernel.eval_graph().max_recompute_count_node();
    auto min_recompute_node = kernel.eval_graph().min_recompute_count_node();
    auto nodes_min_recompute = kernel.eval_graph().nodes_with_min_recompute(0);
    auto nodes_max_recompute = kernel.eval_graph().nodes_with_max_recompute(100);
    auto invalid_by_kind = kernel.eval_graph().invalidate_by_kind(axiom::NodeKind::Cache);
    auto recompute_by_kind = kernel.eval_graph().recompute_by_kind(axiom::NodeKind::Cache);
    auto contains_token = kernel.eval_graph().contains_label_token("batch");
    auto label_hist = kernel.eval_graph().label_histogram_prefix(5);
    auto bound_bodies = kernel.eval_graph().body_binding_bodies();
    auto bound_body_count = kernel.eval_graph().bound_body_count();
    auto has_any_invalid = kernel.eval_graph().has_any_invalid();
    auto has_any_dep = kernel.eval_graph().has_any_dependency();
    auto invalid_kind_nodes = kernel.eval_graph().invalid_nodes_of_kind(axiom::NodeKind::Cache);
    auto valid_kind_nodes = kernel.eval_graph().valid_nodes_of_kind(axiom::NodeKind::Cache);
    if (invalid_list.status != axiom::StatusCode::Ok || !invalid_list.value.has_value() ||
        valid_list.status != axiom::StatusCode::Ok || !valid_list.value.has_value() ||
        kind_nodes.status != axiom::StatusCode::Ok || !kind_nodes.value.has_value() ||
        ids_asc.status != axiom::StatusCode::Ok || !ids_asc.value.has_value() ||
        ids_desc.status != axiom::StatusCode::Ok || !ids_desc.value.has_value() ||
        invalid_ratio.status != axiom::StatusCode::Ok || !invalid_ratio.value.has_value() ||
        recompute_counts.status != axiom::StatusCode::Ok || !recompute_counts.value.has_value() || recompute_counts.value->size() != 2 ||
        total_dep_edges.status != axiom::StatusCode::Ok || !total_dep_edges.value.has_value() ||
        total_rev_edges.status != axiom::StatusCode::Ok || !total_rev_edges.value.has_value() ||
        isolated.status != axiom::StatusCode::Ok || !isolated.value.has_value() ||
        pruned.status != axiom::StatusCode::Ok || !pruned.value.has_value() ||
        relabeled.status != axiom::StatusCode::Ok || !relabeled.value.has_value() ||
        relabel_many.status != axiom::StatusCode::Ok ||
        dep_pairs.status != axiom::StatusCode::Ok || !dep_pairs.value.has_value() ||
        rev_pairs.status != axiom::StatusCode::Ok || !rev_pairs.value.has_value() ||
        max_recompute_node.status != axiom::StatusCode::Ok || !max_recompute_node.value.has_value() ||
        min_recompute_node.status != axiom::StatusCode::Ok || !min_recompute_node.value.has_value() ||
        nodes_min_recompute.status != axiom::StatusCode::Ok || !nodes_min_recompute.value.has_value() ||
        nodes_max_recompute.status != axiom::StatusCode::Ok || !nodes_max_recompute.value.has_value() ||
        invalid_by_kind.status != axiom::StatusCode::Ok ||
        recompute_by_kind.status != axiom::StatusCode::Ok ||
        contains_token.status != axiom::StatusCode::Ok || !contains_token.value.has_value() || !*contains_token.value ||
        label_hist.status != axiom::StatusCode::Ok || !label_hist.value.has_value() ||
        bound_bodies.status != axiom::StatusCode::Ok || !bound_bodies.value.has_value() ||
        bound_body_count.status != axiom::StatusCode::Ok || !bound_body_count.value.has_value() ||
        has_any_invalid.status != axiom::StatusCode::Ok || !has_any_invalid.value.has_value() ||
        has_any_dep.status != axiom::StatusCode::Ok || !has_any_dep.value.has_value() || !*has_any_dep.value ||
        invalid_kind_nodes.status != axiom::StatusCode::Ok || !invalid_kind_nodes.value.has_value() ||
        valid_kind_nodes.status != axiom::StatusCode::Ok || !valid_kind_nodes.value.has_value()) {
        std::cerr << "invalid/valid/kind query failed\n";
        return 1;
    }
    if (kernel.eval_graph().remove_nodes_many(std::array<axiom::NodeId, 1>{*cache_branch_2.value}).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().clear_nodes_of_kind(axiom::NodeKind::Cache).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().unbind_body(*body.value).status != axiom::StatusCode::Ok ||
        kernel.eval_graph().unbind_all_bodies().status != axiom::StatusCode::Ok) {
        std::cerr << "batch remove/clear/unbind failed\n";
        return 1;
    }
    if (kernel.eval_graph().remove_node(*analysis_root.value).status != axiom::StatusCode::Ok) {
        std::cerr << "remove node failed\n";
        return 1;
    }
    auto clear_graph = kernel.eval_graph().clear_graph();
    auto node_count_after_clear = kernel.eval_graph().node_count();
    if (clear_graph.status != axiom::StatusCode::Ok ||
        node_count_after_clear.status != axiom::StatusCode::Ok ||
        !node_count_after_clear.value.has_value() || *node_count_after_clear.value != 0) {
        std::cerr << "clear graph failed\n";
        return 1;
    }

    auto on_curve = kernel.predicates().point_on_curve({0.5, 0.0, 0.0}, *line.value, 1e-6);
    auto on_surface = kernel.predicates().point_on_surface({0.0, 0.0, 5.0}, *sphere.value, 1e-6);
    auto inside_body = kernel.predicates().point_in_body({1.0, 1.0, 1.0}, *body.value, 1e-6);
    auto outside_body = kernel.predicates().point_in_body({5.0, 5.0, 5.0}, *body.value, 1e-6);
    if (on_curve.status != axiom::StatusCode::Ok || !on_curve.value.has_value() || !*on_curve.value ||
        on_curve.diagnostic_id.value == 0 ||
        on_surface.status != axiom::StatusCode::Ok || !on_surface.value.has_value() || !*on_surface.value ||
        on_surface.diagnostic_id.value == 0 ||
        inside_body.status != axiom::StatusCode::Ok || !inside_body.value.has_value() || !*inside_body.value ||
        inside_body.diagnostic_id.value == 0 ||
        outside_body.status != axiom::StatusCode::Ok || !outside_body.value.has_value() || *outside_body.value) {
        std::cerr << "unexpected predicate service success behavior\n";
        return 1;
    }

    auto invalid_curve_pred = kernel.predicates().point_on_curve({0.0, 0.0, 0.0}, axiom::CurveId {999999}, 1e-6);
    if (invalid_curve_pred.status != axiom::StatusCode::InvalidInput || invalid_curve_pred.diagnostic_id.value == 0) {
        std::cerr << "invalid curve predicate should return structured failure\n";
        return 1;
    }
    auto invalid_curve_diag = kernel.diagnostics().get(invalid_curve_pred.diagnostic_id);
    if (invalid_curve_diag.status != axiom::StatusCode::Ok || !invalid_curve_diag.value.has_value() ||
        !has_issue_code(*invalid_curve_diag.value, axiom::diag_codes::kCoreInvalidHandle)) {
        std::cerr << "invalid curve predicate diagnostic is unexpected\n";
        return 1;
    }

    auto invalid_body_pred = kernel.predicates().point_in_body({0.0, 0.0, 0.0}, axiom::BodyId {999999}, 1e-6);
    if (invalid_body_pred.status != axiom::StatusCode::InvalidInput || invalid_body_pred.diagnostic_id.value == 0) {
        std::cerr << "invalid body predicate should return structured failure\n";
        return 1;
    }

    {
        axiom::Kernel k_tel;
        auto n = k_tel.eval_graph().register_node(axiom::NodeKind::Cache, "telemetry:probe");
        if (n.status != axiom::StatusCode::Ok || !n.value.has_value()) {
            std::cerr << "telemetry probe node register failed\n";
            return 1;
        }
        auto ex0 = k_tel.eval_graph().exists(*n.value);
        auto inv0 = k_tel.eval_graph().is_invalid(*n.value);
        auto rc0 = k_tel.eval_graph().recompute_count(*n.value);
        if (ex0.status != axiom::StatusCode::Ok || !ex0.value.has_value() || !*ex0.value ||
            inv0.status != axiom::StatusCode::Ok || !inv0.value.has_value() || *inv0.value ||
            rc0.status != axiom::StatusCode::Ok || !rc0.value.has_value() || *rc0.value != 0) {
            std::cerr << "telemetry probe initial state reads failed\n";
            return 1;
        }
        if (k_tel.eval_graph().invalidate(*n.value).status != axiom::StatusCode::Ok ||
            k_tel.eval_graph().invalidate(*n.value).status != axiom::StatusCode::Ok) {
            std::cerr << "telemetry probe double invalidate failed\n";
            return 1;
        }
        auto t1 = k_tel.eval_graph().telemetry();
        if (t1.status != axiom::StatusCode::Ok || !t1.value.has_value() ||
            t1.value->invalidate_node_redundant_calls != 1U) {
            std::cerr << "expected one redundant invalidate_node call on already-invalid root\n";
            return 1;
        }
        if (k_tel.eval_graph().recompute(*n.value).status != axiom::StatusCode::Ok ||
            k_tel.eval_graph().recompute(*n.value).status != axiom::StatusCode::Ok) {
            std::cerr << "telemetry probe double recompute failed\n";
            return 1;
        }
        auto t2 = k_tel.eval_graph().telemetry();
        if (t2.status != axiom::StatusCode::Ok || !t2.value.has_value() ||
            t2.value->recompute_root_already_valid_calls != 1U) {
            std::cerr << "expected one recompute_root_already_valid when root was not invalid\n";
            return 1;
        }
        (void)k_tel.eval_graph().exists(*n.value);
        (void)k_tel.eval_graph().is_invalid(*n.value);
        (void)k_tel.eval_graph().recompute_count(*n.value);
        auto t3 = k_tel.eval_graph().telemetry();
        if (t3.status != axiom::StatusCode::Ok || !t3.value.has_value() ||
            t3.value->eval_graph_state_read_calls != t2.value->eval_graph_state_read_calls + 3U) {
            std::cerr << "expected eval_graph_state_read_calls to increase by three per read batch\n";
            return 1;
        }
    }

    return 0;
}
