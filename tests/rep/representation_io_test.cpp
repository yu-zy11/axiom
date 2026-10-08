#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <limits>
#include <sstream>
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

// Public OBJ triangles are the reference data: no private mesh record or
// production mass integrator participates in the independent calculations.
struct ReferenceObj {
    std::vector<axiom::Point3> vertices;
    std::vector<std::array<std::size_t,3>> triangles;
};

bool read_reference_obj(axiom::Kernel& kernel, axiom::BodyId body, ReferenceObj& mesh) {
    const auto path=std::filesystem::temp_directory_path()/
        ("axiom_s5_boundary_"+std::to_string(body.value)+".obj");
    if (kernel.io().export_obj(body,path.string(),{}).status!=axiom::StatusCode::Ok) return false;
    std::ifstream input{path};
    input.imbue(std::locale::classic());
    std::string line;
    bool valid=true;
    while (std::getline(input,line)) {
        std::istringstream record{line};
        record.imbue(std::locale::classic());
        std::string kind;
        record >> kind;
        if (kind=="v") {
            axiom::Point3 p{};
            if (!(record >> p.x >> p.y >> p.z) || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
                valid=false; break;
            }
            mesh.vertices.push_back(p);
        } else if (kind=="f") {
            std::array<std::size_t,3> face{};
            if (!(record >> face[0] >> face[1] >> face[2]) ||
                std::any_of(face.begin(),face.end(),[&](std::size_t id) { return id==0 || id>mesh.vertices.size(); })) {
                valid=false; break;
            }
            for (auto& id : face) --id;
            mesh.triangles.push_back(face);
        }
    }
    input.close();
    std::filesystem::remove(path);
    return valid && !mesh.vertices.empty() && !mesh.triangles.empty();
}

// Read the exported public OBJ, independently integrate its actual triangles,
// and require the t=0.5 inner coordinates. A bbox proxy cannot pass this oracle.
bool offset_shell_representation_regression() {
    axiom::Kernel kernel;
    const auto stock=kernel.primitives().box({0,0,0},4,5,6);
    if (!stock.value) return false;
    const auto faces=kernel.topology().query().faces_of_body(*stock.value);
    if (!faces.value) return false;
    axiom::FaceId top {};
    for (const auto face : *faces.value) {
        const auto bbox=kernel.topology().query().bbox_of_face(face);
        if (bbox.value && std::abs(bbox.value->min.z-6)<1e-9 && std::abs(bbox.value->max.z-6)<1e-9) top=face;
    }
    if (!top.value) return false;
    for (const bool open : {false,true}) {
        const std::vector<axiom::FaceId> removed=open ? std::vector<axiom::FaceId>{top} : std::vector<axiom::FaceId>{};
        const auto result=kernel.modify().shell_body(*stock.value,removed,.5);
        if (!result.value) return false;
        const auto body=result.value->output;
        const auto converted=kernel.convert().brep_to_mesh(body,{});
        if (!converted.value) return false;
        const auto inspection=kernel.convert().inspect_mesh(*converted.value);
        ReferenceObj mesh;
        if (!inspection.value || inspection.value->has_degenerate_triangles || inspection.value->has_out_of_range_indices ||
            inspection.value->tessellation_strategy!="owned_topo_welded" ||
            inspection.value->connected_components!=(open ? 1u : 2u) || !read_reference_obj(kernel,body,mesh)) return false;
        long double volume=0, area=0;
        std::size_t inner_vertices=0;
        for (const auto& p : mesh.vertices) {
            const bool inner_x=approx(p.x,.5,1e-9)||approx(p.x,3.5,1e-9);
            const bool inner_y=approx(p.y,.5,1e-9)||approx(p.y,4.5,1e-9);
            const bool inner_z=approx(p.z,.5,1e-9)||approx(p.z,open ? 6 : 5.5,1e-9);
            const bool outer_x=approx(p.x,0,1e-9)||approx(p.x,4,1e-9);
            const bool outer_y=approx(p.y,0,1e-9)||approx(p.y,5,1e-9);
            const bool outer_z=approx(p.z,0,1e-9)||approx(p.z,6,1e-9);
            if (inner_x && inner_y && inner_z) ++inner_vertices;
            else if (!(outer_x && outer_y && outer_z)) return false;
        }
        if (inner_vertices!=8) return false;
        for (const auto& f : mesh.triangles) {
            const auto& a=mesh.vertices[f[0]];
            const auto& b=mesh.vertices[f[1]];
            const auto& c=mesh.vertices[f[2]];
            const long double ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z;
            const long double vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
            const long double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
            area+=std::sqrt(nx*nx+ny*ny+nz*nz)/2;
            volume+=(a.x*(static_cast<long double>(b.y)*c.z-static_cast<long double>(b.z)*c.y)+
                a.y*(static_cast<long double>(b.z)*c.x-static_cast<long double>(b.x)*c.z)+
                a.z*(static_cast<long double>(b.x)*c.y-static_cast<long double>(b.y)*c.x))/6;
        }
        if (std::abs(volume-(open ? 54 : 60))>1e-7L || std::abs(area-(open ? 225 : 242))>1e-7L ||
            kernel.validate().validate_all(body,axiom::ValidationMode::Strict).status!=axiom::StatusCode::Ok) return false;
        const auto cached=kernel.convert().brep_to_mesh(body,{});
        if (cached.value!=converted.value) return false;
    }
    return true;
}

