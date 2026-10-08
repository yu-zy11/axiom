#include "axiom/ops/ops_services.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "axiom/diag/error_codes.h"
#include "axiom/geo/geometry_services.h"
#include "axiom/heal/heal_services.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/core/topology_materialization.h"
#include "axiom/internal/ops/ops_service_internal.h"
#include "axiom/topo/topology_service.h"

namespace axiom {
namespace {

// Only new objects are modified by this service. Diagnostics and monotonically
// increasing IDs deliberately survive a failure; model objects and caches do not.
class BooleanRebuildRollback {
public:
    explicit BooleanRebuildRollback(detail::KernelState& state)
        : state_(state), first_id_(state.next_id),
          curve_cache_(state.curve_eval_cache), surface_cache_(state.surface_eval_cache),
          mesh_cache_(state.tessellation_cache), face_mesh_cache_(state.face_tessellation_cache),
          cache_stats_(state.tessellation_cache_stats), eval_invalid_(state.eval_invalid),
          eval_bridge_(state.eval_invalidation_bridge), eval_telemetry_(state.eval_telemetry) {}

    ~BooleanRebuildRollback() {
        if (!completed_) restore();
    }

    void restore() {
        const auto erase_new = [this](auto& objects) {
            for (auto it = objects.begin(); it != objects.end();) {
                if (it->first >= first_id_) it = objects.erase(it);
                else ++it;
            }
        };
        erase_new(state_.curves);
        erase_new(state_.pcurves);
        erase_new(state_.surfaces);
        erase_new(state_.vertices);
        erase_new(state_.edges);
        erase_new(state_.coedges);
        erase_new(state_.loops);
        erase_new(state_.faces);
        erase_new(state_.shells);
        erase_new(state_.bodies);
        erase_new(state_.meshes);
        erase_new(state_.intersections);
        state_.curve_eval_cache = curve_cache_;
        state_.surface_eval_cache = surface_cache_;
        state_.tessellation_cache = mesh_cache_;
        state_.face_tessellation_cache = face_mesh_cache_;
        state_.tessellation_cache_stats = cache_stats_;
        state_.eval_invalid = eval_invalid_;
        state_.eval_invalidation_bridge = eval_bridge_;
        state_.eval_telemetry = eval_telemetry_;
        detail::rebuild_topology_links(state_);
    }

