

Range2D surface_domain(const detail::SurfaceRecord &surface) {
  const auto inf = std::numeric_limits<Scalar>::infinity();
  const auto two_pi = std::acos(-1.0) * 2.0;
  switch (surface.kind) {
  case detail::SurfaceKind::Plane:
    return Range2D{Range1D{-inf, inf}, Range1D{-inf, inf}};
  case detail::SurfaceKind::Sphere:
    return Range2D{Range1D{0.0, two_pi}, Range1D{0.0, std::acos(-1.0)}};
  case detail::SurfaceKind::Cylinder:
    return Range2D{Range1D{0.0, two_pi}, Range1D{-inf, inf}};
  case detail::SurfaceKind::Cone:
    // v is axial distance from apex along the cone axis direction in our parameterization.
    return Range2D{Range1D{0.0, two_pi}, Range1D{0.0, inf}};
  case detail::SurfaceKind::Torus:
    return Range2D{Range1D{0.0, two_pi}, Range1D{0.0, two_pi}};
  case detail::SurfaceKind::Bezier:
    return Range2D{Range1D{0.0, 1.0}, Range1D{0.0, 1.0}};
  case detail::SurfaceKind::BSpline:
  case detail::SurfaceKind::Nurbs: {
    const auto [rows, cols] = infer_surface_grid_dims(surface.poles.size());
    return Range2D{
        Range1D{0.0, std::max<Scalar>(1.0, static_cast<Scalar>(rows - 1))},
        Range1D{0.0, std::max<Scalar>(1.0, static_cast<Scalar>(cols - 1))}};
  }
  case detail::SurfaceKind::Revolved:
    return Range2D{Range1D{0.0, std::max(surface.sweep_angle_rad, 0.0)},
                   Range1D{0.0, 1.0}};
  case detail::SurfaceKind::Swept:
    return Range2D{Range1D{0.0, 1.0}, Range1D{0.0, 1.0}};
  case detail::SurfaceKind::Trimmed:
    return Range2D{Range1D{surface.trim_u_min, surface.trim_u_max},
                   Range1D{surface.trim_v_min, surface.trim_v_max}};
  case detail::SurfaceKind::Offset:
    // Record-only path: true domain matches base surface (see SurfaceService::domain).
    return Range2D{Range1D{0.0, 1.0}, Range1D{0.0, 1.0}};
  default:
    return Range2D{Range1D{0.0, 1.0}, Range1D{0.0, 1.0}};
  }
}

// ---- 张量积 NURBS/BSpline 曲面：开放均匀结点 + 齐次 de Boor（Piegl–Tiller 标准嵌套算法）----
struct Hom4 {
  Scalar x{};
  Scalar y{};
  Scalar z{};
  Scalar w{};
};

Hom4 lerp_hom4(const Hom4 &a, const Hom4 &b, Scalar t) {
  return Hom4{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
              a.w + (b.w - a.w) * t};
}

template <typename GetHom>
Hom4 bspline_de_boor_hom4_access(const GetHom &get_hom, int p, int k,
                                 const std::vector<Scalar> &U, Scalar u) {
  if (p == 0) {
    return get_hom(k);
  }
  std::vector<Hom4> d(static_cast<std::size_t>(p + 1));
  for (int i = 0; i <= p; ++i) {
    d[static_cast<std::size_t>(i)] = get_hom(k - p + i);
  }
  for (int r = 1; r <= p; ++r) {
    for (int i = p; i >= r; --i) {
      const int idx = k - p + i;
      const Scalar left = U[static_cast<std::size_t>(idx)];
      const Scalar right = U[static_cast<std::size_t>(idx + p - r + 1)];
      const Scalar denom = right - left;
      Scalar alpha = 0.0;
      if (std::abs(denom) > 1e-24) {
        alpha = (u - left) / denom;
      }
      d[static_cast<std::size_t>(i)] =
          lerp_hom4(d[static_cast<std::size_t>(i - 1)], d[static_cast<std::size_t>(i)], alpha);
    }
  }
  return d[static_cast<std::size_t>(p)];
}

Hom4 tensor_hom4_eval(const std::vector<Hom4> &h_net, int nu, int nv,
                      const std::vector<Scalar> &U, const std::vector<Scalar> &V, int pu,
                      int pv, Scalar u, Scalar v) {
  const int ku = bspline_find_span(static_cast<std::size_t>(nu), pu, u, U);
  const int kv = bspline_find_span(static_cast<std::size_t>(nv), pv, v, V);
  std::vector<Hom4> strip(static_cast<std::size_t>(pv + 1));
  for (int jc = 0; jc <= pv; ++jc) {
    const int col = kv - pv + jc;
    auto get_u = [&](int row_idx) -> Hom4 {
      return h_net[static_cast<std::size_t>(row_idx * nv + col)];
    };
    strip[static_cast<std::size_t>(jc)] =
        bspline_de_boor_hom4_access(get_u, pu, ku, U, u);
  }
  auto get_v = [&](int j_global) -> Hom4 {
    return strip[static_cast<std::size_t>(j_global - (kv - pv))];
  };
  return bspline_de_boor_hom4_access(get_v, pv, kv, V, v);
}

Point3 hom4_to_euclidean(const Hom4 &h) {
  const Scalar ww =
      std::abs(h.w) > 1e-30 ? h.w : (h.w >= 0 ? static_cast<Scalar>(1e-30)
                                             : static_cast<Scalar>(-1e-30));
  return Point3{h.x / ww, h.y / ww, h.z / ww};
}

void hom4_quotient_derivative(const Hom4 &a, const Hom4 &da, Vec3 &out) {
  const Scalar w2 = std::max(a.w * a.w, static_cast<Scalar>(1e-36));
  out.x = (da.x * a.w - a.x * da.w) / w2;
  out.y = (da.y * a.w - a.y * da.w) / w2;
  out.z = (da.z * a.w - a.z * da.w) / w2;
}

