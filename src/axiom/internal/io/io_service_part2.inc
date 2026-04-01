    std::error_code ec;
    const auto normalized = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    if (ec) {
        return ok_result(std::filesystem::path(path).lexically_normal().string(),
                         state_->create_diagnostic("已规范化路径"));
    }
    return ok_result(normalized.string(), state_->create_diagnostic("已规范化路径"));
}

Result<BodyId> IOService::import_step_default(std::string_view path) {
    return import_step(path, ImportOptions{});
}

Result<BodyId> IOService::import_axmjson_default(std::string_view path) {
    return import_axmjson(path, ImportOptions{});
}

Result<BodyId> IOService::import_stl_default(std::string_view path) {
    return import_stl(path, ImportOptions{});
}

Result<BodyId> IOService::import_gltf_default(std::string_view path) {
    return import_gltf(path, ImportOptions{});
}

Result<void> IOService::export_step_default(BodyId body_id, std::string_view path) {
    return export_step(body_id, path, ExportOptions{});
}

Result<void> IOService::export_axmjson_default(BodyId body_id, std::string_view path) {
    return export_axmjson(body_id, path, ExportOptions{});
}

Result<void> IOService::export_gltf_default(BodyId body_id, std::string_view path) {
    return export_gltf(body_id, path, ExportOptions{});
}

Result<void> IOService::export_stl_default(BodyId body_id, std::string_view path) {
    return export_stl(body_id, path, ExportOptions{});
}

Result<BodyId> IOService::import_iges_default(std::string_view path) {
    return import_iges(path, ImportOptions{});
}

Result<BodyId> IOService::import_brep_default(std::string_view path) {
    return import_brep(path, ImportOptions{});
}

Result<BodyId> IOService::import_obj_default(std::string_view path) {
    return import_obj(path, ImportOptions{});
}

Result<BodyId> IOService::import_3mf_default(std::string_view path) {
    return import_3mf(path, ImportOptions{});
}

Result<void> IOService::export_iges_default(BodyId body_id, std::string_view path) {
    return export_iges(body_id, path, ExportOptions{});
}

Result<void> IOService::export_brep_default(BodyId body_id, std::string_view path) {
    return export_brep(body_id, path, ExportOptions{});
}

Result<void> IOService::export_obj_default(BodyId body_id, std::string_view path) {
    return export_obj(body_id, path, ExportOptions{});
}

Result<void> IOService::export_3mf_default(BodyId body_id, std::string_view path) {
    return export_3mf(body_id, path, ExportOptions{});
}

Result<BodyId> IOService::import_auto(std::string_view path, const ImportOptions& options) {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<BodyId>(fmt.status, fmt.diagnostic_id);
    }
    if (*fmt.value == "step") return import_step(path, options);
    if (*fmt.value == "axmjson") return import_axmjson(path, options);
    if (*fmt.value == "stl") return import_stl(path, options);
    if (*fmt.value == "gltf") return import_gltf(path, options);
    if (*fmt.value == "iges") return import_iges(path, options);
    if (*fmt.value == "brep") return import_brep(path, options);
    if (*fmt.value == "obj") return import_obj(path, options);
    if (*fmt.value == "3mf") return import_3mf(path, options);
    return detail::invalid_input_result<BodyId>(
        *state_, diag_codes::kIoImportFailure, "自动导入失败：不支持的文件格式", "自动导入失败");
}

Result<void> IOService::export_auto(BodyId body_id, std::string_view path, const ExportOptions& options) {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<void>(fmt.status, fmt.diagnostic_id);
    }
    if (*fmt.value == "step") return export_step(body_id, path, options);
    if (*fmt.value == "axmjson") return export_axmjson(body_id, path, options);
    if (*fmt.value == "gltf") return export_gltf(body_id, path, options);
    if (*fmt.value == "stl") return export_stl(body_id, path, options);
    if (*fmt.value == "iges") return export_iges(body_id, path, options);
    if (*fmt.value == "brep") return export_brep(body_id, path, options);
    if (*fmt.value == "obj") return export_obj(body_id, path, options);
    if (*fmt.value == "3mf") return export_3mf(body_id, path, options);
    return detail::invalid_input_void(
        *state_, diag_codes::kIoExportFailure, "自动导出失败：不支持的文件格式", "自动导出失败");
}

