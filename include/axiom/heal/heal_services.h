#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
}

class ValidationService {
public:
  explicit ValidationService(std::shared_ptr<detail::KernelState> state);

  Result<void> validate_geometry(BodyId body_id, ValidationMode mode) const;
  Result<void> validate_topology(BodyId body_id, ValidationMode mode) const;
  /// 专项：owned B-Rep 壳的边级闭合/流形（两侧各一拓扑面；Standard/Fast 即检；Strict 额外壳内重复面与不连通）。
  Result<void> validate_manifold(BodyId body_id, ValidationMode mode) const;
  Result<void> validate_self_intersection(BodyId body_id,
                                          ValidationMode mode) const;
  Result<void> validate_tolerance(BodyId body_id, ValidationMode mode) const;
  Result<void> validate_tolerance_many(std::span<const BodyId> body_ids,
                                       ValidationMode mode) const;
  Result<void> validate_all(BodyId body_id, ValidationMode mode) const;
  Result<void> validate_manifold_many(std::span<const BodyId> body_ids,
                                      ValidationMode mode) const;
  Result<void> validate_self_intersection_many(std::span<const BodyId> body_ids,
                                               ValidationMode mode) const;
  Result<void> validate_geometry_many(std::span<const BodyId> body_ids,
                                      ValidationMode mode) const;
  Result<void> validate_topology_many(std::span<const BodyId> body_ids,
                                      ValidationMode mode) const;
  Result<void> validate_all_many(std::span<const BodyId> body_ids,
                                 ValidationMode mode) const;
  Result<bool> is_geometry_valid(BodyId body_id, ValidationMode mode) const;
  Result<bool> is_topology_valid(BodyId body_id, ValidationMode mode) const;
  Result<bool> is_valid(BodyId body_id, ValidationMode mode) const;
  Result<void> validate_bbox(BodyId body_id) const;
  Result<void> validate_bbox_many(std::span<const BodyId> body_ids) const;
  Result<std::uint64_t> count_invalid_in(std::span<const BodyId> body_ids,
                                         ValidationMode mode) const;
  Result<BodyId> first_invalid_in(std::span<const BodyId> body_ids,
                                  ValidationMode mode) const;
  Result<std::vector<BodyId>>
  filter_valid_bodies(std::span<const BodyId> body_ids,
                      ValidationMode mode) const;
  Result<std::vector<BodyId>>
  filter_invalid_bodies(std::span<const BodyId> body_ids,
                        ValidationMode mode) const;

private:
  std::shared_ptr<detail::KernelState> state_;
};

class RepairService {
public:
  explicit RepairService(std::shared_ptr<detail::KernelState> state);

  // Trim bridge (Stage 3 minimal): rebuild / overwrite coedge pcurves from 3D edges by projection onto the face surface.
  // Currently supports Plane faces (SurfaceKind::Plane).
  Result<void> repair_face_trim_pcurves(FaceId face_id, RepairMode mode);

  Result<OpReport> sew_faces(std::span<const FaceId> faces, Scalar tolerance,
                             RepairMode mode);
  Result<OpReport> remove_small_edges(BodyId body_id, Scalar threshold,
                                      RepairMode mode);
  Result<OpReport> remove_small_faces(BodyId body_id, Scalar threshold,
                                      RepairMode mode);
  Result<OpReport> merge_near_coplanar_faces(BodyId body_id,
                                             Scalar angle_tolerance,
                                             RepairMode mode);
  Result<OpReport> auto_repair(BodyId body_id, RepairMode mode);
  Result<Scalar> estimate_adaptive_linear_threshold(BodyId body_id,
                                                    Scalar input,
                                                    RepairMode mode) const;
  Result<Scalar> estimate_adaptive_angle_threshold(BodyId body_id, Scalar input,
                                                   RepairMode mode) const;
  Result<OpReport> sew_faces_default(std::span<const FaceId> faces);
  Result<OpReport> remove_small_edges_default(BodyId body_id, Scalar threshold);
  Result<OpReport> remove_small_faces_default(BodyId body_id, Scalar threshold);
  Result<OpReport> merge_near_coplanar_faces_default(BodyId body_id,
                                                     Scalar angle_tolerance);
  Result<OpReport> auto_repair_default(BodyId body_id);
  Result<std::vector<OpReport>>
  repair_many_auto(std::span<const BodyId> body_ids, RepairMode mode);
  Result<std::vector<OpReport>>
  repair_many_remove_small_edges(std::span<const BodyId> body_ids,
                                 Scalar threshold, RepairMode mode);
  Result<std::vector<OpReport>>
  repair_many_remove_small_faces(std::span<const BodyId> body_ids,
                                 Scalar threshold, RepairMode mode);
  Result<std::vector<OpReport>>
  repair_many_merge_near_coplanar_faces(std::span<const BodyId> body_ids,
                                        Scalar angle_tolerance,
                                        RepairMode mode);
  Result<bool> was_modified_output(const OpReport &report) const;
  Result<bool> output_is_new_body(const OpReport &report, BodyId input) const;
  Result<Scalar> body_bbox_shrink_ratio(BodyId before, BodyId after) const;
  Result<Scalar> compare_bbox_extent_change(BodyId before, BodyId after) const;
  Result<void> ensure_valid_after_repair(const OpReport &report,
                                         ValidationMode mode) const;
  Result<std::string> summarize_repair(const OpReport &report) const;

private:
  std::shared_ptr<detail::KernelState> state_;
};

} // namespace axiom