std::vector<Hom4> nurbs_surface_homogeneous_net(const detail::SurfaceRecord &surface,
                                                int nu, int nv) {
  std::vector<Hom4> h(static_cast<std::size_t>(nu * nv));
  const bool use_w = surface.kind == detail::SurfaceKind::Nurbs &&
                     surface.weights.size() == surface.poles.size();
  for (int i = 0; i < nu; ++i) {
    for (int j = 0; j < nv; ++j) {
      const std::size_t ix = static_cast<std::size_t>(i * nv + j);
      const Point3 &p = surface.poles[ix];
      Scalar w = 1.0;
      if (use_w) {
        w = surface.weights[ix];
      }
      h[ix] = Hom4{p.x * w, p.y * w, p.z * w, w};
    }
  }
  return h;
}

bool nurbs_tensor_surface_prepare(const detail::SurfaceRecord &surface, int &nu, int &nv,
                                  int &pu, int &pv, std::vector<Scalar> &U,
                                  std::vector<Scalar> &V) {
  const auto [rows, cols] = infer_surface_grid_dims(surface.poles.size());
  if (rows < 2 || cols < 2) {
    return false;
  }
  nu = static_cast<int>(rows);
  nv = static_cast<int>(cols);

  auto resolve_p_dim = [&](int n, int stored_p,
                           const std::vector<Scalar> &knots) -> int {
    if (stored_p >= 0) {
      return stored_p;
    }
    const int pk = infer_bspline_degree_from_knot_length(knots.size(), n);
    if (pk >= 0) {
      return pk;
    }
    return bspline_degree_for_control_count(static_cast<std::size_t>(n));
  };

  pu = resolve_p_dim(nu, surface.spline_degree_u, surface.knots_u);
  pv = resolve_p_dim(nv, surface.spline_degree_v, surface.knots_v);

  const std::size_t need_u = static_cast<std::size_t>(nu + pu + 1);
  const std::size_t need_v = static_cast<std::size_t>(nv + pv + 1);

  if (surface.knots_u.size() == need_u &&
      bspline_knot_vector_non_decreasing(surface.knots_u)) {
    U = surface.knots_u;
  } else {
    U = bspline_open_uniform_knots(static_cast<std::size_t>(nu), pu);
    bspline_affine_scale_knots_to_span(U, pu, static_cast<std::size_t>(nu),
                                       std::max<Scalar>(1.0, static_cast<Scalar>(nu - 1)));
  }
  if (surface.knots_v.size() == need_v &&
      bspline_knot_vector_non_decreasing(surface.knots_v)) {
    V = surface.knots_v;
  } else {
    V = bspline_open_uniform_knots(static_cast<std::size_t>(nv), pv);
    bspline_affine_scale_knots_to_span(V, pv, static_cast<std::size_t>(nv),
                                       std::max<Scalar>(1.0, static_cast<Scalar>(nv - 1)));
  }
  return true;
}

bool nurbs_tensor_surface_eval_pos(const detail::SurfaceRecord &surface, Scalar u, Scalar v,
                                   Point3 &out_p) {
  int nu = 0;
  int nv = 0;
  int pu = 0;
  int pv = 0;
  std::vector<Scalar> U;
  std::vector<Scalar> V;
  if (!nurbs_tensor_surface_prepare(surface, nu, nv, pu, pv, U, V)) {
    return false;
  }
  const auto dom = surface_domain(surface);
  const Scalar cu = std::clamp(u, dom.u.min, dom.u.max);
  const Scalar cv = std::clamp(v, dom.v.min, dom.v.max);
  const auto h_net = nurbs_surface_homogeneous_net(surface, nu, nv);
  const Hom4 a = tensor_hom4_eval(h_net, nu, nv, U, V, pu, pv, cu, cv);
  out_p = hom4_to_euclidean(a);
  return true;
}

bool nurbs_tensor_surface_eval_with_partials(const detail::SurfaceRecord &surface, Scalar u,
                                               Scalar v, Point3 &out_p, Vec3 &out_du,
                                               Vec3 &out_dv) {
  int nu = 0;
  int nv = 0;
  int pu = 0;
  int pv = 0;
  std::vector<Scalar> U;
  std::vector<Scalar> V;
  if (!nurbs_tensor_surface_prepare(surface, nu, nv, pu, pv, U, V)) {
    return false;
  }
  const auto dom = surface_domain(surface);
  const Scalar cu = std::clamp(u, dom.u.min, dom.u.max);
  const Scalar cv = std::clamp(v, dom.v.min, dom.v.max);
  const auto h_net = nurbs_surface_homogeneous_net(surface, nu, nv);
  const Hom4 a = tensor_hom4_eval(h_net, nu, nv, U, V, pu, pv, cu, cv);
  out_p = hom4_to_euclidean(a);

  Hom4 dau{0.0, 0.0, 0.0, 0.0};
  if (pu > 0) {
    const std::vector<Scalar> ut = bspline_trim_knots_for_derivative(U);
    const int nu1 = nu - 1;
    const int pu1 = pu - 1;
    if (static_cast<int>(ut.size()) >= nu1 + pu1 + 1 && nu1 > 0) {
      std::vector<Hom4> hdu(static_cast<std::size_t>(nu1 * nv));
      for (int j = 0; j < nv; ++j) {
        for (int i = 0; i < nu1; ++i) {
          const Scalar den = U[static_cast<std::size_t>(i + pu + 1)] -
                             U[static_cast<std::size_t>(i + 1)];
          Scalar s = 0.0;
          if (std::abs(den) > 1e-30) {
            s = static_cast<Scalar>(pu) / den;
          }
          const Hom4 &aa = h_net[static_cast<std::size_t>(i * nv + j)];
          const Hom4 &bb = h_net[static_cast<std::size_t>((i + 1) * nv + j)];
          hdu[static_cast<std::size_t>(i * nv + j)] =
              Hom4{s * (bb.x - aa.x), s * (bb.y - aa.y), s * (bb.z - aa.z),
                   s * (bb.w - aa.w)};
        }
      }
      dau = tensor_hom4_eval(hdu, nu1, nv, ut, V, pu1, pv, cu, cv);
    }
  }

  Hom4 dav{0.0, 0.0, 0.0, 0.0};
  if (pv > 0) {
    const std::vector<Scalar> vt = bspline_trim_knots_for_derivative(V);
    const int nv1 = nv - 1;
    const int pv1 = pv - 1;
    if (static_cast<int>(vt.size()) >= nv1 + pv1 + 1 && nv1 > 0) {
      std::vector<Hom4> hdv(static_cast<std::size_t>(nu * nv1));
      for (int i = 0; i < nu; ++i) {
        for (int j = 0; j < nv1; ++j) {
          const Scalar den = V[static_cast<std::size_t>(j + pv + 1)] -
                             V[static_cast<std::size_t>(j + 1)];
          Scalar s = 0.0;
          if (std::abs(den) > 1e-30) {
            s = static_cast<Scalar>(pv) / den;
          }
          const Hom4 &aa = h_net[static_cast<std::size_t>(i * nv + j)];
          const Hom4 &bb = h_net[static_cast<std::size_t>(i * nv + j + 1)];
          hdv[static_cast<std::size_t>(i * nv1 + j)] =
              Hom4{s * (bb.x - aa.x), s * (bb.y - aa.y), s * (bb.z - aa.z),
                   s * (bb.w - aa.w)};
        }
      }
      dav = tensor_hom4_eval(hdv, nu, nv1, U, vt, pu, pv1, cu, cv);
    }
  }

  hom4_quotient_derivative(a, dau, out_du);
  hom4_quotient_derivative(a, dav, out_dv);
  return true;
}

