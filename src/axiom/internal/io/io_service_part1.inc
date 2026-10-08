Result<BodyId> IOService::import_step(std::string_view path, const ImportOptions& options) {
    const auto fail_before_import = [this](StatusCode status, std::string message,
                                           std::string_view stage) {
        return error_result<BodyId>(
            status, create_io_failure_diagnostic(
                        *state_, status, diag_codes::kIoImportFailure,
                        std::move(message), "STEP 导入失败", {0}, stage,
                        {{"materialized_body_count", 0.0, "count"}}));
    };
    if (path.empty()) {
        return fail_before_import(StatusCode::InvalidInput,
                                  "STEP 导入失败：输入路径为空", "io.import.step.input");
    }
    if (!std::filesystem::exists(path)) {
        return fail_before_import(StatusCode::OperationFailed,
                                  "STEP 导入失败：输入文件不存在", "io.import.step.path");
    }

    constexpr std::size_t kStepIgesProbeMaxBytes = static_cast<std::size_t>(64) * 1024 * 1024;
    std::error_code step_path_error;
    const bool is_regular_step_file = std::filesystem::is_regular_file(path, step_path_error);
    const auto file_text = is_regular_step_file
                               ? read_regular_file_bytes_limited(path, kStepIgesProbeMaxBytes)
                               : std::optional<std::string> {};
    if (!file_text.has_value()) {
        return fail_before_import(StatusCode::OperationFailed,
                                  "STEP 导入失败：无法打开输入文件", "io.import.step.open");
    }
    if (step_file_requires_full_standard_importer(std::string_view(*file_text))) {
        std::vector<Issue> issues;
        auto issue = detail::make_error_issue(
            diag_codes::kIoStepStandardEntitiesUnsupported,
            "STEP 导入：检测到 ISO-10303-21 DATA 段含 EXPRESS 实体实例；当前内核仅支持 Axiom 元数据子集。完整 AP 交换需集成 STEP 内核（如 STEPcode/Open CASCADE）。");
        issue.stage = "io.import.step";
        issue.related_entities = {0};
        issue.numeric_evidence = {
            {"status_code", static_cast<Scalar>(StatusCode::NotImplemented), "enum"},
            {"related_entity_count", 1.0, "count"},
            {"input_byte_count", static_cast<Scalar>(file_text->size()), "byte"}};
        issues.push_back(std::move(issue));
        std::string scan_msg;
        std::size_t scan_count = 0;
        if (summarize_step_standard_file_entities(std::string_view(*file_text), scan_msg, scan_count)) {
            auto info = detail::make_info_issue(diag_codes::kIoStepStandardFileScanSummary, std::move(scan_msg));
            info.stage = "io.import.step";
            issues.push_back(std::move(info));
        }
        return error_result<BodyId>(StatusCode::NotImplemented,
                                    state_->create_diagnostic("STEP 标准实体交换未实现", std::move(issues)));
    }

    IOImportBatchRollback rollback {*state_};
    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::ExactBRep;
    record.label = std::string(path);
    record.bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});

    std::istringstream in(*file_text);
    parse_axiom_interchange_metadata_lines(in, record);

    state_->bodies.emplace(body_id.value, record);

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    const auto post_import = run_post_import_validation_pipeline(
        state_, body_id, options, std::string("STEP"), issues, warnings);

    if (post_import.status != StatusCode::Ok || !post_import.value.has_value()) {
        return post_import;
    }
    auto result = ok_result(*post_import.value, state_->create_diagnostic("已导入STEP模型", std::move(issues)));
    result.warnings = std::move(warnings);
    rollback.commit();
    return result;
}

Result<void> IOService::export_step(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return io_failed_void(
            *state_, StatusCode::InvalidInput, diag_codes::kIoExportFailure,
            "STEP 导出失败：目标实体不存在或输出路径为空", "STEP 导出失败",
            {body_id.value}, "io.export.step.input",
            {{"body_exists", detail::has_body(*state_, body_id) ? 1.0 : 0.0, "bool"},
             {"path_length", static_cast<Scalar>(path.size()), "byte"}});
    }
    const auto wx = reject_if_export_directory_not_writable(*state_, path);
    if (wx.status != StatusCode::Ok) {
        const auto diagnostic_it = state_->diagnostics.find(wx.diagnostic_id.value);
        if (diagnostic_it != state_->diagnostics.end()) {
            for (auto& issue : diagnostic_it->second.issues) {
                issue.stage = "io.export.step.path";
                if (issue.related_entities.empty()) {
                    issue.related_entities.push_back(body_id.value);
                }
            }
        }
        return wrap_io_failed_void(
            *state_, wx, diag_codes::kIoExportFailure,
            "STEP 导出路径校验失败", "STEP 导出失败", {body_id.value},
            "io.export.step.path",
            {{"path_length", static_cast<Scalar>(path.size()), "byte"}});
    }

    std::ofstream out {std::string(path)};
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "STEP 导出失败：无法打开输出文件", "STEP 导出失败", {body_id.value},
            "io.export.step.open",
            {{"path_length", static_cast<Scalar>(path.size()), "byte"}});
    }

    const auto body_it = state_->bodies.find(body_id.value);
    const auto& body = body_it->second;

    out << "ISO-10303-21;\n";
    out << "HEADER;\n";
    out << "FILE_DESCRIPTION(('AxiomKernel export'),'2;1');\n";
    if (options.embed_metadata) {
        out << "/* metadata embedded */\n";
        const std::string step_schema =
            body.io_step_file_schema.empty() ? "CONFIG_CONTROL_DESIGN" : body.io_step_file_schema;
        const std::string step_entity =
            body.io_step_entity_name.empty() ? "MANIFOLD_SOLID_BREP" : body.io_step_entity_name;
        out << "/* FILE_NAME('AxiomKernel','','',''); */\n";
        out << "/* FILE_SCHEMA((('" << step_schema << "')) */\n";
        out << "AXIOM_STEP_SCHEMA " << step_schema << "\n";
        out << "AXIOM_STEP_ENTITY " << step_entity << "\n";
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
    out.flush();
    out.close();
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "STEP 导出失败：写入输出文件失败", "STEP 导出失败", {body_id.value},
            "io.export.step.write",
            {{"metadata_enabled", options.embed_metadata ? 1.0 : 0.0, "bool"}});
    }

    return ok_void(state_->create_diagnostic("已导出STEP模型"));
}

