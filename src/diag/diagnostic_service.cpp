#include "axiom/diag/diagnostic_service.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>

#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/diag/diagnostic_internal_utils.h"

namespace axiom {

namespace {

std::string_view issue_severity_name(IssueSeverity severity) {
    switch (severity) {
        case IssueSeverity::Info:
            return "Info";
        case IssueSeverity::Warning:
            return "Warning";
        case IssueSeverity::Error:
            return "Error";
        case IssueSeverity::Fatal:
            return "Fatal";
    }
    return "Info";
}

std::string json_escape(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (const char c : input) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                // JSON requires control characters to be escaped; keep it minimal for now.
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += ' ';
                } else {
                    out += c;
                }
        }
    }
    return out;
}

}  // namespace

DiagnosticService::DiagnosticService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<DiagnosticReport> DiagnosticService::get(DiagnosticId id) const {
    const auto it = state_->diagnostics.find(id.value);
    if (it == state_->diagnostics.end()) {
        return detail::invalid_input_result<DiagnosticReport>(
            *state_, diag_codes::kCoreInvalidHandle,
            "诊断查询失败：目标诊断不存在", "诊断查询失败");
    }
    return ok_result(it->second, id);
}

Result<void> DiagnosticService::append_issue(DiagnosticId id, const Issue& issue) {
    const auto it = state_->diagnostics.find(id.value);
    if (it == state_->diagnostics.end()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kCoreInvalidHandle,
            "诊断追加失败：目标诊断不存在", "诊断追加失败");
    }
    it->second.issues.push_back(issue);
    return ok_void(id);
}

Result<void> DiagnosticService::export_report(DiagnosticId id, std::string_view path) const {
    const auto it = state_->diagnostics.find(id.value);
    if (it == state_->diagnostics.end()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kCoreInvalidHandle,
            "诊断导出失败：目标诊断不存在", "诊断导出失败");
    }
    if (path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "诊断导出失败：输出路径为空", "诊断导出失败");
    }

    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "诊断导出失败：无法打开输出文件", "诊断导出失败");
    }

    out << "DiagnosticId: " << it->second.id.value << '\n';
    out << "Summary: " << it->second.summary << '\n';
    for (const auto& issue : it->second.issues) {
        out << "- [" << issue_severity_name(issue.severity) << "] "
            << issue.code << ": " << issue.message;
        if (!issue.related_entities.empty()) {
            out << " | RelatedEntities:";
            for (const auto entity : issue.related_entities) {
                out << ' ' << entity;
            }
        }
        out << '\n';
    }

    return ok_void(id);
}

Result<void> DiagnosticService::export_report_json(DiagnosticId id, std::string_view path) const {
    const auto it = state_->diagnostics.find(id.value);
    if (it == state_->diagnostics.end()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kCoreInvalidHandle,
            "诊断JSON导出失败：目标诊断不存在", "诊断JSON导出失败");
    }
    if (path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "诊断JSON导出失败：输出路径为空", "诊断JSON导出失败");
    }
    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "诊断JSON导出失败：无法打开输出文件", "诊断JSON导出失败");
    }
    out << "{";
    out << "\"id\":" << it->second.id.value << ",";
    out << "\"summary\":\"" << json_escape(it->second.summary) << "\",";
    out << "\"issues\":[";
    for (std::size_t i = 0; i < it->second.issues.size(); ++i) {
        const auto& issue = it->second.issues[i];
        out << "{";
        out << "\"code\":\"" << json_escape(issue.code) << "\",";
        out << "\"severity\":\"" << json_escape(issue_severity_name(issue.severity)) << "\",";
        out << "\"message\":\"" << json_escape(issue.message) << "\",";
        out << "\"related_entities\":[";
        for (std::size_t j = 0; j < issue.related_entities.size(); ++j) {
            out << issue.related_entities[j];
            if (j + 1 < issue.related_entities.size()) out << ",";
        }
        out << "]";
        out << "}";
        if (i + 1 < it->second.issues.size()) out << ",";
    }
    out << "]";
    out << "}";
    return ok_void(id);
}

Result<std::uint64_t> DiagnosticService::count() const {
    return ok_result<std::uint64_t>(
        static_cast<std::uint64_t>(state_->diagnostics.size()),
        state_->create_diagnostic("已查询诊断数量"));
}

Result<std::uint64_t> DiagnosticService::count_by_severity(IssueSeverity severity) const {
    return ok_result<std::uint64_t>(
        detail::diagnostic_issue_count_by_severity(*state_, severity),
        state_->create_diagnostic("已查询诊断严重级别数量"));
}

