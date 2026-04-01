#include "axiom/internal/geo/geometry_detail_bezier.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace axiom {
namespace detail {
namespace {
struct Vec4 {
  Scalar x{}, y{}, z{}, w{};
};

Vec4 lerp4(const Vec4 &a, const Vec4 &b, Scalar s) {
  return Vec4{a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s,
              a.z + (b.z - a.z) * s, a.w + (b.w - a.w) * s};
}

Vec4 de_casteljau4(std::vector<Vec4> cps, Scalar t) {
  if (cps.empty()) return Vec4{};
  while (cps.size() > 1) {
    for (std::size_t i = 0; i + 1 < cps.size(); ++i) {
      cps[i] = lerp4(cps[i], cps[i + 1], t);
    }
    cps.pop_back();
  }
  return cps.front();
}
}  // namespace

void rational_bezier_eval_all(const std::vector<Point3> &poles,
                              const std::vector<Scalar> &weights_in, Scalar t,
                              Point3 &out_p, Vec3 &out_d1, Vec3 &out_d2) {
  out_p = Point3{0.0, 0.0, 0.0};
  out_d1 = Vec3{0.0, 0.0, 0.0};
  out_d2 = Vec3{0.0, 0.0, 0.0};
  if (poles.empty()) return;
  if (poles.size() != weights_in.size() || poles.size() < 2) {
    out_p = poles.front();
    return;
  }

  const auto clamp01 = [](Scalar v) {
    if (v < 0.0) return Scalar(0.0);
    if (v > 1.0) return Scalar(1.0);
    return v;
  };
  const auto tt = clamp01(t);

  std::vector<Vec4> cps;
  cps.reserve(poles.size());
  for (std::size_t i = 0; i < poles.size(); ++i) {
    const auto w = weights_in[i];
    cps.push_back(Vec4{poles[i].x * w, poles[i].y * w, poles[i].z * w, w});
  }

  const auto eval_point = [&](Scalar param) -> Point3 {
    const auto h = de_casteljau4(cps, param);
    const auto ww = std::abs(h.w) > 1e-18 ? h.w : 1e-18;
    return Point3{h.x / ww, h.y / ww, h.z / ww};
  };

  out_p = eval_point(tt);

  const auto h = std::max<Scalar>(1e-6, 1e-4 * (Scalar(1.0) - std::abs(tt - 0.5)));
  const auto t0 = clamp01(tt - h);
  const auto t1 = clamp01(tt + h);
  const auto tm = tt;
  const auto p0 = eval_point(t0);
  const auto p1 = eval_point(t1);
  out_d1 = Vec3{(p1.x - p0.x) / (t1 - t0), (p1.y - p0.y) / (t1 - t0),
                (p1.z - p0.z) / (t1 - t0)};

  const auto hm = std::max<Scalar>(1e-4, 10.0 * h);
  const auto ta = clamp01(tm - hm);
  const auto tb = tm;
  const auto tc = clamp01(tm + hm);
  const auto pa = eval_point(ta);
  const auto pb = eval_point(tb);
  const auto pc = eval_point(tc);
  const auto denom = (tc - ta) * 0.5;
  if (denom > 1e-18) {
    const Vec3 d_left{(pb.x - pa.x) / (tb - ta + 1e-18),
                      (pb.y - pa.y) / (tb - ta + 1e-18),
                      (pb.z - pa.z) / (tb - ta + 1e-18)};
    const Vec3 d_right{(pc.x - pb.x) / (tc - tb + 1e-18),
                       (pc.y - pb.y) / (tc - tb + 1e-18),
                       (pc.z - pb.z) / (tc - tb + 1e-18)};
    out_d2 = Vec3{(d_right.x - d_left.x) / denom,
                  (d_right.y - d_left.y) / denom,
                  (d_right.z - d_left.z) / denom};
  }
}
}  // namespace detail
}  // namespace axiom