Result<BodyId> IOService::import_axmjson(std::string_view path, const ImportOptions& options) {
    if (path.empty()) {
        return exact_brep_import_failure(*state_, StatusCode::InvalidInput,
                                         diag_codes::kIoImportFailure,
                                         "AXMJSON 导入失败：输入路径为空",
                                         "AXMJSON 导入失败", "io.import.axmjson.input");
    }
    std::error_code path_error;
    if (!std::filesystem::exists(path, path_error) || path_error) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "AXMJSON 导入失败：输入文件不存在或路径不可访问",
                                         "AXMJSON 导入失败", "io.import.axmjson.path");
    }
    path_error.clear();
    if (!std::filesystem::is_regular_file(path, path_error) || path_error) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "AXMJSON 导入失败：输入路径不是可读取的普通文件",
                                         "AXMJSON 导入失败", "io.import.axmjson.open");
    }
    constexpr std::uintmax_t kExactBrepTextMaxBytes = static_cast<std::uintmax_t>(64) * 1024 * 1024;
    path_error.clear();
    const auto file_size = std::filesystem::file_size(path, path_error);
    if (path_error || file_size > kExactBrepTextMaxBytes) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "AXMJSON 导入失败：无法取得完整输入或文件超过 64 MiB 限额",
                                         "AXMJSON 导入失败", "io.import.axmjson.read");
    }
    std::ifstream in {std::string(path), std::ios::binary};
    if (!in) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "AXMJSON 导入失败：无法打开输入文件",
                                         "AXMJSON 导入失败", "io.import.axmjson.open");
    }
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    if (in.bad() || content.size() != file_size) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "AXMJSON 导入失败：读取输入文件失败",
                                         "AXMJSON 导入失败", "io.import.axmjson.read");
    }

    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::ExactBRep;
    record.label = std::string(path);
    record.bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});
    if (const auto parse_error = parse_exact_brep_json_body_record(content, "AXMJSON", record)) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoCorruptFile, *parse_error,
                                         "AXMJSON 导入失败", "io.import.axmjson.parse");
    }
    if (auto validation_failure = reject_invalid_exact_brep_record(
            *state_, record, "AXMJSON", "io.import.axmjson.validation")) {
        return std::move(*validation_failure);
    }

    IOImportBatchRollback rollback {*state_};
    const auto body_id = BodyId {state_->allocate_id()};
    state_->bodies.emplace(body_id.value, record);

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    const auto post_import = run_post_import_validation_pipeline(
        state_, body_id, options, std::string("AXMJSON"), issues, warnings);

    if (post_import.status != StatusCode::Ok || !post_import.value.has_value()) {
        return post_import;
    }
    auto result = ok_result(*post_import.value, state_->create_diagnostic("已导入AXMJSON模型", std::move(issues)));
    result.warnings = std::move(warnings);
    rollback.commit();
    return result;
}

Result<BodyId> IOService::import_stl(std::string_view path, const ImportOptions& options) {
    const auto fail_before_import = [this](StatusCode status, std::string_view code, std::string message,
                                           std::string_view stage) {
        return error_result<BodyId>(
            status, create_io_failure_diagnostic(
                        *state_, status, code, std::move(message),
                        "STL 导入失败", {0}, stage,
                        {{"materialized_body_count", 0.0, "count"}}));
    };
    if (path.empty()) {
        return fail_before_import(StatusCode::InvalidInput, diag_codes::kIoImportFailure,
                                  "STL 导入失败：输入路径为空", "io.import.stl.input");
    }
    if (!std::filesystem::exists(path)) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "STL 导入失败：输入文件不存在", "io.import.stl.path");
    }
    std::error_code file_type_error;
    if (!std::filesystem::is_regular_file(path, file_type_error) || file_type_error) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "STL 导入失败：输入路径不是可读取的普通文件", "io.import.stl.open");
    }
    std::ifstream in {std::string(path), std::ios::binary};
    if (!in) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "STL 导入失败：无法打开输入文件", "io.import.stl.open");
    }
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "STL 导入失败：读取输入文件失败", "io.import.stl.read");
    }
    detail::MeshRecord mesh;
    const auto parse_err = parse_stl_bytes(data, mesh);
    if (parse_err.has_value()) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  *parse_err, "io.import.stl.parse");
    }
    if (mesh.vertices.empty() || mesh.indices.empty() || (mesh.indices.size() % 3) != 0) {
        return fail_before_import(StatusCode::InvalidInput, diag_codes::kIoImportFailure,
                                  "STL 导入失败：网格数据无效", "io.import.stl.validation");
    }
    if (detail::has_out_of_range_indices(mesh.vertices, mesh.indices)) {
        return fail_before_import(StatusCode::InvalidInput, diag_codes::kIoImportFailure,
                                  "STL 导入失败：三角形索引越界", "io.import.stl.validation");
    }
    if (detail::has_degenerate_triangles(mesh.vertices, mesh.indices)) {
        return fail_before_import(StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                                  "STL 导入失败：存在退化三角形", "io.import.stl.validation");
    }

    IOImportBatchRollback rollback {*state_};
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
    const auto post_import = run_post_import_validation_pipeline(state_, body_id, options, std::string("STL"), issues, warnings);
    if (post_import.status != StatusCode::Ok || !post_import.value.has_value()) {
        return post_import;
    }
    auto result = ok_result(*post_import.value, state_->create_diagnostic("已导入STL模型", std::move(issues)));
    result.warnings = std::move(warnings);
    rollback.commit();
    return result;
}

