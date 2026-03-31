#include "axiom/rep/representation_conversion_service.h"

#include <fstream>
#include <limits>

#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/rep/representation_internal_utils.h"

namespace {

std::string json_escape_strategy(std::string_view s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        if (c == '"' || c == '\\') {
            o.push_back('\\');
        }
        o.push_back(static_cast<char>(c));
    }
    return o;
}

}  // namespace

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
    const auto tess_budget_digest = detail::tessellation_budget_digest_json(options);

    const auto body_it = state_->bodies.find(body_id.value);
    if (!detail::is_valid_bbox(body_it->second.bbox)) {
        return detail::failed_result<MeshId>(
            *state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
            "BRep 转网格失败：目标体包围盒无效", "BRep 转网格失败");
    }

    const auto& body = body_it->second;
    const auto cache_key = detail::tessellation_cache_key(body, options);
    const auto cache_it = state_->tessellation_cache.find(cache_key);
    if (cache_it != state_->tessellation_cache.end()) {
        const auto mesh_it = state_->meshes.find(cache_it->second.value);
        if (mesh_it != state_->meshes.end()) {
            return ok_result(cache_it->second, state_->create_diagnostic("已命中三角化缓存"));
        }
        // stale cache entry
        state_->tessellation_cache.erase(cache_it);
    }

    detail::MeshRecord mesh;
    mesh.source_body = body_id;

    const auto is_analytic_primitive =
        body.kind == detail::BodyKind::Box ||
        body.kind == detail::BodyKind::Sphere ||
        body.kind == detail::BodyKind::Cylinder ||
        body.kind == detail::BodyKind::Cone ||
        body.kind == detail::BodyKind::Torus;

    // Prefer Topo-driven tessellation when owned topology exists (industrial path for derived bodies),
    // but keep analytic primitive tessellation for primitives (it is curvature-sensitive and honors options).
    if (!is_analytic_primitive && !body.shells.empty()) {
        detail::MeshRecord assembled;
        assembled.source_body = body_id;
        assembled.label = "mesh_from_brep_owned_faces";
        assembled.bbox = body.bbox;

        for (const auto shell_id : body.shells) {
            const auto shell_it = state_->shells.find(shell_id.value);
            if (shell_it == state_->shells.end()) {
                continue;
            }
            for (const auto face_id : shell_it->second.faces) {
                const auto face_key = detail::face_tessellation_cache_key(*state_, face_id, options);
                auto cache_it = state_->face_tessellation_cache.find(face_key);
                if (cache_it != state_->face_tessellation_cache.end()) {
                    const auto mesh_it = state_->meshes.find(cache_it->second.value);
                    if (mesh_it == state_->meshes.end()) {
                        state_->face_tessellation_cache.erase(cache_it);
                        cache_it = state_->face_tessellation_cache.end();
                    }
                }

                MeshId face_mesh_id{};
                if (cache_it != state_->face_tessellation_cache.end()) {
                    face_mesh_id = cache_it->second;
                } else {
                    auto face_mesh = detail::tessellate_face(*state_, face_id, options);
                    if (face_mesh.vertices.empty() || face_mesh.indices.empty()) {
                        // If face tessellation fails, fall back to analytic primitive or bbox proxy below.
                        assembled.vertices.clear();
                        assembled.indices.clear();
                        goto FALLBACK_ANALYTIC_OR_BBOX;
                    }
                    face_mesh.source_body = body_id;
                    face_mesh.label = "mesh_face_" + std::to_string(face_id.value);
                    const auto new_id = MeshId{state_->allocate_id()};
                    state_->meshes.emplace(new_id.value, std::move(face_mesh));
                    state_->face_tessellation_cache[face_key] = new_id;
                    face_mesh_id = new_id;
                }

                const auto face_mesh_it = state_->meshes.find(face_mesh_id.value);
                if (face_mesh_it == state_->meshes.end()) {
                    assembled.vertices.clear();
                    assembled.indices.clear();
                    goto FALLBACK_ANALYTIC_OR_BBOX;
                }
                const auto& fm = face_mesh_it->second;
                const auto base = static_cast<Index>(assembled.vertices.size());
                assembled.vertices.insert(assembled.vertices.end(), fm.vertices.begin(), fm.vertices.end());
                if (options.compute_normals && !fm.normals.empty()) {
                    assembled.normals.insert(assembled.normals.end(), fm.normals.begin(), fm.normals.end());
                }
                if (!fm.texcoords.empty()) {
                    assembled.texcoords.insert(assembled.texcoords.end(), fm.texcoords.begin(), fm.texcoords.end());
                }
                assembled.indices.reserve(assembled.indices.size() + fm.indices.size());
                for (const auto idx : fm.indices) {
                    assembled.indices.push_back(base + idx);
                }
            }
        }

        if (!assembled.vertices.empty() && !assembled.indices.empty()) {
            // Improve connectivity across faces.
            detail::weld_mesh_vertices(assembled, options.compute_normals);
            assembled.tessellation_budget_digest = tess_budget_digest;
            assembled.tessellation_strategy = "owned_topo_welded";
            mesh = std::move(assembled);
        } else {
            // no usable faces, fall back
            goto FALLBACK_ANALYTIC_OR_BBOX;
        }
    } else {
FALLBACK_ANALYTIC_OR_BBOX:
        switch (body.kind) {
            case detail::BodyKind::Box:
                mesh = detail::tessellate_box(body, options);
                mesh.tessellation_strategy = "primitive_box";
                break;
            case detail::BodyKind::Sphere:
                mesh = detail::tessellate_sphere(body, options);
                mesh.tessellation_strategy = "primitive_sphere";
                break;
            case detail::BodyKind::Cylinder:
                mesh = detail::tessellate_cylinder(body, options);
                mesh.tessellation_strategy = "primitive_cylinder";
                break;
            case detail::BodyKind::Cone:
                mesh = detail::tessellate_cone(body, options);
                mesh.tessellation_strategy = "primitive_cone";
                break;
            case detail::BodyKind::Torus:
                mesh = detail::tessellate_torus(body, options);
                mesh.tessellation_strategy = "primitive_torus";
                break;
            default: {
                // Conservative fallback: bbox proxy mesh (keeps pipeline alive).
                const auto slices_per_face = detail::tessellation_slices_per_face(options);
                mesh.label = "mesh_from_brep_bbox_proxy";
                mesh.tessellation_strategy = "bbox_proxy";
                mesh.bbox = body.bbox;
                mesh.vertices = detail::bbox_corners(body.bbox);
                mesh.indices = detail::triangulate_bbox(slices_per_face);
                if (options.compute_normals) {
                    mesh.normals.assign(mesh.vertices.size(), Vec3{0.0, 0.0, 1.0});
                }
                mesh.texcoords.assign(mesh.vertices.size(), Point2{0.0, 0.0});
                break;
            }
        }
        mesh.tessellation_budget_digest = tess_budget_digest;
    }
    mesh.source_body = body_id;

    const auto id = MeshId {state_->allocate_id()};
    state_->meshes.emplace(id.value, std::move(mesh));
    state_->tessellation_cache.emplace(cache_key, id);
    return ok_result(id, state_->create_diagnostic("已完成BRep转网格"));
}