Result<std::vector<BodyId>> IOService::import_many_step(std::span<const std::string> paths, const ImportOptions& options) {
    if (paths.empty()) {
        return detail::invalid_input_result<std::vector<BodyId>>(
            *state_, diag_codes::kIoImportFailure, "批量STEP导入失败：路径列表为空", "批量STEP导入失败");
    }
    std::vector<BodyId> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto r = import_step(p, options);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::vector<BodyId>>(r.status, r.diagnostic_id);
        out.push_back(*r.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已完成批量STEP导入"));
}

Result<std::vector<BodyId>> IOService::import_many_axmjson(std::span<const std::string> paths, const ImportOptions& options) {
    if (paths.empty()) {
        return detail::invalid_input_result<std::vector<BodyId>>(
            *state_, diag_codes::kIoImportFailure, "批量AXMJSON导入失败：路径列表为空", "批量AXMJSON导入失败");
    }
    std::vector<BodyId> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto r = import_axmjson(p, options);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::vector<BodyId>>(r.status, r.diagnostic_id);
        out.push_back(*r.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已完成批量AXMJSON导入"));
}

Result<void> IOService::export_many_step(std::span<const BodyId> body_ids, std::span<const std::string> paths, const ExportOptions& options) {
    if (body_ids.empty() || body_ids.size() != paths.size()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "批量STEP导出失败：输入数量不一致", "批量STEP导出失败");
    }
    for (std::size_t i = 0; i < body_ids.size(); ++i) {
        const auto r = export_step(body_ids[i], paths[i], options);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已完成批量STEP导出"));
}

Result<void> IOService::export_many_axmjson(std::span<const BodyId> body_ids, std::span<const std::string> paths, const ExportOptions& options) {
    if (body_ids.empty() || body_ids.size() != paths.size()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "批量AXMJSON导出失败：输入数量不一致", "批量AXMJSON导出失败");
    }
    for (std::size_t i = 0; i < body_ids.size(); ++i) {
        const auto r = export_axmjson(body_ids[i], paths[i], options);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已完成批量AXMJSON导出"));
}

Result<std::vector<BodyId>> IOService::import_many_auto(std::span<const std::string> paths, const ImportOptions& options) {
    if (paths.empty()) {
        return detail::invalid_input_result<std::vector<BodyId>>(
            *state_, diag_codes::kIoImportFailure, "批量自动导入失败：路径列表为空", "批量自动导入失败");
    }
    std::vector<BodyId> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto r = import_auto(p, options);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::vector<BodyId>>(r.status, r.diagnostic_id);
        out.push_back(*r.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已完成批量自动导入"));
}

Result<void> IOService::export_many_auto(std::span<const BodyId> body_ids, std::span<const std::string> paths, const ExportOptions& options) {
    if (body_ids.empty() || body_ids.size() != paths.size()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "批量自动导出失败：输入数量不一致", "批量自动导出失败");
    }
    for (std::size_t i = 0; i < body_ids.size(); ++i) {
        const auto r = export_auto(body_ids[i], paths[i], options);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已完成批量自动导出"));
}

Result<std::uint64_t> IOService::count_lines(std::string_view path) const {
    std::ifstream in{std::string(path)};
    if (!in) {
        return detail::failed_result<std::uint64_t>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                                    "行数统计失败：无法打开文件", "行数统计失败");
    }
    std::uint64_t lines = 0;
    std::string tmp;
    while (std::getline(in, tmp)) ++lines;
    return ok_result<std::uint64_t>(lines, state_->create_diagnostic("已统计文件行数"));
}

Result<std::string> IOService::read_text_preview(std::string_view path, std::uint64_t max_chars) const {
    if (max_chars == 0) {
        return detail::invalid_input_result<std::string>(*state_, diag_codes::kIoImportFailure, "文本预览失败：长度上限为0", "文本预览失败");
    }
    std::ifstream in{std::string(path)};
    if (!in) {
        return detail::failed_result<std::string>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                                  "文本预览失败：无法打开文件", "文本预览失败");
    }
    std::string content;
    content.resize(static_cast<std::size_t>(max_chars));
    in.read(content.data(), static_cast<std::streamsize>(max_chars));
    content.resize(static_cast<std::size_t>(in.gcount()));
    return ok_result(std::move(content), state_->create_diagnostic("已读取文本预览"));
}

Result<void> IOService::write_text_snapshot(std::string_view path, std::string_view content) const {
    std::ofstream out{std::string(path)};
    if (!out) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
                                   "文本快照写出失败：无法打开输出文件", "文本快照写出失败");
    }
    out << content;
    return ok_void(state_->create_diagnostic("已写出文本快照"));
}

Result<void> IOService::export_body_summary_txt(BodyId body_id, std::string_view path) const {
    const auto body_it = state_->bodies.find(body_id.value);
    if (body_it == state_->bodies.end()) {
        return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle, "体摘要导出失败：目标体不存在", "体摘要导出失败");
    }
    std::ofstream out{std::string(path)};
    if (!out) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "体摘要导出失败：无法打开输出文件", "体摘要导出失败");
    }
    const auto& b = body_it->second;
    out << "BodyId: " << body_id.value << "\n";
    out << "Kind: " << body_kind_name(b.kind) << "\n";
    out << "Label: " << b.label << "\n";
    out << "BBox: " << b.bbox.min.x << " " << b.bbox.min.y << " " << b.bbox.min.z << " "
        << b.bbox.max.x << " " << b.bbox.max.y << " " << b.bbox.max.z << "\n";
    return ok_void(state_->create_diagnostic("已导出体摘要文本"));
}