bool stage5_native_boundary_reference_regression() {
    axiom::Kernel kernel;
    const long double pi=std::acos(-1.0L);
    const double cone_radius=4*std::tan(static_cast<double>(pi)/6);
    const std::array bodies{kernel.primitives().sphere({0,0,0},2),
        kernel.primitives().cylinder({0,0,0},{0,0,1},2,4),
        kernel.primitives().cone({0,0,0},{0,0,1},static_cast<double>(pi)/6,4),
        kernel.primitives().torus({0,0,0},{0,0,1},2,.5),kernel.primitives().box({0,0,0},2,3,4)};
    const std::array<long double,5> expected_volume{32*pi/3,16*pi,
        pi*cone_radius*cone_radius*4/3,pi*pi,24};
    const std::array<long double,5> expected_area{16*pi,24*pi,
        pi*cone_radius*(cone_radius+std::hypot(cone_radius,4)),4*pi*pi,52};
    for (std::size_t kind=0; kind<bodies.size(); ++kind) {
        const auto fail=[&](int line) {
            std::cerr << "Stage 5 native boundary kind=" << kind << " line=" << line << '\n';
            return false;
        };
        if (!bodies[kind].value) return fail(__LINE__);
        const auto body=*bodies[kind].value;
        const auto converted=kernel.convert().brep_to_mesh(body,{});
        if (!converted.value) return fail(__LINE__);
        const auto report=kernel.convert().inspect_mesh(*converted.value);
        ReferenceObj mesh;
        if (!report.value || report.value->has_degenerate_triangles || report.value->has_out_of_range_indices ||
            !read_reference_obj(kernel,body,mesh)) return fail(__LINE__);
        long double volume=0, area=0;
        for (const auto& face : mesh.triangles) {
            const auto& a=mesh.vertices[face[0]];
            const auto& b=mesh.vertices[face[1]];
            const auto& c=mesh.vertices[face[2]];
            const long double ux=b.x-a.x, uy=b.y-a.y, uz=b.z-a.z;
            const long double vx=c.x-a.x, vy=c.y-a.y, vz=c.z-a.z;
            const long double nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
            const long double length=std::sqrt(nx*nx+ny*ny+nz*nz);
            if (!(length>1e-14L)) return fail(__LINE__);
            area+=length/2;
            volume+=(a.x*(static_cast<long double>(b.y)*c.z-static_cast<long double>(b.z)*c.y)+
                a.y*(static_cast<long double>(b.z)*c.x-static_cast<long double>(b.x)*c.z)+
                a.z*(static_cast<long double>(b.x)*c.y-static_cast<long double>(b.y)*c.x))/6;
            const long double x=(static_cast<long double>(a.x)+b.x+c.x)/3;
            const long double y=(static_cast<long double>(a.y)+b.y+c.y)/3;
            const long double z=(static_cast<long double>(a.z)+b.z+c.z)/3;
            const long double radial=std::hypot(x,y);
            long double rx=0, ry=0, rz=0, deviation=0;
            const bool cap=kind==1 || kind==2;
            if (kind==4) {
                if (std::abs(a.x-b.x)<1e-12 && std::abs(a.x-c.x)<1e-12 &&
                    (std::abs(x)<1e-12 || std::abs(x-2)<1e-12)) rx=x<1 ? -1 : 1;
                else if (std::abs(a.y-b.y)<1e-12 && std::abs(a.y-c.y)<1e-12 &&
                    (std::abs(y)<1e-12 || std::abs(y-3)<1e-12)) ry=y<1.5 ? -1 : 1;
                else if (std::abs(a.z-b.z)<1e-12 && std::abs(a.z-c.z)<1e-12 &&
                    (std::abs(z)<1e-12 || std::abs(z-4)<1e-12)) rz=z<2 ? -1 : 1;
                else return fail(__LINE__);
            } else if (cap && std::abs(a.z-b.z)<1e-12 && std::abs(a.z-c.z)<1e-12) {
                rz=z<2 ? -1 : 1;
            } else if (kind==0) {
                rx=x; ry=y; rz=z;
                deviation=std::abs(std::sqrt(x*x+y*y+z*z)-2);
            } else if (kind==1) {
                rx=x; ry=y;
                deviation=std::abs(radial-2);
            } else if (kind==2) {
                rx=x/radial; ry=y/radial; rz=-cone_radius/4;
                deviation=std::abs(radial-cone_radius*z/4)/std::sqrt(1+cone_radius*cone_radius/16);
            } else {
                rx=(radial-2)*x/radial; ry=(radial-2)*y/radial; rz=z;
                deviation=std::abs(std::hypot(radial-2,z)-.5L);
            }
            const long double normal_length=std::sqrt(rx*rx+ry*ry+rz*rz);
            if (!(normal_length>0) || (nx*rx+ny*ry+nz*rz)/(length*normal_length)<
                std::cos(5*pi/180)-1e-9L || deviation>.1L+1e-10L) return fail(__LINE__);
        }
        // The signed integral catches inward winding; the analytic formulas
        // distinguish the physical surface from any bbox replacement geometry.
        if (!(volume>0) || std::abs(volume-expected_volume[kind])>expected_volume[kind]*.01L ||
            std::abs(area-expected_area[kind])>expected_area[kind]*.01L) {
            std::cerr << "native integral volume=" << volume << " expected_volume=" << expected_volume[kind]
                      << " area=" << area << " expected_area=" << expected_area[kind] << '\n';
            return fail(__LINE__);
        }
        if (kind==4 && (std::abs(volume-24)>1e-10L || std::abs(area-52)>1e-10L)) return fail(__LINE__);
        if (kind==4) continue;
        const auto before=kernel.runtime_store_counts();
        const auto next=kernel.next_object_id();
        const auto cache=kernel.tessellation_cache_stats();
        const auto exhausted=kernel.convert().brep_to_mesh(body,{1e-12,1e-6,true});
        const auto diagnostic=kernel.diagnostics().get(exhausted.diagnostic_id);
        const auto after=kernel.runtime_store_counts();
        const auto cache_after=kernel.tessellation_cache_stats();
        if (exhausted.status==axiom::StatusCode::Ok || exhausted.value || !diagnostic.value ||
            std::none_of(diagnostic.value->issues.begin(),diagnostic.value->issues.end(),[](const axiom::Issue& issue) {
                return issue.code==axiom::diag_codes::kTesFailure && issue.stage=="rep.tessellation.budget";
            }) || !before.value || !after.value || !cache.value || !cache_after.value ||
            kernel.next_object_id().value!=next.value || before.value->mesh_records!=after.value->mesh_records ||
            before.value->tessellation_cache_entries!=after.value->tessellation_cache_entries ||
            before.value->face_tessellation_cache_entries!=after.value->face_tessellation_cache_entries ||
            cache.value->body_cache_hits!=cache_after.value->body_cache_hits ||
            cache.value->body_cache_misses!=cache_after.value->body_cache_misses) return fail(__LINE__);
    }
    return true;
}