Result<BodyId> IOService::import_gltf(std::string_view path, const ImportOptions& options) {
    const auto fail_before_import = [this](StatusCode status, std::string_view code, std::string message,
                                           std::string_view stage) {
        return error_result<BodyId>(
            status, create_io_failure_diagnostic(
                        *state_, status, code, std::move(message),
                        "glTF 导入失败", {0}, stage,
                        {{"materialized_body_count", 0.0, "count"}}));
    };
    if (path.empty()) {
        return fail_before_import(StatusCode::InvalidInput, diag_codes::kIoImportFailure,
                                  "glTF 导入失败：输入路径为空", "io.import.gltf.input");
    }
    if (!std::filesystem::exists(path)) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "glTF 导入失败：输入文件不存在", "io.import.gltf.path");
    }
    std::error_code file_type_error;
    if (!std::filesystem::is_regular_file(path, file_type_error) || file_type_error) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "glTF 导入失败：输入路径不是可读取的普通文件", "io.import.gltf.open");
    }
    std::ifstream in {std::string(path)};
    if (!in) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "glTF 导入失败：无法打开输入文件", "io.import.gltf.open");
    }
    std::string json_text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "glTF 导入失败：读取输入文件失败", "io.import.gltf.read");
    }
    detail::MeshRecord mesh;
    const auto parse_err = parse_gltf_embedded_minimal(json_text, mesh);
    if (parse_err.has_value()) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  *parse_err, "io.import.gltf.parse");
    }
    if (mesh.vertices.empty() || mesh.indices.empty() || (mesh.indices.size() % 3) != 0) {
        return fail_before_import(StatusCode::InvalidInput, diag_codes::kIoImportFailure,
                                  "glTF 导入失败：网格数据无效", "io.import.gltf.validation");
    }
    if (detail::has_out_of_range_indices(mesh.vertices, mesh.indices)) {
        return fail_before_import(StatusCode::InvalidInput, diag_codes::kIoImportFailure,
                                  "glTF 导入失败：三角形索引越界", "io.import.gltf.validation");
    }
    if (detail::has_degenerate_triangles(mesh.vertices, mesh.indices)) {
        return fail_before_import(StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                                  "glTF 导入失败：存在退化三角形", "io.import.gltf.validation");
    }

    IOImportBatchRollback rollback {*state_};
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
    const auto post_import = run_post_import_validation_pipeline(state_, body_id, options, std::string("glTF"), issues, warnings);
    if (post_import.status != StatusCode::Ok || !post_import.value.has_value()) {
        return post_import;
    }
    auto result = ok_result(*post_import.value, state_->create_diagnostic("已导入glTF模型", std::move(issues)));
    result.warnings = std::move(warnings);
    rollback.commit();
    return result;
}

Result<BodyId> IOService::import_iges(std::string_view path, const ImportOptions& options) {
    if (path.empty()) {
        return exact_brep_import_failure(*state_, StatusCode::InvalidInput,
                                         diag_codes::kIoImportFailure,
                                         "IGES 导入失败：输入路径为空",
                                         "IGES 导入失败", "io.import.iges.input");
    }
    std::error_code path_error;
    if (!std::filesystem::exists(path, path_error) || path_error) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "IGES 导入失败：输入文件不存在或路径不可访问",
                                         "IGES 导入失败", "io.import.iges.path");
    }
    path_error.clear();
    if (!std::filesystem::is_regular_file(path, path_error) || path_error) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "IGES 导入失败：输入路径不是可读取的普通文件",
                                         "IGES 导入失败", "io.import.iges.open");
    }
    constexpr std::size_t kStepIgesProbeMaxBytes = static_cast<std::size_t>(64) * 1024 * 1024;
    path_error.clear();
    const auto file_size = std::filesystem::file_size(path, path_error);
    if (path_error || file_size > kStepIgesProbeMaxBytes) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "IGES 导入失败：输入读取不完整或超过 64 MiB 限额",
                                         "IGES 导入失败", "io.import.iges.read");
    }
    const auto file_text = read_regular_file_bytes_limited(path, kStepIgesProbeMaxBytes);
    if (!file_text.has_value()) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "IGES 导入失败：无法打开或读取输入文件",
                                         "IGES 导入失败", "io.import.iges.open");
    }
    if (file_text->size() != file_size) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "IGES 导入失败：读取字节数与输入文件大小不一致",
                                         "IGES 导入失败", "io.import.iges.read");
    }
    if (iges_file_requires_full_standard_importer(std::string_view(*file_text))) {
        std::vector<Issue> issues;
        auto issue = detail::make_error_issue(
            diag_codes::kIgesStandardEntitiesUnsupported,
            "IGES 导入：检测到典型 IGES 卡片/DE 形态；当前内核仅支持 Axiom 元数据子集。完整实体交换需集成 IGES 内核。");
        issue.stage = "io.import.iges";
        issue.related_entities = {0};
        issue.numeric_evidence = {
            {"status_code", static_cast<Scalar>(StatusCode::NotImplemented), "enum"},
            {"related_entity_count", 1.0, "count"},
            {"input_byte_count", static_cast<Scalar>(file_text->size()), "byte"}};
        issues.push_back(std::move(issue));
        std::string scan_msg;
        std::size_t de_lines = 0;
        if (summarize_iges_standard_file_entities(std::string_view(*file_text), scan_msg, de_lines)) {
            auto info = detail::make_info_issue(diag_codes::kIgesStandardFileScanSummary, std::move(scan_msg));
            info.stage = "io.import.iges";
            issues.push_back(std::move(info));
        }
        return error_result<BodyId>(StatusCode::NotImplemented,
                                    state_->create_diagnostic("IGES 标准实体交换未实现", std::move(issues)));
    }

    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::ExactBRep;
    record.label = std::string(path);
    record.bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});
    if (const auto parse_error = parse_iges_subset_body_record(*file_text, record)) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoCorruptFile, *parse_error,
                                         "IGES 导入失败", "io.import.iges.parse");
    }
    if (auto validation_failure = reject_invalid_exact_brep_record(
            *state_, record, "IGES", "io.import.iges.validation")) {
        return std::move(*validation_failure);
    }

    IOImportBatchRollback rollback {*state_};
    const auto body_id = BodyId {state_->allocate_id()};
    state_->bodies.emplace(body_id.value, record);

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    const auto post_import = run_post_import_validation_pipeline(state_, body_id, options, std::string("IGES"), issues, warnings);
    if (post_import.status != StatusCode::Ok || !post_import.value.has_value()) {
        return post_import;
    }
    auto result = ok_result(*post_import.value, state_->create_diagnostic("已导入IGES模型", std::move(issues)));
    result.warnings = std::move(warnings);
    rollback.commit();
    return result;
}