Result<void> IOService::validate_import_path(std::string_view path) const {
    if (path.empty() || !std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        return detail::invalid_input_void(*state_, diag_codes::kIoImportFailure, "导入路径校验失败：路径无效", "导入路径校验失败");
    }
    return ok_void(state_->create_diagnostic("导入路径校验通过"));
}

Result<void> IOService::validate_export_path(std::string_view path) const {
    if (path.empty()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "导出路径校验失败：路径为空", "导出路径校验失败");
    }
    const auto p = std::filesystem::path(path).parent_path();
    if (!p.empty() && !std::filesystem::exists(p)) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "导出路径校验失败：父目录不存在", "导出路径校验失败");
    }
    return ok_void(state_->create_diagnostic("导出路径校验通过"));
}

Result<std::string> IOService::temp_path_for(std::string_view stem, std::string_view ext) const {
    if (stem.empty()) {
        return detail::invalid_input_result<std::string>(*state_, diag_codes::kIoExportFailure, "临时路径生成失败：stem为空", "临时路径生成失败");
    }
    auto extension = std::string(ext);
    if (!extension.empty() && extension.front() != '.') {
        extension = "." + extension;
    }
    const auto path = std::filesystem::temp_directory_path() / (std::string(stem) + extension);
    return ok_result(path.string(), state_->create_diagnostic("已生成临时路径"));
}

Result<void> IOService::copy_file(std::string_view from, std::string_view to) const {
    std::error_code ec;
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "文件复制失败", "文件复制失败");
    }
    return ok_void(state_->create_diagnostic("已复制文件"));
}

Result<void> IOService::remove_file(std::string_view path) const {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "文件删除失败", "文件删除失败");
    }
    return ok_void(state_->create_diagnostic("已删除文件"));
}

Result<void> IOService::ensure_parent_directory(std::string_view path) const {
    if (path.empty()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "目录创建失败：路径为空", "目录创建失败");
    }
    const auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) {
        return ok_void(state_->create_diagnostic("已确认父目录可用"));
    }
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "目录创建失败", "目录创建失败");
    }
    return ok_void(state_->create_diagnostic("已确认父目录可用"));
}

Result<std::pair<BodyId, std::uint64_t>>
IOService::import_step_with_warnings_count(std::string_view path,
                                           const ImportOptions& options) {
    const auto imported = import_step(path, options);
    if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
        return error_result<std::pair<BodyId, std::uint64_t>>(imported.status, imported.diagnostic_id);
    }
    return ok_result(std::make_pair(*imported.value, static_cast<std::uint64_t>(imported.warnings.size())),
                     state_->create_diagnostic("已完成STEP导入并统计告警"));
}

Result<std::pair<BodyId, std::uint64_t>>
IOService::import_axmjson_with_warnings_count(std::string_view path,
                                              const ImportOptions& options) {
    const auto imported = import_axmjson(path, options);
    if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
        return error_result<std::pair<BodyId, std::uint64_t>>(imported.status, imported.diagnostic_id);
    }
    return ok_result(std::make_pair(*imported.value, static_cast<std::uint64_t>(imported.warnings.size())),
                     state_->create_diagnostic("已完成AXMJSON导入并统计告警"));
}

Result<std::pair<BodyId, std::uint64_t>>
IOService::import_auto_with_warnings_count(std::string_view path,
                                           const ImportOptions& options) {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<std::pair<BodyId, std::uint64_t>>(fmt.status, fmt.diagnostic_id);
    }
    if (*fmt.value == "step") return import_step_with_warnings_count(path, options);
    if (*fmt.value == "axmjson") return import_axmjson_with_warnings_count(path, options);
    if (*fmt.value == "stl") {
        const auto imported = import_stl(path, options);
        if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
            return error_result<std::pair<BodyId, std::uint64_t>>(imported.status, imported.diagnostic_id);
        }
        return ok_result(std::make_pair(*imported.value, static_cast<std::uint64_t>(imported.warnings.size())),
                         state_->create_diagnostic("已完成STL导入统计"));
    }
    if (*fmt.value == "gltf") {
        const auto imported = import_gltf(path, options);
        if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
            return error_result<std::pair<BodyId, std::uint64_t>>(imported.status, imported.diagnostic_id);
        }
        return ok_result(std::make_pair(*imported.value, static_cast<std::uint64_t>(imported.warnings.size())),
                         state_->create_diagnostic("已完成glTF导入统计"));
    }
    if (*fmt.value == "iges") {
        const auto imported = import_iges(path, options);
        if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
            return error_result<std::pair<BodyId, std::uint64_t>>(imported.status, imported.diagnostic_id);
        }
        return ok_result(std::make_pair(*imported.value, static_cast<std::uint64_t>(imported.warnings.size())),
                         state_->create_diagnostic("已完成IGES导入统计"));
    }
    if (*fmt.value == "brep") {
        const auto imported = import_brep(path, options);
        if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
            return error_result<std::pair<BodyId, std::uint64_t>>(imported.status, imported.diagnostic_id);
        }
        return ok_result(std::make_pair(*imported.value, static_cast<std::uint64_t>(imported.warnings.size())),
                         state_->create_diagnostic("已完成BREP导入统计"));
    }
    if (*fmt.value == "obj") {
        const auto imported = import_obj(path, options);
        if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
            return error_result<std::pair<BodyId, std::uint64_t>>(imported.status, imported.diagnostic_id);
        }
        return ok_result(std::make_pair(*imported.value, static_cast<std::uint64_t>(imported.warnings.size())),
                         state_->create_diagnostic("已完成OBJ导入统计"));
    }
    if (*fmt.value == "3mf") {
        const auto imported = import_3mf(path, options);
        if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
            return error_result<std::pair<BodyId, std::uint64_t>>(imported.status, imported.diagnostic_id);
        }
        return ok_result(std::make_pair(*imported.value, static_cast<std::uint64_t>(imported.warnings.size())),
                         state_->create_diagnostic("已完成3MF导入统计"));
    }
    return detail::invalid_input_result<std::pair<BodyId, std::uint64_t>>(
        *state_, diag_codes::kIoImportFailure, "自动导入统计失败：不支持的文件格式", "自动导入统计失败");
}