Result<MeshId> RepresentationConversionService::brep_to_mesh_local(
    BodyId body_id, std::span<const FaceId> dirty_faces, const TessellationOptions& options) {
    if (!detail::has_body(*state_, body_id)) {
        return detail::invalid_input_result<MeshId>(
            *state_, diag_codes::kCoreInvalidHandle,
            "局部BRep转网格失败：目标体不存在", "局部BRep转网格失败");
    }
    if (dirty_faces.empty()) {
        return detail::invalid_input_result<MeshId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "局部BRep转网格失败：dirty faces 不能为空", "局部BRep转网格失败");
    }
    if (!detail::has_valid_tessellation_options(options)) {
        return detail::invalid_input_result<MeshId>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "局部BRep转网格失败：三角化误差参数必须为正数", "局部BRep转网格失败");
    }
    const auto tess_budget_digest = detail::tessellation_budget_digest_json(options);

    // If body has no owned topology, fallback to full tessellation.
    const auto body_it = state_->bodies.find(body_id.value);
    if (body_it == state_->bodies.end() || body_it->second.shells.empty()) {
        return brep_to_mesh(body_id, options);
    }

    // Validate that all dirty faces belong to this body.
    for (const auto face_id : dirty_faces) {
        const auto face_it = state_->faces.find(face_id.value);
        if (face_it == state_->faces.end()) {
            return detail::invalid_input_result<MeshId>(
                *state_, diag_codes::kCoreInvalidHandle,
                "局部BRep转网格失败：dirty face 不存在", "局部BRep转网格失败");
        }
        bool belongs = false;
        for (const auto shell_id : body_it->second.shells) {
            const auto shell_it = state_->shells.find(shell_id.value);
            if (shell_it == state_->shells.end()) continue;
            if (std::any_of(shell_it->second.faces.begin(), shell_it->second.faces.end(),
                            [face_id](FaceId f) { return f.value == face_id.value; })) {
                belongs = true;
                break;
            }
        }
        if (!belongs) {
            return detail::invalid_input_result<MeshId>(
                *state_, diag_codes::kCoreParameterOutOfRange,
                "局部BRep转网格失败：dirty face 不属于该体", "局部BRep转网格失败");
        }
    }

    // Build (or reuse) per-face meshes, then assemble.
    detail::MeshRecord assembled;
    assembled.source_body = body_id;
    assembled.label = "mesh_from_brep_local_faces";
    assembled.bbox = body_it->second.bbox;

    auto is_dirty = [&](FaceId f) {
        return std::any_of(dirty_faces.begin(), dirty_faces.end(),
                           [f](FaceId d) { return d.value == f.value; });
    };

    for (const auto shell_id : body_it->second.shells) {
        const auto shell_it = state_->shells.find(shell_id.value);
        if (shell_it == state_->shells.end()) continue;
        for (const auto face_id : shell_it->second.faces) {
            const auto face_key = detail::face_tessellation_cache_key(*state_, face_id, options);
            auto cache_it = state_->face_tessellation_cache.find(face_key);
            bool need_rebuild = is_dirty(face_id);
            if (!need_rebuild && cache_it != state_->face_tessellation_cache.end()) {
                const auto mesh_it = state_->meshes.find(cache_it->second.value);
                if (mesh_it != state_->meshes.end()) {
                    // reuse cached face mesh
                } else {
                    state_->face_tessellation_cache.erase(cache_it);
                    cache_it = state_->face_tessellation_cache.end();
                }
            }

            MeshId face_mesh_id{};
            if (!need_rebuild && cache_it != state_->face_tessellation_cache.end()) {
                face_mesh_id = cache_it->second;
            } else {
                auto face_mesh = detail::tessellate_face(*state_, face_id, options);
                if (face_mesh.vertices.empty() || face_mesh.indices.empty()) {
                    // If we can't tessellate the face boundary, fallback to full body mesh.
                    return brep_to_mesh(body_id, options);
                }
                face_mesh.source_body = body_id;
                face_mesh.label = "mesh_face_" + std::to_string(face_id.value);
                const auto new_id = MeshId{state_->allocate_id()};
                state_->meshes.emplace(new_id.value, std::move(face_mesh));
                state_->face_tessellation_cache[face_key] = new_id;
                face_mesh_id = new_id;
            }

            const auto face_mesh_it = state_->meshes.find(face_mesh_id.value);
            if (face_mesh_it == state_->meshes.end()) {
                return brep_to_mesh(body_id, options);
            }
            const auto& fm = face_mesh_it->second;
            const auto base = static_cast<Index>(assembled.vertices.size());
            assembled.vertices.insert(assembled.vertices.end(), fm.vertices.begin(), fm.vertices.end());
            if (options.compute_normals && !fm.normals.empty()) {
                assembled.normals.insert(assembled.normals.end(), fm.normals.begin(), fm.normals.end());
            }
            if (!fm.texcoords.empty()) {
                assembled.texcoords.insert(assembled.texcoords.end(), fm.texcoords.begin(), fm.texcoords.end());
            }
            assembled.indices.reserve(assembled.indices.size() + fm.indices.size());
            for (const auto idx : fm.indices) {
                assembled.indices.push_back(base + idx);
            }
        }
    }

    if (assembled.vertices.empty() || assembled.indices.empty()) {
        return brep_to_mesh(body_id, options);
    }

    detail::weld_mesh_vertices(assembled, options.compute_normals);
    assembled.tessellation_budget_digest = tess_budget_digest;
    assembled.tessellation_strategy = "local_faces_welded";

    const auto out_id = MeshId{state_->allocate_id()};
    state_->meshes.emplace(out_id.value, std::move(assembled));
    return ok_result(out_id, state_->create_diagnostic("已完成局部BRep转网格"));
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
    record.rep_kind = RepKind::MeshRep;
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
    mesh.tessellation_strategy = "implicit_bbox_proxy";
    mesh.tessellation_budget_digest = detail::tessellation_budget_digest_json(options);
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
    report.tessellation_strategy = it->second.tessellation_strategy;
    report.tessellation_budget_digest = it->second.tessellation_budget_digest;
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

    // Extended stats (Stage 1.5): bbox + simple quality metrics.
    const auto mesh_it = state_->meshes.find(mesh_id.value);
    if (mesh_it != state_->meshes.end()) {
        const auto& mesh = mesh_it->second;
        out << ",\"bbox_is_valid\":" << (detail::is_valid_bbox(mesh.bbox) ? "true" : "false");
        if (detail::is_valid_bbox(mesh.bbox)) {
            out << ",\"bbox_min\":[" << mesh.bbox.min.x << "," << mesh.bbox.min.y << "," << mesh.bbox.min.z << "]";
            out << ",\"bbox_max\":[" << mesh.bbox.max.x << "," << mesh.bbox.max.y << "," << mesh.bbox.max.z << "]";
        }
        out << ",\"has_vertex_normals\":" << (!mesh.normals.empty() ? "true" : "false");
        out << ",\"has_texcoords\":" << (!mesh.texcoords.empty() ? "true" : "false");

        std::uint64_t degenerate_count = 0;
        Scalar min_area2 = std::numeric_limits<Scalar>::infinity();
        Scalar max_area2 = 0.0;
        Scalar min_edge2 = std::numeric_limits<Scalar>::infinity();
        Scalar max_edge2 = 0.0;
        if ((mesh.indices.size() % 3) == 0 && !detail::has_out_of_range_indices(mesh.vertices, mesh.indices)) {
            for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const auto& p0 = mesh.vertices[static_cast<std::size_t>(mesh.indices[i])];
                const auto& p1 = mesh.vertices[static_cast<std::size_t>(mesh.indices[i + 1])];
                const auto& p2 = mesh.vertices[static_cast<std::size_t>(mesh.indices[i + 2])];
                const Vec3 e01 {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
                const Vec3 e12 {p2.x - p1.x, p2.y - p1.y, p2.z - p1.z};
                const Vec3 e20 {p0.x - p2.x, p0.y - p2.y, p0.z - p2.z};
                const auto l01 = detail::dot(e01, e01);
                const auto l12 = detail::dot(e12, e12);
                const auto l20 = detail::dot(e20, e20);
                min_edge2 = std::min(min_edge2, std::min(l01, std::min(l12, l20)));
                max_edge2 = std::max(max_edge2, std::max(l01, std::max(l12, l20)));

                const Vec3 a {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
                const Vec3 b {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
                const Vec3 cp = detail::cross(a, b);
                const auto area2 = detail::dot(cp, cp);
                min_area2 = std::min(min_area2, area2);
                max_area2 = std::max(max_area2, area2);
                if (area2 <= 1e-12) {
                    ++degenerate_count;
                }
            }
        }
        if (min_edge2 != std::numeric_limits<Scalar>::infinity()) {
            out << ",\"min_edge_length\":" << std::sqrt(std::max<Scalar>(0.0, min_edge2));
            out << ",\"max_edge_length\":" << std::sqrt(std::max<Scalar>(0.0, max_edge2));
        }
        if (min_area2 != std::numeric_limits<Scalar>::infinity()) {
            out << ",\"min_triangle_area\":" << 0.5 * std::sqrt(std::max<Scalar>(0.0, min_area2));
            out << ",\"max_triangle_area\":" << 0.5 * std::sqrt(std::max<Scalar>(0.0, max_area2));
        }
        out << ",\"degenerate_triangle_count\":" << degenerate_count;
        out << ",\"tessellation_strategy\":\""
            << json_escape_strategy(mesh.tessellation_strategy) << "\"";
        out << ",\"tessellation_budget_digest\":";
        if (mesh.tessellation_budget_digest.empty()) {
            out << "null";
        } else {
            out << mesh.tessellation_budget_digest;
        }
    }
    out << "}";
    return ok_void(state_->create_diagnostic("已导出网格统计报告"));
}

Result<RoundTripReport> RepresentationConversionService::verify_brep_mesh_round_trip(
    BodyId body_id, const TessellationOptions& options) {
    if (!detail::has_body(*state_, body_id)) {
        return detail::invalid_input_result<RoundTripReport>(
            *state_, diag_codes::kCoreInvalidHandle,
            "round-trip 验证失败：目标体不存在", "round-trip 验证失败");
    }
    if (!detail::has_valid_tessellation_options(options)) {
        return detail::invalid_input_result<RoundTripReport>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "round-trip 验证失败：三角化误差参数必须为正数", "round-trip 验证失败");
    }

    const auto mesh_res = brep_to_mesh(body_id, options);
    if (mesh_res.status != StatusCode::Ok || !mesh_res.value.has_value()) {
        return error_result<RoundTripReport>(mesh_res.status, mesh_res.diagnostic_id);
    }
    const auto brep_res = mesh_to_brep(*mesh_res.value);
    if (brep_res.status != StatusCode::Ok || !brep_res.value.has_value()) {
        return error_result<RoundTripReport>(brep_res.status, brep_res.diagnostic_id);
    }

    const auto body_it = state_->bodies.find(body_id.value);
    const auto mesh_it = state_->meshes.find(mesh_res.value->value);
    const auto rt_it = state_->bodies.find(brep_res.value->value);
    if (body_it == state_->bodies.end() || mesh_it == state_->meshes.end() || rt_it == state_->bodies.end()) {
        return detail::failed_result<RoundTripReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kCoreInvalidHandle,
            "round-trip 验证失败：内部记录缺失", "round-trip 验证失败");
    }

    RoundTripReport report;
    report.budget = detail::conversion_error_budget_from_tessellation(options);
    report.tessellation_strategy = mesh_it->second.tessellation_strategy;

    const auto& src_bbox = body_it->second.bbox;
    const auto& rt_bbox = rt_it->second.bbox;
    report.bbox_max_abs_delta = std::max({
        std::abs(src_bbox.min.x - rt_bbox.min.x), std::abs(src_bbox.min.y - rt_bbox.min.y), std::abs(src_bbox.min.z - rt_bbox.min.z),
        std::abs(src_bbox.max.x - rt_bbox.max.x), std::abs(src_bbox.max.y - rt_bbox.max.y), std::abs(src_bbox.max.z - rt_bbox.max.z),
    });

    // Mesh fidelity to analytic primitive (when available).
    const auto& body = body_it->second;
    const auto& mesh = mesh_it->second;
    Scalar max_point_err = 0.0;
    if (!mesh.vertices.empty()) {
        switch (body.kind) {
            case detail::BodyKind::Sphere: {
                const auto c = body.origin;
                const auto r = std::max<Scalar>(body.a, 0.0);
                for (const auto& p : mesh.vertices) {
                    const Vec3 d {p.x - c.x, p.y - c.y, p.z - c.z};
                    const auto len = std::sqrt(detail::dot(d, d));
                    max_point_err = std::max(max_point_err, std::abs(len - r));
                }
                break;
            }
            case detail::BodyKind::Cylinder: {
                const auto c = body.origin;
                const auto ax = detail::normalize(body.axis);
                const auto r = std::max<Scalar>(body.a, 0.0);
                for (const auto& p : mesh.vertices) {
                    const Vec3 d {p.x - c.x, p.y - c.y, p.z - c.z};
                    const auto t = detail::dot(d, ax);
                    const Vec3 proj {ax.x * t, ax.y * t, ax.z * t};
                    const Vec3 radial {d.x - proj.x, d.y - proj.y, d.z - proj.z};
                    const auto len = std::sqrt(detail::dot(radial, radial));
                    max_point_err = std::max(max_point_err, std::abs(len - r));
                }
                break;
            }
            case detail::BodyKind::Box: {
                // Distance to bbox surfaces (0 for exact box vertices/edges).
                const auto& bb = body.bbox;
                for (const auto& p : mesh.vertices) {
                    const auto dx = detail::axis_distance(p.x, bb.min.x, bb.max.x);
                    const auto dy = detail::axis_distance(p.y, bb.min.y, bb.max.y);
                    const auto dz = detail::axis_distance(p.z, bb.min.z, bb.max.z);
                    max_point_err = std::max(max_point_err, std::sqrt(dx*dx + dy*dy + dz*dz));
                }
                break;
            }
            default:
                break;
        }
    }
    report.max_point_abs_delta = max_point_err;

    report.source_triangles = static_cast<std::uint64_t>(mesh.indices.size() / 3);
    report.roundtrip_triangles = report.source_triangles;
    report.passed = report.bbox_max_abs_delta <= report.budget.bbox_abs_tol &&
                    report.max_point_abs_delta <= report.budget.max_point_abs_tol;
    return ok_result(report, state_->create_diagnostic("已完成 BRep->Mesh->BRep round-trip 验证"));
}