Point3 bilinear_point(const Point3 &p00, const Point3 &p01, const Point3 &p10,
                      const Point3 &p11, Scalar u, Scalar v) {
  return lerp_point(lerp_point(p00, p01, v), lerp_point(p10, p11, v), u);
}

Point3 rational_bilinear_point(const Point3 &p00, const Point3 &p01,
                               const Point3 &p10, const Point3 &p11, Scalar w00,
                               Scalar w01, Scalar w10, Scalar w11, Scalar u,
                               Scalar v) {
  const auto b00 = (1.0 - u) * (1.0 - v);
  const auto b01 = (1.0 - u) * v;
  const auto b10 = u * (1.0 - v);
  const auto b11 = u * v;
  const auto rw00 = std::max(w00, kEpsilon) * b00;
  const auto rw01 = std::max(w01, kEpsilon) * b01;
  const auto rw10 = std::max(w10, kEpsilon) * b10;
  const auto rw11 = std::max(w11, kEpsilon) * b11;
  const auto total = rw00 + rw01 + rw10 + rw11;
  if (total <= kEpsilon) {
    return bilinear_point(p00, p01, p10, p11, u, v);
  }
  return Point3{(p00.x * rw00 + p01.x * rw01 + p10.x * rw10 + p11.x * rw11) /
                    total,
                (p00.y * rw00 + p01.y * rw01 + p10.y * rw10 + p11.y * rw11) /
                    total,
                (p00.z * rw00 + p01.z * rw01 + p10.z * rw10 + p11.z * rw11) /
                    total};
}

// 控制网格上当前实现为「单元双线性 / 有理双线性」片元：对 (lu,lv)∈[0,1]² 给出解析 ∂S/∂u、∂S/∂v（与 evaluate_surface_point 同一剖分）。
bool locate_surface_bilinear_cell(const detail::SurfaceRecord &surface, Scalar cu,
                                  Scalar cv, Point3 &out_p00, Point3 &out_p01,
                                  Point3 &out_p10, Point3 &out_p11, Scalar &out_lu,
                                  Scalar &out_lv, bool &out_rational, Scalar &out_w00,
                                  Scalar &out_w01, Scalar &out_w10,
                                  Scalar &out_w11) {
  if (surface.poles.empty()) {
    return false;
  }
  const auto [rows, cols] = infer_surface_grid_dims(surface.poles.size());
  if (rows < 2 || cols < 2) {
    return false;
  }
  const auto idx = [cols](std::size_t r, std::size_t c) { return r * cols + c; };
  std::size_t ru = 0;
  std::size_t rv = 0;
  Scalar lu = 0.0;
  Scalar lv = 0.0;
  if (surface.kind == detail::SurfaceKind::Bezier) {
    const auto domain = surface_domain(surface);
    const auto ccu = std::clamp(cu, domain.u.min, domain.u.max);
    const auto ccv = std::clamp(cv, domain.v.min, domain.v.max);
    ru = std::min<std::size_t>(
        static_cast<std::size_t>(std::floor(ccu * static_cast<Scalar>(rows - 1))),
        rows - 2);
    rv = std::min<std::size_t>(
        static_cast<std::size_t>(std::floor(ccv * static_cast<Scalar>(cols - 1))),
        cols - 2);
    lu = ccu * static_cast<Scalar>(rows - 1) - static_cast<Scalar>(ru);
    lv = ccv * static_cast<Scalar>(cols - 1) - static_cast<Scalar>(rv);
  } else {
    const auto domain = surface_domain(surface);
    const auto ccu = std::clamp(cu, domain.u.min, domain.u.max);
    const auto ccv = std::clamp(cv, domain.v.min, domain.v.max);
    ru = std::min<std::size_t>(static_cast<std::size_t>(std::floor(ccu)), rows - 2);
    rv = std::min<std::size_t>(static_cast<std::size_t>(std::floor(ccv)), cols - 2);
    lu = ccu - static_cast<Scalar>(ru);
    lv = ccv - static_cast<Scalar>(rv);
  }
  out_p00 = surface.poles[idx(ru, rv)];
  out_p01 = surface.poles[idx(ru, rv + 1)];
  out_p10 = surface.poles[idx(ru + 1, rv)];
  out_p11 = surface.poles[idx(ru + 1, rv + 1)];
  out_lu = lu;
  out_lv = lv;
  out_rational = surface.kind == detail::SurfaceKind::Nurbs &&
                 surface.weights.size() == surface.poles.size();
  if (out_rational) {
    out_w00 = surface.weights[idx(ru, rv)];
    out_w01 = surface.weights[idx(ru, rv + 1)];
    out_w10 = surface.weights[idx(ru + 1, rv)];
    out_w11 = surface.weights[idx(ru + 1, rv + 1)];
  } else {
    out_w00 = out_w01 = out_w10 = out_w11 = 1.0;
  }
  return true;
}