Result<bool> IOService::export_step_checked(BodyId body_id, std::string_view path,
                                            const ExportOptions& options) {
    const auto exported = export_step(body_id, path, options);
    if (exported.status != StatusCode::Ok) {
        return error_result<bool>(exported.status, exported.diagnostic_id);
    }
    return ok_result(std::filesystem::exists(path), state_->create_diagnostic("已完成STEP导出并校验"));
}

Result<bool> IOService::export_axmjson_checked(BodyId body_id, std::string_view path,
                                               const ExportOptions& options) {
    const auto exported = export_axmjson(body_id, path, options);
    if (exported.status != StatusCode::Ok) {
        return error_result<bool>(exported.status, exported.diagnostic_id);
    }
    return ok_result(std::filesystem::exists(path), state_->create_diagnostic("已完成AXMJSON导出并校验"));
}

Result<bool> IOService::export_auto_checked(BodyId body_id, std::string_view path,
                                            const ExportOptions& options) {
    const auto exported = export_auto(body_id, path, options);
    if (exported.status != StatusCode::Ok) {
        return error_result<bool>(exported.status, exported.diagnostic_id);
    }
    return ok_result(std::filesystem::exists(path), state_->create_diagnostic("已完成自动导出并校验"));
}

Result<std::uint64_t>
IOService::import_many_step_count(std::span<const std::string> paths,
                                  const ImportOptions& options) {
    const auto imported = import_many_step(paths, options);
    if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
        return error_result<std::uint64_t>(imported.status, imported.diagnostic_id);
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(imported.value->size()),
                                    state_->create_diagnostic("已统计批量STEP导入数量"));
}

Result<std::uint64_t>
IOService::import_many_axmjson_count(std::span<const std::string> paths,
                                     const ImportOptions& options) {
    const auto imported = import_many_axmjson(paths, options);
    if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
        return error_result<std::uint64_t>(imported.status, imported.diagnostic_id);
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(imported.value->size()),
                                    state_->create_diagnostic("已统计批量AXMJSON导入数量"));
}

Result<std::uint64_t>
IOService::import_many_auto_count(std::span<const std::string> paths,
                                  const ImportOptions& options) {
    const auto imported = import_many_auto(paths, options);
    if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
        return error_result<std::uint64_t>(imported.status, imported.diagnostic_id);
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(imported.value->size()),
                                    state_->create_diagnostic("已统计批量自动导入数量"));
}

Result<std::uint64_t>
IOService::export_many_step_checked(std::span<const BodyId> body_ids,
                                    std::span<const std::string> paths,
                                    const ExportOptions& options) {
    const auto exported = export_many_step(body_ids, paths, options);
    if (exported.status != StatusCode::Ok) {
        return error_result<std::uint64_t>(exported.status, exported.diagnostic_id);
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(paths.size()),
                                    state_->create_diagnostic("已统计批量STEP导出数量"));
}

Result<std::uint64_t>
IOService::export_many_axmjson_checked(std::span<const BodyId> body_ids,
                                       std::span<const std::string> paths,
                                       const ExportOptions& options) {
    const auto exported = export_many_axmjson(body_ids, paths, options);
    if (exported.status != StatusCode::Ok) {
        return error_result<std::uint64_t>(exported.status, exported.diagnostic_id);
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(paths.size()),
                                    state_->create_diagnostic("已统计批量AXMJSON导出数量"));
}

Result<std::uint64_t>
IOService::export_many_auto_checked(std::span<const BodyId> body_ids,
                                    std::span<const std::string> paths,
                                    const ExportOptions& options) {
    const auto exported = export_many_auto(body_ids, paths, options);
    if (exported.status != StatusCode::Ok) {
        return error_result<std::uint64_t>(exported.status, exported.diagnostic_id);
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(paths.size()),
                                    state_->create_diagnostic("已统计批量自动导出数量"));
}

