#include "axiom/rep/representation_conversion_service.h"

#include <fstream>
#include <limits>

#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/rep/representation_internal_utils.h"

namespace axiom {

RepresentationService::RepresentationService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<RepKind> RepresentationService::kind_of_body(BodyId body_id) const {
    const auto it = state_->bodies.find(body_id.value);
    if (it == state_->bodies.end()) {
        return detail::invalid_input_result<RepKind>(
            *state_, diag_codes::kCoreInvalidHandle,
            "表示查询失败：目标体不存在", "表示查询失败");
    }
    return ok_result(it->second.rep_kind, state_->create_diagnostic("已查询表示类型"));
}

Result<BoundingBox> RepresentationService::bbox_of_body(BodyId body_id) const {
    const auto it = state_->bodies.find(body_id.value);
    if (it == state_->bodies.end()) {
        return detail::invalid_input_result<BoundingBox>(
            *state_, diag_codes::kCoreInvalidHandle,
            "包围盒查询失败：目标体不存在", "包围盒查询失败");
    }
    return ok_result(it->second.bbox, state_->create_diagnostic("已查询体包围盒"));
}

Result<bool> RepresentationService::classify_point(BodyId body_id, const Point3& point) const {
    const auto it = state_->bodies.find(body_id.value);
    if (it == state_->bodies.end()) {
        return detail::invalid_input_result<bool>(
            *state_, diag_codes::kCoreInvalidHandle,
            "点分类失败：目标体不存在", "点分类失败");
    }
    const auto& bbox = it->second.bbox;
    if (!detail::is_valid_bbox(bbox)) {
        return detail::failed_result<bool>(
            *state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
            "点分类失败：目标体包围盒无效", "点分类失败");
    }
    const auto tol = std::max(state_->config.tolerance.linear, 0.0);
    const bool inside = point.x >= bbox.min.x - tol && point.x <= bbox.max.x + tol &&
                        point.y >= bbox.min.y - tol && point.y <= bbox.max.y + tol &&
                        point.z >= bbox.min.z - tol && point.z <= bbox.max.z + tol;
    return ok_result(inside, state_->create_diagnostic("已完成点分类"));
}

Result<std::vector<bool>> RepresentationService::classify_points_batch(
    BodyId body_id, std::span<const Point3> points) const {
    if (points.empty()) {
        return detail::invalid_input_result<std::vector<bool>>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "批量点分类失败：输入点集合为空", "批量点分类失败");
    }
    std::vector<bool> out;
    out.reserve(points.size());
    for (const auto& p : points) {
        const auto single = classify_point(body_id, p);
        if (single.status != StatusCode::Ok || !single.value.has_value()) {
            return error_result<std::vector<bool>>(single.status, single.diagnostic_id);
        }
        out.push_back(*single.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已完成批量点分类"));
}

Result<Scalar> RepresentationService::distance_to_body(BodyId body_id, const Point3& point) const {
    const auto it = state_->bodies.find(body_id.value);
    if (it == state_->bodies.end()) {
        return detail::invalid_input_result<Scalar>(
            *state_, diag_codes::kCoreInvalidHandle,
            "点到体距离计算失败：目标体不存在", "距离计算失败");
    }
    const auto& bbox = it->second.bbox;
    if (!detail::is_valid_bbox(bbox)) {
        return detail::failed_result<Scalar>(
            *state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
            "点到体距离计算失败：目标体包围盒无效", "距离计算失败");
    }
    const auto dx = detail::axis_distance(point.x, bbox.min.x, bbox.max.x);
    const auto dy = detail::axis_distance(point.y, bbox.min.y, bbox.max.y);
    const auto dz = detail::axis_distance(point.z, bbox.min.z, bbox.max.z);
    return ok_result<Scalar>(std::sqrt(dx * dx + dy * dy + dz * dz), state_->create_diagnostic("已完成点到体距离计算"));
}

Result<std::vector<Scalar>> RepresentationService::distances_to_body_batch(
    BodyId body_id, std::span<const Point3> points) const {
    if (points.empty()) {
        return detail::invalid_input_result<std::vector<Scalar>>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "批量点到体距离计算失败：输入点集合为空", "批量点到体距离计算失败");
    }
    std::vector<Scalar> out;
    out.reserve(points.size());
    for (const auto& p : points) {
        const auto single = distance_to_body(body_id, p);
        if (single.status != StatusCode::Ok || !single.value.has_value()) {
            return error_result<std::vector<Scalar>>(single.status, single.diagnostic_id);
        }
        out.push_back(*single.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已完成批量点到体距离计算"));
}

RepresentationConversionService::RepresentationConversionService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<MeshId> RepresentationConversionService::brep_to_mesh(BodyId body_id, const TessellationOptions& options) {
    if (!detail::has_body(*state_, body_id)) {
        return detail::invalid_input_result<MeshId>(
            *state_, diag_codes::kCoreInvalidHandle,
            "BRep 转网格失败：目标体不存在", "BRep 转网格失败");
    }
    if (!detail::has_valid_tessellation_options(options)) {
        return detail::invalid_input_result<MeshId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "BRep 转网格失败：三角化误差参数必须为正数", "BRep 转网格失败");
    }

    const auto body_it = state_->bodies.find(body_id.value);
    if (!detail::is_valid_bbox(body_it->second.bbox)) {
        return detail::failed_result<MeshId>(
            *state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
            "BRep 转网格失败：目标体包围盒无效", "BRep 转网格失败");
    }

    const auto slices_per_face = detail::tessellation_slices_per_face(options);

    const auto id = MeshId {state_->allocate_id()};
    detail::MeshRecord mesh;
    mesh.source_body = body_id;
    mesh.label = "mesh_from_brep";
    mesh.bbox = body_it->second.bbox;
    mesh.vertices = detail::bbox_corners(body_it->second.bbox);
    mesh.indices = detail::triangulate_bbox(slices_per_face);
    state_->meshes.emplace(id.value, std::move(mesh));
    return ok_result(id, state_->create_diagnostic("已完成BRep转网格"));
}

Result<BodyId> RepresentationConversionService::mesh_to_brep(MeshId mesh_id) {
    const auto it = state_->meshes.find(mesh_id.value);
    if (it == state_->meshes.end()) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreInvalidHandle,
            "网格转BRep失败：目标网格不存在", "网格转BRep失败");
    }
    if (it->second.vertices.empty()) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "网格转BRep失败：网格顶点为空", "网格转BRep失败");
    }
    if (!it->second.indices.empty() && (it->second.indices.size() % 3) != 0) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "网格转BRep失败：网格三角索引数量非法", "网格转BRep失败");
    }
    if (detail::has_out_of_range_indices(it->second.vertices, it->second.indices)) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "网格转BRep失败：网格索引超出顶点范围", "网格转BRep失败");
    }
    if (detail::has_degenerate_triangles(it->second.vertices, it->second.indices)) {
        return detail::failed_result<BodyId>(
            *state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
            "网格转BRep失败：网格包含退化三角形", "网格转BRep失败");
    }
    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::ExactBRep;
    record.label = "brep_from_mesh";
    record.bbox = detail::is_valid_bbox(it->second.bbox) ? it->second.bbox : detail::mesh_bbox_from_vertices(it->second.vertices);
    state_->bodies.emplace(body_id.value, record);
    return ok_result(body_id, state_->create_diagnostic("已完成网格转BRep"));
}

