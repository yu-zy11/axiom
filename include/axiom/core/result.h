#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "axiom/core/types.h"

namespace axiom {

template <typename T> struct Result {
  StatusCode status{StatusCode::Ok};
  std::optional<T> value;
  std::vector<Warning> warnings;
  DiagnosticId diagnostic_id{};
};

template <> struct Result<void> {
  StatusCode status{StatusCode::Ok};
  std::vector<Warning> warnings;
  DiagnosticId diagnostic_id{};
};

template <typename T>
inline Result<T> ok_result(T value, DiagnosticId diagnostic_id = {}) {
  Result<T> result;
  result.status = StatusCode::Ok;
  result.value = std::move(value);
  result.diagnostic_id = diagnostic_id;
  return result;
}

template <typename T>
inline Result<T> error_result(StatusCode status,
                              DiagnosticId diagnostic_id = {},
                              std::vector<Warning> warnings = {}) {
  Result<T> result;
  result.status = status;
  result.diagnostic_id = diagnostic_id;
  result.warnings = std::move(warnings);
  return result;
}

inline Result<void> ok_void(DiagnosticId diagnostic_id = {}) {
  Result<void> result;
  result.status = StatusCode::Ok;
  result.diagnostic_id = diagnostic_id;
  return result;
}

inline Result<void> error_void(StatusCode status,
                               DiagnosticId diagnostic_id = {},
                               std::vector<Warning> warnings = {}) {
  Result<void> result;
  result.status = status;
  result.diagnostic_id = diagnostic_id;
  result.warnings = std::move(warnings);
  return result;
}

} // namespace axiom