Result<std::vector<std::string>>
IOService::scan_formats(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
            return error_result<std::vector<std::string>>(fmt.status, fmt.diagnostic_id);
        }
        out.push_back(*fmt.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已完成格式扫描"));
}

Result<std::uint64_t>
IOService::count_existing_files(std::span<const std::string> paths) const {
    std::uint64_t count = 0;
    for (const auto& p : paths) {
        if (std::filesystem::exists(p)) ++count;
    }
    return ok_result<std::uint64_t>(count, state_->create_diagnostic("已统计存在文件数量"));
}

Result<std::vector<std::string>>
IOService::filter_existing_files(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    for (const auto& p : paths) {
        if (std::filesystem::exists(p)) out.push_back(p);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已筛选存在文件"));
}

Result<std::vector<std::string>>
IOService::filter_missing_files(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    for (const auto& p : paths) {
        if (!std::filesystem::exists(p)) out.push_back(p);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已筛选缺失文件"));
}

Result<std::string>
IOService::first_missing_file(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        if (!std::filesystem::exists(p)) {
            return ok_result(p, state_->create_diagnostic("已查询首个缺失文件"));
        }
    }
    return ok_result(std::string{}, state_->create_diagnostic("已查询首个缺失文件"));
}

Result<std::string>
IOService::first_existing_file(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        if (std::filesystem::exists(p)) {
            return ok_result(p, state_->create_diagnostic("已查询首个存在文件"));
        }
    }
    return ok_result(std::string{}, state_->create_diagnostic("已查询首个存在文件"));
}

Result<std::string> IOService::sanitize_export_stem(std::string_view stem) const {
    if (stem.empty()) {
        return detail::invalid_input_result<std::string>(
            *state_, diag_codes::kIoExportFailure, "导出名净化失败：stem为空", "导出名净化失败");
    }
    std::string out;
    out.reserve(stem.size());
    for (const auto ch : stem) {
        const bool valid = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        out.push_back(valid ? ch : '_');
    }
    return ok_result(std::move(out), state_->create_diagnostic("已净化导出名"));
}

Result<std::string> IOService::compose_path(std::string_view dir, std::string_view name,
                                            std::string_view ext) const {
    if (name.empty()) {
        return detail::invalid_input_result<std::string>(
            *state_, diag_codes::kIoExportFailure, "路径拼装失败：name为空", "路径拼装失败");
    }
    auto extension = std::string(ext);
    if (!extension.empty() && extension.front() != '.') extension = "." + extension;
    const auto path = std::filesystem::path(dir) / (std::string(name) + extension);
    return ok_result(path.string(), state_->create_diagnostic("已拼装路径"));
}

Result<std::string> IOService::change_extension(std::string_view path,
                                                std::string_view ext) const {
    if (path.empty()) {
        return detail::invalid_input_result<std::string>(
            *state_, diag_codes::kIoExportFailure, "扩展名变更失败：路径为空", "扩展名变更失败");
    }
    auto p = std::filesystem::path(path);
    auto extension = std::string(ext);
    if (!extension.empty() && extension.front() != '.') extension = "." + extension;
    p.replace_extension(extension);
    return ok_result(p.string(), state_->create_diagnostic("已变更扩展名"));
}

Result<std::string> IOService::basename(std::string_view path) const {
    return ok_result(std::filesystem::path(path).filename().string(),
                     state_->create_diagnostic("已提取文件名"));
}

Result<std::string> IOService::dirname(std::string_view path) const {
    return ok_result(std::filesystem::path(path).parent_path().string(),
                     state_->create_diagnostic("已提取目录名"));
}

Result<std::uint64_t> IOService::file_mtime_unix(std::string_view path) const {
    if (!std::filesystem::exists(path)) {
        return detail::invalid_input_result<std::uint64_t>(
            *state_, diag_codes::kIoImportFailure, "文件时间查询失败：文件不存在", "文件时间查询失败");
    }
    const auto t = std::filesystem::last_write_time(path).time_since_epoch();
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(t).count();
    return ok_result<std::uint64_t>(secs < 0 ? 0 : static_cast<std::uint64_t>(secs),
                                    state_->create_diagnostic("已查询文件修改时间"));
}

Result<void> IOService::touch_empty_file(std::string_view path) const {
    std::ofstream out{std::string(path), std::ios::app};
    if (!out) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "touch失败：无法打开文件", "touch失败");
    }
    return ok_void(state_->create_diagnostic("已touch文件"));
}

Result<void> IOService::append_text(std::string_view path, std::string_view content) const {
    std::ofstream out{std::string(path), std::ios::app};
    if (!out) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "追加文本失败：无法打开文件", "追加文本失败");
    }
    out << content;
    return ok_void(state_->create_diagnostic("已追加文本"));
}