bool stage5_bilinear_boundary_regression() {
    axiom::Kernel kernel;
    const auto fail=[](int line) {
        std::cerr << "Stage 5 bilinear boundary line=" << line << '\n';
        return false;
    };
    const std::array<axiom::Point3,4> poles{{{0,0,0},{0,1,0},{1,0,0},{1,1,1}}};
    const auto surface=kernel.surfaces().make_bezier(poles);
    const auto trimmed=surface.value ? kernel.surfaces().make_trimmed(*surface.value,.2,.8,.2,.8)
        : axiom::Result<axiom::SurfaceId>{};
    if (!surface.value || !trimmed.value) return fail(__LINE__);
    struct Patch { axiom::BodyId body; axiom::FaceId face; axiom::ShellId shell; };
    const auto make_patch=[&](axiom::SurfaceId support, std::vector<axiom::Point2> uv,
                              bool pcurves, bool wrong_pcurve=false) {
        Patch out{};
        auto tx=kernel.topology().begin_transaction();
        std::vector<axiom::VertexId> vertices;
        for (const auto& p : uv) {
            // z=xy is the independent polynomial definition, not SurfaceService.
            const auto vertex=tx.create_vertex({p.x,p.y,p.x*p.y});
            if (!vertex.value) return out;
            vertices.push_back(*vertex.value);
        }
        std::vector<axiom::CoedgeId> coedges;
        for (std::size_t i=0; i<uv.size(); ++i) {
            const auto j=(i+1)%uv.size();
            const auto curve=kernel.curves().make_line_segment({uv[i].x,uv[i].y,uv[i].x*uv[i].y},
                {uv[j].x,uv[j].y,uv[j].x*uv[j].y});
            if (!curve.value) return out;
            const auto edge=tx.create_edge(*curve.value,vertices[i],vertices[j]);
            if (!edge.value) return out;
            const auto coedge=tx.create_coedge(*edge.value,false);
            if (!coedge.value) return out;
            if (pcurves) {
                auto start=uv[i];
                if (wrong_pcurve && i==0) start.x+=.1;
                const auto pc=kernel.pcurves().make_polyline(std::array{start,uv[j]});
                if (!pc.value || tx.set_coedge_pcurve(*coedge.value,*pc.value).status!=axiom::StatusCode::Ok) return out;
            }
            coedges.push_back(*coedge.value);
        }
        const auto loop=tx.create_loop(coedges);
        if (!loop.value) return out;
        const auto face=tx.create_face(support,*loop.value,{});
        if (!face.value) return out;
        const auto shell=tx.create_shell(std::array{*face.value});
        if (!shell.value) return out;
        const auto body=tx.create_body(std::array{*shell.value});
        if (!body.value || tx.commit().status!=axiom::StatusCode::Ok) return out;
        return Patch{*body.value,*face.value,*shell.value};
    };
    axiom::BSplineSurfaceDesc spline_desc;
    spline_desc.poles.assign(poles.begin(),poles.end());
    spline_desc.degree_u=1;
    spline_desc.degree_v=1;
    const auto spline=kernel.surfaces().make_bspline(spline_desc);
    axiom::NURBSSurfaceDesc nurbs_desc;
    nurbs_desc.poles=spline_desc.poles;
    nurbs_desc.weights={1,1,1,1};
    nurbs_desc.degree_u=1;
    nurbs_desc.degree_v=1;
    const auto nurbs=kernel.surfaces().make_nurbs(nurbs_desc);
    nurbs_desc.weights={1,2,1,1};
    const auto unequal=kernel.surfaces().make_nurbs(nurbs_desc);
    spline_desc.knots_u={2,2,5,5};
    const auto custom_domain=kernel.surfaces().make_bspline(spline_desc);
    // The public factory affine-normalizes {2,2,5,5} to unit-clamped knots.
    // This distinct vector retains its non-clamped ends after normalization.
    spline_desc.knots_u={-1,0,1,2};
    const auto unclamped=kernel.surfaces().make_bspline(spline_desc);
    std::vector<axiom::Point3> high_order_poles;
    for (const double u : {0.,.5,1.}) for (const double v : {0.,.5,1.}) high_order_poles.push_back({u,v,u*v});
    const auto high_order=kernel.surfaces().make_bezier(high_order_poles);
    if (!spline.value || !nurbs.value || !unequal.value || !custom_domain.value ||
        !unclamped.value || !high_order.value) return fail(__LINE__);
    const auto full=make_patch(*surface.value,{{0,0},{1,0},{1,1},{0,1}},false);
    const auto spline_patch=make_patch(*spline.value,{{0,0},{1,0},{1,1},{0,1}},true);
    const auto nurbs_patch=make_patch(*nurbs.value,{{0,0},{1,0},{1,1},{0,1}},true);
    const auto unequal_patch=make_patch(*unequal.value,{{0,0},{1,0},{1,1},{0,1}},true);
    const auto custom_patch=make_patch(*custom_domain.value,{{0,0},{1,0},{1,1},{0,1}},true);
    const auto unclamped_patch=make_patch(*unclamped.value,{{0,0},{1,0},{1,1},{0,1}},true);
    const auto high_order_patch=make_patch(*high_order.value,{{0,0},{1,0},{1,1},{0,1}},true);
    const auto trim=make_patch(*trimmed.value,{{.2,.2},{.8,.2},{.8,.8},{.2,.8}},true);
    const auto reversed=make_patch(*surface.value,{{0,1},{1,1},{1,0},{0,0}},true);
    const auto wrong=make_patch(*surface.value,{{0,0},{1,0},{1,1},{0,1}},true,true);
    const auto triangle=make_patch(*surface.value,{{0,0},{1,0},{0,1}},true);
    const std::array<axiom::Point2,3> triangular_uv{{{0,0},{1,0},{0,1}}};
    const auto polygon_trim=kernel.surfaces().make_trimmed_polygon(*surface.value,0,1,0,1,triangular_uv);
    const std::array<axiom::Point2,4> outer_uv{{{0,0},{1,0},{1,1},{0,1}}};
    const auto hole_trim=kernel.surfaces().make_trimmed_polygon_with_holes(*surface.value,0,1,0,1,outer_uv,
        {{{.3,.3},{.7,.3},{.7,.7},{.3,.7}}});
    if (!polygon_trim.value || !hole_trim.value) return fail(__LINE__);
    const auto trimmed_triangle=make_patch(*polygon_trim.value,{{0,0},{1,0},{0,1}},true);
    const auto trimmed_hole=make_patch(*hole_trim.value,{{0,0},{1,0},{1,1},{0,1}},true);
    if (full.body.value==0 || trim.body.value==0 || reversed.body.value==0 ||
        wrong.body.value==0 || triangle.body.value==0 || trimmed_triangle.body.value==0 ||
        trimmed_hole.body.value==0 || spline_patch.body.value==0 || nurbs_patch.body.value==0 ||
        unequal_patch.body.value==0 || custom_patch.body.value==0 || unclamped_patch.body.value==0 ||
        high_order_patch.body.value==0) return fail(__LINE__);
    const auto check_patch=[&](Patch patch, long double low, long double high, int winding) {
        const auto converted=kernel.convert().brep_to_mesh(patch.body,{});
        if (!converted.value || kernel.convert().brep_to_mesh(patch.body,{}).value!=converted.value) return fail(__LINE__);
        const auto report=kernel.convert().inspect_mesh(*converted.value);
        ReferenceObj mesh;
        if (!report.value || report.value->has_degenerate_triangles || report.value->has_out_of_range_indices ||
            !read_reference_obj(kernel,patch.body,mesh)) return fail(__LINE__);
        long double area=0, max_deviation=0;
        for (const auto& p : mesh.vertices)
            if (p.x<low-1e-9L || p.x>high+1e-9L || p.y<low-1e-9L || p.y>high+1e-9L ||
                std::abs(static_cast<long double>(p.z)-static_cast<long double>(p.x)*p.y)>1e-9L) return fail(__LINE__);
        for (const auto& face : mesh.triangles) {
            const auto& a=mesh.vertices[face[0]];
            const auto& b=mesh.vertices[face[1]];
            const auto& c=mesh.vertices[face[2]];
            const long double ux=b.x-a.x, uy=b.y-a.y, uz=b.z-a.z;
            const long double vx=c.x-a.x, vy=c.y-a.y, vz=c.z-a.z;
            const long double nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
            const long double length=std::sqrt(nx*nx+ny*ny+nz*nz);
            if (!(length>1e-14L)) return fail(__LINE__);
            area+=length/2;
            const long double x=(static_cast<long double>(a.x)+b.x+c.x)/3;
            const long double y=(static_cast<long double>(a.y)+b.y+c.y)/3;
            const long double z=(static_cast<long double>(a.z)+b.z+c.z)/3;
            max_deviation=std::max(max_deviation,std::abs(z-x*y));
            for (const auto id : face) {
                const auto& p=mesh.vertices[id];
                const long double dot=winding*(-p.y*nx-p.x*ny+nz)/
                    (length*std::sqrt(1+static_cast<long double>(p.x)*p.x+static_cast<long double>(p.y)*p.y));
                if (dot<std::cos(5*std::acos(-1.0L)/180)-1e-9L) return fail(__LINE__);
            }
        }
        // Independent composite Simpson integral of sqrt(1+x²+y²); no kernel
        // quadrature, bbox volume, or production triangle integration is used.
        constexpr int intervals=64;
        const long double step=(high-low)/intervals;
        long double expected=0;
        for (int i=0; i<=intervals; ++i) for (int j=0; j<=intervals; ++j) {
            const long double x=low+i*step, y=low+j*step;
            const int wi=(i==0 || i==intervals) ? 1 : i%2 ? 4 : 2;
            const int wj=(j==0 || j==intervals) ? 1 : j%2 ? 4 : 2;
            expected+=wi*wj*std::sqrt(1+x*x+y*y);
        }
        expected*=step*step/9;
        // This is an open display surface, not a certified closed solid.
        const auto mass=kernel.query().mass_properties(patch.body);
        if (max_deviation>.1L+1e-10L || std::abs(area-expected)>=.002L || mass.value) {
            std::cerr << "bilinear patch body=" << patch.body.value << " area=" << area
                      << " expected_area=" << expected << " deviation=" << max_deviation
                      << " mass_value=" << mass.value.has_value() << '\n';
            return fail(__LINE__);
        }
        return true;
    };
    if (!check_patch(full,0,1,1) || !check_patch(spline_patch,0,1,1) || !check_patch(nurbs_patch,0,1,1) ||
        !check_patch(custom_patch,0,1,1) || !check_patch(trim,.2,.8,1) ||
        !check_patch(reversed,0,1,-1)) return fail(__LINE__);
    const auto fingerprint=[&] {
        const auto r=kernel.runtime_store_counts();
        const auto c=kernel.tessellation_cache_stats();
        if (!r.value || !c.value) return std::array<std::uint64_t,13>{};
        return std::array{kernel.next_object_id().value.value_or(0),kernel.object_count_total().value.value_or(0),
            r.value->mesh_records,kernel.body_count().value.value_or(0),r.value->tessellation_cache_entries,
            r.value->face_tessellation_cache_entries,r.value->surface_eval_cache_entries,
            c.value->body_cache_hits,c.value->body_cache_misses,c.value->body_cache_stale_evictions,
            c.value->face_cache_hits,c.value->face_cache_misses,c.value->face_cache_stale_evictions};
    };
    const auto rejects=[&](const axiom::Result<axiom::MeshId>& result) {
        const auto diagnostic=kernel.diagnostics().get(result.diagnostic_id);
        return result.status!=axiom::StatusCode::Ok && !result.value && diagnostic.value &&
            std::any_of(diagnostic.value->issues.begin(),diagnostic.value->issues.end(),[](const axiom::Issue& issue) {
                return issue.code==axiom::diag_codes::kTesFailure && issue.stage=="rep.tessellation.face";
            });
    };
    const std::array unsupported{wrong,triangle,trimmed_triangle,trimmed_hole,unequal_patch,unclamped_patch,high_order_patch};
    for (std::size_t unsupported_kind=0; unsupported_kind<unsupported.size(); ++unsupported_kind) {
        const auto patch=unsupported[unsupported_kind];
        const auto before=fingerprint();
        for (int operation=0; operation<3; ++operation) {
            const auto result=operation==0 ? kernel.convert().brep_to_mesh(patch.body,{}) :
                operation==1 ? kernel.convert().brep_to_mesh_local(patch.body,std::array{patch.face},{}) :
                               kernel.convert().brep_to_mesh_shell(patch.body,patch.shell,{});
            const auto after=fingerprint();
            if (!rejects(result) || after!=before) {
                std::cerr << "bilinear unsupported kind=" << unsupported_kind
                          << " body=" << patch.body.value << " face=" << patch.face.value
                          << " operation=" << operation << " status=" << static_cast<int>(result.status)
                          << " value=" << result.value.has_value() << '\n';
                const auto diagnostic=kernel.diagnostics().get(result.diagnostic_id);
                if (diagnostic.value) for (const auto& issue : diagnostic.value->issues)
                    std::cerr << issue.stage << ' ' << issue.code << ' ' << issue.message << '\n';
                for (std::size_t i=0; i<after.size(); ++i)
                    if (after[i]!=before[i])
                        std::cerr << "rejection fingerprint index=" << i << " before=" << before[i]
                                  << " after=" << after[i] << '\n';
                return fail(__LINE__);
            }
        }
    }
    const auto original=kernel.convert().brep_to_mesh(full.body,{});
    if (!original.value) return fail(__LINE__);
    const auto roundtrip_before=fingerprint();
    const auto success=kernel.convert().verify_brep_mesh_round_trip(full.body,{});
    const auto mesh_success=kernel.convert().verify_mesh_brep_round_trip(*original.value,{});
    if (!success.value || !success.value->passed || !mesh_success.value || !mesh_success.value->passed ||
        fingerprint()!=roundtrip_before || kernel.convert().brep_to_mesh(full.body,{}).value!=original.value) return fail(__LINE__);
    const auto before=fingerprint();
    const auto mesh_batch=kernel.convert().mesh_to_brep_batch(
        std::array{*original.value,axiom::MeshId{std::numeric_limits<std::uint64_t>::max()}});
    if (mesh_batch.status==axiom::StatusCode::Ok || mesh_batch.value || fingerprint()!=before) return fail(__LINE__);
    const auto batch=kernel.convert().brep_to_mesh_batch(std::array{full.body,triangle.body},{.073,11,true});
    const auto roundtrip=kernel.convert().verify_brep_mesh_round_trip(triangle.body,{});
    if (batch.status==axiom::StatusCode::Ok || batch.value || roundtrip.status==axiom::StatusCode::Ok ||
        roundtrip.value || fingerprint()!=before ||
        !rejects(kernel.convert().brep_to_mesh(full.body,{1e-12,5,true})) || fingerprint()!=before) return fail(__LINE__);
    for (const auto invalid : {axiom::TessellationOptions{std::numeric_limits<double>::infinity(),5,true},
                              axiom::TessellationOptions{.1,std::numeric_limits<double>::quiet_NaN(),true}}) {
        const auto result=kernel.convert().brep_to_mesh(full.body,invalid);
        const auto diagnostic=kernel.diagnostics().get(result.diagnostic_id);
        if (result.status!=axiom::StatusCode::InvalidInput || result.value || !diagnostic.value ||
            !has_issue_code(*diagnostic.value,axiom::diag_codes::kCoreParameterOutOfRange) || fingerprint()!=before) return fail(__LINE__);
    }
    const auto embedded=kernel.convert().mesh_to_brep(*original.value);
    if (!embedded.value) return fail(__LINE__);
    const auto rebound_before=fingerprint();
    const auto repeated=kernel.convert().mesh_to_brep(*original.value);
    const auto duplicates=kernel.convert().mesh_to_brep_batch(std::array{*original.value,*original.value});
    if (repeated.value!=embedded.value || !duplicates.value || duplicates.value->size()!=2 ||
        duplicates.value->front()!=*embedded.value || duplicates.value->back()!=*embedded.value ||
        fingerprint()!=rebound_before || kernel.convert().brep_to_mesh(*embedded.value,{}).value!=original.value) return fail(__LINE__);
    const auto embedded_report=kernel.convert().inspect_mesh(*original.value);
    if (!embedded_report.value || embedded_report.value->tessellation_strategy!="owned_topo_welded" ||
        kernel.query().mass_properties(*embedded.value).value) return fail(__LINE__);
    // Clearing the public mesh store leaves a MeshRep handle without its actual
    // boundary. It must remain that representation and fail, never create a bbox.
    axiom::Kernel orphan_kernel;
    const auto orphan_source=orphan_kernel.primitives().box({0,0,0},2,3,4);
    if (!orphan_source.value) return fail(__LINE__);
    const auto orphan_mesh=orphan_kernel.convert().brep_to_mesh(*orphan_source.value,{});
    if (!orphan_mesh.value) return fail(__LINE__);
    const auto orphan_body=orphan_kernel.convert().mesh_to_brep(*orphan_mesh.value);
    if (!orphan_body.value || orphan_kernel.clear_mesh_store().status!=axiom::StatusCode::Ok) return fail(__LINE__);
    const auto orphan_before=orphan_kernel.runtime_store_counts();
    const auto orphan_next=orphan_kernel.next_object_id();
    const auto orphan_stats=orphan_kernel.tessellation_cache_stats();
    const auto missing=orphan_kernel.convert().brep_to_mesh(*orphan_body.value,{});
    const auto missing_diagnostic=orphan_kernel.diagnostics().get(missing.diagnostic_id);
    const auto orphan_after=orphan_kernel.runtime_store_counts();
    const auto orphan_stats_after=orphan_kernel.tessellation_cache_stats();
    const auto orphan_kind=orphan_kernel.representation().kind_of_body(*orphan_body.value);
    if (missing.status==axiom::StatusCode::Ok || missing.value || !missing_diagnostic.value ||
        std::none_of(missing_diagnostic.value->issues.begin(),missing_diagnostic.value->issues.end(),[](const axiom::Issue& issue) {
            return issue.code==axiom::diag_codes::kTesFailure && issue.stage=="rep.tessellation.support";
        }) || !orphan_before.value || !orphan_after.value || !orphan_stats.value || !orphan_stats_after.value ||
        !orphan_kind.value || *orphan_kind.value!=axiom::RepKind::MeshRep ||
        orphan_kernel.next_object_id().value!=orphan_next.value ||
        orphan_before.value->mesh_records!=orphan_after.value->mesh_records ||
        orphan_before.value->tessellation_cache_entries!=orphan_after.value->tessellation_cache_entries ||
        orphan_before.value->face_tessellation_cache_entries!=orphan_after.value->face_tessellation_cache_entries ||
        orphan_stats.value->body_cache_hits!=orphan_stats_after.value->body_cache_hits ||
        orphan_stats.value->body_cache_misses!=orphan_stats_after.value->body_cache_misses ||
        orphan_stats.value->body_cache_stale_evictions!=orphan_stats_after.value->body_cache_stale_evictions ||
        orphan_stats.value->face_cache_hits!=orphan_stats_after.value->face_cache_hits ||
        orphan_stats.value->face_cache_misses!=orphan_stats_after.value->face_cache_misses ||
        orphan_stats.value->face_cache_stale_evictions!=orphan_stats_after.value->face_cache_stale_evictions) return fail(__LINE__);
    return true;
}


