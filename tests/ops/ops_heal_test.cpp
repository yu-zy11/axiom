#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "axiom/core/types.h"
#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"

namespace {

bool has_issue_code(const axiom::DiagnosticReport& report, std::string_view code) {
    for (const auto& issue : report.issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

bool has_warning_code(const std::vector<axiom::Warning>& warnings, std::string_view code) {
    for (const auto& warning : warnings) {
        if (warning.code == code) {
            return true;
        }
    }
    return false;
}

const axiom::Issue* find_issue(const axiom::DiagnosticReport& report, std::string_view code) {
    for (const auto& issue : report.issues) {
        if (issue.code == code) {
            return &issue;
        }
    }
    return nullptr;
}

bool has_issue_stage(const axiom::DiagnosticReport& report, std::string_view stage) {
    for (const auto& issue : report.issues) {
        if (issue.stage == stage) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    axiom::Kernel kernel;

    auto box_a = kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 10.0, 10.0);
    auto box_b = kernel.primitives().box({30.0, 30.0, 30.0}, 5.0, 5.0, 5.0);
    if (box_a.status != axiom::StatusCode::Ok || box_b.status != axiom::StatusCode::Ok ||
        !box_a.value.has_value() || !box_b.value.has_value()) {
        std::cerr << "failed to create boxes for ops/heal test\n";
        return 1;
    }

    auto wedge = kernel.primitives().wedge({1.0, 2.0, 3.0}, 2.0, 3.0, 4.0);
    if (wedge.status != axiom::StatusCode::Ok || !wedge.value.has_value()) {
        std::cerr << "failed to create wedge for mass_properties test\n";
        return 1;
    }
    auto wedge_props = kernel.query().mass_properties(*wedge.value);
    if (wedge_props.status != axiom::StatusCode::Ok || !wedge_props.value.has_value()) {
        std::cerr << "wedge mass_properties failed\n";
        return 1;
    }
    const auto expected_wedge_volume = 0.5 * 2.0 * 3.0 * 4.0;
    if (std::abs(wedge_props.value->volume - expected_wedge_volume) > 1e-9) {
        std::cerr << "wedge volume should use prism formula not bbox\n";
        return 1;
    }
    const auto expected_wedge_cx = 1.0 + 2.0 / 3.0;
    const auto expected_wedge_cy = 2.0 + 3.0 / 3.0;
    const auto expected_wedge_cz = 3.0 + 2.0;
    if (std::abs(wedge_props.value->centroid.x - expected_wedge_cx) > 1e-9 ||
        std::abs(wedge_props.value->centroid.y - expected_wedge_cy) > 1e-9 ||
        std::abs(wedge_props.value->centroid.z - expected_wedge_cz) > 1e-9) {
        std::cerr << "wedge centroid mismatch\n";
        return 1;
    }
    const auto wdx = 2.0;
    const auto wdy = 3.0;
    const auto wdz = 4.0;
    const auto wm = expected_wedge_volume;
    const auto w_io_xx = wm * wdy * wdy / 6.0 + wm * wdz * wdz / 3.0;
    const auto w_io_yy = wm * wdx * wdx / 6.0 + wm * wdz * wdz / 3.0;
    const auto w_io_zz = wm * (wdx * wdx + wdy * wdy) / 6.0;
    const auto wcx = wdx / 3.0;
    const auto wcy = wdy / 3.0;
    const auto wcz = wdz * 0.5;
    const auto w_ic_xx = w_io_xx - wm * (wcy * wcy + wcz * wcz);
    const auto w_ic_yy = w_io_yy - wm * (wcx * wcx + wcz * wcz);
    const auto w_ic_zz = w_io_zz - wm * (wcx * wcx + wcy * wcy);
    const auto w_ic_xy = -wm * wdx * wdy / 12.0 + wm * wcx * wcy;
    const auto w_ic_xz = -wm * wdx * wdz / 6.0 + wm * wcx * wcz;
    const auto w_ic_yz = -wm * wdy * wdz / 6.0 + wm * wcy * wcz;
    if (std::abs(wedge_props.value->inertia[0] - w_ic_xx) > 1e-5 ||
        std::abs(wedge_props.value->inertia[4] - w_ic_yy) > 1e-5 ||
        std::abs(wedge_props.value->inertia[8] - w_ic_zz) > 1e-5 ||
        std::abs(wedge_props.value->inertia[1] - w_ic_xy) > 1e-5 ||
        std::abs(wedge_props.value->inertia[3] - w_ic_xy) > 1e-5 ||
        std::abs(wedge_props.value->inertia[2] - w_ic_xz) > 1e-5 ||
        std::abs(wedge_props.value->inertia[6] - w_ic_xz) > 1e-5 ||
        std::abs(wedge_props.value->inertia[5] - w_ic_yz) > 1e-5 ||
        std::abs(wedge_props.value->inertia[7] - w_ic_yz) > 1e-5) {
        std::cerr << "wedge inertia tensor about centroid mismatch\n";
        return 1;
    }

    auto box_props = kernel.query().mass_properties(*box_a.value);
    if (box_props.status != axiom::StatusCode::Ok || !box_props.value.has_value()) {
        std::cerr << "box mass_properties failed\n";
        return 1;
    }
    const auto m = 1000.0;
    const auto expected_ixx = m / 12.0 * (10.0 * 10.0 + 10.0 * 10.0);
    if (std::abs(box_props.value->inertia[0] - expected_ixx) > 1e-3 ||
        std::abs(box_props.value->inertia[4] - expected_ixx) > 1e-3 ||
        std::abs(box_props.value->inertia[8] - expected_ixx) > 1e-3) {
        std::cerr << "box principal inertia about centroid unexpected\n";
        return 1;
    }

    auto torus = kernel.primitives().torus({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 4.0, 1.0);
    if (torus.status != axiom::StatusCode::Ok || !torus.value.has_value()) {
        std::cerr << "failed to create torus for mass_properties test\n";
        return 1;
    }
    auto torus_props = kernel.query().mass_properties(*torus.value);
    if (torus_props.status != axiom::StatusCode::Ok || !torus_props.value.has_value()) {
        std::cerr << "torus mass_properties failed\n";
        return 1;
    }
    const auto r_t = 4.0;
    const auto r_m = 1.0;
    const auto m_t = torus_props.value->volume;
    const auto i_ax_expected = m_t * (r_t * r_t + 0.75 * r_m * r_m);
    const auto i_tr_expected = m_t * (0.5 * r_t * r_t + 0.625 * r_m * r_m);
    if (std::abs(torus_props.value->inertia[8] - i_ax_expected) > 1e-3 * std::max(1.0, i_ax_expected) ||
        std::abs(torus_props.value->inertia[0] - i_tr_expected) > 1e-3 * std::max(1.0, i_tr_expected) ||
        std::abs(torus_props.value->inertia[4] - i_tr_expected) > 1e-3 * std::max(1.0, i_tr_expected)) {
        std::cerr << "torus inertia tensor mismatch\n";
        return 1;
    }

    axiom::ProfileRef tri_profile;
    tri_profile.label = "tri_vol";
    tri_profile.polygon_xyz = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 2.0, 0.0}};
    auto tri_extrude = kernel.sweeps().extrude(tri_profile, {0.0, 0.0, 1.0}, 3.0);
    if (tri_extrude.status != axiom::StatusCode::Ok || !tri_extrude.value.has_value()) {
        std::cerr << "triangle extrude failed for mass_properties test\n";
        return 1;
    }
    auto tri_mp = kernel.query().mass_properties(*tri_extrude.value);
    if (tri_mp.status != axiom::StatusCode::Ok || !tri_mp.value.has_value()) {
        std::cerr << "triangle extrude mass_properties failed\n";
        return 1;
    }
    const auto expected_prism_vol = 2.0 * 3.0;
    if (std::abs(tri_mp.value->volume - expected_prism_vol) > 1e-6) {
        std::cerr << "polygon extrude volume should use prism formula not bbox\n";
        return 1;
    }
    const auto expected_prism_area = 4.0 + 12.0 + 6.0 * std::sqrt(2.0);
    if (std::abs(tri_mp.value->area - expected_prism_area) > 1e-4) {
        std::cerr << "polygon extrude surface area mismatch\n";
        return 1;
    }
    if (std::abs(tri_mp.value->centroid.x - 2.0 / 3.0) > 1e-5 ||
        std::abs(tri_mp.value->centroid.y - 2.0 / 3.0) > 1e-5 ||
        std::abs(tri_mp.value->centroid.z - 1.5) > 1e-5) {
        std::cerr << "polygon extrude centroid mismatch\n";
        return 1;
    }

    auto bad_intersection = kernel.booleans().run(axiom::BooleanOp::Intersect, *box_a.value, *box_b.value, {});
    if (bad_intersection.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "expected failed intersection for disjoint boxes\n";
        return 1;
    }

    auto bad_intersection_diag = kernel.diagnostics().get(bad_intersection.diagnostic_id);
    if (bad_intersection_diag.status != axiom::StatusCode::Ok || !bad_intersection_diag.value.has_value() ||
        !has_issue_code(*bad_intersection_diag.value, axiom::diag_codes::kBoolIntersectionFailure)) {
        std::cerr << "missing intersection failure diagnostic\n";
        return 1;
    }

    // 不相交两盒并集：质量属性应为两盒体积/表面积之和与体积加权质心，而非 union AABB 的单一长方体体积。
    axiom::BooleanOptions bool_opts;
    auto disjoint_union = kernel.booleans().run(axiom::BooleanOp::Union, *box_a.value, *box_b.value, bool_opts);
    if (disjoint_union.status != axiom::StatusCode::Ok || !disjoint_union.value.has_value()) {
        std::cerr << "disjoint union failed\n";
        return 1;
    }
    auto union_mp = kernel.query().mass_properties(disjoint_union.value->output);
    if (union_mp.status != axiom::StatusCode::Ok || !union_mp.value.has_value()) {
        std::cerr << "disjoint union mass_properties failed\n";
        return 1;
    }
    const double vol_a = 10.0 * 10.0 * 10.0;
    const double vol_b = 5.0 * 5.0 * 5.0;
    const double area_a = 2.0 * (10.0 * 10.0 + 10.0 * 10.0 + 10.0 * 10.0);
    const double area_b = 2.0 * (5.0 * 5.0 + 5.0 * 5.0 + 5.0 * 5.0);
    const double exp_vol = vol_a + vol_b;
    const double exp_area = area_a + area_b;
    const double cx = (vol_a * 5.0 + vol_b * 32.5) / exp_vol;
    if (std::abs(union_mp.value->volume - exp_vol) > 1e-6 || std::abs(union_mp.value->area - exp_area) > 1e-6 ||
        std::abs(union_mp.value->centroid.x - cx) > 1e-5 || std::abs(union_mp.value->centroid.y - cx) > 1e-5 ||
        std::abs(union_mp.value->centroid.z - cx) > 1e-5) {
        std::cerr << "disjoint union mass_properties should combine primitive boxes analytically\n";
        return 1;
    }

    auto disjoint_sub = kernel.booleans().run(axiom::BooleanOp::Subtract, *box_a.value, *box_b.value, bool_opts);
    if (disjoint_sub.status != axiom::StatusCode::Ok || !disjoint_sub.value.has_value()) {
        std::cerr << "disjoint subtract failed\n";
        return 1;
    }
    auto sub_mp = kernel.query().mass_properties(disjoint_sub.value->output);
    if (sub_mp.status != axiom::StatusCode::Ok || !sub_mp.value.has_value()) {
        std::cerr << "disjoint subtract mass_properties failed\n";
        return 1;
    }
    if (std::abs(sub_mp.value->volume - vol_a) > 1e-6 || std::abs(sub_mp.value->area - area_a) > 1e-6 ||
        std::abs(sub_mp.value->centroid.x - 5.0) > 1e-5 || std::abs(sub_mp.value->centroid.y - 5.0) > 1e-5 ||
        std::abs(sub_mp.value->centroid.z - 5.0) > 1e-5) {
        std::cerr << "disjoint subtract mass_properties should match left box only\n";
        return 1;
    }

    // 不相交 盒 + 球：并集用两解析体组合；减（左球右盒）退化为左球解析量（7.7 扩展，非仅限双盒）。
    const double pi_bs = 3.14159265358979323846;
    auto ball_far = kernel.primitives().sphere({60.0, 5.0, 5.0}, 2.0);
    if (ball_far.status != axiom::StatusCode::Ok || !ball_far.value.has_value()) {
        std::cerr << "sphere for disjoint boolean mass test failed\n";
        return 1;
    }
    const double v_ball = (4.0 / 3.0) * pi_bs * 8.0;
    const double a_ball = 4.0 * pi_bs * 4.0;
    auto union_box_sphere =
        kernel.booleans().run(axiom::BooleanOp::Union, *box_a.value, *ball_far.value, bool_opts);
    if (union_box_sphere.status != axiom::StatusCode::Ok || !union_box_sphere.value.has_value()) {
        std::cerr << "disjoint union box+sphere failed\n";
        return 1;
    }
    auto ubs_mp = kernel.query().mass_properties(union_box_sphere.value->output);
    if (ubs_mp.status != axiom::StatusCode::Ok || !ubs_mp.value.has_value()) {
        std::cerr << "union box+sphere mass_properties failed\n";
        return 1;
    }
    const double exp_vol_bs = vol_a + v_ball;
    const double exp_area_bs = area_a + a_ball;
    const double cx_bs = (vol_a * 5.0 + v_ball * 60.0) / exp_vol_bs;
    if (std::abs(ubs_mp.value->volume - exp_vol_bs) > 1e-5 || std::abs(ubs_mp.value->area - exp_area_bs) > 1e-4 ||
        std::abs(ubs_mp.value->centroid.x - cx_bs) > 1e-4 || std::abs(ubs_mp.value->centroid.y - 5.0) > 1e-5 ||
        std::abs(ubs_mp.value->centroid.z - 5.0) > 1e-5) {
        std::cerr << "disjoint union box+sphere mass_properties mismatch\n";
        return 1;
    }
    auto sub_sphere_box =
        kernel.booleans().run(axiom::BooleanOp::Subtract, *ball_far.value, *box_a.value, bool_opts);
    if (sub_sphere_box.status != axiom::StatusCode::Ok || !sub_sphere_box.value.has_value()) {
        std::cerr << "disjoint subtract sphere-box failed\n";
        return 1;
    }
    auto ssb_mp = kernel.query().mass_properties(sub_sphere_box.value->output);
    if (ssb_mp.status != axiom::StatusCode::Ok || !ssb_mp.value.has_value()) {
        std::cerr << "subtract sphere-box mass_properties failed\n";
        return 1;
    }
    if (std::abs(ssb_mp.value->volume - v_ball) > 1e-5 || std::abs(ssb_mp.value->area - a_ball) > 1e-4 ||
        std::abs(ssb_mp.value->centroid.x - 60.0) > 1e-5 || std::abs(ssb_mp.value->centroid.y - 5.0) > 1e-5 ||
        std::abs(ssb_mp.value->centroid.z - 5.0) > 1e-5) {
        std::cerr << "disjoint subtract (lhs sphere) mass_properties should match sphere only\n";
        return 1;
    }

    // AABB 面接触两单位立方并集：union 长方体体积 = 2，与两体体积之和一致，可走 Touching 解析组合路径。
    auto touch_c0 = kernel.primitives().box({0.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
    auto touch_c1 = kernel.primitives().box({1.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
    if (touch_c0.status != axiom::StatusCode::Ok || touch_c1.status != axiom::StatusCode::Ok ||
        !touch_c0.value.has_value() || !touch_c1.value.has_value()) {
        std::cerr << "touching cubes primitives failed\n";
        return 1;
    }
    auto u_touch = kernel.booleans().run(axiom::BooleanOp::Union, *touch_c0.value, *touch_c1.value, bool_opts);
    if (u_touch.status != axiom::StatusCode::Ok || !u_touch.value.has_value()) {
        std::cerr << "touching cubes union failed\n";
        return 1;
    }
    auto touch_mp = kernel.query().mass_properties(u_touch.value->output);
    if (touch_mp.status != axiom::StatusCode::Ok || !touch_mp.value.has_value() ||
        std::abs(touch_mp.value->volume - 2.0) > 1e-5) {
        std::cerr << "touching union mass_properties should sum volumes when union AABB matches sum\n";
        return 1;
    }

    // 大盒完全包含小盒：并集占位 bbox 为大盒，质量应取大盒解析量。
    auto big_h = kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 10.0, 10.0);
    auto small_h = kernel.primitives().box({4.0, 4.0, 4.0}, 2.0, 2.0, 2.0);
    if (big_h.status != axiom::StatusCode::Ok || small_h.status != axiom::StatusCode::Ok ||
        !big_h.value.has_value() || !small_h.value.has_value()) {
        std::cerr << "contain-test boxes failed\n";
        return 1;
    }
    auto u_hole = kernel.booleans().run(axiom::BooleanOp::Union, *big_h.value, *small_h.value, bool_opts);
    if (u_hole.status != axiom::StatusCode::Ok || !u_hole.value.has_value()) {
        std::cerr << "union big contains small failed\n";
        return 1;
    }
    auto hole_mp = kernel.query().mass_properties(u_hole.value->output);
    if (hole_mp.status != axiom::StatusCode::Ok || !hole_mp.value.has_value() ||
        std::abs(hole_mp.value->volume - 1000.0) > 1e-3) {
        std::cerr << "lhs-contains-rhs union mass_properties should match outer box\n";
        return 1;
    }

    // 不相交 占位 extrude + 远距盒：操作数之一为 Sweep，体积为缓存之和。
    auto sw_u = kernel.sweeps().extrude({"bool_sw"}, {0.0, 0.0, 1.0}, 3.0);
    auto far_cube = kernel.primitives().box({80.0, 0.0, 0.0}, 2.0, 2.0, 2.0);
    if (sw_u.status != axiom::StatusCode::Ok || far_cube.status != axiom::StatusCode::Ok ||
        !sw_u.value.has_value() || !far_cube.value.has_value()) {
        std::cerr << "extrude/far cube for boolean mass failed\n";
        return 1;
    }
    auto u_sw_box = kernel.booleans().run(axiom::BooleanOp::Union, *sw_u.value, *far_cube.value, bool_opts);
    if (u_sw_box.status != axiom::StatusCode::Ok || !u_sw_box.value.has_value()) {
        std::cerr << "union extrude+box failed\n";
        return 1;
    }
    auto sw_box_mp = kernel.query().mass_properties(u_sw_box.value->output);
    if (sw_box_mp.status != axiom::StatusCode::Ok || !sw_box_mp.value.has_value() ||
        std::abs(sw_box_mp.value->volume - 11.0) > 1e-4) {
        std::cerr << "disjoint union extrude+box mass_properties should sum operand volumes\n";
        return 1;
    }

    // 双盒 Intersect：结果 bbox 与 AABB 交一致时，质量属性按交叠长方体解析（依赖 BodyRecord.boolean_op）。
    auto overlap_box = kernel.primitives().box({5.0, 0.0, 0.0}, 5.0, 5.0, 5.0);
    if (overlap_box.status != axiom::StatusCode::Ok || !overlap_box.value.has_value()) {
        std::cerr << "overlap box for intersect mass test failed\n";
        return 1;
    }
    auto inter_ab =
        kernel.booleans().run(axiom::BooleanOp::Intersect, *box_a.value, *overlap_box.value, bool_opts);
    if (inter_ab.status != axiom::StatusCode::Ok || !inter_ab.value.has_value()) {
        std::cerr << "box intersect for mass_properties failed\n";
        return 1;
    }
    auto inter_mp = kernel.query().mass_properties(inter_ab.value->output);
    if (inter_mp.status != axiom::StatusCode::Ok || !inter_mp.value.has_value() ||
        std::abs(inter_mp.value->volume - 125.0) > 1e-3) {
        std::cerr << "intersect mass_properties should use AABB overlap volume for two boxes\n";
        return 1;
    }

    // Subtract + 左盒完全包含右盒：体积差（216 - 8 = 208），质心/惯性走差分占位。
    auto shell_outer = kernel.primitives().box({0.0, 0.0, 0.0}, 6.0, 6.0, 6.0);
    auto shell_inner = kernel.primitives().box({2.0, 2.0, 2.0}, 2.0, 2.0, 2.0);
    if (shell_outer.status != axiom::StatusCode::Ok || shell_inner.status != axiom::StatusCode::Ok ||
        !shell_outer.value.has_value() || !shell_inner.value.has_value()) {
        std::cerr << "nested boxes for subtract mass failed\n";
        return 1;
    }
    auto sub_nested =
        kernel.booleans().run(axiom::BooleanOp::Subtract, *shell_outer.value, *shell_inner.value, bool_opts);
    if (sub_nested.status != axiom::StatusCode::Ok || !sub_nested.value.has_value()) {
        std::cerr << "subtract nested boxes failed\n";
        return 1;
    }
    auto sub_n_mp = kernel.query().mass_properties(sub_nested.value->output);
    if (sub_n_mp.status != axiom::StatusCode::Ok || !sub_n_mp.value.has_value() ||
        std::abs(sub_n_mp.value->volume - 208.0) > 1e-3) {
        std::cerr << "nested subtract mass_properties volume mismatch\n";
        return 1;
    }
    const double exp_cx = (216.0 * 3.0 - 8.0 * 3.0) / 208.0;
    if (std::abs(sub_n_mp.value->centroid.x - exp_cx) > 1e-4 ||
        std::abs(sub_n_mp.value->centroid.y - exp_cx) > 1e-4 ||
        std::abs(sub_n_mp.value->centroid.z - exp_cx) > 1e-4) {
        std::cerr << "nested subtract mass_properties centroid mismatch\n";
        return 1;
    }

    auto bad_offset = kernel.modify().offset_body(*box_a.value, -6.0, {});
    if (bad_offset.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "expected failed inward offset\n";
        return 1;
    }
    auto bad_offset_diag = kernel.diagnostics().get(bad_offset.diagnostic_id);
    if (bad_offset_diag.status != axiom::StatusCode::Ok || !bad_offset_diag.value.has_value() ||
        !has_issue_stage(*bad_offset_diag.value, "modify.offset.self_intersection")) {
        std::cerr << "expected staged diagnostic for failed inward offset\n";
        return 1;
    }
    auto shell_too_thick = kernel.modify().shell_body(*box_a.value, {}, 4.9999995);
    if (shell_too_thick.status != axiom::StatusCode::OperationFailed) {
        std::cerr << "expected failed shell for tolerance-near thick wall\n";
        return 1;
    }
    auto shell_thick_diag = kernel.diagnostics().get(shell_too_thick.diagnostic_id);
    if (shell_thick_diag.status != axiom::StatusCode::Ok || !shell_thick_diag.value.has_value() ||
        !has_issue_stage(*shell_thick_diag.value, "modify.shell.cavity_tolerance")) {
        std::cerr << "expected staged diagnostic for shell cavity tolerance failure\n";
        return 1;
    }
    auto original_box_bbox_after_shell_fail = kernel.representation().bbox_of_body(*box_a.value);
    if (original_box_bbox_after_shell_fail.status != axiom::StatusCode::Ok ||
        !original_box_bbox_after_shell_fail.value.has_value() ||
        original_box_bbox_after_shell_fail.value->max.x != 10.0) {
        std::cerr << "shell failure should not mutate source body\n";
        return 1;
    }

    auto sweep_ok = kernel.sweeps().extrude({"profile"}, {0.0, 0.0, 1.0}, 5.0);
    auto sweep_bad = kernel.sweeps().extrude({"profile"}, {0.0, 0.0, 0.0}, 5.0);
    if (sweep_ok.status != axiom::StatusCode::Ok || !sweep_ok.value.has_value() ||
        sweep_bad.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "unexpected extrude behavior\n";
        return 1;
    }
    auto extrude_strict = kernel.validate().validate_topology(*sweep_ok.value, axiom::ValidationMode::Strict);
    if (extrude_strict.status != axiom::StatusCode::Ok) {
        std::cerr << "extrude result failed strict topology validation\n";
        return 1;
    }
    auto placeholder_extrude_mp = kernel.query().mass_properties(*sweep_ok.value);
    if (placeholder_extrude_mp.status != axiom::StatusCode::Ok || !placeholder_extrude_mp.value.has_value()) {
        std::cerr << "placeholder extrude mass_properties failed\n";
        return 1;
    }
    // 占位 extrude：1×1 截面 × 距离 5，体积 5，表面积 2+4×5。
    if (std::abs(placeholder_extrude_mp.value->volume - 5.0) > 1e-9 ||
        std::abs(placeholder_extrude_mp.value->area - 22.0) > 1e-9 ||
        std::abs(placeholder_extrude_mp.value->centroid.z - 2.5) > 1e-9) {
        std::cerr << "placeholder extrude mass_properties mismatch\n";
        return 1;
    }

    // 子午面多边形 revolve：真实三角化闭壳 BRep（8 面）+ Eberly 体积/表面积/质心/惯性；与 Pappus 解析体积近似一致。
    axiom::ProfileRef tri_meridian;
    tri_meridian.label = "tri_meridian";
    tri_meridian.polygon_xyz = {{1.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, {1.0, 0.0, 2.0}};
    const axiom::Axis3 axis_z {{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
    const double pi = 3.14159265358979323846;
    auto rev_mer = kernel.sweeps().revolve(tri_meridian, axis_z, pi);
    if (rev_mer.status != axiom::StatusCode::Ok || !rev_mer.value.has_value()) {
        std::cerr << "meridian polygon revolve failed\n";
        return 1;
    }
    auto rev_mer_strict = kernel.validate().validate_topology(*rev_mer.value, axiom::ValidationMode::Strict);
    if (rev_mer_strict.status != axiom::StatusCode::Ok) {
        std::cerr << "meridian revolve failed strict topology validation\n";
        return 1;
    }
    auto rev_mer_all = kernel.validate().validate_all(*rev_mer.value, axiom::ValidationMode::Strict);
    if (rev_mer_all.status != axiom::StatusCode::Ok) {
        std::cerr << "meridian revolve should pass strict validate_all\n";
        return 1;
    }
    auto rev_mer_shells = kernel.topology().query().shells_of_body(*rev_mer.value);
    if (rev_mer_shells.status != axiom::StatusCode::Ok || !rev_mer_shells.value.has_value() ||
        rev_mer_shells.value->size() != 1) {
        std::cerr << "meridian revolve expected one shell\n";
        return 1;
    }
    auto rev_mer_faces = kernel.topology().query().faces_of_shell(rev_mer_shells.value->front());
    if (rev_mer_faces.status != axiom::StatusCode::Ok || !rev_mer_faces.value.has_value() ||
        rev_mer_faces.value->size() != 8) {
        std::cerr << "meridian revolve expected 8 triangular faces (2n lateral + 2(n-2) caps)\n";
        return 1;
    }
    auto rev_mer_mp = kernel.query().mass_properties(*rev_mer.value);
    if (rev_mer_mp.status != axiom::StatusCode::Ok || !rev_mer_mp.value.has_value()) {
        std::cerr << "meridian revolve mass_properties failed\n";
        return 1;
    }
    const double area_m = 2.0;
    const double r_bar_m = 5.0 / 3.0;
    const double pappus_vol_m = area_m * r_bar_m * pi;
    if (std::abs(rev_mer_mp.value->volume - pappus_vol_m) > 0.25) {
        std::cerr << "meridian revolve polyhedral volume should be near Pappus analytic volume\n";
        return 1;
    }
    if (rev_mer_mp.value->area <= 0.0 || rev_mer_mp.value->inertia[0] <= 0.0 ||
        rev_mer_mp.value->inertia[4] <= 0.0 || rev_mer_mp.value->inertia[8] <= 0.0) {
        std::cerr << "meridian revolve should report positive area and principal inertia diagonals\n";
        return 1;
    }

    // 非子午面（轮廓平面不含旋转轴）：回退 bbox 壳，质量属性仍用 Pappus。
    axiom::ProfileRef tri_xy;
    tri_xy.label = "tri_xy";
    tri_xy.polygon_xyz = {{1.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, {1.0, 2.0, 0.0}};
    auto rev_xy = kernel.sweeps().revolve(tri_xy, axis_z, pi);
    if (rev_xy.status != axiom::StatusCode::Ok || !rev_xy.value.has_value()) {
        std::cerr << "non-meridian revolve failed\n";
        return 1;
    }
    auto rev_xy_strict = kernel.validate().validate_topology(*rev_xy.value, axiom::ValidationMode::Strict);
    if (rev_xy_strict.status != axiom::StatusCode::Ok) {
        std::cerr << "non-meridian revolve strict topology failed\n";
        return 1;
    }
    auto rev_xy_mp = kernel.query().mass_properties(*rev_xy.value);
    if (rev_xy_mp.status != axiom::StatusCode::Ok || !rev_xy_mp.value.has_value()) {
        std::cerr << "non-meridian revolve mass_properties failed\n";
        return 1;
    }
    const double r_bar_xy = std::sqrt((5.0 / 3.0) * (5.0 / 3.0) + (2.0 / 3.0) * (2.0 / 3.0));
    const double expected_xy_vol = area_m * r_bar_xy * pi;
    if (std::abs(rev_xy_mp.value->volume - expected_xy_vol) > 1e-5) {
        std::cerr << "non-meridian revolve volume should follow Pappus\n";
        return 1;
    }

    std::array<axiom::ProfileRef, 2> loft_profiles {};
    loft_profiles[0].label = "lof1";
    loft_profiles[1].label = "lof2";
    auto loft_body = kernel.sweeps().loft(std::span<const axiom::ProfileRef>(loft_profiles));
    if (loft_body.status != axiom::StatusCode::Ok || !loft_body.value.has_value()) {
        std::cerr << "loft failed\n";
        return 1;
    }
    auto loft_mp = kernel.query().mass_properties(*loft_body.value);
    if (loft_mp.status != axiom::StatusCode::Ok || !loft_mp.value.has_value()) {
        std::cerr << "loft mass_properties failed\n";
        return 1;
    }
    // 占位 loft：extent=max(2,n)=2，体积 8。
    if (std::abs(loft_mp.value->volume - 8.0) > 1e-9) {
        std::cerr << "loft volume should use cached extent^3\n";
        return 1;
    }

    // Stage-2 minimal: 矩形拉伸物化为三角剖分棱柱 BRep（顶/底各 2 三角 + 侧面 4×2，共 12 面；非工业「合并共面」语义）。
    axiom::ProfileRef rect;
    rect.label = "rect";
    rect.polygon_xyz = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {2.0, 1.0, 0.0}, {0.0, 1.0, 0.0}};
    auto prism = kernel.sweeps().extrude(rect, {0.0, 0.0, 1.0}, 3.0);
    if (prism.status != axiom::StatusCode::Ok || !prism.value.has_value()) {
        std::cerr << "polygon extrude failed\n";
        return 1;
    }
    auto prism_strict = kernel.validate().validate_topology(*prism.value, axiom::ValidationMode::Strict);
    if (prism_strict.status != axiom::StatusCode::Ok) {
        std::cerr << "polygon extrude failed strict topology validation\n";
        return 1;
    }
    auto prism_strict_all = kernel.validate().validate_all(*prism.value, axiom::ValidationMode::Strict);
    if (prism_strict_all.status != axiom::StatusCode::Ok) {
        std::cerr << "prism should pass strict validate_all including surface domain checks\n";
        return 1;
    }
    auto prism_manifold_std = kernel.validate().validate_manifold(*prism.value, axiom::ValidationMode::Standard);
    auto prism_manifold_strict = kernel.validate().validate_manifold(*prism.value, axiom::ValidationMode::Strict);
    if (prism_manifold_std.status != axiom::StatusCode::Ok || prism_manifold_strict.status != axiom::StatusCode::Ok) {
        std::cerr << "prism should pass validate_manifold Standard and Strict\n";
        return 1;
    }
    auto prism_shells = kernel.topology().query().shells_of_body(*prism.value);
    if (prism_shells.status != axiom::StatusCode::Ok || !prism_shells.value.has_value() || prism_shells.value->size() != 1) {
        std::cerr << "polygon extrude expected one shell\n";
        return 1;
    }
    auto prism_faces = kernel.topology().query().faces_of_shell(prism_shells.value->front());
    if (prism_faces.status != axiom::StatusCode::Ok || !prism_faces.value.has_value() || prism_faces.value->size() != 12) {
        std::cerr << "polygon rect extrude expected 12 triangulated prism faces\n";
        return 1;
    }

    auto box_edges = kernel.topology().query().edges_of_body(*box_a.value);
    if (box_edges.status != axiom::StatusCode::Ok || !box_edges.value.has_value() ||
        box_edges.value->empty()) {
        std::cerr << "expected edges on box for fillet/chamfer test\n";
        return 1;
    }
    std::vector<axiom::EdgeId> single_edge {box_edges.value->front()};
    auto fillet_ok = kernel.blends().fillet_edges(*box_a.value, single_edge, 0.5);
    if (fillet_ok.status != axiom::StatusCode::Ok || !fillet_ok.value.has_value()) {
        std::cerr << "fillet_edges failed\n";
        return 1;
    }
    auto fillet_ok_diag = kernel.diagnostics().get(fillet_ok.value->diagnostic_id);
    if (fillet_ok_diag.status != axiom::StatusCode::Ok || !fillet_ok_diag.value.has_value() ||
        !has_issue_stage(*fillet_ok_diag.value, "blend.fillet.placeholder")) {
        std::cerr << "expected blend.fillet.placeholder staged diagnostic for fillet\n";
        return 1;
    }
    if (!has_warning_code(fillet_ok.value->warnings, axiom::diag_codes::kBlendApproximatePlaceholder)) {
        std::cerr << "expected fillet placeholder capability warning\n";
        return 1;
    }
    auto chamfer_ok = kernel.blends().chamfer_edges(*box_a.value, single_edge, 0.3);
    if (chamfer_ok.status != axiom::StatusCode::Ok || !chamfer_ok.value.has_value()) {
        std::cerr << "chamfer_edges failed\n";
        return 1;
    }
    if (!has_warning_code(chamfer_ok.value->warnings, axiom::diag_codes::kBlendApproximatePlaceholder)) {
        std::cerr << "expected chamfer placeholder capability warning\n";
        return 1;
    }

    if (box_edges.value->size() < 2) {
        std::cerr << "expected at least two edges for multi-edge blend diagnostic test\n";
        return 1;
    }
    std::vector<axiom::EdgeId> two_edges {(*box_edges.value)[0], (*box_edges.value)[1]};
    auto fillet_multi = kernel.blends().fillet_edges(*box_a.value, two_edges, 0.4);
    if (fillet_multi.status != axiom::StatusCode::Ok || !fillet_multi.value.has_value()) {
        std::cerr << "fillet_edges (multi) failed\n";
        return 1;
    }
    if (!has_warning_code(fillet_multi.value->warnings, axiom::diag_codes::kBlendMultiEdgeCornerPlaceholder)) {
        std::cerr << "expected multi-edge fillet corner placeholder warning\n";
        return 1;
    }
    auto fillet_multi_diag = kernel.diagnostics().get(fillet_multi.value->diagnostic_id);
    if (fillet_multi_diag.status != axiom::StatusCode::Ok || !fillet_multi_diag.value.has_value() ||
        !has_issue_code(*fillet_multi_diag.value, axiom::diag_codes::kBlendMultiEdgeCornerPlaceholder) ||
        !has_issue_stage(*fillet_multi_diag.value, "blend.fillet.multi_edge")) {
        std::cerr << "expected diagnostic issue kBlendMultiEdgeCornerPlaceholder for multi-edge fillet\n";
        return 1;
    }
    auto chamfer_multi = kernel.blends().chamfer_edges(*box_a.value, two_edges, 0.25);
    if (chamfer_multi.status != axiom::StatusCode::Ok || !chamfer_multi.value.has_value()) {
        std::cerr << "chamfer_edges (multi) failed\n";
        return 1;
    }
    if (!has_warning_code(chamfer_multi.value->warnings, axiom::diag_codes::kBlendMultiEdgeCornerPlaceholder)) {
        std::cerr << "expected multi-edge chamfer corner placeholder warning\n";
        return 1;
    }
    auto chamfer_multi_diag = kernel.diagnostics().get(chamfer_multi.value->diagnostic_id);
    if (chamfer_multi_diag.status != axiom::StatusCode::Ok || !chamfer_multi_diag.value.has_value() ||
        !has_issue_code(*chamfer_multi_diag.value, axiom::diag_codes::kBlendMultiEdgeCornerPlaceholder)) {
        std::cerr << "expected diagnostic issue kBlendMultiEdgeCornerPlaceholder for multi-edge chamfer\n";
        return 1;
    }

    auto box_shells = kernel.topology().query().shells_of_body(*box_a.value);
    if (box_shells.status != axiom::StatusCode::Ok || !box_shells.value.has_value() || box_shells.value->empty()) {
        std::cerr << "expected shells on box_a for thicken test\n";
        return 1;
    }
    auto box_faces = kernel.topology().query().faces_of_shell(box_shells.value->front());
    if (box_faces.status != axiom::StatusCode::Ok || !box_faces.value.has_value() || box_faces.value->empty()) {
        std::cerr << "expected faces on box_a shell for thicken test\n";
        return 1;
    }
    auto thicken_ok = kernel.sweeps().thicken(box_faces.value->front(), 1.0);
    if (thicken_ok.status != axiom::StatusCode::Ok || !thicken_ok.value.has_value()) {
        std::cerr << "unexpected thicken failure\n";
        return 1;
    }
    auto thicken_strict = kernel.validate().validate_topology(*thicken_ok.value, axiom::ValidationMode::Strict);
    if (thicken_strict.status != axiom::StatusCode::Ok) {
        std::cerr << "thicken result failed strict topology validation\n";
        return 1;
    }
    auto thicken_mp = kernel.query().mass_properties(*thicken_ok.value);
    if (thicken_mp.status != axiom::StatusCode::Ok || !thicken_mp.value.has_value()) {
        std::cerr << "thicken mass_properties failed\n";
        return 1;
    }
    // 10×10 轴对齐面 × 厚度 1：面面积估计 100，体积缓存 100。
    if (std::abs(thicken_mp.value->volume - 100.0) > 1e-6) {
        std::cerr << "thicken volume should use face-area×thickness not pure bbox volume\n";
        return 1;
    }

    auto plane0 = kernel.surfaces().make_plane({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    auto plane1 = kernel.surfaces().make_plane({0.0, 0.0, 1.0}, {0.0, 0.0, 1.0});
    auto line = kernel.curves().make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    if (plane0.status != axiom::StatusCode::Ok || plane1.status != axiom::StatusCode::Ok ||
        line.status != axiom::StatusCode::Ok || !plane0.value.has_value() || !plane1.value.has_value() ||
        !line.value.has_value()) {
        std::cerr << "failed to create topology prerequisites\n";
        return 1;
    }

    auto txn = kernel.topology().begin_transaction();
    auto v0 = txn.create_vertex({0.0, 0.0, 0.0});
    auto v1 = txn.create_vertex({1.0, 0.0, 0.0});
    if (v0.status != axiom::StatusCode::Ok || v1.status != axiom::StatusCode::Ok ||
        !v0.value.has_value() || !v1.value.has_value()) {
        std::cerr << "failed to create vertices\n";
        return 1;
    }

    auto edge = txn.create_edge(*line.value, *v0.value, *v1.value);
    if (edge.status != axiom::StatusCode::Ok || !edge.value.has_value()) {
        std::cerr << "failed to create edge\n";
        return 1;
    }

    auto coedge = txn.create_coedge(*edge.value, false);
    if (coedge.status != axiom::StatusCode::Ok || !coedge.value.has_value()) {
        std::cerr << "failed to create coedge\n";
        return 1;
    }

    const std::array<axiom::CoedgeId, 1> coedges {*coedge.value};
    auto loop = txn.create_loop(coedges);
    if (loop.status != axiom::StatusCode::Ok || !loop.value.has_value()) {
        std::cerr << "failed to create loop\n";
        return 1;
    }

    auto face = txn.create_face(*plane0.value, *loop.value, {});
    if (face.status != axiom::StatusCode::Ok || !face.value.has_value()) {
        std::cerr << "failed to create face\n";
        return 1;
    }

    auto replace_face = kernel.modify().replace_face(*box_a.value, *face.value, *plane1.value);
    auto delete_face_and_heal = kernel.modify().delete_face_and_heal(*box_a.value, *face.value);
    if (replace_face.status != axiom::StatusCode::Ok || !replace_face.value.has_value() ||
        delete_face_and_heal.status != axiom::StatusCode::Ok || !delete_face_and_heal.value.has_value()) {
        std::cerr << "failed modify operations on face\n";
        return 1;
    }

    auto delete_face_diag = kernel.diagnostics().get(delete_face_and_heal.value->diagnostic_id);
    if (delete_face_diag.status != axiom::StatusCode::Ok || !delete_face_diag.value.has_value() ||
        !has_issue_code(*delete_face_diag.value, axiom::diag_codes::kHealFeatureRemovedWarning) ||
        !has_issue_stage(*delete_face_diag.value, "modify.delete_face.heal")) {
        std::cerr << "expected warning diagnostic for delete_face_and_heal\n";
        return 1;
    }

    auto replace_face_sources = kernel.topology().query().source_faces_of_body(replace_face.value->output);
    auto delete_face_sources = kernel.topology().query().source_faces_of_body(delete_face_and_heal.value->output);
    auto replace_face_bodies = kernel.topology().query().source_bodies_of_body(replace_face.value->output);
    auto replace_face_shells = kernel.topology().query().source_shells_of_body(replace_face.value->output);
    auto delete_face_shells = kernel.topology().query().source_shells_of_body(delete_face_and_heal.value->output);
    auto replace_face_owned_shells = kernel.topology().query().shells_of_body(replace_face.value->output);
    auto delete_face_owned_shells = kernel.topology().query().shells_of_body(delete_face_and_heal.value->output);
    auto replace_face_owned_faces = replace_face_owned_shells.status == axiom::StatusCode::Ok && replace_face_owned_shells.value.has_value() &&
                                            replace_face_owned_shells.value->size() == 1
                                        ? kernel.topology().query().faces_of_shell(replace_face_owned_shells.value->front())
                                        : axiom::Result<std::vector<axiom::FaceId>> {};
    auto delete_face_owned_faces = delete_face_owned_shells.status == axiom::StatusCode::Ok && delete_face_owned_shells.value.has_value() &&
                                           delete_face_owned_shells.value->size() == 1
                                       ? kernel.topology().query().faces_of_shell(delete_face_owned_shells.value->front())
                                       : axiom::Result<std::vector<axiom::FaceId>> {};
    if (replace_face_sources.status != axiom::StatusCode::Ok || !replace_face_sources.value.has_value() ||
        delete_face_sources.status != axiom::StatusCode::Ok || !delete_face_sources.value.has_value() ||
        replace_face_bodies.status != axiom::StatusCode::Ok || !replace_face_bodies.value.has_value() ||
        replace_face_shells.status != axiom::StatusCode::Ok || !replace_face_shells.value.has_value() ||
        delete_face_shells.status != axiom::StatusCode::Ok || !delete_face_shells.value.has_value() ||
        replace_face_owned_shells.status != axiom::StatusCode::Ok || !replace_face_owned_shells.value.has_value() ||
        delete_face_owned_shells.status != axiom::StatusCode::Ok || !delete_face_owned_shells.value.has_value() ||
        replace_face_owned_faces.status != axiom::StatusCode::Ok || !replace_face_owned_faces.value.has_value() ||
        delete_face_owned_faces.status != axiom::StatusCode::Ok || !delete_face_owned_faces.value.has_value() ||
        replace_face_sources.value->size() != 1 || replace_face_sources.value->front().value != face.value->value ||
        delete_face_sources.value->size() != 1 || delete_face_sources.value->front().value != face.value->value ||
        replace_face_bodies.value->size() != 1 || replace_face_bodies.value->front().value != box_a.value->value ||
        !replace_face_shells.value->empty() ||
        !delete_face_shells.value->empty() ||
        replace_face_owned_shells.value->size() != 1 ||
        delete_face_owned_shells.value->size() != 1 ||
        replace_face_owned_faces.value->size() != 6 ||
        delete_face_owned_faces.value->size() != 6) {
        std::cerr << "modify result provenance is unexpected\n";
        return 1;
    }

    auto mp_box_a_ref = kernel.query().mass_properties(*box_a.value);
    auto mp_replace_face_body = kernel.query().mass_properties(replace_face.value->output);
    if (mp_box_a_ref.status != axiom::StatusCode::Ok || !mp_box_a_ref.value.has_value() ||
        mp_replace_face_body.status != axiom::StatusCode::Ok || !mp_replace_face_body.value.has_value() ||
        std::abs(mp_box_a_ref.value->volume - mp_replace_face_body.value->volume) > 1e-3) {
        std::cerr << "replace_face Modified body should inherit source mass when bbox unchanged\n";
        return 1;
    }

    auto first_offset = kernel.modify().offset_body(*box_a.value, 1.0, {});
    if (first_offset.status != axiom::StatusCode::Ok || !first_offset.value.has_value()) {
        std::cerr << "failed to create first derived offset body\n";
        return 1;
    }
    auto second_offset = kernel.modify().offset_body(first_offset.value->output, 0.5, {});
    if (second_offset.status != axiom::StatusCode::Ok || !second_offset.value.has_value()) {
        std::cerr << "failed to create second derived offset body\n";
        return 1;
    }

    auto first_offset_shells = kernel.topology().query().shells_of_body(first_offset.value->output);
    auto second_offset_shells = kernel.topology().query().shells_of_body(second_offset.value->output);
    auto second_offset_source_shells = kernel.topology().query().source_shells_of_body(second_offset.value->output);
    if (first_offset_shells.status != axiom::StatusCode::Ok || !first_offset_shells.value.has_value() ||
        second_offset_shells.status != axiom::StatusCode::Ok || !second_offset_shells.value.has_value() ||
        second_offset_source_shells.status != axiom::StatusCode::Ok || !second_offset_source_shells.value.has_value() ||
        first_offset_shells.value->size() != 1 || second_offset_shells.value->size() != 1 ||
        second_offset_source_shells.value->size() != 1 ||
        second_offset_source_shells.value->front().value != first_offset_shells.value->front().value) {
        std::cerr << "second-generation offset did not inherit source shell provenance as expected\n";
        return 1;
    }

    auto second_offset_owned_faces = kernel.topology().query().faces_of_shell(second_offset_shells.value->front());
    auto second_offset_source_faces = kernel.topology().query().source_faces_of_shell(second_offset_shells.value->front());
    if (second_offset_owned_faces.status != axiom::StatusCode::Ok || !second_offset_owned_faces.value.has_value() ||
        second_offset_source_faces.status != axiom::StatusCode::Ok || !second_offset_source_faces.value.has_value() ||
        second_offset_owned_faces.value->size() != 6 || second_offset_source_faces.value->size() != 6) {
        std::cerr << "second-generation offset shell layout is unexpected\n";
        return 1;
    }
    const bool cloned_face_ids_reused = second_offset_owned_faces.value->front().value == second_offset_source_faces.value->front().value;
    if (cloned_face_ids_reused) {
        std::cerr << "second-generation offset should clone source shell topology instead of reusing owned face ids\n";
        return 1;
    }

    auto degrade_source_txn = kernel.topology().begin_transaction();
    auto degrade_source = degrade_source_txn.delete_face(second_offset_source_faces.value->front());
    if (degrade_source.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to delete one source face for degraded-source reconstruction case\n";
        return 1;
    }
    auto degrade_commit = degrade_source_txn.commit();
    if (degrade_commit.status != axiom::StatusCode::Ok) {
        std::cerr << "failed to commit degraded-source topology change\n";
        return 1;
    }

    auto third_offset = kernel.modify().offset_body(second_offset.value->output, 0.25, {});
    if (third_offset.status != axiom::StatusCode::Ok || !third_offset.value.has_value()) {
        std::cerr << "third-generation offset should still succeed when part of source topology is missing\n";
        return 1;
    }
    auto third_offset_shells = kernel.topology().query().shells_of_body(third_offset.value->output);
    auto third_offset_standard = kernel.validate().validate_topology(third_offset.value->output, axiom::ValidationMode::Standard);
    if (third_offset_shells.status != axiom::StatusCode::Ok || !third_offset_shells.value.has_value() ||
        third_offset_shells.value->size() != 1 || third_offset_standard.status != axiom::StatusCode::Ok) {
        std::cerr << "degraded-source reconstruction did not produce a valid owned topology fallback\n";
        return 1;
    }

    return 0;
}