void bilinear_patch_partials_non_rational(const Point3 &p00, const Point3 &p01,
                                          const Point3 &p10, const Point3 &p11,
                                          Scalar lu, Scalar lv, Point3 &out_p,
                                          Vec3 &out_du, Vec3 &out_dv) {
  out_p = bilinear_point(p00, p01, p10, p11, lu, lv);
  const auto du0 = detail::scale(detail::subtract(p10, p00), 1.0 - lv);
  const auto du1 = detail::scale(detail::subtract(p11, p01), lv);
  out_du = Vec3{du0.x + du1.x, du0.y + du1.y, du0.z + du1.z};
  const auto dv0 = detail::scale(detail::subtract(p01, p00), 1.0 - lu);
  const auto dv1 = detail::scale(detail::subtract(p11, p10), lu);
  out_dv = Vec3{dv0.x + dv1.x, dv0.y + dv1.y, dv0.z + dv1.z};
}

void rational_bilinear_patch_partials(const Point3 &p00, const Point3 &p01,
                                      const Point3 &p10, const Point3 &p11,
                                      Scalar w00, Scalar w01, Scalar w10,
                                      Scalar w11, Scalar lu, Scalar lv,
                                      Point3 &out_p, Vec3 &out_du,
                                      Vec3 &out_dv) {
  out_p = rational_bilinear_point(p00, p01, p10, p11, w00, w01, w10, w11, lu, lv);
  const Scalar b00 = (1.0 - lu) * (1.0 - lv);
  const Scalar b01 = (1.0 - lu) * lv;
  const Scalar b10 = lu * (1.0 - lv);
  const Scalar b11 = lu * lv;
  const Scalar db00_du = -(1.0 - lv);
  const Scalar db01_du = -lv;
  const Scalar db10_du = (1.0 - lv);
  const Scalar db11_du = lv;
  const Scalar db00_dv = -(1.0 - lu);
  const Scalar db01_dv = (1.0 - lu);
  const Scalar db10_dv = -lu;
  const Scalar db11_dv = lu;

  const auto nx = p00.x * w00 * b00 + p01.x * w01 * b01 + p10.x * w10 * b10 +
                  p11.x * w11 * b11;
  const auto ny = p00.y * w00 * b00 + p01.y * w01 * b01 + p10.y * w10 * b10 +
                  p11.y * w11 * b11;
  const auto nz = p00.z * w00 * b00 + p01.z * w01 * b01 + p10.z * w10 * b10 +
                  p11.z * w11 * b11;
  const Scalar wsum = w00 * b00 + w01 * b01 + w10 * b10 + w11 * b11;
  const Scalar w2 = std::max(wsum * wsum, static_cast<Scalar>(1e-36));

  const Scalar dnx_u = p00.x * w00 * db00_du + p01.x * w01 * db01_du +
                       p10.x * w10 * db10_du + p11.x * w11 * db11_du;
  const Scalar dny_u = p00.y * w00 * db00_du + p01.y * w01 * db01_du +
                       p10.y * w10 * db10_du + p11.y * w11 * db11_du;
  const Scalar dnz_u = p00.z * w00 * db00_du + p01.z * w01 * db01_du +
                       p10.z * w10 * db10_du + p11.z * w11 * db11_du;
  const Scalar dw_u = w00 * db00_du + w01 * db01_du + w10 * db10_du + w11 * db11_du;
  out_du.x = (dnx_u * wsum - nx * dw_u) / w2;
  out_du.y = (dny_u * wsum - ny * dw_u) / w2;
  out_du.z = (dnz_u * wsum - nz * dw_u) / w2;

  const Scalar dnx_v = p00.x * w00 * db00_dv + p01.x * w01 * db01_dv +
                       p10.x * w10 * db10_dv + p11.x * w11 * db11_dv;
  const Scalar dny_v = p00.y * w00 * db00_dv + p01.y * w01 * db01_dv +
                       p10.y * w10 * db10_dv + p11.y * w11 * db11_dv;
  const Scalar dnz_v = p00.z * w00 * db00_dv + p01.z * w01 * db01_dv +
                       p10.z * w10 * db10_dv + p11.z * w11 * db11_dv;
  const Scalar dw_v = w00 * db00_dv + w01 * db01_dv + w10 * db10_dv + w11 * db11_dv;
  out_dv.x = (dnx_v * wsum - nx * dw_v) / w2;
  out_dv.y = (dny_v * wsum - ny * dw_v) / w2;
  out_dv.z = (dnz_v * wsum - nz * dw_v) / w2;
}

// 张量积 Bézier：先对每列做 u 向 de Casteljau，再对结果做 v 向（或对称顺序；此处与标准嵌套一致）。
bool bezier_tensor_surface_eval_with_partials(const detail::SurfaceRecord &surface, Scalar u,
                                                Scalar v, Point3 &out_p, Vec3 &out_du,
                                                Vec3 &out_dv) {
  if (surface.kind != detail::SurfaceKind::Bezier) {
    return false;
  }
  const auto [rows, cols] = infer_surface_grid_dims(surface.poles.size());
  const int nr = static_cast<int>(rows);
  const int nc = static_cast<int>(cols);
  if (nr < 1 || nc < 1) {
    return false;
  }
  const auto dom = surface_domain(surface);
  const Scalar cu = std::clamp(u, dom.u.min, dom.u.max);
  const Scalar cv = std::clamp(v, dom.v.min, dom.v.max);
  const auto &poles = surface.poles;
  std::vector<Point3> q_row(static_cast<std::size_t>(nc));
  std::vector<Point3> du_row(static_cast<std::size_t>(nc));
  for (int j = 0; j < nc; ++j) {
    std::vector<Point3> col(static_cast<std::size_t>(nr));
    for (int i = 0; i < nr; ++i) {
      col[static_cast<std::size_t>(i)] =
          poles[static_cast<std::size_t>(i * nc + j)];
    }
    q_row[static_cast<std::size_t>(j)] = de_casteljau_point(col, cu);
    const Vec3 du_c = bezier_first_derivative(col, cu);
    du_row[static_cast<std::size_t>(j)] = Point3{du_c.x, du_c.y, du_c.z};
  }
  out_p = de_casteljau_point(q_row, cv);
  out_dv = bezier_first_derivative(
      std::span<const Point3>(q_row.data(), q_row.size()), cv);
  const Point3 du_pt = de_casteljau_point(du_row, cv);
  out_du = Vec3{du_pt.x, du_pt.y, du_pt.z};
  return true;
}

