#include <cmath>
#include <iostream>

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
