#pragma once

#include <memory>
#include <string>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
}

class DiagnosticService {
public:
    explicit DiagnosticService(std::shared_ptr<detail::KernelState> state);

    Result<DiagnosticReport> get(DiagnosticId id) const;
    Result<void> append_issue(DiagnosticId id, const Issue& issue);
    Result<void> export_report(DiagnosticId id, std::string_view path) const;
    Result<void> export_report_json(DiagnosticId id, std::string_view path) const;
    Result<std::uint64_t> count() const;
    Result<std::uint64_t> count_by_severity(IssueSeverity severity) const;
    Result<DiagnosticStats> stats() const;
    Result<std::vector<DiagnosticId>> find_by_issue_code(std::string_view code, std::uint64_t max_results) const;
    /// 任一 issue 的 `code` 以 `code_prefix` 开头时命中；`code_prefix` 为空或 `max_results==0` 时参数非法。
    Result<std::vector<DiagnosticId>> find_by_issue_code_prefix(std::string_view code_prefix,
                                                                std::uint64_t max_results) const;
    Result<std::vector<DiagnosticId>> find_by_related_entity(std::uint64_t entity_id, std::uint64_t max_results) const;
    Result<std::vector<DiagnosticId>> snapshot_recent(std::uint64_t max_results) const;
    Result<void> clear_all();
    Result<std::uint64_t> prune_to_max(std::uint64_t max_keep);
    Result<bool> exists(DiagnosticId id) const;
    Result<std::uint64_t> issue_count(DiagnosticId id) const;
    Result<std::uint64_t> warning_count(DiagnosticId id) const;
    Result<std::uint64_t> error_count(DiagnosticId id) const;
    Result<std::uint64_t> fatal_count(DiagnosticId id) const;
    Result<bool> has_issue_code(DiagnosticId id, std::string_view code) const;
    Result<bool> has_related_entity(DiagnosticId id, std::uint64_t entity_id) const;
    Result<std::vector<Issue>> issues_of(DiagnosticId id) const;
    Result<std::vector<Issue>> issues_of_severity(DiagnosticId id, IssueSeverity severity) const;
    Result<std::vector<Issue>> issues_of_code(DiagnosticId id, std::string_view code) const;
    Result<std::vector<std::string>> unique_issue_codes(DiagnosticId id) const;
    Result<DiagnosticId> latest_id() const;
    Result<DiagnosticId> earliest_id() const;
    Result<std::vector<DiagnosticId>> find_with_min_issue_count(std::uint64_t min_issues, std::uint64_t max_results) const;
    Result<std::vector<DiagnosticId>> find_with_severity(IssueSeverity severity, std::uint64_t max_results) const;
    Result<std::vector<DiagnosticId>> find_summary_contains(std::string_view token, std::uint64_t max_results) const;
    Result<std::vector<std::string>> summaries_recent(std::uint64_t max_results) const;
    Result<std::string> summary_of(DiagnosticId id) const;
    Result<void> set_summary(DiagnosticId id, std::string_view summary);
    Result<void> append_summary_suffix(DiagnosticId id, std::string_view suffix);
    Result<void> remove_issue_code(DiagnosticId id, std::string_view code);
    Result<void> remove_issues_of_severity(DiagnosticId id, IssueSeverity severity);
    Result<std::uint64_t> total_issue_count() const;
    Result<std::uint64_t> total_warning_count() const;
    Result<std::uint64_t> total_error_count() const;
    Result<std::uint64_t> total_fatal_count() const;
    Result<void> export_reports_txt(std::span<const DiagnosticId> ids, std::string_view path) const;
    Result<void> export_reports_json(std::span<const DiagnosticId> ids, std::string_view path) const;
    /// 导出当前存储内**全部**诊断报告为单个 JSON（与 `export_report_json` 单条结构一致，顶层为 `{"diagnostics":[...]}`）；顺序为 `DiagnosticId` 升序，供 CI/回归归档。
    Result<void> export_all_reports_json(std::string_view path) const;
    /// 全部诊断的文本拼接导出（顺序为 `DiagnosticId` 升序）；条目间以空行分隔。
    Result<void> export_all_reports_txt(std::string_view path) const;
    /// 任一 issue 的 `stage` **精确等于** `stage` 时命中该诊断报告。
    Result<std::vector<DiagnosticId>> find_by_issue_stage(std::string_view stage, std::uint64_t max_results) const;
    /// 任一 issue 的 `stage` 以 `stage_prefix` 开头时命中（如 `bool.` 聚合 BOOL 子阶段）；`stage_prefix` 为空或 `max_results==0` 时参数非法。
    Result<std::vector<DiagnosticId>> find_by_issue_stage_prefix(std::string_view stage_prefix,
                                                                 std::uint64_t max_results) const;
    Result<std::vector<DiagnosticId>> all_ids() const;
    Result<void> append_issue_many(std::span<const DiagnosticId> ids, const Issue& issue);
    Result<std::vector<DiagnosticId>> report_ids_by_severity(IssueSeverity severity, std::uint64_t max_results) const;
    Result<std::vector<DiagnosticId>> report_ids_by_code(std::string_view code, std::uint64_t max_results) const;
    Result<std::vector<DiagnosticId>> report_ids_by_entity(std::uint64_t entity_id, std::uint64_t max_results) const;
    Result<std::unordered_map<std::string, std::uint64_t>> issue_code_histogram() const;
    Result<std::unordered_map<std::string, std::uint64_t>> severity_histogram() const;
    Result<std::unordered_map<std::uint64_t, std::uint64_t>> entity_histogram() const;
    Result<std::vector<std::pair<std::string, std::uint64_t>>> top_issue_codes(std::uint64_t max_results) const;
    /// 按 `issue.stage` 聚合后的 Top-N（空 stage 计入 `(unset)`，与 `issue_stage_histogram` 一致）。
    Result<std::vector<std::pair<std::string, std::uint64_t>>> top_issue_stages(std::uint64_t max_results) const;
    /// 跨**全部**报告累计：满足 `issue.stage` 以 `stage_prefix` 开头（含完全相等）的 issue 条数；`stage_prefix` 为空时参数非法。
    Result<std::uint64_t> total_issues_with_stage_prefix(std::string_view stage_prefix) const;
    Result<std::vector<std::pair<std::uint64_t, std::uint64_t>>> top_entities(std::uint64_t max_results) const;
    Result<std::vector<DiagnosticId>> diagnostics_for_bodies(std::span<const BodyId> body_ids, std::uint64_t max_results) const;
    Result<std::vector<DiagnosticId>> diagnostics_for_faces(std::span<const FaceId> face_ids, std::uint64_t max_results) const;
    Result<std::vector<DiagnosticId>> diagnostics_for_shells(std::span<const ShellId> shell_ids, std::uint64_t max_results) const;
    Result<std::vector<DiagnosticId>> diagnostics_for_edges(std::span<const EdgeId> edge_ids, std::uint64_t max_results) const;
    Result<std::vector<DiagnosticId>> diagnostics_for_vertices(std::span<const VertexId> vertex_ids, std::uint64_t max_results) const;
    Result<void> export_grouped_by_code_txt(std::string_view path) const;
    Result<void> export_grouped_by_entity_txt(std::string_view path) const;
    Result<void> export_grouped_by_severity_txt(std::string_view path) const;
    Result<void> export_grouped_by_code_json(std::string_view path) const;
    Result<void> export_grouped_by_entity_json(std::string_view path) const;
    Result<void> export_grouped_by_severity_json(std::string_view path) const;
    Result<std::unordered_map<std::string, std::uint64_t>> issue_stage_histogram() const;
    Result<void> export_grouped_by_stage_txt(std::string_view path) const;
    Result<void> export_grouped_by_stage_json(std::string_view path) const;
    Result<std::vector<std::string>> summaries_by_ids(std::span<const DiagnosticId> ids) const;
    Result<DiagnosticReport> merge_reports(std::span<const DiagnosticId> ids, std::string_view summary) const;
    Result<DiagnosticId> copy_report(DiagnosticId id) const;
    Result<DiagnosticId> create_report(std::string_view summary, std::span<const Issue> issues);
    Result<std::uint64_t> remove_reports(std::span<const DiagnosticId> ids);
    Result<std::uint64_t> keep_only(std::span<const DiagnosticId> ids);
    Result<std::vector<DiagnosticId>> ids_sorted_asc() const;
    Result<std::vector<DiagnosticId>> ids_sorted_desc() const;
    Result<std::vector<DiagnosticId>> recent_ids_with_issue_code(std::string_view code, std::uint64_t max_results) const;
    Result<std::vector<DiagnosticId>> recent_ids_with_entity(std::uint64_t entity_id, std::uint64_t max_results) const;
    Result<DiagnosticStats> stats_of_ids(std::span<const DiagnosticId> ids) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

}  // namespace axiom
