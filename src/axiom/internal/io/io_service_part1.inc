Result<BodyId> IOService::import_step(std::string_view path, const ImportOptions& options) {
    if (path.empty()) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kIoImportFailure,
            "STEP 导入失败：输入路径为空", "STEP 导入失败");
    }
    if (!std::filesystem::exists(path)) {
        return detail::failed_result<BodyId>(
            *state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
            "STEP 导入失败：输入文件不存在", "STEP 导入失败");
    }

    std::ifstream in {std::string(path)};
    if (!in) {
        return detail::failed_result<BodyId>(
            *state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
            "STEP 导入失败：无法打开输入文件", "STEP 导入失败");
    }

    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::ExactBRep;
    record.label = std::string(path);
    record.bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});

    parse_axiom_interchange_metadata_lines(in, record);

    state_->bodies.emplace(body_id.value, record);

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    auto append_issues_from_diag = [this, &issues, &warnings](DiagnosticId diagnostic_id,
                                                              std::initializer_list<std::uint64_t> fallback_related_entities) {
        if (diagnostic_id.value == 0) {
            return;
        }
        const auto diag_it = state_->diagnostics.find(diagnostic_id.value);
        if (diag_it == state_->diagnostics.end()) {
            return;
        }
        for (const auto& issue : diag_it->second.issues) {
            auto tracked_issue = issue;
            if (tracked_issue.related_entities.empty()) {
                tracked_issue.related_entities.assign(fallback_related_entities.begin(), fallback_related_entities.end());
            }
            issues.push_back(tracked_issue);
            if (tracked_issue.severity == IssueSeverity::Warning ||
                tracked_issue.severity == IssueSeverity::Error ||
                tracked_issue.severity == IssueSeverity::Fatal) {
                warnings.push_back(Warning {tracked_issue.code, tracked_issue.message});
            }
        }
    };

    BodyId result_body_id = body_id;
    if (options.run_validation) {
        auto validation_issue = detail::make_info_issue(diag_codes::kIoPostImportValidation, "STEP 导入后已触发自动验证");
        validation_issue.related_entities = {body_id.value};
        validation_issue.stage = "io.post_import.validation";
        issues.push_back(std::move(validation_issue));

        ValidationService validation {state_};
        const auto validation_result = validation.validate_all(result_body_id, ValidationMode::Standard);
        if (validation_result.status != StatusCode::Ok) {
            append_issues_from_diag(validation_result.diagnostic_id, {result_body_id.value});
            if (validation_result.diagnostic_id.value == 0) {
                const auto message = std::string("STEP 导入后自动验证失败");
                auto fallback_issue = detail::make_warning_issue(diag_codes::kIoImportFailure, message);
                fallback_issue.related_entities = {result_body_id.value};
                issues.push_back(std::move(fallback_issue));
                warnings.push_back(Warning {std::string(diag_codes::kIoImportFailure), message});
            }

            if (options.auto_repair) {
                const auto repair_mode = options.repair_mode == RepairMode::ReportOnly
                                             ? RepairMode::Safe
                                             : options.repair_mode;
                auto repair_mode_issue = detail::make_info_issue(
                    diag_codes::kIoPostImportRepairMode,
                    "STEP 导入后自动修复策略已启用: mode=" +
                        std::to_string(static_cast<int>(repair_mode)));
                repair_mode_issue.related_entities = {result_body_id.value};
                repair_mode_issue.stage = "io.post_import.repair_mode";
                issues.push_back(std::move(repair_mode_issue));

                RepairService repair {state_};
                const auto repair_result = repair.auto_repair(result_body_id, repair_mode);
                if (repair_result.status == StatusCode::Ok && repair_result.value.has_value()) {
                    result_body_id = repair_result.value->output;
                    append_issues_from_diag(repair_result.value->diagnostic_id, {body_id.value, result_body_id.value});

                    const auto repaired_validation = validation.validate_all(result_body_id, ValidationMode::Standard);
                    if (repaired_validation.status != StatusCode::Ok) {
                        append_issues_from_diag(repaired_validation.diagnostic_id, {result_body_id.value});
                    }
                } else {
                    append_issues_from_diag(repair_result.diagnostic_id, {body_id.value});
                }
            }
        }
    }

    auto result = ok_result(result_body_id, state_->create_diagnostic("已导入STEP模型", std::move(issues)));
    result.warnings = std::move(warnings);
    return result;
}

