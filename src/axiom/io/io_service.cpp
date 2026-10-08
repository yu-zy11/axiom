#include "axiom/io/io_service.h"

#include <atomic>
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
#include <locale>

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

template <typename Map>
void erase_io_allocations_since(Map& records, std::uint64_t first_id) {
    for (auto it = records.begin(); it != records.end();) {
        if (it->first >= first_id) {
            it = records.erase(it);
        } else {
            ++it;
        }
    }
}

// Single and batch imports invoke validation and auto-repair; their transaction
// boundary covers every model store and derived cache touched by that pipeline.
class IOImportBatchRollback {
public:
    explicit IOImportBatchRollback(detail::KernelState& state)
        : state_(state), first_id_(state.next_id),
          edge_to_coedges_(state.edge_to_coedges),
          coedge_to_loop_(state.coedge_to_loop),
          loop_to_faces_(state.loop_to_faces),
          face_to_shells_(state.face_to_shells),
          shell_to_bodies_(state.shell_to_bodies),
          tessellation_cache_(state.tessellation_cache),
          face_tessellation_cache_(state.face_tessellation_cache),
          tessellation_cache_stats_(state.tessellation_cache_stats),
          curve_eval_cache_(state.curve_eval_cache),
          surface_eval_cache_(state.surface_eval_cache),
          eval_invalid_(state.eval_invalid),
          eval_invalidation_bridge_(state.eval_invalidation_bridge) {}

    IOImportBatchRollback(const IOImportBatchRollback&) = delete;
    IOImportBatchRollback& operator=(const IOImportBatchRollback&) = delete;

    ~IOImportBatchRollback() {
        if (committed_) {
            return;
        }
        erase_io_allocations_since(state_.curves, first_id_);
        erase_io_allocations_since(state_.pcurves, first_id_);
        erase_io_allocations_since(state_.surfaces, first_id_);
        erase_io_allocations_since(state_.vertices, first_id_);
        erase_io_allocations_since(state_.edges, first_id_);
        erase_io_allocations_since(state_.coedges, first_id_);
        erase_io_allocations_since(state_.loops, first_id_);
        erase_io_allocations_since(state_.faces, first_id_);
        erase_io_allocations_since(state_.shells, first_id_);
        erase_io_allocations_since(state_.bodies, first_id_);
        erase_io_allocations_since(state_.meshes, first_id_);
        erase_io_allocations_since(state_.intersections, first_id_);
        state_.edge_to_coedges = std::move(edge_to_coedges_);
        state_.coedge_to_loop = std::move(coedge_to_loop_);
        state_.loop_to_faces = std::move(loop_to_faces_);
        state_.face_to_shells = std::move(face_to_shells_);
        state_.shell_to_bodies = std::move(shell_to_bodies_);
        state_.tessellation_cache = std::move(tessellation_cache_);
        state_.face_tessellation_cache = std::move(face_tessellation_cache_);
        state_.tessellation_cache_stats = tessellation_cache_stats_;
        state_.curve_eval_cache = std::move(curve_eval_cache_);
        state_.surface_eval_cache = std::move(surface_eval_cache_);
        state_.eval_invalid = std::move(eval_invalid_);
        state_.eval_invalidation_bridge = eval_invalidation_bridge_;
        state_.next_id = first_id_;
    }

    void commit() { committed_ = true; }

private:
    detail::KernelState& state_;
    std::uint64_t first_id_ {};
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> edge_to_coedges_;
    std::unordered_map<std::uint64_t, std::uint64_t> coedge_to_loop_;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> loop_to_faces_;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> face_to_shells_;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> shell_to_bodies_;
    std::unordered_map<std::string, MeshId> tessellation_cache_;
    std::unordered_map<std::string, MeshId> face_tessellation_cache_;
    TessellationCacheStats tessellation_cache_stats_ {};
    std::unordered_map<std::string, CurveEvalResult> curve_eval_cache_;
    std::unordered_map<std::string, SurfaceEvalResult> surface_eval_cache_;
    std::unordered_map<std::uint64_t, bool> eval_invalid_;
    EvalInvalidationBridgeMetrics eval_invalidation_bridge_ {};
    bool committed_ {false};
};

Result<void> io_failed_void(
    detail::KernelState& state, StatusCode status, std::string_view code,
    std::string message, std::string summary,
    std::vector<std::uint64_t> related_entities, std::string_view stage,
    std::vector<NumericEvidence> evidence = {}) {
    return error_void(
        status, create_io_failure_diagnostic(
                    state, status, code, std::move(message), std::move(summary),
                    std::move(related_entities), stage, std::move(evidence)));
}

