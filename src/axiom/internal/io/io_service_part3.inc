}

Result<void> IOService::rename_file(std::string_view from, std::string_view to) const {
    return move_file(from, to);
}

Result<void> IOService::move_file(std::string_view from, std::string_view to) const {
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(from), std::filesystem::path(to), ec);
    if (ec) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "移动文件失败", "移动文件失败");
    return ok_void(state_->create_diagnostic("已移动文件"));
}

Result<void> IOService::ensure_directory(std::string_view directory) const {
    if (directory.empty()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "目录创建失败：目录为空", "目录创建失败");
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(directory), ec);
    if (ec) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "目录创建失败", "目录创建失败");
    return ok_void(state_->create_diagnostic("已确认目录可用"));
}

Result<bool> IOService::directory_exists(std::string_view directory) const {
    if (directory.empty()) return ok_result(false, state_->create_diagnostic("已查询目录存在性"));
    return ok_result(std::filesystem::is_directory(std::filesystem::path(directory)), state_->create_diagnostic("已查询目录存在性"));
}

Result<std::vector<std::string>> IOService::list_files_in_directory(std::string_view directory) const {
    if (!std::filesystem::is_directory(std::filesystem::path(directory))) {
        return detail::invalid_input_result<std::vector<std::string>>(*state_, diag_codes::kIoImportFailure, "目录扫描失败：目录无效", "目录扫描失败");
    }
    std::vector<std::string> out;
    for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(directory))) {
        if (e.is_regular_file()) out.push_back(e.path().string());
    }
    std::sort(out.begin(), out.end());
    return ok_result(std::move(out), state_->create_diagnostic("已扫描目录文件"));
}

Result<std::vector<std::string>> IOService::list_files_recursive(std::string_view directory) const {
    if (!std::filesystem::is_directory(std::filesystem::path(directory))) {
        return detail::invalid_input_result<std::vector<std::string>>(*state_, diag_codes::kIoImportFailure, "递归扫描失败：目录无效", "递归扫描失败");
    }
    std::vector<std::string> out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(std::filesystem::path(directory))) {
        if (e.is_regular_file()) out.push_back(e.path().string());
    }
    std::sort(out.begin(), out.end());
    return ok_result(std::move(out), state_->create_diagnostic("已递归扫描目录文件"));
}

Result<std::uint64_t> IOService::count_files_in_directory(std::string_view directory, bool recursive) const {
    const auto files = recursive ? list_files_recursive(directory) : list_files_in_directory(directory);
    if (files.status != StatusCode::Ok || !files.value.has_value()) return error_result<std::uint64_t>(files.status, files.diagnostic_id);
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(files.value->size()), state_->create_diagnostic("已统计目录文件数量"));
}

Result<void> IOService::write_lines(std::string_view path, std::span<const std::string> lines) const {
    std::ofstream out{std::string(path)};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "写入文本行失败：无法打开文件", "写入文本行失败");
    for (const auto& line : lines) out << line << "\n";
    return ok_void(state_->create_diagnostic("已写入文本行"));
}

Result<std::vector<std::string>> IOService::read_lines(std::string_view path) const {
    std::ifstream in{std::string(path)};
    if (!in) return detail::failed_result<std::vector<std::string>>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure, "读取文本行失败：无法打开文件", "读取文本行失败");
    std::vector<std::string> out;
    std::string line;
    while (std::getline(in, line)) out.push_back(line);
    return ok_result(std::move(out), state_->create_diagnostic("已读取文本行"));
}

Result<std::vector<std::string>> IOService::grep_lines_contains(std::string_view path, std::string_view token) const {
    const auto lines = read_lines(path);
    if (lines.status != StatusCode::Ok || !lines.value.has_value()) return error_result<std::vector<std::string>>(lines.status, lines.diagnostic_id);
    std::vector<std::string> out;
    for (const auto& line : *lines.value) if (line.find(token) != std::string::npos) out.push_back(line);
    return ok_result(std::move(out), state_->create_diagnostic("已按token筛选文本行"));
}

Result<std::uint64_t> IOService::replace_in_file_text(std::string_view path, std::string_view from, std::string_view to) const {
    if (from.empty()) return detail::invalid_input_result<std::uint64_t>(*state_, diag_codes::kIoExportFailure, "文本替换失败：from为空", "文本替换失败");
    const auto all = read_all_text(path);
    if (all.status != StatusCode::Ok || !all.value.has_value()) return error_result<std::uint64_t>(all.status, all.diagnostic_id);
    std::string text = *all.value;
    std::uint64_t replaced = 0;
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
        ++replaced;
    }
    const auto write = write_text_snapshot(path, text);
    if (write.status != StatusCode::Ok) return error_result<std::uint64_t>(write.status, write.diagnostic_id);
    return ok_result<std::uint64_t>(replaced, state_->create_diagnostic("已替换文件文本"));
}