Result<void> IOService::export_step(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "STEP 导出失败：目标实体不存在或输出路径为空", "STEP 导出失败");
    }

    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "STEP 导出失败：无法打开输出文件", "STEP 导出失败");
    }

    const auto body_it = state_->bodies.find(body_id.value);
    const auto& body = body_it->second;

    out << "ISO-10303-21;\n";
    out << "HEADER;\n";
    out << "FILE_DESCRIPTION(('AxiomKernel export'),'2;1');\n";
    if (options.embed_metadata) {
        out << "/* metadata embedded */\n";
        out << "AXIOM_LABEL " << body.label << "\n";
        out << "AXIOM_BODY_KIND " << body_kind_name(body.kind) << "\n";
        out << "AXIOM_ORIGIN " << body.origin.x << " " << body.origin.y << " " << body.origin.z << "\n";
        out << "AXIOM_AXIS " << body.axis.x << " " << body.axis.y << " " << body.axis.z << "\n";
        out << "AXIOM_PARAMS " << body.a << " " << body.b << " " << body.c << "\n";
        out << "AXIOM_BBOX "
            << body.bbox.min.x << " " << body.bbox.min.y << " " << body.bbox.min.z << " "
            << body.bbox.max.x << " " << body.bbox.max.y << " " << body.bbox.max.z << "\n";
    }
    out << "ENDSEC;\n";
    out << "DATA;\n";
    out << "/* body " << body_id.value << " */\n";
    out << "ENDSEC;\n";
    out << "END-ISO-10303-21;\n";

    return ok_void(state_->create_diagnostic("已导出STEP模型"));
}

Result<BodyId> IOService::import_axmjson(std::string_view path, const ImportOptions& options) {
    if (path.empty()) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kIoImportFailure,
            "AXMJSON 导入失败：输入路径为空", "AXMJSON 导入失败");
    }
    if (!std::filesystem::exists(path)) {
        return detail::failed_result<BodyId>(
            *state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
            "AXMJSON 导入失败：输入文件不存在", "AXMJSON 导入失败");
    }
    std::ifstream in {std::string(path)};
    if (!in) {
        return detail::failed_result<BodyId>(
            *state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
            "AXMJSON 导入失败：无法打开输入文件", "AXMJSON 导入失败");
    }
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());

    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::ExactBRep;
    record.label = std::string(path);
    record.bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});

    fill_body_record_from_axmjson_content(content, record);

    state_->bodies.emplace(body_id.value, record);

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    auto append_issues_from_diag = [this, &issues, &warnings](DiagnosticId diagnostic_id,
                                                              std::initializer_list<std::uint64_t> fallback_related_entities) {
        if (diagnostic_id.value == 0) {
            return;
        }
        const auto diag_it = state_->diagnostics.find(diagnostic_id.value);
        if (diag_it == state_->diagnostics.end()) {
            return;
        }
        for (const auto& issue : diag_it->second.issues) {
            auto tracked_issue = issue;
            if (tracked_issue.related_entities.empty()) {
                tracked_issue.related_entities.assign(fallback_related_entities.begin(), fallback_related_entities.end());
            }
            issues.push_back(tracked_issue);
            if (tracked_issue.severity == IssueSeverity::Warning ||
                tracked_issue.severity == IssueSeverity::Error ||
                tracked_issue.severity == IssueSeverity::Fatal) {
                warnings.push_back(Warning {tracked_issue.code, tracked_issue.message});
            }
        }
    };

    BodyId result_body_id = body_id;
    if (options.run_validation) {
        auto validation_issue = detail::make_info_issue(diag_codes::kIoPostImportValidation, "AXMJSON 导入后已触发自动验证");
        validation_issue.related_entities = {body_id.value};
        validation_issue.stage = "io.post_import.validation";
        issues.push_back(std::move(validation_issue));

        ValidationService validation {state_};
        const auto validation_result = validation.validate_all(result_body_id, ValidationMode::Standard);
        if (validation_result.status != StatusCode::Ok) {
            append_issues_from_diag(validation_result.diagnostic_id, {result_body_id.value});
            if (validation_result.diagnostic_id.value == 0) {
                const auto message = std::string("AXMJSON 导入后自动验证失败");
                auto fallback_issue = detail::make_warning_issue(diag_codes::kIoImportFailure, message);
                fallback_issue.related_entities = {result_body_id.value};
                issues.push_back(std::move(fallback_issue));
                warnings.push_back(Warning {std::string(diag_codes::kIoImportFailure), message});
            }

            if (options.auto_repair) {
                const auto repair_mode = options.repair_mode == RepairMode::ReportOnly
                                             ? RepairMode::Safe
                                             : options.repair_mode;
                auto repair_mode_issue = detail::make_info_issue(
                    diag_codes::kIoPostImportRepairMode,
                    "AXMJSON 导入后自动修复策略已启用: mode=" +
                        std::to_string(static_cast<int>(repair_mode)));
                repair_mode_issue.related_entities = {result_body_id.value};
                repair_mode_issue.stage = "io.post_import.repair_mode";
                issues.push_back(std::move(repair_mode_issue));

                RepairService repair {state_};
                const auto repair_result = repair.auto_repair(result_body_id, repair_mode);
                if (repair_result.status == StatusCode::Ok && repair_result.value.has_value()) {
                    result_body_id = repair_result.value->output;
                    append_issues_from_diag(repair_result.value->diagnostic_id, {body_id.value, result_body_id.value});

                    const auto repaired_validation = validation.validate_all(result_body_id, ValidationMode::Standard);
                    if (repaired_validation.status != StatusCode::Ok) {
                        append_issues_from_diag(repaired_validation.diagnostic_id, {result_body_id.value});
                    }
                } else {
                    append_issues_from_diag(repair_result.diagnostic_id, {body_id.value});
                }
            }
        }
    }

    auto result = ok_result(result_body_id, state_->create_diagnostic("已导入AXMJSON模型", std::move(issues)));
    result.warnings = std::move(warnings);
    return result;
}