Result<std::string> IOService::read_all_text(std::string_view path) const {
    std::ifstream in{std::string(path)};
    if (!in) {
        return detail::failed_result<std::string>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                                  "读取全文失败：无法打开文件", "读取全文失败");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ok_result(ss.str(), state_->create_diagnostic("已读取全文"));
}

Result<bool> IOService::compare_file_text(std::string_view lhs,
                                          std::string_view rhs) const {
    const auto l = read_all_text(lhs);
    if (l.status != StatusCode::Ok || !l.value.has_value()) return error_result<bool>(l.status, l.diagnostic_id);
    const auto r = read_all_text(rhs);
    if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<bool>(r.status, r.diagnostic_id);
    return ok_result(*l.value == *r.value, state_->create_diagnostic("已比较文件文本"));
}

Result<void> IOService::export_bodies_summary_txt(std::span<const BodyId> body_ids,
                                                  std::string_view path) const {
    if (body_ids.empty()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "批量摘要导出失败：体列表为空", "批量摘要导出失败");
    }
    std::ofstream out{std::string(path)};
    if (!out) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "批量摘要导出失败：无法打开输出文件", "批量摘要导出失败");
    }
    for (const auto body_id : body_ids) {
        const auto it = state_->bodies.find(body_id.value);
        if (it == state_->bodies.end()) continue;
        const auto& b = it->second;
        out << "BodyId: " << body_id.value << ", Kind: " << body_kind_name(b.kind) << ", Label: " << b.label << "\n";
    }
    return ok_void(state_->create_diagnostic("已导出批量体摘要文本"));
}

Result<BodyId>
IOService::import_auto_from_candidates(std::span<const std::string> paths,
                                       const ImportOptions& options) {
    if (paths.empty()) {
        return detail::invalid_input_result<BodyId>(*state_, diag_codes::kIoImportFailure, "候选导入失败：路径列表为空", "候选导入失败");
    }
    for (const auto& p : paths) {
        if (!std::filesystem::exists(p)) continue;
        return import_auto(p, options);
    }
    return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                         "候选导入失败：没有可用文件", "候选导入失败");
}

Result<std::uint64_t> IOService::count_importable_paths(std::span<const std::string> paths) const {
    std::uint64_t n = 0;
    for (const auto& p : paths) {
        if (std::filesystem::exists(p) && detect_format(p).value.value_or("unknown") != "unknown") ++n;
    }
    return ok_result<std::uint64_t>(n, state_->create_diagnostic("已统计可导入路径数量"));
}

Result<std::uint64_t> IOService::count_exportable_paths(std::span<const std::string> paths) const {
    std::uint64_t n = 0;
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status == StatusCode::Ok && fmt.value.has_value() && *fmt.value != "unknown") ++n;
    }
    return ok_result<std::uint64_t>(n, state_->create_diagnostic("已统计可导出路径数量"));
}

Result<std::vector<std::string>> IOService::filter_step_paths(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    for (const auto& p : paths) if (is_step_path(p).value.value_or(false)) out.push_back(p);
    return ok_result(std::move(out), state_->create_diagnostic("已筛选STEP路径"));
}

Result<std::vector<std::string>> IOService::filter_axmjson_paths(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    for (const auto& p : paths) if (is_axmjson_path(p).value.value_or(false)) out.push_back(p);
    return ok_result(std::move(out), state_->create_diagnostic("已筛选AXMJSON路径"));
}

Result<std::vector<std::string>> IOService::filter_unknown_format_paths(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status == StatusCode::Ok && fmt.value.has_value() && *fmt.value == "unknown") out.push_back(p);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已筛选未知格式路径"));
}