bool bezier_tensor_surface_eval_pos(const detail::SurfaceRecord &surface, Scalar u, Scalar v,
                                    Point3 &out_p) {
  Vec3 du{};
  Vec3 dv{};
  return bezier_tensor_surface_eval_with_partials(surface, u, v, out_p, du, dv);
}

Point3 evaluate_surface_point(const detail::SurfaceRecord &surface, Scalar u,
                              Scalar v) {
  if (surface.poles.empty()) {
    return surface.origin;
  }
  const auto [rows, cols] = infer_surface_grid_dims(surface.poles.size());
  if (rows == 0 || cols == 0) {
    return surface.origin;
  }
  if (rows == 1 || cols == 1) {
    return evaluate_polyline_point(surface.poles, cols == 1 ? u : v);
  }
  if (surface.kind == detail::SurfaceKind::Bezier) {
    Point3 tp{};
    if (bezier_tensor_surface_eval_pos(surface, u, v, tp)) {
      return tp;
    }
    return surface.origin;
  }
  if (surface.kind == detail::SurfaceKind::BSpline ||
      surface.kind == detail::SurfaceKind::Nurbs) {
    Point3 tensor_p{};
    if (nurbs_tensor_surface_eval_pos(surface, u, v, tensor_p)) {
      return tensor_p;
    }
  }
  const auto domain = surface_domain(surface);
  const auto cu = std::clamp(u, domain.u.min, domain.u.max);
  const auto cv = std::clamp(v, domain.v.min, domain.v.max);
  const auto ru =
      std::min<std::size_t>(static_cast<std::size_t>(std::floor(cu)), rows - 2);
  const auto rv =
      std::min<std::size_t>(static_cast<std::size_t>(std::floor(cv)), cols - 2);
  const auto lu = cu - static_cast<Scalar>(ru);
  const auto lv = cv - static_cast<Scalar>(rv);
  const auto idx = [cols](std::size_t r, std::size_t c) { return r * cols + c; };
  const auto &p00 = surface.poles[idx(ru, rv)];
  const auto &p01 = surface.poles[idx(ru, rv + 1)];
  const auto &p10 = surface.poles[idx(ru + 1, rv)];
  const auto &p11 = surface.poles[idx(ru + 1, rv + 1)];
  if (surface.kind == detail::SurfaceKind::Nurbs &&
      surface.weights.size() == surface.poles.size()) {
    return rational_bilinear_point(
        p00, p01, p10, p11, surface.weights[idx(ru, rv)],
        surface.weights[idx(ru, rv + 1)], surface.weights[idx(ru + 1, rv)],
        surface.weights[idx(ru + 1, rv + 1)], lu, lv);
  }
  return bilinear_point(p00, p01, p10, p11, lu, lv);
}

std::pair<Scalar, Scalar> approximate_surface_uv(
    const detail::SurfaceRecord &surface, const Point3 &point) {
  const auto domain = surface_domain(surface);
  const int samples = 24;
  Scalar best_u = domain.u.min;
  Scalar best_v = domain.v.min;
  Scalar best_distance = std::numeric_limits<Scalar>::max();
  for (int ui = 0; ui <= samples; ++ui) {
    const auto au = static_cast<Scalar>(ui) / static_cast<Scalar>(samples);
    const auto u = domain.u.min + (domain.u.max - domain.u.min) * au;
    for (int vi = 0; vi <= samples; ++vi) {
      const auto av = static_cast<Scalar>(vi) / static_cast<Scalar>(samples);
      const auto v = domain.v.min + (domain.v.max - domain.v.min) * av;
      const auto sample = evaluate_surface_point(surface, u, v);
      const auto distance = detail::norm(detail::subtract(point, sample));
      if (distance < best_distance) {
        best_distance = distance;
        best_u = u;
        best_v = v;
      }
    }
  }
  return {best_u, best_v};
}

// Principal curvatures from first/second fundamental forms: W = I^{-1} II, k1/k2 = eig(W).
bool principal_curvatures_from_fundamental(Scalar E, Scalar F, Scalar G, Scalar L,
                                           Scalar M_coef, Scalar N, Scalar &k1,
                                           Scalar &k2) {
  k1 = k2 = 0.0;
  const Scalar det_I = E * G - F * F;
  if (!(det_I > 1e-30) || !std::isfinite(det_I)) {
    return false;
  }
  const Scalar inv00 = G / det_I;
  const Scalar inv01 = -F / det_I;
  const Scalar inv10 = -F / det_I;
  const Scalar inv11 = E / det_I;
  const Scalar w00 = inv00 * L + inv01 * M_coef;
  const Scalar w01 = inv00 * M_coef + inv01 * N;
  const Scalar w10 = inv10 * L + inv11 * M_coef;
  const Scalar w11 = inv10 * M_coef + inv11 * N;
  const Scalar trace = w00 + w11;
  const Scalar detW = w00 * w11 - w01 * w10;
  const Scalar half_trace = trace * 0.5;
  const Scalar disc = half_trace * half_trace - detW;
  const Scalar sdisc = disc > 0.0 ? std::sqrt(disc) : 0.0;
  k1 = half_trace + sdisc;
  k2 = half_trace - sdisc;
  return std::isfinite(k1) && std::isfinite(k2);
}

// 非有理双线性片元：Suu=Svv=0，Suv 为常向量 → 主曲率可由第一/第二基本形式闭式得到（无位置 FD）。
void bilinear_patch_principal_curvatures_non_rational(const Point3 &p00,
                                                      const Point3 &p01,
                                                      const Point3 &p10,
                                                      const Point3 &p11,
                                                      Scalar lu, Scalar lv,
                                                      Scalar &k1, Scalar &k2) {
  k1 = k2 = 0.0;
  Point3 patch_p{};
  Vec3 ru{};
  Vec3 rv{};
  bilinear_patch_partials_non_rational(p00, p01, p10, p11, lu, lv, patch_p, ru, rv);
  const auto n_vec = detail::normalize(detail::cross(ru, rv));
  if (!std::isfinite(n_vec.x) || !std::isfinite(n_vec.y) ||
      !std::isfinite(n_vec.z)) {
    return;
  }
  const Vec3 ruv{-p00.x + p01.x + p10.x - p11.x, -p00.y + p01.y + p10.y - p11.y,
                 -p00.z + p01.z + p10.z - p11.z};
  const Scalar E = detail::dot(ru, ru);
  const Scalar Fp = detail::dot(ru, rv);
  const Scalar G = detail::dot(rv, rv);
  const Scalar L = 0.0;
  const Scalar M_coef = detail::dot(n_vec, ruv);
  const Scalar Nn = 0.0;
  principal_curvatures_from_fundamental(E, Fp, G, L, M_coef, Nn, k1, k2);
}