Result<BodyId> IOService::import_stl(std::string_view path, const ImportOptions& options) {
    if (path.empty()) {
        return detail::invalid_input_result<BodyId>(*state_, diag_codes::kIoImportFailure, "STL 导入失败：输入路径为空",
                                                    "STL 导入失败");
    }
    if (!std::filesystem::exists(path)) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "STL 导入失败：输入文件不存在", "STL 导入失败");
    }
    std::ifstream in {std::string(path), std::ios::binary};
    if (!in) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "STL 导入失败：无法打开输入文件", "STL 导入失败");
    }
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    detail::MeshRecord mesh;
    const auto parse_err = parse_stl_bytes(data, mesh);
    if (parse_err.has_value()) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             *parse_err, "STL 导入失败");
    }
    if (mesh.vertices.empty() || mesh.indices.empty() || (mesh.indices.size() % 3) != 0) {
        return detail::invalid_input_result<BodyId>(*state_, diag_codes::kIoImportFailure, "STL 导入失败：网格数据无效",
                                                    "STL 导入失败");
    }
    if (detail::has_out_of_range_indices(mesh.vertices, mesh.indices)) {
        return detail::invalid_input_result<BodyId>(*state_, diag_codes::kIoImportFailure, "STL 导入失败：三角形索引越界",
                                                    "STL 导入失败");
    }
    if (detail::has_degenerate_triangles(mesh.vertices, mesh.indices)) {
        return detail::failed_result<BodyId>(*state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                                           "STL 导入失败：存在退化三角形", "STL 导入失败");
    }

    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::MeshRep;
    record.label = std::string(path);
    record.bbox = mesh.bbox;
    state_->bodies.emplace(body_id.value, record);
    mesh.source_body = body_id;
    const auto mesh_id = MeshId {state_->allocate_id()};
    state_->meshes.emplace(mesh_id.value, std::move(mesh));

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    const auto result_body_id = run_post_import_validation_pipeline(state_, body_id, options, std::string("STL"), issues, warnings);
    auto result = ok_result(result_body_id, state_->create_diagnostic("已导入STL模型", std::move(issues)));
    result.warnings = std::move(warnings);
    return result;
}

Result<BodyId> IOService::import_gltf(std::string_view path, const ImportOptions& options) {
    if (path.empty()) {
        return detail::invalid_input_result<BodyId>(*state_, diag_codes::kIoImportFailure, "glTF 导入失败：输入路径为空",
                                                    "glTF 导入失败");
    }
    if (!std::filesystem::exists(path)) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "glTF 导入失败：输入文件不存在", "glTF 导入失败");
    }
    std::ifstream in {std::string(path)};
    if (!in) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "glTF 导入失败：无法打开输入文件", "glTF 导入失败");
    }
    std::string json_text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    detail::MeshRecord mesh;
    const auto parse_err = parse_gltf_embedded_minimal(json_text, mesh);
    if (parse_err.has_value()) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             *parse_err, "glTF 导入失败");
    }
    if (mesh.vertices.empty() || mesh.indices.empty() || (mesh.indices.size() % 3) != 0) {
        return detail::invalid_input_result<BodyId>(*state_, diag_codes::kIoImportFailure, "glTF 导入失败：网格数据无效",
                                                    "glTF 导入失败");
    }
    if (detail::has_out_of_range_indices(mesh.vertices, mesh.indices)) {
        return detail::invalid_input_result<BodyId>(*state_, diag_codes::kIoImportFailure, "glTF 导入失败：三角形索引越界",
                                                    "glTF 导入失败");
    }
    if (detail::has_degenerate_triangles(mesh.vertices, mesh.indices)) {
        return detail::failed_result<BodyId>(*state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                                           "glTF 导入失败：存在退化三角形", "glTF 导入失败");
    }

    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::MeshRep;
    record.label = std::string(path);
    record.bbox = mesh.bbox;
    state_->bodies.emplace(body_id.value, record);
    mesh.source_body = body_id;
    const auto mesh_id = MeshId {state_->allocate_id()};
    state_->meshes.emplace(mesh_id.value, std::move(mesh));

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    const auto result_body_id = run_post_import_validation_pipeline(state_, body_id, options, std::string("glTF"), issues, warnings);
    auto result = ok_result(result_body_id, state_->create_diagnostic("已导入glTF模型", std::move(issues)));
    result.warnings = std::move(warnings);
    return result;
}

