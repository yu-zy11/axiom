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

/// 无诊断、无缓存的内部曲线点求值，供允许依赖 GeoCore 的上层模块校验拓扑裁剪参数。
/// 返回 false 表示曲线/子曲线引用无效或结果包含非有限数值。
bool evaluate_curve_point_no_cache(const detail::KernelState& state,
                                   CurveId curve_id, Scalar parameter,
                                   Point3& point);

/// 计算曲线裁剪区间的保守包围盒；解析圆锥曲线包含区间内坐标极值，
/// Bezier/样条使用控制点凸包，CompositeChain 使用相关子曲线全域包围盒。
bool curve_interval_bbox_no_cache(const detail::KernelState& state,
                                  CurveId curve_id, Scalar start_parameter,
                                  Scalar end_parameter, BoundingBox& bbox);

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
