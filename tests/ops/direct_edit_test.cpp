#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

namespace {

std::array<double,3> coordinates(const axiom::Point3& point) { return {point.x,point.y,point.z}; }

axiom::FaceId side_face(axiom::Kernel& kernel, axiom::BodyId body, int side,
                        const std::array<double,3>& low, const std::array<double,3>& length) {
    const auto faces=kernel.topology().query().faces_of_body(body);
    if (!faces.value) return {};
    const int axis=side/2;
    const double coordinate=low[axis]+(side%2 ? length[axis] : 0);
    for (const auto face : *faces.value) {
        const auto bbox=kernel.topology().query().bbox_of_face(face);
        if (bbox.value && std::abs(coordinates(bbox.value->min)[axis]-coordinate)<1e-9 &&
            std::abs(coordinates(bbox.value->max)[axis]-coordinate)<1e-9) return face;
    }
    return {};
}

// Analytic rectangular-solid reference: actual corners, retrimmed planes,
// closure, centroid and density-one inertia, independently of body metadata.
bool direct_edit_boundary_reference(axiom::Kernel& kernel, axiom::BodyId body,
                                    const std::array<double,3>& low, const std::array<double,3>& length) {
    auto& query=kernel.topology().query();
    const auto fail=[&](int line) { std::cerr << "direct edit analytic reference line=" << line << '\n'; return false; };
    const double volume=length[0]*length[1]*length[2];
    const double area=2*(length[0]*length[1]+length[0]*length[2]+length[1]*length[2]);
    const double epsilon=1e-7*std::max({1.0,length[0],length[1],length[2]});
    const auto mass=kernel.query().mass_properties(body);
    const auto bbox=kernel.representation().bbox_of_body(body);
    const auto vertices=query.vertices_of_body(body);
    const auto faces=query.faces_of_body(body);
    const auto edges=query.edges_of_body(body);
    const auto shells=query.shells_of_body(body);
    if (!mass.value || !bbox.value || !vertices.value || !faces.value || !edges.value || !shells.value ||
        vertices.value->size()!=8 || faces.value->size()!=6 || edges.value->size()!=12 || shells.value->size()!=1 ||
        std::abs(mass.value->volume-volume)>epsilon || std::abs(mass.value->area-area)>epsilon ||
        kernel.validate().validate_all(body,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok ||
        kernel.topology().validate().validate_body_closedness(body).status!=axiom::StatusCode::Ok ||
        kernel.topology().validate().validate_body_sources(body).status!=axiom::StatusCode::Ok) return fail(__LINE__);
    for (int axis=0; axis<3; ++axis) {
        const double expected_inertia=volume*(length[(axis+1)%3]*length[(axis+1)%3]+
            length[(axis+2)%3]*length[(axis+2)%3])/12;
        if (std::abs(coordinates(mass.value->centroid)[axis]-low[axis]-length[axis]/2)>epsilon ||
            std::abs(coordinates(bbox.value->min)[axis]-low[axis])>epsilon ||
            std::abs(coordinates(bbox.value->max)[axis]-low[axis]-length[axis])>epsilon ||
            std::abs(mass.value->inertia[4*axis]-expected_inertia)>1e-7*std::max(1.0,expected_inertia)) return fail(__LINE__);
    }
    for (int row=0; row<3; ++row) for (int col=0; col<3; ++col)
        if (row!=col && std::abs(mass.value->inertia[3*row+col])>epsilon*volume) return fail(__LINE__);
    std::array<bool,8> corners {};
    for (const auto vertex : *vertices.value) {
        const auto point=query.point_of_vertex(vertex);
        if (!point.value) return fail(__LINE__);
        const auto p=coordinates(*point.value);
        int corner=0;
        for (int axis=0; axis<3; ++axis) {
            if (std::abs(p[axis]-low[axis]-length[axis])<epsilon) corner|=1<<axis;
            else if (std::abs(p[axis]-low[axis])>=epsilon) return fail(__LINE__);
        }
        if (corners[corner]) return fail(__LINE__);
        corners[corner]=true;
    }
    double face_area=0;
    for (int side=0; side<6; ++side) {
        const auto face=side_face(kernel,body,side,low,length);
        const auto surface=query.surface_of_face(face);
        const auto loops=query.loops_of_face(face);
        const auto area_result=query.planar_face_area(face);
        if (!face.value || !surface.value || !loops.value || loops.value->size()!=1 || !area_result.value) return fail(__LINE__);
        const auto evaluated=kernel.surface_service().eval(*surface.value,0,0,1);
        const auto loop_vertices=query.vertices_of_loop(loops.value->front());
        if (!evaluated.value || !loop_vertices.value || loop_vertices.value->size()!=4) return fail(__LINE__);
        const int axis=side/2;
        const auto normal=std::array{evaluated.value->normal.x,evaluated.value->normal.y,evaluated.value->normal.z};
        if (std::abs(coordinates(evaluated.value->point)[axis]-low[axis]-(side%2 ? length[axis] : 0))>epsilon ||
            normal[axis]*(side%2 ? 1 : -1)<0.999999 ||
            std::abs(*area_result.value-length[(axis+1)%3]*length[(axis+2)%3])>epsilon) return fail(__LINE__);
        face_area+=*area_result.value;
    }
    if (std::abs(face_area-area)>epsilon) return fail(__LINE__);
    for (const auto edge : *edges.value) {
        const auto coedges=query.coedges_of_edge(edge);
        const auto adjacent=query.faces_of_edge(edge);
        if (!coedges.value || coedges.value->size()!=2 || !adjacent.value || adjacent.value->size()!=2 ||
            (*adjacent.value)[0]==(*adjacent.value)[1]) return fail(__LINE__);
    }
    const axiom::Point3 center {low[0]+length[0]/2,low[1]+length[1]/2,low[2]+length[2]/2};
    const auto inside=query.locate_point(body,center);
    const auto outside=query.locate_point(body,{low[0]-1,center.y,center.z});
    return inside.value && inside.value->location==axiom::BodyPointLocation::Inside &&
        outside.value && outside.value->location==axiom::BodyPointLocation::Outside;
}

// Public OBJ vertices/triangles prove that the representation follows the
// edited boundary. Integrate triangles relative to the analytic center.
bool direct_edit_obj_reference(axiom::Kernel& kernel, axiom::BodyId body,
                              const std::array<double,3>& low, const std::array<double,3>& length) {
    const auto path=std::filesystem::temp_directory_path()/("axiom_direct_edit_"+std::to_string(body.value)+".obj");
    if (kernel.io().export_obj(body,path.string(),{}).status!=axiom::StatusCode::Ok) return false;
    std::ifstream input{path}; input.imbue(std::locale::classic());
    std::vector<std::array<double,3>> vertices;
    std::array<bool,8> corners {};
    double area=0, volume=0;
    bool valid=true;
    std::string line;
    while (valid && std::getline(input,line)) {
        std::istringstream record{line}; record.imbue(std::locale::classic());
        std::string kind; record>>kind;
        if (kind=="v") {
            std::array<double,3> point {};
            if (!(record>>point[0]>>point[1]>>point[2])) { valid=false; break; }
            int corner=0;
            for (int axis=0; axis<3; ++axis) {
                if (std::abs(point[axis]-low[axis]-length[axis])<1e-7) corner|=1<<axis;
                else if (std::abs(point[axis]-low[axis])>=1e-7) valid=false;
                point[axis]-=low[axis]+length[axis]/2;
            }
            corners[corner]=true; vertices.push_back(point);
        } else if (kind=="f") {
            std::array<std::size_t,3> ids {};
            if (!(record>>ids[0]>>ids[1]>>ids[2]) || std::any_of(ids.begin(),ids.end(),[&](auto id) {
                return id==0 || id>vertices.size();
            })) { valid=false; break; }
            const auto a=vertices[ids[0]-1], b=vertices[ids[1]-1], c=vertices[ids[2]-1];
            const std::array u {b[0]-a[0],b[1]-a[1],b[2]-a[2]}, v {c[0]-a[0],c[1]-a[1],c[2]-a[2]};
            const std::array cross {u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
            area+=std::hypot(cross[0],cross[1],cross[2])/2;
            volume+=(a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+a[2]*(b[0]*c[1]-b[1]*c[0]))/6;
        }
    }
    input.close(); std::filesystem::remove(path);
    return valid && std::all_of(corners.begin(),corners.end(),[](bool found) { return found; }) &&
        std::abs(area-2*(length[0]*length[1]+length[0]*length[2]+length[1]*length[2]))<1e-6 &&
        std::abs(volume-length[0]*length[1]*length[2])<1e-6;
}

bool direct_edit_success_references() {
    for (const double scale : {0.1,1.0,10.0}) for (int side=0; side<6; ++side) {
        axiom::Kernel kernel;
        const std::array low {-7*scale,11*scale,-3*scale}, length {4*scale,5*scale,6*scale};
        const auto source=kernel.primitives().box({low[0],low[1],low[2]},length[0],length[1],length[2]);
        if (!source.value) return false;
        auto& query=kernel.topology().query();
        const auto faces=query.faces_of_body(*source.value).value;
        const auto shells=query.shells_of_body(*source.value).value;
        const auto target=side_face(kernel,*source.value,side,low,length);
        if (!faces || !shells || !target.value) return false;
        for (const double distance : {-0.25*scale,0.5*scale}) for (const bool replacement : {false,true}) {
            const int axis=side/2;
            std::array edited_low=low, edited_length=length;
            edited_length[axis]+=distance;
            if (side%2==0) edited_low[axis]-=distance;
            axiom::Result<axiom::OpReport> result;
            if (replacement) {
                axiom::Point3 origin {edited_low[0],edited_low[1],edited_low[2]};
                if (side%2) {
                    if (axis==0) origin.x+=edited_length[0];
                    if (axis==1) origin.y+=edited_length[1];
                    if (axis==2) origin.z+=edited_length[2];
                }
                // Deliberately reverse the supplied plane normal; output
                // orientation must follow the source material boundary.
                axiom::Vec3 normal {};
                if (axis==0) normal.x=side%2 ? -1 : 1;
                if (axis==1) normal.y=side%2 ? -1 : 1;
                if (axis==2) normal.z=side%2 ? -1 : 1;
                const auto plane=kernel.surfaces().make_plane(origin,normal);
                if (!plane.value) return false;
                result=kernel.modify().replace_face(*source.value,target,*plane.value);
            } else result=kernel.modify().move_face(*source.value,target,distance);
            const auto report=kernel.diagnostics().get(result.diagnostic_id);
            if (result.status!=axiom::StatusCode::Ok || !result.value || !report.value ||
                std::none_of(report.value->issues.begin(),report.value->issues.end(),[&](const auto& issue) {
                    return issue.code==axiom::diag_codes::kModCompleted &&
                        issue.stage==(replacement ? "modify.replace_face.complete" : "modify.move_face.complete");
                }) || !direct_edit_boundary_reference(kernel,result.value->output,edited_low,edited_length) ||
                (scale==1 && !direct_edit_obj_reference(kernel,result.value->output,edited_low,edited_length))) return false;
            auto expected_faces=*faces; std::sort(expected_faces.begin(),expected_faces.end(),[](auto a,auto b) { return a.value<b.value; });
            auto source_faces=query.source_faces_of_body(result.value->output).value;
            if (!source_faces) return false;
            std::sort(source_faces->begin(),source_faces->end(),[](auto a,auto b) { return a.value<b.value; });
            if (*source_faces!=expected_faces || query.source_bodies_of_body(result.value->output).value!=std::optional{std::vector{*source.value}} ||
                query.source_shells_of_body(result.value->output).value!=shells) return false;
            for (int mapped_side=0; mapped_side<6; ++mapped_side) {
                const auto output_face=side_face(kernel,result.value->output,mapped_side,edited_low,edited_length);
                const auto input_face=side_face(kernel,*source.value,mapped_side,low,length);
                if (query.source_faces_of_face(output_face).value!=std::optional{std::vector{input_face}}) return false;
            }
        }
        if (query.faces_of_body(*source.value).value!=faces || !direct_edit_boundary_reference(kernel,*source.value,low,length)) return false;
    }
    // Mechanical stock: top 6 -> 7; then right 4 -> 4.5 by plane replacement.
    // Reference 4.5*5*7=157.5, A=178, center=(2.25,2.5,3.5).
    axiom::Kernel kernel;
    const auto stock=kernel.primitives().box({0,0,0},4,5,6);
    if (!stock.value) return false;
    const auto moved=kernel.modify().move_face(*stock.value,side_face(kernel,*stock.value,5,{0,0,0},{4,5,6}),1);
    const auto plane=kernel.surfaces().make_plane({4.5,0,0},{1,0,0});
    if (!moved.value || !plane.value) return false;
    const auto edited=kernel.modify().replace_face(moved.value->output,side_face(kernel,moved.value->output,1,{0,0,0},{4,5,7}),*plane.value);
    return edited.value && direct_edit_boundary_reference(kernel,edited.value->output,{0,0,0},{4.5,5,7}) &&
        direct_edit_obj_reference(kernel,edited.value->output,{0,0,0},{4.5,5,7}) &&
        kernel.topology().query().source_bodies_of_body(edited.value->output).value==std::optional{std::vector{moved.value->output}};
}

bool direct_edit_failure_isolation() {
    axiom::Kernel kernel;
    auto& query=kernel.topology().query();
    const auto source=kernel.primitives().box({0,0,0},4,5,6);
    const auto foreign=kernel.primitives().box({20,0,0},4,5,6);
    const auto sphere=kernel.primitives().sphere({40,0,0},2);
    const auto huge=kernel.primitives().box({1e16,1e16,1e16},16,16,16);
    const auto rotated=kernel.surfaces().make_plane({0,0,7},{1,0,1});
    const auto tilted=kernel.surfaces().make_plane({1e12,0,7},{1e-11,0,1});
    const auto parallel=kernel.surfaces().make_plane({0,0,7},{0,0,1});
    const auto unchanged=kernel.surfaces().make_plane({0,0,6},{0,0,-1});
    const auto collapsed=kernel.surfaces().make_plane({0,0,0},{0,0,1});
    const auto cylinder=kernel.surfaces().make_cylinder({0,0,0},{0,0,1},2);
    if (!source.value || !foreign.value || !sphere.value || !huge.value || !rotated.value || !tilted.value ||
        !parallel.value || !unchanged.value || !collapsed.value || !cylinder.value) return false;
    const auto faces=query.faces_of_body(*source.value).value, foreign_faces=query.faces_of_body(*foreign.value).value;
    const auto curved_faces=query.faces_of_body(*sphere.value).value;
    const auto edges=query.edges_of_body(*source.value).value;
    const auto mesh=kernel.convert().brep_to_mesh(*source.value,{});
    if (!faces || !foreign_faces || !curved_faces || curved_faces->empty() || !edges || !mesh.value) return false;
    const auto top=side_face(kernel,*source.value,5,{0,0,0},{4,5,6});
    const auto warm_curve=query.curve_of_edge(edges->front());
    const auto warm_surface=query.surface_of_face(top);
    if (!warm_curve.value || !warm_surface.value || !kernel.curve_service().eval(*warm_curve.value,0,1).value ||
        !kernel.surface_service().eval(*warm_surface.value,0,0,1).value) return false;
    const auto node=kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"body:"+std::to_string(source.value->value));
    const auto consumer=kernel.eval_graph().register_node(axiom::NodeKind::Analysis,"direct_edit:consumer");
    if (!node.value || !consumer.value || kernel.eval_graph().add_dependency(*consumer.value,*node.value).status!=axiom::StatusCode::Ok) return false;
    auto transaction=kernel.topology().begin_transaction();
    const auto sentinel=transaction.create_vertex({80,81,82});
    if (!sentinel.value) return false;
    const auto rejected=[&](const auto& operation,std::string_view code,std::string_view stage) {
        const auto objects=kernel.object_count_total().value, geometry=kernel.geometry_count().value;
        const auto topology=kernel.topology_count().value, bodies=kernel.body_count().value;
        const auto next=kernel.next_object_id().value, version=kernel.topology_version_next().value;
        const auto writes=transaction.write_operation_count().value;
        const auto before=kernel.runtime_store_counts().value;
        const auto metrics=kernel.eval_graph_metrics().value;
        const auto source_bodies=query.source_bodies_of_body(*source.value).value;
        const auto source_faces=query.source_faces_of_body(*source.value).value;
        const auto source_shells=query.source_shells_of_body(*source.value).value;
        const auto result=operation();
        const auto report=kernel.diagnostics().get(result.diagnostic_id);
        const auto after=kernel.runtime_store_counts().value;
        const auto after_metrics=kernel.eval_graph_metrics().value;
        return result.status!=axiom::StatusCode::Ok && !result.value && report.value && before && after && metrics && after_metrics &&
            std::any_of(report.value->issues.begin(),report.value->issues.end(),[&](const auto& issue) { return issue.code==code && issue.stage==stage; }) &&
            kernel.object_count_total().value==objects && kernel.geometry_count().value==geometry && kernel.next_object_id().value==next &&
            kernel.topology_count().value==topology && kernel.body_count().value==bodies &&
            kernel.topology_version_next().value==version && transaction.write_operation_count().value==writes &&
            query.faces_of_body(*source.value).value==faces && query.edges_of_body(*source.value).value==edges &&
            query.source_bodies_of_body(*source.value).value==source_bodies && query.source_faces_of_body(*source.value).value==source_faces &&
            query.source_shells_of_body(*source.value).value==source_shells &&
            before->mesh_records==after->mesh_records && before->tessellation_cache_entries==after->tessellation_cache_entries &&
            before->face_tessellation_cache_entries==after->face_tessellation_cache_entries &&
            before->curve_eval_cache_entries==after->curve_eval_cache_entries && before->surface_eval_cache_entries==after->surface_eval_cache_entries &&
            before->tessellation_metrics.body_cache_hits==after->tessellation_metrics.body_cache_hits &&
            before->tessellation_metrics.body_cache_misses==after->tessellation_metrics.body_cache_misses &&
            metrics->invalidation_bridge.for_body_entries==after_metrics->invalidation_bridge.for_body_entries &&
            metrics->invalidation_bridge.downstream_invalidation_steps==after_metrics->invalidation_bridge.downstream_invalidation_steps &&
            kernel.eval_graph().is_invalid(*node.value).value==std::optional<bool>{false} &&
            kernel.eval_graph().is_invalid(*consumer.value).value==std::optional<bool>{false} &&
            query.has_vertex(*sentinel.value).value==std::optional<bool>{true};
    };
    using namespace axiom::diag_codes;
    for (const double invalid : {0.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
        if (!rejected([&] { return kernel.modify().move_face(*source.value,top,invalid); },kModMoveFaceInvalid,"modify.move_face.input_gate")) return false;
    for (const double degenerate : {1e-8,-6.0,-5.99999975,std::numeric_limits<double>::max()})
        if (!rejected([&] { return kernel.modify().move_face(*source.value,top,degenerate); },kModDegenerateGeometry,"modify.move_face.geometry_gate")) return false;
    if (!rejected([&] { return kernel.modify().move_face({},top,.5); },kModMoveFaceInvalid,"modify.move_face.input_gate") ||
        !rejected([&] { return kernel.modify().move_face(*source.value,foreign_faces->front(),.5); },kModMoveFaceInvalid,"modify.move_face.input_gate") ||
        !rejected([&] { return kernel.modify().move_face(*sphere.value,curved_faces->front(),.5); },kModUnsupportedGeometry,"modify.move_face.support_gate") ||
        !rejected([&] { return kernel.modify().move_face(*huge.value,side_face(kernel,*huge.value,5,{1e16,1e16,1e16},{16,16,16}),.1); },kModDegenerateGeometry,"modify.move_face.geometry_gate") ||
        !rejected([&] { return kernel.modify().replace_face(*source.value,foreign_faces->front(),*parallel.value); },kModReplaceFaceIncompatible,"modify.replace_face.input_gate") ||
        !rejected([&] { return kernel.modify().replace_face(*source.value,top,*rotated.value); },kModReplaceFaceIncompatible,"modify.replace_face.support_gate") ||
        !rejected([&] { return kernel.modify().replace_face(*source.value,top,*tilted.value); },kModReplaceFaceIncompatible,"modify.replace_face.support_gate") ||
        !rejected([&] { return kernel.modify().replace_face(*source.value,top,*unchanged.value); },kModDegenerateGeometry,"modify.replace_face.geometry_gate") ||
        !rejected([&] { return kernel.modify().replace_face(*source.value,top,*collapsed.value); },kModDegenerateGeometry,"modify.replace_face.geometry_gate") ||
        !rejected([&] { return kernel.modify().replace_face(*source.value,top,*cylinder.value); },kModReplaceFaceIncompatible,"modify.replace_face.support_gate") ||
        !rejected([&] { return kernel.modify().replace_face(*source.value,top,{}); },kModReplaceFaceIncompatible,"modify.replace_face.input_gate") ||
        !rejected([&] { return kernel.modify().delete_face_and_heal(*source.value,top); },kModUnsupportedGeometry,"modify.delete_face.support_gate") ||
        !rejected([&] { return kernel.modify().delete_face_and_heal(*source.value,foreign_faces->front()); },kModDeleteFaceHealFailure,"modify.delete_face.input_gate")) return false;
    const auto warm=kernel.convert().brep_to_mesh(*source.value,{});
    return warm.value==mesh.value && transaction.rollback().status==axiom::StatusCode::Ok &&
        direct_edit_boundary_reference(kernel,*source.value,{0,0,0},{4,5,6});
}

}  // namespace

bool direct_edit_regression() {
    return direct_edit_success_references() && direct_edit_failure_isolation();
}