Result<BodyId> IOService::import_iges(std::string_view path, const ImportOptions& options) {
    if (path.empty()) {
        return detail::invalid_input_result<BodyId>(*state_, diag_codes::kIoImportFailure, "IGES 导入失败：输入路径为空",
                                                    "IGES 导入失败");
    }
    if (!std::filesystem::exists(path)) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "IGES 导入失败：输入文件不存在", "IGES 导入失败");
    }
    std::ifstream in {std::string(path)};
    if (!in) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "IGES 导入失败：无法打开输入文件", "IGES 导入失败");
    }
    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::ExactBRep;
    record.label = std::string(path);
    record.bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});
    parse_axiom_interchange_metadata_lines(in, record);
    state_->bodies.emplace(body_id.value, record);

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    const auto result_body_id = run_post_import_validation_pipeline(state_, body_id, options, std::string("IGES"), issues, warnings);
    auto result = ok_result(result_body_id, state_->create_diagnostic("已导入IGES模型", std::move(issues)));
    result.warnings = std::move(warnings);
    return result;
}

Result<BodyId> IOService::import_brep(std::string_view path, const ImportOptions& options) {
    if (path.empty()) {
        return detail::invalid_input_result<BodyId>(*state_, diag_codes::kIoImportFailure, "BREP 导入失败：输入路径为空",
                                                    "BREP 导入失败");
    }
    if (!std::filesystem::exists(path)) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "BREP 导入失败：输入文件不存在", "BREP 导入失败");
    }
    std::ifstream in {std::string(path)};
    if (!in) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "BREP 导入失败：无法打开输入文件", "BREP 导入失败");
    }
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string content = strip_leading_hash_lines(raw);

    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::ExactBRep;
    record.label = std::string(path);
    record.bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});
    fill_body_record_from_axmjson_content(content, record);
    state_->bodies.emplace(body_id.value, record);

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    const auto result_body_id = run_post_import_validation_pipeline(state_, body_id, options, std::string("BREP"), issues, warnings);
    auto result = ok_result(result_body_id, state_->create_diagnostic("已导入BREP模型", std::move(issues)));
    result.warnings = std::move(warnings);
    return result;
}

Result<BodyId> IOService::import_obj(std::string_view path, const ImportOptions& options) {
    if (path.empty()) {
        return detail::invalid_input_result<BodyId>(*state_, diag_codes::kIoImportFailure, "OBJ 导入失败：输入路径为空",
                                                    "OBJ 导入失败");
    }
    if (!std::filesystem::exists(path)) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "OBJ 导入失败：输入文件不存在", "OBJ 导入失败");
    }
    std::ifstream in {std::string(path)};
    if (!in) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "OBJ 导入失败：无法打开输入文件", "OBJ 导入失败");
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    detail::MeshRecord mesh;
    const auto err = parse_obj_text_to_mesh(text, mesh);
    if (err.has_value()) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure, *err,
                                             "OBJ 导入失败");
    }
    if (detail::has_degenerate_triangles(mesh.vertices, mesh.indices)) {
        return detail::failed_result<BodyId>(*state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                                             "OBJ 导入失败：存在退化三角形", "OBJ 导入失败");
    }
    mesh.label = "obj_import";
    mesh.tessellation_strategy = "io_import_obj";
    mesh.bbox = detail::mesh_bbox_from_vertices(mesh.vertices);

    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::MeshRep;
    record.label = std::string(path);
    record.bbox = mesh.bbox;
    state_->bodies.emplace(body_id.value, record);
    mesh.source_body = body_id;
    const auto mesh_id = MeshId {state_->allocate_id()};
    state_->meshes.emplace(mesh_id.value, std::move(mesh));

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    const auto result_body_id = run_post_import_validation_pipeline(state_, body_id, options, std::string("OBJ"), issues, warnings);
    auto result = ok_result(result_body_id, state_->create_diagnostic("已导入OBJ模型", std::move(issues)));
    result.warnings = std::move(warnings);
    return result;
}