Result<void> wrap_io_failed_void(
    detail::KernelState& state, const Result<void>& child,
    std::string_view code, std::string message, std::string summary,
    std::vector<std::uint64_t> related_entities, std::string_view stage,
    std::vector<NumericEvidence> evidence = {}) {
    return error_void(
        child.status,
        wrap_io_failure_diagnostic(
            state, child.status, child.diagnostic_id, code, std::move(message),
            std::move(summary), std::move(related_entities), stage,
            std::move(evidence)));
}

// Reserve an exclusive sibling directory rather than guessing an unused filename.
// The payload stays on the destination filesystem, and only a checked, closed
// stream may replace the destination. A failed export never truncates that file.
class IOExportFile {
public:
    explicit IOExportFile(std::string_view path, std::ios::openmode mode = std::ios::out)
        : destination_(std::string(path)) {
        std::error_code ec;
        const auto status = std::filesystem::status(destination_, ec);
        if (!ec && std::filesystem::is_directory(status)) {
            out.setstate(std::ios::failbit);
            return;
        }
        special_target_ = !ec && std::filesystem::exists(status) &&
                          !std::filesystem::is_regular_file(status);
        if (special_target_) {
            // Leave a closed stream in its initial state so the serializer and
            // finish() report a write failure without touching a device node.
            return;
        }
        const auto parent = destination_.parent_path();
        static std::atomic<std::uint64_t> sequence {0};
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            const auto candidate = parent / (".axiom_export_tmp_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            ec.clear();
            if (std::filesystem::create_directory(candidate, ec)) {
                temporary_directory_ = candidate;
                temporary_file_ = candidate / "payload";
                out.open(temporary_file_, mode | std::ios::out | std::ios::trunc);
                out.imbue(std::locale::classic());
                out << std::setprecision(std::numeric_limits<Scalar>::max_digits10);
                return;
            }
            if (ec) {
                break;
            }
        }
        out.setstate(std::ios::failbit);
    }

    IOExportFile(const IOExportFile&) = delete;
    IOExportFile& operator=(const IOExportFile&) = delete;

    ~IOExportFile() {
        if (out.is_open()) {
            out.close();
        }
        if (!temporary_directory_.empty()) {
            std::error_code ec;
            std::filesystem::remove(temporary_file_, ec);
            ec.clear();
            std::filesystem::remove(temporary_directory_, ec);
        }
    }

    void finish() {
        out.flush();
        out.close();
        // Devices and symlinks resolving to devices are never replaced. Keep the
        // historical write-stage failure, including /dev/full regression inputs.
        if (special_target_) {
            out.setstate(std::ios::failbit);
        }
    }

    Result<void> publish(detail::KernelState& state, BodyId body_id, std::string_view format) {
        std::error_code ec;
        const auto current_status = std::filesystem::status(destination_, ec);
        if (ec == std::errc::no_such_file_or_directory) {
            ec.clear();
        }
        if (!ec && std::filesystem::exists(current_status) &&
            !std::filesystem::is_regular_file(current_status)) {
            ec = std::make_error_code(std::errc::operation_not_permitted);
        }
        if (!ec) {
            if (out && !out.is_open()) {
                std::filesystem::rename(temporary_file_, destination_, ec);
            } else {
                ec = std::make_error_code(std::errc::io_error);
            }
        }
        if (ec) {
            return io_failed_void(
                state, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
                "导出失败：无法发布完整输出文件", "导出失败", {body_id.value},
                "io.export." + std::string(format) + ".publish",
                {{"filesystem_error", static_cast<Scalar>(ec.value()), "code"}});
        }
        return ok_void({});
    }

    std::ofstream out;

private:
    std::filesystem::path destination_;
    std::filesystem::path temporary_directory_;
    std::filesystem::path temporary_file_;
    bool special_target_ {false};
};

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
    return error_void(
        result.status,
        wrap_io_failure_diagnostic(
            state, result.status, result.diagnostic_id,
            diag_codes::kIoExportFailure,
            "网格导出子流程失败且未提供可传播的问题记录",
            "网格导出失败", {body_id.value}, stage,
            {{"body_id", static_cast<Scalar>(body_id.value), "id"}}));
}

Result<BodyId> exact_brep_import_failure(detail::KernelState& state, StatusCode status,
                                         std::string_view code, std::string message,
                                         std::string summary, std::string_view stage) {
    return error_result<BodyId>(
        status, create_io_failure_diagnostic(
                    state, status, code, std::move(message), std::move(summary),
                    {0}, stage, {{"materialized_body_count", 0.0, "count"}}));
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