    std::uint64_t first_id() const { return first_id_; }
    void commit() { completed_ = true; }

private:
    detail::KernelState& state_;
    std::uint64_t first_id_;
    decltype(detail::KernelState::curve_eval_cache) curve_cache_;
    decltype(detail::KernelState::surface_eval_cache) surface_cache_;
    decltype(detail::KernelState::tessellation_cache) mesh_cache_;
    decltype(detail::KernelState::face_tessellation_cache) face_mesh_cache_;
    TessellationCacheStats cache_stats_;
    decltype(detail::KernelState::eval_invalid) eval_invalid_;
    EvalInvalidationBridgeMetrics eval_bridge_;
    EvalGraphTelemetry eval_telemetry_;
    bool completed_{false};
};

}  // namespace

Result<BooleanRebuildReport> BooleanService::run_rebuilt(
    BooleanOp op, BodyId lhs, BodyId rhs, const BooleanRebuildOptions& options) {
    const auto failure = [&](StatusCode status, std::string_view code, std::string message,
                             std::string_view stage, DiagnosticId cause = {}) {
        std::vector<Issue> issues;
        if (const auto previous = state_->diagnostics.find(cause.value);
            previous != state_->diagnostics.end()) issues = previous->second.issues;
        auto issue = detail::make_error_issue(code, std::move(message));
        issue.stage = std::string(stage);
        issue.related_entities = {lhs.value, rhs.value};
        issue.numeric_evidence = {{"operation", static_cast<Scalar>(op), "enum"},
                                  {"model_unchanged", 1.0, "bool"}};
        issues.push_back(std::move(issue));
        return error_result<BooleanRebuildReport>(status,
            state_->create_diagnostic("真实布尔重建失败", std::move(issues)));
    };
    if (op != BooleanOp::Union && op != BooleanOp::Subtract && op != BooleanOp::Intersect)
        return failure(StatusCode::InvalidInput, diag_codes::kBoolInvalidInput,
                       "不支持的布尔操作枚举", "bool.rebuild");

    const auto preparation = prepare_split_classification_impl(lhs, rhs, options.preparation, true);
    if (preparation.status != StatusCode::Ok)
        return error_result<BooleanRebuildReport>(preparation.status, preparation.diagnostic_id);
    const auto& fragments = preparation.value->fragments;
    const Scalar linear = options.preparation.intersection.tolerance.linear;
    TopologyValidationService topology_validation {state_};
    for (const auto source_body : std::array<BodyId, 2>{lhs, rhs}) {
        const auto closedness = topology_validation.validate_body_closedness(source_body);
        if (closedness.status != StatusCode::Ok)
            return failure(closedness.status, diag_codes::kBoolRebuildFailure,
                           "源实体各壳须为连通闭壳", "bool.rebuild", closedness.diagnostic_id);
    }

    // The read-only preparation uses shell parity for material, independently of
    // loop orientation. Rebuilding needs material-oriented loops as well. Check
    // one well-resolved representative per connected source shell; edge pairing
    // in preparation has already established a consistent shell orientation.
    std::map<std::uint64_t, std::size_t> shell_representatives;
    std::map<std::uint64_t, Scalar> shell_areas;
    for (std::size_t index = 0; index < fragments.size(); ++index) {
        const auto& fragment = fragments[index];
        const auto& body = state_->bodies.at(fragment.source_body.value);
        const auto area = detail::norm(detail::cross(
            detail::subtract(fragment.vertices[1], fragment.vertices[0]),
            detail::subtract(fragment.vertices[2], fragment.vertices[0])));
        for (const auto shell_id : body.shells) {
            const auto& shell = state_->shells.at(shell_id.value);
            if (std::find(shell.faces.begin(), shell.faces.end(), fragment.source_face) == shell.faces.end())
                continue;
            if (!shell_representatives.contains(shell_id.value) || area > shell_areas[shell_id.value]) {
                shell_representatives[shell_id.value] = index;
                shell_areas[shell_id.value] = area;
            }
        }
    }
    for (const auto& [shell_value, index] : shell_representatives) {
        (void)shell_value;
        const auto& fragment = fragments[index];
        const auto e0 = detail::subtract(fragment.vertices[1], fragment.vertices[0]);
        const auto e1 = detail::subtract(fragment.vertices[2], fragment.vertices[0]);
        const auto cross = detail::cross(e0, e1);
        const auto cross_length = detail::norm(cross);
        const auto edge_length = std::max({detail::norm(e0), detail::norm(e1),
            detail::norm(detail::subtract(fragment.vertices[2], fragment.vertices[1]))});
        if (!(cross_length > 0) || !(edge_length > 0))
            return failure(StatusCode::DegenerateGeometry, diag_codes::kBoolRebuildFailure,
                           "源壳方向参考面退化", "bool.rebuild");
        const auto normal = detail::scale(cross, 1.0 / cross_length);
        const Point3 center {(fragment.vertices[0].x + fragment.vertices[1].x + fragment.vertices[2].x) / 3,
                             (fragment.vertices[0].y + fragment.vertices[1].y + fragment.vertices[2].y) / 3,
                             (fragment.vertices[0].z + fragment.vertices[1].z + fragment.vertices[2].z) / 3};
        const Scalar distance = std::max(32 * linear, cross_length / edge_length * 0.001);
        const std::array<Point3, 2> probes {
            detail::add_point_vec(center, detail::scale(normal, -distance)),
            detail::add_point_vec(center, detail::scale(normal, distance))};
        const auto directions = classify_points(fragment.source_body, probes,
                                                 options.preparation.intersection);
        if (directions.status != StatusCode::Ok)
            return failure(directions.status, diag_codes::kBoolRebuildFailure,
                           "源壳材料方向无法确定", "bool.rebuild", directions.diagnostic_id);
        if ((*directions.value)[0].location != BooleanPointLocation::Inside ||
            (*directions.value)[1].location != BooleanPointLocation::Outside)
            return failure(StatusCode::InvalidTopology, diag_codes::kBoolRebuildFailure,
                           "源壳绕向须指向奇偶材料区域外部", "bool.rebuild");
    }

    struct SelectedFace {
        std::vector<FaceId> sources;
        std::array<Point3, 3> triangle{};
        std::array<int, 3> corners{};
        std::vector<int> ring;
        std::vector<std::vector<int>> inner_rings;
        std::vector<std::pair<int, bool>> edge_refs;
        std::vector<std::vector<std::pair<int, bool>>> inner_edge_refs;
    };
    std::vector<SelectedFace> selected;
    Scalar coordinate_scale = 1;
    std::vector<ops_internal::BooleanPlanarFace> probe_faces;
    std::size_t probe_cost = 0;
    const auto truth = [&](bool left, bool right) {
        return op == BooleanOp::Union ? left || right :
            op == BooleanOp::Intersect ? left && right : left && !right;
    };
    if (std::any_of(fragments.begin(),fragments.end(),[](const auto& fragment) {
        return fragment.classification.location == BooleanPointLocation::Boundary;
    })) {
        for (const auto source_body : std::array<BodyId,2>{lhs,rhs}) {
            for (const auto shell : state_->bodies.at(source_body.value).shells)
                for (const auto id : state_->shells.at(shell.value).faces) {
                    ops_internal::BooleanPlanarFace face;
                    StatusCode status {};
                    if (!ops_internal::read_boolean_planar_face(*state_,id,linear,
                        options.preparation.intersection.max_edges_per_face,face,status))
                        return failure(status,diag_codes::kBoolRebuildFailure,
                                       "共面边界探针无法读取真实面", "bool.rebuild");
                    // Bound the repeated classification work conservatively:
                    // face/edge reading plus all six independent ray attempts.
                    probe_cost += 16;
                    for (const auto& ring : face.edges) probe_cost += ring.size();
                    probe_faces.push_back(std::move(face));
                }
        }
    }
    std::size_t probe_work = 0;
    for (const auto& fragment : fragments) {
        const auto location = fragment.classification.location;
        const bool left = fragment.source_body == lhs;
        bool keep = op == BooleanOp::Union ? location == BooleanPointLocation::Outside :
            op == BooleanOp::Intersect ? location == BooleanPointLocation::Inside :
            (left ? location == BooleanPointLocation::Outside : location == BooleanPointLocation::Inside);
        bool reverse = op == BooleanOp::Subtract && !left;
        if (location == BooleanPointLocation::Boundary) {
            // The common region is cut by both sets of real polygon boundaries.
            // Its triangles may differ between operands: retain only the left
            // source there, rather than attempting whole-triangle deduplication.
            if (!left) continue;
            const auto a = fragment.vertices[0];
            const auto first = detail::subtract(fragment.vertices[1],a);
            const auto second = detail::subtract(fragment.vertices[2],a);
            const auto normal = detail::normalize(detail::cross(first,second));
            const auto center = detail::add_point_vec(a,detail::scale(
                Vec3 {first.x+second.x,first.y+second.y,first.z+second.z},Scalar(1)/3));
            Scalar clearance = std::numeric_limits<Scalar>::infinity();
            const Scalar resolution = 64 * std::numeric_limits<Scalar>::epsilon() *
                std::max({Scalar(1),std::abs(center.x),std::abs(center.y),std::abs(center.z)});
            for (const auto& face : probe_faces) {
                const auto denominator = detail::dot(face.normal,normal);
                if (std::abs(denominator) <= 64 * std::numeric_limits<Scalar>::epsilon()) continue;
                const auto parameter = detail::dot(face.normal,detail::subtract(face.origin,center))/denominator;
                if (!std::isfinite(parameter))
                    return failure(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,
                                   "共面边界两侧材料探针距离不可分辨", "bool.rebuild");
                if (std::abs(parameter) <= resolution) continue;
                const auto hit = detail::add_point_vec(center,detail::scale(normal,parameter));
                if (ops_internal::boolean_point_in_face(face,hit,linear))
                    clearance = std::min(clearance,std::abs(parameter));
            }
            if (clearance <= 8 * linear)
                return failure(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,
                               "共面边界邻近薄层无法解析两侧材料", "bool.rebuild");
            const auto distance = std::min(32 * linear,clearance/4);
            const std::array<Point3,2> probes {detail::add_point_vec(center,detail::scale(normal,-distance)),
                                             detail::add_point_vec(center,detail::scale(normal,distance))};
            const auto limit = options.preparation.intersection.max_face_pairs;
            if (probe_cost > (limit-probe_work)/2)
                return failure(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                               "共面边界两侧材料查询超过累计工作预算", "bool.rebuild");
            probe_work += 2 * probe_cost;
            const auto own = classify_points(lhs,probes,options.preparation.intersection);
            const auto other = classify_points(rhs,probes,options.preparation.intersection);
            if (own.status != StatusCode::Ok || other.status != StatusCode::Ok)
                return failure(own.status != StatusCode::Ok ? own.status : other.status,
                    diag_codes::kBoolRebuildFailure,"共面区域两侧材料无法确定", "bool.rebuild",
                    own.status != StatusCode::Ok ? own.diagnostic_id : other.diagnostic_id);
            if ((*own.value)[0].location != BooleanPointLocation::Inside ||
                (*own.value)[1].location != BooleanPointLocation::Outside ||
                (*other.value)[0].location == BooleanPointLocation::Boundary ||
                (*other.value)[1].location == BooleanPointLocation::Boundary)
                return failure(StatusCode::NumericalInstability,diag_codes::kBoolNumericalFailure,
                               "共面区域两侧探针未解析真实材料方向", "bool.rebuild");
            const bool minus = truth(true,(*other.value)[0].location == BooleanPointLocation::Inside);
            const bool plus = truth(false,(*other.value)[1].location == BooleanPointLocation::Inside);
            keep = minus != plus;
            reverse = plus;
        }
        if (!keep) continue;
        SelectedFace face;
        face.sources.push_back(fragment.source_face);
        for (const auto source : fragment.classification.boundary_faces)
            detail::append_unique_raw_id(face.sources,source);
        face.triangle = fragment.vertices;
        if (reverse) std::swap(face.triangle[1], face.triangle[2]);
        for (const auto& point : face.triangle)
            coordinate_scale = std::max({coordinate_scale, std::abs(point.x), std::abs(point.y), std::abs(point.z)});
        selected.push_back(std::move(face));
    }
    if (selected.empty()) {
        Issue issue;
        issue.code = std::string(diag_codes::kBoolRebuildCompleted);
        issue.severity = IssueSeverity::Info;
        issue.message = "真实布尔结果为空，不分配替代实体";
        issue.stage = "bool.rebuild";
        issue.related_entities = {lhs.value, rhs.value};
        return ok_result(BooleanRebuildReport{}, state_->create_diagnostic("真实布尔空结果", {std::move(issue)}));
    }

    // Weld only coordinate roundoff, never the user's geometric tolerance.
    // This keeps a narrow gap from silently becoming a topological connection.
    const Scalar weld = 64 * std::numeric_limits<Scalar>::epsilon() * coordinate_scale;
    using Cell = std::array<std::int64_t, 3>;
    std::map<Cell, std::vector<int>> cells;
    std::vector<Point3> points;
    for (auto& face : selected) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto& point = face.triangle[corner];
            const Cell cell {static_cast<std::int64_t>(std::floor(point.x / weld)),
                             static_cast<std::int64_t>(std::floor(point.y / weld)),
                             static_cast<std::int64_t>(std::floor(point.z / weld))};
            int found = -1;
            for (int x = -1; x <= 1; ++x)
                for (int y = -1; y <= 1; ++y)
                    for (int z = -1; z <= 1; ++z) {
                        const auto bucket = cells.find(Cell {cell[0] + x, cell[1] + y, cell[2] + z});
                        if (bucket == cells.end()) continue;
                        for (const auto candidate : bucket->second)
                            if (detail::norm(detail::subtract(points[static_cast<std::size_t>(candidate)], point)) <= weld) {
                                if (found >= 0 && found != candidate)
                                    return failure(StatusCode::NumericalInstability, diag_codes::kBoolNumericalFailure,
                                                   "舍入焊接存在多个不一致候选", "bool.rebuild");
                                found = candidate;
                            }
                    }
            if (found < 0) {
                if (points.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    return failure(StatusCode::OperationFailed, diag_codes::kBoolPreparationBudgetExceeded,
                                   "重建顶点数量超出索引预算", "bool.rebuild");
                found = static_cast<int>(points.size());
                points.push_back(point);
                cells[cell].push_back(found);
            }
            face.corners[corner] = found;
        }
    }