Result<BodyId> IOService::import_brep(std::string_view path, const ImportOptions& options) {
    if (path.empty()) {
        return exact_brep_import_failure(*state_, StatusCode::InvalidInput,
                                         diag_codes::kIoImportFailure,
                                         "BREP 导入失败：输入路径为空",
                                         "BREP 导入失败", "io.import.brep.input");
    }
    std::error_code path_error;
    if (!std::filesystem::exists(path, path_error) || path_error) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "BREP 导入失败：输入文件不存在或路径不可访问",
                                         "BREP 导入失败", "io.import.brep.path");
    }
    path_error.clear();
    if (!std::filesystem::is_regular_file(path, path_error) || path_error) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "BREP 导入失败：输入路径不是可读取的普通文件",
                                         "BREP 导入失败", "io.import.brep.open");
    }
    constexpr std::uintmax_t kExactBrepTextMaxBytes = static_cast<std::uintmax_t>(64) * 1024 * 1024;
    path_error.clear();
    const auto file_size = std::filesystem::file_size(path, path_error);
    if (path_error || file_size > kExactBrepTextMaxBytes) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "BREP 导入失败：无法取得完整输入或文件超过 64 MiB 限额",
                                         "BREP 导入失败", "io.import.brep.read");
    }
    std::ifstream in {std::string(path), std::ios::binary};
    if (!in) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "BREP 导入失败：无法打开输入文件",
                                         "BREP 导入失败", "io.import.brep.open");
    }
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad() || raw.size() != file_size) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoImportFailure,
                                         "BREP 导入失败：读取输入文件失败",
                                         "BREP 导入失败", "io.import.brep.read");
    }
    if (raw.find("AXIOM_BREP_INTERCHANGE") == std::string::npos) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoCorruptFile,
                                         "BREP 导入失败：缺少 AXIOM_BREP_INTERCHANGE 文件头",
                                         "BREP 导入失败", "io.import.brep.parse");
    }
    const std::string content = strip_leading_hash_lines(raw);

    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::ExactBRep;
    record.label = std::string(path);
    record.bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});
    if (const auto parse_error = parse_exact_brep_json_body_record(content, "AXIOM_BREP", record)) {
        return exact_brep_import_failure(*state_, StatusCode::OperationFailed,
                                         diag_codes::kIoCorruptFile, *parse_error,
                                         "BREP 导入失败", "io.import.brep.parse");
    }
    if (auto validation_failure = reject_invalid_exact_brep_record(
            *state_, record, "BREP", "io.import.brep.validation")) {
        return std::move(*validation_failure);
    }

    IOImportBatchRollback rollback {*state_};
    const auto body_id = BodyId {state_->allocate_id()};
    state_->bodies.emplace(body_id.value, record);

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    const auto post_import = run_post_import_validation_pipeline(state_, body_id, options, std::string("BREP"), issues, warnings);
    if (post_import.status != StatusCode::Ok || !post_import.value.has_value()) {
        return post_import;
    }
    auto result = ok_result(*post_import.value, state_->create_diagnostic("已导入BREP模型", std::move(issues)));
    result.warnings = std::move(warnings);
    rollback.commit();
    return result;
}

Result<BodyId> IOService::import_obj(std::string_view path, const ImportOptions& options) {
    const auto fail_before_import = [this](StatusCode status, std::string_view code, std::string message,
                                           std::string_view stage) {
        return error_result<BodyId>(
            status, create_io_failure_diagnostic(
                        *state_, status, code, std::move(message),
                        "OBJ 导入失败", {0}, stage,
                        {{"materialized_body_count", 0.0, "count"}}));
    };
    if (path.empty()) {
        return fail_before_import(StatusCode::InvalidInput, diag_codes::kIoImportFailure,
                                  "OBJ 导入失败：输入路径为空", "io.import.obj.input");
    }
    if (!std::filesystem::exists(path)) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "OBJ 导入失败：输入文件不存在", "io.import.obj.path");
    }
    std::error_code file_type_error;
    if (!std::filesystem::is_regular_file(path, file_type_error) || file_type_error) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "OBJ 导入失败：输入路径不是可读取的普通文件", "io.import.obj.open");
    }
    std::ifstream in {std::string(path)};
    if (!in) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "OBJ 导入失败：无法打开输入文件", "io.import.obj.open");
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    detail::MeshRecord mesh;
    const auto err = parse_obj_text_to_mesh(text, mesh);
    if (err.has_value()) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure, *err,
                                  "io.import.obj.parse");
    }
    if (detail::has_degenerate_triangles(mesh.vertices, mesh.indices)) {
        return fail_before_import(StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                                  "OBJ 导入失败：存在退化三角形", "io.import.obj.validation");
    }
    mesh.label = "obj_import";
    mesh.tessellation_strategy = "io_import_obj";
    mesh.bbox = detail::mesh_bbox_from_vertices(mesh.vertices);

    IOImportBatchRollback rollback {*state_};
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
    const auto post_import = run_post_import_validation_pipeline(state_, body_id, options, std::string("OBJ"), issues, warnings);
    if (post_import.status != StatusCode::Ok || !post_import.value.has_value()) {
        return post_import;
    }
    auto result = ok_result(*post_import.value, state_->create_diagnostic("已导入OBJ模型", std::move(issues)));
    result.warnings = std::move(warnings);
    rollback.commit();
    return result;
}

