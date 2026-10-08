#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

namespace {

std::array<double,3> xyz(const axiom::Point3& point) { return {point.x,point.y,point.z}; }

axiom::FaceId plane_face(axiom::Kernel& kernel, axiom::BodyId body, int axis, double coordinate) {
    const auto faces=kernel.topology().query().faces_of_body(body);
    if (!faces.value) return {};
    for (const auto face : *faces.value) {
        const auto bounds=kernel.topology().query().bbox_of_face(face);
        if (bounds.value && std::abs(xyz(bounds.value->min)[axis]-coordinate)<1e-9 &&
            std::abs(xyz(bounds.value->max)[axis]-coordinate)<1e-9) return face;
    }
    return {};
}

// Fixed enclosure references below use an actual rectangular stock, never
// body creation parameters as the oracle for an edited or shelled result.
bool box_reference(axiom::Kernel& kernel, axiom::BodyId body,
                   const std::array<double,3>& low, const std::array<double,3>& length) {
    auto& query=kernel.topology().query();
    const auto mass=kernel.query().mass_properties(body);
    const auto bounds=kernel.representation().bbox_of_body(body);
    const auto vertices=query.vertices_of_body(body);
    const auto edges=query.edges_of_body(body);
    const auto faces=query.faces_of_body(body);
    const auto volume=length[0]*length[1]*length[2];
    const auto area=2*(length[0]*length[1]+length[0]*length[2]+length[1]*length[2]);
    if (!mass.value || !bounds.value || !vertices.value || !edges.value || !faces.value ||
        vertices.value->size()!=8 || edges.value->size()!=12 || faces.value->size()!=6 ||
        std::abs(mass.value->volume-volume)>1e-7 || std::abs(mass.value->area-area)>1e-7 ||
        kernel.validate().validate_all(body,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok ||
        kernel.topology().validate().validate_body_sources(body).status!=axiom::StatusCode::Ok ||
        kernel.representation().kind_of_body(body).value!=std::optional{axiom::RepKind::ExactBRep}) return false;
    for (int a=0; a<3; ++a)
        if (std::abs(xyz(bounds.value->min)[a]-low[a])>1e-8 ||
            std::abs(xyz(bounds.value->max)[a]-low[a]-length[a])>1e-8 ||
            std::abs(xyz(mass.value->centroid)[a]-low[a]-length[a]/2)>1e-8 ||
            std::abs(mass.value->inertia[4*a]-volume*(length[(a+1)%3]*length[(a+1)%3]+
                length[(a+2)%3]*length[(a+2)%3])/12)>1e-6) return false;
    std::array<bool,8> found {};
    for (const auto vertex : *vertices.value) {
        const auto point=query.point_of_vertex(vertex);
        if (!point.value) return false;
        int corner=0;
        for (int a=0; a<3; ++a) {
            if (std::abs(xyz(*point.value)[a]-low[a]-length[a])<1e-8) corner|=1<<a;
            else if (std::abs(xyz(*point.value)[a]-low[a])>=1e-8) return false;
        }
        if (found[corner]) return false;
        found[corner]=true;
    }
    const axiom::Point3 center {low[0]+length[0]/2,low[1]+length[1]/2,low[2]+length[2]/2};
    const auto inside=query.locate_point(body,center);
    const auto outside=query.locate_point(body,{low[0]-1,center.y,center.z});
    return inside.value && inside.value->location==axiom::BodyPointLocation::Inside &&
        outside.value && outside.value->location==axiom::BodyPointLocation::Outside;
}

// Independently integrate the public OBJ triangles. All enclosure branches
// are in the same frame; translating by the known stock center reduces
// cancellation and keeps signed-volume winding errors observable.
bool obj_reference(axiom::Kernel& kernel, axiom::BodyId body, double expected_volume,
                   double expected_area, double volume_budget=1e-6, double area_budget=1e-6) {
    const auto path=std::filesystem::temp_directory_path()/("axiom_stage6_exit_"+std::to_string(body.value)+".obj");
    if (kernel.io().export_obj(body,path.string(),{}).status!=axiom::StatusCode::Ok) return false;
    std::ifstream input{path}; input.imbue(std::locale::classic());
    std::vector<std::array<double,3>> points;
    double volume=0, area=0;
    std::size_t triangle_count=0;
    bool valid=input.good();
    std::string line;
    while (valid && std::getline(input,line)) {
        std::istringstream record{line}; record.imbue(std::locale::classic());
        std::string kind; record>>kind;
        if (kind=="v") {
            std::array<double,3> point {};
            if (!(record>>point[0]>>point[1]>>point[2]) ||
                std::any_of(point.begin(),point.end(),[](double v) { return !std::isfinite(v); })) { valid=false; break; }
            point[0]-=4.5; point[1]-=2.5; point[2]-=2;
            points.push_back(point);
        } else if (kind=="f") {
            std::array<std::size_t,3> ids {};
            if (!(record>>ids[0]>>ids[1]>>ids[2]) || std::any_of(ids.begin(),ids.end(),[&](auto id) {
                return id==0 || id>points.size();
            })) { valid=false; break; }
            const auto a=points[ids[0]-1], b=points[ids[1]-1], c=points[ids[2]-1];
            const std::array u {b[0]-a[0],b[1]-a[1],b[2]-a[2]}, v {c[0]-a[0],c[1]-a[1],c[2]-a[2]};
            const std::array cross {u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
            const double triangle_area=std::hypot(cross[0],cross[1],cross[2])/2;
            if (!(triangle_area>0)) { valid=false; break; }
            area+=triangle_area;
            volume+=(a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+a[2]*(b[0]*c[1]-b[1]*c[0]))/6;
            ++triangle_count;
        }
    }
    input.close(); std::filesystem::remove(path);
    if (!valid || triangle_count==0 || std::abs(volume-expected_volume)>volume_budget ||
        std::abs(area-expected_area)>area_budget) {
        std::cerr << "Stage 6 OBJ reference body=" << body.value << " V=" << volume << '/' << expected_volume
                  << " A=" << area << '/' << expected_area << '\n';
        return false;
    }
    return true;
}

bool fillet_reference(axiom::Kernel& kernel, axiom::BodyId body) {
    auto& query=kernel.topology().query();
    const double radius=.5, pi=std::acos(-1.0);
    const auto edges=query.edges_of_body(body);
    const auto faces=query.faces_of_body(body);
    const auto vertices=query.vertices_of_body(body);
    if (!edges.value || !faces.value || !vertices.value || edges.value->size()!=15 || faces.value->size()!=7 ||
        vertices.value->size()!=10 ||
        kernel.validate().validate_all(body,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok) return false;
    std::size_t arcs=0, cylinders=0;
    for (const auto edge : *edges.value) {
        const auto curve=query.curve_of_edge(edge);
        const auto interval=query.edge_curve_interval(edge);
        if (!curve.value || !interval.value || query.coedge_count_of_edge(edge).value!=std::optional<std::uint64_t>{2}) return false;
        if (!*interval.value) continue;
        const auto range=**interval.value;
        const auto midpoint=kernel.curve_service().eval(*curve.value,(range.start_parameter+range.end_parameter)/2,2);
        if (!midpoint.value) return false;
        if (std::abs(midpoint.value->curvature)<1e-12) continue;
        ++arcs;
        const auto length=query.edge_length(edge);
        const auto p=midpoint.value->point;
        const auto tangent=midpoint.value->tangent;
        if (!length.value || std::abs(*length.value-pi*radius/2)>1e-8 ||
            std::abs(midpoint.value->curvature-1/radius)>1e-8 ||
            std::abs(p.x+radius/std::sqrt(2.0))>1e-8 || std::abs(p.y+radius/std::sqrt(2.0))>1e-8 ||
            std::abs(std::hypot(p.x,p.y)-radius)>1e-8 ||
            std::abs(tangent.x*p.x+tangent.y*p.y)>1e-8 || std::abs(tangent.z)>1e-8 ||
            (std::abs(p.z+.5)>1e-8 && std::abs(p.z-4.5)>1e-8)) return false;
    }
    for (const auto face : *faces.value) {
        const auto surface=query.surface_of_face(face);
        if (!surface.value) return false;
        const auto evaluated=kernel.surface_service().eval(*surface.value,0,0,2);
        if (!evaluated.value) return false;
        const double maximum=std::max(std::abs(evaluated.value->k1),std::abs(evaluated.value->k2));
        if (maximum<1e-12) continue;
        ++cylinders;
        if (std::abs(maximum-1/radius)>1e-8 ||
            std::min(std::abs(evaluated.value->k1),std::abs(evaluated.value->k2))>1e-8) return false;
        const auto projected=kernel.surface_service().closest_point(*surface.value,{-radius/std::sqrt(2.0),-radius/std::sqrt(2.0),2});
        if (!projected.value || std::abs(projected.value->x+radius/std::sqrt(2.0))>1e-8 ||
            std::abs(projected.value->y+radius/std::sqrt(2.0))>1e-8 || std::abs(projected.value->z-2)>1e-8) return false;
    }
    // Curved mass queries remain unsupported. The independent analytic area
    // and volume below certify only this quarter-cylinder mesh, not general
    // curved mass integration or an external industrial-kernel comparison.
    const auto mass=kernel.query().mass_properties(body);
    const auto report=kernel.diagnostics().get(mass.diagnostic_id);
    if (arcs!=2 || cylinders!=1 || mass.status!=axiom::StatusCode::NotImplemented || mass.value || !report.value ||
        std::none_of(report.value->issues.begin(),report.value->issues.end(),[](const auto& issue) {
            return issue.code==axiom::diag_codes::kCoreOperationUnsupported && issue.stage=="query.mass_properties.support_gate";
        })) return false;
    const double section=60-(1-pi/4)*radius*radius;
    const double perimeter=32+(pi/2-2)*radius;
    // OBJ's default five-degree angular budget gives delta<=5*pi/180.
    // theta-sin(theta)<=theta^3/6 and theta-2*sin(theta/2)<=theta^3/24
    // bound the quarter-sector and strip losses independently of sampling N.
    const double delta=5*pi/180;
    const double section_loss=radius*radius*pi*delta*delta/24;
    const double arc_loss=radius*pi*delta*delta/48;
    return obj_reference(kernel,body,5*section,2*section+5*perimeter,
                         5*section_loss+1e-6,2*section_loss+5*arc_loss+1e-6);
}

bool stage6_mechanical_fixture() {
    axiom::Kernel kernel;
    auto& query=kernel.topology().query();
    auto& eval=kernel.eval_graph();
    const auto fail=[&](int line) { std::cerr << "Stage 6 fixed mechanical fixture line=" << line << '\n'; return false; };
    const auto completed=[&](const axiom::Result<axiom::OpReport>& result,std::string_view code,std::string_view stage) {
        const auto diagnostic=kernel.diagnostics().get(result.diagnostic_id);
        return result.status==axiom::StatusCode::Ok && result.value && result.value->warnings.empty() && diagnostic.value &&
            std::any_of(diagnostic.value->issues.begin(),diagnostic.value->issues.end(),[&](const auto& issue) {
                return issue.code==code && issue.stage==stage && issue.severity==axiom::IssueSeverity::Info;
            });
    };
    const auto stock=kernel.primitives().box({0,0,0},8,5,3);
    const auto replacement=kernel.surfaces().make_plane({0,0,4},{0,0,-1});
    if (!stock.value || !replacement.value || !box_reference(kernel,*stock.value,{0,0,0},{8,5,3})) return fail(__LINE__);
    const auto source_faces=query.faces_of_body(*stock.value).value;
    const auto source_edges=query.edges_of_body(*stock.value).value;
    const auto source_sources=query.source_bodies_of_body(*stock.value).value;
    const auto source_mesh=kernel.convert().brep_to_mesh(*stock.value,{});
    if (!source_faces || !source_edges || !source_sources || !source_mesh.value) return fail(__LINE__);
    const auto source_curve=query.curve_of_edge(source_edges->front());
    const auto source_surface=query.surface_of_face(source_faces->front());
    if (!source_curve.value || !source_surface.value || !kernel.curve_service().eval(*source_curve.value,0,1).value ||
        !kernel.surface_service().eval(*source_surface.value,0,0,1).value) return fail(__LINE__);
    std::vector<axiom::BodyId> outputs;
    std::vector<axiom::MeshId> output_meshes;
    std::vector<std::array<axiom::NodeId,2>> bindings;
    const auto bind=[&](axiom::BodyId body) {
        const auto node=eval.register_node(axiom::NodeKind::Geometry,"body:"+std::to_string(body.value));
        const auto consumer=eval.register_node(axiom::NodeKind::Analysis,"stage6:fixture:"+std::to_string(body.value));
        if (!node.value || !consumer.value || eval.add_dependency(*consumer.value,*node.value).status!=axiom::StatusCode::Ok) return false;
        bindings.push_back({*node.value,*consumer.value});
        return true;
    };
    const auto invalid=[&](std::size_t index,bool expected) {
        return eval.is_invalid(bindings[index][0]).value==std::optional{expected} &&
            eval.is_invalid(bindings[index][1]).value==std::optional{expected};
    };
    if (!bind(*stock.value) || !invalid(0,false)) return fail(__LINE__);
    const auto baseline=kernel.runtime_store_counts().value;
    const auto baseline_objects=kernel.object_count_total().value, baseline_geometry=kernel.geometry_count().value;
    auto transaction=kernel.topology().begin_transaction();
    const auto sentinel=transaction.create_vertex({30,31,32});
    if (!baseline || !sentinel.value) return fail(__LINE__);

    // 120/158 -> 135/174 -> 180/202 -> 300/280 (volume/surface area).
    const auto moved=kernel.modify().move_face(*stock.value,plane_face(kernel,*stock.value,0,8),1);
    if (!completed(moved,axiom::diag_codes::kModCompleted,"modify.move_face.complete") || !invalid(0,true) || !box_reference(kernel,moved.value->output,{0,0,0},{9,5,3}) ||
        eval.recompute(bindings[0][1]).status!=axiom::StatusCode::Ok || !bind(moved.value->output)) return fail(__LINE__);
    outputs.push_back(moved.value->output);
    const auto replaced=kernel.modify().replace_face(moved.value->output,plane_face(kernel,moved.value->output,2,3),*replacement.value);
    if (!completed(replaced,axiom::diag_codes::kModCompleted,"modify.replace_face.complete") || !invalid(1,true) || !invalid(0,false) ||
        !box_reference(kernel,replaced.value->output,{0,0,0},{9,5,4}) ||
        eval.recompute(bindings[1][1]).status!=axiom::StatusCode::Ok || !bind(replaced.value->output)) return fail(__LINE__);
    outputs.push_back(replaced.value->output);
    const auto offset=kernel.modify().offset_body(replaced.value->output,.5,{});
    if (!completed(offset,axiom::diag_codes::kModCompleted,"modify.offset.complete") || !invalid(2,true) || !box_reference(kernel,offset.value->output,{-.5,-.5,-.5},{10,6,5}) ||
        eval.recompute(bindings[2][1]).status!=axiom::StatusCode::Ok || !bind(offset.value->output) ||
        !obj_reference(kernel,offset.value->output,300,280)) return fail(__LINE__);
    outputs.push_back(offset.value->output);
    const auto base=offset.value->output;
    const auto base_faces=query.faces_of_body(base).value;
    const auto base_edges=query.edges_of_body(base).value;
    const auto base_source_bodies=query.source_bodies_of_body(base).value;
    const auto base_source_faces=query.source_faces_of_body(base).value;
    if (!base_faces || !base_edges || !base_source_bodies || !base_source_faces) return fail(__LINE__);
    axiom::EdgeId selected {};
    for (const auto edge : *base_edges) {
        const auto endpoints=query.vertices_of_edge(edge);
        if (!endpoints.value) return fail(__LINE__);
        const auto a=query.point_of_vertex((*endpoints.value)[0]), b=query.point_of_vertex((*endpoints.value)[1]);
        if (a.value && b.value && std::abs(a.value->x+.5)<1e-8 && std::abs(b.value->x+.5)<1e-8 &&
            std::abs(a.value->y+.5)<1e-8 && std::abs(b.value->y+.5)<1e-8 && std::abs(a.value->z-b.value->z)>4.9) selected=edge;
    }
    if (!selected.value) return fail(__LINE__);

    // Advanced-feature endpoints branch from the same certified stock.
    // Their outputs are deliberately not chained into another box-only edit.
    const auto chamfer=kernel.blends().chamfer_edges(base,std::array{selected},.5);
    const double chamfer_volume=(60-.5*.5/2)*5;
    const double chamfer_area=2*(60-.5*.5/2)+(32+(std::sqrt(2.0)-2)*.5)*5;
    const auto chamfer_mass=chamfer.value ? kernel.query().mass_properties(chamfer.value->output) : axiom::Result<axiom::MassProperties>{};
    if (!completed(chamfer,axiom::diag_codes::kBlendCompleted,"blend.chamfer.complete") || !chamfer_mass.value || std::abs(chamfer_mass.value->volume-chamfer_volume)>1e-7 ||
        std::abs(chamfer_mass.value->area-chamfer_area)>1e-7 || !invalid(3,false) ||
        kernel.validate().validate_all(chamfer.value->output,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok ||
        !obj_reference(kernel,chamfer.value->output,chamfer_volume,chamfer_area) || !bind(chamfer.value->output)) return fail(__LINE__);
    outputs.push_back(chamfer.value->output);
    const auto fillet=kernel.blends().fillet_edges(base,std::array{selected},.5);
    if (!completed(fillet,axiom::diag_codes::kBlendCompleted,"blend.fillet.complete") || !invalid(3,false) || !fillet_reference(kernel,fillet.value->output) || !bind(fillet.value->output)) return fail(__LINE__);
    outputs.push_back(fillet.value->output);
    for (const bool open : {false,true}) {
        const auto top=plane_face(kernel,base,2,4.5);
        const std::vector<axiom::FaceId> removed=open ? std::vector{top} : std::vector<axiom::FaceId>{};
        const auto shell=kernel.modify().shell_body(base,removed,.5);
        const double void_height=open ? 4.5 : 4;
        const double void_volume=9*5*void_height, expected_volume=300-void_volume;
        const double expected_area=280+2*(45+14*void_height)-(open ? 90 : 0);
        const double expected_z=(300*2-void_volume*(void_height/2))/expected_volume;
        const auto mass=shell.value ? kernel.query().mass_properties(shell.value->output) : axiom::Result<axiom::MassProperties>{};
        if (!completed(shell,axiom::diag_codes::kModCompleted,"modify.shell.complete") || !mass.value || !invalid(3,true) ||
            std::abs(mass.value->volume-expected_volume)>1e-7 || std::abs(mass.value->area-expected_area)>1e-7 ||
            std::abs(mass.value->centroid.x-4.5)>1e-7 || std::abs(mass.value->centroid.y-2.5)>1e-7 ||
            std::abs(mass.value->centroid.z-expected_z)>1e-7 ||
            kernel.validate().validate_all(shell.value->output,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok ||
            !obj_reference(kernel,shell.value->output,expected_volume,expected_area) ||
            eval.recompute(bindings[3][1]).status!=axiom::StatusCode::Ok || !bind(shell.value->output)) return fail(__LINE__);
        const auto void_point=query.locate_point(shell.value->output,{4.5,2.5,2});
        const auto wall_point=query.locate_point(shell.value->output,{-.25,2.5,2});
        if (!void_point.value || void_point.value->location!=axiom::BodyPointLocation::Outside ||
            !wall_point.value || wall_point.value->location!=axiom::BodyPointLocation::Inside) return fail(__LINE__);
        outputs.push_back(shell.value->output);
    }
    for (std::size_t i=0; i<outputs.size(); ++i) {
        const auto body=outputs[i];
        const auto expected_source=i==0 ? *stock.value : i<3 ? outputs[i-1] : base;
        if (query.source_bodies_of_body(body).value!=std::optional{std::vector{expected_source}} ||
            kernel.topology().validate().validate_body_sources(body).status!=axiom::StatusCode::Ok ||
            kernel.representation().kind_of_body(body).value!=std::optional{axiom::RepKind::ExactBRep}) return fail(__LINE__);
        const auto mesh=kernel.convert().brep_to_mesh(body,{});
        const auto inspection=mesh.value ? kernel.convert().inspect_mesh(*mesh.value) : axiom::Result<axiom::MeshInspectionReport>{};
        if (!mesh.value || !inspection.value || inspection.value->triangle_count==0 ||
            inspection.value->has_out_of_range_indices || inspection.value->has_degenerate_triangles ||
            inspection.value->mesh_label!="mesh_from_brep_owned_faces" ||
            inspection.value->tessellation_strategy!="owned_topo_welded" ||
            inspection.value->connected_components!=(i==5 ? 2u : 1u)) return fail(__LINE__);
        output_meshes.push_back(*mesh.value);
    }

    // Invalid continuation and degeneration must preserve every earlier
    // result, the active transaction's sentinel, Eval and warm mesh caches.
    const auto rejected=[&](const auto& operation,axiom::StatusCode status,std::string_view code,std::string_view stage) {
        const auto objects=kernel.object_count_total().value, geometry=kernel.geometry_count().value;
        const auto next=kernel.next_object_id().value, version=kernel.topology_version_next().value;
        const auto writes=transaction.write_operation_count().value;
        const auto before=kernel.runtime_store_counts().value;
        const auto metrics=kernel.eval_graph_metrics().value;
        const auto recomputes=eval.total_recompute_count().value;
        const auto result=operation();
        const auto report=kernel.diagnostics().get(result.diagnostic_id);
        const auto after=kernel.runtime_store_counts().value;
        const auto after_metrics=kernel.eval_graph_metrics().value;
        if (result.status!=status || result.value || !report.value || !before || !after || !metrics || !after_metrics ||
            std::none_of(report.value->issues.begin(),report.value->issues.end(),[&](const auto& issue) {
                return issue.code==code && issue.stage==stage && issue.severity==axiom::IssueSeverity::Error;
            }) || kernel.object_count_total().value!=objects || kernel.geometry_count().value!=geometry ||
            kernel.next_object_id().value!=next || kernel.topology_version_next().value!=version ||
            transaction.write_operation_count().value!=writes || eval.total_recompute_count().value!=recomputes ||
            before->mesh_records!=after->mesh_records || before->tessellation_cache_entries!=after->tessellation_cache_entries ||
            before->face_tessellation_cache_entries!=after->face_tessellation_cache_entries ||
            before->curve_eval_cache_entries!=after->curve_eval_cache_entries || before->surface_eval_cache_entries!=after->surface_eval_cache_entries ||
            before->eval_node_records!=after->eval_node_records ||
            before->tessellation_metrics.body_cache_hits!=after->tessellation_metrics.body_cache_hits ||
            before->tessellation_metrics.body_cache_misses!=after->tessellation_metrics.body_cache_misses ||
            metrics->invalidation_bridge.for_body_entries!=after_metrics->invalidation_bridge.for_body_entries ||
            metrics->invalidation_bridge.downstream_invalidation_steps!=after_metrics->invalidation_bridge.downstream_invalidation_steps ||
            query.faces_of_body(base).value!=base_faces || query.edges_of_body(base).value!=base_edges ||
            query.source_bodies_of_body(base).value!=base_source_bodies || query.source_faces_of_body(base).value!=base_source_faces ||
            query.faces_of_body(*stock.value).value!=source_faces || query.edges_of_body(*stock.value).value!=source_edges ||
            query.source_bodies_of_body(*stock.value).value!=source_sources ||
            query.has_vertex(*sentinel.value).value!=std::optional{true}) return false;
        for (std::size_t i=0; i<bindings.size(); ++i) if (!invalid(i,false)) return false;
        for (const auto body : outputs) if (query.has_body(body).value!=std::optional{true}) return false;
        return true;
    };
    using namespace axiom::diag_codes;
    for (const auto body : {chamfer.value->output,fillet.value->output,outputs.back()}) {
        const auto faces=query.faces_of_body(body).value;
        const auto edges=query.edges_of_body(body).value;
        if (!faces || !edges || faces->empty() || edges->empty() ||
            !rejected([&] { return kernel.modify().offset_body(body,.2,{}); },axiom::StatusCode::NotImplemented,kModUnsupportedGeometry,"modify.offset.support_gate") ||
            !rejected([&] { return kernel.modify().shell_body(body,{},.2); },axiom::StatusCode::NotImplemented,kModUnsupportedGeometry,"modify.shell.support_gate") ||
            !rejected([&] { return kernel.modify().move_face(body,faces->front(),.2); },axiom::StatusCode::NotImplemented,kModUnsupportedGeometry,"modify.move_face.support_gate") ||
            !rejected([&] { return kernel.blends().chamfer_edges(body,std::array{edges->front()},.2); },axiom::StatusCode::NotImplemented,kBlendUnsupportedGeometry,"blend.chamfer.support_gate")) return fail(__LINE__);
    }
    if (!rejected([&] { return kernel.modify().move_face(base,plane_face(kernel,base,2,4.5),-5); },axiom::StatusCode::DegenerateGeometry,kModDegenerateGeometry,"modify.move_face.geometry_gate") ||
        !rejected([&] { return kernel.modify().offset_body(base,-2.5,{}); },axiom::StatusCode::OperationFailed,kModOffsetSelfIntersection,"modify.offset.self_intersection") ||
        !rejected([&] { return kernel.modify().shell_body(base,{},2.5); },axiom::StatusCode::OperationFailed,kModShellFailure,"modify.shell.thickness") ||
        !rejected([&] { return kernel.blends().fillet_edges(base,std::array{selected},6); },axiom::StatusCode::OperationFailed,kBlendParameterTooLarge,"blend.fillet.radius_gate") ||
        !rejected([&] { return kernel.blends().chamfer_edges(base,std::array{selected},1e-8); },axiom::StatusCode::DegenerateGeometry,kBlendDegenerateGeometry,"blend.chamfer.geometry_gate")) return fail(__LINE__);

    std::vector<axiom::FaceId> discarded_faces;
    std::vector<axiom::EdgeId> discarded_edges;
    std::vector<axiom::VertexId> discarded_vertices;
    std::vector<axiom::ShellId> discarded_shells;
    std::vector<axiom::CurveId> discarded_curves;
    std::vector<axiom::SurfaceId> discarded_surfaces;
    for (const auto body : outputs) {
        const auto faces=query.faces_of_body(body).value;
        const auto edges=query.edges_of_body(body).value;
        const auto vertices=query.vertices_of_body(body).value;
        const auto shells=query.shells_of_body(body).value;
        if (!faces || !edges || !vertices || !shells) return fail(__LINE__);
        discarded_faces.insert(discarded_faces.end(),faces->begin(),faces->end());
        discarded_edges.insert(discarded_edges.end(),edges->begin(),edges->end());
        discarded_vertices.insert(discarded_vertices.end(),vertices->begin(),vertices->end());
        discarded_shells.insert(discarded_shells.end(),shells->begin(),shells->end());
        const auto curve=query.curve_of_edge(edges->front());
        const auto surface=query.surface_of_face(faces->front());
        if (!curve.value || !surface.value || !kernel.curve_service().eval(*curve.value,.5,1).value ||
            !kernel.surface_service().eval(*surface.value,0,0,1).value) return fail(__LINE__);
        discarded_curves.push_back(*curve.value); discarded_surfaces.push_back(*surface.value);
    }
    if (transaction.rollback().status!=axiom::StatusCode::Ok || kernel.object_count_total().value!=baseline_objects ||
        kernel.geometry_count().value!=baseline_geometry || query.has_vertex(*sentinel.value).value!=std::optional{false}) return fail(__LINE__);
    const auto after=kernel.runtime_store_counts().value;
    if (!after || after->mesh_records!=baseline->mesh_records ||
        after->tessellation_cache_entries!=baseline->tessellation_cache_entries ||
        after->face_tessellation_cache_entries!=baseline->face_tessellation_cache_entries ||
        after->curve_eval_cache_entries!=baseline->curve_eval_cache_entries ||
        after->surface_eval_cache_entries!=baseline->surface_eval_cache_entries || !invalid(0,false)) return fail(__LINE__);
    for (std::size_t i=1; i<bindings.size(); ++i) if (!invalid(i,true)) return fail(__LINE__);
    const auto remaining_bindings=eval.body_binding_bodies();
    if (!remaining_bindings.value) return fail(__LINE__);
    for (const auto body : outputs)
        if (query.has_body(body).value!=std::optional{false} ||
            std::find(remaining_bindings.value->begin(),remaining_bindings.value->end(),body)!=remaining_bindings.value->end()) return fail(__LINE__);
    for (const auto id : discarded_faces) if (query.has_face(id).value!=std::optional{false}) return fail(__LINE__);
    for (const auto id : discarded_edges) if (query.has_edge(id).value!=std::optional{false}) return fail(__LINE__);
    for (const auto id : discarded_vertices) if (query.has_vertex(id).value!=std::optional{false}) return fail(__LINE__);
    for (const auto id : discarded_shells) if (query.has_shell(id).value!=std::optional{false}) return fail(__LINE__);
    for (const auto id : discarded_curves) if (kernel.has_curve_id(id).value!=std::optional{false}) return fail(__LINE__);
    for (const auto id : discarded_surfaces) if (kernel.has_surface_id(id).value!=std::optional{false}) return fail(__LINE__);
    for (const auto mesh : output_meshes) if (kernel.convert().inspect_mesh(mesh).value) return fail(__LINE__);
    if (kernel.convert().brep_to_mesh(*stock.value,{}).value!=source_mesh.value ||
        query.faces_of_body(*stock.value).value!=source_faces || query.edges_of_body(*stock.value).value!=source_edges ||
        query.source_bodies_of_body(*stock.value).value!=source_sources ||
        !box_reference(kernel,*stock.value,{0,0,0},{8,5,3}) ||
        kernel.topology().validate().validate_indices_consistency().status!=axiom::StatusCode::Ok ||
        kernel.core_runtime_invariants_hold().value!=std::optional{true}) return fail(__LINE__);
    const auto retry=kernel.modify().move_face(*stock.value,plane_face(kernel,*stock.value,0,8),1);
    return retry.value && box_reference(kernel,retry.value->output,{0,0,0},{9,5,3});
}

}  // namespace

bool stage6_exit_regression() { return stage6_mechanical_fixture(); }