Result<BodyId> IOService::import_3mf(std::string_view path, const ImportOptions& options) {
    if (path.empty()) {
        return detail::invalid_input_result<BodyId>(*state_, diag_codes::kIoImportFailure, "3MF 导入失败：输入路径为空",
                                                    "3MF 导入失败");
    }
    if (!std::filesystem::exists(path)) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "3MF 导入失败：输入文件不存在", "3MF 导入失败");
    }
    std::ifstream in {std::string(path), std::ios::binary};
    if (!in) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "3MF 导入失败：无法打开输入文件", "3MF 导入失败");
    }
    std::vector<std::uint8_t> zip_data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> files;
    if (!zip_parse_stored_locals(zip_data, files)) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "3MF 导入失败：ZIP 解析失败或非 STORE 压缩包", "3MF 导入失败");
    }
    std::string model_xml;
    if (!extract_3mf_model_xml(files, model_xml)) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                             "3MF 导入失败：未找到 3dmodel.model", "3MF 导入失败");
    }
    detail::MeshRecord mesh;
    const auto perr = parse_3mf_model_xml_to_mesh(model_xml, mesh);
    if (perr.has_value()) {
        return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure, *perr,
                                             "3MF 导入失败");
    }
    if (detail::has_degenerate_triangles(mesh.vertices, mesh.indices)) {
        return detail::failed_result<BodyId>(*state_, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                                             "3MF 导入失败：存在退化三角形", "3MF 导入失败");
    }
    mesh.label = "3mf_import";
    mesh.tessellation_strategy = "io_import_3mf";
    mesh.bbox = detail::mesh_bbox_from_vertices(mesh.vertices);

    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::MeshRep;
    record.label = std::string(path);
    record.bbox = mesh.bbox;
    state_->bodies.emplace(body_id.value, record);
    mesh.source_body = body_id;
    const auto mesh_id = MeshId {state_->allocate_id()};
    state_->meshes.emplace(mesh_id.value, std::move(mesh));

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    const auto result_body_id = run_post_import_validation_pipeline(state_, body_id, options, std::string("3MF"), issues, warnings);
    auto result = ok_result(result_body_id, state_->create_diagnostic("已导入3MF模型", std::move(issues)));
    result.warnings = std::move(warnings);
    return result;
}

Result<void> IOService::export_axmjson(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "AXMJSON 导出失败：目标实体不存在或输出路径为空", "AXMJSON 导出失败");
    }
    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "AXMJSON 导出失败：无法打开输出文件", "AXMJSON 导出失败");
    }
    const auto body_it = state_->bodies.find(body_id.value);
    const auto& body = body_it->second;
    write_axmjson_payload(out, body, options, "AXMJSON");
    return ok_void(state_->create_diagnostic("已导出AXMJSON模型"));
}

Result<void> IOService::export_iges(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "IGES 导出失败：目标实体不存在或输出路径为空", "IGES 导出失败");
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) return ensured;
    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "IGES 导出失败：无法打开输出文件", "IGES 导出失败");
    }
    const auto body_it = state_->bodies.find(body_id.value);
    const auto& body = body_it->second;
    out << "START\n";
    out << "1H,,1HAXIOM,AxiomKernel IGES metadata interchange (subset)\n";
    if (options.embed_metadata) {
        out << "AXIOM_LABEL " << body.label << "\n";
        out << "AXIOM_BODY_KIND " << body_kind_name(body.kind) << "\n";
        out << "AXIOM_ORIGIN " << body.origin.x << " " << body.origin.y << " " << body.origin.z << "\n";
        out << "AXIOM_AXIS " << body.axis.x << " " << body.axis.y << " " << body.axis.z << "\n";
        out << "AXIOM_PARAMS " << body.a << " " << body.b << " " << body.c << "\n";
        out << "AXIOM_BBOX "
            << body.bbox.min.x << " " << body.bbox.min.y << " " << body.bbox.min.z << " "
            << body.bbox.max.x << " " << body.bbox.max.y << " " << body.bbox.max.z << "\n";
    }
    out << "S 1\n";
    out << "TERMINATE\n";
    return ok_void(state_->create_diagnostic("已导出IGES模型"));
}

Result<void> IOService::export_brep(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "BREP 导出失败：目标实体不存在或输出路径为空", "BREP 导出失败");
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) return ensured;
    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "BREP 导出失败：无法打开输出文件", "BREP 导出失败");
    }
    const auto body_it = state_->bodies.find(body_id.value);
    const auto& body = body_it->second;
    out << "# AXIOM_BREP_INTERCHANGE v1\n";
    write_axmjson_payload(out, body, options, "AXIOM_BREP");
    return ok_void(state_->create_diagnostic("已导出BREP模型"));
}

Result<void> IOService::export_obj(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "OBJ 导出失败：目标实体不存在或输出路径为空", "OBJ 导出失败");
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) return ensured;

    RepresentationConversionService convert {state_};
    const auto mesh_id = convert.brep_to_mesh(body_id, TessellationOptions{});
    if (mesh_id.status != StatusCode::Ok || !mesh_id.value.has_value()) {
        return error_void(mesh_id.status, mesh_id.diagnostic_id);
    }
    const auto strict_gate = mesh_export_strict_gate(*state_, convert, *mesh_id.value, options, body_id);
    if (strict_gate.status != StatusCode::Ok) {
        return strict_gate;
    }
    const auto mesh_it = state_->meshes.find(mesh_id.value->value);
    if (mesh_it == state_->meshes.end()) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
                                   "OBJ 导出失败：网格记录不存在", "OBJ 导出失败");
    }
    const auto& mesh = mesh_it->second;
    if (mesh.vertices.empty() || mesh.indices.empty() || (mesh.indices.size() % 3) != 0) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "OBJ 导出失败：网格数据无效",
                                          "OBJ 导出失败");
    }

    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "OBJ 导出失败：无法打开输出文件", "OBJ 导出失败");
    }
    out << "# AxiomKernel OBJ\n";
    out << "o axiom_body_" << body_id.value << "\n";
    out << std::setprecision(17);
    for (const auto& v : mesh.vertices) {
        out << "v " << v.x << " " << v.y << " " << v.z << "\n";
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const auto a = static_cast<std::size_t>(mesh.indices[i]) + 1;
        const auto b = static_cast<std::size_t>(mesh.indices[i + 1]) + 1;
        const auto c = static_cast<std::size_t>(mesh.indices[i + 2]) + 1;
        out << "f " << a << " " << b << " " << c << "\n";
    }
    const auto sidecar = mesh_export_write_validation_sidecar(*state_, convert, *mesh_id.value, path, options);
    return merge_mesh_export_sidecar_summary(*state_, sidecar, "已导出OBJ模型");
}