Result<BodyId> IOService::import_3mf(std::string_view path, const ImportOptions& options) {
    const auto fail_before_import = [this](StatusCode status, std::string_view code, std::string message,
                                           std::string_view stage) {
        return error_result<BodyId>(
            status, create_io_failure_diagnostic(
                        *state_, status, code, std::move(message),
                        "3MF 导入失败", {0}, stage,
                        {{"materialized_body_count", 0.0, "count"}}));
    };
    if (path.empty()) {
        return fail_before_import(StatusCode::InvalidInput, diag_codes::kIoImportFailure,
                                  "3MF 导入失败：输入路径为空", "io.import.3mf.input");
    }
    if (!std::filesystem::exists(path)) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "3MF 导入失败：输入文件不存在", "io.import.3mf.path");
    }
    std::error_code file_type_error;
    if (!std::filesystem::is_regular_file(path, file_type_error) || file_type_error) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "3MF 导入失败：输入路径不是可读取的普通文件", "io.import.3mf.open");
    }
    std::ifstream in {std::string(path), std::ios::binary};
    if (!in) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "3MF 导入失败：无法打开输入文件", "io.import.3mf.open");
    }
    std::vector<std::uint8_t> zip_data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "3MF 导入失败：读取输入文件失败", "io.import.3mf.read");
    }
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> files;
    if (!zip_parse_stored_locals(zip_data, files)) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "3MF 导入失败：ZIP 解析失败或非 STORE 压缩包", "io.import.3mf.parse");
    }
    std::string model_xml;
    if (!extract_3mf_model_xml(files, model_xml)) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "3MF 导入失败：未找到 3dmodel.model", "io.import.3mf.parse");
    }
    detail::MeshRecord mesh;
    try {
        const auto perr = parse_3mf_model_xml_to_mesh(model_xml, mesh);
        if (perr.has_value()) {
            return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                      *perr, "io.import.3mf.parse");
        }
    } catch (const std::invalid_argument&) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "3MF 解析失败：网格数值非法", "io.import.3mf.parse");
    } catch (const std::out_of_range&) {
        return fail_before_import(StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                  "3MF 解析失败：网格数值超出范围", "io.import.3mf.parse");
    }
    for (const auto& vertex : mesh.vertices) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z)) {
            return fail_before_import(StatusCode::InvalidInput, diag_codes::kIoImportFailure,
                                      "3MF 导入失败：顶点坐标非有限值", "io.import.3mf.validation");
        }
    }
    if (detail::has_degenerate_triangles(mesh.vertices, mesh.indices)) {
        return fail_before_import(StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                                  "3MF 导入失败：存在退化三角形", "io.import.3mf.validation");
    }
    mesh.label = "3mf_import";
    mesh.tessellation_strategy = "io_import_3mf";
    mesh.bbox = detail::mesh_bbox_from_vertices(mesh.vertices);

    IOImportBatchRollback rollback {*state_};
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
    const auto post_import = run_post_import_validation_pipeline(state_, body_id, options, std::string("3MF"), issues, warnings);
    if (post_import.status != StatusCode::Ok || !post_import.value.has_value()) {
        return post_import;
    }
    auto result = ok_result(*post_import.value, state_->create_diagnostic("已导入3MF模型", std::move(issues)));
    result.warnings = std::move(warnings);
    rollback.commit();
    return result;
}

Result<void> IOService::export_axmjson(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return io_failed_void(
            *state_, StatusCode::InvalidInput, diag_codes::kIoExportFailure,
            "AXMJSON 导出失败：目标实体不存在或输出路径为空", "AXMJSON 导出失败",
            {body_id.value}, "io.export.axmjson.input",
            {{"body_exists", detail::has_body(*state_, body_id) ? 1.0 : 0.0, "bool"},
             {"path_length", static_cast<Scalar>(path.size()), "byte"}});
    }
    const auto wx = reject_if_export_directory_not_writable(*state_, path);
    if (wx.status != StatusCode::Ok) {
        return wrap_io_failed_void(
            *state_, wx, diag_codes::kIoExportFailure,
            "AXMJSON 导出路径校验失败", "AXMJSON 导出失败",
            {body_id.value}, "io.export.axmjson.path",
            {{"path_length", static_cast<Scalar>(path.size()), "byte"}});
    }
    std::ofstream out {std::string(path)};
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "AXMJSON 导出失败：无法打开输出文件", "AXMJSON 导出失败",
            {body_id.value}, "io.export.axmjson.open",
            {{"path_length", static_cast<Scalar>(path.size()), "byte"}});
    }
    const auto body_it = state_->bodies.find(body_id.value);
    const auto& body = body_it->second;
    write_axmjson_payload(out, body, options, "AXMJSON");
    out.flush();
    out.close();
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "AXMJSON 导出失败：写入输出文件失败", "AXMJSON 导出失败",
            {body_id.value}, "io.export.axmjson.write",
            {{"metadata_enabled", options.embed_metadata ? 1.0 : 0.0, "bool"}});
    }
    return ok_void(state_->create_diagnostic("已导出AXMJSON模型"));
}