bool stage5_planar_normal_boundary_regression() {
    axiom::Kernel kernel;
    if (kernel.set_linear_tolerance(2e-5).status!=axiom::StatusCode::Ok) return false;
    const auto plane=kernel.surfaces().make_plane({0,0,0},{0,0,1});
    if (!plane.value) return false;
    // Each vertex is within 1e-5 of plane Z=0 and below chordal_error/4.
    // Its short edge exceeds linear tolerance, while the triangle normal is
    // tilted atan(.2)=11.31 deg. A 30 deg budget succeeds; a 5 deg budget fails.
    const std::array<axiom::Point3,4> corners{{{0,0,-1e-5},{1,0,-1e-5},{1,1e-4,1e-5},{0,1e-4,1e-5}}};
    auto tx=kernel.topology().begin_transaction();
    std::array<axiom::VertexId,4> vertices{};
    std::array<axiom::CoedgeId,4> coedges{};
    for (std::size_t i=0; i<corners.size(); ++i) {
        const auto v=tx.create_vertex(corners[i]);
        if (!v.value) return false;
        vertices[i]=*v.value;
    }
    for (std::size_t i=0; i<corners.size(); ++i) {
        const auto j=(i+1)%corners.size();
        const auto line=kernel.curves().make_line_segment(corners[i],corners[j]);
        if (!line.value) return false;
        const auto edge=tx.create_edge(*line.value,vertices[i],vertices[j]);
        if (!edge.value) return false;
        const auto coedge=tx.create_coedge(*edge.value,false);
        if (!coedge.value) return false;
        coedges[i]=*coedge.value;
    }
    const auto loop=tx.create_loop(coedges);
    if (!loop.value) return false;
    const auto face=tx.create_face(*plane.value,*loop.value,{});
    if (!face.value) return false;
    const auto shell=tx.create_shell(std::array{*face.value});
    if (!shell.value) return false;
    const auto body=tx.create_body(std::array{*shell.value});
    if (!body.value || tx.commit().status!=axiom::StatusCode::Ok) return false;
    const auto loose=kernel.convert().brep_to_mesh(*body.value,{.1,30,true});
    if (!loose.value || !kernel.convert().inspect_mesh(*loose.value).value) return false;
    const auto before=kernel.runtime_store_counts();
    const auto next=kernel.next_object_id();
    const auto stats=kernel.tessellation_cache_stats();
    for (const auto result : {kernel.convert().brep_to_mesh(*body.value,{}),
                             kernel.convert().brep_to_mesh_local(*body.value,std::array{*face.value},{}),
                             kernel.convert().brep_to_mesh_shell(*body.value,*shell.value,{})}) {
        const auto diagnostic=kernel.diagnostics().get(result.diagnostic_id);
        const auto after=kernel.runtime_store_counts();
        const auto stats_after=kernel.tessellation_cache_stats();
        if (result.status==axiom::StatusCode::Ok || result.value || !diagnostic.value ||
            std::none_of(diagnostic.value->issues.begin(),diagnostic.value->issues.end(),[](const axiom::Issue& issue) {
                return issue.code==axiom::diag_codes::kTesFailure && issue.stage=="rep.tessellation.face";
            }) || !before.value || !after.value || !stats.value || !stats_after.value ||
            kernel.next_object_id().value!=next.value || before.value->mesh_records!=after.value->mesh_records ||
            before.value->tessellation_cache_entries!=after.value->tessellation_cache_entries ||
            before.value->face_tessellation_cache_entries!=after.value->face_tessellation_cache_entries ||
            stats.value->body_cache_hits!=stats_after.value->body_cache_hits ||
            stats.value->body_cache_misses!=stats_after.value->body_cache_misses ||
            stats.value->face_cache_hits!=stats_after.value->face_cache_hits ||
            stats.value->face_cache_misses!=stats_after.value->face_cache_misses) return false;
    }
    return true;
}