Result<void> IOService::export_3mf(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "3MF 导出失败：目标实体不存在或输出路径为空", "3MF 导出失败");
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) return ensured;

    RepresentationConversionService convert {state_};
    const auto mesh_id = convert.brep_to_mesh(body_id, TessellationOptions{});
    if (mesh_id.status != StatusCode::Ok || !mesh_id.value.has_value()) {
        return error_void(mesh_id.status, mesh_id.diagnostic_id);
    }
    const auto strict_gate = mesh_export_strict_gate(*state_, convert, *mesh_id.value, options, body_id);
    if (strict_gate.status != StatusCode::Ok) {
        return strict_gate;
    }
    const auto mesh_it = state_->meshes.find(mesh_id.value->value);
    if (mesh_it == state_->meshes.end()) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
                                   "3MF 导出失败：网格记录不存在", "3MF 导出失败");
    }
    const auto& mesh = mesh_it->second;

    const std::string model_xml = build_3mf_model_xml_from_mesh(mesh);
    std::vector<std::uint8_t> model_bytes(model_xml.begin(), model_xml.end());
    static const char k_ct[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
        "<Default Extension=\"rels\" "
        "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
        "<Default Extension=\"model\" "
        "ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n"
        "</Types>\n";
    static const char k_rels[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        "<Relationship Id=\"rel0\" "
        "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\" "
        "Target=\"/3D/3dmodel.model\"/>\n"
        "</Relationships>\n";
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>> pack = {
        {"[Content_Types].xml", std::vector<std::uint8_t>(k_ct, k_ct + sizeof(k_ct) - 1)},
        {"_rels/.rels", std::vector<std::uint8_t>(k_rels, k_rels + sizeof(k_rels) - 1)},
        {"3D/3dmodel.model", std::move(model_bytes)},
    };
    const std::vector<std::uint8_t> zip_bytes = build_zip_store_archive(pack);
    std::ofstream out {std::string(path), std::ios::binary};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "3MF 导出失败：无法打开输出文件", "3MF 导出失败");
    }
    out.write(reinterpret_cast<const char*>(zip_bytes.data()),
              static_cast<std::streamsize>(zip_bytes.size()));
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "3MF 导出失败：写入失败", "3MF 导出失败");
    }
    const auto sidecar = mesh_export_write_validation_sidecar(*state_, convert, *mesh_id.value, path, options);
    return merge_mesh_export_sidecar_summary(*state_, sidecar, "已导出3MF模型");
}