Result<RoundTripReport> RepresentationConversionService::verify_mesh_brep_round_trip(
    MeshId mesh_id, const TessellationOptions& options) {
    const auto it = state_->meshes.find(mesh_id.value);
    if (it == state_->meshes.end()) {
        return detail::invalid_input_result<RoundTripReport>(
            *state_, diag_codes::kCoreInvalidHandle,
            "round-trip 验证失败：目标网格不存在", "round-trip 验证失败");
    }
    if (!detail::has_valid_tessellation_options(options)) {
        return detail::invalid_input_result<RoundTripReport>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "round-trip 验证失败：三角化误差参数必须为正数", "round-trip 验证失败");
    }

    const auto brep_res = mesh_to_brep(mesh_id);
    if (brep_res.status != StatusCode::Ok || !brep_res.value.has_value()) {
        return error_result<RoundTripReport>(brep_res.status, brep_res.diagnostic_id);
    }
    const auto mesh2_res = brep_to_mesh(*brep_res.value, options);
    if (mesh2_res.status != StatusCode::Ok || !mesh2_res.value.has_value()) {
        return error_result<RoundTripReport>(mesh2_res.status, mesh2_res.diagnostic_id);
    }

    const auto& src = it->second;
    const auto it2 = state_->meshes.find(mesh2_res.value->value);
    if (it2 == state_->meshes.end()) {
        return detail::failed_result<RoundTripReport>(
            *state_, StatusCode::OperationFailed, diag_codes::kCoreInvalidHandle,
            "round-trip 验证失败：内部网格记录缺失", "round-trip 验证失败");
    }
    const auto& rt = it2->second;

    RoundTripReport report;
    report.budget = detail::conversion_error_budget_from_tessellation(options);

    report.bbox_max_abs_delta = std::max({
        std::abs(src.bbox.min.x - rt.bbox.min.x), std::abs(src.bbox.min.y - rt.bbox.min.y), std::abs(src.bbox.min.z - rt.bbox.min.z),
        std::abs(src.bbox.max.x - rt.bbox.max.x), std::abs(src.bbox.max.y - rt.bbox.max.y), std::abs(src.bbox.max.z - rt.bbox.max.z),
    });
    report.max_point_abs_delta = report.bbox_max_abs_delta;
    report.source_triangles = static_cast<std::uint64_t>(src.indices.size() / 3);
    report.roundtrip_triangles = static_cast<std::uint64_t>(rt.indices.size() / 3);
    report.passed = report.bbox_max_abs_delta <= report.budget.bbox_abs_tol;

    return ok_result(report, state_->create_diagnostic("已完成 Mesh->BRep->Mesh round-trip 验证"));
}

}  // namespace axiom