Result<MeshId> RepresentationConversionService::implicit_to_mesh(ImplicitFieldId field_id, const TessellationOptions& options) {
    if (field_id.value == 0) {
        return detail::invalid_input_result<MeshId>(
            *state_, diag_codes::kCoreInvalidHandle,
            "隐式体转网格失败：隐式场句柄无效", "隐式体转网格失败");
    }
    if (!detail::has_valid_tessellation_options(options)) {
        return detail::invalid_input_result<MeshId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "隐式体转网格失败：三角化误差参数必须为正数", "隐式体转网格失败");
    }

    const auto extent = std::max(options.chordal_error, std::numeric_limits<Scalar>::epsilon()) * 10.0;
    const auto id = MeshId {state_->allocate_id()};
    detail::MeshRecord mesh;
    mesh.source_body = BodyId {0};
    mesh.label = "mesh_from_implicit";
    mesh.bbox = detail::make_bbox({-extent, -extent, -extent}, {extent, extent, extent});
    mesh.vertices = detail::bbox_corners(mesh.bbox);
    mesh.indices = detail::triangulate_bbox(detail::tessellation_slices_per_face(options));
    state_->meshes.emplace(id.value, std::move(mesh));
    return ok_result(id, state_->create_diagnostic("已完成隐式体转网格"));
}

