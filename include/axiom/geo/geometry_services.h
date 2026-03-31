#pragma once

#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
}

class CurveFactory {
public:
    explicit CurveFactory(std::shared_ptr<detail::KernelState> state);

    Result<CurveId> make_line(const Point3& origin, const Vec3& direction);
    Result<CurveId> make_line_segment(const Point3& a, const Point3& b);
    Result<CurveId> make_circle(const Point3& center, const Vec3& normal, Scalar radius);
    Result<CurveId> make_ellipse(const Point3& center, const Vec3& axis_u, const Vec3& axis_v);
    Result<CurveId> make_parabola(const Point3& origin, const Vec3& axis_u, const Vec3& axis_v, Scalar focal_param);
    Result<CurveId> make_hyperbola(const Point3& origin, const Vec3& axis_u, const Vec3& axis_v, Scalar a, Scalar b);
    Result<CurveId> make_bezier(std::span<const Point3> poles);
    Result<CurveId> make_bspline(const BSplineCurveDesc& desc);
    Result<CurveId> make_nurbs(const NURBSCurveDesc& desc);
    Result<CurveId> make_composite_polyline(std::span<const Point3> poles);
    // Composite curve chain (Stage 2 minimal): concatenate child curves into a single parameter domain [0, n].
    // Each child occupies one unit interval; evaluation maps t -> (child_index, local_t).
    Result<CurveId> make_composite_chain(std::span<const CurveId> children);

private:
    std::shared_ptr<detail::KernelState> state_;
};

class PCurveFactory {
public:
    explicit PCurveFactory(std::shared_ptr<detail::KernelState> state);

    // Minimal pcurve support for Stage 2: polyline in UV space.
    Result<PCurveId> make_polyline(std::span<const Point2> poles);

private:
    std::shared_ptr<detail::KernelState> state_;
};

class SurfaceFactory {
public:
    explicit SurfaceFactory(std::shared_ptr<detail::KernelState> state);

    Result<SurfaceId> make_plane(const Point3& origin, const Vec3& normal);
    Result<SurfaceId> make_cylinder(const Point3& origin, const Vec3& axis, Scalar radius);
    Result<SurfaceId> make_cone(const Point3& apex, const Vec3& axis, Scalar semi_angle);
    Result<SurfaceId> make_sphere(const Point3& center, Scalar radius);
    Result<SurfaceId> make_torus(const Point3& center, const Vec3& axis, Scalar major_r, Scalar minor_r);
    Result<SurfaceId> make_bezier(std::span<const Point3> poles);
    Result<SurfaceId> make_bspline(const BSplineSurfaceDesc& desc);
    Result<SurfaceId> make_nurbs(const NURBSSurfaceDesc& desc);
    // Stage 2 minimal: surfaces required by docs (7.1).
    Result<SurfaceId> make_revolved(CurveId generatrix, const Axis3& axis, Scalar sweep_angle_radians);
    Result<SurfaceId> make_swept_linear(CurveId profile, const Vec3& direction, Scalar sweep_length);
    Result<SurfaceId> make_trimmed(SurfaceId base_surface, Scalar u_min, Scalar u_max, Scalar v_min, Scalar v_max);
    Result<SurfaceId> make_offset(SurfaceId base_surface, Scalar offset_distance);

private:
    std::shared_ptr<detail::KernelState> state_;
};

class CurveService {
public:
    explicit CurveService(std::shared_ptr<detail::KernelState> state);

    Result<CurveEvalResult> eval(CurveId curve_id, Scalar t, int deriv_order) const;
    Result<std::vector<CurveEvalResult>> eval_batch(CurveId curve_id, std::span<const Scalar> ts, int deriv_order) const;
    Result<Scalar> closest_parameter(CurveId curve_id, const Point3& point) const;
    Result<std::vector<Scalar>> closest_parameters_batch(CurveId curve_id, std::span<const Point3> points) const;
    Result<Point3> closest_point(CurveId curve_id, const Point3& point) const;
    Result<std::vector<Point3>> closest_points_batch(CurveId curve_id, std::span<const Point3> points) const;
    Result<Range1D> domain(CurveId curve_id) const;
    Result<BoundingBox> bbox(CurveId curve_id) const;
    Result<std::vector<BoundingBox>> bbox_batch(std::span<const CurveId> curve_ids) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class PCurveService {
public:
    explicit PCurveService(std::shared_ptr<detail::KernelState> state);

    Result<PCurveEvalResult> eval(PCurveId pcurve_id, Scalar t, int deriv_order) const;
    Result<Scalar> closest_parameter(PCurveId pcurve_id, const Point2& point) const;
    Result<Point2> closest_point(PCurveId pcurve_id, const Point2& point) const;
    Result<Range1D> domain(PCurveId pcurve_id) const;
    Result<BoundingBox> bbox(PCurveId pcurve_id) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class SurfaceService {
public:
    explicit SurfaceService(std::shared_ptr<detail::KernelState> state);

    Result<SurfaceEvalResult> eval(SurfaceId surface_id, Scalar u, Scalar v, int deriv_order) const;
    Result<std::vector<SurfaceEvalResult>> eval_batch(
        SurfaceId surface_id, std::span<const std::pair<Scalar, Scalar>> uvs, int deriv_order) const;
    Result<Point3> closest_point(SurfaceId surface_id, const Point3& point) const;
    Result<std::vector<Point3>> closest_points_batch(SurfaceId surface_id, std::span<const Point3> points) const;
    Result<std::pair<Scalar, Scalar>> closest_uv(SurfaceId surface_id, const Point3& point) const;
    Result<std::vector<std::pair<Scalar, Scalar>>> closest_uv_batch(
        SurfaceId surface_id, std::span<const Point3> points) const;
    Result<Range2D> domain(SurfaceId surface_id) const;
    Result<BoundingBox> bbox(SurfaceId surface_id) const;
    Result<std::vector<BoundingBox>> bbox_batch(std::span<const SurfaceId> surface_ids) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class GeometryTransformService {
public:
    explicit GeometryTransformService(std::shared_ptr<detail::KernelState> state);

    Result<CurveId> transform_curve(CurveId curve_id, const Transform3& transform);
    Result<SurfaceId> transform_surface(SurfaceId surface_id, const Transform3& transform);

private:
    std::shared_ptr<detail::KernelState> state_;
};

struct CurveSurfaceIntersection {
    Point3 point{};
    Scalar curve_t{0.0};
    Scalar surface_u{0.0};
    Scalar surface_v{0.0};
};

class GeometryIntersectionService {
public:
    explicit GeometryIntersectionService(std::shared_ptr<detail::KernelState> state);

    // Minimal intersection service for Stage 2/3: analytic pairs first (Line/Segment/Circle with Plane/Sphere/Cylinder).
    Result<std::vector<CurveSurfaceIntersection>> intersect_curve_surface(CurveId curve_id, SurfaceId surface_id) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

}  // namespace axiom