    // Synchronize nodes across both operands before looking up shared edges.
    // Retaining collinear nodes in a planar polygon avoids small fan triangles.
    constexpr std::size_t kMaxNodeTests = 20000000;
    if (selected.size() > kMaxNodeTests / 3 / points.size())
        return failure(StatusCode::OperationFailed, diag_codes::kBoolPreparationBudgetExceeded,
                       "跨来源边节点同步超出两千万次比较预算", "bool.rebuild");
    std::size_t ring_entries = 0;
    for (auto& face : selected) {
        for (std::size_t side = 0; side < 3; ++side) {
            const int begin = face.corners[side], end = face.corners[(side + 1) % 3];
            if (begin == end)
                return failure(StatusCode::DegenerateGeometry, diag_codes::kBoolRebuildFailure,
                               "焊接后分片边退化", "bool.rebuild");
            const auto& a = points[static_cast<std::size_t>(begin)];
            const auto& b = points[static_cast<std::size_t>(end)];
            const std::array<long double, 3> d {static_cast<long double>(b.x) - a.x,
                static_cast<long double>(b.y) - a.y, static_cast<long double>(b.z) - a.z};
            const auto length_squared = d[0]*d[0] + d[1]*d[1] + d[2]*d[2];
            std::vector<std::pair<long double, int>> nodes {{0, begin}};
            for (std::size_t vertex = 0; vertex < points.size(); ++vertex) {
                if (static_cast<int>(vertex) == begin || static_cast<int>(vertex) == end) continue;
                const auto& p = points[vertex];
                const std::array<long double, 3> w {static_cast<long double>(p.x) - a.x,
                    static_cast<long double>(p.y) - a.y, static_cast<long double>(p.z) - a.z};
                const auto fraction = (w[0]*d[0] + w[1]*d[1] + w[2]*d[2]) / length_squared;
                if (!(fraction > 0 && fraction < 1)) continue;
                const auto rx = w[0] - fraction*d[0], ry = w[1] - fraction*d[1], rz = w[2] - fraction*d[2];
                if (rx*rx + ry*ry + rz*rz <= static_cast<long double>(weld)*weld)
                    nodes.emplace_back(fraction, static_cast<int>(vertex));
            }
            std::sort(nodes.begin(), nodes.end());
            for (const auto& [fraction, vertex] : nodes) {
                (void)fraction;
                face.ring.push_back(vertex);
                if ((++ring_entries + 11) / 12 > options.preparation.max_fragments)
                    return failure(StatusCode::OperationFailed, diag_codes::kBoolPreparationBudgetExceeded,
                                   "重建环节点超过分片预算的十二倍", "bool.rebuild");
            }
        }
    }

