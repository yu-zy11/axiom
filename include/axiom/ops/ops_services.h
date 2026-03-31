#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
}

class PrimitiveService {
public:
    explicit PrimitiveService(std::shared_ptr<detail::KernelState> state);

    Result<BodyId> box(const Point3& origin, Scalar dx, Scalar dy, Scalar dz);
    Result<BodyId> sphere(const Point3& center, Scalar radius);
    Result<BodyId> cylinder(const Point3& center, const Vec3& axis, Scalar radius, Scalar height);
    Result<BodyId> cone(const Point3& apex, const Vec3& axis, Scalar semi_angle, Scalar height);
    Result<BodyId> torus(const Point3& center, const Vec3& axis, Scalar major_r, Scalar minor_r);

private:
    std::shared_ptr<detail::KernelState> state_;
};

class SweepService {
public:
    explicit SweepService(std::shared_ptr<detail::KernelState> state);

    Result<BodyId> extrude(const ProfileRef& profile, const Vec3& direction, Scalar distance);
    Result<BodyId> revolve(const ProfileRef& profile, const Axis3& axis, Scalar angle);
    Result<BodyId> sweep(const ProfileRef& profile, CurveId rail);
    Result<BodyId> loft(std::span<const ProfileRef> profiles);
    Result<BodyId> thicken(FaceId face_id, Scalar distance);

private:
    std::shared_ptr<detail::KernelState> state_;
};

class BooleanService {
public:
    explicit BooleanService(std::shared_ptr<detail::KernelState> state);

    Result<OpReport> run(BooleanOp op, BodyId lhs, BodyId rhs, const BooleanOptions& options);
    Result<void> export_boolean_prep_stats(BodyId lhs, BodyId rhs, std::string_view path) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

class ModifyService {
public:
    explicit ModifyService(std::shared_ptr<detail::KernelState> state);

    Result<OpReport> offset_body(BodyId body_id, Scalar distance, const TolerancePolicy& tolerance);
    Result<OpReport> shell_body(BodyId body_id, std::span<const FaceId> removed_faces, Scalar thickness);
    Result<OpReport> draft_faces(BodyId body_id, std::span<const FaceId> faces, const Vec3& pull_dir, Scalar angle);
    Result<OpReport> replace_face(BodyId body_id, FaceId target, SurfaceId replacement);
    Result<OpReport> delete_face_and_heal(BodyId body_id, FaceId target);

private:
    std::shared_ptr<detail::KernelState> state_;
};

class BlendService {
public:
    explicit BlendService(std::shared_ptr<detail::KernelState> state);

    Result<OpReport> fillet_edges(BodyId body_id, std::span<const EdgeId> edges, Scalar radius);
    Result<OpReport> chamfer_edges(BodyId body_id, std::span<const EdgeId> edges, Scalar distance);

private:
    std::shared_ptr<detail::KernelState> state_;
};

class QueryService {
public:
    explicit QueryService(std::shared_ptr<detail::KernelState> state);

    Result<IntersectionId> intersect(CurveId curve_id, SurfaceId surface_id) const;
    Result<IntersectionId> intersect(SurfaceId lhs, SurfaceId rhs) const;
    Result<MeshId> section(BodyId body_id, const Plane& plane) const;
    Result<MassProperties> mass_properties(BodyId body_id) const;
    Result<Scalar> min_distance(BodyId lhs, BodyId rhs) const;

private:
    std::shared_ptr<detail::KernelState> state_;
};

}  // namespace axiom