Result<void> IOService::export_iges(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return io_failed_void(
            *state_, StatusCode::InvalidInput, diag_codes::kIoExportFailure,
            "IGES 导出失败：目标实体不存在或输出路径为空", "IGES 导出失败",
            {body_id.value}, "io.export.iges.input",
            {{"body_exists", detail::has_body(*state_, body_id) ? 1.0 : 0.0, "bool"},
             {"path_length", static_cast<Scalar>(path.size()), "byte"}});
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) {
        return wrap_io_failed_void(
            *state_, ensured, diag_codes::kIoExportFailure,
            "IGES 导出父目录创建失败", "IGES 导出失败", {body_id.value},
            "io.export.iges.path");
    }
    const auto wx_iges = reject_if_export_directory_not_writable(*state_, path);
    if (wx_iges.status != StatusCode::Ok) {
        return wrap_io_failed_void(
            *state_, wx_iges, diag_codes::kIoExportFailure,
            "IGES 导出路径校验失败", "IGES 导出失败", {body_id.value},
            "io.export.iges.path");
    }
    std::ofstream out {std::string(path)};
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "IGES 导出失败：无法打开输出文件", "IGES 导出失败",
            {body_id.value}, "io.export.iges.open");
    }
    const auto body_it = state_->bodies.find(body_id.value);
    const auto& body = body_it->second;
    out << "START\n";
    out << "1H,,1HAXIOM,AxiomKernel IGES metadata interchange (subset)\n";
    if (options.embed_metadata) {
        const std::string iges_ent =
            body.io_iges_entity_hint.empty() ? "186_SUBSET" : body.io_iges_entity_hint;
        out << "AXIOM_IGES_ENTITY " << iges_ent << "\n";
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
    out.flush();
    out.close();
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "IGES 导出失败：写入输出文件失败", "IGES 导出失败",
            {body_id.value}, "io.export.iges.write",
            {{"metadata_enabled", options.embed_metadata ? 1.0 : 0.0, "bool"}});
    }
    return ok_void(state_->create_diagnostic("已导出IGES模型"));
}

Result<void> IOService::export_brep(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return io_failed_void(
            *state_, StatusCode::InvalidInput, diag_codes::kIoExportFailure,
            "BREP 导出失败：目标实体不存在或输出路径为空", "BREP 导出失败",
            {body_id.value}, "io.export.brep.input",
            {{"body_exists", detail::has_body(*state_, body_id) ? 1.0 : 0.0, "bool"},
             {"path_length", static_cast<Scalar>(path.size()), "byte"}});
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) {
        return wrap_io_failed_void(
            *state_, ensured, diag_codes::kIoExportFailure,
            "BREP 导出父目录创建失败", "BREP 导出失败", {body_id.value},
            "io.export.brep.path");
    }
    const auto wx_brep = reject_if_export_directory_not_writable(*state_, path);
    if (wx_brep.status != StatusCode::Ok) {
        return wrap_io_failed_void(
            *state_, wx_brep, diag_codes::kIoExportFailure,
            "BREP 导出路径校验失败", "BREP 导出失败", {body_id.value},
            "io.export.brep.path");
    }
    std::ofstream out {std::string(path)};
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "BREP 导出失败：无法打开输出文件", "BREP 导出失败",
            {body_id.value}, "io.export.brep.open");
    }
    const auto body_it = state_->bodies.find(body_id.value);
    const auto& body = body_it->second;
    out << "# AXIOM_BREP_INTERCHANGE v1\n";
    write_axmjson_payload(out, body, options, "AXIOM_BREP");
    out.flush();
    out.close();
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "BREP 导出失败：写入输出文件失败", "BREP 导出失败",
            {body_id.value}, "io.export.brep.write",
            {{"metadata_enabled", options.embed_metadata ? 1.0 : 0.0, "bool"}});
    }
    return ok_void(state_->create_diagnostic("已导出BREP模型"));
}

Result<void> IOService::export_obj(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return io_failed_void(
            *state_, StatusCode::InvalidInput, diag_codes::kIoExportFailure,
            "OBJ 导出失败：目标实体不存在或输出路径为空", "OBJ 导出失败", {body_id.value},
            "io.export.obj.input",
            {{"body_exists", detail::has_body(*state_, body_id) ? 1.0 : 0.0, "bool"},
             {"path_length", static_cast<Scalar>(path.size()), "byte"}});
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, ensured, body_id, "io.export.obj.path");
    }
    const auto wx_obj = reject_if_export_directory_not_writable(*state_, path);
    if (wx_obj.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, wx_obj, body_id, "io.export.obj.path");
    }

    MeshExportRollback rollback {*state_};
    RepresentationConversionService convert {state_};
    const auto mesh_id = convert.brep_to_mesh(body_id, TessellationOptions{});
    if (mesh_id.status != StatusCode::Ok || !mesh_id.value.has_value()) {
        return bind_mesh_export_failure(*state_, error_void(mesh_id.status, mesh_id.diagnostic_id),
                                        body_id, "io.export.obj.convert");
    }
    const auto strict_gate = mesh_export_strict_gate(*state_, convert, *mesh_id.value, options, body_id);
    if (strict_gate.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, strict_gate, body_id, "io.export.mesh_strict_qa");
    }
    const auto mesh_it = state_->meshes.find(mesh_id.value->value);
    if (mesh_it == state_->meshes.end()) {
        return io_failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
                              "OBJ 导出失败：网格记录不存在", "OBJ 导出失败", {body_id.value},
                              "io.export.obj.mesh", {{"mesh_exists", 0.0, "bool"}});
    }
    const auto& mesh = mesh_it->second;
    if (mesh.vertices.empty() || mesh.indices.empty() || (mesh.indices.size() % 3) != 0 ||
        detail::has_out_of_range_indices(mesh.vertices, mesh.indices) ||
        std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [](const Point3& point) {
            return !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z);
        })) {
        return io_failed_void(
            *state_, StatusCode::InvalidInput, diag_codes::kIoExportFailure,
            "OBJ 导出失败：网格数据无效", "OBJ 导出失败", {body_id.value},
            "io.export.obj.mesh",
            {{"vertex_count", static_cast<Scalar>(mesh.vertices.size()), "count"},
             {"index_count", static_cast<Scalar>(mesh.indices.size()), "count"}});
    }

    std::ofstream out {std::string(path)};
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "OBJ 导出失败：无法打开输出文件", "OBJ 导出失败", {body_id.value},
            "io.export.obj.open", {{"path_length", static_cast<Scalar>(path.size()), "byte"}});
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
    out.flush();
    out.close();
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "OBJ 导出失败：写入输出文件失败", "OBJ 导出失败", {body_id.value},
            "io.export.obj.write",
            {{"vertex_count", static_cast<Scalar>(mesh.vertices.size()), "count"},
             {"triangle_count", static_cast<Scalar>(mesh.indices.size() / 3), "count"}});
    }
    const auto sidecar = mesh_export_write_validation_sidecar(*state_, convert, *mesh_id.value, path, options);
    if (sidecar.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, sidecar, body_id, "io.export.obj.sidecar");
    }
    rollback.completed = true;
    return merge_mesh_export_sidecar_summary(*state_, sidecar, "已导出OBJ模型");
}

