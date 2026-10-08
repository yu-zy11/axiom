#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

namespace {

std::array<double,3> xyz(const axiom::Point3& p) { return {p.x,p.y,p.z}; }

// Rectangular material minus rectangular void is an independent analytic
// reference. In an open box the void reaches the selected outside plane.
bool check_boundary(axiom::Kernel& kernel, axiom::BodyId body,
                    const std::array<double,3>& low, const std::array<double,3>& length,
                    double thickness = 0, int opening = -1) {
    auto& query = kernel.topology().query();
    const auto fail = [&](int line) {
        std::cerr << "offset/shell analytic boundary opening=" << opening << " line=" << line << '\n';
        return false;
    };
    const double epsilon = 1e-7*std::max({1.0,length[0],length[1],length[2]});
    std::array<double,3> inner_low = low, inner_length = length;
    for (int a=0; a<3; ++a) { inner_low[a] += thickness; inner_length[a] -= 2*thickness; }
    if (opening >= 0) {
        const int axis = opening/2;
        inner_length[axis] += thickness;
        if (opening%2 == 0) inner_low[axis] = low[axis];
    }
    const auto volume_of = [](const auto& d) { return d[0]*d[1]*d[2]; };
    const auto area_of = [](const auto& d) { return 2*(d[0]*d[1]+d[0]*d[2]+d[1]*d[2]); };
    const double outer_volume = volume_of(length);
    const double inner_volume = thickness > 0 ? volume_of(inner_length) : 0;
    const double volume = outer_volume-inner_volume;
    double area = area_of(length)+(thickness > 0 ? area_of(inner_length) : 0);
    if (opening >= 0) {
        const int a=(opening/2+1)%3, b=(opening/2+2)%3;
        // Removing the outer cap, omitting the inner cap and adding the rim
        // reduces the sum of both closed-box areas by twice the inner cap.
        area -= 2*inner_length[a]*inner_length[b];
    }
    const auto mass = query.body_mass_properties(body);
    const auto sdk_mass = kernel.query().mass_properties(body);
    if (!mass.value || !sdk_mass.value || std::abs(mass.value->volume-volume)>epsilon ||
        std::abs(mass.value->area-area)>epsilon || std::abs(sdk_mass.value->volume-volume)>epsilon ||
        kernel.validate().validate_all(body,axiom::ValidationMode::Strict).status != axiom::StatusCode::Ok ||
        kernel.topology().validate().validate_body_closedness(body).status != axiom::StatusCode::Ok ||
        kernel.topology().validate().validate_body_sources(body).status != axiom::StatusCode::Ok) return fail(__LINE__);
    const auto center = xyz(mass.value->centroid);
    for (int a=0; a<3; ++a) {
        const double expected = (outer_volume*(low[a]+length[a]/2)-
            inner_volume*(inner_low[a]+inner_length[a]/2))/volume;
        if (std::abs(center[a]-expected)>epsilon) return fail(__LINE__);
    }
    const auto vertices = query.vertices_of_body(body);
    const auto faces = query.faces_of_body(body);
    const auto edges = query.edges_of_body(body);
    const auto shells = query.shells_of_body(body);
    const auto bbox = kernel.representation().bbox_of_body(body);
    if (!vertices.value || !faces.value || !edges.value || !shells.value || !bbox.value ||
        shells.value->size() != (thickness > 0 && opening < 0 ? 2u : 1u) ||
        vertices.value->size() != (thickness > 0 ? 16u : 8u)) return fail(__LINE__);
    const auto bbox_low=xyz(bbox.value->min), bbox_high=xyz(bbox.value->max);
    for (int a=0; a<3; ++a)
        if (std::abs(bbox_low[a]-low[a])>epsilon || std::abs(bbox_high[a]-low[a]-length[a])>epsilon)
            return fail(__LINE__);
    // Match every actual vertex to a unique analytic inner or outer corner.
    std::vector<std::array<double,3>> reference;
    for (int inner=0; inner<(thickness>0 ? 2 : 1); ++inner) for (int corner=0; corner<8; ++corner) {
        std::array<double,3> p {};
        for (int a=0; a<3; ++a) p[a]=(inner ? inner_low[a] : low[a])+
            ((corner&(1<<a)) ? (inner ? inner_length[a] : length[a]) : 0);
        reference.push_back(p);
    }
    for (const auto vertex : *vertices.value) {
        const auto point = query.point_of_vertex(vertex);
        if (!point.value) return fail(__LINE__);
        const auto p=xyz(*point.value);
        const auto matched = std::find_if(reference.begin(),reference.end(),[&](const auto& expected) {
            return std::hypot(p[0]-expected[0],p[1]-expected[1],p[2]-expected[2]) <= epsilon;
        });
        if (matched == reference.end()) return fail(__LINE__);
        reference.erase(matched);
    }
    double face_area=0;
    std::array<bool,6> outer_planes {}, inner_planes {};
    for (const auto face : *faces.value) {
        const auto surface=query.surface_of_face(face);
        const auto loops=query.loops_of_face(face);
        const auto a=query.planar_face_area(face);
        if (!surface.value || !loops.value || loops.value->size()!=1 || !a.value || *a.value<=0)
            return fail(__LINE__);
        face_area += *a.value;
        const auto loop_vertices=query.vertices_of_loop(loops.value->front());
        if (!loop_vertices.value || loop_vertices.value->size()!=4) return fail(__LINE__);
        std::array<double,3> minimum {INFINITY,INFINITY,INFINITY}, maximum {-INFINITY,-INFINITY,-INFINITY};
        for (const auto vertex : *loop_vertices.value) {
            const auto p=query.point_of_vertex(vertex);
            if (!p.value) return fail(__LINE__);
            const auto coordinates=xyz(*p.value);
            for (int axis=0; axis<3; ++axis) {
                minimum[axis]=std::min(minimum[axis],coordinates[axis]);
                maximum[axis]=std::max(maximum[axis],coordinates[axis]);
            }
        }
        const auto evaluated=kernel.surface_service().eval(*surface.value,0,0,1);
        if (!evaluated.value) return fail(__LINE__);
        const std::array normal {evaluated.value->normal.x,evaluated.value->normal.y,evaluated.value->normal.z};
        bool plane_matched=false;
        for (int axis=0; axis<3; ++axis) if (maximum[axis]-minimum[axis]<=epsilon) {
            const double coordinate=minimum[axis];
            if (std::abs(std::abs(normal[axis])-1)>epsilon) return fail(__LINE__);
            for (int high=0; high<2; ++high) {
                const int side=2*axis+high;
                if (std::abs(coordinate-low[axis]-(high ? length[axis] : 0))<=epsilon) {
                    // Opening rim has the removed face's outward normal.
                    if (normal[axis]*(high ? 1 : -1)<0.99) return fail(__LINE__);
                    outer_planes[side]=true; plane_matched=true;
                } else if (thickness>0 && side!=opening &&
                           std::abs(coordinate-inner_low[axis]-(high ? inner_length[axis] : 0))<=epsilon) {
                    if (normal[axis]*(high ? -1 : 1)<0.99) return fail(__LINE__);
                    inner_planes[side]=true; plane_matched=true;
                }
            }
        }
        if (!plane_matched) return fail(__LINE__);
    }
    if (std::abs(face_area-area)>epsilon) return fail(__LINE__);
    for (int side=0; side<6; ++side)
        if (!outer_planes[side] || (thickness>0 && side!=opening && !inner_planes[side])) return fail(__LINE__);
    for (const auto edge : *edges.value) {
        const auto uses=query.coedges_of_edge(edge);
        const auto adjacent=query.faces_of_edge(edge);
        if (!uses.value || uses.value->size()!=2 || (*uses.value)[0]==(*uses.value)[1] ||
            !adjacent.value || adjacent.value->size()!=2 || (*adjacent.value)[0]==(*adjacent.value)[1])
            return fail(__LINE__);
    }
    if (thickness>0) {
        const axiom::Point3 cavity {inner_low[0]+inner_length[0]/2,inner_low[1]+inner_length[1]/2,
                                   inner_low[2]+inner_length[2]/2};
        const auto location=query.locate_point(body,cavity);
        if (!location.value || location.value->location != axiom::BodyPointLocation::Outside) return fail(__LINE__);
        const auto regions=query.body_shell_regions(body);
        if (!regions.value || regions.value->size()!=shells.value->size()) return fail(__LINE__);
        const auto voids=std::count_if(regions.value->begin(),regions.value->end(),[](const auto& region) {
            return region.role==axiom::BodyShellRole::Void && region.nesting_depth==1 && region.parent_shell.has_value();
        });
        if (voids!=(opening<0 ? 1 : 0)) return fail(__LINE__);
    }
    return true;
}

axiom::FaceId select_face(axiom::Kernel& kernel, axiom::BodyId body, int side,
                           const std::array<double,3>& low, const std::array<double,3>& length) {
    const auto faces=kernel.topology().query().faces_of_body(body);
    if (!faces.value) return {};
    const int axis=side/2;
    for (const auto face : *faces.value) {
        const auto bbox=kernel.topology().query().bbox_of_face(face);
        if (!bbox.value) continue;
        const auto minimum=xyz(bbox.value->min), maximum=xyz(bbox.value->max);
        const double coordinate=low[axis]+(side%2 ? length[axis] : 0);
        if (std::abs(minimum[axis]-coordinate)<1e-9 && std::abs(maximum[axis]-coordinate)<1e-9) return face;
    }
    return {};
}

bool success_references() {
    for (const double scale : {0.1,1.0,10.0}) {
        axiom::Kernel kernel;
        const std::array low {-7*scale,11*scale,-3*scale}, length {4*scale,5*scale,6*scale};
        const auto source=kernel.primitives().box({low[0],low[1],low[2]},length[0],length[1],length[2]);
        if (!source.value) return false;
        const auto source_faces=kernel.topology().query().faces_of_body(*source.value).value;
        const auto source_edges=kernel.topology().query().edges_of_body(*source.value).value;
        for (const double distance : {-0.2*scale,0.3*scale}) {
            const auto result=kernel.modify().offset_body(*source.value,distance,{});
            std::array<double,3> shifted_low {}, shifted_length {};
            for (int a=0; a<3; ++a) { shifted_low[a]=low[a]-distance; shifted_length[a]=length[a]+2*distance; }
            if (!result.value || !check_boundary(kernel,result.value->output,shifted_low,shifted_length)) return false;
        }
        for (int opening=-1; opening<6; ++opening) {
            std::vector<axiom::FaceId> removed;
            if (opening>=0) {
                const auto face=select_face(kernel,*source.value,opening,low,length);
                if (!face.value) return false;
                removed.push_back(face);
            }
            const auto result=kernel.modify().shell_body(*source.value,removed,0.25*scale);
            const auto report=kernel.diagnostics().get(result.diagnostic_id);
            if (!result.value || !report.value || !check_boundary(kernel,result.value->output,low,length,0.25*scale,opening) ||
                std::none_of(report.value->issues.begin(),report.value->issues.end(),[](const auto& issue) {
                    return issue.code==axiom::diag_codes::kModCompleted && issue.stage=="modify.shell.complete";
                })) return false;
            const auto sources=kernel.topology().query().source_bodies_of_body(result.value->output);
            if (!sources.value || *sources.value!=std::vector<axiom::BodyId>{*source.value}) return false;
        }
        if (kernel.topology().query().faces_of_body(*source.value).value!=source_faces ||
            kernel.topology().query().edges_of_body(*source.value).value!=source_edges ||
            !check_boundary(kernel,*source.value,low,length)) return false;
    }
    // Common enclosure workflow: enlarge the stock, then open its upper cap.
    axiom::Kernel kernel;
    const auto stock=kernel.primitives().box({1,2,3},4,5,6);
    if (!stock.value) return false;
    const auto offset=kernel.modify().offset_body(*stock.value,0.5,{});
    if (!offset.value) return false;
    const std::array low {0.5,1.5,2.5}, length {5.0,6.0,7.0};
    const auto cap=select_face(kernel,offset.value->output,5,low,length);
    const auto shell=kernel.modify().shell_body(offset.value->output,std::array{cap},0.5);
    return shell.value && check_boundary(kernel,shell.value->output,low,length,0.5,5);
}

bool failures_and_rollback() {
    axiom::Kernel kernel;
    const auto source=kernel.primitives().box({0,0,0},4,5,6);
    const auto foreign=kernel.primitives().box({10,0,0},4,5,6);
    const auto curved=kernel.primitives().sphere({20,0,0},2);
    const auto huge=kernel.primitives().box({1e16,1e16,1e16},16,16,16);
    if (!source.value || !foreign.value || !curved.value || !huge.value) return false;
    auto& query=kernel.topology().query();
    const auto faces=query.faces_of_body(*source.value).value, foreign_faces=query.faces_of_body(*foreign.value).value;
    const auto edges=query.edges_of_body(*source.value).value;
    const auto shells=query.shells_of_body(*source.value).value;
    const auto source_bodies=query.source_bodies_of_body(*source.value).value;
    const auto source_shells=query.source_shells_of_body(*source.value).value;
    const auto source_faces=query.source_faces_of_body(*source.value).value;
    if (!faces || !foreign_faces || !edges || !shells) return false;
    const auto mesh=kernel.convert().brep_to_mesh(*source.value,{});
    const auto source_curve=query.curve_of_edge(edges->front());
    const auto source_surface=query.surface_of_face(faces->front());
    if (!source_curve.value || !source_surface.value ||
        !kernel.curve_service().eval(*source_curve.value,0,1).value ||
        !kernel.surface_service().eval(*source_surface.value,0,0,1).value) return false;
    const auto node=kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"body:"+std::to_string(source.value->value));
    const auto consumer=kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"offset_shell:consumer");
    if (!mesh.value || !node.value || !consumer.value ||
        kernel.eval_graph().add_dependency(*consumer.value,*node.value).status!=axiom::StatusCode::Ok ||
        kernel.eval_graph().is_invalid(*node.value).value!=std::optional<bool>{false} ||
        kernel.eval_graph().is_invalid(*consumer.value).value!=std::optional<bool>{false}) return false;
    const auto bridge_counts=[&] {
        const auto metrics=kernel.eval_graph_metrics();
        if (!metrics.value) return std::array<std::uint64_t,5>{};
        const auto& b=metrics.value->invalidation_bridge;
        return std::array{b.for_body_entries,b.for_faces_entries,b.for_bodies_batches,
            b.for_bodies_list_size_total,b.downstream_invalidation_steps};
    };
    if (!kernel.eval_graph_metrics().value) return false;
    auto transaction=kernel.topology().begin_transaction();
    const auto sentinel=transaction.create_vertex({50,60,70});
    if (!sentinel.value) return false;
    const auto rejected=[&](const auto& operation, std::string_view code, std::string_view stage) {
        const auto objects=kernel.object_count_total().value, geometry=kernel.geometry_count().value;
        const auto topology=kernel.topology_count().value, bodies=kernel.body_count().value;
        const auto next=kernel.next_object_id().value, version=kernel.topology_version_next().value;
        const auto writes=transaction.write_operation_count().value;
        const auto before=kernel.runtime_store_counts().value;
        const auto bridge_before=bridge_counts();
        const auto node_invalid=kernel.eval_graph().is_invalid(*node.value).value;
        const auto consumer_invalid=kernel.eval_graph().is_invalid(*consumer.value).value;
        const auto recomputes=kernel.eval_graph().total_recompute_count().value;
        const auto result=operation();
        const auto report=kernel.diagnostics().get(result.diagnostic_id);
        const auto after=kernel.runtime_store_counts().value;
        if (result.status==axiom::StatusCode::Ok || result.value || !report.value || !before || !after ||
            std::none_of(report.value->issues.begin(),report.value->issues.end(),[&](const auto& issue) {
                return issue.code==code && issue.stage==stage && issue.severity==axiom::IssueSeverity::Error;
            }) || kernel.object_count_total().value!=objects || kernel.geometry_count().value!=geometry ||
            kernel.topology_count().value!=topology || kernel.body_count().value!=bodies ||
            kernel.next_object_id().value!=next || kernel.topology_version_next().value!=version ||
            transaction.write_operation_count().value!=writes ||
            query.faces_of_body(*source.value).value!=faces || query.edges_of_body(*source.value).value!=edges ||
            query.source_bodies_of_body(*source.value).value!=source_bodies ||
            query.source_shells_of_body(*source.value).value!=source_shells ||
            query.source_faces_of_body(*source.value).value!=source_faces ||
            before->mesh_records!=after->mesh_records || before->tessellation_cache_entries!=after->tessellation_cache_entries ||
            before->face_tessellation_cache_entries!=after->face_tessellation_cache_entries ||
            before->curve_eval_cache_entries!=after->curve_eval_cache_entries ||
            before->surface_eval_cache_entries!=after->surface_eval_cache_entries ||
            before->eval_node_records!=after->eval_node_records ||
            before->tessellation_metrics.body_cache_hits!=after->tessellation_metrics.body_cache_hits ||
            before->tessellation_metrics.body_cache_misses!=after->tessellation_metrics.body_cache_misses ||
            bridge_counts()!=bridge_before || kernel.eval_graph().total_recompute_count().value!=recomputes ||
            kernel.eval_graph().is_invalid(*node.value).value!=node_invalid ||
            kernel.eval_graph().is_invalid(*consumer.value).value!=consumer_invalid ||
            query.has_vertex(*sentinel.value).value!=std::optional<bool>{true}) {
            std::cerr << "offset/shell rejection isolation code=" << code << " stage=" << stage << '\n';
            if (report.value) for (const auto& issue : report.value->issues) std::cerr << issue.code << ' ' << issue.stage << '\n';
            return false;
        }
        return true;
    };
    using namespace axiom::diag_codes;
    if (!rejected([&] { return kernel.modify().offset_body(*source.value,-2,{}); },kModOffsetSelfIntersection,"modify.offset.self_intersection") ||
        !rejected([&] { return kernel.modify().shell_body(*source.value,{},2); },kModShellFailure,"modify.shell.thickness") ||
        !rejected([&] { return kernel.modify().shell_body(*source.value,{},1.99999975); },kModShellFailure,"modify.shell.cavity_tolerance") ||
        !rejected([&] { return kernel.modify().offset_body(*curved.value,.2,{}); },kModUnsupportedGeometry,"modify.offset.support_gate") ||
        !rejected([&] { return kernel.modify().shell_body(*curved.value,{},.2); },kModUnsupportedGeometry,"modify.shell.support_gate") ||
        !rejected([&] { return kernel.modify().shell_body(*source.value,std::array{foreign_faces->front()},.2); },kModShellFailure,"modify.shell.invalid_faces") ||
        !rejected([&] { return kernel.modify().shell_body(*source.value,std::array{(*faces)[0],(*faces)[1]},.2); },kModUnsupportedGeometry,"modify.shell.support_gate")) return false;
    for (const double invalid : {0.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        if (!rejected([&] { return kernel.modify().offset_body(*source.value,invalid,{}); },kModOffsetInvalid,"modify.offset.input_gate") ||
            !rejected([&] { return kernel.modify().shell_body(*source.value,{},invalid); },kModShellFailure,"modify.shell.input_gate")) return false;
    }
    if (!rejected([&] { return kernel.modify().offset_body(*source.value,1e-7,{}); },kModDegenerateGeometry,"modify.offset.geometry_gate") ||
        !rejected([&] { return kernel.modify().shell_body(*source.value,{},1e-7); },kModDegenerateGeometry,"modify.shell.geometry_gate")) return false;
    axiom::TolerancePolicy invalid_tolerance;
    invalid_tolerance.linear=-1;
    if (!rejected([&] { return kernel.modify().offset_body(*source.value,.2,invalid_tolerance); },kModOffsetInvalid,"modify.offset.input_gate")) return false;
    if (!rejected([&] { return kernel.modify().offset_body(*huge.value,.1,{}); },kModDegenerateGeometry,"modify.offset.geometry_gate") ||
        !rejected([&] { return kernel.modify().shell_body(*huge.value,{},.1); },kModDegenerateGeometry,"modify.shell.geometry_gate") ||
        !rejected([&] { return kernel.modify().offset_body(*source.value,std::numeric_limits<double>::max(),{}); },kModDegenerateGeometry,"modify.offset.geometry_gate") ||
        !rejected([&] { return kernel.modify().shell_body(*source.value,std::array{faces->front(),faces->front()},.2); },kModShellFailure,"modify.shell.invalid_faces") ||
        !rejected([&] { return kernel.modify().shell_body(*source.value,std::array{axiom::FaceId{999999999}},.2); },kModShellFailure,"modify.shell.invalid_faces")) return false;
    for (int field=0; field<4; ++field) {
        axiom::TolerancePolicy policy;
        if (field==0) policy.linear=std::numeric_limits<double>::infinity();
        if (field==1) policy.angular=-1;
        if (field==2) policy.min_local=-1;
        if (field==3) policy.max_local=policy.min_local/2;
        if (!rejected([&] { return kernel.modify().offset_body(*source.value,.2,policy); },kModOffsetInvalid,"modify.offset.input_gate")) return false;
    }
    axiom::TolerancePolicy wide;
    wide.linear=3; wide.min_local=3; wide.max_local=3;
    if (!rejected([&] { return kernel.modify().offset_body(*source.value,4,wide); },kModDegenerateGeometry,"modify.offset.geometry_gate")) return false;
    // The caller's fine construction tolerance permits a remaining 5e-7
    // width, but the unchanged kernel Strict tolerance is 1e-6. This reaches
    // real scratch materialization and then rejects before publication.
    axiom::TolerancePolicy fine;
    fine.linear=1e-9; fine.min_local=1e-9; fine.max_local=1e-3;
    if (!rejected([&] { return kernel.modify().offset_body(*source.value,-1.99999975,fine); },
                  kModOffsetValidateFailed,"modify.offset.validate")) return false;
    // A successful operation enlists all newly owned topology in this active
    // transaction; a following failure preserves both it and earlier writes.
    const auto runtime_before_success=kernel.runtime_store_counts().value;
    const auto objects_before_success=kernel.object_count_total().value;
    auto expected_bridge=bridge_counts();
    ++expected_bridge[0]; ++expected_bridge[2]; ++expected_bridge[3]; expected_bridge[4]+=2;
    const auto offset=kernel.modify().offset_body(*source.value,.25,{});
    if (!offset.value || bridge_counts()!=expected_bridge ||
        kernel.eval_graph().is_invalid(*node.value).value!=std::optional<bool>{true} ||
        kernel.eval_graph().is_invalid(*consumer.value).value!=std::optional<bool>{true} ||
        !rejected([&] { return kernel.modify().shell_body(*source.value,{},3); },kModShellFailure,"modify.shell.thickness") ||
        kernel.eval_graph().recompute(*consumer.value).status!=axiom::StatusCode::Ok ||
        kernel.eval_graph().is_invalid(*node.value).value!=std::optional<bool>{false} ||
        kernel.eval_graph().is_invalid(*consumer.value).value!=std::optional<bool>{false}) return false;
    const auto offset_node=kernel.eval_graph().register_node(axiom::NodeKind::Geometry,"body:"+std::to_string(offset.value->output.value));
    const auto offset_consumer=kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"offset_shell:output_consumer");
    if (!offset_node.value || !offset_consumer.value ||
        kernel.eval_graph().add_dependency(*offset_consumer.value,*offset_node.value).status!=axiom::StatusCode::Ok) return false;
    expected_bridge=bridge_counts();
    ++expected_bridge[0]; ++expected_bridge[2]; ++expected_bridge[3]; expected_bridge[4]+=2;
    const auto closed=kernel.modify().shell_body(offset.value->output,{},.25);
    if (!closed.value || bridge_counts()!=expected_bridge ||
        kernel.eval_graph().is_invalid(*offset_node.value).value!=std::optional<bool>{true} ||
        kernel.eval_graph().is_invalid(*offset_consumer.value).value!=std::optional<bool>{true} ||
        kernel.eval_graph().is_invalid(*node.value).value!=std::optional<bool>{false} ||
        kernel.eval_graph().is_invalid(*consumer.value).value!=std::optional<bool>{false} ||
        !rejected([&] { return kernel.modify().shell_body(offset.value->output,{},3); },kModShellFailure,"modify.shell.thickness") ||
        kernel.eval_graph().is_invalid(*offset_node.value).value!=std::optional<bool>{true} ||
        kernel.eval_graph().is_invalid(*offset_consumer.value).value!=std::optional<bool>{true} ||
        kernel.eval_graph().recompute(*offset_consumer.value).status!=axiom::StatusCode::Ok ||
        kernel.eval_graph().is_invalid(*offset_node.value).value!=std::optional<bool>{false} ||
        kernel.eval_graph().is_invalid(*offset_consumer.value).value!=std::optional<bool>{false}) return false;
    const auto output=closed.value->output;
    const auto output_faces=query.faces_of_body(output).value;
    const auto output_edges=query.edges_of_body(output).value;
    const auto output_vertices=query.vertices_of_body(output).value;
    const auto output_shells=query.shells_of_body(output).value;
    if (!output_faces || !output_edges || !output_vertices || !output_shells) return false;
    const auto output_mesh=kernel.convert().brep_to_mesh(output,{});
    const auto output_curve=query.curve_of_edge(output_edges->front());
    const auto output_surface=query.surface_of_face(output_faces->front());
    if (!output_mesh.value || !output_curve.value || !output_surface.value ||
        !kernel.curve_service().eval(*output_curve.value,0,1).value ||
        !kernel.surface_service().eval(*output_surface.value,0,0,1).value) return false;
    if (!rejected([&] { return kernel.modify().shell_body(*source.value,{},3); },kModShellFailure,"modify.shell.thickness") ||
        query.has_body(output).value!=std::optional<bool>{true} ||
        transaction.rollback().status!=axiom::StatusCode::Ok || !objects_before_success ||
        kernel.object_count_total().value!=std::optional<std::uint64_t>{*objects_before_success-1} ||
        kernel.has_body_id(output).value!=std::optional<bool>{false} ||
        kernel.has_body_id(offset.value->output).value!=std::optional<bool>{false}) return false;
    const auto runtime_after_rollback=kernel.runtime_store_counts().value;
    if (!runtime_before_success || !runtime_after_rollback ||
        runtime_before_success->mesh_records!=runtime_after_rollback->mesh_records ||
        runtime_before_success->tessellation_cache_entries!=runtime_after_rollback->tessellation_cache_entries ||
        runtime_before_success->face_tessellation_cache_entries!=runtime_after_rollback->face_tessellation_cache_entries ||
        runtime_before_success->curve_eval_cache_entries!=runtime_after_rollback->curve_eval_cache_entries ||
        runtime_before_success->surface_eval_cache_entries!=runtime_after_rollback->surface_eval_cache_entries ||
        kernel.convert().inspect_mesh(*output_mesh.value).value.has_value() ||
        kernel.has_curve_id(*output_curve.value).value!=std::optional<bool>{false} ||
        kernel.has_surface_id(*output_surface.value).value!=std::optional<bool>{false}) return false;
    for (const auto id : *output_faces) if (query.has_face(id).value!=std::optional<bool>{false}) return false;
    for (const auto id : *output_edges) if (query.has_edge(id).value!=std::optional<bool>{false}) return false;
    for (const auto id : *output_vertices) if (query.has_vertex(id).value!=std::optional<bool>{false}) return false;
    for (const auto id : *output_shells) if (query.has_shell(id).value!=std::optional<bool>{false}) return false;
    const auto warm=kernel.convert().brep_to_mesh(*source.value,{});
    if (warm.value!=mesh.value || kernel.topology().validate().validate_indices_consistency().status!=axiom::StatusCode::Ok ||
        query.faces_of_body(*source.value).value!=faces || query.edges_of_body(*source.value).value!=edges ||
        kernel.eval_graph().is_invalid(*node.value).value!=std::optional<bool>{false}) return false;
    const auto retry=kernel.modify().shell_body(*source.value,std::array{faces->front()},.25);
    return retry.value && kernel.validate().validate_all(retry.value->output,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok &&
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status==axiom::StatusCode::Ok;
}