Result<void> IOService::export_gltf(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "glTF 导出失败：目标实体不存在或输出路径为空", "glTF 导出失败");
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) return ensured;

    RepresentationConversionService convert {state_};
    const auto mesh_id = convert.brep_to_mesh(body_id, TessellationOptions{});
    if (mesh_id.status != StatusCode::Ok || !mesh_id.value.has_value()) {
        return error_void(mesh_id.status, mesh_id.diagnostic_id);
    }
    const auto strict_gate = mesh_export_strict_gate(*state_, convert, *mesh_id.value, options, body_id);
    if (strict_gate.status != StatusCode::Ok) {
        return strict_gate;
    }

    const auto mesh_it = state_->meshes.find(mesh_id.value->value);
    if (mesh_it == state_->meshes.end()) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
                                   "glTF 导出失败：网格记录不存在", "glTF 导出失败");
    }
    const auto& mesh = mesh_it->second;
    if (mesh.vertices.empty()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure,
                                          "glTF 导出失败：网格顶点为空", "glTF 导出失败");
    }
    if (!mesh.indices.empty() && (mesh.indices.size() % 3) != 0) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure,
                                          "glTF 导出失败：网格索引数量非法", "glTF 导出失败");
    }

    std::vector<std::uint8_t> buffer;
    buffer.reserve(mesh.vertices.size() * sizeof(float) * 3 +
                   mesh.indices.size() * sizeof(std::uint32_t) + 64);

    const std::size_t pos_offset = buffer.size();
    float min_x = static_cast<float>(mesh.vertices.front().x);
    float min_y = static_cast<float>(mesh.vertices.front().y);
    float min_z = static_cast<float>(mesh.vertices.front().z);
    float max_x = min_x, max_y = min_y, max_z = min_z;
    for (const auto& v : mesh.vertices) {
        const float fx = static_cast<float>(v.x);
        const float fy = static_cast<float>(v.y);
        const float fz = static_cast<float>(v.z);
        append_pod(buffer, fx);
        append_pod(buffer, fy);
        append_pod(buffer, fz);
        min_x = std::min(min_x, fx); min_y = std::min(min_y, fy); min_z = std::min(min_z, fz);
        max_x = std::max(max_x, fx); max_y = std::max(max_y, fy); max_z = std::max(max_z, fz);
    }
    append_padding_4(buffer);
    const std::size_t pos_length = buffer.size() - pos_offset;

    const std::size_t idx_offset = buffer.size();
    for (const auto idx : mesh.indices) {
        const std::uint32_t v = static_cast<std::uint32_t>(idx);
        append_pod(buffer, v);
    }
    append_padding_4(buffer);
    const std::size_t idx_length = buffer.size() - idx_offset;

    const auto uri = std::string("data:application/octet-stream;base64,") + base64_encode(buffer);

    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "glTF 导出失败：无法打开输出文件", "glTF 导出失败");
    }

    const auto vertex_count = static_cast<std::uint64_t>(mesh.vertices.size());
    const auto index_count = static_cast<std::uint64_t>(mesh.indices.size());

    out << "{";
    out << "\"asset\":{\"version\":\"2.0\",\"generator\":\"AxiomKernel\"},";
    out << "\"scene\":0,";
    out << "\"scenes\":[{\"nodes\":[0]}],";
    out << "\"nodes\":[{\"mesh\":0,\"name\":\"" << json_escape(mesh.label) << "\"}],";
    out << "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],";
    out << "\"buffers\":[{\"uri\":\"" << uri << "\",\"byteLength\":" << buffer.size() << "}],";
    out << "\"bufferViews\":[";
    out << "{\"buffer\":0,\"byteOffset\":" << pos_offset << ",\"byteLength\":" << pos_length << ",\"target\":34962},";
    out << "{\"buffer\":0,\"byteOffset\":" << idx_offset << ",\"byteLength\":" << idx_length << ",\"target\":34963}";
    out << "],";
    out << "\"accessors\":[";
    out << "{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":" << vertex_count
        << ",\"type\":\"VEC3\",\"min\":[" << min_x << "," << min_y << "," << min_z << "],"
        << "\"max\":[" << max_x << "," << max_y << "," << max_z << "]},";
    out << "{\"bufferView\":1,\"byteOffset\":0,\"componentType\":5125,\"count\":" << index_count
        << ",\"type\":\"SCALAR\"}";
    out << "]";
    out << "}";

    const auto sidecar = mesh_export_write_validation_sidecar(*state_, convert, *mesh_id.value, path, options);
    return merge_mesh_export_sidecar_summary(*state_, sidecar, "已导出glTF模型");
}

Result<void> IOService::export_stl(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "STL 导出失败：目标实体不存在或输出路径为空", "STL 导出失败");
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) return ensured;

    RepresentationConversionService convert {state_};
    const auto mesh_id = convert.brep_to_mesh(body_id, TessellationOptions{});
    if (mesh_id.status != StatusCode::Ok || !mesh_id.value.has_value()) {
        return error_void(mesh_id.status, mesh_id.diagnostic_id);
    }
    const auto strict_gate = mesh_export_strict_gate(*state_, convert, *mesh_id.value, options, body_id);
    if (strict_gate.status != StatusCode::Ok) {
        return strict_gate;
    }
    const auto mesh_it = state_->meshes.find(mesh_id.value->value);
    if (mesh_it == state_->meshes.end()) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
                                   "STL 导出失败：网格记录不存在", "STL 导出失败");
    }
    const auto& mesh = mesh_it->second;
    if (mesh.vertices.empty()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure,
                                          "STL 导出失败：网格顶点为空", "STL 导出失败");
    }
    if (mesh.indices.empty() || (mesh.indices.size() % 3) != 0) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure,
                                          "STL 导出失败：网格索引为空或数量非法", "STL 导出失败");
    }

    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "STL 导出失败：无法打开输出文件", "STL 导出失败");
    }

    out << "solid axiom\n";
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const auto i0 = static_cast<std::size_t>(mesh.indices[i]);
        const auto i1 = static_cast<std::size_t>(mesh.indices[i + 1]);
        const auto i2 = static_cast<std::size_t>(mesh.indices[i + 2]);
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
            return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure,
                                              "STL 导出失败：网格索引超出顶点范围", "STL 导出失败");
        }
        const auto& a = mesh.vertices[i0];
        const auto& b = mesh.vertices[i1];
        const auto& c = mesh.vertices[i2];
        const auto ab = detail::subtract(b, a);
        const auto ac = detail::subtract(c, a);
        const auto n = detail::normalize(detail::cross(ab, ac));

        out << "  facet normal " << n.x << " " << n.y << " " << n.z << "\n";
        out << "    outer loop\n";
        out << "      vertex " << a.x << " " << a.y << " " << a.z << "\n";
        out << "      vertex " << b.x << " " << b.y << " " << b.z << "\n";
        out << "      vertex " << c.x << " " << c.y << " " << c.z << "\n";
        out << "    endloop\n";
        out << "  endfacet\n";
    }
    out << "endsolid axiom\n";

    const auto sidecar = mesh_export_write_validation_sidecar(*state_, convert, *mesh_id.value, path, options);
    return merge_mesh_export_sidecar_summary(*state_, sidecar, "已导出STL模型");
}