Result<std::vector<std::string>> IOService::normalize_paths(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto n = normalize_path(p);
        if (n.status != StatusCode::Ok || !n.value.has_value()) return error_result<std::vector<std::string>>(n.status, n.diagnostic_id);
        out.push_back(*n.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已批量规范化路径"));
}

Result<std::vector<std::string>> IOService::compose_paths(std::string_view dir, std::span<const std::string> names, std::string_view ext) const {
    std::vector<std::string> out;
    out.reserve(names.size());
    for (const auto& n : names) {
        const auto p = compose_path(dir, n, ext);
        if (p.status != StatusCode::Ok || !p.value.has_value()) return error_result<std::vector<std::string>>(p.status, p.diagnostic_id);
        out.push_back(*p.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已批量拼装路径"));
}

Result<std::vector<std::string>> IOService::change_extensions(std::span<const std::string> paths, std::string_view ext) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto changed = change_extension(p, ext);
        if (changed.status != StatusCode::Ok || !changed.value.has_value()) return error_result<std::vector<std::string>>(changed.status, changed.diagnostic_id);
        out.push_back(*changed.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已批量修改扩展名"));
}

Result<std::vector<std::string>> IOService::basenames(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) out.push_back(std::filesystem::path(p).filename().string());
    return ok_result(std::move(out), state_->create_diagnostic("已批量提取文件名"));
}

Result<std::vector<std::string>> IOService::dirnames(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) out.push_back(std::filesystem::path(p).parent_path().string());
    return ok_result(std::move(out), state_->create_diagnostic("已批量提取目录名"));
}

Result<std::vector<std::uint64_t>> IOService::file_sizes(std::span<const std::string> paths) const {
    std::vector<std::uint64_t> out;
    out.reserve(paths.size());
    for (const auto& p : paths) out.push_back(file_size_bytes(p).value.value_or(0));
    return ok_result(std::move(out), state_->create_diagnostic("已批量查询文件大小"));
}

Result<std::vector<std::uint64_t>> IOService::file_mtimes(std::span<const std::string> paths) const {
    std::vector<std::uint64_t> out;
    out.reserve(paths.size());
    for (const auto& p : paths) out.push_back(file_mtime_unix(p).value.value_or(0));
    return ok_result(std::move(out), state_->create_diagnostic("已批量查询文件时间"));
}

Result<void> IOService::ensure_parent_directories(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        const auto r = ensure_parent_directory(p);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已批量确认父目录可用"));
}

Result<void> IOService::touch_empty_files(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        const auto r = touch_empty_file(p);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已批量touch文件"));
}

Result<void> IOService::remove_files(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        const auto r = remove_file(p);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已批量删除文件"));
}

Result<void> IOService::append_text_many(std::span<const std::string> paths, std::string_view content) const {
    for (const auto& p : paths) {
        const auto r = append_text(p, content);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已批量追加文本"));
}

Result<std::vector<std::string>> IOService::read_all_text_many(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto r = read_all_text(p);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::vector<std::string>>(r.status, r.diagnostic_id);
        out.push_back(*r.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已批量读取全文"));
}

Result<std::vector<std::uint64_t>> IOService::count_lines_many(std::span<const std::string> paths) const {
    std::vector<std::uint64_t> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto r = count_lines(p);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::vector<std::uint64_t>>(r.status, r.diagnostic_id);
        out.push_back(*r.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已批量统计行数"));
}

Result<std::vector<std::string>> IOService::read_text_preview_many(std::span<const std::string> paths, std::uint64_t max_chars) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto r = read_text_preview(p, max_chars);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::vector<std::string>>(r.status, r.diagnostic_id);
        out.push_back(*r.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已批量读取文本预览"));
}

Result<void> IOService::export_body_summaries_many(std::span<const BodyId> body_ids, std::span<const std::string> paths) const {
    if (body_ids.size() != paths.size()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "批量导出失败：输入数量不一致", "批量导出失败");
    for (std::size_t i = 0; i < body_ids.size(); ++i) {
        const auto r = export_body_summary_txt(body_ids[i], paths[i]);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已批量导出体摘要"));
}

Result<std::uint64_t> IOService::compare_file_text_many_equal(std::span<const std::string> lhs, std::span<const std::string> rhs) const {
    if (lhs.size() != rhs.size()) return detail::invalid_input_result<std::uint64_t>(*state_, diag_codes::kIoImportFailure, "批量比较失败：输入数量不一致", "批量比较失败");
    std::uint64_t eq = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const auto r = compare_file_text(lhs[i], rhs[i]);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::uint64_t>(r.status, r.diagnostic_id);
        if (*r.value) ++eq;
    }
    return ok_result<std::uint64_t>(eq, state_->create_diagnostic("已批量比较文件文本"));
}

Result<std::vector<std::pair<std::string, std::string>>> IOService::detect_formats_with_paths(std::span<const std::string> paths) const {
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) return error_result<std::vector<std::pair<std::string, std::string>>>(fmt.status, fmt.diagnostic_id);
        out.emplace_back(p, *fmt.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已查询路径格式对"));
}

Result<std::string> IOService::first_importable_path(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        if (std::filesystem::exists(p) && detect_format(p).value.value_or("unknown") != "unknown") return ok_result(std::string(p), state_->create_diagnostic("已查询首个可导入路径"));
    }
    return ok_result(std::string{}, state_->create_diagnostic("已查询首个可导入路径"));
}

Result<std::string> IOService::first_exportable_path(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status == StatusCode::Ok && fmt.value.has_value() && *fmt.value != "unknown") return ok_result(std::string(p), state_->create_diagnostic("已查询首个可导出路径"));
    }
    return ok_result(std::string{}, state_->create_diagnostic("已查询首个可导出路径"));
}

Result<std::vector<BodyId>> IOService::import_existing_auto(std::span<const std::string> paths, const ImportOptions& options) {
    std::vector<std::string> existing;
    for (const auto& p : paths) if (std::filesystem::exists(p)) existing.push_back(p);
    return import_many_auto(std::span<const std::string>(existing), options);
}

Result<void> IOService::export_auto_to_directory(std::span<const BodyId> body_ids, std::string_view directory, std::string_view ext, const ExportOptions& options) {
    if (directory.empty()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "目录导出失败：目录为空", "目录导出失败");
    std::vector<std::string> paths;
    paths.reserve(body_ids.size());
    for (std::size_t i = 0; i < body_ids.size(); ++i) {
        auto p = compose_path(directory, "body_" + std::to_string(i), ext);
        if (p.status != StatusCode::Ok || !p.value.has_value()) return error_result<void>(p.status, p.diagnostic_id);
        paths.push_back(*p.value);
    }
    return export_many_auto(body_ids, std::span<const std::string>(paths), options);
}

Result<std::uint64_t> IOService::import_existing_auto_count(std::span<const std::string> paths, const ImportOptions& options) {
    const auto r = import_existing_auto(paths, options);
    if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::uint64_t>(r.status, r.diagnostic_id);
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(r.value->size()), state_->create_diagnostic("已统计现有文件自动导入数量"));
}

