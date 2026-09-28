#include "axiom/io/io_service.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <regex>
#include <sstream>
#include <type_traits>
#include <system_error>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <iomanip>
#include <limits>

#include "axiom/heal/heal_services.h"
#include "axiom/rep/representation_conversion_service.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/rep/representation_internal_utils.h"

#include "axiom/internal/io/io_service_internal.h"
#include "axiom/internal/io/step_iges_standard_scan.h"

namespace axiom {

using namespace io_internal;

namespace {

// Only meshes and tessellation caches are written by export conversion. Keep
// existing embedded/cached meshes; discard this call's allocations on failure.
struct MeshExportRollback {
    detail::KernelState& state;
    const std::uint64_t next_id;
    std::unordered_map<std::string, MeshId> body_cache;
    std::unordered_map<std::string, MeshId> face_cache;
    TessellationCacheStats cache_stats;
    bool completed {false};

    explicit MeshExportRollback(detail::KernelState& input)
        : state(input), next_id(input.next_id), body_cache(input.tessellation_cache),
          face_cache(input.face_tessellation_cache), cache_stats(input.tessellation_cache_stats) {}

    ~MeshExportRollback() {
        if (!completed) {
            std::erase_if(state.meshes, [this](const auto& entry) { return entry.first >= next_id; });
            state.tessellation_cache.swap(body_cache);
            state.face_tessellation_cache.swap(face_cache);
            state.tessellation_cache_stats = cache_stats;
            state.next_id = next_id;
        }
    }
};

Result<void> bind_mesh_export_failure(detail::KernelState& state, Result<void> result,
                                      BodyId body_id, std::string_view stage) {
    const auto it = state.diagnostics.find(result.diagnostic_id.value);
    if (it != state.diagnostics.end()) {
        for (auto& issue : it->second.issues) {
            issue.stage = std::string(stage);
            if (std::find(issue.related_entities.begin(), issue.related_entities.end(), body_id.value) ==
                issue.related_entities.end()) {
                issue.related_entities.push_back(body_id.value);
            }
        }
    }
    return result;
}

Result<BodyId> exact_brep_import_failure(detail::KernelState& state, StatusCode status,
                                         std::string_view code, std::string message,
                                         std::string summary, std::string_view stage) {
    auto issue = detail::make_error_issue(code, std::move(message));
    issue.stage = std::string(stage);
    return error_result<BodyId>(status,
                                state.create_diagnostic(std::move(summary), {std::move(issue)}));
}

std::optional<Result<BodyId>> reject_invalid_exact_brep_record(
    detail::KernelState& state, const detail::BodyRecord& record,
    std::string_view format_name, std::string_view stage) {
    switch (validate_exact_brep_body_record(record)) {
        case ExactBrepRecordValidationFailure::None:
            return std::nullopt;
        case ExactBrepRecordValidationFailure::NonFinite:
            return exact_brep_import_failure(
                state, StatusCode::InvalidInput, diag_codes::kValNonFiniteGeometry,
                std::string(format_name) + " 导入失败：几何记录包含 NaN 或 Inf",
                std::string(format_name) + " 导入失败", stage);
        case ExactBrepRecordValidationFailure::InvalidBounds:
            return exact_brep_import_failure(
                state, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                std::string(format_name) + " 导入失败：包围盒边界反转或无效",
                std::string(format_name) + " 导入失败", stage);
        case ExactBrepRecordValidationFailure::DegenerateAxis:
            return exact_brep_import_failure(
                state, StatusCode::DegenerateGeometry, diag_codes::kValDegenerateGeometry,
                std::string(format_name) + " 导入失败：几何轴方向退化",
                std::string(format_name) + " 导入失败", stage);
    }
    return std::nullopt;
}

}  // namespace

IOService::IOService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

#include "axiom/internal/io/io_service_part1.inc"
#include "axiom/internal/io/io_service_part2.inc"
#include "axiom/internal/io/io_service_part3.inc"

}  // namespace axiom