Result<bool> IOService::file_exists(std::string_view path) const {
    if (path.empty()) {
        return ok_result(false, state_->create_diagnostic("已查询文件存在性"));
    }
    return ok_result(std::filesystem::exists(path), state_->create_diagnostic("已查询文件存在性"));
}

Result<bool> IOService::is_regular_file(std::string_view path) const {
    if (path.empty()) {
        return ok_result(false, state_->create_diagnostic("已查询常规文件属性"));
    }
    return ok_result(std::filesystem::is_regular_file(path), state_->create_diagnostic("已查询常规文件属性"));
}

Result<std::uint64_t> IOService::file_size_bytes(std::string_view path) const {
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        return detail::invalid_input_result<std::uint64_t>(
            *state_, diag_codes::kIoImportFailure,
            "文件大小查询失败：文件不存在或不是常规文件", "文件大小查询失败");
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(std::filesystem::file_size(path)),
                                    state_->create_diagnostic("已查询文件大小"));
}

Result<bool> IOService::has_extension(std::string_view path, std::string_view ext) const {
    if (path.empty() || ext.empty()) {
        return ok_result(false, state_->create_diagnostic("已查询文件扩展名匹配"));
    }
    const auto got = lower_copy(std::filesystem::path(path).extension().string());
    auto expected = lower_copy(std::string(ext));
    if (!expected.empty() && expected.front() != '.') {
        expected = "." + expected;
    }
    return ok_result(got == expected, state_->create_diagnostic("已查询文件扩展名匹配"));
}

Result<std::string> IOService::detect_format(std::string_view path) const {
    if (path.empty()) {
        return detail::invalid_input_result<std::string>(
            *state_, diag_codes::kIoImportFailure, "格式识别失败：路径为空", "格式识别失败");
    }
    const auto ext = lower_copy(std::filesystem::path(path).extension().string());
    if (ext == ".step" || ext == ".stp") {
        return ok_result(std::string("step"), state_->create_diagnostic("已识别IO格式"));
    }
    if (ext == ".axmjson" || ext == ".json") {
        return ok_result(std::string("axmjson"), state_->create_diagnostic("已识别IO格式"));
    }
    if (ext == ".gltf") {
        return ok_result(std::string("gltf"), state_->create_diagnostic("已识别IO格式"));
    }
    if (ext == ".stl") {
        return ok_result(std::string("stl"), state_->create_diagnostic("已识别IO格式"));
    }
    if (ext == ".iges" || ext == ".igs") {
        return ok_result(std::string("iges"), state_->create_diagnostic("已识别IO格式"));
    }
    if (ext == ".brep") {
        return ok_result(std::string("brep"), state_->create_diagnostic("已识别IO格式"));
    }
    if (ext == ".obj") {
        return ok_result(std::string("obj"), state_->create_diagnostic("已识别IO格式"));
    }
    if (ext == ".3mf") {
        return ok_result(std::string("3mf"), state_->create_diagnostic("已识别IO格式"));
    }
    return ok_result(std::string("unknown"), state_->create_diagnostic("已识别IO格式"));
}

Result<bool> IOService::is_step_path(std::string_view path) const {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<bool>(fmt.status, fmt.diagnostic_id);
    }
    return ok_result(*fmt.value == "step", state_->create_diagnostic("已识别STEP路径"));
}

Result<bool> IOService::is_axmjson_path(std::string_view path) const {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<bool>(fmt.status, fmt.diagnostic_id);
    }
    return ok_result(*fmt.value == "axmjson", state_->create_diagnostic("已识别AXMJSON路径"));
}

Result<bool> IOService::is_gltf_path(std::string_view path) const {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<bool>(fmt.status, fmt.diagnostic_id);
    }
    return ok_result(*fmt.value == "gltf", state_->create_diagnostic("已识别glTF路径"));
}

Result<bool> IOService::is_stl_path(std::string_view path) const {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<bool>(fmt.status, fmt.diagnostic_id);
    }
    return ok_result(*fmt.value == "stl", state_->create_diagnostic("已识别STL路径"));
}

Result<std::string> IOService::normalize_path(std::string_view path) const {
    if (path.empty()) {
        return detail::invalid_input_result<std::string>(
            *state_, diag_codes::kIoImportFailure, "路径规范化失败：路径为空", "路径规范化失败");
    }