Result<void> IOService::export_3mf(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return io_failed_void(
            *state_, StatusCode::InvalidInput, diag_codes::kIoExportFailure,
            "3MF 导出失败：目标实体不存在或输出路径为空", "3MF 导出失败", {body_id.value},
            "io.export.3mf.input",
            {{"body_exists", detail::has_body(*state_, body_id) ? 1.0 : 0.0, "bool"},
             {"path_length", static_cast<Scalar>(path.size()), "byte"}});
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, ensured, body_id, "io.export.3mf.path");
    }
    const auto wx_3mf = reject_if_export_directory_not_writable(*state_, path);
    if (wx_3mf.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, wx_3mf, body_id, "io.export.3mf.path");
    }

    MeshExportRollback rollback {*state_};
    RepresentationConversionService convert {state_};
    const auto mesh_id = convert.brep_to_mesh(body_id, TessellationOptions{});
    if (mesh_id.status != StatusCode::Ok || !mesh_id.value.has_value()) {
        return bind_mesh_export_failure(*state_, error_void(mesh_id.status, mesh_id.diagnostic_id),
                                        body_id, "io.export.3mf.convert");
    }
    const auto strict_gate = mesh_export_strict_gate(*state_, convert, *mesh_id.value, options, body_id);
    if (strict_gate.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, strict_gate, body_id, "io.export.mesh_strict_qa");
    }
    const auto mesh_it = state_->meshes.find(mesh_id.value->value);
    if (mesh_it == state_->meshes.end()) {
        return io_failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
                              "3MF 导出失败：网格记录不存在", "3MF 导出失败", {body_id.value},
                              "io.export.3mf.mesh", {{"mesh_exists", 0.0, "bool"}});
    }
    const auto& mesh = mesh_it->second;
    if (mesh.vertices.empty() || mesh.indices.empty() || (mesh.indices.size() % 3) != 0 ||
        detail::has_out_of_range_indices(mesh.vertices, mesh.indices) ||
        std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [](const Point3& point) {
            return !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z);
        })) {
        return io_failed_void(
            *state_, StatusCode::InvalidInput, diag_codes::kIoExportFailure,
            "3MF 导出失败：网格数据无效", "3MF 导出失败", {body_id.value},
            "io.export.3mf.mesh",
            {{"vertex_count", static_cast<Scalar>(mesh.vertices.size()), "count"},
             {"index_count", static_cast<Scalar>(mesh.indices.size()), "count"}});
    }

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
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "3MF 导出失败：无法打开输出文件", "3MF 导出失败", {body_id.value},
            "io.export.3mf.open", {{"archive_byte_count", static_cast<Scalar>(zip_bytes.size()), "byte"}});
    }
    out.write(reinterpret_cast<const char*>(zip_bytes.data()),
              static_cast<std::streamsize>(zip_bytes.size()));
    out.flush();
    out.close();
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "3MF 导出失败：写入输出文件失败", "3MF 导出失败", {body_id.value},
            "io.export.3mf.write",
            {{"archive_byte_count", static_cast<Scalar>(zip_bytes.size()), "byte"}});
    }
    const auto sidecar = mesh_export_write_validation_sidecar(*state_, convert, *mesh_id.value, path, options);
    if (sidecar.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, sidecar, body_id, "io.export.3mf.sidecar");
    }
    rollback.completed = true;
    return merge_mesh_export_sidecar_summary(*state_, sidecar, "已导出3MF模型");
}