    struct BoundaryEdge {
        int begin{};
        int end{};
        std::vector<std::pair<std::size_t, bool>> uses;
    };
    const auto error_void_from_failure = [&](StatusCode status, std::string_view code,
                                              std::string message, std::string_view stage) {
        const auto report = failure(status, code, std::move(message), stage);
        return error_void(report.status, report.diagnostic_id);
    };
    std::vector<BoundaryEdge> boundary_edges;
    std::vector<std::vector<std::size_t>> adjacency;
    const auto connect_boundary = [&](std::string_view stage) -> Result<void> {
        boundary_edges.clear();
        adjacency.assign(selected.size(), {});
        std::map<std::pair<int, int>, int> edge_indices;
        for (std::size_t index = 0; index < selected.size(); ++index) {
            auto& face = selected[index];
            face.edge_refs.clear();
            face.inner_edge_refs.assign(face.inner_rings.size(), {});
            for (std::size_t loop = 0; loop <= face.inner_rings.size(); ++loop) {
                const auto& ring = loop == 0 ? face.ring : face.inner_rings[loop - 1];
                auto& refs = loop == 0 ? face.edge_refs : face.inner_edge_refs[loop - 1];
                for (std::size_t side = 0; side < ring.size(); ++side) {
                    const int begin = ring[side], end = ring[(side + 1) % ring.size()];
                    const auto key = std::minmax(begin, end);
                    auto [found, added] = edge_indices.emplace(std::pair<int, int>{key.first, key.second},
                                                               static_cast<int>(boundary_edges.size()));
                    if (added) boundary_edges.push_back(BoundaryEdge {key.first, key.second, {}});
                    const bool reversed = begin != key.first;
                    auto& edge = boundary_edges[static_cast<std::size_t>(found->second)];
                    edge.uses.emplace_back(index, reversed);
                    refs.emplace_back(found->second, reversed);
                }
            }
        }
        for (const auto& edge : boundary_edges) {
            if (edge.uses.size() != 2 || edge.uses[0].first == edge.uses[1].first ||
                edge.uses[0].second == edge.uses[1].second)
                return error_void_from_failure(StatusCode::InvalidTopology, diag_codes::kBoolRebuildFailure,
                               "真实分片选择无法形成每边两侧反向的闭合流形", stage);
            adjacency[edge.uses[0].first].push_back(edge.uses[1].first);
            adjacency[edge.uses[1].first].push_back(edge.uses[0].first);
        }

        // Two-sided edges alone do not certify a manifold vertex: one connected
        // shell may still contain two fans pinched at a welded point. Its incident
        // faces must form one degree-two link through the incident boundary edges.
        struct VertexLink {
            std::array<std::size_t,2> neighbors {};
            std::size_t degree {};
            bool visited {false};
        };
        std::vector<std::map<std::size_t,VertexLink>> vertex_links(points.size());
        std::size_t link_entries = 0;
        for (const auto& edge : boundary_edges) {
            for (const auto vertex : {edge.begin,edge.end}) {
                auto& link = vertex_links[static_cast<std::size_t>(vertex)];
                for (std::size_t side = 0; side < 2; ++side) {
                    auto& node = link[edge.uses[side].first];
                    if (node.degree == node.neighbors.size())
                        return error_void_from_failure(StatusCode::InvalidTopology,diag_codes::kBoolRebuildFailure,
                                       "真实结果顶点面扇具有分叉，不能发布非流形实体", stage);
                    node.neighbors[node.degree++] = edge.uses[1-side].first;
                    // Each ring entry contributes exactly two directed link arcs;
                    // the existing ring_entries cap also bounds all vertex-link work.
                    if (++link_entries > 2 * ring_entries)
                        return error_void_from_failure(StatusCode::OperationFailed,diag_codes::kBoolPreparationBudgetExceeded,
                                       "真实顶点邻接认证超过环节点工作预算", stage);
                }
            }
        }
        for (auto& link : vertex_links) {
            if (link.empty()) continue;
            if (std::any_of(link.begin(),link.end(),[](const auto& node) {
                return node.second.degree != 2;
            }))
                return error_void_from_failure(StatusCode::InvalidTopology,diag_codes::kBoolRebuildFailure,
                               "真实结果顶点面扇未形成闭合环", stage);
            std::vector<std::size_t> pending {link.begin()->first};
            link.begin()->second.visited = true;
            for (std::size_t cursor = 0; cursor < pending.size(); ++cursor)
                for (const auto neighbor : link.at(pending[cursor]).neighbors) {
                    auto& next = link.at(neighbor);
                    if (!next.visited) {
                        next.visited = true;
                        pending.push_back(neighbor);
                    }
                }
            if (pending.size() != link.size())
                return error_void_from_failure(StatusCode::InvalidTopology,diag_codes::kBoolRebuildFailure,
                               "真实结果顶点具有多个不连通面扇，不能发布夹点实体", stage);
        }

        return ok_void();
    };
    const auto connected = connect_boundary("bool.rebuild");
    if (connected.status != StatusCode::Ok)
        return error_result<BooleanRebuildReport>(connected.status, connected.diagnostic_id);

