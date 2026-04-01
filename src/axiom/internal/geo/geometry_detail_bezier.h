#pragma once

#include "axiom/geo/geometry_services.h"

#include <vector>

namespace axiom {
namespace detail {

/// 有理 Bezier 曲线求值（含有限差分导数占位），实现见 `geometry_detail_bezier.cpp`。
void rational_bezier_eval_all(const std::vector<Point3> &poles, const std::vector<Scalar> &weights_in,
                              Scalar t, Point3 &out_p, Vec3 &out_d1, Vec3 &out_d2);

}  // namespace detail
}  // namespace axiom