Result<void> IOService::export_gltf(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return io_failed_void(
            *state_, StatusCode::InvalidInput, diag_codes::kIoExportFailure,
            "glTF 导出失败：目标实体不存在或输出路径为空", "glTF 导出失败", {body_id.value},
            "io.export.gltf.input",
            {{"body_exists", detail::has_body(*state_, body_id) ? 1.0 : 0.0, "bool"},
             {"path_length", static_cast<Scalar>(path.size()), "byte"}});
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, ensured, body_id, "io.export.gltf.path");
    }
    const auto wx_gltf = reject_if_export_directory_not_writable(*state_, path);
    if (wx_gltf.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, wx_gltf, body_id, "io.export.gltf.path");
    }

    MeshExportRollback rollback {*state_};
    RepresentationConversionService convert {state_};
    const auto mesh_id = convert.brep_to_mesh(body_id, TessellationOptions{});
    if (mesh_id.status != StatusCode::Ok || !mesh_id.value.has_value()) {
        return bind_mesh_export_failure(*state_, error_void(mesh_id.status, mesh_id.diagnostic_id),
                                        body_id, "io.export.gltf.convert");
    }
    const auto strict_gate = mesh_export_strict_gate(*state_, convert, *mesh_id.value, options, body_id);
    if (strict_gate.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, strict_gate, body_id, "io.export.mesh_strict_qa");
    }

    const auto mesh_it = state_->meshes.find(mesh_id.value->value);
    if (mesh_it == state_->meshes.end()) {
        return io_failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
                              "glTF 导出失败：网格记录不存在", "glTF 导出失败", {body_id.value},
                              "io.export.gltf.mesh", {{"mesh_exists", 0.0, "bool"}});
    }
    const auto& mesh = mesh_it->second;
    if (mesh.vertices.empty() || mesh.indices.empty() || (mesh.indices.size() % 3) != 0 ||
        detail::has_out_of_range_indices(mesh.vertices, mesh.indices) ||
        std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [](const Point3& point) {
            return !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z);
        })) {
        return io_failed_void(
            *state_, StatusCode::InvalidInput, diag_codes::kIoExportFailure,
            "glTF 导出失败：网格数据无效", "glTF 导出失败", {body_id.value},
            "io.export.gltf.mesh",
            {{"vertex_count", static_cast<Scalar>(mesh.vertices.size()), "count"},
             {"index_count", static_cast<Scalar>(mesh.indices.size()), "count"}});
    }

    const auto float_limit = static_cast<Scalar>(std::numeric_limits<float>::max());
    if (std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [float_limit](const Point3& point) {
            return std::abs(point.x) > float_limit || std::abs(point.y) > float_limit ||
                   std::abs(point.z) > float_limit;
        })) {
        return io_failed_void(
            *state_, StatusCode::InvalidInput, diag_codes::kIoExportFailure,
            "glTF 导出失败：坐标超出 float32 范围", "glTF 导出失败", {body_id.value},
            "io.export.gltf.mesh",
            {{"float32_max", float_limit, "model_unit"},
             {"vertex_count", static_cast<Scalar>(mesh.vertices.size()), "count"}});
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
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "glTF 导出失败：无法打开输出文件", "glTF 导出失败", {body_id.value},
            "io.export.gltf.open", {{"buffer_byte_count", static_cast<Scalar>(buffer.size()), "byte"}});
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

    out.flush();
    out.close();
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "glTF 导出失败：写入输出文件失败", "glTF 导出失败", {body_id.value},
            "io.export.gltf.write",
            {{"buffer_byte_count", static_cast<Scalar>(buffer.size()), "byte"},
             {"vertex_count", static_cast<Scalar>(vertex_count), "count"},
             {"index_count", static_cast<Scalar>(index_count), "count"}});
    }
    const auto sidecar = mesh_export_write_validation_sidecar(*state_, convert, *mesh_id.value, path, options);
    if (sidecar.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, sidecar, body_id, "io.export.gltf.sidecar");
    }
    rollback.completed = true;
    return merge_mesh_export_sidecar_summary(*state_, sidecar, "已导出glTF模型");
}

Result<void> IOService::export_stl(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return io_failed_void(
            *state_, StatusCode::InvalidInput, diag_codes::kIoExportFailure,
            "STL 导出失败：目标实体不存在或输出路径为空", "STL 导出失败", {body_id.value},
            "io.export.stl.input",
            {{"body_exists", detail::has_body(*state_, body_id) ? 1.0 : 0.0, "bool"},
             {"path_length", static_cast<Scalar>(path.size()), "byte"}});
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, ensured, body_id, "io.export.stl.path");
    }
    const auto wx_stl = reject_if_export_directory_not_writable(*state_, path);
    if (wx_stl.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, wx_stl, body_id, "io.export.stl.path");
    }

    MeshExportRollback rollback {*state_};
    RepresentationConversionService convert {state_};
    const auto mesh_id = convert.brep_to_mesh(body_id, TessellationOptions{});
    if (mesh_id.status != StatusCode::Ok || !mesh_id.value.has_value()) {
        return bind_mesh_export_failure(*state_, error_void(mesh_id.status, mesh_id.diagnostic_id),
                                        body_id, "io.export.stl.convert");
    }
    const auto strict_gate = mesh_export_strict_gate(*state_, convert, *mesh_id.value, options, body_id);
    if (strict_gate.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, strict_gate, body_id, "io.export.mesh_strict_qa");
    }
    const auto mesh_it = state_->meshes.find(mesh_id.value->value);
    if (mesh_it == state_->meshes.end()) {
        return io_failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
                              "STL 导出失败：网格记录不存在", "STL 导出失败", {body_id.value},
                              "io.export.stl.mesh", {{"mesh_exists", 0.0, "bool"}});
    }
    const auto& mesh = mesh_it->second;
    if (mesh.vertices.empty() || mesh.indices.empty() || (mesh.indices.size() % 3) != 0 ||
        detail::has_out_of_range_indices(mesh.vertices, mesh.indices) ||
        std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [](const Point3& point) {
            return !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z);
        })) {
        return io_failed_void(
            *state_, StatusCode::InvalidInput, diag_codes::kIoExportFailure,
            "STL 导出失败：网格数据无效", "STL 导出失败", {body_id.value},
            "io.export.stl.mesh",
            {{"vertex_count", static_cast<Scalar>(mesh.vertices.size()), "count"},
             {"index_count", static_cast<Scalar>(mesh.indices.size()), "count"}});
    }

    std::ofstream out {std::string(path)};
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "STL 导出失败：无法打开输出文件", "STL 导出失败", {body_id.value},
            "io.export.stl.open", {{"triangle_count", static_cast<Scalar>(mesh.indices.size() / 3), "count"}});
    }

    out << "solid axiom\n";
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const auto i0 = static_cast<std::size_t>(mesh.indices[i]);
        const auto i1 = static_cast<std::size_t>(mesh.indices[i + 1]);
        const auto i2 = static_cast<std::size_t>(mesh.indices[i + 2]);
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

    out.flush();
    out.close();
    if (!out) {
        return io_failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "STL 导出失败：写入输出文件失败", "STL 导出失败", {body_id.value},
            "io.export.stl.write",
            {{"triangle_count", static_cast<Scalar>(mesh.indices.size() / 3), "count"}});
    }
    const auto sidecar = mesh_export_write_validation_sidecar(*state_, convert, *mesh_id.value, path, options);
    if (sidecar.status != StatusCode::Ok) {
        return bind_mesh_export_failure(*state_, sidecar, body_id, "io.export.stl.sidecar");
    }
    rollback.completed = true;
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