// Central FD on a parametric surface S(u,v) clamped to `domain`, then first/second fundamental
// forms → principal curvatures. Used for tensor patches and for revolved/swept/offset embeddings.
void parametric_surface_principal_curvature_fd(
    const std::function<Point3(Scalar, Scalar)> &S_in, Scalar u, Scalar v,
    const Range2D &domain, Scalar &k1, Scalar &k2) {
  k1 = k2 = 0.0;
  const auto umin = domain.u.min;
  const auto umax = domain.u.max;
  const auto vmin = domain.v.min;
  const auto vmax = domain.v.max;
  const Scalar span_u = umax - umin;
  const Scalar span_v = vmax - vmin;
  if (!(span_u > 1e-18) || !(span_v > 1e-18)) {
    return;
  }
  Scalar hu = std::max(span_u * 1e-4, 1e-6);
  Scalar hv = std::max(span_v * 1e-4, 1e-6);
  hu = std::min(hu, span_u * 0.25);
  hv = std::min(hv, span_v * 0.25);

  const auto S = [&S_in, umin, umax, vmin, vmax](Scalar uu, Scalar vv) -> Point3 {
    return S_in(std::clamp(uu, umin, umax), std::clamp(vv, vmin, vmax));
  };

  const Scalar cu = std::clamp(u, umin, umax);
  const Scalar cv = std::clamp(v, vmin, vmax);

  const Point3 p = S(cu, cv);
  const Point3 pu_p = S(cu + hu, cv);
  const Point3 pu_m = S(cu - hu, cv);
  const Point3 pv_p = S(cu, cv + hv);
  const Point3 pv_m = S(cu, cv - hv);

  const Vec3 ru = detail::scale(detail::subtract(pu_p, pu_m), 1.0 / (2.0 * hu));
  const Vec3 rv = detail::scale(detail::subtract(pv_p, pv_m), 1.0 / (2.0 * hv));
  Vec3 n_raw = detail::cross(ru, rv);
  const Scalar n_len = detail::norm(n_raw);
  if (!(n_len > 1e-18) || !std::isfinite(n_len)) {
    return;
  }
  const Vec3 n_vec = detail::scale(n_raw, 1.0 / n_len);

  const Vec3 ruu_vec{
      (pu_p.x - 2.0 * p.x + pu_m.x) / (hu * hu),
      (pu_p.y - 2.0 * p.y + pu_m.y) / (hu * hu),
      (pu_p.z - 2.0 * p.z + pu_m.z) / (hu * hu),
  };
  const Vec3 rvv_vec{
      (pv_p.x - 2.0 * p.x + pv_m.x) / (hv * hv),
      (pv_p.y - 2.0 * p.y + pv_m.y) / (hv * hv),
      (pv_p.z - 2.0 * p.z + pv_m.z) / (hv * hv),
  };
  const Point3 p_pp = S(cu + hu, cv + hv);
  const Point3 p_pm = S(cu + hu, cv - hv);
  const Point3 p_mp = S(cu - hu, cv + hv);
  const Point3 p_mm = S(cu - hu, cv - hv);
  const Vec3 ruv_vec{
      (p_pp.x - p_pm.x - p_mp.x + p_mm.x) / (4.0 * hu * hv),
      (p_pp.y - p_pm.y - p_mp.y + p_mm.y) / (4.0 * hu * hv),
      (p_pp.z - p_pm.z - p_mp.z + p_mm.z) / (4.0 * hu * hv),
  };

  const Scalar E = detail::dot(ru, ru);
  const Scalar Fp = detail::dot(ru, rv);
  const Scalar G = detail::dot(rv, rv);
  const Scalar L = detail::dot(ruu_vec, n_vec);
  const Scalar M_coef = detail::dot(ruv_vec, n_vec);
  const Scalar Nn = detail::dot(rvv_vec, n_vec);

  principal_curvatures_from_fundamental(E, Fp, G, L, M_coef, Nn, k1, k2);
}