    // Integrate exactly these oriented planar fragments, including negative
    // cavity contributions. Never make each component positive independently.
    std::vector<std::array<int, 3>> triangles;
    triangles.reserve(selected.size());
    for (const auto& face : selected) triangles.push_back(face.corners);
    const Point3 reference = points.front();
    std::vector<Point3> local_points;
    local_points.reserve(points.size());
    for (const auto& point : points)
        local_points.push_back(Point3 {point.x-reference.x, point.y-reference.y, point.z-reference.z});
    Scalar volume{}, area{};
    Point3 centroid{};
    std::array<Scalar, 9> inertia{};
    detail::polyhedral_mass_properties_from_triangles(local_points, triangles, volume, centroid, inertia, area);
    if (!(volume > 0) || !std::isfinite(volume) || !(area > 0) || !std::isfinite(area) ||
        !std::isfinite(centroid.x) || !std::isfinite(centroid.y) || !std::isfinite(centroid.z) ||
        !std::all_of(inertia.begin(), inertia.end(), [](Scalar entry) { return std::isfinite(entry); }))
        return failure(StatusCode::NumericalInstability, diag_codes::kBoolRebuildFailure,
                       "真实边界体积或质量积分退化", "bool.rebuild");

    const std::size_t selected_fragments = selected.size();
    BooleanRebuildRollback rollback {*state_};
    BodyId output {};
    std::vector<FaceId> faces;
    bool did_repair = false;
    DiagnosticId initial_validation {};
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::vector<bool> used_points(points.size(), false);
        for (const auto& edge : boundary_edges) {
            used_points[static_cast<std::size_t>(edge.begin)] = true;
            used_points[static_cast<std::size_t>(edge.end)] = true;
        }
        std::vector<VertexId> vertices(points.size());
        for (std::size_t index = 0; index < points.size(); ++index) {
            if (!used_points[index]) continue;
            const auto& point = points[index];
            const VertexId id {state_->allocate_id()};
            state_->vertices.emplace(id.value, detail::VertexRecord {point});
            vertices[index] = id;
        }
        std::vector<EdgeId> edges;
        edges.reserve(boundary_edges.size());
        for (const auto& boundary : boundary_edges) {
            detail::CurveRecord curve;
            curve.kind = detail::CurveKind::LineSegment;
            curve.origin = points[static_cast<std::size_t>(boundary.begin)];
            curve.direction = detail::subtract(points[static_cast<std::size_t>(boundary.end)], curve.origin);
            curve.param_a = 0;
            curve.param_b = 1;
            curve.poles = {curve.origin, points[static_cast<std::size_t>(boundary.end)]};
            const CurveId curve_id {state_->allocate_id()};
            state_->curves.emplace(curve_id.value, std::move(curve));
            const EdgeId edge_id {state_->allocate_id()};
            state_->edges.emplace(edge_id.value, detail::EdgeRecord {curve_id,
                vertices[static_cast<std::size_t>(boundary.begin)], vertices[static_cast<std::size_t>(boundary.end)], true, 0, 1});
            edges.push_back(edge_id);
        }
        faces.clear();
        faces.reserve(selected.size());
        SurfaceService surface_service {state_};
        for (const auto& face : selected) {
            const auto normal = detail::cross(
                detail::subtract(points[static_cast<std::size_t>(face.corners[1])], points[static_cast<std::size_t>(face.corners[0])]),
                detail::subtract(points[static_cast<std::size_t>(face.corners[2])], points[static_cast<std::size_t>(face.corners[0])]));
            const auto face_id = detail::create_materialized_polygon_face(*state_, edges, face.edge_refs, normal, face.sources);
            faces.push_back(face_id);
            const auto surface_id = state_->faces.at(face_id.value).surface_id;
            std::vector<LoopId> loop_ids {state_->faces.at(face_id.value).outer_loop};
            for (const auto& refs : face.inner_edge_refs) {
                detail::LoopRecord loop;
                for (const auto& [edge_index, reversed] : refs) {
                    const CoedgeId coedge_id {state_->allocate_id()};
                    state_->coedges.emplace(coedge_id.value, detail::CoedgeRecord {edges[static_cast<std::size_t>(edge_index)], reversed});
                    loop.coedges.push_back(coedge_id);
                }
                const LoopId loop_id {state_->allocate_id()};
                state_->loops.emplace(loop_id.value, std::move(loop));
                state_->faces.at(face_id.value).inner_loops.push_back(loop_id);
                loop_ids.push_back(loop_id);
            }
            for (std::size_t loop_index = 0; loop_index < loop_ids.size(); ++loop_index) {
                const auto& ring = loop_index == 0 ? face.ring : face.inner_rings[loop_index - 1];
                std::vector<Point2> uv_points;
                uv_points.reserve(ring.size());
                for (const auto vertex : ring) {
                    const auto uv = surface_service.closest_uv(surface_id, points[static_cast<std::size_t>(vertex)]);
                    if (uv.status != StatusCode::Ok)
                        return failure(uv.status, diag_codes::kBoolRebuildFailure,
                                       "重建平面边界无法投影为真实PCurve",
                                       attempt == 0 ? "bool.rebuild" : "bool.repair", uv.diagnostic_id);
                    uv_points.push_back(Point2 {uv.value->first, uv.value->second});
                }
                const auto& loop = state_->loops.at(loop_ids[loop_index].value);
                for (std::size_t side = 0; side < loop.coedges.size(); ++side) {
                    detail::PCurveRecord pcurve;
                    pcurve.poles = {uv_points[side], uv_points[(side + 1) % uv_points.size()]};
                    // PCurve data follows the canonical edge; public loop traversal
                    // applies the coedge reversal once, just as it does to the 3-D edge.
                    if (state_->coedges.at(loop.coedges[side].value).reversed)
                        std::swap(pcurve.poles[0], pcurve.poles[1]);
                    const PCurveId pcurve_id {state_->allocate_id()};
                    state_->pcurves.emplace(pcurve_id.value, std::move(pcurve));
                    state_->coedges.at(loop.coedges[side].value).pcurve_id = pcurve_id;
                }
            }
        }
        detail::BodyRecord body;
        body.kind = detail::BodyKind::BooleanResult;
        body.boolean_rebuilt_boundary = true;
        body.rep_kind = RepKind::ExactBRep;
        body.label = "boolean_rebuilt";
        detail::append_unique_raw_id(body.source_bodies,lhs);
        detail::append_unique_raw_id(body.source_bodies,rhs);
        body.has_boolean_op = true;
        body.boolean_op = op;
        for (const auto& point : points) detail::extend_materialization_bbox(body.bbox, point);
        std::vector<bool> visited(selected.size(), false);
        for (std::size_t seed = 0; seed < selected.size(); ++seed) {
            if (visited[seed]) continue;
            detail::ShellRecord shell;
            std::vector<std::size_t> pending {seed};
            visited[seed] = true;
            for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
                const auto index = pending[cursor];
                shell.faces.push_back(faces[index]);
                for (const auto source_face : selected[index].sources) {
                    detail::append_unique_raw_id(shell.source_faces, source_face);
                    detail::append_unique_raw_id(body.source_faces, source_face);
                    for (const auto source_body : std::array<BodyId,2>{lhs,rhs})
                        for (const auto source_shell : state_->bodies.at(source_body.value).shells) {
                            const auto& source_faces = state_->shells.at(source_shell.value).faces;
                            if (std::find(source_faces.begin(),source_faces.end(),source_face) != source_faces.end()) {
                                detail::append_unique_raw_id(shell.source_shells,source_shell);
                                detail::append_unique_raw_id(body.source_shells,source_shell);
                            }
                        }
                }
                for (const auto adjacent : adjacency[index])
                    if (!visited[adjacent]) { visited[adjacent] = true; pending.push_back(adjacent); }
            }
            const ShellId shell_id {state_->allocate_id()};
            state_->shells.emplace(shell_id.value, std::move(shell));
            body.shells.push_back(shell_id);
        }
        output = BodyId {state_->allocate_id()};
        state_->bodies.emplace(output.value, std::move(body));
        detail::rebuild_topology_links(*state_);
        ValidationService validation {state_};
        auto validated = validation.validate_all(output, ValidationMode::Strict);
        if (validated.status == StatusCode::Ok) {
            // Strict permits triangles sharing a vertex. Distinct output shells
            // must additionally have an unambiguous, non-contacting arrangement so
            // the successful solid supports the public material/section queries.
            TopologyQueryService query {state_};
            const auto regions = query.body_shell_regions(output);
            if (regions.status != StatusCode::Ok)
                validated = error_void(regions.status, regions.diagnostic_id);
        }
        if (validated.status != StatusCode::Ok) {
            if (!options.auto_repair)
                return failure(validated.status, diag_codes::kBoolRebuildFailure,
                               "重建结果未通过Strict验证", "bool.validate", validated.diagnostic_id);
            if (attempt != 0)
                return failure(validated.status, diag_codes::kBoolRebuildFailure,
                               "真实边界保持Safe修复后仍未通过Strict与壳材料验证",
                               "bool.repair", validated.diagnostic_id);
            initial_validation = validated.diagnostic_id;
            // Only tessellation topology defects are candidates for this Safe path.
            // A failed material arrangement, domain or self-intersection check
            // is not made repairable by changing the representation of planar faces.
            // A trim failure qualifies only with the validator's explicit
            // nonzero, below-threshold loop-area evidence, never a generic trim error.
            const auto diagnostic = state_->diagnostics.find(validated.diagnostic_id.value);
            const bool repairable = diagnostic != state_->diagnostics.end() &&
                std::any_of(diagnostic->second.issues.begin(), diagnostic->second.issues.end(),
                    [](const Issue& issue) { return issue.severity == IssueSeverity::Error; }) &&
                std::all_of(diagnostic->second.issues.begin(), diagnostic->second.issues.end(),
                    [](const Issue& issue) {
                        if (issue.severity == IssueSeverity::Fatal) return false;
                        if (issue.severity != IssueSeverity::Error) return true;
                        if (issue.stage == "heal.validate_geometry.near_duplicate_vertices" ||
                            issue.stage == "heal.validate_geometry.edges" ||
                            issue.stage == "heal.validate_geometry.face_area") return true;
                        if (issue.stage != "heal.validate_topology.trim_consistency" ||
                            (issue.code != diag_codes::kTopoFaceOuterLoopInvalid &&
                             issue.code != diag_codes::kTopoFaceInnerLoopInvalid)) return false;
                        Scalar loop_area = std::numeric_limits<Scalar>::quiet_NaN();
                        Scalar threshold = std::numeric_limits<Scalar>::quiet_NaN();
                        for (const auto& evidence : issue.numeric_evidence) {
                            if (evidence.name == "uv_loop_area") loop_area = evidence.value;
                            if (evidence.name == "uv_loop_area_threshold") threshold = evidence.value;
                        }
                        return std::isfinite(loop_area) && std::isfinite(threshold) &&
                            threshold > 0 && std::abs(loop_area) > 0 && std::abs(loop_area) <= threshold;
                    });
            if (!repairable)
                return failure(StatusCode::NotImplemented, diag_codes::kBoolRebuildFailure,
                               "Safe只认证共面内部分片与人工共线节点修复",
                               "bool.repair", validated.diagnostic_id);

            // Build maximal connected, equally oriented regions against a fixed
            // seed plane. Comparing each candidate to that seed prevents an epsilon
            // chain of slightly tilted planes from being flattened transitively.
            std::vector<int> groups(selected.size(), -1);
            std::vector<SelectedFace> merged;
            std::size_t repair_work = 0;
            for (std::size_t seed = 0; seed < selected.size(); ++seed) {
                if (groups[seed] >= 0) continue;
                const auto& source = selected[seed];
                const auto origin = source.triangle[0];
                const auto normal = detail::normalize(detail::cross(
                    detail::subtract(source.triangle[1], origin),
                    detail::subtract(source.triangle[2], origin)));
                const int group = static_cast<int>(merged.size());
                std::vector<std::size_t> members {seed};
                groups[seed] = group;
                for (std::size_t cursor = 0; cursor < members.size(); ++cursor) {
                    for (const auto next : adjacency[members[cursor]]) {
                        if (++repair_work > kMaxNodeTests)
                            return failure(StatusCode::OperationFailed, diag_codes::kBoolPreparationBudgetExceeded,
                                           "Safe共面分组超过累计工作预算", "bool.repair", initial_validation);
                        if (groups[next] >= 0) continue;
                        const auto& candidate = selected[next];
                        const auto candidate_normal = detail::normalize(detail::cross(
                            detail::subtract(candidate.triangle[1], candidate.triangle[0]),
                            detail::subtract(candidate.triangle[2], candidate.triangle[0])));
                        if (detail::dot(normal, candidate_normal) <= 0 ||
                            detail::norm(detail::cross(normal, candidate_normal)) >
                                256 * std::numeric_limits<Scalar>::epsilon()) continue;
                        if (std::any_of(candidate.ring.begin(), candidate.ring.end(), [&](int vertex) {
                            return std::abs(detail::dot(normal,
                                detail::subtract(points[static_cast<std::size_t>(vertex)], origin))) > weld;
                        })) continue;
                        groups[next] = group;
                        members.push_back(next);
                    }
                }
                SelectedFace face;
                face.triangle = source.triangle;
                face.corners = source.corners;
                std::map<int, int> successors;
                std::map<int, int> predecessors;
                std::size_t boundary_count = 0;
                Vec3 original_area {};
                for (const auto member : members) {
                    for (const auto id : selected[member].sources)
                        detail::append_unique_raw_id(face.sources, id);
                    const auto& ring = selected[member].ring;
                    for (std::size_t side = 0; side < ring.size(); ++side) {
                        const int begin = ring[side], end = ring[(side + 1) % ring.size()];
                        const auto a = detail::subtract(points[static_cast<std::size_t>(begin)], origin);
                        const auto b = detail::subtract(points[static_cast<std::size_t>(end)], origin);
                        const auto contribution = detail::cross(a, b);
                        original_area.x += contribution.x;
                        original_area.y += contribution.y;
                        original_area.z += contribution.z;
                        const auto& edge = boundary_edges[static_cast<std::size_t>(selected[member].edge_refs[side].first)];
                        const auto other = edge.uses[0].first == member ? edge.uses[1].first : edge.uses[0].first;
                        // Every omitted seam has exactly two opposite uses already
                        // certified by connect_boundary. All other directed edges
                        // survive once: the true outer and hole boundaries persist.
                        if (groups[other] == group) continue;
                        if (!successors.emplace(begin, end).second || !predecessors.emplace(end, begin).second)
                            return failure(StatusCode::InvalidTopology, diag_codes::kBoolRebuildFailure,
                                           "Safe共面区域边界具有分叉或接触孔，拒绝并面", "bool.repair", initial_validation);
                        ++boundary_count;
                    }
                }
                if (successors.size() != predecessors.size() || successors.empty())
                    return failure(StatusCode::InvalidTopology, diag_codes::kBoolRebuildFailure,
                                   "Safe共面区域没有可认证闭合边界", "bool.repair", initial_validation);
                Vec3 rebuilt_area {};
                std::size_t consumed = 0;
                bool has_outer = false;
                while (!successors.empty()) {
                    std::vector<int> ring;
                    const int first = successors.begin()->first;
                    int current = first;
                    do {
                        const auto next = successors.find(current);
                        if (next == successors.end())
                            return failure(StatusCode::InvalidTopology, diag_codes::kBoolRebuildFailure,
                                           "Safe区域环无法连续闭合", "bool.repair", initial_validation);
                        ring.push_back(current);
                        current = next->second;
                        successors.erase(next);
                        ++consumed;
                    } while (current != first);
                    if (ring.size() < 3)
                        return failure(StatusCode::DegenerateGeometry, diag_codes::kBoolRebuildFailure,
                                       "Safe区域环退化", "bool.repair", initial_validation);
                    Vec3 ring_area {};
                    for (std::size_t side = 0; side < ring.size(); ++side) {
                        const auto a = detail::subtract(points[static_cast<std::size_t>(ring[side])], origin);
                        const auto b = detail::subtract(points[static_cast<std::size_t>(ring[(side + 1) % ring.size()])], origin);
                        const auto contribution = detail::cross(a, b);
                        ring_area.x += contribution.x;
                        ring_area.y += contribution.y;
                        ring_area.z += contribution.z;
                    }
                    const Scalar orientation = detail::dot(normal, ring_area);
                    if (!std::isfinite(orientation) || orientation == 0)
                        return failure(StatusCode::NumericalInstability, diag_codes::kBoolRebuildFailure,
                                       "Safe外孔环方向不可解析", "bool.repair", initial_validation);
                    if (orientation > 0) {
                        if (has_outer)
                            return failure(StatusCode::InvalidTopology, diag_codes::kBoolRebuildFailure,
                                           "Safe连通区域含多个外环，拒绝不确定并面", "bool.repair", initial_validation);
                        face.ring = std::move(ring);
                        has_outer = true;
                    } else face.inner_rings.push_back(std::move(ring));
                    rebuilt_area.x += ring_area.x;
                    rebuilt_area.y += ring_area.y;
                    rebuilt_area.z += ring_area.z;
                }
                if (!has_outer || consumed != boundary_count ||
                    detail::norm(Vec3 {original_area.x-rebuilt_area.x, original_area.y-rebuilt_area.y,
                                       original_area.z-rebuilt_area.z}) >
                        8 * weld * coordinate_scale * static_cast<Scalar>(ring_entries))
                    return failure(StatusCode::NumericalInstability, diag_codes::kBoolRebuildFailure,
                                   "Safe有向边抵消与区域面积向量不一致", "bool.repair", initial_validation);
                merged.push_back(std::move(face));
            }

            // Remove an artificial collinear node only when *every* incident loop
            // agrees on its two neighbors. Endpoints stay fixed, the point lies
            // strictly between them, and roundoff-level deviation is the only
            // accepted deviation. No user tolerance can erase a real corner.
            std::vector<std::vector<int>*> rings;
            for (auto& face : merged) {
                rings.push_back(&face.ring);
                for (auto& ring : face.inner_rings) rings.push_back(&ring);
            }
            std::vector<std::vector<std::size_t>> incident(points.size());
            for (std::size_t loop = 0; loop < rings.size(); ++loop)
                for (const auto vertex : *rings[loop]) incident[static_cast<std::size_t>(vertex)].push_back(loop);
            std::size_t removed_nodes = 0;
            for (std::size_t vertex = 0; vertex < points.size(); ++vertex) {
                if (incident[vertex].empty()) continue;
                std::pair<int,int> neighbors {-1,-1};
                bool removable = true;
                std::vector<std::pair<std::size_t,std::size_t>> positions;
                for (const auto loop : incident[vertex]) {
                    auto& ring = *rings[loop];
                    repair_work += ring.size();
                    if (repair_work > kMaxNodeTests)
                        return failure(StatusCode::OperationFailed, diag_codes::kBoolPreparationBudgetExceeded,
                                       "Safe共边节点同步超过累计工作预算", "bool.repair", initial_validation);
                    const auto found = std::find(ring.begin(), ring.end(), static_cast<int>(vertex));
                    if (found == ring.end()) continue;
                    if (ring.size() <= 3) { removable = false; break; }
                    const auto index = static_cast<std::size_t>(found-ring.begin());
                    const int before = ring[(index+ring.size()-1)%ring.size()];
                    const int after = ring[(index+1)%ring.size()];
                    const auto key = std::minmax(before, after);
                    const std::pair<int,int> pair {key.first,key.second};
                    if (neighbors.first >= 0 && neighbors != pair) { removable = false; break; }
                    neighbors = pair;
                    positions.emplace_back(loop,index);
                }
                if (!removable || positions.empty()) continue;
                const auto& a = points[static_cast<std::size_t>(neighbors.first)];
                const auto& b = points[static_cast<std::size_t>(neighbors.second)];
                const std::array<long double,3> d {static_cast<long double>(b.x)-a.x,
                    static_cast<long double>(b.y)-a.y, static_cast<long double>(b.z)-a.z};
                const auto& p = points[vertex];
                const std::array<long double,3> w {static_cast<long double>(p.x)-a.x,
                    static_cast<long double>(p.y)-a.y, static_cast<long double>(p.z)-a.z};
                const auto length_squared = d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
                if (!(length_squared > 0)) continue;
                const auto fraction = (w[0]*d[0]+w[1]*d[1]+w[2]*d[2])/length_squared;
                if (!(fraction > 0 && fraction < 1)) continue;
                const auto rx = w[0]-fraction*d[0], ry = w[1]-fraction*d[1], rz = w[2]-fraction*d[2];
                if (rx*rx+ry*ry+rz*rz > static_cast<long double>(weld)*weld) continue;
                for (const auto& [loop,index] : positions)
                    rings[loop]->erase(rings[loop]->begin()+static_cast<std::ptrdiff_t>(index));
                ++removed_nodes;
            }
            if (merged.size() == selected.size() && removed_nodes == 0)
                return failure(StatusCode::NotImplemented, diag_codes::kBoolRebuildFailure,
                               "Safe没有可移除的共面内边或人工共线节点", "bool.repair", initial_validation);
            selected = std::move(merged);
            const auto repaired_boundary = connect_boundary("bool.repair");
            if (repaired_boundary.status != StatusCode::Ok)
                return error_result<BooleanRebuildReport>(repaired_boundary.status, repaired_boundary.diagnostic_id);
            // Remove the rejected materialization and all of its query caches before
            // allocating the repaired boundary. The writer sees one allocation range
            // only after the second Strict + shell-region certification succeeds.
            rollback.restore();
            did_repair = true;
            continue;
        }
        break;
    }
    if (!state_->active_topology_transaction.expired())
        state_->topology_service_allocation_ranges.emplace_back(rollback.first_id(), state_->next_id);
    rollback.commit();
    Issue issue;
    issue.code = std::string(diag_codes::kBoolRebuildCompleted);
    issue.severity = IssueSeverity::Info;
    issue.message = "真实平面布尔实体已重建并通过Strict验证";
    issue.stage = "bool.rebuild";
    issue.related_entities = {lhs.value, rhs.value, output.value};
    issue.numeric_evidence = {{"selected_fragments", static_cast<Scalar>(selected_fragments), "count"},
                              {"output_faces", static_cast<Scalar>(faces.size()), "count"},
                              {"volume", volume, "model_unit^3"}, {"surface_area", area, "model_unit^2"}};
    std::vector<Issue> issues;
    for (const auto& [code, stage] : std::array<std::pair<std::string_view, std::string_view>, 4>{
             std::pair{diag_codes::kBoolStageSplit, std::string_view{"bool.split"}},
             std::pair{diag_codes::kBoolStageClassify, std::string_view{"bool.classify"}},
             std::pair{diag_codes::kBoolStageRebuild, std::string_view{"bool.rebuild"}},
             std::pair{diag_codes::kBoolStageValidate, std::string_view{"bool.validate"}}}) {
        Issue trace;
        trace.code = std::string(code);
        trace.severity = IssueSeverity::Info;
        trace.stage = std::string(stage);
        trace.message = stage == "bool.validate" ? "真实重建结果Strict验证通过" : "真实布尔阶段完成";
        trace.related_entities = {lhs.value, rhs.value, output.value};
        trace.numeric_evidence = {{"source_fragments", static_cast<Scalar>(fragments.size()), "count"},
                                  {"selected_fragments", static_cast<Scalar>(selected_fragments), "count"}};
        issues.push_back(std::move(trace));
    }
    if (did_repair) {
        Issue repaired;
        repaired.code = std::string(diag_codes::kBoolStageRepair);
        repaired.severity = IssueSeverity::Info;
        repaired.stage = "bool.repair";
        repaired.message = "Safe消除共面内部边及同步人工共线节点，真实外孔边界保持并通过Strict与壳材料验证";
        repaired.related_entities = {lhs.value, rhs.value, output.value};
        repaired.numeric_evidence = {{"input_fragments", static_cast<Scalar>(selected_fragments), "count"},
                                    {"output_faces", static_cast<Scalar>(faces.size()), "count"},
                                    {"boundary_preserved", 1.0, "bool"},
                                    {"initial_validation_diagnostic", static_cast<Scalar>(initial_validation.value), "id"}};
        issues.push_back(std::move(repaired));
    }
    issues.push_back(std::move(issue));
    return ok_result(BooleanRebuildReport {output, selected_fragments, faces.size(), did_repair},
        state_->create_diagnostic("真实布尔重建完成", std::move(issues)));
}

}  // namespace axiom
