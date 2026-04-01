#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "axiom/core/result.h"
#include "axiom/diag/error_codes.h"
#include "axiom/internal/core/kernel_state.h"

namespace axiom::detail {

inline Warning make_warning(std::string_view code, std::string message) {
    return Warning {std::string(code), std::move(message)};
}

inline Issue make_error_issue(std::string_view code, std::string message) {
    Issue issue;
    issue.code = std::string(code);
    issue.severity = IssueSeverity::Error;
    issue.message = std::move(message);
    return issue;
}

inline Issue make_error_issue(std::string_view code, std::string message,
                              std::vector<std::uint64_t> related_entities) {
    Issue issue;
    issue.code = std::string(code);
    issue.severity = IssueSeverity::Error;
    issue.message = std::move(message);
    issue.related_entities = std::move(related_entities);
    return issue;
}

inline Issue make_info_issue(std::string_view code, std::string message) {
    Issue issue;
    issue.code = std::string(code);
    issue.severity = IssueSeverity::Info;
    issue.message = std::move(message);
    return issue;
}

inline Issue make_warning_issue(std::string_view code, std::string message) {
    Issue issue;
    issue.code = std::string(code);
    issue.severity = IssueSeverity::Warning;
    issue.message = std::move(message);
    return issue;
}

inline DiagnosticId success_diag(KernelState& state, std::string summary) {
    return state.create_diagnostic(std::move(summary));
}

inline DiagnosticId error_diag(KernelState& state, std::string summary, std::string_view code, std::string message) {
    return state.create_diagnostic(std::move(summary), {make_error_issue(code, std::move(message))});
}

inline DiagnosticId error_diag(KernelState& state, std::string summary, std::string_view code,
                               std::string message, std::vector<std::uint64_t> related_entities) {
    return state.create_diagnostic(std::move(summary),
                                   {make_error_issue(code, std::move(message), std::move(related_entities))});
}

inline DiagnosticId warning_diag(KernelState& state, std::string summary, std::string_view code, std::string message) {
    return state.create_diagnostic(std::move(summary), {make_warning_issue(code, std::move(message))});
}

template <typename T>
inline Result<T> invalid_input_result(KernelState& state, std::string_view code, std::string message, std::string summary) {
    return error_result<T>(StatusCode::InvalidInput, error_diag(state, std::move(summary), code, std::move(message)));
}

template <typename T>
inline Result<T> failed_result(KernelState& state, StatusCode status, std::string_view code, std::string message, std::string summary) {
    return error_result<T>(status, error_diag(state, std::move(summary), code, std::move(message)));
}

template <typename T>
inline Result<T> failed_result(KernelState& state, StatusCode status, std::string_view code, std::string message, std::string summary,
                               std::vector<std::uint64_t> related_entities) {
    return error_result<T>(status,
                           error_diag(state, std::move(summary), code, std::move(message), std::move(related_entities)));
}

inline Result<void> invalid_input_void(KernelState& state, std::string_view code, std::string message, std::string summary) {
    return error_void(StatusCode::InvalidInput, error_diag(state, std::move(summary), code, std::move(message)));
}

inline Result<void> failed_void(KernelState& state, StatusCode status, std::string_view code, std::string message, std::string summary) {
    return error_void(status, error_diag(state, std::move(summary), code, std::move(message)));
}

inline Result<void> failed_void(KernelState& state, StatusCode status, std::string_view code,
                                std::string message, std::string summary,
                                std::vector<std::uint64_t> related_entities) {
    return error_void(status, error_diag(state, std::move(summary), code, std::move(message), std::move(related_entities)));
}

}  // namespace axiom::detail