Result<DiagnosticStats> DiagnosticService::stats() const {
    return ok_result(
        detail::diagnostic_stats(*state_),
        state_->create_diagnostic("已查询诊断统计信息"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::find_by_issue_code(
    std::string_view code, std::uint64_t max_results) const {
    if (code.empty() || max_results == 0) {
        return detail::invalid_input_result<std::vector<DiagnosticId>>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "诊断检索失败：检索参数非法", "诊断检索失败");
    }
    return ok_result(
        detail::diagnostic_find_by_issue_code(*state_, code, max_results),
        state_->create_diagnostic("已完成按问题码检索诊断"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::find_by_related_entity(
    std::uint64_t entity_id, std::uint64_t max_results) const {
    if (entity_id == 0 || max_results == 0) {
        return detail::invalid_input_result<std::vector<DiagnosticId>>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "诊断检索失败：相关实体ID或数量上限非法", "诊断检索失败");
    }
    std::vector<DiagnosticId> out;
    out.reserve(static_cast<std::size_t>(max_results));
    for (const auto& [diag_value, report] : state_->diagnostics) {
        if (out.size() >= max_results) {
            break;
        }
        bool matched = false;
        for (const auto& issue : report.issues) {
            if (std::find(issue.related_entities.begin(), issue.related_entities.end(), entity_id) !=
                issue.related_entities.end()) {
                matched = true;
                break;
            }
        }
        if (matched) {
            out.push_back(DiagnosticId {diag_value});
        }
    }
    return ok_result(std::move(out), state_->create_diagnostic("已完成按相关实体检索诊断"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::snapshot_recent(std::uint64_t max_results) const {
    if (max_results == 0) {
        return detail::invalid_input_result<std::vector<DiagnosticId>>(
            *state_, diag_codes::kCoreParameterOutOfRange,
            "诊断快照失败：快照数量必须大于零", "诊断快照失败");
    }
    return ok_result(
        detail::diagnostic_snapshot_recent(*state_, max_results),
        state_->create_diagnostic("已生成诊断快照"));
}

Result<void> DiagnosticService::clear_all() {
    state_->diagnostics.clear();
    return ok_void(state_->create_diagnostic("已清空全部诊断"));
}

Result<std::uint64_t> DiagnosticService::prune_to_max(std::uint64_t max_keep) {
    const auto pruned = detail::diagnostic_prune_to_max(*state_, max_keep);
    return ok_result<std::uint64_t>(
        pruned,
        state_->create_diagnostic("已按上限清理历史诊断"));
}

Result<bool> DiagnosticService::exists(DiagnosticId id) const {
    return ok_result(state_->diagnostics.find(id.value) != state_->diagnostics.end(),
                     state_->create_diagnostic("已查询诊断存在性"));
}

Result<std::uint64_t> DiagnosticService::issue_count(DiagnosticId id) const {
    const auto report = get(id);
    if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<std::uint64_t>(report.status, report.diagnostic_id);
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(report.value->issues.size()), state_->create_diagnostic("已查询诊断问题数量"));
}

Result<std::uint64_t> DiagnosticService::warning_count(DiagnosticId id) const {
    const auto report = get(id);
    if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<std::uint64_t>(report.status, report.diagnostic_id);
    std::uint64_t n = 0;
    for (const auto& issue : report.value->issues) if (issue.severity == IssueSeverity::Warning) ++n;
    return ok_result<std::uint64_t>(n, state_->create_diagnostic("已查询诊断告警数量"));
}

Result<std::uint64_t> DiagnosticService::error_count(DiagnosticId id) const {
    const auto report = get(id);
    if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<std::uint64_t>(report.status, report.diagnostic_id);
    std::uint64_t n = 0;
    for (const auto& issue : report.value->issues) if (issue.severity == IssueSeverity::Error) ++n;
    return ok_result<std::uint64_t>(n, state_->create_diagnostic("已查询诊断错误数量"));
}

Result<std::uint64_t> DiagnosticService::fatal_count(DiagnosticId id) const {
    const auto report = get(id);
    if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<std::uint64_t>(report.status, report.diagnostic_id);
    std::uint64_t n = 0;
    for (const auto& issue : report.value->issues) if (issue.severity == IssueSeverity::Fatal) ++n;
    return ok_result<std::uint64_t>(n, state_->create_diagnostic("已查询诊断致命错误数量"));
}

Result<bool> DiagnosticService::has_issue_code(DiagnosticId id, std::string_view code) const {
    const auto report = get(id);
    if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<bool>(report.status, report.diagnostic_id);
    for (const auto& issue : report.value->issues) if (issue.code == code) return ok_result(true, state_->create_diagnostic("已查询诊断问题码"));
    return ok_result(false, state_->create_diagnostic("已查询诊断问题码"));
}

Result<bool> DiagnosticService::has_related_entity(DiagnosticId id, std::uint64_t entity_id) const {
    const auto report = get(id);
    if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<bool>(report.status, report.diagnostic_id);
    for (const auto& issue : report.value->issues) {
        if (std::find(issue.related_entities.begin(), issue.related_entities.end(), entity_id) != issue.related_entities.end()) {
            return ok_result(true, state_->create_diagnostic("已查询诊断关联实体"));
        }
    }
    return ok_result(false, state_->create_diagnostic("已查询诊断关联实体"));
}

Result<std::vector<Issue>> DiagnosticService::issues_of(DiagnosticId id) const {
    const auto report = get(id);
    if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<std::vector<Issue>>(report.status, report.diagnostic_id);
    return ok_result(report.value->issues, state_->create_diagnostic("已查询诊断问题列表"));
}

Result<std::vector<Issue>> DiagnosticService::issues_of_severity(DiagnosticId id, IssueSeverity severity) const {
    const auto report = get(id);
    if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<std::vector<Issue>>(report.status, report.diagnostic_id);
    std::vector<Issue> out;
    for (const auto& issue : report.value->issues) if (issue.severity == severity) out.push_back(issue);
    return ok_result(std::move(out), state_->create_diagnostic("已按严重级别查询问题"));
}

Result<std::vector<Issue>> DiagnosticService::issues_of_code(DiagnosticId id, std::string_view code) const {
    const auto report = get(id);
    if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<std::vector<Issue>>(report.status, report.diagnostic_id);
    std::vector<Issue> out;
    for (const auto& issue : report.value->issues) if (issue.code == code) out.push_back(issue);
    return ok_result(std::move(out), state_->create_diagnostic("已按问题码查询问题"));
}

Result<std::vector<std::string>> DiagnosticService::unique_issue_codes(DiagnosticId id) const {
    const auto report = get(id);
    if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<std::vector<std::string>>(report.status, report.diagnostic_id);
    std::set<std::string> codes;
    for (const auto& issue : report.value->issues) codes.insert(issue.code);
    return ok_result(std::vector<std::string>(codes.begin(), codes.end()), state_->create_diagnostic("已查询唯一问题码列表"));
}

Result<DiagnosticId> DiagnosticService::latest_id() const {
    if (state_->diagnostics.empty()) return ok_result(DiagnosticId{0}, state_->create_diagnostic("已查询最新诊断ID"));
    std::uint64_t max_id = 0;
    for (const auto& [id, _] : state_->diagnostics) max_id = std::max(max_id, id);
    return ok_result(DiagnosticId{max_id}, state_->create_diagnostic("已查询最新诊断ID"));
}

Result<DiagnosticId> DiagnosticService::earliest_id() const {
    if (state_->diagnostics.empty()) return ok_result(DiagnosticId{0}, state_->create_diagnostic("已查询最早诊断ID"));
    std::uint64_t min_id = std::numeric_limits<std::uint64_t>::max();
    for (const auto& [id, _] : state_->diagnostics) min_id = std::min(min_id, id);
    return ok_result(DiagnosticId{min_id}, state_->create_diagnostic("已查询最早诊断ID"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::find_with_min_issue_count(std::uint64_t min_issues, std::uint64_t max_results) const {
    if (max_results == 0) return detail::invalid_input_result<std::vector<DiagnosticId>>(*state_, diag_codes::kCoreParameterOutOfRange, "诊断检索失败：数量上限非法", "诊断检索失败");
    std::vector<DiagnosticId> out;
    for (const auto& [id, report] : state_->diagnostics) {
        if (out.size() >= max_results) break;
        if (report.issues.size() >= min_issues) out.push_back(DiagnosticId{id});
    }
    return ok_result(std::move(out), state_->create_diagnostic("已按问题数量检索诊断"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::find_with_severity(IssueSeverity severity, std::uint64_t max_results) const {
    if (max_results == 0) return detail::invalid_input_result<std::vector<DiagnosticId>>(*state_, diag_codes::kCoreParameterOutOfRange, "诊断检索失败：数量上限非法", "诊断检索失败");
    std::vector<DiagnosticId> out;
    for (const auto& [id, report] : state_->diagnostics) {
        if (out.size() >= max_results) break;
        bool matched = false;
        for (const auto& issue : report.issues) if (issue.severity == severity) { matched = true; break; }
        if (matched) out.push_back(DiagnosticId{id});
    }
    return ok_result(std::move(out), state_->create_diagnostic("已按严重级别检索诊断"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::find_summary_contains(std::string_view token, std::uint64_t max_results) const {
    if (token.empty() || max_results == 0) return detail::invalid_input_result<std::vector<DiagnosticId>>(*state_, diag_codes::kCoreParameterOutOfRange, "诊断检索失败：参数非法", "诊断检索失败");
    std::vector<DiagnosticId> out;
    for (const auto& [id, report] : state_->diagnostics) {
        if (out.size() >= max_results) break;
        if (report.summary.find(token) != std::string::npos) out.push_back(DiagnosticId{id});
    }
    return ok_result(std::move(out), state_->create_diagnostic("已按摘要关键字检索诊断"));
}

Result<std::vector<std::string>> DiagnosticService::summaries_recent(std::uint64_t max_results) const {
    if (max_results == 0) return detail::invalid_input_result<std::vector<std::string>>(*state_, diag_codes::kCoreParameterOutOfRange, "摘要快照失败：数量上限非法", "摘要快照失败");
    std::vector<std::string> out;
    std::vector<std::uint64_t> ids;
    ids.reserve(state_->diagnostics.size());
    for (const auto& [id, _] : state_->diagnostics) ids.push_back(id);
    std::sort(ids.begin(), ids.end(), std::greater<std::uint64_t>());
    for (const auto id : ids) {
        if (out.size() >= max_results) break;
        const auto it = state_->diagnostics.find(id);
        if (it != state_->diagnostics.end()) out.push_back(it->second.summary);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已生成摘要快照"));
}

Result<std::string> DiagnosticService::summary_of(DiagnosticId id) const {
    const auto report = get(id);
    if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<std::string>(report.status, report.diagnostic_id);
    return ok_result(report.value->summary, state_->create_diagnostic("已查询诊断摘要"));
}

Result<void> DiagnosticService::set_summary(DiagnosticId id, std::string_view summary) {
    const auto it = state_->diagnostics.find(id.value);
    if (it == state_->diagnostics.end()) return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle, "设置摘要失败：目标诊断不存在", "设置摘要失败");
    it->second.summary = std::string(summary);
    return ok_void(state_->create_diagnostic("已设置诊断摘要"));
}

Result<void> DiagnosticService::append_summary_suffix(DiagnosticId id, std::string_view suffix) {
    const auto it = state_->diagnostics.find(id.value);
    if (it == state_->diagnostics.end()) return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle, "追加摘要失败：目标诊断不存在", "追加摘要失败");
    it->second.summary += std::string(suffix);
    return ok_void(state_->create_diagnostic("已追加诊断摘要"));
}

Result<void> DiagnosticService::remove_issue_code(DiagnosticId id, std::string_view code) {
    const auto it = state_->diagnostics.find(id.value);
    if (it == state_->diagnostics.end()) return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle, "移除问题失败：目标诊断不存在", "移除问题失败");
    auto& issues = it->second.issues;
    issues.erase(std::remove_if(issues.begin(), issues.end(), [code](const Issue& issue) { return issue.code == code; }), issues.end());
    return ok_void(state_->create_diagnostic("已按问题码移除问题"));
}

Result<void> DiagnosticService::remove_issues_of_severity(DiagnosticId id, IssueSeverity severity) {
    const auto it = state_->diagnostics.find(id.value);
    if (it == state_->diagnostics.end()) return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle, "移除问题失败：目标诊断不存在", "移除问题失败");
    auto& issues = it->second.issues;
    issues.erase(std::remove_if(issues.begin(), issues.end(), [severity](const Issue& issue) { return issue.severity == severity; }), issues.end());
    return ok_void(state_->create_diagnostic("已按严重级别移除问题"));
}

Result<std::uint64_t> DiagnosticService::total_issue_count() const {
    std::uint64_t total = 0;
    for (const auto& [_, report] : state_->diagnostics) total += static_cast<std::uint64_t>(report.issues.size());
    return ok_result<std::uint64_t>(total, state_->create_diagnostic("已统计总问题数量"));
}

Result<std::uint64_t> DiagnosticService::total_warning_count() const {
    std::uint64_t total = 0;
    for (const auto& [_, report] : state_->diagnostics) for (const auto& issue : report.issues) if (issue.severity == IssueSeverity::Warning) ++total;
    return ok_result<std::uint64_t>(total, state_->create_diagnostic("已统计总告警数量"));
}

Result<std::uint64_t> DiagnosticService::total_error_count() const {
    std::uint64_t total = 0;
    for (const auto& [_, report] : state_->diagnostics) for (const auto& issue : report.issues) if (issue.severity == IssueSeverity::Error) ++total;
    return ok_result<std::uint64_t>(total, state_->create_diagnostic("已统计总错误数量"));
}

Result<std::uint64_t> DiagnosticService::total_fatal_count() const {
    std::uint64_t total = 0;
    for (const auto& [_, report] : state_->diagnostics) for (const auto& issue : report.issues) if (issue.severity == IssueSeverity::Fatal) ++total;
    return ok_result<std::uint64_t>(total, state_->create_diagnostic("已统计总致命错误数量"));
}

Result<void> DiagnosticService::export_reports_txt(std::span<const DiagnosticId> ids, std::string_view path) const {
    if (ids.empty() || path.empty()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "批量导出失败：输入非法", "批量导出失败");
    std::ofstream out{std::string(path)};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "批量导出失败：无法打开文件", "批量导出失败");
    for (const auto id : ids) {
        const auto it = state_->diagnostics.find(id.value);
        if (it == state_->diagnostics.end()) continue;
        out << "DiagnosticId: " << id.value << "\nSummary: " << it->second.summary << "\n";
    }
    return ok_void(state_->create_diagnostic("已批量导出诊断文本"));
}

Result<void> DiagnosticService::export_reports_json(std::span<const DiagnosticId> ids, std::string_view path) const {
    if (ids.empty() || path.empty()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "批量导出失败：输入非法", "批量导出失败");
    std::ofstream out{std::string(path)};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "批量导出失败：无法打开文件", "批量导出失败");
    out << "{\"diagnostics\":[";
    bool first = true;
    for (const auto id : ids) {
        const auto it = state_->diagnostics.find(id.value);
        if (it == state_->diagnostics.end()) continue;
        if (!first) out << ",";
        first = false;
        out << "{\"id\":" << id.value << ",\"summary\":\"" << it->second.summary << "\"}";
    }
    out << "]}";
    return ok_void(state_->create_diagnostic("已批量导出诊断JSON"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::all_ids() const {
    std::vector<DiagnosticId> out;
    out.reserve(state_->diagnostics.size());
    for (const auto& [id, _] : state_->diagnostics) out.push_back(DiagnosticId{id});
    return ok_result(std::move(out), state_->create_diagnostic("已查询全部诊断ID"));
}

Result<void> DiagnosticService::append_issue_many(std::span<const DiagnosticId> ids, const Issue& issue) {
    if (ids.empty()) return detail::invalid_input_void(*state_, diag_codes::kCoreParameterOutOfRange, "批量追加失败：诊断ID列表为空", "批量追加失败");
    for (const auto id : ids) {
        auto r = append_issue(id, issue);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已批量追加诊断问题"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::report_ids_by_severity(IssueSeverity severity, std::uint64_t max_results) const {
    return find_with_severity(severity, max_results);
}

Result<std::vector<DiagnosticId>> DiagnosticService::report_ids_by_code(std::string_view code, std::uint64_t max_results) const {
    return find_by_issue_code(code, max_results);
}

Result<std::vector<DiagnosticId>> DiagnosticService::report_ids_by_entity(std::uint64_t entity_id, std::uint64_t max_results) const {
    return find_by_related_entity(entity_id, max_results);
}

Result<std::unordered_map<std::string, std::uint64_t>> DiagnosticService::issue_code_histogram() const {
    std::unordered_map<std::string, std::uint64_t> out;
    for (const auto& [id, report] : state_->diagnostics) {
        (void)id;
        for (const auto& issue : report.issues) ++out[issue.code];
    }
    return ok_result(std::move(out), state_->create_diagnostic("已统计问题码直方图"));
}

Result<std::unordered_map<std::string, std::uint64_t>> DiagnosticService::severity_histogram() const {
    std::unordered_map<std::string, std::uint64_t> out;
    for (const auto& [id, report] : state_->diagnostics) {
        (void)id;
        for (const auto& issue : report.issues) ++out[std::string(issue_severity_name(issue.severity))];
    }
    return ok_result(std::move(out), state_->create_diagnostic("已统计严重级别直方图"));
}

Result<std::unordered_map<std::uint64_t, std::uint64_t>> DiagnosticService::entity_histogram() const {
    std::unordered_map<std::uint64_t, std::uint64_t> out;
    for (const auto& [id, report] : state_->diagnostics) {
        (void)id;
        for (const auto& issue : report.issues) for (const auto entity : issue.related_entities) ++out[entity];
    }
    return ok_result(std::move(out), state_->create_diagnostic("已统计关联实体直方图"));
}

Result<std::vector<std::pair<std::string, std::uint64_t>>> DiagnosticService::top_issue_codes(std::uint64_t max_results) const {
    if (max_results == 0) return detail::invalid_input_result<std::vector<std::pair<std::string, std::uint64_t>>>(*state_, diag_codes::kCoreParameterOutOfRange, "问题码排行失败：上限非法", "问题码排行失败");
    const auto hist = issue_code_histogram();
    if (hist.status != StatusCode::Ok || !hist.value.has_value()) return error_result<std::vector<std::pair<std::string, std::uint64_t>>>(hist.status, hist.diagnostic_id);
    std::vector<std::pair<std::string, std::uint64_t>> out(hist.value->begin(), hist.value->end());
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b){ return a.second > b.second; });
    if (out.size() > max_results) out.resize(static_cast<std::size_t>(max_results));
    return ok_result(std::move(out), state_->create_diagnostic("已生成问题码排行"));
}

Result<std::vector<std::pair<std::uint64_t, std::uint64_t>>> DiagnosticService::top_entities(std::uint64_t max_results) const {
    if (max_results == 0) return detail::invalid_input_result<std::vector<std::pair<std::uint64_t, std::uint64_t>>>(*state_, diag_codes::kCoreParameterOutOfRange, "实体排行失败：上限非法", "实体排行失败");
    const auto hist = entity_histogram();
    if (hist.status != StatusCode::Ok || !hist.value.has_value()) return error_result<std::vector<std::pair<std::uint64_t, std::uint64_t>>>(hist.status, hist.diagnostic_id);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> out(hist.value->begin(), hist.value->end());
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b){ return a.second > b.second; });
    if (out.size() > max_results) out.resize(static_cast<std::size_t>(max_results));
    return ok_result(std::move(out), state_->create_diagnostic("已生成实体排行"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::diagnostics_for_bodies(std::span<const BodyId> body_ids, std::uint64_t max_results) const {
    std::vector<DiagnosticId> out;
    for (const auto body_id : body_ids) {
        const auto ids = find_by_related_entity(body_id.value, max_results);
        if (ids.status != StatusCode::Ok || !ids.value.has_value()) return error_result<std::vector<DiagnosticId>>(ids.status, ids.diagnostic_id);
        for (const auto id : *ids.value) if (std::none_of(out.begin(), out.end(), [id](DiagnosticId d){ return d.value == id.value; })) out.push_back(id);
        if (out.size() >= max_results) break;
    }
    if (out.size() > max_results) out.resize(static_cast<std::size_t>(max_results));
    return ok_result(std::move(out), state_->create_diagnostic("已按体查询诊断"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::diagnostics_for_faces(std::span<const FaceId> face_ids, std::uint64_t max_results) const {
    std::vector<BodyId> ids;
    ids.reserve(face_ids.size());
    for (const auto id : face_ids) ids.push_back(BodyId{id.value});
    return diagnostics_for_bodies(std::span<const BodyId>(ids), max_results);
}

Result<std::vector<DiagnosticId>> DiagnosticService::diagnostics_for_shells(std::span<const ShellId> shell_ids, std::uint64_t max_results) const {
    std::vector<BodyId> ids;
    ids.reserve(shell_ids.size());
    for (const auto id : shell_ids) ids.push_back(BodyId{id.value});
    return diagnostics_for_bodies(std::span<const BodyId>(ids), max_results);
}

Result<std::vector<DiagnosticId>> DiagnosticService::diagnostics_for_edges(std::span<const EdgeId> edge_ids, std::uint64_t max_results) const {
    std::vector<BodyId> ids;
    ids.reserve(edge_ids.size());
    for (const auto id : edge_ids) ids.push_back(BodyId{id.value});
    return diagnostics_for_bodies(std::span<const BodyId>(ids), max_results);
}

Result<std::vector<DiagnosticId>> DiagnosticService::diagnostics_for_vertices(std::span<const VertexId> vertex_ids, std::uint64_t max_results) const {
    std::vector<BodyId> ids;
    ids.reserve(vertex_ids.size());
    for (const auto id : vertex_ids) ids.push_back(BodyId{id.value});
    return diagnostics_for_bodies(std::span<const BodyId>(ids), max_results);
}

Result<void> DiagnosticService::export_grouped_by_code_txt(std::string_view path) const {
    if (path.empty()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "分组导出失败：路径为空", "分组导出失败");
    std::ofstream out{std::string(path)};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "分组导出失败：无法打开文件", "分组导出失败");
    const auto hist = issue_code_histogram();
    if (hist.status != StatusCode::Ok || !hist.value.has_value()) return error_result<void>(hist.status, hist.diagnostic_id);
    for (const auto& [code, count] : *hist.value) out << code << ": " << count << "\n";
    return ok_void(state_->create_diagnostic("已按问题码导出文本分组"));
}

Result<void> DiagnosticService::export_grouped_by_entity_txt(std::string_view path) const {
    if (path.empty()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "分组导出失败：路径为空", "分组导出失败");
    std::ofstream out{std::string(path)};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "分组导出失败：无法打开文件", "分组导出失败");
    const auto hist = entity_histogram();
    if (hist.status != StatusCode::Ok || !hist.value.has_value()) return error_result<void>(hist.status, hist.diagnostic_id);
    for (const auto& [entity, count] : *hist.value) out << entity << ": " << count << "\n";
    return ok_void(state_->create_diagnostic("已按实体导出文本分组"));
}

Result<void> DiagnosticService::export_grouped_by_severity_txt(std::string_view path) const {
    if (path.empty()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "分组导出失败：路径为空", "分组导出失败");
    std::ofstream out{std::string(path)};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "分组导出失败：无法打开文件", "分组导出失败");
    const auto hist = severity_histogram();
    if (hist.status != StatusCode::Ok || !hist.value.has_value()) return error_result<void>(hist.status, hist.diagnostic_id);
    for (const auto& [sev, count] : *hist.value) out << sev << ": " << count << "\n";
    return ok_void(state_->create_diagnostic("已按严重级别导出文本分组"));
}

Result<void> DiagnosticService::export_grouped_by_code_json(std::string_view path) const {
    if (path.empty()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "分组导出失败：路径为空", "分组导出失败");
    std::ofstream out{std::string(path)};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "分组导出失败：无法打开文件", "分组导出失败");
    const auto hist = issue_code_histogram();
    if (hist.status != StatusCode::Ok || !hist.value.has_value()) return error_result<void>(hist.status, hist.diagnostic_id);
    out << "{\"codes\":{";
    bool first = true;
    for (const auto& [code, count] : *hist.value) { if (!first) out << ","; first = false; out << "\"" << code << "\":" << count; }
    out << "}}";
    return ok_void(state_->create_diagnostic("已按问题码导出JSON分组"));
}

Result<void> DiagnosticService::export_grouped_by_entity_json(std::string_view path) const {
    if (path.empty()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "分组导出失败：路径为空", "分组导出失败");
    std::ofstream out{std::string(path)};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "分组导出失败：无法打开文件", "分组导出失败");
    const auto hist = entity_histogram();
    if (hist.status != StatusCode::Ok || !hist.value.has_value()) return error_result<void>(hist.status, hist.diagnostic_id);
    out << "{\"entities\":{";
    bool first = true;
    for (const auto& [entity, count] : *hist.value) { if (!first) out << ","; first = false; out << "\"" << entity << "\":" << count; }
    out << "}}";
    return ok_void(state_->create_diagnostic("已按实体导出JSON分组"));
}

Result<void> DiagnosticService::export_grouped_by_severity_json(std::string_view path) const {
    if (path.empty()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "分组导出失败：路径为空", "分组导出失败");
    std::ofstream out{std::string(path)};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "分组导出失败：无法打开文件", "分组导出失败");
    const auto hist = severity_histogram();
    if (hist.status != StatusCode::Ok || !hist.value.has_value()) return error_result<void>(hist.status, hist.diagnostic_id);
    out << "{\"severities\":{";
    bool first = true;
    for (const auto& [sev, count] : *hist.value) { if (!first) out << ","; first = false; out << "\"" << sev << "\":" << count; }
    out << "}}";
    return ok_void(state_->create_diagnostic("已按严重级别导出JSON分组"));
}

Result<std::vector<std::string>> DiagnosticService::summaries_by_ids(std::span<const DiagnosticId> ids) const {
    std::vector<std::string> out;
    out.reserve(ids.size());
    for (const auto id : ids) {
        const auto report = get(id);
        if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<std::vector<std::string>>(report.status, report.diagnostic_id);
        out.push_back(report.value->summary);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已按ID批量查询摘要"));
}

Result<DiagnosticReport> DiagnosticService::merge_reports(std::span<const DiagnosticId> ids, std::string_view summary) const {
    if (ids.empty()) return detail::invalid_input_result<DiagnosticReport>(*state_, diag_codes::kCoreParameterOutOfRange, "合并诊断失败：输入为空", "合并诊断失败");
    DiagnosticReport merged;
    merged.id = DiagnosticId{0};
    merged.summary = std::string(summary);
    for (const auto id : ids) {
        const auto report = get(id);
        if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<DiagnosticReport>(report.status, report.diagnostic_id);
        merged.issues.insert(merged.issues.end(), report.value->issues.begin(), report.value->issues.end());
    }
    return ok_result(std::move(merged), state_->create_diagnostic("已合并诊断报告"));
}

Result<DiagnosticId> DiagnosticService::copy_report(DiagnosticId id) const {
    const auto report = get(id);
    if (report.status != StatusCode::Ok || !report.value.has_value()) return error_result<DiagnosticId>(report.status, report.diagnostic_id);
    return ok_result(state_->create_diagnostic(report.value->summary, report.value->issues),
                     state_->create_diagnostic("已复制诊断报告"));
}

Result<DiagnosticId> DiagnosticService::create_report(std::string_view summary, std::span<const Issue> issues) {
    return ok_result(state_->create_diagnostic(std::string(summary), std::vector<Issue>(issues.begin(), issues.end())),
                     state_->create_diagnostic("已创建诊断报告"));
}

Result<std::uint64_t> DiagnosticService::remove_reports(std::span<const DiagnosticId> ids) {
    std::uint64_t removed = 0;
    for (const auto id : ids) removed += static_cast<std::uint64_t>(state_->diagnostics.erase(id.value));
    return ok_result<std::uint64_t>(removed, state_->create_diagnostic("已批量移除诊断报告"));
}

Result<std::uint64_t> DiagnosticService::keep_only(std::span<const DiagnosticId> ids) {
    std::set<std::uint64_t> keep;
    for (const auto id : ids) keep.insert(id.value);
    std::uint64_t removed = 0;
    for (auto it = state_->diagnostics.begin(); it != state_->diagnostics.end();) {
        if (keep.find(it->first) == keep.end()) { it = state_->diagnostics.erase(it); ++removed; }
        else ++it;
    }
    return ok_result<std::uint64_t>(removed, state_->create_diagnostic("已按白名单保留诊断"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::ids_sorted_asc() const {
    std::vector<DiagnosticId> out;
    for (const auto& [id, _] : state_->diagnostics) out.push_back(DiagnosticId{id});
    std::sort(out.begin(), out.end(), [](DiagnosticId a, DiagnosticId b){ return a.value < b.value; });
    return ok_result(std::move(out), state_->create_diagnostic("已按升序查询诊断ID"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::ids_sorted_desc() const {
    std::vector<DiagnosticId> out;
    for (const auto& [id, _] : state_->diagnostics) out.push_back(DiagnosticId{id});
    std::sort(out.begin(), out.end(), [](DiagnosticId a, DiagnosticId b){ return a.value > b.value; });
    return ok_result(std::move(out), state_->create_diagnostic("已按降序查询诊断ID"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::recent_ids_with_issue_code(std::string_view code, std::uint64_t max_results) const {
    if (code.empty() || max_results == 0) return detail::invalid_input_result<std::vector<DiagnosticId>>(*state_, diag_codes::kCoreParameterOutOfRange, "最近诊断检索失败：参数非法", "最近诊断检索失败");
    const auto sorted = ids_sorted_desc();
    if (sorted.status != StatusCode::Ok || !sorted.value.has_value()) return error_result<std::vector<DiagnosticId>>(sorted.status, sorted.diagnostic_id);
    std::vector<DiagnosticId> out;
    for (const auto id : *sorted.value) {
        if (out.size() >= max_results) break;
        const auto has = has_issue_code(id, code);
        if (has.status == StatusCode::Ok && has.value.has_value() && *has.value) out.push_back(id);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已按问题码查询最近诊断"));
}

Result<std::vector<DiagnosticId>> DiagnosticService::recent_ids_with_entity(std::uint64_t entity_id, std::uint64_t max_results) const {
    if (entity_id == 0 || max_results == 0) return detail::invalid_input_result<std::vector<DiagnosticId>>(*state_, diag_codes::kCoreParameterOutOfRange, "最近诊断检索失败：参数非法", "最近诊断检索失败");
    const auto sorted = ids_sorted_desc();
    if (sorted.status != StatusCode::Ok || !sorted.value.has_value()) return error_result<std::vector<DiagnosticId>>(sorted.status, sorted.diagnostic_id);
    std::vector<DiagnosticId> out;
    for (const auto id : *sorted.value) {
        if (out.size() >= max_results) break;
        const auto has = has_related_entity(id, entity_id);
        if (has.status == StatusCode::Ok && has.value.has_value() && *has.value) out.push_back(id);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已按实体查询最近诊断"));
}

Result<DiagnosticStats> DiagnosticService::stats_of_ids(std::span<const DiagnosticId> ids) const {
    DiagnosticStats stats{};
    stats.total = static_cast<std::uint64_t>(ids.size());
    for (const auto id : ids) {
        const auto report = get(id);
        if (report.status != StatusCode::Ok || !report.value.has_value()) continue;
        for (const auto& issue : report.value->issues) {
            if (issue.severity == IssueSeverity::Warning) ++stats.warning;
            else if (issue.severity == IssueSeverity::Error) ++stats.error;
            else if (issue.severity == IssueSeverity::Fatal) ++stats.fatal;
        }
    }
    return ok_result(stats, state_->create_diagnostic("已统计指定诊断集合"));
}

}  // namespace axiom