bool stage3_representation_consistency_regression() {
    axiom::Kernel kernel;
    auto& topo = kernel.topology().query();
    const axiom::ProfileRef first_profile{"same_bbox_first",{{0,0,0},{4,0,0},{0,3,0}}};
    const axiom::ProfileRef second_profile{"same_bbox_second",{{4,3,0},{0,3,0},{4,0,0}}};
    const auto first = kernel.sweeps().extrude(first_profile,{0,0,1},2);
    const auto second = kernel.sweeps().extrude(second_profile,{0,0,1},2);
    const auto duplicate = kernel.sweeps().extrude(first_profile,{0,0,1},2);
    if (!first.value || !second.value || !duplicate.value) return false;
    const auto a = kernel.convert().brep_to_mesh(*first.value,{});
    const auto b = kernel.convert().brep_to_mesh(*second.value,{});
    const auto c = kernel.convert().brep_to_mesh(*duplicate.value,{});
    if (!a.value || !b.value || !c.value || a.value==b.value || a.value==c.value || b.value==c.value) return false;
    const auto bbox_a = topo.bbox_of_body_from_topology(*first.value);
    const auto bbox_b = topo.bbox_of_body_from_topology(*second.value);
    if (!bbox_a.value || !bbox_b.value || bbox_a.value->min.x!=bbox_b.value->min.x ||
        bbox_a.value->min.y!=bbox_b.value->min.y || bbox_a.value->max.x!=bbox_b.value->max.x ||
        bbox_a.value->max.y!=bbox_b.value->max.y || bbox_a.value->max.z!=bbox_b.value->max.z) return false;

    // Read the public OBJ output and integrate its actual triangles independently.
    // Equal bbox/area/height profiles still have different centroids and vertices.
    const auto check_export = [&](axiom::BodyId body, double volume, double area, axiom::Point3 centroid) {
        const auto path = std::filesystem::temp_directory_path()/
            ("axiom_stage3_consistency_"+std::to_string(body.value)+".obj");
        if (kernel.io().export_obj(body,path.string(),{}).status!=axiom::StatusCode::Ok) return false;
        std::ifstream input{path};
        std::vector<axiom::Point3> vertices;
        long double v = 0, surface_area = 0;
        std::array<long double,3> moment{};
        std::string line;
        bool valid = true;
        while (std::getline(input,line)) {
            std::istringstream record{line};
            std::string kind;
            record >> kind;
            if (kind=="v") {
                axiom::Point3 point{};
                if (!(record >> point.x >> point.y >> point.z)) { valid=false; break; }
                vertices.push_back(point);
            } else if (kind=="f") {
                std::array<std::size_t,3> ids{};
                if (!(record >> ids[0] >> ids[1] >> ids[2]) ||
                    std::any_of(ids.begin(),ids.end(),[&](std::size_t id) { return id==0 || id>vertices.size(); })) {
                    valid=false; break;
                }
                const auto& p=vertices[ids[0]-1];
                const auto& q=vertices[ids[1]-1];
                const auto& r=vertices[ids[2]-1];
                const long double cx=static_cast<long double>(q.y)*r.z-static_cast<long double>(q.z)*r.y;
                const long double cy=static_cast<long double>(q.z)*r.x-static_cast<long double>(q.x)*r.z;
                const long double cz=static_cast<long double>(q.x)*r.y-static_cast<long double>(q.y)*r.x;
                const long double tetra=(p.x*cx+p.y*cy+p.z*cz)/6;
                v+=tetra;
                moment[0]+=tetra*(p.x+q.x+r.x)/4;
                moment[1]+=tetra*(p.y+q.y+r.y)/4;
                moment[2]+=tetra*(p.z+q.z+r.z)/4;
                const long double ux=q.x-p.x, uy=q.y-p.y, uz=q.z-p.z;
                const long double vx=r.x-p.x, vy=r.y-p.y, vz=r.z-p.z;
                const long double nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
                surface_area+=std::sqrt(nx*nx+ny*ny+nz*nz)/2;
            }
        }
        input.close();
        std::filesystem::remove(path);
        const auto mass=kernel.query().mass_properties(body);
        const auto close=[](long double x, long double y) { return std::abs(x-y)<=1e-8L*std::max(1.0L,std::abs(y)); };
        return valid && mass.value && close(v,volume) && close(surface_area,area) &&
            close(moment[0]/v,centroid.x) && close(moment[1]/v,centroid.y) && close(moment[2]/v,centroid.z) &&
            close(v,mass.value->volume) && close(surface_area,mass.value->area) &&
            close(moment[0]/v,mass.value->centroid.x) && close(moment[1]/v,mass.value->centroid.y) &&
            close(moment[2]/v,mass.value->centroid.z);
    };
    if (!check_export(*first.value,12,36,{4.0/3,1,1}) ||
        !check_export(*second.value,12,36,{8.0/3,2,1})) return false;
    const axiom::ProfileRef holed{"mesh_hole",{{0,0,0},{4,0,0},{4,4,0},{0,4,0}},
        {{{1,1,0},{3,1,0},{3,3,0},{1,3,0}}}};
    const axiom::ProfileRef concave{"mesh_concave",{{0,0,0},{3,0,0},{3,1,0},{1,1,0},{1,3,0},{0,3,0}}};
    const auto hole_body=kernel.sweeps().extrude(holed,{0,0,1},2);
    const auto concave_body=kernel.sweeps().extrude(concave,{0,0,1},2);
    if (!hole_body.value || !concave_body.value || !check_export(*hole_body.value,24,72,{2,2,1}) ||
        !check_export(*concave_body.value,10,34,{1.1,1.1,1})) return false;
    // Independent point probes on every horizontal cap triangle catch triangles
    // spanning the removed hole or the L profile's missing upper-right quadrant.
    for (const auto body : {*hole_body.value,*concave_body.value}) {
        ReferenceObj actual;
        if (!read_reference_obj(kernel,body,actual)) return false;
        for (const auto& triangle : actual.triangles) {
            const auto& a=actual.vertices[triangle[0]];
            const auto& b=actual.vertices[triangle[1]];
            const auto& c=actual.vertices[triangle[2]];
            if (std::abs(a.z-b.z)>1e-10 || std::abs(a.z-c.z)>1e-10) continue;
            for (int i=0; i<=4; ++i) for (int j=0; j<=4-i; ++j) {
                const double x=(i*a.x+j*b.x+(4-i-j)*c.x)/4;
                const double y=(i*a.y+j*b.y+(4-i-j)*c.y)/4;
                if (body==*hole_body.value ? (x>1+1e-9 && x<3-1e-9 && y>1+1e-9 && y<3-1e-9)
                    : (x>1+1e-9 && y>1+1e-9)) return false;
            }
        }
    }
    const axiom::ProfileRef rectangle{"mesh_model_chain",{{0,0,0},{2,0,0},{2,3,0},{0,3,0}}};
    const auto rail=kernel.curves().make_line_segment({0,0,0},{0,0,4});
    const auto swept=rail.value ? kernel.sweeps().sweep(rectangle,*rail.value) : axiom::Result<axiom::BodyId>{};
    auto upper=rectangle;
    for (auto& point : upper.polygon_xyz) point.z=4;
    const auto lofted=kernel.sweeps().loft(std::array{rectangle,upper});
    const auto box=kernel.primitives().box({0,0,0},2,3,4);
    const auto box_faces=box.value ? topo.faces_of_body(*box.value) : axiom::Result<std::vector<axiom::FaceId>>{};
    const auto thickened=box_faces.value && !box_faces.value->empty()
        ? kernel.sweeps().thicken(box_faces.value->front(),2) : axiom::Result<axiom::BodyId>{};
    const axiom::ProfileRef meridian{"mesh_revolve",{{2,0,0},{3,0,0},{3,0,4},{2,0,4}}};
    const double span=std::acos(-1.0)/2;
    const auto revolved=kernel.sweeps().revolve_between(meridian,{{0,0,0},{0,0,1}},0,-span);
    if (!swept.value || !lofted.value || !thickened.value || !revolved.value ||
        !check_export(*swept.value,24,52,{1,1.5,2}) || !check_export(*lofted.value,24,52,{1,1.5,2}) ||
        !check_export(*thickened.value,12,32,{1,1.5,-1})) return false;
    const auto revolve_vertices=topo.vertex_count_of_body(*revolved.value);
    if (!revolve_vertices.value || *revolve_vertices.value%4!=0 || *revolve_vertices.value<8) return false;
    const double intervals=static_cast<double>(*revolve_vertices.value/4-1);
    const double annulus=2.5*intervals*std::sin(span/intervals);
    const double perimeter=10*intervals*std::sin(span/(2*intervals))+2;
    // Sum the two homothetic polygon sectors' triangle first moments. The
    // negative interval puts the centroid in quadrant IV; this is a chord mesh.
    const double center=19/(15*intervals*std::tan(span/(2*intervals)));
    if (!check_export(*revolved.value,4*annulus,2*annulus+4*perimeter,{center,-center,2})) return false;

    // mesh_to_brep explicitly rebinds the assembled mesh to its new MeshRep body.
    // The original body's cache must not return that reassociated record.
    const auto embedded=kernel.convert().mesh_to_brep(*a.value);
    const auto rebuilt=kernel.convert().brep_to_mesh(*first.value,{});
    if (!embedded.value || !rebuilt.value || rebuilt.value==a.value ||
        kernel.convert().brep_to_mesh(*embedded.value,{}).value!=a.value) return false;
    const auto faces=topo.faces_of_body(*first.value);
    const auto shells=topo.shells_of_body(*first.value);
    const auto sources=topo.source_faces_of_body(*first.value);
    const auto displaced=kernel.surfaces().make_plane({0,0,100},{0,0,1});
    const auto curved=kernel.surfaces().make_sphere({0,0,0},1);
    if (!faces.value || faces.value->empty() || !shells.value || shells.value->size()!=1 ||
        !sources.value || !displaced.value || !curved.value) return false;
    auto txn=kernel.topology().begin_transaction();
    const auto savepoint=txn.create_savepoint();
    if (!savepoint.value || txn.replace_surface(faces.value->back(),*displaced.value).status!=axiom::StatusCode::Ok) return false;
    const axiom::TessellationOptions cold{.271,31,true};
    const auto stores=kernel.runtime_store_counts();
    const auto objects=kernel.object_count_total();
    const auto next=kernel.next_object_id();
    const auto writes=txn.write_operation_count();
    const auto rejects=[&](const axiom::Result<axiom::MeshId>& result) {
        const auto report=kernel.diagnostics().get(result.diagnostic_id);
        const auto after=kernel.runtime_store_counts();
        return result.status==axiom::StatusCode::OperationFailed && !result.value && report.value &&
            std::any_of(report.value->issues.begin(),report.value->issues.end(),[&](const axiom::Issue& issue) {
                return issue.code==axiom::diag_codes::kTesFailure && issue.stage=="rep.tessellation.face" &&
                    std::find(issue.related_entities.begin(),issue.related_entities.end(),faces.value->back().value)!=issue.related_entities.end();
            }) && stores.value && after.value && objects.value && next.value && writes.value &&
            kernel.object_count_total().value==objects.value && kernel.next_object_id().value==next.value &&
            txn.write_operation_count().value==writes.value && after.value->mesh_records==stores.value->mesh_records &&
            after.value->tessellation_cache_entries==stores.value->tessellation_cache_entries &&
            after.value->face_tessellation_cache_entries==stores.value->face_tessellation_cache_entries;
    };
    // Warm body cache, cold body/face cache, forced local rebuild and shell path
    // must all reject a displaced support; no fallback or early-face publication.
    if (!rejects(kernel.convert().brep_to_mesh(*first.value,{})) ||
        !rejects(kernel.convert().brep_to_mesh(*first.value,cold)) ||
        !rejects(kernel.convert().brep_to_mesh_local(*first.value,*faces.value,cold)) ||
        !rejects(kernel.convert().brep_to_mesh_shell(*first.value,shells.value->front(),cold)) ||
        txn.rollback_to_savepoint(*savepoint.value).status!=axiom::StatusCode::Ok ||
        kernel.convert().brep_to_mesh(*first.value,{}).value!=rebuilt.value ||
        topo.source_faces_of_body(*first.value).value!=sources.value) return false;
    if (txn.replace_surface(faces.value->front(),*curved.value).status!=axiom::StatusCode::Ok) return false;
    const auto unsupported=kernel.convert().brep_to_mesh(*first.value,{});
    const auto support_report=kernel.diagnostics().get(unsupported.diagnostic_id);
    if (unsupported.status!=axiom::StatusCode::NotImplemented || unsupported.value || !support_report.value ||
        std::none_of(support_report.value->issues.begin(),support_report.value->issues.end(),[](const axiom::Issue& issue) {
            return issue.code==axiom::diag_codes::kTesFailure && issue.stage=="rep.tessellation.support";
        }) || txn.rollback().status!=axiom::StatusCode::Ok ||
        !check_export(*first.value,12,36,{4.0/3,1,1})) return false;

    const auto before_bad_options=kernel.runtime_store_counts();
    const auto next_bad_options=kernel.next_object_id();
    const auto bad_options=kernel.convert().brep_to_mesh(*first.value,{0,15,true});
    const auto after_bad_options=kernel.runtime_store_counts();
    const auto options_report=kernel.diagnostics().get(bad_options.diagnostic_id);
    if (bad_options.status!=axiom::StatusCode::InvalidInput || bad_options.value || !options_report.value ||
        !has_issue_code(*options_report.value,axiom::diag_codes::kCoreParameterOutOfRange) ||
        !before_bad_options.value || !after_bad_options.value ||
        kernel.next_object_id().value!=next_bad_options.value ||
        before_bad_options.value->mesh_records!=after_bad_options.value->mesh_records ||
        before_bad_options.value->tessellation_cache_entries!=after_bad_options.value->tessellation_cache_entries ||
        before_bad_options.value->face_tessellation_cache_entries!=after_bad_options.value->face_tessellation_cache_entries) return false;

    for (const auto primitive : {kernel.primitives().box({0,0,0},2,3,4),
                                 kernel.primitives().sphere({0,0,0},2)}) {
        if (!primitive.value) return false;
        const auto mesh=kernel.convert().brep_to_mesh(*primitive.value,{});
        const auto owned=topo.faces_of_body(*primitive.value);
        if (!mesh.value || !owned.value || owned.value->empty()) return false;
        auto edit=kernel.topology().begin_transaction();
        if (edit.replace_surface(owned.value->back(),*displaced.value).status!=axiom::StatusCode::Ok) return false;
        const auto result=kernel.convert().brep_to_mesh(*primitive.value,{});
        const auto report=kernel.diagnostics().get(result.diagnostic_id);
        if (result.value || !report.value || !has_issue_code(*report.value,axiom::diag_codes::kTesFailure) ||
            edit.rollback().status!=axiom::StatusCode::Ok ||
            kernel.convert().brep_to_mesh(*primitive.value,{}).value!=mesh.value) return false;
    }
    return true;
}


