#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "axiom/core/types.h"
#include "axiom/internal/core/kernel_state.h"

namespace axiom::detail {

std::uint64_t diagnostic_issue_count_by_severity(const KernelState& state, IssueSeverity severity);
DiagnosticStats diagnostic_stats(const KernelState& state);
std::vector<DiagnosticId> diagnostic_find_by_issue_code(
    const KernelState& state, std::string_view code, std::uint64_t max_results);
std::vector<DiagnosticId> diagnostic_snapshot_recent(const KernelState& state, std::uint64_t max_results);
std::uint64_t diagnostic_prune_to_max(KernelState& state, std::uint64_t max_keep);

}  // namespace axiom::detail