Result<std::vector<MeshId>> RepresentationConversionService::brep_to_mesh_batch(
    std::span<const BodyId> body_ids, const TessellationOptions& options) {
    if (body_ids.empty()) {
        return detail::invalid_input_result<std::vector<MeshId>>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "批量 BRep 转网格失败：输入体集合为空", "批量 BRep 转网格失败");
    }
    std::vector<MeshId> outputs;
    outputs.reserve(body_ids.size());
    for (const auto body_id : body_ids) {
        auto converted = brep_to_mesh(body_id, options);
        if (converted.status != StatusCode::Ok || !converted.value.has_value()) {
            return error_result<std::vector<MeshId>>(converted.status, converted.diagnostic_id);
        }
        outputs.push_back(*converted.value);
    }
    return ok_result(std::move(outputs), state_->create_diagnostic("已完成批量 BRep 转网格"));
}

Result<std::vector<BodyId>> RepresentationConversionService::mesh_to_brep_batch(std::span<const MeshId> mesh_ids) {
    if (mesh_ids.empty()) {
        return detail::invalid_input_result<std::vector<BodyId>>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "批量网格转BRep失败：输入网格集合为空", "批量网格转BRep失败");
    }
    std::vector<BodyId> outputs;
    outputs.reserve(mesh_ids.size());
    for (const auto mesh_id : mesh_ids) {
        auto converted = mesh_to_brep(mesh_id);
        if (converted.status != StatusCode::Ok || !converted.value.has_value()) {
            return error_result<std::vector<BodyId>>(converted.status, converted.diagnostic_id);
        }
        outputs.push_back(*converted.value);
    }
    return ok_result(std::move(outputs), state_->create_diagnostic("已完成批量网格转BRep"));
}

Result<std::uint64_t> RepresentationConversionService::mesh_vertex_count(MeshId mesh_id) const {
    const auto it = state_->meshes.find(mesh_id.value);
    if (it == state_->meshes.end()) {
        return detail::invalid_input_result<std::uint64_t>(
            *state_, diag_codes::kCoreInvalidHandle,
            "网格顶点数量查询失败：目标网格不存在", "网格统计查询失败");
    }
    return ok_result<std::uint64_t>(
        static_cast<std::uint64_t>(it->second.vertices.size()),
        state_->create_diagnostic("已查询网格顶点数量"));
}

Result<std::uint64_t> RepresentationConversionService::mesh_index_count(MeshId mesh_id) const {
    const auto it = state_->meshes.find(mesh_id.value);
    if (it == state_->meshes.end()) {
        return detail::invalid_input_result<std::uint64_t>(
            *state_, diag_codes::kCoreInvalidHandle,
            "网格索引数量查询失败：目标网格不存在", "网格统计查询失败");
    }
    return ok_result<std::uint64_t>(
        static_cast<std::uint64_t>(it->second.indices.size()),
        state_->create_diagnostic("已查询网格索引数量"));
}

Result<std::uint64_t> RepresentationConversionService::mesh_triangle_count(MeshId mesh_id) const {
    const auto it = state_->meshes.find(mesh_id.value);
    if (it == state_->meshes.end()) {
        return detail::invalid_input_result<std::uint64_t>(
            *state_, diag_codes::kCoreInvalidHandle,
            "网格三角形数量查询失败：目标网格不存在", "网格统计查询失败");
    }
    return ok_result<std::uint64_t>(
        static_cast<std::uint64_t>(it->second.indices.size() / 3),
        state_->create_diagnostic("已查询网格三角形数量"));
}