// 解析切向 (ru,rv) + 位置的中心差分二阶导 → 第二基本形式；比全位置 FD 更稳定（退化法向单独处理）。
void parametric_surface_principal_curvature_hybrid(
    const std::function<Point3(Scalar, Scalar)> &S_in, Scalar u, Scalar v,
    const Range2D &domain, const Vec3 &ru, const Vec3 &rv, Scalar &k1,
    Scalar &k2) {
  k1 = k2 = 0.0;
  const auto umin = domain.u.min;
  const auto umax = domain.u.max;
  const auto vmin = domain.v.min;
  const auto vmax = domain.v.max;
  const Scalar span_u = umax - umin;
  const Scalar span_v = vmax - vmin;
  if (!(span_u > static_cast<Scalar>(1e-18)) ||
      !(span_v > static_cast<Scalar>(1e-18))) {
    return;
  }
  Scalar hu = std::max(span_u * static_cast<Scalar>(1e-4), static_cast<Scalar>(1e-6));
  Scalar hv = std::max(span_v * static_cast<Scalar>(1e-4), static_cast<Scalar>(1e-6));
  hu = std::min(hu, span_u * static_cast<Scalar>(0.25));
  hv = std::min(hv, span_v * static_cast<Scalar>(0.25));

  const auto S = [&S_in, umin, umax, vmin, vmax](Scalar uu, Scalar vv) -> Point3 {
    return S_in(std::clamp(uu, umin, umax), std::clamp(vv, vmin, vmax));
  };

  const Scalar cu = std::clamp(u, umin, umax);
  const Scalar cv = std::clamp(v, vmin, vmax);

  Vec3 n_raw = detail::cross(ru, rv);
  const Scalar n_len = detail::norm(n_raw);
  if (!(n_len > static_cast<Scalar>(1e-18)) || !std::isfinite(n_len)) {
    return;
  }
  const Vec3 n_vec = detail::scale(n_raw, static_cast<Scalar>(1.0) / n_len);

  const Point3 p = S(cu, cv);
  const Point3 pu_p = S(cu + hu, cv);
  const Point3 pu_m = S(cu - hu, cv);
  const Point3 pv_p = S(cu, cv + hv);
  const Point3 pv_m = S(cu, cv - hv);

  const Vec3 ruu_vec{
      (pu_p.x - static_cast<Scalar>(2.0) * p.x + pu_m.x) / (hu * hu),
      (pu_p.y - static_cast<Scalar>(2.0) * p.y + pu_m.y) / (hu * hu),
      (pu_p.z - static_cast<Scalar>(2.0) * p.z + pu_m.z) / (hu * hu),
  };
  const Vec3 rvv_vec{
      (pv_p.x - static_cast<Scalar>(2.0) * p.x + pv_m.x) / (hv * hv),
      (pv_p.y - static_cast<Scalar>(2.0) * p.y + pv_m.y) / (hv * hv),
      (pv_p.z - static_cast<Scalar>(2.0) * p.z + pv_m.z) / (hv * hv),
  };
  const Point3 p_pp = S(cu + hu, cv + hv);
  const Point3 p_pm = S(cu + hu, cv - hv);
  const Point3 p_mp = S(cu - hu, cv + hv);
  const Point3 p_mm = S(cu - hu, cv - hv);
  const Vec3 ruv_vec{
      (p_pp.x - p_pm.x - p_mp.x + p_mm.x) / (static_cast<Scalar>(4.0) * hu * hv),
      (p_pp.y - p_pm.y - p_mp.y + p_mm.y) / (static_cast<Scalar>(4.0) * hu * hv),
      (p_pp.z - p_pm.z - p_mp.z + p_mm.z) / (static_cast<Scalar>(4.0) * hu * hv),
  };

  const Scalar E = detail::dot(ru, ru);
  const Scalar Fp = detail::dot(ru, rv);
  const Scalar G = detail::dot(rv, rv);
  const Scalar L = detail::dot(ruu_vec, n_vec);
  const Scalar M_coef = detail::dot(ruv_vec, n_vec);
  const Scalar Nn = detail::dot(rvv_vec, n_vec);

  principal_curvatures_from_fundamental(E, Fp, G, L, M_coef, Nn, k1, k2);
}

// Engineering-grade principal curvatures for spline-like tensor patches (Bezier/BSpline/NURBS
// control-grid sampling): central FD on position, then I/II → Weingarten eigenvalues.
void spline_surface_principal_curvature_fd(const detail::SurfaceRecord &surface, Scalar u,
                                           Scalar v, const Range2D &domain, Scalar &k1,
                                           Scalar &k2) {
  k1 = k2 = 0.0;
  if (surface.poles.empty()) {
    return;
  }
  parametric_surface_principal_curvature_fd(
      [&surface](Scalar uu, Scalar vv) {
        return evaluate_surface_point(surface, uu, vv);
      },
      u, v, domain, k1, k2);
}

void tensor_spline_surface_principal_curvature_improved(
    const detail::SurfaceRecord &surface, Scalar u, Scalar v, const Range2D &domain,
    Scalar &k1, Scalar &k2) {
  Point3 p{};
  Vec3 du{};
  Vec3 dv{};
  bool ok = false;
  if (surface.kind == detail::SurfaceKind::BSpline ||
      surface.kind == detail::SurfaceKind::Nurbs) {
    ok = nurbs_tensor_surface_eval_with_partials(surface, u, v, p, du, dv);
  } else if (surface.kind == detail::SurfaceKind::Bezier) {
    ok = bezier_tensor_surface_eval_with_partials(surface, u, v, p, du, dv);
  }
  if (!ok) {
    spline_surface_principal_curvature_fd(surface, u, v, domain, k1, k2);
    return;
  }
  if (!(detail::norm(detail::cross(du, dv)) > static_cast<Scalar>(1e-20))) {
    k1 = k2 = 0.0;
    return;
  }
  parametric_surface_principal_curvature_hybrid(
      [&surface](Scalar uu, Scalar vv) {
        return evaluate_surface_point(surface, uu, vv);
      },
      u, v, domain, du, dv, k1, k2);
}



// 1D 最近参数阻尼 GN 需要 ∂C/∂t 的**真实速度**；`tangent` 常为归一化方向，CompositeChain 等会错标度步长。
Vec3 curve_velocity_for_closest(const CurveEvalResult &r) {
  if (!r.derivatives.empty()) {
    const auto &d0 = r.derivatives[0];
    if (std::isfinite(d0.x) && std::isfinite(d0.y) && std::isfinite(d0.z) &&
        detail::dot(d0, d0) > 1e-36) {
      return d0;
    }
  }
  return r.tangent;
}

