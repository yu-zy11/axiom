#pragma once

#include <utility>

#include "axiom/core/types.h"
#include "axiom/internal/core/kernel_state.h"

namespace axiom::geo_internal {

/// 与 `geometry_services.cpp` 中 `geo_service_internal_part2.inc` 的实现链接（供 `axiom_rep` 做参数域三角化）。
Range2D surface_domain(const detail::SurfaceRecord& surface);
bool bezier_tensor_surface_eval_with_partials(const detail::SurfaceRecord& surface, Scalar u, Scalar v,
                                              Point3& out_p, Vec3& out_du, Vec3& out_dv);
bool nurbs_tensor_surface_eval_with_partials(const detail::SurfaceRecord& surface, Scalar u, Scalar v,
                                             Point3& out_p, Vec3& out_du, Vec3& out_dv);
std::pair<Scalar, Scalar> approximate_surface_uv(const detail::SurfaceRecord& surface, const Point3& point);

/// 派生/修剪曲面：`approximate_surface_uv` 无状态路径不可靠时用 `SurfaceService::closest_uv`（`state` 非拥有借用）。
std::pair<Scalar, Scalar> rep_project_point_to_surface_uv(detail::KernelState* state, SurfaceId surface_id,
                                                          const detail::SurfaceRecord& surface,
                                                          const Point3& point);

/// Revolved / Swept / Offset：需要 `KernelState` 与曲面句柄以调用 `SurfaceService::eval`。
bool rep_surface_eval_grid_partials(detail::KernelState* state, SurfaceId surface_id, Scalar u, Scalar v,
                                    Point3& out_p, Vec3& out_du, Vec3& out_dv, Vec3& out_n);
bool rep_surface_eval_grid_point(detail::KernelState* state, SurfaceId surface_id, Scalar u, Scalar v,
                                 Point3& out_p, Vec3& out_n);

/// 取 patch 参数点处主曲率幅度 `max(|k1|,|k2|)`：优先 `SurfaceService::eval`；无宿主句柄时对 Bezier/BSpline/NURBS 走张量主曲率。
bool rep_patch_principal_curvatures_max(detail::KernelState* state, SurfaceId surface_id,
                                      const detail::SurfaceRecord& surf, Scalar u, Scalar v,
                                      Scalar& k_max_abs);

}  // namespace axiom::geo_internal