Result<std::uint64_t> RepresentationConversionService::mesh_connected_components(MeshId mesh_id) const {
    const auto it = state_->meshes.find(mesh_id.value);
    if (it == state_->meshes.end()) {
        return detail::invalid_input_result<std::uint64_t>(
            *state_, diag_codes::kCoreInvalidHandle,
            "网格连通分量查询失败：目标网格不存在", "网格统计查询失败");
    }
    return ok_result<std::uint64_t>(
        detail::mesh_connected_components(it->second.indices, it->second.vertices.size()),
        state_->create_diagnostic("已查询网格连通分量数量"));
}

Result<bool> RepresentationConversionService::mesh_has_out_of_range_indices(MeshId mesh_id) const {
    const auto it = state_->meshes.find(mesh_id.value);
    if (it == state_->meshes.end()) {
        return detail::invalid_input_result<bool>(
            *state_, diag_codes::kCoreInvalidHandle,
            "网格索引合法性查询失败：目标网格不存在", "网格统计查询失败");
    }
    return ok_result(
        detail::has_out_of_range_indices(it->second.vertices, it->second.indices),
        state_->create_diagnostic("已查询网格索引合法性"));
}

Result<bool> RepresentationConversionService::mesh_has_degenerate_triangles(MeshId mesh_id) const {
    const auto it = state_->meshes.find(mesh_id.value);
    if (it == state_->meshes.end()) {
        return detail::invalid_input_result<bool>(
            *state_, diag_codes::kCoreInvalidHandle,
            "网格退化三角形查询失败：目标网格不存在", "网格统计查询失败");
    }
    return ok_result(
        detail::has_degenerate_triangles(it->second.vertices, it->second.indices),
        state_->create_diagnostic("已查询网格退化三角形状态"));
}

Result<MeshInspectionReport> RepresentationConversionService::inspect_mesh(MeshId mesh_id) const {
    const auto it = state_->meshes.find(mesh_id.value);
    if (it == state_->meshes.end()) {
        return detail::invalid_input_result<MeshInspectionReport>(
            *state_, diag_codes::kCoreInvalidHandle,
            "网格检查失败：目标网格不存在", "网格统计查询失败");
    }
    MeshInspectionReport report;
    report.vertex_count = static_cast<std::uint64_t>(it->second.vertices.size());
    report.index_count = static_cast<std::uint64_t>(it->second.indices.size());
    report.triangle_count = report.index_count / 3;
    report.is_indexed = report.index_count > 0;
    report.has_out_of_range_indices = detail::has_out_of_range_indices(it->second.vertices, it->second.indices);
    report.has_degenerate_triangles = detail::has_degenerate_triangles(it->second.vertices, it->second.indices);
    report.connected_components =
        detail::mesh_connected_components(it->second.indices, it->second.vertices.size());
    return ok_result(report, state_->create_diagnostic("已完成网格统计检查"));
}

Result<void> RepresentationConversionService::export_mesh_report_json(
    MeshId mesh_id, std::string_view path) const {
    if (path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "网格报告导出失败：输出路径为空", "网格报告导出失败");
    }
    const auto report_result = inspect_mesh(mesh_id);
    if (report_result.status != StatusCode::Ok || !report_result.value.has_value()) {
        return detail::failed_void(
            *state_, report_result.status, diag_codes::kCoreInvalidHandle,
            "网格报告导出失败：目标网格不存在或报告不可用", "网格报告导出失败");
    }
    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "网格报告导出失败：无法打开输出文件", "网格报告导出失败");
    }
    const auto &r = *report_result.value;
    out << "{";
    out << "\"vertex_count\":" << r.vertex_count << ",";
    out << "\"index_count\":" << r.index_count << ",";
    out << "\"triangle_count\":" << r.triangle_count << ",";
    out << "\"connected_components\":" << r.connected_components << ",";
    out << "\"is_indexed\":" << (r.is_indexed ? "true" : "false") << ",";
    out << "\"has_out_of_range_indices\":"
        << (r.has_out_of_range_indices ? "true" : "false") << ",";
    out << "\"has_degenerate_triangles\":"
        << (r.has_degenerate_triangles ? "true" : "false");
    out << "}";
    return ok_void(state_->create_diagnostic("已导出网格统计报告"));
}

}  // namespace axiom
