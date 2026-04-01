#pragma once

#include <cstddef>
#include <string>

#include "axiom/internal/core/kernel_state.h"

namespace axiom::detail {

enum class MeshSelfIntersectionStatus {
    Ok,
    Hit,
    TooManyTriangles,
    InvalidMesh,
};

struct MeshSelfIntersectionResult {
    MeshSelfIntersectionStatus status{MeshSelfIntersectionStatus::Ok};
    std::size_t tri_i{};
    std::size_t tri_j{};
};

// 基于三角网格的保守自交检测：SAT + AABB 预筛；跳过共享 ≥2 顶点索引的三角形对（焊接后的共边）。
MeshSelfIntersectionResult analyze_mesh_self_intersection(const MeshRecord& mesh,
                                                            Scalar linear_tolerance,
                                                            std::size_t max_triangles);

}  // namespace axiom::detail
