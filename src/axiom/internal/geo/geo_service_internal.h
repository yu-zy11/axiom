#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "axiom/core/types.h"
#include "axiom/internal/core/kernel_state.h"

namespace axiom {
namespace geo_internal {

inline constexpr Scalar kEpsilon = 1e-12;
inline constexpr int kClosestParamRefineIters = 8;

template <typename F>
Vec3 surface_partial_u_from_eval(const F &eval_fn, Scalar cu, Scalar cv,
                                 const Range2D &domain) {
  const auto step_u =
      std::max((domain.u.max - domain.u.min) * 1e-4, Scalar{1e-6});
  const auto u0 = std::clamp(cu - step_u, domain.u.min, domain.u.max);
  const auto u1 = std::clamp(cu + step_u, domain.u.min, domain.u.max);
  const Scalar denom = u1 - u0;
  if (!(std::abs(denom) > 1e-18)) {
    return Vec3{0.0, 0.0, 0.0};
  }
  return detail::scale(detail::subtract(eval_fn(u1, cv), eval_fn(u0, cv)),
                       1.0 / denom);
}

template <typename F>
Vec3 surface_partial_v_from_eval(const F &eval_fn, Scalar cu, Scalar cv,
                                 const Range2D &domain) {
  const auto step_v =
      std::max((domain.v.max - domain.v.min) * 1e-4, Scalar{1e-6});
  const auto v0 = std::clamp(cv - step_v, domain.v.min, domain.v.max);
  const auto v1 = std::clamp(cv + step_v, domain.v.min, domain.v.max);
  const Scalar denom = v1 - v0;
  if (!(std::abs(denom) > 1e-18)) {
    return Vec3{0.0, 0.0, 0.0};
  }
  return detail::scale(detail::subtract(eval_fn(cu, v1), eval_fn(cu, v0)),
                       1.0 / denom);
}

}  // namespace geo_internal
}  // namespace axiom
