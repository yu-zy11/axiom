#include "axiom/internal/diag/diagnostic_internal_utils.h"

#include <algorithm>
#include <utility>

namespace axiom::detail {

std::uint64_t diagnostic_issue_count_by_severity(const KernelState& state, IssueSeverity severity) {
    std::uint64_t count = 0;
    for (const auto& [_, report] : state.diagnostics) {
        for (const auto& issue : report.issues) {
            if (issue.severity == severity) {
                ++count;
            }
        }
    }
    return count;
}

DiagnosticStats diagnostic_stats(const KernelState& state) {
    DiagnosticStats out;
    out.total = static_cast<std::uint64_t>(state.diagnostics.size());
    out.info = diagnostic_issue_count_by_severity(state, IssueSeverity::Info);
    out.warning = diagnostic_issue_count_by_severity(state, IssueSeverity::Warning);
    out.error = diagnostic_issue_count_by_severity(state, IssueSeverity::Error);
    out.fatal = diagnostic_issue_count_by_severity(state, IssueSeverity::Fatal);
    return out;
}

std::vector<DiagnosticId> diagnostic_find_by_issue_code(
    const KernelState& state, std::string_view code, std::uint64_t max_results) {
    std::vector<DiagnosticId> result;
    if (max_results == 0 || code.empty()) {
        return result;
    }
    std::vector<std::uint64_t> ordered_ids;
    ordered_ids.reserve(state.diagnostics.size());
    for (const auto& [id, _] : state.diagnostics) {
        ordered_ids.push_back(id);
    }
    std::sort(ordered_ids.begin(), ordered_ids.end(), std::greater<>());
    for (const auto id : ordered_ids) {
        const auto it = state.diagnostics.find(id);
        if (it == state.diagnostics.end()) {
            continue;
        }
        const auto has_code = std::any_of(it->second.issues.begin(), it->second.issues.end(),
                                          [code](const Issue& issue) { return issue.code == code; });
        if (!has_code) {
            continue;
        }
        result.push_back(DiagnosticId {id});
        if (result.size() >= max_results) {
            break;
        }
    }
    return result;
}

std::vector<DiagnosticId> diagnostic_snapshot_recent(const KernelState& state, std::uint64_t max_results) {
    std::vector<DiagnosticId> result;
    if (max_results == 0) {
        return result;
    }
    std::vector<std::uint64_t> ordered_ids;
    ordered_ids.reserve(state.diagnostics.size());
    for (const auto& [id, _] : state.diagnostics) {
        ordered_ids.push_back(id);
    }
    std::sort(ordered_ids.begin(), ordered_ids.end(), std::greater<>());
    for (const auto id : ordered_ids) {
        result.push_back(DiagnosticId {id});
        if (result.size() >= max_results) {
            break;
        }
    }
    return result;
}

std::uint64_t diagnostic_prune_to_max(KernelState& state, std::uint64_t max_keep) {
    if (state.diagnostics.size() <= max_keep) {
        return 0;
    }
    std::vector<std::uint64_t> ordered_ids;
    ordered_ids.reserve(state.diagnostics.size());
    for (const auto& [id, _] : state.diagnostics) {
        ordered_ids.push_back(id);
    }
    std::sort(ordered_ids.begin(), ordered_ids.end());
    const auto erase_count = state.diagnostics.size() - static_cast<std::size_t>(max_keep);
    for (std::size_t i = 0; i < erase_count; ++i) {
        state.diagnostics.erase(ordered_ids[i]);
    }
    return static_cast<std::uint64_t>(erase_count);
}

}  // namespace axiom::detail