// A fixed closed tetrahedron distinguishes actual triangles from its 2x3x4
// bbox: volume is 4, not 24. Integrate emitted OBJ facets independently, using
// long double moments and the analytic native-model-unit reference.
bool stage5_stl_geometry_reference_regression() {
    const auto data=std::filesystem::path(__FILE__).parent_path().parent_path()/"data"/"io";
    const auto root=std::filesystem::temp_directory_path()/
        ("axiom_s5_stl_reference_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    axiom::Kernel kernel;
    axiom::ImportOptions options;
    options.auto_repair=true;
    const auto source=kernel.io().import_stl((data/"s5_io_precision_tetra.stl").string(),options);
    const auto stl=root/"roundtrip.stl";
    if (!source.value || source.status!=axiom::StatusCode::Ok ||
        kernel.io().export_stl(*source.value,stl.string(),{}).status!=axiom::StatusCode::Ok) return false;
    const auto reread=kernel.io().import_stl(stl.string(),options);
    if (!reread.value || reread.status!=axiom::StatusCode::Ok) return false;
    for (const auto body : {*source.value,*reread.value}) {
        if (kernel.validate().validate_all(body,axiom::ValidationMode::Standard).status!=axiom::StatusCode::Ok)
            return false;
        const auto output=root/(std::to_string(body.value)+".obj");
        if (kernel.io().export_obj(body,output.string(),{}).status!=axiom::StatusCode::Ok) return false;
        std::ifstream input {output};
        input.imbue(std::locale::classic());
        std::vector<axiom::Point3> vertices;
        long double volume=0,area=0;
        std::array<long double,3> moment{};
        std::size_t triangles=0;
        std::string line;
        while (std::getline(input,line)) {
            std::istringstream record {line};
            record.imbue(std::locale::classic());
            std::string kind;
            record>>kind;
            if (kind=="v") {
                axiom::Point3 point{};
                if (!(record>>point.x>>point.y>>point.z)) return false;
                vertices.push_back(point);
            } else if (kind=="f") {
                std::array<std::size_t,3> ids{};
                if (!(record>>ids[0]>>ids[1]>>ids[2]) ||
                    std::any_of(ids.begin(),ids.end(),[&](const auto id) {return id==0 || id>vertices.size();}))
                    return false;
                const auto& p=vertices[ids[0]-1];
                const auto& q=vertices[ids[1]-1];
                const auto& r=vertices[ids[2]-1];
                const long double cx=static_cast<long double>(q.y)*r.z-static_cast<long double>(q.z)*r.y;
                const long double cy=static_cast<long double>(q.z)*r.x-static_cast<long double>(q.x)*r.z;
                const long double cz=static_cast<long double>(q.x)*r.y-static_cast<long double>(q.y)*r.x;
                const long double tetra=(p.x*cx+p.y*cy+p.z*cz)/6;
                volume+=tetra;
                moment[0]+=tetra*(p.x+q.x+r.x)/4;
                moment[1]+=tetra*(p.y+q.y+r.y)/4;
                moment[2]+=tetra*(p.z+q.z+r.z)/4;
                const long double ux=q.x-p.x,uy=q.y-p.y,uz=q.z-p.z;
                const long double vx=r.x-p.x,vy=r.y-p.y,vz=r.z-p.z;
                const long double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
                area+=std::sqrt(nx*nx+ny*ny+nz*nz)/2;
                ++triangles;
            }
        }
        const auto close=[](long double actual,long double expected) {return std::abs(actual-expected)<=1e-12L;};
        const auto mesh=kernel.convert().brep_to_mesh(body,{});
        const auto kind=kernel.representation().kind_of_body(body);
        const auto shells=kernel.topology().query().shell_count_of_body(body);
        if (triangles!=4 || vertices.size()!=12 || !close(volume,4) || !close(area,13+std::sqrt(244.0L)/2) ||
            !close(moment[0]/volume,0.12345678901234566L+0.5L) ||
            !close(moment[1]/volume,-1.2345678901234567L+0.75L) ||
            !close(moment[2]/volume,3.456789012345679L+1) || !mesh.value || !kind.value ||
            *kind.value!=axiom::RepKind::MeshRep || !shells.value || *shells.value!=0) return false;
        const auto inspection=kernel.convert().inspect_mesh(*mesh.value);
        if (!inspection.value || inspection.value->triangle_count!=4 || inspection.value->has_degenerate_triangles ||
            inspection.value->has_out_of_range_indices || inspection.value->tessellation_strategy!="io_import_stl")
            return false;
    }
    std::filesystem::remove_all(root);
    return true;
}

}  // namespace