Result<std::uint64_t> IOService::export_auto_to_directory_count(std::span<const BodyId> body_ids, std::string_view directory, std::string_view ext, const ExportOptions& options) {
    const auto r = export_auto_to_directory(body_ids, directory, ext, options);
    if (r.status != StatusCode::Ok) return error_result<std::uint64_t>(r.status, r.diagnostic_id);
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(body_ids.size()), state_->create_diagnostic("已统计目录自动导出数量"));
}

Result<std::vector<std::string>> IOService::summarize_files_txt(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto exists = std::filesystem::exists(p);
        const auto size = exists ? static_cast<std::uint64_t>(std::filesystem::file_size(p)) : 0;
        out.push_back(std::string(p) + " | exists=" + (exists ? "true" : "false") + " | size=" + std::to_string(size));
    }
    return ok_result(std::move(out), state_->create_diagnostic("已汇总文件文本信息"));
}

Result<void> IOService::export_files_summary_txt(std::span<const std::string> paths, std::string_view out_path) const {
    auto lines = summarize_files_txt(paths);
    if (lines.status != StatusCode::Ok || !lines.value.has_value()) return error_result<void>(lines.status, lines.diagnostic_id);
    std::ofstream out{std::string(out_path)};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "文件摘要导出失败：无法打开输出文件", "文件摘要导出失败");
    for (const auto& line : *lines.value) out << line << "\n";
    return ok_void(state_->create_diagnostic("已导出文件摘要文本"));
}

Result<std::string> IOService::canonical_or_normalized_path(std::string_view path) const {
    return normalize_path(path);
}

Result<std::string> IOService::relative_to(std::string_view path, std::string_view base) const {
    if (path.empty() || base.empty()) {
        return detail::invalid_input_result<std::string>(*state_, diag_codes::kIoImportFailure, "相对路径计算失败：输入为空", "相对路径计算失败");
    }
    std::error_code ec;
    const auto rel = std::filesystem::relative(std::filesystem::path(path), std::filesystem::path(base), ec);
    if (ec) {
        return ok_result(std::filesystem::path(path).lexically_relative(std::filesystem::path(base)).string(),
                         state_->create_diagnostic("已计算相对路径"));
    }
    return ok_result(rel.string(), state_->create_diagnostic("已计算相对路径"));
}

Result<std::string> IOService::common_parent_directory(std::span<const std::string> paths) const {
    if (paths.empty()) return ok_result(std::string{}, state_->create_diagnostic("已计算公共父目录"));
    std::filesystem::path common = std::filesystem::path(paths.front()).parent_path();
    for (std::size_t i = 1; i < paths.size() && !common.empty(); ++i) {
        auto p = std::filesystem::path(paths[i]).parent_path();
        while (!common.empty() && p.string().rfind(common.string(), 0) != 0) {
            common = common.parent_path();
        }
    }
    return ok_result(common.string(), state_->create_diagnostic("已计算公共父目录"));
}

Result<std::vector<std::string>> IOService::unique_paths(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已去重路径"));
}

Result<std::vector<std::string>> IOService::sort_paths_lex(std::span<const std::string> paths) const {
    std::vector<std::string> out(paths.begin(), paths.end());
    std::sort(out.begin(), out.end());
    return ok_result(std::move(out), state_->create_diagnostic("已排序路径"));
}

Result<std::vector<std::pair<std::string, std::uint64_t>>> IOService::count_by_format(std::span<const std::string> paths) const {
    std::unordered_map<std::string, std::uint64_t> counts;
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) return error_result<std::vector<std::pair<std::string, std::uint64_t>>>(fmt.status, fmt.diagnostic_id);
        ++counts[*fmt.value];
    }
    std::vector<std::pair<std::string, std::uint64_t>> out;
    out.reserve(counts.size());
    for (const auto& kv : counts) out.push_back(kv);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b){ return a.first < b.first; });
    return ok_result(std::move(out), state_->create_diagnostic("已按格式计数"));
}

Result<std::vector<std::string>> IOService::paths_of_format(std::span<const std::string> paths, std::string_view format) const {
    std::vector<std::string> out;
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) return error_result<std::vector<std::string>>(fmt.status, fmt.diagnostic_id);
        if (*fmt.value == format) out.push_back(p);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已按格式筛选路径"));