BoundingBox make_curve_bbox(const detail::CurveRecord &curve) {
  switch (curve.kind) {
  case detail::CurveKind::Line:
    return detail::bbox_from_center_radius(curve.origin, 1.0, 1.0, 1.0);
  case detail::CurveKind::LineSegment: {
    if (curve.poles.size() >= 2) {
      const auto &a = curve.poles.front();
      const auto &b = curve.poles.back();
      const Point3 min{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
      const Point3 max{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
      return detail::make_bbox(min, max);
    }
    return detail::bbox_from_center_radius(curve.origin, 1.0, 1.0, 1.0);
  }
  case detail::CurveKind::Circle:
    return bbox_from_span_vectors(
        curve.origin, detail::scale(curve.axis_u, curve.radius),
        detail::scale(curve.axis_v, curve.radius));
  case detail::CurveKind::Ellipse:
    return bbox_from_span_vectors(curve.origin, curve.axis_u, curve.axis_v);
  case detail::CurveKind::Parabola:
  case detail::CurveKind::Hyperbola:
    return detail::bbox_from_center_radius(curve.origin, 1000.0, 1000.0, 1000.0);
  case detail::CurveKind::Bezier:
  case detail::CurveKind::BSpline:
  case detail::CurveKind::Nurbs:
  case detail::CurveKind::CompositePolyline: {
    if (curve.poles.empty()) {
      return detail::bbox_from_center_radius(curve.origin, 1.0, 1.0, 1.0);
    }
    auto min = curve.poles.front();
    auto max = curve.poles.front();
    for (const auto &p : curve.poles) {
      min.x = std::min(min.x, p.x);
      min.y = std::min(min.y, p.y);
      min.z = std::min(min.z, p.z);
      max.x = std::max(max.x, p.x);
      max.y = std::max(max.y, p.y);
      max.z = std::max(max.z, p.z);
    }
    return detail::make_bbox(min, max);
  }
  default:
    return detail::bbox_from_center_radius(curve.origin, 1.0, 1.0, 1.0);
  }
}

BoundingBox make_surface_bbox(const detail::SurfaceRecord &surface) {
  switch (surface.kind) {
  case detail::SurfaceKind::Plane:
    return detail::bbox_from_center_radius(surface.origin, 1000.0, 1000.0, 0.0);
  case detail::SurfaceKind::Sphere:
    return detail::bbox_from_center_radius(surface.origin, surface.radius_a,
                                           surface.radius_a, surface.radius_a);
  case detail::SurfaceKind::Cylinder:
    return detail::bbox_from_center_radius(surface.origin, surface.radius_a,
                                           surface.radius_a, 1000.0);
  case detail::SurfaceKind::Cone:
    return detail::bbox_from_center_radius(surface.origin, 1000.0, 1000.0, 1000.0);
  case detail::SurfaceKind::Torus:
    return detail::bbox_from_center_radius(surface.origin, surface.radius_a + surface.radius_b,
                                           surface.radius_a + surface.radius_b, surface.radius_b);
  case detail::SurfaceKind::BSpline:
  case detail::SurfaceKind::Nurbs: {
    if (surface.poles.empty()) {
      return detail::bbox_from_center_radius(surface.origin, 10.0, 10.0, 10.0);
    }
    auto min = surface.poles.front();
    auto max = surface.poles.front();
    for (const auto &p : surface.poles) {
      min.x = std::min(min.x, p.x);
      min.y = std::min(min.y, p.y);
      min.z = std::min(min.z, p.z);
      max.x = std::max(max.x, p.x);
      max.y = std::max(max.y, p.y);
      max.z = std::max(max.z, p.z);
    }
    return detail::make_bbox(min, max);
  }
  case detail::SurfaceKind::Bezier: {
    if (surface.poles.empty()) {
      return detail::bbox_from_center_radius(surface.origin, 10.0, 10.0, 10.0);
    }
    auto min = surface.poles.front();
    auto max = surface.poles.front();
    for (const auto &p : surface.poles) {
      min.x = std::min(min.x, p.x);
      min.y = std::min(min.y, p.y);
      min.z = std::min(min.z, p.z);
      max.x = std::max(max.x, p.x);
      max.y = std::max(max.y, p.y);
      max.z = std::max(max.z, p.z);
    }
    return detail::make_bbox(min, max);
  }
  case detail::SurfaceKind::Revolved:
  case detail::SurfaceKind::Swept:
  case detail::SurfaceKind::Trimmed:
  case detail::SurfaceKind::Offset:
    return detail::bbox_from_center_radius(surface.origin, 50.0, 50.0, 50.0);
  default:
    return detail::bbox_from_center_radius(surface.origin, 10.0, 10.0, 10.0);
  }
}

detail::CurveRecord make_curve_record(detail::CurveKind kind,
                                      const Point3 &origin,
                                      const Vec3 &direction, Scalar radius,
                                      const Vec3 &normal, const Vec3 &axis_u,
                                      const Vec3 &axis_v,
                                      std::vector<Point3> poles = {},
                                      std::vector<Scalar> weights = {},
                                      std::vector<Scalar> knots_u = {},
                                      Scalar param_a = 0.0,
                                      Scalar param_b = 0.0,
                                      std::vector<CurveId> children = {}) {
  detail::CurveRecord record;
  record.kind = kind;
  record.origin = origin;
  record.direction = direction;
  record.radius = radius;
  record.param_a = param_a;
  record.param_b = param_b;
  record.normal = normal;
  record.axis_u = axis_u;
  record.axis_v = axis_v;
  record.poles = std::move(poles);
  record.weights = std::move(weights);
  record.knots_u = std::move(knots_u);
  record.children = std::move(children);
  return record;
}

detail::SurfaceRecord make_surface_record(detail::SurfaceKind kind,
                                          const Point3 &origin,
                                          const Vec3 &axis,
                                          const Vec3 &normal,
                                          Scalar radius_a, Scalar radius_b,
                                          Scalar semi_angle,
                                          std::vector<Point3> poles = {},
                                          std::vector<Scalar> weights = {},
                                          std::vector<Scalar> knots_u = {},
                                          std::vector<Scalar> knots_v = {},
                                          SurfaceId base_surface_id = {},
                                          CurveId profile_curve_id = {},
                                          Scalar sweep_angle_rad = 0.0,
                                          Scalar sweep_length = 0.0,
                                          Scalar trim_u_min = 0.0,
                                          Scalar trim_u_max = 1.0,
                                          Scalar trim_v_min = 0.0,
                                          Scalar trim_v_max = 1.0,
                                          std::vector<Point2> trim_uv_loop = {},
                                          Scalar offset_distance = 0.0,
                                          int spline_degree_u = -1,
                                          int spline_degree_v = -1) {
  detail::SurfaceRecord record;
  record.kind = kind;
  record.origin = origin;
  record.axis = axis;
  record.normal = normal;
  record.radius_a = radius_a;
  record.radius_b = radius_b;
  record.semi_angle = semi_angle;
  record.base_surface_id = base_surface_id;
  record.profile_curve_id = profile_curve_id;
  record.sweep_angle_rad = sweep_angle_rad;
  record.sweep_length = sweep_length;
  record.trim_u_min = trim_u_min;
  record.trim_u_max = trim_u_max;
  record.trim_v_min = trim_v_min;
  record.trim_v_max = trim_v_max;
  record.trim_uv_loop = std::move(trim_uv_loop);
  record.offset_distance = offset_distance;
  record.poles = std::move(poles);
  record.weights = std::move(weights);
  record.knots_u = std::move(knots_u);
  record.knots_v = std::move(knots_v);
  record.spline_degree_u = spline_degree_u;
  record.spline_degree_v = spline_degree_v;
  return record;
}