int main() {
    if (!offset_shell_representation_regression()) {
        std::cerr << "Stage 6 shell actual OBJ/mesh thickness regression failed\n";
        return 1;
    }
    if (!stage5_native_boundary_reference_regression()) {
        std::cerr << "Stage 5 native tessellation boundary independent reference regression failed\n";
        return 1;
    }
    if (!stage5_bilinear_boundary_regression()) {
        std::cerr << "Stage 5 bilinear tessellation boundary independent reference regression failed\n";
        return 1;
    }
    if (!stage5_planar_normal_boundary_regression()) {
        std::cerr << "Stage 5 planar normal boundary independent reference regression failed\n";
        return 1;
    }
    if (!stage5_stl_geometry_reference_regression()) {
        std::cerr << "Stage 5 STL actual geometry independent reference regression failed\n";
        return 1;
    }
    if (!stage3_representation_consistency_regression()) {
        std::cerr << "Stage 3 representation consistency regression failed\n";
        return 1;
    }
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
        if (!pl.value || !trimmed_plane.value) return 1;
        std::array<axiom::Point3, 4> trim_corners{};
        const std::array<axiom::Point2, 4> trim_uv{{{0,0},{10,0},{10,20},{0,20}}};
        for (std::size_t i=0; i<trim_uv.size(); ++i) {
            const auto sample=kernel.surface_service().eval(*pl.value,trim_uv[i].x,trim_uv[i].y,0);
            if (!sample.value) return 1;
            trim_corners[i]=sample.value->point;
        }
        auto l0 = kernel.curves().make_line_segment(trim_corners[0],trim_corners[1]);
        auto l1 = kernel.curves().make_line_segment(trim_corners[1],trim_corners[2]);
        auto l2 = kernel.curves().make_line_segment(trim_corners[2],trim_corners[3]);
        auto l3 = kernel.curves().make_line_segment(trim_corners[3],trim_corners[0]);
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
        auto tv0 = ttx.create_vertex(trim_corners[0]);
        auto tv1 = ttx.create_vertex(trim_corners[1]);
        auto tv2 = ttx.create_vertex(trim_corners[2]);
        auto tv3 = ttx.create_vertex(trim_corners[3]);
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
        ReferenceObj swept_reference;
        if (!read_reference_obj(kernel,*sw_body.value,swept_reference)) return 1;
        long double swept_area=0;
        for (const auto& p : swept_reference.vertices)
            if (std::abs(p.y)>1e-12 || p.x<-1e-12 || p.x>1+1e-12 || p.z<-1e-12 || p.z>2+1e-12) return 1;
        for (const auto& face : swept_reference.triangles) {
            const auto& a=swept_reference.vertices[face[0]];
            const auto& b=swept_reference.vertices[face[1]];
            const auto& c=swept_reference.vertices[face[2]];
            const long double ny=(static_cast<long double>(b.z)-a.z)*(c.x-a.x)-
                (static_cast<long double>(b.x)-a.x)*(c.z-a.z);
            if (!(ny<0)) return 1;
            swept_area-=ny/2;
        }
        if (std::abs(swept_area-2)>1e-10L) return 1;
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
        const auto before=kernel.runtime_store_counts();
        const auto next=kernel.next_object_id();
        auto rev_mesh = kernel.convert().brep_to_mesh(*rv_body.value, rev_tes);
        const auto diagnostic=kernel.diagnostics().get(rev_mesh.diagnostic_id);
        const auto after=kernel.runtime_store_counts();
        // These edges are chords, not the two circular arcs of this UV patch.
        // Refusing the unsupported boundary prevents the old bbox-domain fill.
        if (rev_mesh.status == axiom::StatusCode::Ok || rev_mesh.value || !diagnostic.value ||
            std::none_of(diagnostic.value->issues.begin(),diagnostic.value->issues.end(),[](const axiom::Issue& issue) {
                return issue.code==axiom::diag_codes::kTesFailure && issue.stage=="rep.tessellation.face";
            }) || !before.value || !after.value || kernel.next_object_id().value!=next.value ||
            before.value->mesh_records!=after.value->mesh_records ||
            before.value->tessellation_cache_entries!=after.value->tessellation_cache_entries ||
            before.value->face_tessellation_cache_entries!=after.value->face_tessellation_cache_entries) {
            std::cerr << "revolved chord boundary must fail without publishing a curved patch\n";
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
    if (section_fail.status != axiom::StatusCode::Ok || !section_fail.value || section_fail.value->value != 0) {
        std::cerr << "expected successful empty section for non-intersecting plane\n";
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
    // STEP currently restores primitive metadata, not an owned physical BRep.
    // Such imported records do not gain the native factory's mass certificate.
    const auto unsupported_mass = [&](const auto& result) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        if (result.status != axiom::StatusCode::NotImplemented || result.value || !report.value) return false;
        for (const auto& issue : report.value->issues)
            if (issue.code == axiom::diag_codes::kCoreOperationUnsupported &&
                issue.stage == "query.mass_properties.support_gate") return true;
        return false;
    };
    if (!unsupported_mass(imported_sphere_props) || !unsupported_mass(imported_cone_props) ||
        !unsupported_mass(kernel.query().mass_properties(*brep.value)) ||
        !unsupported_mass(kernel.query().mass_properties(*implicit_brep.value))) {
        std::cerr << "metadata/mesh representations must not fabricate physical mass\n";
        return 1;
    }
    // Independent native references, rather than comparing two implementations
    // that could share the same incorrect cone-height inertia coefficient.
    const double mass_pi = std::acos(-1.0);
    const double cone_radius = 6*std::tan(mass_pi/6);
    const double cone_volume = mass_pi*cone_radius*cone_radius*6/3;
    const double cone_transverse = cone_volume*(3*cone_radius*cone_radius/20+3.0*36/80);
    if (!sphere_props.value || !cone_props.value ||
        !approx(sphere_props.value->volume,32*mass_pi/3) ||
        !approx(sphere_props.value->area,16*mass_pi) ||
        !approx(sphere_props.value->inertia[0],sphere_props.value->volume*8/5) ||
        !approx(cone_props.value->volume,cone_volume) ||
        !approx(cone_props.value->area,mass_pi*cone_radius*(cone_radius+std::hypot(cone_radius,6))) ||
        !approx(cone_props.value->centroid.z,4.5) ||
        !approx(cone_props.value->inertia[0],cone_transverse) ||
        !approx(cone_props.value->inertia[4],cone_transverse) ||
        !approx(cone_props.value->inertia[8],cone_volume*3*cone_radius*cone_radius/10)) {
        std::cerr << "native primitive analytic mass reference mismatch\n";
        return 1;
    }

    std::filesystem::remove(out_path);
    std::filesystem::remove(sphere_path);
    std::filesystem::remove(cone_path);
    std::filesystem::remove(mesh_report_path);
    return 0;
}