Result<void> IOService::prepend_text(std::string_view path, std::string_view content) const {
    const auto all = read_all_text(path);
    if (all.status != StatusCode::Ok || !all.value.has_value()) return error_result<void>(all.status, all.diagnostic_id);
    return write_text_snapshot(path, std::string(content) + *all.value);
}

Result<void> IOService::truncate_file(std::string_view path) const {
    std::ofstream out{std::string(path), std::ios::trunc};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "文件清空失败", "文件清空失败");
    return ok_void(state_->create_diagnostic("已清空文件"));
}

Result<std::string> IOService::file_stem(std::string_view path) const {
    return ok_result(std::filesystem::path(path).stem().string(), state_->create_diagnostic("已提取文件主名"));
}

Result<std::string> IOService::extension_of(std::string_view path) const {
    return ok_result(std::filesystem::path(path).extension().string(), state_->create_diagnostic("已提取扩展名"));
}

Result<std::string> IOService::with_stem(std::string_view path, std::string_view new_stem) const {
    if (new_stem.empty()) return detail::invalid_input_result<std::string>(*state_, diag_codes::kIoExportFailure, "路径主名替换失败：主名为空", "路径主名替换失败");
    auto p = std::filesystem::path(path);
    const auto ext = p.extension().string();
    p.replace_filename(std::string(new_stem) + ext);
    return ok_result(p.string(), state_->create_diagnostic("已替换路径主名"));
}

Result<std::string> IOService::append_suffix_before_ext(std::string_view path, std::string_view suffix) const {
    auto p = std::filesystem::path(path);
    const auto name = p.stem().string() + std::string(suffix) + p.extension().string();
    p.replace_filename(name);
    return ok_result(p.string(), state_->create_diagnostic("已在扩展名前追加后缀"));
}

Result<std::vector<std::string>> IOService::generate_sequential_paths(std::string_view directory, std::string_view prefix, std::string_view ext, std::uint64_t count) const {
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        const auto p = compose_path(directory, std::string(prefix) + std::to_string(i), ext);
        if (p.status != StatusCode::Ok || !p.value.has_value()) return error_result<std::vector<std::string>>(p.status, p.diagnostic_id);
        out.push_back(*p.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已生成连续路径"));
}

Result<std::string> IOService::first_writable_path(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        const auto parent = std::filesystem::path(p).parent_path();
        if (parent.empty() || std::filesystem::exists(parent)) return ok_result(std::string(p), state_->create_diagnostic("已查询首个可写路径"));
    }
    return ok_result(std::string{}, state_->create_diagnostic("已查询首个可写路径"));
}

Result<std::uint64_t> IOService::export_auto_existing_only(std::span<const BodyId> body_ids, std::span<const std::string> paths, const ExportOptions& options) {
    if (body_ids.size() != paths.size()) return detail::invalid_input_result<std::uint64_t>(*state_, diag_codes::kIoExportFailure, "条件导出失败：输入数量不一致", "条件导出失败");
    std::uint64_t exported = 0;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (!std::filesystem::exists(std::filesystem::path(paths[i]).parent_path())) continue;
        const auto r = export_auto(body_ids[i], paths[i], options);
        if (r.status == StatusCode::Ok) ++exported;
    }
    return ok_result<std::uint64_t>(exported, state_->create_diagnostic("已完成条件自动导出"));
}

Result<std::vector<BodyId>> IOService::import_auto_existing_strict(std::span<const std::string> paths, const ImportOptions& options) {
    for (const auto& p : paths) {
        if (!std::filesystem::exists(p)) {
            return detail::failed_result<std::vector<BodyId>>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure, "严格导入失败：包含不存在路径", "严格导入失败");
        }
    }
    return import_many_auto(paths, options);
}

Result<std::vector<std::string>> IOService::summarize_format_histogram_txt(std::span<const std::string> paths) const {
    const auto counts = count_by_format(paths);
    if (counts.status != StatusCode::Ok || !counts.value.has_value()) return error_result<std::vector<std::string>>(counts.status, counts.diagnostic_id);
    std::vector<std::string> out;
    out.reserve(counts.value->size());
    for (const auto& kv : *counts.value) out.push_back(kv.first + ": " + std::to_string(kv.second));
    return ok_result(std::move(out), state_->create_diagnostic("已汇总格式直方文本"));
}