// Deliberately bind a wrong PCurve without changing the box's 3D support.
// Support certification passes; source Strict must reject before allocation.
bool source_validation_failure() {
    axiom::Kernel kernel;
    const auto source=kernel.primitives().box({0,0,0},4,5,6);
    if (!source.value) return false;
    auto& query=kernel.topology().query();
    const auto edges=query.edges_of_body(*source.value);
    const auto faces=query.faces_of_body(*source.value).value;
    if (!edges.value || !faces) return false;
    const auto coedges=query.coedges_of_edge(edges.value->front());
    const auto pcurve=kernel.pcurves().make_polyline(std::array<axiom::Point2,2>{{{100,100},{101,100}}});
    if (!coedges.value || coedges.value->empty() || !pcurve.value) return false;
    auto transaction=kernel.topology().begin_transaction();
    if (transaction.set_coedge_pcurve(coedges.value->front(),*pcurve.value).status!=axiom::StatusCode::Ok) return false;
    const auto objects=kernel.object_count_total().value, next=kernel.next_object_id().value;
    const auto writes=transaction.write_operation_count().value;
    const auto before=kernel.runtime_store_counts().value;
    for (const bool shell : {false,true}) {
        const auto rejected=shell ? kernel.modify().shell_body(*source.value,{},.25) :
            kernel.modify().offset_body(*source.value,.25,{});
        const auto report=kernel.diagnostics().get(rejected.diagnostic_id);
        const auto after=kernel.runtime_store_counts().value;
        if (rejected.status!=axiom::StatusCode::InvalidTopology || rejected.value || !report.value || !before || !after ||
            kernel.object_count_total().value!=objects || kernel.next_object_id().value!=next ||
            transaction.write_operation_count().value!=writes || query.faces_of_body(*source.value).value!=faces ||
            before->mesh_records!=after->mesh_records || before->tessellation_cache_entries!=after->tessellation_cache_entries ||
            before->curve_eval_cache_entries!=after->curve_eval_cache_entries || before->surface_eval_cache_entries!=after->surface_eval_cache_entries ||
            std::none_of(report.value->issues.begin(),report.value->issues.end(),[&](const auto& issue) {
                return issue.code==(shell ? axiom::diag_codes::kModShellValidateFailed : axiom::diag_codes::kModOffsetValidateFailed) &&
                    issue.stage==(shell ? "modify.shell.source_validate" : "modify.offset.source_validate");
            })) return false;
    }
    if (transaction.rollback().status!=axiom::StatusCode::Ok ||
        kernel.validate().validate_all(*source.value,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok) return false;
    const auto retry=kernel.modify().offset_body(*source.value,.25,{});
    return retry.value && check_boundary(kernel,retry.value->output,{-0.25,-0.25,-0.25},{4.5,5.5,6.5});
}

}  // namespace

bool offset_shell_regression() {
    return success_references() && failures_and_rollback() && source_validation_failure();
}
